/* psp_flightrec.c: the flight recorder (build switch PSP_FLIGHTREC=1, keep
word "flightrec"; docs/flightrec.md; the lab's coord/briefs/flightrec.md).

Why. The heartbeat, the step profile, hwprofile, lanetrace and the censuses
each answer one question over their own window (5 s, 10 s, 60 frames after a
phase edge), so what the game was doing when the cost moved had to be pieced
together from several logs. The recorder keeps them on one time axis: every
2 s of game time (60 ticks) one record holds the engine's cost as the step
profile's rows split by the main CPU's phase, the CPU's counters, the game's
events and counts, every frame with its job spans, and the instruments' own
cost, so a reader can draw events in lanes over a heatmap tree and play the
frames back.

What it reuses rather than repeats:
  - the step profile (psp_platform.c, psp_step): the time from one step to
    the next, charged to the first; the recorder adds each charge to a cell
    of its own by the main CPU's phase (psp_current_phase), and the slots'
    names are the steps' own string constants. Steps are timed one frame in
    four ("stepsall": every frame), so each slice carries its timed frames;
  - lanetrace's notes (psp_lanetrace.c): the main CPU's phase changes, the
    GE's busy spans (gebusy or gebusy2 on), the ME's slot and ring jobs, all
    on the main CPU's clock. While the recorder runs, lanetrace hands each
    frame's notes here instead of opening its own windows;
  - hwprofile's counters (psp_platform.c; halo_profile.prx, ten times a
    second), totals and by phase, when "hwprofile" is on;
  - the frame's and the tick's times psp_frame_account and psp_tick_account
    already take for the heartbeat.
New here: the events (map, cutscenes, BSP, the player's seat, deaths, the
level script's beats) and the per-slice counts (objects made and deleted by
type, actors by action, script functions called), from FIXES_FLIGHTREC.

Honest accounting. Each instrument's own cost is a row of every slice: the
recorder's hooks, its slice ends and its Memory Stick writes timed by the
clock; the step timers, lanetrace's notes, the game hooks and hwprofile's
phase calls as counts times a cost measured at the start (a loop of each
call, timed, written into the file's calibration). What none of these can
see (cache and bus disturbance, the profiler module's own sampling) shows as
the gap to a bare run of the same held replay.

The file: ms0:/halo/log/flightrec.flr, a run of records, each a 32-bit word
(type << 24 | payload bytes) and its payload, little-endian, 4-byte aligned;
psp/sierra/flightrec.py in the lab reads it. Written at each slice's end, so
nothing waits in memory longer than 2 s of game time, and at the exit.

Measurement only: it reads the game's state, never writes it. Its engine
hooks return at once on the Media Engine (PSP_ME_WORK) and when the word is
off. Its memory (about 100 KB, lanetrace's notes another 18) comes from the heap when the word is first
seen; without the word every hook is one test. */

#ifdef PSP_FLIGHTREC

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pspkernel.h>
#include <psppower.h>

#include "psp_paths.h"
#include "psp_me.h"
#include "psp_lanetrace.h"

int psp_keep(const char *name);
int psp_keep_loaded(void);
void platform_log(const char *format, ...);
long psp_game_time_peek(void);
extern unsigned char cinematic_in_progress(void); /* the engine's boolean */
extern short global_structure_bsp_index;
extern volatile unsigned long psp_stick_read_us; /* psp_frametrace.c */
extern volatile int psp_current_phase;           /* psp_platform.c */

/* psp_platform.c */
int psp_flightrec_hw(unsigned long long *count, unsigned long long *phase_count, unsigned int *changes);
int psp_flightrec_hw_phase_call(void);
void psp_step(const char *name);
int psp_flightrec_steps_timed(void);
/* psp_lanetrace.c */
int psp_lane_flightrec_start(void);
extern volatile unsigned int psp_lane_calls;
/* FIXES_FLIGHTREC (psp_fixups.py) */
int psp_flightrec_player(short *out);
void psp_flightrec_actors(unsigned short *by_action, int actions, unsigned short *summary);
const char *psp_flightrec_hs_name(int index);

#define FR_FILE PSP_DATA_ROOT "/log/flightrec.flr" /* (.bin is refused in results: push-results.sh) */
#define FR_VERSION 1
#define FR_SLICE_TICKS 60

#define FR_REC_HEADER 1
#define FR_REC_NAME 2
#define FR_REC_SLICE 3
#define FR_REC_END 4

#define FR_NAME_STEP 1
#define FR_NAME_SCRIPT 2
#define FR_NAME_MAP 3

#define FR_SEC_STEPS 'S'
#define FR_SEC_HW 'H'
#define FR_SEC_OBJECTS 'O'
#define FR_SEC_ACTORS 'A'
#define FR_SEC_SCRIPTS 'C'
#define FR_SEC_EVENTS 'E'
#define FR_SEC_INSTRUMENTS 'I'
#define FR_SEC_FRAMES 'F'

