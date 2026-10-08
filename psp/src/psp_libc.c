/*
PSP_LIBC.C

Small things the PSP's newlib lacks or the port's other platforms provide,
compiled as plain C (no game prefix):
- POSIX calls the Linux platform layer uses: pread/pwrite (the cache
  file thread reads maps with them), sysconf, pause, ftruncate, utimensat;
- the maths functions every port shares (port/include/halo_math.h). The PC
  ports compute them in double precision, bit-identical across machines
  for system link; the PSP computes them in single precision on its FPU
  (lever 1 in the feasibility notes), since it won't play against PCs;
- internet play (p2p.c) and the Android GL host, which the PSP doesn't
  have: they report nothing.
*/

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <malloc.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <pspkernel.h>
#include <pspsysmem.h>
#include <pspsdk.h>
#include <time.h>

/* ---------- POSIX */

static pthread_mutex_t positioned_io_lock = PTHREAD_MUTEX_INITIALIZER;

/* newlib has no pread/pwrite: seek and transfer under one lock, so two
threads' positioned transfers on the same file can't interleave */
ssize_t pread(int descriptor, void *buffer, size_t count, off_t offset)
{
	ssize_t result = -1;

	pthread_mutex_lock(&positioned_io_lock);
	if (lseek(descriptor, offset, SEEK_SET) == offset)
		result = read(descriptor, buffer, count);
	pthread_mutex_unlock(&positioned_io_lock);
	return result;
}

ssize_t pwrite(int descriptor, const void *buffer, size_t count, off_t offset)
{
	ssize_t result = -1;

	pthread_mutex_lock(&positioned_io_lock);
	if (lseek(descriptor, offset, SEEK_SET) == offset)
		result = write(descriptor, buffer, count);
	pthread_mutex_unlock(&positioned_io_lock);
	return result;
}

/* extends a file (the game sizes its save file this way, game_state_xbox.c)
by writing its last byte; newlib can't shrink one, so a shorter length is
left as it is */
int ftruncate(int descriptor, off_t length)
{
	off_t end, position;
	int result = 0;

	pthread_mutex_lock(&positioned_io_lock);
	position = lseek(descriptor, 0, SEEK_CUR);
	end = lseek(descriptor, 0, SEEK_END);
	if (end < 0)
		result = -1;
	else if (length > end)
	{
		char zero = 0;

		if (lseek(descriptor, length - 1, SEEK_SET) != length - 1 || write(descriptor, &zero, 1) != 1)
			result = -1;
	}
	lseek(descriptor, position, SEEK_SET);
	pthread_mutex_unlock(&positioned_io_lock);
	return result;
}

struct timespec;
int utimensat(int directory, const char *path, const struct timespec times[2], int flags)
{
	(void)directory; (void)path; (void)times; (void)flags;
	return 0;
}

int pause(void)
{
	sceKernelSleepThread();
	return -1;
}

long sysconf(int name)
{
	switch (name)
	{
	case _SC_PAGESIZE: return 4096;
	case _SC_PHYS_PAGES: return 24 * 1024 * 1024 / 4096;
	case _SC_AVPHYS_PAGES: return (long)sceKernelTotalFreeMemSize() / 4096;
	default: errno = EINVAL; return -1;
	}
}

void _ReadWriteBarrier(void)
{
	__asm__ __volatile__("" ::: "memory");
}

/* ---------- build 106: condition variables that sleep

pspdev's pthreads (pthreads-embedded) waits on a condition variable by
polling: a look at its semaphore, then sceKernelDelayThread(100), again and
again. So every thread waiting on one (the Xbox shim's events: the cache
file thread between reads, the input thread...) wakes up to 10,000 times a
second. The emulator counted 4,000 wakes a second for the cache file
thread, 5% of the CPU. Below the game thread that only used the time the
game left; the cache file thread now runs above it (psp_platform.c,
psp_cache_thread_priority), where every wake would take the CPU from the
game. These replace pthread_cond_* (psp_build.py links with --wrap): a
count of the waiters and a kernel semaphore they sleep on.

pthread_cond_t is a pointer here; PTHREAD_COND_INITIALIZER is (pointer) -1,
made into a condition at its first use. POSIX lets a waiter wake without a
signal (every caller loops on its own test), which keeps the timed-out case
simple */
struct psp_cond
{
	SceUID sleep;   /* the waiters sleep on it; a signal adds one */
	SceUID guard;   /* a binary semaphore over the count */
	int waiters;    /* waiting and not yet signalled */
};
#define PSP_COND_STATIC ((pthread_cond_t)-1)

