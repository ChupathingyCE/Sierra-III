/* psp_me.h - jobs on the Media Engine (psp_me.c, build 87) */
#ifndef PSP_ME_H
#define PSP_ME_H

#define ME_SLOTS 3
#define ME_SLOT_AUDIO 0
#define ME_SLOT_RENDER 1
#define ME_SLOT_PROBE 2 /* build 115: the heartbeat's eDRAM probe (edramcheck) */

#define ME_JOB_ARGS 12
#define ME_JOB_RESULTS 8

#define ME_JOB_AUDIO 1
#define ME_JOB_CLASSIFY 2
#define ME_JOB_ANGLE 3
#define ME_JOB_GE 4 /* build 112: a run of the GE layer's waiting calls (psp_ge.c, "gemeon") */
#define ME_JOB_EDRAM 5 /* build 115: fill or check a stretch of the ME's eDRAM (edramcheck) */
/* 6: ME_JOB_SLANT, a ring job (below) */

int psp_me_start(void);
int psp_me_running(void);
void psp_me_stop(const char *why);
int psp_me_run(int which, unsigned int type, const unsigned int *args, int arg_count, unsigned int *results,
	unsigned int first_look_us);
void psp_me_handover(int which, unsigned long us);
void psp_me_report(void);
float psp_me_busy_percent(void);
void psp_me_edram_probe(void);
/* build 118: a job handed over without waiting, then finished (gemeoverlap) */
int psp_me_submit(int which, unsigned int type, const unsigned int *args, int arg_count);
int psp_me_finish(int which, unsigned int *results);

/* sleep and wake: the power callback stops the ME as the PSP sleeps and asks
for a restart once it is awake; with "mewake" the main thread's frame end
carries the restart out, and otherwise the ME stays stopped */
void psp_me_sleep_stop(void);
void psp_me_wake_request(void);
void psp_me_wake_poll(void);
/* "mewaketest": a sleep's hold on the ME, without a sleep */
void psp_me_wake_test_sleep(void);
/* the exit (HOME, the game's own): the ME stopped, and held in reset if it
may still be running, before the firmware takes the PSP back */
void psp_me_exit(const char *why);

/* The job ring (ME phase 1, keep word "meslant" turns it on): jobs handed
to the ME without a slot of their own, collected later. Each job is a leaf:
everything it reads is named at the push and stays unchanged until the wait;
it writes only its output (whole 64-byte lines of its own) and its results.
The ring serves one producer thread, the main CPU's renderer. */
#define ME_RING_ENTRIES 8 /* a power of two */
#define ME_RING_ARGS 16
#define ME_RING_INPUTS 3
#define ME_RING_RESULTS 11
#define ME_JOB_SLANT 6 /* angle_sort's bucket loop over part of a draw (psp_ge.c) */
#define ME_JOB_RINGTEST 7 /* "meringtest": the ring's own check (psp_me_ring_test) */
#define ME_JOB_PERC_RAYS 8 /* "meraysperc": perception's queued rays (actor_perception.c, psp_fixups.py FIXES_122) */
#define ME_JOB_POSES 9 /* next tick's base poses (objects.c, psp_fixups.py FIXES_131 #poseahead) */
/* an input of this many bytes, from NULL: the job may read anywhere in main
RAM (the level, the objects), so the main CPU writes back its whole cache
before the push (psp_me_writeback(NULL, ME_RING_ALL_OF_RAM)) and the ME drops
its whole cache before the job */
#define ME_RING_ALL_OF_RAM 0x01000000u
/* the last argument of a ring job, non-zero: an earlier job of the same
hand-over (pushed just before it, with nothing written by the main CPU in
between that this job reads) has already made this job's inputs fresh on the
ME, so the ME doesn't drop them again (psp_me_push_perc_rays) */
#define ME_RING_ARG_CONTINUES (ME_RING_ARGS - 1)

struct psp_me_job
{
	unsigned int type; /* ME_JOB_... */
	unsigned int args[ME_RING_ARGS];
	/* what the job reads from main RAM: the ME drops its own copies of these
	before the job. The main CPU writes back what it wrote (psp_me_writeback) */
	const void *input[ME_RING_INPUTS];
	unsigned int input_bytes[ME_RING_INPUTS];
	/* what the job writes besides its results: 64-byte aligned, whole lines,
	read by the main CPU only after the wait */
	void *output;
	unsigned int output_bytes;
};

int psp_me_ring_ready(void);
void psp_me_writeback(const void *start, unsigned int bytes);
int psp_me_push(const struct psp_me_job *job, unsigned int *ticket);
int psp_me_wait(unsigned int ticket, unsigned int *results);
/* a pushed job collected later: collect runs before the ring would reuse its entry */
void psp_me_hold(unsigned int ticket, void (*collect)(void));
void psp_me_ring_test(void);
/* a long ring job's step, on the ME: the debug stash notes it, and an audio
block waiting is mixed now (audio comes before every other job). Nothing
anywhere else */
void psp_me_ring_step(unsigned int step);

/* build 118: 1 on the Media Engine. me-core starts it on a stack in its own
eDRAM ($sp 0x80200000 or 0x80400000, me-core.h), and every thread of the main
CPU runs on a stack in main RAM, from 0x08000000 up. So code both CPUs run
(the GE layer's, psp_ge.c) can tell where it is without a shared word: a word
both CPUs write would share a cache line between them */
static inline int psp_on_me(void)
{
	unsigned int sp;

	__asm__ ("move %0, $sp" : "=r"(sp));
	return (sp & 0x1FFFFFFFu) < 0x01000000u;
}

/* build 118: a variable of the GE layer's or the texture cache's files that
the main CPU writes while the Media Engine works (gemeoverlap: the recorder's
words, the heartbeat's windows, the see-through level triangles' wall
answers). psp_build.py's link script puts these together on cache lines of
their own, away from the ME's variables of the same files. A static gets it
before its type: static PSP_MAIN_SIDE int x = 0; */
#define PSP_MAIN_SIDE __attribute__((section(".data.psp_main_side")))

/* work that is (or, in the emulator, stands for) the Media Engine's: on the
ME, or marked as its by psp_me_context (gemesim: the main CPU runs it) */
extern volatile unsigned int psp_me_context;
#define PSP_ME_WORK() (psp_me_context != 0 || psp_on_me())

#endif
