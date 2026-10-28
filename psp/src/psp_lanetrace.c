/* psp_lanetrace.c: "lanetrace" (keep.txt), the main CPU's, the GE's and the
Media Engine's work placed on one clock, frame by frame.

Why. frametrace splits the main CPU per frame, and gebusy2 and mekinds
average the GE and the ME over 300 frames. Nothing put the three lanes on one
timeline per frame, so the time map's swimlane (the lab's
psp/sierra/timemap/recon_build.md, A3) placed the GE and ME blocks by
estimate.

What. While a window records, each of these is noted with the main CPU's
microsecond clock (sceKernelGetSystemTimeLow), so every time is on one clock:
  c  the main CPU's phase changes (psp_phase: 0 other, 1 tick, 2 drawing,
     3 frame end);
  g  the GE's busy spans, as gebusy and gebusy2 close them (their SIGNAL and
     FINISH interrupts): start, end and the draws' kind;
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
tools: the lab's psp/sierra/timemap/lanes.py reads them.

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

volatile int psp_lane_on = 0;          /* 1 while a window records */
static int lane_word = -1;              /* -1: the keep word not read yet */
static int lane_frames_wanted = 60;
static struct lane_note *lane_notes;
static volatile unsigned int lane_count;
static unsigned int lane_frames, lane_window_start, lane_last_end;
static unsigned int lane_windows;

static int lane_ready(void)
{
	if (lane_word < 0)
	{
		int n = psp_keep_number("lanetrace", 0);

		lane_word = psp_keep("lanetrace") != 0 || n > 0;
		if (lane_word)
		{
			lane_frames_wanted = n > 0 ? n : 60;
			lane_notes = (struct lane_note *)malloc(LANE_NOTES * sizeof(*lane_notes));
			if (!lane_notes)
			{
				platform_log("lanetrace: no room for its %u KB; off", (unsigned)(LANE_NOTES * sizeof(*lane_notes) / 1024));
				lane_word = 0;
			}
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

	if (!psp_lane_on)
		return;
	flags = sceKernelCpuSuspendIntr();
	index = lane_count;
	if (index < LANE_NOTES)
		lane_count = index + 1;
	sceKernelCpuResumeIntr(flags);
	if (index >= LANE_NOTES)
		return;
	lane_notes[index].from = from;
	lane_notes[index].t = t;
	lane_notes[index].packed = kind << 28 | (b & 0xFFF) << 16 | (c > 0xFFFF ? 0xFFFF : c);
}

/* a phase edge (psp_platform.c's phase_mark): a window opens unless one is
recording */
void psp_lane_arm(void)
{
	if (lane_ready() <= 0 || psp_lane_on)
		return;
	lane_count = 0;
	lane_frames = 0;
	lane_window_start = lane_last_end = sceKernelGetSystemTimeLow();
	lane_windows++;
	psp_lane_on = 1;
}

static int lane_add(char *line, int at, const char *format, int x, int y, int z, int w)
{
	int n = snprintf(line + at, LANE_LINE - at, format, x, y, z, w);

	return n > 0 && at + n < LANE_LINE ? at + n : at;
}

/* the window's frames, one line each (several when a frame's notes don't fit
one): notes are taken in the order they were made, each into the frame whose
end follows it */
static void lane_print(void)
{
	unsigned int k = 0, frame_start = lane_window_start, frame = 0, count = lane_count;
	char line[LANE_LINE];

	while (k < count)
	{
		unsigned int end_note = k, frame_end, at, j, first = 1;

		while (end_note < count && LANE_KIND(&lane_notes[end_note]) != PSP_LANE_FRAME)
			end_note++;
		frame_end = end_note < count ? lane_notes[end_note].t : lane_notes[count - 1].t;
		for (j = k; j < end_note;)
		{
			at = snprintf(line, sizeof(line), "lanes %u%s +%u:", lane_notes[end_note < count ? end_note : count - 1].from,
				first ? "" : " (more)", frame_end - frame_start);
			first = 0;
			for (; j < end_note && at < LANE_LINE - 48; j++)
			{
				struct lane_note const *n = &lane_notes[j];

				int from = (int)(n->from - frame_start), t = (int)(n->t - frame_start);

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
		frame++;
		frame_start = frame_end;
		k = end_note + 1;
	}
	platform_log("lanetrace: window %u, %u frames, %u notes%s", lane_windows, frame, count,
		count >= LANE_NOTES ? " (full: the window ended early)" : "");
}

/* a frame has ended (psp_platform.c's psp_frame_account, after the GE's
frame end): the window closes after its frames, or when its notes run out */
void psp_lane_frame(unsigned long frame_number, unsigned int ticks)
{
	unsigned int now;

	if (!psp_lane_on)
		return;
	now = sceKernelGetSystemTimeLow();
	psp_lane_note(PSP_LANE_FRAME, (unsigned int)frame_number, now, ticks, 0);
	lane_last_end = now;
	if (++lane_frames >= (unsigned int)lane_frames_wanted || lane_count >= LANE_NOTES)
	{
		psp_lane_on = 0;
		lane_print();
	}
}

#endif