#define FR_EV_MAP 1
#define FR_EV_CUTSCENE_BEGIN 2
#define FR_EV_CUTSCENE_END 3
#define FR_EV_BSP 4
#define FR_EV_SEAT 5
#define FR_EV_DEATH 6
#define FR_EV_SCRIPT 7
#define FR_EV_TIME_BACK 8

/* the instruments' rows; flags: 1 timed by the clock, 2 a count times its
calibrated cost, 4 "calls" are bytes, 8 a part it can't time (named only) */
enum
{
	FR_INS_TICK_HOOK = 1,  /* the recorder's tick hook */
	FR_INS_FRAME_HOOK,     /* the recorder's frame hook, with lanetrace's hand-over */
	FR_INS_SLICE,          /* the slice's end: counters, censuses, encoding */
	FR_INS_WRITE,          /* the Memory Stick writes */
	FR_INS_GAME_HOOKS,     /* objects, scripts, deaths (FIXES_FLIGHTREC) */
	FR_INS_STEPS,          /* the step profile's timers (timed frames) */
	FR_INS_LANES,          /* lanetrace's notes (gebusy's, the ME's, the phases) */
	FR_INS_HWPROFILE,      /* hwprofile's phase calls; its sampling is the module's */
	FR_INS_CLOCK,          /* the recorder's own clock reads */
	FR_INS_CALIBRATION,    /* the start's calibration loops, once */
	FR_INSTRUMENTS
};

#define FR_STEPS 512          /* the step profile's PROFILE_SLOTS */
#define FR_PHASES 4
#define FR_SCRIPTS 1024
#define FR_EVENTS 256
#define FR_OBJECT_TYPES 16
#define FR_ACTIONS 14
#define FR_HW 19
#define FR_FRAMES_BYTES (56 * 1024) /* ~60 frames of ~25 notes; past it notes are dropped and counted */
#define FR_HEAD_BYTES (16 * 1024)   /* the slice's other sections and its names */
#define FR_CAL_LOOPS 256

volatile int psp_flightrec_on = 0;
static int fr_word = -1;

struct fr_event
{
	int tick;
	unsigned char kind, a;
	unsigned short b;
	unsigned int c;
};

struct fr_state
{
	/* the step profile's charges in this slice, by slot and phase */
	const char *step_name[FR_STEPS];
	unsigned int step_us[FR_STEPS][FR_PHASES];
	unsigned int step_calls[FR_STEPS][FR_PHASES];
	unsigned int steps_lost_us;
	/* step names already written to the file (by address), a small open table */
	const char *named[1024];
	/* script functions: calls this slice; 1 a beat (an event each call), 2 not, 0 unknown */
	unsigned short script_calls[FR_SCRIPTS];
	unsigned char script_kind[FR_SCRIPTS];
	unsigned char script_named[FR_SCRIPTS];
	unsigned short objects_made[FR_OBJECT_TYPES], objects_deleted[FR_OBJECT_TYPES];
	struct fr_event events[FR_EVENTS];
	unsigned int event_count, events_dropped;
	unsigned int game_hook_calls;
	/* the slice */
	unsigned int index;
	long tick_from;
	unsigned int t_open;
	unsigned int frames, ticks, tick_us, draw_us, end_us;
	unsigned int steps_frames, steps_timed_frames;
	unsigned long stick_us_open;
	unsigned int actors_sum, objects_sum, effects_sum, player_ticks;
	unsigned int notes_dropped, notes;
	unsigned int lane_calls_open;
	unsigned long long hw_last[FR_HW], hw_phase_last[FR_PHASES][FR_HW];
	unsigned int hw_changes_last;
	int hw_have_last;
	/* the instruments this slice */
	unsigned int ins_us[FR_INSTRUMENTS], ins_calls[FR_INSTRUMENTS];
	unsigned int clock_reads;
	/* calibration: each call's cost, in ns */
	unsigned int cal_clock_ns, cal_intr_ns, cal_step_ns, cal_hook_ns, cal_phase_ns;
	/* what the tick hook last saw */
	char map[32];
	int in_scene, have_scene;
	short bsp, seat, seat_type;
	int have_player;
	long last_tick;
	/* the frames section in the making: a 20-byte head and the frame's notes */
	unsigned char *frame_buf;
	unsigned int frames_used;
	unsigned int frame_note_at, frame_note_count;
	unsigned int file_bytes, slices_written, write_failures;
	unsigned char *head; /* FR_HEAD_BYTES */
};
static struct fr_state *fr;

static unsigned int fr_clock(void)
{
	if (fr)
		fr->clock_reads++;
	return sceKernelGetSystemTimeLow();
}

/* ---------- the file */

static int fr_put(unsigned char *buffer, unsigned int *at, unsigned int room, const void *data, unsigned int bytes)
{
	if (*at + bytes > room)
		return 0;
	memcpy(buffer + *at, data, bytes);
	*at += bytes;
	return 1;
}

static void fr_put32(unsigned char *buffer, unsigned int *at, unsigned int room, unsigned int value)
{
	fr_put(buffer, at, room, &value, 4);
}

