/* The frame picture's block (docs/frame_picture.md), shared by
halo_profile.prx (psp/profile/main.c), which charges each mark's counters to
the box that was open, and the game (psp/src/psp_framepic.c), which folds
them into histograms at each tick's and frame's end. The block is the game's
(partition 2, outside the heap), handed to the module once; the module writes
only `acc`, `sum`, `marks`, `switched` and `unbalanced`, and only from the game
thread's own system call, so neither side needs a lock */

#ifndef PSP_FRAMEPIC_H
#define PSP_FRAMEPIC_H

#define FRAMEPIC_MAGIC 0x46504943u /* "FPIC" */
#define FRAMEPIC_BOXES 24
#define FRAMEPIC_STACK 8
#define FRAMEPIC_POP (-1)

/* what a mark reads, by its index after the profiler's enable word
(PspDebugProfilerRegs order: 0 system clocks, 1 CPU cycles, 3 memory stall,
6 asleep, 8 and 9 uncached loads and stores, 12 I-misses, 13 D-misses, 14
write-backs) */
enum framepic_measure
{
	FRAMEPIC_CYCLES,
	FRAMEPIC_MEMORY,
	FRAMEPIC_ASLEEP,
	FRAMEPIC_IMISS,
	FRAMEPIC_DMISS,
	FRAMEPIC_WRITEBACK,
	FRAMEPIC_UNCACHED_LOAD,
	FRAMEPIC_UNCACHED_STORE,
	FRAMEPIC_MEASURES
};
#define FRAMEPIC_REGISTERS { 1, 3, 6, 12, 13, 14, 8, 9 }

struct framepic_block
{
	unsigned int magic;
	/* the game's: 1 asks the module to read the thread-switch count at each
	mark (a refer call), so a box's sample with a switch in it is told apart */
	unsigned int want_switch;
	/* the module's: the counts since the box's last fold (the game zeroes
	them as it folds), and since launch */
	unsigned int acc[FRAMEPIC_BOXES][FRAMEPIC_MEASURES];
	unsigned long long sum[FRAMEPIC_BOXES][FRAMEPIC_MEASURES];
	/* marks made with each box on top before it (every mark charges one) */
	unsigned int marks[FRAMEPIC_BOXES];
	/* 1 when a thread switch fell inside the box since its last fold */
	unsigned int switched[FRAMEPIC_BOXES];
	/* a pop with nothing open, or a push past the stack's depth */
	unsigned int unbalanced;
	unsigned int depth;
};

#endif
