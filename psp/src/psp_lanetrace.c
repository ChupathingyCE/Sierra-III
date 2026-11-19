/* psp_lanetrace.c: "lanetrace" (keep.txt), the main CPU's, the GE's and the
Media Engine's work placed on one clock, frame by frame.

Why. frametrace splits the main CPU per frame, and gebusy2 and mekinds
average the GE and the ME over 300 frames. Nothing put the three lanes on one
timeline per frame, so the time map's swimlane (its design note,
item A3) placed the GE and ME blocks by
estimate.

What. While a window records, each of these is noted with the main CPU's
microsecond clock (sceKernelGetSystemTimeLow), so every time is on one clock:
  c  the main CPU's phase changes (psp_phase: 0 other, 1 tick, 2 drawing,
     3 frame end);
  g  the GE's busy spans, as gebusy and gebusy2 close them (their SIGNAL and
     FINISH interrupts): start, end and the draws' kind; spans less than
     0.1 ms apart joined (kind 127: of several kinds);
  m  a Media Engine slot job (audio, the replay of the frame, the eDRAM
     probe): when the main CPU handed it over, when it saw it done, and the
     ME's own time for it (its cycle counter, as psp_me.c converts it);
  r  a ring job (perception's rays, poses, points): pushed, collected, and
     the ME's own time.
The ME reads no clock of its own here: its spans are the main CPU's hand-over
and collection, with the ME's own time inside them [E] (nothing in psp_me.c's
ME code changes). With the emulator the ME's jobs run on the main CPU and the
GE isn't timed: the lanes are the PSP's.

A window opens at each phase edge (phasemark's: a map loaded, a cutscene
begun or ended, a BSP switch) and records "lanetraceN" frames (default 60)
or until its 1,536 notes are used; then it prints one line a frame:
  lanes F +W: c1@T c2@T ... | g T-T/K ... | m S.J T-T o U ... | r J T-T o U ...
F the frame, W its length in us, every T in us from the last frame's end
(negative: before it, a job handed over in the frame before).
tools: a lanes reader script reads them.

"lanetraceatN" opens one window at game tick N instead (as flashlightatN
counts ticks), and the phase edges open none: phase edges fall where a map
changes, so a steady stretch well inside one (a long fight, a heavy view)
had no window. It turns lanetrace on by itself.

Measurement only: nothing the game does changes. The notes (18 KB) come from
the heap when the word is first seen; without it every hook is one test of a
word. Compiled with PSP_METRICS, so a final release has none of it. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pspkernel.h>

#include "psp_lanetrace.h"

#ifdef PSP_METRICS

int psp_keep(const char *name);
int psp_keep_number(const char *prefix, int fallback);
void platform_log(const char *format, ...);

#define LANE_NOTES 1536
#define LANE_LINE 480
#define LANE_GE_JOIN_US 100
#define LANE_GE_MIXED 0x7F /* a joined span's kinds differed */

/* 12 bytes: the end, the start, and kind (4 bits), b (12) and c (16) */
struct lane_note
{
	unsigned int t;    /* the end (a phase change: its moment) */
	unsigned int from; /* a span's start; a frame's number */
	unsigned int packed;
};
#define LANE_KIND(n) ((n)->packed >> 28)
#define LANE_B(n) (((n)->packed >> 16) & 0xFFF) /* g: the kind; m: slot << 8 | type; r: type; a frame: its ticks */
#define LANE_C(n) ((n)->packed & 0xFFFF)        /* m, r: the ME's own time, us (to 65 ms); c: the phase */

volatile int psp_lane_on = 0;          /* 1 while a window records, 2 while it prints */
static int lane_word = -1;              /* -1: the keep word not read yet */
static int lane_frames_wanted = 60;
static long lane_at = -1;               /* "lanetraceatN": the one window's game tick, else -1 */
/* "lanekinds": a GE span joins the last only when their kinds match, so each
span keeps its kind (what the GE is drawing while a tick runs: getick.py in
the lab). Windows fill sooner, in fewer frames */
static int lane_kinds = 0;
static struct lane_note *lane_notes;
static volatile unsigned int lane_count;
static unsigned int lane_frames, lane_window_start, lane_last_end;
static unsigned int lane_windows;

#ifdef PSP_FLIGHTREC
/* "flightrec" (psp_flightrec.c): while it records, the notes are kept every
frame and handed to it at the frame's end instead of opening windows here.
Every note made is counted, so the recorder can charge their cost */
volatile unsigned int psp_lane_calls = 0;
static int lane_flightrec = 0;
void psp_flightrec_lanes(const void *notes, unsigned int count);