static void fr_write(const void *a, unsigned int a_bytes, const void *b, unsigned int b_bytes, int truncate)
{
	SceUID file;
	unsigned int t0 = fr_clock();

	file = sceIoOpen(FR_FILE, PSP_O_WRONLY | PSP_O_CREAT | (truncate ? PSP_O_TRUNC : PSP_O_APPEND), 0777);
	if (file < 0)
	{
		fr->write_failures++;
		return;
	}
	if (a_bytes && sceIoWrite(file, a, a_bytes) != (int)a_bytes)
		fr->write_failures++;
	if (b_bytes && sceIoWrite(file, b, b_bytes) != (int)b_bytes)
		fr->write_failures++;
	sceIoClose(file);
	fr->file_bytes += a_bytes + b_bytes;
	fr->ins_us[FR_INS_WRITE] += fr_clock() - t0;
	fr->ins_calls[FR_INS_WRITE] += a_bytes + b_bytes;
}

/* a name record, into the head buffer ahead of the slice's own record */
static void fr_name(unsigned int *at, int kind, unsigned int id, const char *text)
{
	unsigned int length = 0, padded;
	unsigned char record[4 + 8 + 204];

	while (text && length < 200 && text[length])
		length++;
	padded = (8 + length + 3) & ~3u;

	memset(record, 0, sizeof(record));
	*(unsigned int *)record = (unsigned int)FR_REC_NAME << 24 | padded;
	record[4] = (unsigned char)kind;
	*(unsigned short *)(record + 6) = (unsigned short)length;
	*(unsigned int *)(record + 8) = id;
	if (length)
		memcpy(record + 12, text, length);
	fr_put(fr->head, at, FR_HEAD_BYTES, record, 4 + padded);
}

static int fr_step_named(const char *name)
{
	unsigned int slot = ((unsigned int)name >> 2) & 1023, n;

	for (n = 0; n < 1024; n++, slot = (slot + 1) & 1023)
	{
		if (fr->named[slot] == name)
			return 1;
		if (!fr->named[slot])
		{
			fr->named[slot] = name;
			return 0;
		}
	}
	return 1; /* the table is full: names past it go unnamed (the reader shows the address) */
}

/* ---------- calibration: each counted call's own cost, timed in a loop */

static unsigned int fr_ns(unsigned int us, unsigned int loops)
{
	return (unsigned int)((unsigned long long)us * 1000u / loops);
}

static void fr_calibrate(void)
{
	static const char calibration_step[] = "flightrec: calibration";
	unsigned int t0, t1, k, flags;

	t0 = sceKernelGetSystemTimeLow();
	for (k = 0; k < FR_CAL_LOOPS; k++)
		(void)sceKernelGetSystemTimeLow();
	t1 = sceKernelGetSystemTimeLow();
	fr->cal_clock_ns = fr_ns(t1 - t0, FR_CAL_LOOPS);

	/* a lane note's cost is its interrupt hold and its store: the pair is
	most of it, so this is a floor */
	t0 = sceKernelGetSystemTimeLow();
	for (k = 0; k < FR_CAL_LOOPS; k++)
	{
		flags = sceKernelCpuSuspendIntr();
		sceKernelCpuResumeIntr(flags);
	}
	t1 = sceKernelGetSystemTimeLow();
	fr->cal_intr_ns = fr_ns(t1 - t0, FR_CAL_LOOPS);

	/* a step: the real psp_step, on a name of its own that the reader
	leaves out of the tree. In an untimed frame it measures the untimed
	path; the calibration waits for a timed one */
	t0 = sceKernelGetSystemTimeLow();
	for (k = 0; k < FR_CAL_LOOPS; k++)
		psp_step(calibration_step);
	t1 = sceKernelGetSystemTimeLow();
	fr->cal_step_ns = fr_ns(t1 - t0, FR_CAL_LOOPS);

	/* a game hook: the script hook on the last bucket, taken back after */
	t0 = sceKernelGetSystemTimeLow();
	for (k = 0; k < FR_CAL_LOOPS; k++)
	{
		extern void psp_flightrec_script(int index);

		psp_flightrec_script(FR_SCRIPTS - 1);
	}
	t1 = sceKernelGetSystemTimeLow();
	fr->cal_hook_ns = fr_ns(t1 - t0, FR_CAL_LOOPS);
	fr->script_calls[FR_SCRIPTS - 1] = 0;
	fr->game_hook_calls = 0;
	fr->event_count = 0;

	/* hwprofile's phase call (a system call into its module), the phase
	unchanged, so its counts go on to the same phase */
	fr->cal_phase_ns = 0;
	t0 = sceKernelGetSystemTimeLow();
	for (k = 0; k < 64; k++)
		if (!psp_flightrec_hw_phase_call())
			break;
	t1 = sceKernelGetSystemTimeLow();
	if (k == 64)
		fr->cal_phase_ns = fr_ns(t1 - t0, 64);
}

/* ---------- the slice */