static struct psp_cond *cond_new(void)
{
	struct psp_cond *cond = malloc(sizeof(*cond));

	if (!cond)
		return NULL;
	cond->sleep = sceKernelCreateSema("halo cond", 0, 0, 0x7FFFFFFF, NULL);
	cond->guard = sceKernelCreateSema("halo cond guard", 0, 1, 1, NULL);
	cond->waiters = 0;
	if (cond->sleep < 0 || cond->guard < 0)
	{
		if (cond->sleep >= 0)
			sceKernelDeleteSema(cond->sleep);
		if (cond->guard >= 0)
			sceKernelDeleteSema(cond->guard);
		free(cond);
		return NULL;
	}
	return cond;
}

static void cond_free(struct psp_cond *cond)
{
	sceKernelDeleteSema(cond->sleep);
	sceKernelDeleteSema(cond->guard);
	free(cond);
}

/* a statically made condition becomes a real one at its first use; two
threads racing for that make one each and one keeps its own */
static struct psp_cond *cond_get(pthread_cond_t *handle)
{
	struct psp_cond *made;
	int interrupts;

	if (!handle)
		return NULL;
	if (*handle != PSP_COND_STATIC)
		return (struct psp_cond *)*handle;
	made = cond_new();
	if (!made)
		return NULL;
	interrupts = pspSdkDisableInterrupts();
	if (*handle == PSP_COND_STATIC)
	{
		*handle = (pthread_cond_t)made;
		made = NULL;
	}
	pspSdkEnableInterrupts(interrupts);
	if (made)
		cond_free(made);
	return (struct psp_cond *)*handle;
}

int __wrap_pthread_cond_init(pthread_cond_t *handle, const pthread_condattr_t *attributes)
{
	struct psp_cond *cond = cond_new();

	(void)attributes;
	if (!handle)
		return EINVAL;
	if (!cond)
		return ENOMEM;
	*handle = (pthread_cond_t)cond;
	return 0;
}

int __wrap_pthread_cond_destroy(pthread_cond_t *handle)
{
	if (!handle)
		return EINVAL;
	if (*handle != PSP_COND_STATIC && *handle)
		cond_free((struct psp_cond *)*handle);
	*handle = NULL;
	return 0;
}

static int cond_wait(pthread_cond_t *handle, pthread_mutex_t *mutex, const struct timespec *deadline)
{
	struct psp_cond *cond = cond_get(handle);
	int slept;

	if (!cond)
		return EINVAL;
	sceKernelWaitSema(cond->guard, 1, NULL);
	cond->waiters++;
	sceKernelSignalSema(cond->guard, 1);
	pthread_mutex_unlock(mutex);
	if (deadline)
	{
		struct timespec now;
		long long left;
		SceUInt timeout;

		clock_gettime(CLOCK_REALTIME, &now);
		left = (long long)(deadline->tv_sec - now.tv_sec) * 1000000LL + (deadline->tv_nsec - now.tv_nsec) / 1000;
		timeout = left <= 0 ? 0 : left > 0x7FFFFFFFLL ? 0x7FFFFFFF : (SceUInt)left;
		slept = sceKernelWaitSema(cond->sleep, 1, &timeout);
	}
	else
		slept = sceKernelWaitSema(cond->sleep, 1, NULL);
	if (slept < 0)
	{
		/* timed out: out of the count, unless a signal came meanwhile and
		left its wake in the semaphore (taken here: this waiter was the
		one it woke) */
		sceKernelWaitSema(cond->guard, 1, NULL);
		if (sceKernelPollSema(cond->sleep, 1) >= 0)
			slept = 0;
		else if (cond->waiters > 0)
			cond->waiters--;
		sceKernelSignalSema(cond->guard, 1);
	}
	pthread_mutex_lock(mutex);
	return slept < 0 ? ETIMEDOUT : 0;
}

