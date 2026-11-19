/*
KSAMPLE (recon only, build switch PSP_KSAMPLE=1, keep word ksample with
pcsample and hwprofile): where the main CPU is when the PC sampler finds it in
kernel space. Without it every such sample is one count, pc_kernel, and
nobody can say whose time that is: the game's system calls, another thread's
wakes, the idle thread holding the CPU while the game waits, or the
firmware's own work. The game allocates this block and hands its address to
halo_profile.prx through the table's ksample_block; the prx fills it and the
game writes it to log/pcsample.txt beside the program's bins
(psp/sierra/ksample.py in the lab names the bins).

The bins cover the kernel's RAM (physical 0x08000000-0x083FFFFF, where the
kernel's modules load) in 1 << shift bytes; the modules' own load addresses
and sizes come from the kernel's module list, read at start and refreshed,
so the bins are named offline from what this PSP reported, not from the
firmware's files. Each sample is split by the game's phase and by the
thread the kernel says is running (the game's, another, or not known: the
call refused, or before the game's first phase).
*/
#ifndef HALO_PROFILE_KSAMPLE_H
#define HALO_PROFILE_KSAMPLE_H

#define KSAMPLE_MAGIC 0x4B534D50u /* "KSMP" */
#define KSAMPLE_LOW 0x08000000u /* physical: kseg0 0x88000000 and kseg1 0xA8000000 both land here */
#define KSAMPLE_SPAN 0x00400000u
#define KSAMPLE_PHASES 4
#define KSAMPLE_CTX 3 /* 0 the game's thread, 1 another thread, 2 not known */
#define KSAMPLE_MODULES 64
#define KSAMPLE_THREADS 32
#define KSAMPLE_FAR 16

struct ksample_module
{
	unsigned int text_addr, text_size;
	char name[28];
};

struct ksample_thread
{
	volatile int uid;
	volatile unsigned int count;
	char name[32];
};

struct ksample_block
{
	unsigned int magic;
	unsigned int shift, bins;
	/* the game's: 1 asks the prx to split by thread */
	unsigned int want_ctx;
	/* the game's: [phase][ctx][bin] counts, KSAMPLE_PHASES * KSAMPLE_CTX * bins words */
	unsigned int counts;
	/* every sample by phase (the denominator of each phase's kernel share),
	the kernel's by phase, those outside the binned span, and the thread
	lookups the kernel refused */
	volatile unsigned int phase_samples[KSAMPLE_PHASES];
	volatile unsigned int phase_kernel[KSAMPLE_PHASES];
	volatile unsigned int far, refused, threads_full;
	/* the last kernel PCs outside the span, a ring */
	volatile unsigned int far_pc[KSAMPLE_FAR];
	volatile unsigned int far_next;
	/* the kernel's module list: how many, how many times read, the last
	error (0 none) */
	volatile unsigned int modules, module_reads;
	volatile int module_error;
	struct ksample_module module[KSAMPLE_MODULES];
	/* the threads found in the kernel, by the kernel's id, and their names
	(filled by the prx's sampler thread, outside the interrupt) */
	volatile unsigned int threads;
	struct ksample_thread thread[KSAMPLE_THREADS];
};

#endif