static void fr_slice_open(long tick)
{
	unsigned long long hw[FR_HW], hw_phase[FR_PHASES][FR_HW];
	unsigned int changes;

	fr->tick_from = tick;
	fr->index = (unsigned int)(tick / FR_SLICE_TICKS);
	fr->t_open = fr_clock();
	fr->stick_us_open = psp_stick_read_us;
	fr->lane_calls_open = psp_lane_calls;
	if (psp_flightrec_hw(hw, &hw_phase[0][0], &changes))
	{
		memcpy(fr->hw_last, hw, sizeof(hw));
		memcpy(fr->hw_phase_last, hw_phase, sizeof(hw_phase));
		fr->hw_changes_last = changes;
		fr->hw_have_last = 1;
	}
	else
		fr->hw_have_last = 0;
}

static void fr_event(long tick, int kind, int a, int b, unsigned int c)
{
	struct fr_event *e;

	if (fr->event_count >= FR_EVENTS)
	{
		fr->events_dropped++;
		return;
	}
	e = &fr->events[fr->event_count++];
	e->tick = (int)tick;
	e->kind = (unsigned char)kind;
	e->a = (unsigned char)a;
	e->b = (unsigned short)b;
	e->c = c;
}

static void fr_section(unsigned int *at, int id, unsigned int count)
{
	fr_put32(fr->head, at, FR_HEAD_BYTES, (unsigned int)id | count << 16);
}

static void fr_instrument(unsigned int *at, int id, int flags, unsigned int calls, unsigned int us)
{
	unsigned int word = (unsigned int)id | (unsigned int)flags << 8;

	fr_put32(fr->head, at, FR_HEAD_BYTES, word);
	fr_put32(fr->head, at, FR_HEAD_BYTES, calls);
	fr_put32(fr->head, at, FR_HEAD_BYTES, us);
}

static unsigned int fr_estimate_us(unsigned int calls, unsigned int ns)
{
	return (unsigned int)(((unsigned long long)calls * ns + 500u) / 1000u);
}

