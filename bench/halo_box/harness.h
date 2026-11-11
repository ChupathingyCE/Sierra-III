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
};

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
void plat_counters(unsigned int out[COUNTERS]); /* the CPU's counters as they stand (only called when options.counted) */
void plat_out(const char *format, ...) __attribute__((format(printf, 1, 2)));
void plat_step(const char *format, ...) __attribute__((format(printf, 1, 2))); /* names a step before it starts, on disk before it returns */

#endif