int __wrap_pthread_cond_wait(pthread_cond_t *handle, pthread_mutex_t *mutex)
{
	return cond_wait(handle, mutex, NULL);
}

int __wrap_pthread_cond_timedwait(pthread_cond_t *handle, pthread_mutex_t *mutex, const struct timespec *deadline)
{
	return cond_wait(handle, mutex, deadline);
}

int __wrap_pthread_cond_signal(pthread_cond_t *handle)
{
	struct psp_cond *cond = cond_get(handle);

	if (!cond)
		return EINVAL;
	sceKernelWaitSema(cond->guard, 1, NULL);
	if (cond->waiters > 0)
	{
		cond->waiters--;
		sceKernelSignalSema(cond->sleep, 1);
	}
	sceKernelSignalSema(cond->guard, 1);
	return 0;
}

int __wrap_pthread_cond_broadcast(pthread_cond_t *handle)
{
	struct psp_cond *cond = cond_get(handle);

	if (!cond)
		return EINVAL;
	sceKernelWaitSema(cond->guard, 1, NULL);
	if (cond->waiters > 0)
	{
		sceKernelSignalSema(cond->sleep, cond->waiters);
		cond->waiters = 0;
	}
	sceKernelSignalSema(cond->guard, 1);
	return 0;
}

/* ---------- maths, in single precision */

double halo_sin(double x) { return sinf((float)x); }
double halo_cos(double x) { return cosf((float)x); }
double halo_tan(double x) { return tanf((float)x); }
double halo_asin(double x) { return asinf((float)x); }
double halo_acos(double x) { return acosf((float)x); }
double halo_atan(double x) { return atanf((float)x); }
double halo_atan2(double y, double x) { return atan2f((float)y, (float)x); }
double halo_exp(double x) { return expf((float)x); }
double halo_log(double x) { return logf((float)x); }
double halo_log2(double x) { return log2f((float)x); }
double halo_log10(double x) { return log10f((float)x); }
double halo_pow(double x, double y) { return powf((float)x, (float)y); }

/* ---------- no internet play (p2p.c) */

const unsigned char *p2p_identifier(void) { static unsigned char identifier[16]; return identifier; }
void p2p_hardware_id(char *hex, int size) { if (size > 0) { strncpy(hex, "5053500000000000", size - 1); hex[size - 1] = 0; } }
void p2p_hardware_id_sanitize(char *destination, int size, const char *source)
{ if (size > 0) { strncpy(destination, source ? source : "", size - 1); destination[size - 1] = 0; } }
void p2p_discord_identity(char *id, int id_size, char *name, int name_size)
{ if (id_size > 0) id[0] = 0; if (name_size > 0) name[0] = 0; }
void p2p_discord_sanitize(char *destination, int size, const char *source, int name)
{ (void)name; p2p_hardware_id_sanitize(destination, size, source); }
unsigned long p2p_peer_endpoint_address(unsigned long address) { return address; }
int p2p_outgoing(int stream, int socket, unsigned long *address, unsigned short *port)
{ (void)stream; (void)socket; (void)address; (void)port; return 0; }
int p2p_incoming(int stream, unsigned long *address, unsigned short *port) { (void)stream; (void)address; (void)port; return 0; }
void p2p_socket_port(int socket, int stream, int listening, unsigned short port) { (void)socket; (void)stream; (void)listening; (void)port; }
void p2p_port_taken(int stream, unsigned short port) { (void)stream; (void)port; }
void p2p_socket_closed(int socket, unsigned short port) { (void)socket; (void)port; }
int p2p_broadcast_targets(unsigned short port, unsigned long *addresses, unsigned short *ports, int maximum)
{ (void)port; (void)addresses; (void)ports; (void)maximum; return 0; }
int p2p_send_datagram(unsigned short source_port, unsigned long address, unsigned short port, const void *data, int size)
{ (void)source_port; (void)address; (void)port; (void)data; (void)size; return 0; }
int p2p_broadcast_datagram(unsigned short source_port, unsigned short port, const void *data, int size)
{ (void)source_port; (void)port; (void)data; (void)size; return 0; }
int p2p_peer_address(const unsigned char *peer, unsigned long *address) { (void)peer; (void)address; return 0; }
void p2p_set_game_player_counts(int count, int maximum) { (void)count; (void)maximum; }
const char *p2p_take_clipboard_text(void) { return NULL; }
int p2p_join_invite(const char *text) { (void)text; return 0; }
int p2p_hand_off_invite(void) { return 0; }
void p2p_initialize(unsigned long local_address) { (void)local_address; }