static void fr_slice_close(long tick_to)
{
	unsigned int t0 = fr_clock(), at = 0, record_at, k, p, n, step_calls = 0, lane_calls;
	unsigned long long hw[FR_HW], hw_phase[FR_PHASES][FR_HW];
	unsigned int changes = 0, hw_changes = 0;
	int hw_on = psp_flightrec_hw(hw, &hw_phase[0][0], &changes);
	unsigned short by_action[FR_ACTIONS], actor_summary[2];

	/* the step names this slice brings in, as their own records first */
	for (k = 0; k < FR_STEPS; k++)
		if (fr->step_name[k] && !fr_step_named(fr->step_name[k]))
			fr_name(&at, FR_NAME_STEP, (unsigned int)fr->step_name[k], fr->step_name[k]);
	for (k = 0; k < FR_SCRIPTS - 1; k++)
		if (fr->script_calls[k] && !fr->script_named[k])
		{
			fr->script_named[k] = 1;
			fr_name(&at, FR_NAME_SCRIPT, k, psp_flightrec_hs_name((int)k));
		}

	memset(by_action, 0, sizeof(by_action));
	memset(actor_summary, 0, sizeof(actor_summary));
	psp_flightrec_actors(by_action, FR_ACTIONS, actor_summary);

	record_at = at;
	fr_put32(fr->head, &at, FR_HEAD_BYTES, 0); /* the record's word, filled below */
	/* the head: 24 words */
	fr_put32(fr->head, &at, FR_HEAD_BYTES, FR_VERSION);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, fr->index);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, (unsigned int)fr->tick_from);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, (unsigned int)tick_to);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, fr->t_open);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, t0);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, fr->frames);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, fr->ticks);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, fr->tick_us);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, fr->draw_us);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, fr->end_us);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, (unsigned int)(psp_stick_read_us - fr->stick_us_open));
	fr_put32(fr->head, &at, FR_HEAD_BYTES, fr->steps_frames);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, fr->steps_timed_frames);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, fr->notes);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, fr->notes_dropped);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, fr->events_dropped);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, fr->steps_lost_us);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, fr->player_ticks);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, fr->actors_sum);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, fr->objects_sum);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, fr->effects_sum);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, (unsigned int)actor_summary[0] | (unsigned int)actor_summary[1] << 16);
	fr_put32(fr->head, &at, FR_HEAD_BYTES, (unsigned int)scePowerGetCpuClockFrequencyInt());

	/* the step profile's cells: name, us, calls and phase */
	n = 0;
	for (k = 0; k < FR_STEPS; k++)
		for (p = 0; p < FR_PHASES; p++)
			if (fr->step_name[k] && (fr->step_us[k][p] || fr->step_calls[k][p]))
				n++;
	fr_section(&at, FR_SEC_STEPS, n);
	for (k = 0; k < FR_STEPS; k++)
		for (p = 0; p < FR_PHASES; p++)
			if (fr->step_name[k] && (fr->step_us[k][p] || fr->step_calls[k][p]))
			{
				fr_put32(fr->head, &at, FR_HEAD_BYTES, (unsigned int)fr->step_name[k]);
				fr_put32(fr->head, &at, FR_HEAD_BYTES, fr->step_us[k][p]);
				fr_put32(fr->head, &at, FR_HEAD_BYTES, (fr->step_calls[k][p] & 0x0FFFFFFFu) | p << 28);
				step_calls += fr->step_calls[k][p];
			}

	/* the CPU's counters: the slice's totals, then by phase; the module adds
	them ten times a second, so a slice's edges are good to 0.1 s */
	if (hw_on && fr->hw_have_last)
	{
		fr_section(&at, FR_SEC_HW, FR_HW * (1 + FR_PHASES));
		for (k = 0; k < FR_HW; k++)
			fr_put32(fr->head, &at, FR_HEAD_BYTES, (unsigned int)(hw[k] - fr->hw_last[k]));
		for (p = 0; p < FR_PHASES; p++)
			for (k = 0; k < FR_HW; k++)
				fr_put32(fr->head, &at, FR_HEAD_BYTES, (unsigned int)(hw_phase[p][k] - fr->hw_phase_last[p][k]));
		hw_changes = changes - fr->hw_changes_last;
	}

	fr_section(&at, FR_SEC_OBJECTS, 2 * FR_OBJECT_TYPES);
	fr_put(fr->head, &at, FR_HEAD_BYTES, fr->objects_made, sizeof(fr->objects_made));
	fr_put(fr->head, &at, FR_HEAD_BYTES, fr->objects_deleted, sizeof(fr->objects_deleted));

	fr_section(&at, FR_SEC_ACTORS, FR_ACTIONS);
	fr_put(fr->head, &at, FR_HEAD_BYTES, by_action, sizeof(by_action));

	n = 0;
	for (k = 0; k < FR_SCRIPTS; k++)
		if (fr->script_calls[k])
			n++;
	fr_section(&at, FR_SEC_SCRIPTS, n);
	for (k = 0; k < FR_SCRIPTS; k++)
		if (fr->script_calls[k])
			fr_put32(fr->head, &at, FR_HEAD_BYTES, k | (unsigned int)fr->script_calls[k] << 16);

	fr_section(&at, FR_SEC_EVENTS, fr->event_count);
	fr_put(fr->head, &at, FR_HEAD_BYTES, fr->events, fr->event_count * sizeof(struct fr_event));

	/* the instruments: the timed ones as they stand now (this slice end's
	own time and its write go into the next slice: they happen after this
	record is made) */
	lane_calls = psp_lane_calls - fr->lane_calls_open;
	fr_section(&at, FR_SEC_INSTRUMENTS, FR_INSTRUMENTS - 1);
	fr_instrument(&at, FR_INS_TICK_HOOK, 1, fr->ins_calls[FR_INS_TICK_HOOK], fr->ins_us[FR_INS_TICK_HOOK]);
	fr_instrument(&at, FR_INS_FRAME_HOOK, 1, fr->ins_calls[FR_INS_FRAME_HOOK], fr->ins_us[FR_INS_FRAME_HOOK]);
	fr_instrument(&at, FR_INS_SLICE, 1, fr->ins_calls[FR_INS_SLICE], fr->ins_us[FR_INS_SLICE]);
	fr_instrument(&at, FR_INS_WRITE, 1 | 4, fr->ins_calls[FR_INS_WRITE], fr->ins_us[FR_INS_WRITE]);
	fr_instrument(&at, FR_INS_GAME_HOOKS, 2, fr->game_hook_calls, fr_estimate_us(fr->game_hook_calls, fr->cal_hook_ns));
	fr_instrument(&at, FR_INS_STEPS, 2, step_calls, fr_estimate_us(step_calls, fr->cal_step_ns));
	fr_instrument(&at, FR_INS_LANES, 2, lane_calls, fr_estimate_us(lane_calls, fr->cal_intr_ns));
	fr_instrument(&at, FR_INS_HWPROFILE, hw_on ? 2 | 8 : 8, hw_changes, fr_estimate_us(hw_changes, fr->cal_phase_ns));
	fr_instrument(&at, FR_INS_CLOCK, 2, fr->clock_reads, fr_estimate_us(fr->clock_reads, fr->cal_clock_ns));
	fr_instrument(&at, FR_INS_CALIBRATION, 1, fr->ins_calls[FR_INS_CALIBRATION], fr->ins_us[FR_INS_CALIBRATION]);

	fr_section(&at, FR_SEC_FRAMES, fr->frames);
	/* the record's word: the head from its word on, then the frames */
	*(unsigned int *)(fr->head + record_at) = (unsigned int)FR_REC_SLICE << 24 | (at - record_at - 4 + fr->frames_used);

	/* the next slice's counts start here; the write is timed into it */
	memset(fr->step_name, 0, sizeof(fr->step_name));
	memset(fr->step_us, 0, sizeof(fr->step_us));
	memset(fr->step_calls, 0, sizeof(fr->step_calls));
	memset(fr->script_calls, 0, sizeof(fr->script_calls));
	memset(fr->objects_made, 0, sizeof(fr->objects_made));
	memset(fr->objects_deleted, 0, sizeof(fr->objects_deleted));
	memset(fr->ins_us, 0, sizeof(fr->ins_us));
	memset(fr->ins_calls, 0, sizeof(fr->ins_calls));
	fr->steps_lost_us = 0;
	fr->event_count = fr->events_dropped = 0;
	fr->game_hook_calls = 0;
	fr->frames = fr->ticks = fr->tick_us = fr->draw_us = fr->end_us = 0;
	fr->steps_frames = fr->steps_timed_frames = 0;
	fr->actors_sum = fr->objects_sum = fr->effects_sum = fr->player_ticks = 0;
	fr->notes = fr->notes_dropped = 0;
	fr->clock_reads = 0;

	fr->ins_us[FR_INS_SLICE] += fr_clock() - t0;
	fr->ins_calls[FR_INS_SLICE]++;
	fr_write(fr->head, at, fr->frame_buf, fr->frames_used, 0);
	fr->frames_used = 0;
	fr->slices_written++;
}

