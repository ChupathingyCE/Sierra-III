/* B41's proper fix: the game thread puts itself to sleep (keep word
"sleepready", on by default; "sleepreadyoff" takes the old way;
docs/b41_sleep_design.md). The stopgap (psp_sleepguard.c) still holds the
switch off unless "sleepallowed", until a real sleep has passed by hand.

Without it, the power callback's thread stops the Media Engine and gives the
volatile partition back while the game thread and the GE's forwarding thread
go on: the ME is stopped wherever its frame is, the GE is left in a list the
ME was feeding, and the game may still write into the partition the
firmware saves the GE's and the ME's eDRAM into as the PSP goes to sleep.

With it, the callback only asks. The game thread, at its next point with no
frame of its own open (a window's start, a load's frame), finishes what the
GE layer has out (psp_ge_sleep_quiet), stops the ME with no job out, gives
the partition back and waits for the wake; then it takes the partition
again, restarts the ME ("mewake", as before) and goes on, and its next list
is a new one. Every wait on the way has a bound. If the game thread hasn't
stopped within a second (a long load), the callback takes the old way.

"sleepdryN": every N seconds of game time the same stop and wake, without a
sleep: the ME held as a sleep holds it (mewaketest's way), and the partition
filled with a pattern for the 2 s the game is stopped, then checked, so a
writer that kept going shows as words changed. For the emulator and the rig,
which have no hand on the power switch.

"powerflags": every power callback's flags in the log (which ones arrive
while the stopgap holds the switch off decides how the sleep is heard).

The stall reporter: each wait in psp_sleep.h's list counted as it begins
and ends, and looked at by the heartbeat (every 5 s). The same wait found on
two looks running is named; on the third its queue is stopped
(sceGeBreak), so its waiter returns and the log says where it was. */

#include <pspkernel.h>
#include <pspge.h>
#include <pspthreadman.h>
#include <psppower.h>
#include <pspsuspend.h>
#include <stdio.h>
#include <string.h>
#include "psp_me.h"
#include "psp_sleep.h"

extern int psp_keep(const char *word);
extern int psp_keep_number(const char *prefix, int fallback);
extern void platform_log(const char *format, ...);
extern void psp_io_reopen_all(int shift, char *line, int size);
void psp_ge_sleep_quiet(char *line, int size);
void psp_tex_suspend(void);
void psp_tex_resume(void);
void psp_geo_suspend(void);
void psp_geo_resume(void);

PSP_MAIN_SIDE volatile unsigned int psp_wait_count[PSP_WAITS] = { 0 };
PSP_MAIN_SIDE volatile int psp_sleep_due = 0;

#define DUE_SLEEP 1
#define DUE_DRY 2

/* the callback's ask and the game thread's answer: idle (or withdrawn at the
callback's timeout); asked (the callback waits); taken (the game thread is
stopping); parked (stopped: the callback may let the sleep go on) */
enum { SLEEP_IDLE, SLEEP_ASKED, SLEEP_TAKEN, SLEEP_PARKED };
static PSP_MAIN_SIDE volatile int sleep_state = SLEEP_IDLE;
/* set by the game thread for as long as it waits for the wake, whatever the
handshake's state: a wake releases it even after a second ask or the old
way (Sheepdog01's fourth sleep, 2026-10-29: a second slide while it waited) */
static PSP_MAIN_SIDE volatile int game_parked = 0;
static PSP_MAIN_SIDE int dry_double = 0;
static PSP_MAIN_SIDE int ready = 0, flags_logged = 0, stall_report = 1;
static PSP_MAIN_SIDE long dry_every = 0;
static PSP_MAIN_SIDE SceUID wake_flag = -1;
static PSP_MAIN_SIDE unsigned long sleeps_ready = 0, sleeps_old_way = 0, dry_sleeps = 0, dry_words_changed = 0;
static const char *const wait_names[PSP_WAITS] = {
	"show_picture's GE end", "show2_settle's show", "show2_list_drop's empty list", "a load frame's GE end",
	"the ME's restart",
};

