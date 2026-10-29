/* psp_sleep.h - B41: a sleep the game thread is ready for (psp_sleep.c;
docs/b41_sleep_design.md) */
#ifndef PSP_SLEEP_H
#define PSP_SLEEP_H

/* The waits with no time limit of their own that a sleep can leave waiting
for good (a list the GE lost, an ME that never answers). Each is counted as
it begins and as it ends, so its count is odd while a thread is in it; the
heartbeat names one it finds odd at the same count on two looks running,
and on the next look stops the GE's queue so the waiter returns
(psp_sleep_stall_check) */
enum
{
	PSP_WAIT_SHOW_SYNC,    /* show_picture: the GE's end of the frame */
	PSP_WAIT_SHOW_SETTLE,  /* show2_settle: the show the forwarding thread was asked for */
	PSP_WAIT_LIST_DROP,    /* show2_list_drop: an empty list's end */
	PSP_WAIT_LOADING_SYNC, /* psp_ge_loading_frame: a load's frame */
	PSP_WAIT_ME_RESTART,   /* psp_me_wake_poll: the ME's restart, in kernel mode */
	PSP_WAITS
};
extern volatile unsigned int psp_wait_count[PSP_WAITS];
#define PSP_WAIT_BEGIN(site) (psp_wait_count[site]++)
#define PSP_WAIT_END(site) (psp_wait_count[site]++)

/* non-zero when the game thread has a sleep to carry out (asked by the power
callback with "sleepready", or sleepdryN's): at its next quiet point, a
window's start or a load's frame, where it has no frame of its own open */
extern volatile int psp_sleep_due;
void psp_sleep_point(void);
static inline void psp_sleep_check(void)
{
	if (psp_sleep_due)
		psp_sleep_point();
}

void psp_sleep_start(void);
void psp_sleep_tick(long ticks);
void psp_sleep_power_flags(int flags);
int psp_sleep_suspending(void);
int psp_sleep_resumed(void);
void psp_sleep_stall_check(void);

#endif
