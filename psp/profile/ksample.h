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
firmware's files.

Who holds the CPU at a sample comes from the game, not the kernel: the
kernel won't name the running thread inside an interrupt (the first PSP run
had every sceKernelGetThreadId there refused), so the game's threads say
when they block. psp_platform.c wraps the blocking calls (the waits on a
semaphore, an event flag, a delay, the GE, the display, the audio output)
and each thread writes its waiter slot's site, the caller's PC, for as long
as the call blocks. At each sample the prx reads the slots: the thread that
names the phases (the game's main thread) awake, or waiting with another of
the game's threads awake, or no game thread awake (the idle thread or a
firmware thread then holds the CPU), or not known. The main thread's wait
site is counted too, so a wait is named by the code that made it (the GE's
sync, an ME handoff, the vblank).

The interrupts by source: the interrupt manager's own count of each
handler's calls (QueryIntrHandlerInfo, kernel mode), read by the prx at
every phase change and split by phase.
*/
#ifndef HALO_PROFILE_KSAMPLE_H
#define HALO_PROFILE_KSAMPLE_H

#define KSAMPLE_MAGIC 0x4B534D32u /* "KSM2": the waiter slots and the interrupt counts */
#define KSAMPLE_LOW 0x08000000u /* physical: kseg0 0x88000000 and kseg1 0xA8000000 both land here */
#define KSAMPLE_SPAN 0x00400000u
#define KSAMPLE_PHASES 4
/* 0 the main thread awake, 1 the main thread waiting and another of the
game's threads awake, 2 no game thread awake, 3 not known */
#define KSAMPLE_CTX 4
#define KSAMPLE_MODULES 64
#define KSAMPLE_WAITERS 16
#define KSAMPLE_SITES 32
#define KSAMPLE_INTRS 32
#define KSAMPLE_FAR 16

struct ksample_module
{
	unsigned int text_addr, text_size;
	char name[28];
};

/* one of the game's threads, claimed at its first wrapped wait: site is
the wait's caller while the call blocks, 0 while the thread runs */
struct ksample_waiter
{
	volatile int uid;
	volatile unsigned int stack_lo, stack_hi;
	volatile unsigned int site;
	char name[32];
};

struct ksample_block
{
	unsigned int magic;
	unsigned int shift, bins;
	/* the game's: 1 when the waiter slots are kept (ksamplenoctx: 0) */
	unsigned int want_ctx;
	/* the game's: [phase][ctx][bin] counts, KSAMPLE_PHASES * KSAMPLE_CTX * bins words */
	unsigned int counts;
	/* every sample by phase (the denominator of each phase's kernel share),
	the kernel's by phase, and those outside the binned span */
	volatile unsigned int phase_samples[KSAMPLE_PHASES];
	volatile unsigned int phase_kernel[KSAMPLE_PHASES];
	volatile unsigned int far;
	/* the last kernel PCs outside the span, a ring */
	volatile unsigned int far_pc[KSAMPLE_FAR];
	volatile unsigned int far_next;
	/* the kernel's module list: how many, how many times read, the last
	error (0 none) */
	volatile unsigned int modules, module_reads;
	volatile int module_error;
	struct ksample_module module[KSAMPLE_MODULES];
	/* sceKernelGetThreadId's answer inside the sampler's interrupt (asked
	once) and in the sampler's thread: the cause of the first run's refusals */
	volatile int kid_interrupt, kid_thread;
	/* the prx's: the thread that names the phases */
	volatile int game_uid;
	/* the game's waiter slots (claimed under the game's lock; a slot whose
	thread has ended is freed by the game's heartbeat, its uid negated, and
	taken again by the next claim), how many were freed, and the first
	free's reason (the refer call's error, or the thread's status) */
	volatile unsigned int waiters, waiters_full, waiters_freed;
	volatile int freed_reason;
	struct ksample_waiter waiter[KSAMPLE_WAITERS];
	/* the prx's: every sample by phase and ctx; the main thread's wait sites
	and, by phase, every sample and the kernel's while it waited there */
	volatile unsigned int ctx_samples[KSAMPLE_PHASES][KSAMPLE_CTX];
	volatile unsigned int sites, sites_full;
	volatile unsigned int site_pc[KSAMPLE_SITES];
	volatile unsigned int site_all[KSAMPLE_PHASES][KSAMPLE_SITES];
	volatile unsigned int site_kernel[KSAMPLE_PHASES][KSAMPLE_SITES];
	/* the game's: 1 asks for the interrupt counts (ksamplenointr: 0). The
	prx's: the interrupt numbers with a handler, each one's calls and
	handler clocks (as the interrupt manager keeps them) by phase, the
	first error, and how many times each phase was entered */
	unsigned int want_intr;
	volatile unsigned int intr_sources;
	volatile int intr_error;
	volatile unsigned int intr_no[KSAMPLE_INTRS];
	volatile unsigned int intr_calls[KSAMPLE_PHASES][KSAMPLE_INTRS];
	volatile unsigned long long intr_clock[KSAMPLE_PHASES][KSAMPLE_INTRS];
	volatile unsigned int phase_entries[KSAMPLE_PHASES];
};

#endif