int psp_lane_flightrec_start(void)
{
	if (!lane_notes)
		lane_notes = (struct lane_note *)malloc(LANE_NOTES * sizeof(*lane_notes));
	if (!lane_notes)
		return 0;
	lane_word = 1;
	lane_at = -1;
	lane_flightrec = 1;
	lane_count = 0;
	psp_lane_on = 1;
	return 1;
}
#endif

static int lane_ready(void)
{
#ifdef PSP_FLIGHTREC
	if (lane_flightrec)
		return 0; /* the recorder has the notes: no windows of lanetrace's own */
#endif
	if (lane_word < 0)
	{
		int n = psp_keep_number("lanetrace", 0);

		lane_at = psp_keep_number("lanetraceat", -1);
		lane_word = psp_keep("lanetrace") != 0 || n > 0 || lane_at >= 0;
		lane_kinds = psp_keep("lanekinds") != 0;
		if (lane_word)
		{
			lane_frames_wanted = n > 0 ? n : 60;
			lane_notes = (struct lane_note *)malloc(LANE_NOTES * sizeof(*lane_notes));
			if (!lane_notes)
			{
				platform_log("lanetrace: no room for its %u KB; off", (unsigned)(LANE_NOTES * sizeof(*lane_notes) / 1024));
				lane_word = 0;
			}
			else if (lane_at >= 0)
				platform_log("lanetrace on: %d frames from game tick %ld (lanetraceat; no windows at phase edges)",
					lane_frames_wanted, lane_at);
			else
				platform_log("lanetrace on: %d frames after each phase edge (the main CPU's phases, the GE's busy spans with gebusy or gebusy2, the ME's jobs)",
					lane_frames_wanted);
		}
	}
	return lane_word;
}

/* from any thread, and from the GE's interrupts: one slot taken with the
interrupts held off, so two notes never share it */
void psp_lane_note(unsigned int kind, unsigned int from, unsigned int t, unsigned int b, unsigned int c)
{
	unsigned int flags, index;

	if (psp_lane_on != 1)
		return;
	flags = sceKernelCpuSuspendIntr();
#ifdef PSP_FLIGHTREC
	psp_lane_calls++;
#endif
	index = lane_count;
	/* a GE span that starts within LANE_GE_JOIN_US of the last note's end,
	that note a GE span too, extends it (kind 127 when their kinds differ:
	gebusy2's line has the kinds' totals): the GE closes 60-150
	spans a frame (gebusy2's chunks), which filled a window's notes in 10-22
	frames on the PSP; joined, ~5-8 a frame */
	if (kind == PSP_LANE_GE && index > 0 && index <= LANE_NOTES &&
		LANE_KIND(&lane_notes[index - 1]) == PSP_LANE_GE &&
		(int)(from - lane_notes[index - 1].t) <= LANE_GE_JOIN_US && (int)(t - lane_notes[index - 1].t) > 0 &&
		(!lane_kinds || LANE_B(&lane_notes[index - 1]) == (b & 0xFFF)))
	{
		lane_notes[index - 1].t = t;
		if (LANE_B(&lane_notes[index - 1]) != (b & 0xFFF))
			lane_notes[index - 1].packed |= (unsigned int)LANE_GE_MIXED << 16;
		sceKernelCpuResumeIntr(flags);
		return;
	}
	if (index < LANE_NOTES)
	{
		/* written before the interrupts come back, so a GE span joining the
		last note never sees a slot taken and not yet filled */
		lane_count = index + 1;
		lane_notes[index].from = from;
		lane_notes[index].t = t;
		lane_notes[index].packed = kind << 28 | (b & 0xFFF) << 16 | (c > 0xFFFF ? 0xFFFF : c);
	}
	sceKernelCpuResumeIntr(flags);
}

static void lane_open(void)
{
	lane_count = 0;
	lane_frames = 0;
	lane_window_start = lane_last_end = sceKernelGetSystemTimeLow();
	lane_windows++;
	psp_lane_on = 1;
}

/* a phase edge (psp_platform.c's phase_mark): a window opens unless one is
recording, or lanetraceatN has chosen the window's moment */
void psp_lane_arm(void)
{
	if (lane_ready() <= 0 || psp_lane_on || lane_at >= 0)
		return;
	lane_open();
}

/* each game tick while "lanetraceatN" waits (psp_platform.c's tick hook):
the window opens at tick N */
int psp_lane_at(unsigned long tick)
{
	if (lane_ready() <= 0 || lane_at < 0)
		return 0;
	if ((long)tick != lane_at)
		return (long)tick < lane_at;
	if (!psp_lane_on)
	{
		platform_log("lanetrace: window %u opens at game tick %lu (lanetraceat)", lane_windows + 1, tick);
		lane_open();
	}
	return 0;
}

