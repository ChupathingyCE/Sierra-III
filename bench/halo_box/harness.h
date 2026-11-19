/* The harness's portable part (the capture's reader, the passes, the
comparison, the figures) and what it asks of the machine it runs on. The same
file runs on the PSP (main.c) and on a desktop (host/host.c), where only the
reader, the comparison and the report's arithmetic mean anything. */
#ifndef HALO_BOX_HARNESS_H
#define HALO_BOX_HARNESS_H

#include "box.h"

enum { MODE_WARM, MODE_EMPTIED, MODE_CROWDED, MODES };
#define DUTIES_MOST 4

struct harness_options
{
	int ticks_most;          /* 0: every tick of the capture */
	int duties[DUTIES_MOST]; /* the GE's share of the time on main RAM, percent; duties[0] is 0 */
	int duty_count;
	/* a box that reads nothing (the null box) runs without a capture over
	this many ticks of this many entities */
	int synthetic_ticks, synthetic_entities;
	int counted;             /* 1: a third pass in each mode, alone, with the CPU's counters read either side of each call */
	int on_me;               /* the Media Engine's places to run the per-entity passes in, a bit each (1 << PLACE_ME_...): keep words me, meedram, mebusy */
	int me_jobs;             /* 1 (mejob): in the ram and eDRAM places, each entity also handed to the ME as a job of its own, timed from the main CPU */
};

/* where a pass runs on the Media Engine (README, "On the Media Engine") */
enum { PLACE_ME_RAM, PLACE_ME_EDRAM, PLACE_ME_BUSY, PLACES_ME };
/* the main CPU's own loop: µs over main-RAM lines read */
/* the main CPU's loop beside the ME: all of it, and split by what the ME was
doing while each step ran (mode x the copy in, the box's calls, the copy
back; a step that spanned a change is counted in the total only) */
enum { BUSY_COPY_IN, BUSY_CALLS, BUSY_COPY_BACK, BUSY_DMA_IN, BUSY_DMA_BACK, BUSY_PHASES };
/* with medma, after each busy pass the ME also moves the same bytes the copy
in moved, main RAM to eDRAM and back, by DMACplus, which it drives itself:
dma_cycles are the ME's cycles for each way, dma_bytes the bytes a way */
struct me_busy { double us; unsigned int lines; double phase_us[MODES * BUSY_PHASES]; unsigned int phase_lines[MODES * BUSY_PHASES];
	double dma_cycles[MODES * 2]; double dma_bytes[MODES]; unsigned int dma_passes[MODES], dma_failed; int dma_error; };
extern int plat_me_dma; /* the keep word medma */

#define COUNTERS 19 /* the CPU's, in the SDK's PspDebugProfilerRegs order after its enable word */
enum { C_SYSTEM, C_CYCLES, C_STALL_INTERNAL, C_STALL_MEMORY, C_STALL_COP, C_STALL_VFPU, C_SLEEP, C_BUS, C_UNCACHED_LOAD,
	C_UNCACHED_STORE, C_CACHED_LOAD, C_CACHED_STORE, C_I_MISS, C_D_MISS, C_D_WRITEBACK, C_COP0, C_FPU, C_VFPU, C_LOCAL_BUS };

/* runs `box`, and the null box beside it, over the capture files; the
report goes out through plat_out. Returns 0 when every output was within the
box's tolerance */
int harness_run(const struct box_adapter *box, const char *const *files, int file_count, const struct harness_options *options);

/* writes the self-test box's capture to `path` (selftest_capture.c); a spoil tick of -1 spoils nothing. 1 when written */
int selftest_capture(const char *path, unsigned int ticks, unsigned int entities, int spoil_tick, int spoil_entity);

/* ---- the machine ---- */
unsigned int plat_now(void);                 /* microseconds */
void plat_empty(void);                       /* both caches emptied */
void plat_crowd(void);                       /* both caches filled with other work's lines, the data lines dirty */
void plat_ge_start(int duty, unsigned int span_us); /* the GE reading main RAM `duty` percent of the time, for at least the span */
int plat_ge_stop(void);                      /* 1 if the GE was still at it (the span was covered) */
/* of the span just stopped: its length, and the part of it the GE spent off main RAM (the blocks it finished, at what such a block spends there alone). 0 if unknown */
int plat_ge_spent(double *span_us, double *other_us);
void plat_counters(unsigned int out[COUNTERS]); /* the CPU's counters as they stand (only called when options.counted) */
/* the Media Engine (me.c; none on a desktop): started with the crowded mode's
data and code, its eDRAM copy made only when want_copy; 1 when it runs passes, 2 when they run here in its place (the
emulator), 0 when it doesn't */
int plat_me_start(void *crowd_data, void (*crowd_code)(void), int want_copy);
int plat_me_copied(void);                        /* 1: the eDRAM copy is in place */
void *plat_me_where(void *function, int place);  /* the function's copy for an eDRAM place, else itself */
/* the per-entity pass on the ME: each call's cycles into `cycles`; the
adapter's place hook (or 0) moves its data into eDRAM and back. 0, or -1 the
ME didn't finish (plat_me_at names the call), -2 the place can't run, -3 the
data didn't fit its room. busy is null for the null box's passes: the busy
place's loop still runs then, its figure not kept */
int plat_me_pass(void (*run)(int), int (*place)(void *, unsigned int, int), unsigned int calls, int mode, int where,
	unsigned int *cycles, struct me_busy *busy);
void plat_me_busy_alone(struct me_busy *into, unsigned int lines);
/* the per-entity jobs (mejob): each of `calls` entities handed to the ME on
its own (its caches emptied or crowded first by the mode, untimed), the main
CPU's D-cache written back before each (its µs into writeback_us), the
hand-over and the wait timed from the main CPU (µs into us), the ME's own
cycles for the call into cycles. The three arrays must sit on cache lines of
their own: the main CPU writes two of them while the ME writes the third.
0, -1 the ME didn't finish, -2 the place can't run */
int plat_me_jobs(void (*run)(int), unsigned int calls, int mode, int where, unsigned int *us, unsigned int *cycles,
	unsigned int *writeback_us);
unsigned int plat_me_at(void);
int plat_me_placed(void);
void plat_out(const char *format, ...) __attribute__((format(printf, 1, 2)));
void plat_step(const char *format, ...) __attribute__((format(printf, 1, 2))); /* names a step before it starts, on disk before it returns */

#endif