/* ---------- the start */

static int fr_ready(void)
{
	unsigned int t0;

	if (fr_word >= 0)
		return fr_word;
	if (!psp_keep_loaded())
		return 0;
	fr_word = psp_keep("flightrec") != 0;
	if (!fr_word)
		return 0;
	fr = (struct fr_state *)calloc(1, sizeof(*fr));
	if (fr)
	{
		fr->frame_buf = (unsigned char *)malloc(FR_FRAMES_BYTES);
		fr->head = (unsigned char *)malloc(FR_HEAD_BYTES);
	}
	if (!fr || !fr->frame_buf || !fr->head || !psp_lane_flightrec_start())
	{
		platform_log("flightrec: no room for its %u KB; off",
			(unsigned int)((sizeof(*fr) + FR_FRAMES_BYTES + FR_HEAD_BYTES) / 1024));
		if (fr)
		{
			free(fr->frame_buf);
			free(fr->head);
			free(fr);
		}
		fr = NULL;
		fr_word = 0;
		return 0;
	}
	fr->bsp = -1;
	fr->seat = -3;
	fr->last_tick = -1;
	t0 = sceKernelGetSystemTimeLow();
	platform_log("flightrec: on (%u KB; %s every %d ticks; the instruments' own cost in each record)",
		(unsigned int)((sizeof(*fr) + FR_FRAMES_BYTES + FR_HEAD_BYTES) / 1024), FR_FILE, FR_SLICE_TICKS);
	fr->ins_us[FR_INS_CALIBRATION] += sceKernelGetSystemTimeLow() - t0;
	return 1;
}

static int fr_calibrated = 0;

/* the file's header: its version, the calibration (ns a call) and whether
the pad path is built in */
static void fr_header(void)
{
	unsigned int at = 0, t0 = sceKernelGetSystemTimeLow();
	unsigned char record[64];

	/* on before the loops, so the game hook's cost is its recording path */
	psp_flightrec_on = 1;
	fr_calibrate();
	fr->ins_us[FR_INS_CALIBRATION] += sceKernelGetSystemTimeLow() - t0;
	fr->ins_calls[FR_INS_CALIBRATION]++;
	fr_put32(record, &at, sizeof(record), (unsigned int)FR_REC_HEADER << 24 | 40);
	fr_put32(record, &at, sizeof(record), 0x3152464Cu); /* "LFR1" */
	fr_put32(record, &at, sizeof(record), FR_VERSION);
	fr_put32(record, &at, sizeof(record), FR_SLICE_TICKS);
	fr_put32(record, &at, sizeof(record), fr->cal_clock_ns);
	fr_put32(record, &at, sizeof(record), fr->cal_intr_ns);
	fr_put32(record, &at, sizeof(record), fr->cal_step_ns);
	fr_put32(record, &at, sizeof(record), fr->cal_hook_ns);
	fr_put32(record, &at, sizeof(record), fr->cal_phase_ns);
	fr_put32(record, &at, sizeof(record), FR_CAL_LOOPS);
	fr_put32(record, &at, sizeof(record),
#ifdef PSP_PADPATH
		1
#else
		0
#endif
		);
	fr_write(record, at, NULL, 0, 1);
	platform_log("flightrec: calibrated (ns a call): clock %u, interrupt hold %u, step %u, game hook %u, hwprofile phase %u",
		fr->cal_clock_ns, fr->cal_intr_ns, fr->cal_step_ns, fr->cal_hook_ns, fr->cal_phase_ns);
	fr_calibrated = 1;
}

/* ---------- the hooks */

