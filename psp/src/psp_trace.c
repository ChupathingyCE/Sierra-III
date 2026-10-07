/* psp_trace.c - the load trace (build 96), the blackbox (build 98)

Why: Sheepdog01's b95 runs 1 and 2 on the PSP-1000 died loading The Library, just
after the texture pack opened, with the ME on and off: the log stopped, the
heartbeat thread (which outranks the game) stopped with it, no crash.txt was
written, and about 10 s later the PSP powered itself off. The emulator loads
the same files with the same settings. A user-mode fault would have reached
the crash handler; this looks like the kernel or the bus giving out, which
user code reaches through system calls. So from the map's geometry pack
opening until two seconds of play, every Memory Stick open, seek and read,
every DMA copy, every GE list and display change is logged before it is made
and after it returns, with the breadcrumb steps between them. Start-up log
lines go to the file at once, so the last line is the last call made.

The calls are wrapped at link time (psp_build.py's --wrap list); the log's
own file is passed through untraced. */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspdebug.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void platform_log(const char *format, ...);
int psp_keep(const char *name);

volatile int psp_trace = 0;
static volatile int trace_lines = 0, trace_busy = 0;
#define TRACE_LIMIT 400 /* The Library takes 66 to its first frame; Blood Gulch's texture preload would take thousands */

void psp_trace_line(const char *format, ...)
{
	char text[200];
	va_list arguments;
	int n;

	if (!psp_trace || trace_busy)
		return;
	if (psp_trace == 1 && trace_lines >= TRACE_LIMIT)
	{
		psp_trace = 0;
		platform_log("trace: %d lines, stopped", TRACE_LIMIT);
		return;
	}
	trace_busy = 1;
	n = ++trace_lines;
	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	platform_log("trace %d [%x] %u: %s", n, (unsigned)sceKernelGetThreadId(), (unsigned)sceKernelGetSystemTimeLow() / 1000, text);
	trace_busy = 0;
}

void psp_trace_start(const char *why)
{
	/* build 102: only with "blackbox". On by default (b96-b101) it wrote
	hundreds of lines at once at every map load: Blood Gulch's texture
	preload took 9 s instead of 1.2 (Sheepdog01's b101 run 3) */
	if (psp_trace || !psp_keep("blackbox"))
		return;
	psp_trace = 1;
	psp_trace_line("on (%s)", why);
}

/* build 98: "blackbox" (keep.txt, the launch menu): the trace goes on in
play, without the steps (Memory Stick, DMA, GE list and display calls: a few
lines a frame), and the heartbeat saves the log four times a second, so a
freeze that takes the whole PSP down leaves its last quarter second. Sheepdog01's
b97 run 1 froze in The Library's first fight with nothing in the log after
the last 5 s batch */
void psp_trace_stop(const char *why)
{
	if (psp_trace != 1)
		return;
	if (psp_keep("blackbox"))
	{
		extern void psp_log_batch_now(void);

		psp_trace = 2;
		platform_log("trace: %d lines to %s; blackbox: tracing on in play", trace_lines, why);
		psp_log_batch_now();
		return;
	}
	psp_trace = 0;
	platform_log("trace: off after %d lines (%s)", trace_lines, why);
}


/* build 101: "crumbs". Every crash or freeze in The Library left nothing:
the crash handler's own test (b100 run 2, a store to address 4) froze the
PSP exactly like the real freezes, no report on the screen, no crash.txt,
no sound, no input, then the power-off. So whatever runs at a fault doesn't
get as far as drawing. Instead the game draws its own last steps into the
picture on the screen as it goes (four rows at the bottom, in turn, each
with a running number): what the frozen screen shows is the last thing it
started. Slow (an uncached write per pixel), so only with the keep word */
volatile unsigned int psp_shown_buffer = 0;
static int crumbs_on = -1;
static unsigned int crumb_count = 0;

/* armed at the game's first frame on the screen (keep.txt is read by then;
the launch menu's pictures don't count) */
void psp_crumbs_arm(void)
{
	crumbs_on = psp_keep("crumbs");
}

int psp_crumb_mark(const char *what, const char *detail)
{
	char text[69];

	if (crumbs_on <= 0 || !psp_shown_buffer)
		return 0;
	crumb_count++;
	{
		int length = snprintf(text, 49, "%06u %s %s", crumb_count % 1000000, what ? what : "", detail ? detail : "");

		/* a fixed 48 columns, the last crumb on this row written over */
		if (length < 0)
			length = 0;
		if (length > 48)
			length = 48;
		memset(text + length, ' ', 48 - length);
		text[48] = 0;
	}
	pspDebugScreenSetBase((u32 *)(0x40000000u | (psp_shown_buffer & 0x0FFFFFFFu)));
	pspDebugScreenSetXY(0, 30 + (int)(crumb_count & 3));
	pspDebugScreenPuts(text);
	return 0;
}

/* the file names behind the descriptors, for the read lines */
#define TRACE_FILES 64
static char trace_names[TRACE_FILES][40];

static const char *name_of(SceUID fd)
{
	return fd >= 0 && fd < TRACE_FILES && trace_names[fd][0] ? trace_names[fd] : "?";
}