#define DRY_PARK_US 2000000
/* how long the callback waits for the game thread to stop: a frame at the
slowest is under 200 ms; a load's frames come a few a second */
#define ASK_MS 1000

void psp_sleep_start(void)
{
	ready = psp_keep("sleepready") != 0;
	dry_every = (long)psp_keep_number("sleepdry", 0) * 30;
	flags_logged = psp_keep("powerflags") != 0;
	dry_double = dry_every && psp_keep("sleepdrydouble");
	stall_report = !psp_keep("stallreportoff");
	if (ready || dry_every)
		wake_flag = sceKernelCreateEventFlag("halo wake", 0, 0, NULL);
	if (ready || dry_every || flags_logged)
		platform_log("sleep: %s%s%s%s (B41's fix; the stopgap's switch hold %s)", ready ? "sleepready " : "",
			dry_every ? "sleepdry " : "", dry_double ? "sleepdrydouble " : "", flags_logged ? "powerflags" : "",
			psp_keep("sleepallowed") ? "off: sleepallowed" : "still on unless sleepallowed");
}

/* every tick (psp_platform.c, beside exitatN): sleepdryN's moment */
void psp_sleep_tick(long ticks)
{
	if (dry_every > 0 && ticks > 0 && ticks % dry_every == 0)
		psp_sleep_due |= DUE_DRY;
}

void psp_sleep_power_flags(int flags)
{
	if (flags_logged)
		platform_log("power: callback flags %08x (switch %d, hold %d, standby %d, resume complete %d, resuming %d, suspending %d)",
			(unsigned int)flags, !!(flags & PSP_POWER_CB_POWER_SWITCH), !!(flags & PSP_POWER_CB_HOLD_SWITCH),
			!!(flags & PSP_POWER_CB_STANDBY), !!(flags & PSP_POWER_CB_RESUME_COMPLETE), !!(flags & PSP_POWER_CB_RESUMING),
			!!(flags & PSP_POWER_CB_SUSPENDING));
}

/* the power callback's thread, PSP_POWER_CB_SUSPENDING: 1 when the game
thread has stopped itself and the sleep may go on; 0 for the old way */
int psp_sleep_suspending(void)
{
	unsigned int interrupts;
	int waited;

	if (!ready || wake_flag < 0)
		return 0;
	/* asked again while stopped (a second slide of the switch): the stop
	holds, and the wake already owed releases it */
	if (game_parked || sleep_state == SLEEP_PARKED)
	{
		platform_log("sleep: asked again while stopped; the stop holds");
		return 1;
	}
	interrupts = sceKernelCpuSuspendIntr();
	if (sleep_state == SLEEP_IDLE)
	{
		sceKernelClearEventFlag(wake_flag, 0);
		sleep_state = SLEEP_ASKED;
	}
	sceKernelCpuResumeIntr(interrupts);
	psp_sleep_due |= DUE_SLEEP;
	for (waited = 0; sleep_state != SLEEP_PARKED && waited < ASK_MS; waited++)
		sceKernelDelayThread(1000);
	if (sleep_state == SLEEP_PARKED)
	{
		sleeps_ready++;
		platform_log("sleep: the game stopped itself in %d ms; the PSP may sleep", waited);
		return 1;
	}
	/* withdrawn, unless the game thread has taken it: then it is stopping,
	and gets one more second */
	interrupts = sceKernelCpuSuspendIntr();
	if (sleep_state == SLEEP_ASKED)
		sleep_state = SLEEP_IDLE;
	sceKernelCpuResumeIntr(interrupts);
	if (sleep_state != SLEEP_IDLE)
	{
		for (waited = 0; sleep_state != SLEEP_PARKED && waited < ASK_MS; waited++)
			sceKernelDelayThread(1000);
		if (sleep_state == SLEEP_PARKED)
		{
			sleeps_ready++;
			return 1;
		}
	}
	psp_sleep_due &= ~DUE_SLEEP;
	sleeps_old_way++;
	platform_log("sleep: the game didn't stop itself in %d ms (state %d); the old way", ASK_MS, sleep_state);
	return 0;
}

