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