/* ---------- no Android GL host */

int host_gl_has_extension(const char *name) { (void)name; return 0; }
unsigned int host_gl_read_buffer_word(unsigned int buffer, unsigned int offset) { (void)buffer; (void)offset; return 0; }
void host_gl_buffer_write(unsigned int target, unsigned int offset, unsigned int size, const void *data)
{ (void)target; (void)offset; (void)size; (void)data; }
void host_gl_fence_frame(unsigned int slot) { (void)slot; }
void host_gl_wait_frame(unsigned int slot) { (void)slot; }

/* ---------- no TIFF screenshots */

void *TIFFOpen(const char *name, const char *mode) { (void)name; (void)mode; return NULL; }
void TIFFClose(void *tiff) { (void)tiff; }
int TIFFSetField(void *tiff, unsigned int tag, ...) { (void)tiff; (void)tag; return 0; }
int TIFFWriteScanline(void *tiff, void *buffer, unsigned int row, unsigned short sample)
{ (void)tiff; (void)buffer; (void)row; (void)sample; return -1; }

/* ---------- build 105: one thread at a time in newlib's number formatting

pspdev's newlib keeps one shared state for every thread (a single global
_impure_ptr, no per-thread one). Its float-to-text converter (_dtoa_r, under
every %f) keeps scratch memory there and returns a string that the next call
frees. So two threads formatting floats at once corrupt each other: the
heartbeat (priority 0x18) can interrupt the game thread (0x20) in the middle
of a line with %f and format its own. The emulator hit it with two copies
running side by side (a crash in _dtoa_r reading 0x00333033, text used as a
pointer); on the PSP it would be rare and random. The printf engines and
strtod (the same scratch memory) are wrapped (psp_build.py) to take one
lock. It is recursive for the thread that holds it, and after a crash it is
skipped: the crashed thread may hold it, and the crash handler formats text */

#include <stdarg.h>
#include <reent.h>

extern volatile int psp_crashed;
static SceUID format_lock = -1;
static volatile SceUID format_owner = 0;
static volatile int format_depth = 0;

/* called first thing at start-up, while only one thread runs (psp_platform.c) */
void psp_format_lock_init(void)
{
	if (format_lock < 0)
		format_lock = sceKernelCreateSema("halo format", 0, 1, 1, NULL);
}

static int format_take(void)
{
	SceUID self;

	if (format_lock < 0 || psp_crashed)
		return 0;
	self = sceKernelGetThreadId();
	if (format_owner == self)
	{
		format_depth++;
		return 1;
	}
	if (sceKernelWaitSema(format_lock, 1, NULL) < 0)
		return 0;
	format_owner = self;
	format_depth = 1;
	return 1;
}

static void format_give(int taken)
{
	if (!taken)
		return;
	if (--format_depth == 0)
	{
		format_owner = 0;
		sceKernelSignalSema(format_lock, 1);
	}
}

/* build 106: "printfcount" (keep.txt): which format strings the formatting
goes to, the busiest logged by psp_printf_report (the emulator's PC samples
put 4% of b30's tick in the lock taken here) */
#define PRINTF_FORMATS 64
static struct { const char *format; unsigned long calls; } printf_formats[PRINTF_FORMATS];
static int printf_count_on = -1;