/* PSP_POWER_CB_RESUME_COMPLETE: 1 if the game thread is parked for this
sleep (it takes everything back itself); 0 for the old way */
static int release_parked(int reopen)
{
	char line[160];

	if (!game_parked && sleep_state != SLEEP_PARKED)
		return 0;
	/* first, as before: a thread may already be waiting to read. Only after
	a real sleep, which kills the descriptors: in a dry stop they are live,
	and another thread's read may be in flight on one */
	if (reopen && !psp_keep("noreopen"))
	{
		psp_io_reopen_all(0, line, sizeof(line));
		platform_log("sleep: awake; %s", line);
	}
	sceKernelSetEventFlag(wake_flag, 1);
	return 1;
}

int psp_sleep_resumed(void)
{
	return release_parked(1);
}

/* sleepdryN: the partition filled for the stop and checked after it. The
firmware would use it while the PSP sleeps, so any word the game's side
changes meanwhile would have broken what the firmware kept there */
static unsigned int scribble_word(unsigned int k)
{
	return 0xB4100000u ^ (k * 2654435761u);
}

/* "sleepdrydouble" (with sleepdryN): the dry stop parked as a real sleep's
is, and a thread standing in for the power callback asks again while it is
parked (a second slide of the switch), then sends the wake */
static int dry_double_thread(SceSize args, void *argp)
{
	int kept;

	(void)args; (void)argp;
	sceKernelDelayThread(DRY_PARK_US / 4);
	kept = psp_sleep_suspending();
	platform_log("sleepdrydouble: a second ask while stopped: %s", kept ? "the stop held" : "the old way (the bug)");
	sceKernelDelayThread(DRY_PARK_US / 4);
	if (!release_parked(0))
		platform_log("sleepdrydouble: the wake found no stop to release (the bug)");
	return 0;
}

static void dry_wait(void)
{
	SceUInt timeout = 10000000;
	SceUID thread;
	int result;

	if (!dry_double)
	{
		sceKernelDelayThread(DRY_PARK_US);
		return;
	}
	sceKernelClearEventFlag(wake_flag, 0);
	game_parked = 1;
	sleep_state = SLEEP_PARKED;
	thread = sceKernelCreateThread("halo sleepdrydouble", dry_double_thread, 0x11, 0x2000, 0, NULL);
	if (thread >= 0)
		sceKernelStartThread(thread, 0, NULL);
	result = sceKernelWaitEventFlag(wake_flag, 1, PSP_EVENT_WAITOR | PSP_EVENT_WAITCLEAR, NULL, &timeout);
	game_parked = 0;
	if (result < 0)
		platform_log("sleepdrydouble: no wake released the stop within 10 s (%08x)", (unsigned int)result);
	if (thread >= 0)
	{
		SceUInt join = 2000000;

		sceKernelWaitThreadEnd(thread, &join);
		sceKernelDeleteThread(thread);
	}
}

static void dry_park(void)
{
	void *block = NULL;
	int size = 0;
	unsigned int words, k, changed = 0, first[4] = { 0 }, seen[4] = { 0 };
	volatile unsigned int *uncached;
	unsigned int *cached;

	if (sceKernelVolatileMemTryLock(0, &block, &size) != 0 || !block || size <= 0)
	{
		platform_log("sleepdry: the partition couldn't be taken for the check (the stop goes on unchecked)");
		sceKernelDelayThread(DRY_PARK_US);
		return;
	}
	cached = (unsigned int *)block;
	uncached = (volatile unsigned int *)(0x40000000u | (unsigned int)block);
	words = (unsigned int)size / 4;
	for (k = 0; k < words; k++)
		cached[k] = scribble_word(k);
	sceKernelDcacheWritebackInvalidateAll();
	dry_wait();
	/* what any thread of the main CPU wrote meanwhile reaches RAM first */
	sceKernelDcacheWritebackInvalidateAll();
	for (k = 0; k < words; k++)
		if (uncached[k] != scribble_word(k))
		{
			if (changed < 4)
			{
				first[changed] = k * 4;
				seen[changed] = uncached[k];
			}
			changed++;
		}
	sceKernelVolatileMemUnlock(0);
	dry_words_changed += changed;
	if (changed)
		platform_log("sleepdry: %u words of the partition changed while stopped (first at +%06x %08x, +%06x %08x, +%06x %08x, +%06x %08x)",
			changed, first[0], seen[0], first[1], seen[1], first[2], seen[2], first[3], seen[3]);
	else
		platform_log("sleepdry: the partition (%u KB) unchanged while stopped", (unsigned int)size / 1024);
}