/* psp_platform.c's psp_tick_account, after each game tick: the tick's time,
the player's state and the port's own edges (the map, cutscenes, BSP) */
void psp_flightrec_tick(unsigned long tick_us, const char *map)
{
	unsigned int t0;
	long tick;
	short player[8];
	int scene;

	if (fr_word == 0 || (fr_word < 0 && !fr_ready()))
		return;
	t0 = fr_clock();
	tick = psp_game_time_peek();
	if (tick < 0)
		return;
	if (!fr_calibrated)
	{
		/* in a timed frame, so the step's cost is the timed path's */
		if (!psp_flightrec_steps_timed())
			return;
		fr_header();
		fr_slice_open(tick);
	}
	if (fr->last_tick >= 0 && tick < fr->last_tick)
		fr_event(tick, FR_EV_TIME_BACK, 0, 0, (unsigned int)fr->last_tick);
	fr->last_tick = tick;
	fr->ticks++;
	fr->tick_us += (unsigned int)tick_us;
	if (map && strncmp(map, fr->map, sizeof(fr->map)) != 0)
	{
		unsigned int at = 0;

		snprintf(fr->map, sizeof(fr->map), "%s", map);
		fr_event(tick, FR_EV_MAP, 0, 0, fr->index);
		fr_name(&at, FR_NAME_MAP, (unsigned int)tick, fr->map);
		fr_write(fr->head, at, NULL, 0, 0);
		fr->have_scene = 0;
		fr->bsp = global_structure_bsp_index;
	}
	scene = cinematic_in_progress() != 0;
	if (!fr->have_scene || scene != fr->in_scene)
	{
		if (fr->have_scene || scene)
			fr_event(tick, scene ? FR_EV_CUTSCENE_BEGIN : FR_EV_CUTSCENE_END, 0, 0, 0);
		fr->in_scene = scene;
		fr->have_scene = 1;
	}
	if (global_structure_bsp_index != fr->bsp)
	{
		fr_event(tick, FR_EV_BSP, fr->bsp & 0xFF, global_structure_bsp_index, 0);
		fr->bsp = global_structure_bsp_index;
	}
	/* the player: out[0] seat (-1 on foot, -2 no unit), out[1] what he rides
	(its object type, -1 none), out[2..4] actors, objects, effects live */
	if (psp_flightrec_player(player))
	{
		if (player[0] != fr->seat || player[1] != fr->seat_type)
		{
			fr_event(tick, FR_EV_SEAT, player[1] & 0xFF, player[0], 0);
			fr->seat = player[0];
			fr->seat_type = player[1];
		}
		fr->actors_sum += (unsigned short)player[2];
		fr->objects_sum += (unsigned short)player[3];
		fr->effects_sum += (unsigned short)player[4];
		fr->player_ticks++;
	}
	fr->ins_us[FR_INS_TICK_HOOK] += fr_clock() - t0;
	fr->ins_calls[FR_INS_TICK_HOOK]++;
}

/* psp_lanetrace.c's frame end, with the interrupts held: this frame's notes */
void psp_flightrec_lanes(const void *notes, unsigned int count)
{
	unsigned int bytes = count * 12, room;

	if (!psp_flightrec_on || !fr)
		return;
	if (fr->frame_note_count == 0)
		fr->frame_note_at = fr->frames_used + 20;
	room = fr->frame_note_at + fr->frame_note_count * 12 <= FR_FRAMES_BYTES ?
		FR_FRAMES_BYTES - (fr->frame_note_at + fr->frame_note_count * 12) : 0;
	if (bytes > room)
	{
		fr->notes_dropped += (bytes - room) / 12;
		bytes = room - room % 12;
	}
	memcpy(fr->frame_buf + fr->frame_note_at + fr->frame_note_count * 12, notes, bytes);
	fr->frame_note_count += bytes / 12;
}

/* psp_platform.c's psp_frame_account, after lanetrace's hand-over: the
frame's head, and the slice's end when the game time has passed it */
void psp_flightrec_frame(unsigned long draw_us, unsigned long end_us, unsigned int ticks, unsigned long frame_number,
	int steps_timed)
{
	unsigned int t0, *head;
	long tick;

	if (!psp_flightrec_on || !fr)
		return;
	t0 = fr_clock();
	if (fr->frames_used + 20 <= FR_FRAMES_BYTES)
	{
		head = (unsigned int *)(fr->frame_buf + fr->frames_used);
		head[0] = (unsigned int)frame_number;
		head[1] = t0;
		head[2] = (unsigned int)draw_us;
		head[3] = (unsigned int)end_us;
		head[4] = (ticks > 0xFFFF ? 0xFFFF : ticks) | (fr->frame_note_count > 0xFFFF ? 0xFFFF : fr->frame_note_count) << 16;
		fr->frames_used += 20 + fr->frame_note_count * 12;
		fr->notes += fr->frame_note_count;
		fr->frames++;
	}
	else
		fr->notes_dropped += fr->frame_note_count;
	fr->frame_note_count = 0;
	fr->draw_us += (unsigned int)draw_us;
	fr->end_us += (unsigned int)end_us;
	fr->steps_frames++;
	if (steps_timed)
		fr->steps_timed_frames++;
	tick = psp_game_time_peek();
	fr->ins_us[FR_INS_FRAME_HOOK] += fr_clock() - t0;
	fr->ins_calls[FR_INS_FRAME_HOOK]++;
	if (tick >= 0 && ((unsigned int)(tick / FR_SLICE_TICKS) != fr->index || tick < fr->tick_from))
	{
		fr_slice_close(tick);
		fr_slice_open(tick);
	}
}