static void printf_count(const char *format)
{
	extern int psp_keep(const char *name);
	extern int psp_keep_loaded(void);
	unsigned int slot = ((unsigned int)format >> 2) % PRINTF_FORMATS, probes;

	if (printf_count_on < 0)
	{
		if (!psp_keep_loaded())
			return;
		/* psp_keep formats too: off while it looks */
		printf_count_on = 0;
		printf_count_on = psp_keep("printfcount");
	}
	if (!printf_count_on)
		return;
	for (probes = 0; probes < PRINTF_FORMATS; probes++, slot = (slot + 1) % PRINTF_FORMATS)
		if (!printf_formats[slot].format || printf_formats[slot].format == format)
		{
			printf_formats[slot].format = format;
			printf_formats[slot].calls++;
			return;
		}
}

/* the five busiest formats since the last report, into the log */
void psp_printf_report(void)
{
	extern void platform_log(const char *format, ...);
	int pass, k;

	if (printf_count_on <= 0)
		return;
	for (pass = 0; pass < 5; pass++)
	{
		int best = -1;

		for (k = 0; k < PRINTF_FORMATS; k++)
			if (printf_formats[k].format && printf_formats[k].calls && (best < 0 || printf_formats[k].calls > printf_formats[best].calls))
				best = k;
		if (best < 0)
			break;
		{
			char shown[48];
			int c;

			for (c = 0; c < 47 && printf_formats[best].format[c]; c++)
				shown[c] = printf_formats[best].format[c] < ' ' ? ' ' : printf_formats[best].format[c];
			shown[c] = 0;
			platform_log("printf: %lu calls with \"%s\"", printf_formats[best].calls, shown);
		}
		printf_formats[best].calls = 0;
	}
	for (k = 0; k < PRINTF_FORMATS; k++)
		printf_formats[k].calls = 0;
}

int __real__svfprintf_r(struct _reent *reent, FILE *file, const char *format, va_list arguments);
int __wrap__svfprintf_r(struct _reent *reent, FILE *file, const char *format, va_list arguments)
{
	int taken = format_take(), result;

	printf_count(format);
	result = __real__svfprintf_r(reent, file, format, arguments);

	format_give(taken);
	return result;
}

int __real__vfprintf_r(struct _reent *reent, FILE *file, const char *format, va_list arguments);
int __wrap__vfprintf_r(struct _reent *reent, FILE *file, const char *format, va_list arguments)
{
	int taken = format_take(), result = __real__vfprintf_r(reent, file, format, arguments);

	format_give(taken);
	return result;
}

/* vfprintf, strtod and strtof live in the same objects as the engines they
call, so those calls never pass through the wrappers above: the entry points
themselves are wrapped too (the other printf and scanf calls reach an engine
from another object, and are covered) */
int __real_vfprintf(FILE *file, const char *format, va_list arguments);
int __wrap_vfprintf(FILE *file, const char *format, va_list arguments)
{
	int taken = format_take(), result = __real_vfprintf(file, format, arguments);

	format_give(taken);
	return result;
}

double __real_strtod(const char *text, char **end);
double __wrap_strtod(const char *text, char **end)
{
	int taken = format_take();
	double result = __real_strtod(text, end);

	format_give(taken);
	return result;
}

float __real_strtof(const char *text, char **end);
float __wrap_strtof(const char *text, char **end)
{
	int taken = format_take();
	float result = __real_strtof(text, end);

	format_give(taken);
	return result;
}

double __real__strtod_r(struct _reent *reent, const char *text, char **end);
double __wrap__strtod_r(struct _reent *reent, const char *text, char **end)
{
	int taken = format_take();
	double result = __real__strtod_r(reent, text, end);

	format_give(taken);
	return result;
}

/* ---------- heap accounting (linked with --wrap=malloc,calloc,realloc,memalign):
large requests and every failure are logged, so the log says what the heap
went to */

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void *__real_memalign(size_t alignment, size_t size);
void platform_log(const char *format, ...);

static unsigned long heap_large_total = 0;

/* build 93: who asks for the heap (bytes asked for since start, by the
calling address; frees aren't counted), reported at the first failure and
on psp_heap_report(): psp-addr2line -f -e halo.elf turns the addresses into
functions */
#define HEAP_CALLERS 96
static struct { void *caller; unsigned long bytes, count; } heap_callers[HEAP_CALLERS];
static int heap_caller_count = 0, heap_failed_reported = 0;