static int lane_add(char *line, int at, const char *format, int x, int y, int z, int w)
{
	int n = snprintf(line + at, LANE_LINE - at, format, x, y, z, w);

	return n > 0 && at + n < LANE_LINE ? at + n : at;
}

/* the window's frames, one line each (several when a frame's notes don't fit
one), LANE_PRINT_FRAMES frames at each frame's end after the window: all at
once they overran the log's batch (tmlane2: its lines dropped). Notes are
taken in the order they were made, each into the frame whose end follows it */
#define LANE_PRINT_FRAMES 4
static unsigned int print_k, print_frame_start, print_frames;

static void lane_print_some(void)
{
	unsigned int count = lane_count, done = 0;
	char line[LANE_LINE];

	while (print_k < count && done < LANE_PRINT_FRAMES)
	{
		unsigned int end_note = print_k, frame_end, at, j, first = 1;

		while (end_note < count && LANE_KIND(&lane_notes[end_note]) != PSP_LANE_FRAME)
			end_note++;
		frame_end = end_note < count ? lane_notes[end_note].t : lane_notes[count - 1].t;
		for (j = print_k; j < end_note;)
		{
			at = snprintf(line, sizeof(line), "lanes %u%s +%u:", lane_notes[end_note < count ? end_note : count - 1].from,
				first ? "" : " (more)", frame_end - print_frame_start);
			first = 0;
			for (; j < end_note && at < LANE_LINE - 48; j++)
			{
				struct lane_note const *n = &lane_notes[j];
				int from = (int)(n->from - print_frame_start), t = (int)(n->t - print_frame_start);

				switch (LANE_KIND(n))
				{
				case PSP_LANE_PHASE:
					at = lane_add(line, at, " c%d@%d", (int)LANE_C(n), t, 0, 0);
					break;
				case PSP_LANE_GE:
					at = lane_add(line, at, " g%d-%d/%d", from, t, (int)LANE_B(n), 0);
					break;
				case PSP_LANE_ME_SLOT:
					at = lane_add(line, at, " m%d.%d", (int)(LANE_B(n) >> 8), (int)(LANE_B(n) & 0xFF), 0, 0);
					at = lane_add(line, at, ":%d-%d/o%d", from, t, (int)LANE_C(n), 0);
					break;
				case PSP_LANE_ME_RING:
					at = lane_add(line, at, " r%d:%d-%d/o%d", (int)LANE_B(n), from, t, (int)LANE_C(n));
					break;
				}
			}
			platform_log("%s", line);
		}
		print_frames++;
		done++;
		print_frame_start = frame_end;
		print_k = end_note + 1;
	}
	if (print_k >= count)
	{
		platform_log("lanetrace: window %u, %u frames, %u notes%s", lane_windows, print_frames, count,
			count >= LANE_NOTES ? " (full: the window ended early)" : "");
		psp_lane_on = 0;
	}
}

/* a frame has ended (psp_platform.c's psp_frame_account, after the GE's
frame end): the window closes after its frames, or when its notes run out,
and its lines go out a few frames at a time (psp_lane_on 2) */
void psp_lane_frame(unsigned long frame_number, unsigned int ticks)
{
	unsigned int now;

#ifdef PSP_FLIGHTREC
	if (lane_flightrec)
	{
		unsigned int flags = sceKernelCpuSuspendIntr();

		(void)frame_number;
		(void)ticks;
		psp_flightrec_lanes(lane_notes, lane_count < LANE_NOTES ? lane_count : LANE_NOTES);
		lane_count = 0;
		sceKernelCpuResumeIntr(flags);
		return;
	}
#endif
	if (psp_lane_on == 2)
	{
		lane_print_some();
		return;
	}
	if (psp_lane_on != 1)
		return;
	now = sceKernelGetSystemTimeLow();
	psp_lane_note(PSP_LANE_FRAME, (unsigned int)frame_number, now, ticks, 0);
	lane_last_end = now;
	if (++lane_frames >= (unsigned int)lane_frames_wanted || lane_count >= LANE_NOTES)
	{
		psp_lane_on = 2;
		print_k = 0;
		print_frames = 0;
		print_frame_start = lane_window_start;
	}
}

#else

/* a final release (PSP_METRICS=0): the hooks' tests stay (psp_lanetrace.h),
so these answer them and record nothing (the header names them *_off) */
volatile int psp_lane_on = 0;

void psp_lane_note(unsigned int kind, unsigned int from, unsigned int t, unsigned int b, unsigned int c)
{
	(void)kind; (void)from; (void)t; (void)b; (void)c;
}

void psp_lane_arm(void)
{
}

int psp_lane_at(unsigned long tick)
{
	(void)tick;
	return 0;
}

void psp_lane_frame(unsigned long frame_number, unsigned int ticks)
{
	(void)frame_number; (void)ticks;
}

#endif
