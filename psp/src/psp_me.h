/* psp_me.h - jobs on the Media Engine (psp_me.c, build 87) */
#ifndef PSP_ME_H
#define PSP_ME_H

#define ME_SLOTS 2
#define ME_SLOT_AUDIO 0
#define ME_SLOT_RENDER 1

#define ME_JOB_ARGS 12
#define ME_JOB_RESULTS 8

#define ME_JOB_AUDIO 1
#define ME_JOB_CLASSIFY 2
#define ME_JOB_ANGLE 3

int psp_me_start(void);
int psp_me_running(void);
void psp_me_stop(const char *why);
int psp_me_run(int which, unsigned int type, const unsigned int *args, int arg_count, unsigned int *results,
	unsigned int first_look_us);
void psp_me_handover(int which, unsigned long us);
void psp_me_report(void);
float psp_me_busy_percent(void);

#endif