/* psp_platform.c's step profile: each charge, as it lands in its slot */
void psp_flightrec_step(int slot, const char *name, unsigned int us)
{
	int phase = psp_current_phase & 3;

	if (!fr || slot < 0 || slot >= FR_STEPS)
		return;
	if (fr->step_name[slot] != name)
	{
		/* the profile's table was cleared under us (its first window's
		start) and the slot given to another name: the old name's part of
		this slice is counted as lost, not mixed in */
		if (fr->step_name[slot])
		{
			int p;

			for (p = 0; p < FR_PHASES; p++)
			{
				fr->steps_lost_us += fr->step_us[slot][p];
				fr->step_us[slot][p] = fr->step_calls[slot][p] = 0;
			}
		}
		fr->step_name[slot] = name;
	}
	fr->step_us[slot][phase] += us;
	fr->step_calls[slot][phase]++;
}

/* FIXES_FLIGHTREC: an object made (made 1) or deleted, by its type */
void psp_flightrec_object(int type, int made)
{
	if (!psp_flightrec_on || PSP_ME_WORK())
		return;
	fr->game_hook_calls++;
	if (type < 0 || type >= FR_OBJECT_TYPES)
		type = FR_OBJECT_TYPES - 1;
	if (made)
		fr->objects_made[type]++;
	else
		fr->objects_deleted[type]++;
}

/* the level script's beats: the calls that place, make, end or play
something, each an event; every other function is counted only */
static int fr_script_kind(int index)
{
	static const char *const beats[] =
	{
		"ai_place", "ai_migrate", "ai_kill", "ai_erase", "ai_attach", "ai_magically_see",
		"object_create", "object_destroy", "unit_enter_vehicle", "unit_exit_vehicle",
		"sound_looping_start", "sound_looping_stop", "sound_impulse_start",
		"cinematic_start", "cinematic_stop", "camera_control", "fade_in", "fade_out",
		"game_save", "device_set_position", "show_hud", "hud_",
	};
	const char *name = psp_flightrec_hs_name(index);
	unsigned int k;

	if (!name)
		return 2;
	for (k = 0; k < sizeof(beats) / sizeof(beats[0]); k++)
		if (strncmp(name, beats[k], strlen(beats[k])) == 0)
			return 1;
	return 2;
}

/* FIXES_FLIGHTREC: a script function called (its first evaluation) */
void psp_flightrec_script(int index)
{
	long tick;

	if (!psp_flightrec_on || PSP_ME_WORK())
		return;
	fr->game_hook_calls++;
	if (index < 0 || index >= FR_SCRIPTS)
		index = FR_SCRIPTS - 1;
	if (fr->script_calls[index] < 0xFFFF)
		fr->script_calls[index]++;
	if (index == FR_SCRIPTS - 1)
		return;
	if (!fr->script_kind[index])
		fr->script_kind[index] = (unsigned char)fr_script_kind(index);
	if (fr->script_kind[index] == 1)
	{
		tick = psp_game_time_peek();
		fr_event(tick, FR_EV_SCRIPT, 0, index, 0);
	}
}

/* FIXES_FLIGHTREC: a unit died (ai_handle_death): its team, whether a
player caused it, and the death's hostility as the AI hears it */
void psp_flightrec_death(int team, int by_player, int hostility)
{
	if (!psp_flightrec_on || PSP_ME_WORK())
		return;
	fr->game_hook_calls++;
	fr_event(psp_game_time_peek(), FR_EV_DEATH, team & 0xFF, (by_player ? 1 : 0) | (hostility & 0xFF) << 8, 0);
}

/* psp_platform.c's exit_game: the slice in hand, and the end record */
void psp_flightrec_exit(const char *why)
{
	unsigned int record[8], at = 0;

	if (!psp_flightrec_on || !fr)
		return;
	if (fr->frames || fr->ticks)
		fr_slice_close(psp_game_time_peek());
	fr_put32((unsigned char *)record, &at, sizeof(record), (unsigned int)FR_REC_END << 24 | 16);
	fr_put32((unsigned char *)record, &at, sizeof(record), fr->slices_written);
	fr_put32((unsigned char *)record, &at, sizeof(record), fr->file_bytes);
	fr_put32((unsigned char *)record, &at, sizeof(record), fr->write_failures);
	fr_put32((unsigned char *)record, &at, sizeof(record), (unsigned int)(why ? why[0] : 0));
	fr_write(record, at, NULL, 0, 0);
	platform_log("flightrec: %u slices, %u KB written to %s (%u write failures); ended by %s",
		fr->slices_written, fr->file_bytes / 1024, FR_FILE, fr->write_failures, why ? why : "?");
	psp_flightrec_on = 0;
}

#endif