static void heap_caller_add(void *caller, size_t size)
{
	int k;

	for (k = 0; k < heap_caller_count; k++)
		if (heap_callers[k].caller == caller)
			break;
	if (k == heap_caller_count)
	{
		if (heap_caller_count >= HEAP_CALLERS)
			return;
		heap_callers[heap_caller_count].caller = caller;
		heap_caller_count++;
	}
	heap_callers[k].bytes += size;
	heap_callers[k].count++;
}

void psp_heap_report(const char *why)
{
	char line[480];
	int length = 0, shown = 0, k, best;
	static unsigned char done[HEAP_CALLERS];
	struct mallinfo info = mallinfo();

	platform_log("heap (%s): %d KB in use, %d KB free inside, %d KB taken from the system; largest callers (bytes asked since start):",
		why, info.uordblks / 1024, info.fordblks / 1024, info.arena / 1024);
	memset(done, 0, sizeof(done));
	while (shown < 16)
	{
		best = -1;
		for (k = 0; k < heap_caller_count; k++)
			if (!done[k] && (best < 0 || heap_callers[k].bytes > heap_callers[best].bytes))
				best = k;
		if (best < 0)
			break;
		done[best] = 1;
		length += snprintf(line + length, sizeof(line) - length, " %p %lu KB (%lu);", heap_callers[best].caller,
			heap_callers[best].bytes / 1024, heap_callers[best].count);
		if (++shown % 8 == 0)
		{
			platform_log("heap callers:%s", line);
			length = 0;
			line[0] = 0;
		}
	}
	if (length)
		platform_log("heap callers:%s", line);
}

/* build 105: failures are counted for the overlay's warning (psp_debug.c)
and logged one by one only for the first 64; b104 run 1 wrote 15,128 such
lines, which the log then had to carry. The heartbeat's heap line has the
running count */
volatile unsigned long psp_heap_failures = 0;
#define HEAP_FAILURES_LOGGED 64

static void *heap_note(void *result, size_t size, const char *kind, void *caller)
{
	if (result)
		heap_caller_add(caller, size);
	if (!result && !heap_failed_reported)
	{
		heap_failed_reported = 1;
		psp_heap_report("first failure");
	}
	if (!result)
	{
		if (++psp_heap_failures <= HEAP_FAILURES_LOGGED)
			platform_log("heap: %s of %u bytes FAILED (large requests so far %lu KB)%s", kind, (unsigned)size,
				heap_large_total / 1024, psp_heap_failures == HEAP_FAILURES_LOGGED ? "; further failures are only counted" : "");
	}
	else if (size >= 0x10000)
	{
		heap_large_total += size;
		platform_log("heap: %s %u KB from %p (large requests so far %lu KB)", kind, (unsigned)(size / 1024),
			caller, heap_large_total / 1024);
	}
	return result;
}

/* build 105: for the heartbeat. Free space inside the heap split into many
small pieces (allocations made and freed every frame) can leave a large
request failing though plenty is free in all: the pieces' count says
whether that is happening */
void psp_heap_line(char *line, int size)
{
	struct mallinfo info = mallinfo();

	snprintf(line, size, "heap: %d KB in use, %d KB free inside in %d pieces, %d KB taken from the system; %lu requests failed",
		info.uordblks / 1024, info.fordblks / 1024, info.ordblks, info.arena / 1024, psp_heap_failures);
}

void *__wrap_malloc(size_t size) { return heap_note(__real_malloc(size), size, "malloc", __builtin_return_address(0)); }
void *__wrap_calloc(size_t count, size_t size) { return heap_note(__real_calloc(count, size), count * size, "calloc", __builtin_return_address(0)); }
void *__wrap_realloc(void *pointer, size_t size) { return heap_note(__real_realloc(pointer, size), size, "realloc", __builtin_return_address(0)); }
void *__wrap_memalign(size_t alignment, size_t size) { return heap_note(__real_memalign(alignment, size), size, "memalign", __builtin_return_address(0)); }