/* build 100: one thread on the Memory Stick at a time. Every freeze in
The Library came with the game's loader thread reading the map while the
main thread read textures or the heartbeat wrote the log (Blood Gulch
preloads everything and never does this, the emulator's file system has no
timing at all). Each wrapped call below takes this lock (re-entrant, for
the log's own writes from inside a traced call); keep word "stickparallel"
leaves it off */
static SceUID stick_lock = -1;

/* build 102: where the Memory Stick's time goes, by thread: the calls'
own time (lock or no lock: the stick does one request at a time, so a
thread's synchronous read also waits for any other thread's read in the
driver; an asynchronous read's transfer is not in it) and, with the lock,
the time spent waiting for it. Sheepdog01's b101 run 2 waited 59 s for the lock
over 2,379 waits, 0.9 s at worst, and which thread waited decides whether
that was a stall on the screen (the game thread) or a late block (the
loader, the sound) */
#define STICK_THREADS 8
static struct stick_thread
{
	SceUID id;
	int depth, locked;
	unsigned int calls, waits, start;
	unsigned long long in_us, wait_us;
	unsigned int worst_in_us, worst_wait_us;
} stick_threads[STICK_THREADS], stick_spare;
static volatile int stick_enabled = -1;
int psp_keep_loaded(void);

static struct stick_thread *stick_thread_of(SceUID me)
{
	int k;

	for (k = 0; k < STICK_THREADS; k++)
		if (stick_threads[k].id == me)
			return &stick_threads[k];
	for (k = 0; k < STICK_THREADS; k++)
		if (!stick_threads[k].id)
		{
			/* a slot is only ever taken, never given back: at worst two
			threads race for one and one of them counts in the other's */
			stick_threads[k].id = me;
			return &stick_threads[k];
		}
	return &stick_spare;
}

static void stick_take(void)
{
	SceUID me = sceKernelGetThreadId();
	struct stick_thread *thread = stick_thread_of(me);
	unsigned int start = sceKernelGetSystemTimeLow();

	/* re-entrant: the log's own writes from inside a traced call */
	if (thread->depth++ > 0)
		return;
	/* asked each time until keep.txt is read */
	if (stick_enabled < 0)
	{
		int on = !psp_keep("stickparallel");

		if (on && stick_lock < 0)
			stick_lock = sceKernelCreateSema("halo stick", 0, 1, 1, NULL);
		if (psp_keep_loaded())
			stick_enabled = on && stick_lock >= 0;
	}
	thread->locked = 0;
	if (stick_enabled != 0 && stick_lock >= 0 && !psp_keep("stickparallel"))
	{
		if (sceKernelPollSema(stick_lock, 1) < 0)
		{
			unsigned int waited;

			sceKernelWaitSema(stick_lock, 1, NULL);
			waited = sceKernelGetSystemTimeLow() - start;
			thread->waits++;
			thread->wait_us += waited;
			if (waited > thread->worst_wait_us)
				thread->worst_wait_us = waited;
			start = sceKernelGetSystemTimeLow();
		}
		thread->locked = 1;
	}
	thread->start = start;
}

static void stick_give(void)
{
	struct stick_thread *thread = stick_thread_of(sceKernelGetThreadId());
	unsigned int took;

	if (thread->depth <= 0 || --thread->depth > 0)
		return;
	took = sceKernelGetSystemTimeLow() - thread->start;
	thread->calls++;
	thread->in_us += took;
	if (took > thread->worst_in_us)
		thread->worst_in_us = took;
	if (thread->locked)
	{
		thread->locked = 0;
		sceKernelSignalSema(stick_lock, 1);
	}
}

/* the heartbeat's report: one line, the threads by name */
void psp_stick_report(void)
{
	char line[400];
	int k, length;

	length = snprintf(line, sizeof(line), "stick (%s):", stick_enabled > 0 && !psp_keep("stickparallel") ? "one thread at a time" : "parallel");
	for (k = 0; k < STICK_THREADS && length < (int)sizeof(line) - 1; k++)
	{
		struct stick_thread *thread = &stick_threads[k];
		SceKernelThreadInfo info;
		const char *name = "?";

		if (!thread->id || !thread->calls)
			continue;
		memset(&info, 0, sizeof(info));
		info.size = sizeof(info);
		if (sceKernelReferThreadStatus(thread->id, &info) >= 0)
			name = info.name;
		length += snprintf(line + length, sizeof(line) - length, " %s %u calls %llu ms (worst %u)",
			name, thread->calls, thread->in_us / 1000, thread->worst_in_us / 1000);
		if (thread->waits && length < (int)sizeof(line) - 1)
			length += snprintf(line + length, sizeof(line) - length, ", waited %u times %llu ms (worst %u);",
				thread->waits, thread->wait_us / 1000, thread->worst_wait_us / 1000);
		else if (length < (int)sizeof(line) - 1)
			length += snprintf(line + length, sizeof(line) - length, ";");
	}
	platform_log("%s", line);
}