/* the game thread, at a window's start or a load's frame (psp_ge.c) */
void psp_sleep_point(void)
{
	unsigned int interrupts, start, quiet_us, park_us;
	int due, real, dry;
	char line[256];

	if (PSP_ME_WORK())
		return;
	interrupts = sceKernelCpuSuspendIntr();
	due = psp_sleep_due;
	psp_sleep_due = 0;
	real = sleep_state == SLEEP_ASKED;
	if (real)
		sleep_state = SLEEP_TAKEN;
	sceKernelCpuResumeIntr(interrupts);
	dry = !real && (due & DUE_DRY);
	if (!real && !dry)
		return;
	start = sceKernelGetSystemTimeLow();
	/* nothing of the GE layer's out: the frame out with the ME collected and
	shown, the next list ended empty, the GE at the end of its queue */
	psp_ge_sleep_quiet(line, sizeof(line));
	/* the ME with no job out: it halts at its next look */
	if (dry)
		psp_me_wake_test_sleep();
	else
		psp_me_sleep_stop();
	/* the caches in the partition forgotten and the partition given back,
	by the one thread that writes them */
	psp_geo_suspend();
	psp_tex_suspend();
	quiet_us = sceKernelGetSystemTimeLow() - start;
	platform_log("sleep: %s: stopped in %u us; %s", dry ? "sleepdry" : "sleepready", quiet_us, line);
	start = sceKernelGetSystemTimeLow();
	if (dry)
	{
		dry_sleeps++;
		dry_park();
	}
	else
	{
		/* until the wake: a sleep the firmware called off never sends one, so
		after a minute the game goes on as after a wake */
		SceUInt timeout = 60000000;
		int result;

		game_parked = 1;
		sleep_state = SLEEP_PARKED;
		result = sceKernelWaitEventFlag(wake_flag, 1, PSP_EVENT_WAITOR | PSP_EVENT_WAITCLEAR, NULL, &timeout);
		game_parked = 0;
		if (result < 0)
			platform_log("sleep: no wake within a minute of stopping (%08x): going on as after one", (unsigned int)result);
	}
	park_us = sceKernelGetSystemTimeLow() - start;
	start = sceKernelGetSystemTimeLow();
	psp_tex_resume();
	psp_geo_resume();
	/* the ME's restart, as the old way's next frame end would ask it */
	psp_me_wake_request();
	psp_me_wake_poll();
	sleep_state = SLEEP_IDLE;
	platform_log("sleep: %s: going on after %u ms stopped; back in %u us (%lu dry, %lu ready, %lu the old way, %lu words changed)",
		dry ? "sleepdry" : "sleepready", park_us / 1000, sceKernelGetSystemTimeLow() - start, dry_sleeps,
		sleeps_ready, sleeps_old_way, dry_words_changed);
}

/* the heartbeat, every 5 s */
void psp_sleep_stall_check(void)
{
	static unsigned int last[PSP_WAITS], looks[PSP_WAITS];
	int k;

	if (!stall_report)
		return;
	for (k = 0; k < PSP_WAITS; k++)
	{
		unsigned int count = psp_wait_count[k];

		if (!(count & 1) || count != last[k])
		{
			last[k] = count;
			looks[k] = 0;
			continue;
		}
		looks[k]++;
		if (looks[k] == 1)
			platform_log("stall: a thread has waited in %s for over 5 s (B41's reporter)", wait_names[k]);
		else if (looks[k] == 2 && k != PSP_WAIT_ME_RESTART)
		{
			PspGeBreakParam unused;
			int stopped = sceGeBreak(1, &unused);

			platform_log("stall: still in %s after 10 s: the GE's queue stopped (%08x) so the wait returns",
				wait_names[k], (unsigned int)stopped);
		}
	}
}