SceUID __real_sceIoOpen(const char *file, int flags, SceMode mode);
SceUID __wrap_sceIoOpen(const char *file, int flags, SceMode mode)
{
	SceUID fd;
	int quiet = !file || strstr(file, "/log/") != NULL;

	if (!quiet)
		psp_trace_line("open %s (flags %x)", file, flags);
	stick_take();
	fd = __real_sceIoOpen(file, flags, mode);
	stick_give();
	if (!quiet)
	{
		if (fd >= 0 && fd < TRACE_FILES)
		{
			const char *slash = strrchr(file, '/');

			snprintf(trace_names[fd], sizeof(trace_names[fd]), "%s", slash ? slash + 1 : file);
		}
		psp_trace_line("open %s = %d", file, (int)fd);
	}
	return fd;
}

int __real_sceIoLseek32(SceUID fd, int offset, int whence);
int __wrap_sceIoLseek32(SceUID fd, int offset, int whence)
{
	int result;

	stick_take();
	result = __real_sceIoLseek32(fd, offset, whence);
	stick_give();

	psp_trace_line("seek %s %d to %08x (%d) = %08x", name_of(fd), (int)fd, (unsigned)offset, whence, (unsigned)result);
	return result;
}

SceOff __real_sceIoLseek(SceUID fd, SceOff offset, int whence);
SceOff __wrap_sceIoLseek(SceUID fd, SceOff offset, int whence)
{
	SceOff result;

	stick_take();
	result = __real_sceIoLseek(fd, offset, whence);
	stick_give();

	psp_trace_line("seek64 %s %d to %08x (%d) = %08x", name_of(fd), (int)fd, (unsigned)offset, whence, (unsigned)result);
	return result;
}

int __real_sceIoRead(SceUID fd, void *data, SceSize size);
int __wrap_sceIoRead(SceUID fd, void *data, SceSize size)
{
	int result;

	psp_trace_line("read %s %d into %08x, %u bytes", name_of(fd), (int)fd, (unsigned)data, (unsigned)size);
	stick_take();
	result = __real_sceIoRead(fd, data, size);
	stick_give();
	psp_trace_line("read %s %d = %d", name_of(fd), (int)fd, result);
	return result;
}

int __real_sceIoReadAsync(SceUID fd, void *data, SceSize size);
int __wrap_sceIoReadAsync(SceUID fd, void *data, SceSize size)
{
	int result;

	psp_trace_line("read async %s %d into %08x, %u bytes", name_of(fd), (int)fd, (unsigned)data, (unsigned)size);
	stick_take();
	result = __real_sceIoReadAsync(fd, data, size);
	stick_give();
	psp_trace_line("read async %s %d started = %d", name_of(fd), (int)fd, result);
	return result;
}

int __real_sceIoClose(SceUID fd);
int __wrap_sceIoClose(SceUID fd)
{
	int traced = fd >= 0 && fd < TRACE_FILES && trace_names[fd][0];
	int result;

	if (traced)
		psp_trace_line("close %s %d", name_of(fd), (int)fd);
	stick_take();
	result = __real_sceIoClose(fd);
	stick_give();
	if (traced)
		trace_names[fd][0] = 0;
	return result;
}

int __real_sceDmacMemcpy(void *dst, const void *src, SceSize size);
int __wrap_sceDmacMemcpy(void *dst, const void *src, SceSize size)
{
	int result;

	psp_trace_line("dma %08x <- %08x, %u bytes", (unsigned)dst, (unsigned)src, (unsigned)size);
	result = __real_sceDmacMemcpy(dst, src, size);
	psp_trace_line("dma = %d", result);
	return result;
}

int __real_sceGeListEnQueue(const void *list, void *stall, int cbid, void *arg);
int __wrap_sceGeListEnQueue(const void *list, void *stall, int cbid, void *arg)
{
	int result;

	psp_trace_line("GE list %08x, stall %08x", (unsigned)list, (unsigned)stall);
	result = __real_sceGeListEnQueue(list, stall, cbid, arg);
	psp_trace_line("GE list = %d", result);
	return result;
}

int __real_sceDisplaySetFrameBuf(void *top, int width, int format, int sync);
int __wrap_sceDisplaySetFrameBuf(void *top, int width, int format, int sync)
{
	int result;

	psp_trace_line("display %08x, width %d, format %d, sync %d", (unsigned)top, width, format, sync);
	psp_shown_buffer = (unsigned int)top;
	result = __real_sceDisplaySetFrameBuf(top, width, format, sync);
	psp_trace_line("display = %d", result);
	return result;
}

int __real_sceIoWrite(SceUID fd, const void *data, SceSize size);
int __wrap_sceIoWrite(SceUID fd, const void *data, SceSize size)
{
	int result;

	stick_take();
	result = __real_sceIoWrite(fd, data, size);
	stick_give();
	return result;
}

int __real_sceIoGetstat(const char *file, SceIoStat *stat);
int __wrap_sceIoGetstat(const char *file, SceIoStat *stat)
{
	int result;

	stick_take();
	result = __real_sceIoGetstat(file, stat);
	stick_give();
	return result;
}
