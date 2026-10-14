/* psp_me.c - jobs on the Media Engine (build 86: the sound mixer; build 87:
job slots and the ME's own timing).

The PSP's second CPU, the Media Engine (ME), does nothing for the game: its
firmware runs the XMB's music and video decoders, which Halo doesn't use.
Build 44's test (halo_me, Sheepdog01's PSP-1000, 2026-10-03) ran our C on it with
mcidclan's me-core-mapper library; build 86 gave it the game's sound mixer
(Sheepdog01's b86 run 2: ~2,500 blocks, none failed, a clean HOME exit).

A job, start to end:
	1. the main CPU writes its cache back, so main RAM holds what the ME
	   must read;
	2. it fills a SLOT (a few words of UNCACHED memory: both CPUs see the
	   same words at once) and bumps the slot's request number;
	3. the ME, polling its slots, drops its own cache (stale copies from the
	   last job), runs the job (integers or FPU, never the VFPU, which the ME
	   lacks; no system calls, no library calls), writes its cache back, and
	   sets the slot's done number;
	4. the main CPU drops its cached copies of what the ME changed.
The ME writes only what the job owns, on whole 64-byte cache lines (a line
written back carries all 64 bytes, so a shared line would overwrite a
neighbour's newer bytes), and its own stack in its private memory.

One slot per client thread (the audio thread; the renderer later), so two
threads never share a slot; the ME serves the slots in order, audio first.

Timing: the ME reads its own cycle counter (coprocessor 0 register 9)
around each job; its rate is measured against the main CPU's microsecond
clock at start. So the log has the ME's own time a job, its share of the
ME, and the main CPU's side (hand-over and waiting) apart.

While idle, the ME reads the uncached words once every ~25 us and otherwise
spins in registers: the bus is the PSP's scarce resource (memory_traffic.md).

Stopping: the ME must be stopped before the PSP leaves the game (HOME, quit)
or sleeps (build 44's audio sample, which never stopped its ME, crashed the
PSP on HOME). Stop asks, waits for the ME to acknowledge and halt itself;
with no answer it holds the ME in reset through the kernel. The library's
sleep handler (it replaces the firmware's own ME one) holds it in reset too.
After a stop every job runs on the main CPU instead.

The ME is the firmware's until the next reboot of the PSP once taken: XMB
music and video stay off until then. PPSSPP doesn't run ME code at all:
there, each job runs on the main CPU ("simulated"), to exercise everything
but the ME itself.

Built on mcidclan's me-core-mapper (psp-media-engine-custom-core), MIT
licence, copyright (c) 2025-2026 mcidclan (m-c/d, m-cid); LICENSE-mecore.md
ships beside the EBOOT. */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <me-core-mapper/me-core.h>
#include "psp_me.h"

void platform_log(const char *format, ...);
void psp_log_flush(void);
int psp_keep(const char *name);

/* the jobs (their own files): run on the ME, or on the main CPU when
simulated or after a stop */
unsigned long psp_dsound_mix_list(void *const *list, unsigned long count, int *sums, short *output,
	unsigned long frames, unsigned long *clipped);
unsigned int psp_ge_me_classify(const unsigned int *args, unsigned int *results);
unsigned int psp_ge_me_angle(const unsigned int *args, unsigned int *results);
unsigned int psp_ge_me_replay(const unsigned int *args, unsigned int *results);
unsigned int psp_ge_me_slant(const unsigned int *args, unsigned int *results);

#define AUDIO_LIST_MAX 256
#define MIX_FRAMES_MAX 512

struct me_slot
{
	unsigned int request;  /* the main CPU: the number of the job to run */
	unsigned int done;     /* the ME: the number of the last job finished */
	unsigned int type;     /* ME_JOB_... */
	unsigned int cycles;   /* the ME: its cycle counter across the last job */
	unsigned int job_cycles; /* the ME: of those, the job's own code (without
	                         the two cache hand-overs around it) */
	unsigned int args[ME_JOB_ARGS];
	unsigned int results[ME_JOB_RESULTS];
} __attribute__((aligned(64)));

struct me_control
{
	unsigned int alive;    /* the ME: idle polls, counted */
	unsigned int stop;     /* the main CPU 1: stop; the ME 2: stopped */
	unsigned int clock;    /* the ME: its cycle counter at the last poll */
	unsigned int jobs;     /* the ME: jobs run */
	unsigned int cause_before, cause_after; /* build 88: its Cause register at start */
	/* the main CPU, set once after start: ME_FEATURE_HALT ("mehalt": the ME
	halts when it has nothing to do and the main CPU wakes it with an
	interrupt, instead of polling the slots), ME_FEATURE_RING (the job ring).
	One word, so the idle loop's look at it costs what mehalt's alone did */
	unsigned int features;
	unsigned int halted;    /* the ME: 1 from just before a halt until it wakes */
	unsigned int halts;     /* the ME: halts, counted */
	unsigned int wakes;     /* the ME: wake interrupts taken (meLibOnExternalInterrupt) */
} __attribute__((aligned(64)));
#define ME_FEATURE_HALT 1u
#define ME_FEATURE_RING 2u

/* whole cache lines of their own; only ever used through the uncached view */
static struct me_control control_storage __attribute__((aligned(64)));
static struct me_slot slot_storage[ME_SLOTS] __attribute__((aligned(64)));
static void *audio_list_storage[AUDIO_LIST_MAX] __attribute__((aligned(64)));
#define UNCACHED(p) ((volatile __typeof__(*(p)) *)(0x40000000u | (unsigned int)(p)))
#define CONTROL UNCACHED(&control_storage)
#define SLOT(k) UNCACHED(&slot_storage[k])
#define AUDIO_LIST ((void *volatile *)(0x40000000u | (unsigned int)audio_list_storage))

/* ---------- the job ring (ME phase 1; psp_me.h has its contract)

A slot holds one job, and its caller sleeps or spins until the ME is done.
The ring holds up to ME_RING_ENTRIES jobs, so the main CPU can hand one over,
get on with its own share of the work, and collect the answer when it needs
it. The single-writer queue (research_brief_for_master.md 1.4-1.5):
- ring_pushed is written by the main CPU only and ring_popped by the ME
  only, each on a 64-byte line of its own, touched only through the uncached
  view; both only grow (unsigned wrap is fine), and popped passing a job
  means the job is done, its results and output in main RAM.
- A request's lines are written by the main CPU (cached, then written back
  before pushed moves); a result's lines and the job's output by the ME
  (cached, written back before popped moves). Each CPU's cache writes back
  whole lines and doesn't see the other's, so no line has two writers.
- The ME drops its copies of a request's lines and of the job's inputs
  before reading them; the main CPU drops its copies of a result's lines and
  of the output before reading them. */
#define RING_MASK (ME_RING_ENTRIES - 1)
/* past this a range of cache operations costs more than doing the whole
16 KB data cache: one instruction a line, whether the line is held or not */
#define RING_WHOLE_CACHE_BYTES (16 * 1024)
/* a job may use this much of its CPU's stack; the word past it is the
sentinel checked at the job's end */
#define RING_JOB_STACK_BYTES 4096
#define RING_SENTINEL 0x5E471E1Du
#define RING_OK 0
#define RING_BAD_TYPE 1
#define RING_STACK 2

struct me_ring_counter
{
	unsigned int value;
	unsigned int pad[15];
} __attribute__((aligned(64)));

struct me_ring_request
{
	unsigned int type, ticket;
	unsigned int input[ME_RING_INPUTS], input_bytes[ME_RING_INPUTS];
	unsigned int output, output_bytes;
	unsigned int args[ME_RING_ARGS];
	unsigned int pad[32 - 4 - 2 * ME_RING_INPUTS - ME_RING_ARGS];
} __attribute__((aligned(64)));

struct me_ring_result
{
	unsigned int ticket;      /* the job these are for */
	unsigned int status;      /* RING_OK, ... */
	unsigned int job_cycles;  /* the ME's cycle counter across the job's own code */
	unsigned int all_cycles;  /* and across the whole job, its cache work included */
	unsigned int look_cycles; /* from the ME's last look that found the ring empty to the job's start: how
	                          long the job can have waited to be picked up */
	unsigned int results[ME_RING_RESULTS];
} __attribute__((aligned(64)));

static struct me_ring_counter ring_pushed_storage, ring_popped_storage;
static struct me_ring_request ring_requests[ME_RING_ENTRIES];
static struct me_ring_result ring_results[ME_RING_ENTRIES];
#define RING_PUSHED (UNCACHED(&ring_pushed_storage)->value)
#define RING_POPPED (UNCACHED(&ring_popped_storage)->value)

/* hit operations on the lines holding [start, start + bytes): 0x1A writes a
line back, 0x1B writes it back and drops it. On either CPU, in user mode (as
list_take's and ge_writeback's in psp_ge.c). A range seen through the
uncached view never went through a cache: nothing to do */
static inline void lines_writeback(unsigned int start, unsigned int bytes)
{
	unsigned int line, end = start + bytes;

	if (start & 0x40000000u)
		return;
	for (line = start & ~63u; line < end; line += 64)
		__builtin_allegrex_cache(0x1A, line);
	__asm__ __volatile__("sync");
}

static inline void lines_drop(unsigned int start, unsigned int bytes)
{
	unsigned int line, end = start + bytes;

	if (start & 0x40000000u)
		return;
	for (line = start & ~63u; line < end; line += 64)
		__builtin_allegrex_cache(0x1B, line);
	__asm__ __volatile__("sync");
}

/* 0 off, 1 the ME runs the jobs, 2 simulated (the emulator: the main CPU
runs them), -1 stopped (after HOME, quit, sleep or a failure) */
static volatile int me_state = 0;
static unsigned int slot_request[ME_SLOTS];
static unsigned int last_job_us[ME_SLOTS]; /* build 87: psp_me_last_job_us */
/* the ME's cycle counter, counts a microsecond (measured at start) */
static float me_cycles_per_us = 0.0f;

/* ---------- the Media Engine's side: no system calls, no library calls, no
VFPU; everything it touches is listed above */

static inline unsigned int me_cycles(void)
{
	unsigned int count;

	__asm__ __volatile__("mfc0 %0, $9" : "=r"(count));
	return count;
}

static inline void me_idle(void)
{
	unsigned int k;

	/* ~25 us at 333 MHz in registers and the instruction cache */
	for (k = 0; k < 2000; k++)
		__asm__ __volatile__("nop");
}

/* me-core's exception handler calls this on the ME for each
external interrupt (the main CPU's wake, me_wake below). The handler saves
only k0, k1, ra and at, so this may use only k0 and k1: no C. It counts the
wake, so the log can tell "wakes arrive but the ME stays halted" (halt
resuming at itself) from "wakes are lost" (me-core's samples/exceptions
counts interrupts the same way) */
__attribute__((noinline, aligned(4)))
void meLibOnExternalInterrupt(void)
{
	__asm__ __volatile__(
		".set push\n"
		".set noreorder\n"
		"la      $k0, %0\n"
		"li      $k1, 0x40000000\n" /* the uncached view, as UNCACHED() */
		"or      $k0, $k0, $k1\n"
		"lw      $k1, 0($k0)\n"
		"addiu   $k1, $k1, 1\n"
		"sw      $k1, 0($k0)\n"
		"sync\n"
		".set pop\n"
		:
		: "i"(&control_storage.wakes)
		: "k0", "k1", "memory");
}

/* "mehalt"'s idle. halted is raised before the slots are looked
at once more, so a job asked for during this pass is served, not slept
through. One window is left: a wake that lands between that look and the
halt is taken by the handler before the halt. The main CPU covers it by
waking again while it waits (me_wake), so it costs one look, never a job */
static void me_halt_idle(volatile struct me_control *control, int ring_ready, unsigned int ring_popped)
{
	int k, waiting = 0;

	control->halted = 1;
	__asm__ __volatile__("sync");
	for (k = 0; k < ME_SLOTS; k++)
		if (SLOT(k)->request != SLOT(k)->done)
			waiting = 1;
	if (ring_ready && RING_PUSHED != ring_popped)
		waiting = 1;
	if (!waiting && !control->stop)
	{
		control->halts++;
		meLibHalt();
	}
	control->halted = 0;
	__asm__ __volatile__("sync");
}

/* one job (either CPU; on_me: this is the ME) */
static void run_job(volatile struct me_slot *slot, int *sums, int on_me)
{
	switch (slot->type)
	{
	case ME_JOB_AUDIO:
	{
		unsigned long clipped = 0, voices;

		/* args: count, frames, output */
		voices = psp_dsound_mix_list((void *const *)AUDIO_LIST, slot->args[0], sums, (short *)slot->args[2],
			slot->args[1], &clipped);
		slot->results[0] = (unsigned int)voices;
		slot->results[1] = (unsigned int)clipped;
		break;
	}
	case ME_JOB_CLASSIFY:
	case ME_JOB_ANGLE:
	case ME_JOB_GE:
	{
		unsigned int args[ME_JOB_ARGS], results[ME_JOB_RESULTS] = { 0 }, k;

		for (k = 0; k < ME_JOB_ARGS; k++)
			args[k] = slot->args[k];
		/* build 113: the replay's fourth argument says whether it runs on
		the ME, where it may mix audio between records (psp_me_serve_audio);
		gemesim marks the main CPU's own work as the ME's, so the context
		word can't tell */
		if (slot->type == ME_JOB_GE)
			args[3] = (unsigned int)on_me;
		if (slot->type == ME_JOB_CLASSIFY)
			psp_ge_me_classify(args, results);
		else if (slot->type == ME_JOB_ANGLE)
			psp_ge_me_angle(args, results);
		else
			psp_ge_me_replay(args, results);
		for (k = 0; k < ME_JOB_RESULTS; k++)
			slot->results[k] = results[k];
		break;
	}
	case ME_JOB_EDRAM:
	{
		/* build 115: "edramcheck". args: 0 fill / 1 check, the first byte
		(eDRAM offset), the bytes. The ME's own view of its eDRAM, cached
		(me_serve drops the cache before the job and writes it back after).
		Each word holds its own address mixed with a constant, so a word
		moved or left from elsewhere shows too. Only on the ME: the main CPU
		can't see eDRAM (simulated, nothing is touched) */
		unsigned int first = slot->args[1], bytes = slot->args[2], at, bad = 0, first_bad = 0xFFFFFFFFu, found = 0;

		if (!on_me)
		{
			slot->results[0] = 0xFFFFFFFFu;
			break;
		}
		for (at = first; at < first + bytes; at += 4)
		{
			volatile unsigned int *word = (volatile unsigned int *)(0x80000000u + at);
			unsigned int want = at ^ 0x5A5AA5A5u;

			if (slot->args[0] == 0)
				*word = want;
			else if (*word != want)
			{
				if (!bad++)
				{
					first_bad = at;
					found = *word;
				}
			}
		}
		slot->results[0] = bad;
		slot->results[1] = first_bad;
		slot->results[2] = found;
		break;
	}
	default:
		break;
	}
}

/* build 113: a slot's job run on the ME, with the cache hand-overs around it
(meLibOnProcess's loop, and psp_me_serve_audio below) */
static void me_serve(volatile struct me_slot *slot, int *sums)
{
	volatile struct me_control *control = CONTROL;
	unsigned int request = slot->request, start, job_start, job_end;

	start = me_cycles();
	/* what the main CPU wrote: drop any copy left from the last job */
	meLibDcacheWritebackInvalidateAll();
	job_start = me_cycles();
	run_job(slot, sums, 1);
	job_end = me_cycles();
	/* what this job wrote, to main RAM, before saying so */
	meLibDcacheWritebackInvalidateAll();
	slot->cycles = me_cycles() - start;
	slot->job_cycles = job_end - job_start;
	control->jobs++;
	__asm__ __volatile__("sync");
	slot->done = request;
	__asm__ __volatile__("sync");
}

/* build 113: called by a long job on the ME (the GE layer's replay,
psp_ge.c's defer_replay) every few records: an audio block asked for
meanwhile is mixed now, between two records, instead of waiting behind the
whole render job. b112's Test 3 on the PSP: audio blocks waited up to 38 ms
behind render jobs, and 7 of 109 were late [M] (b113_candidates.md, step
4b). Only ever called on the ME (the replay's on-the-ME argument) */
void psp_me_serve_audio(void)
{
	static int sums[MIX_FRAMES_MAX * 2] __attribute__((aligned(64)));
	volatile struct me_slot *slot = SLOT(ME_SLOT_AUDIO);

	if (slot->request == slot->done)
		return;
	me_serve(slot, sums);
}

/* "meringtest"'s job (psp_me_ring_test): each output word from its input
word, and the input's sum, so a stale input line on the ME or a stale output
line on the main CPU shows as a wrong word */
#define RINGTEST_WORDS 64
static void ring_test_job(const unsigned int *args, unsigned int *results)
{
	const unsigned int *in = (const unsigned int *)args[0];
	unsigned int *out = (unsigned int *)args[1], i, sum = 0;

	for (i = 0; i < RINGTEST_WORDS; i++)
	{
		sum += in[i];
		out[i] = (in[i] ^ 0x5A5A5A5Au) + i;
	}
	results[0] = sum;
	results[1] = args[2];
}

/* the word RING_JOB_STACK_BYTES below the caller's stack pointer */
static inline volatile unsigned int *ring_sentinel(void)
{
	unsigned int sp;

	__asm__ __volatile__("move %0, $sp" : "=r"(sp));
	return (volatile unsigned int *)((sp - RING_JOB_STACK_BYTES) & ~3u);
}

/* one ring job, on the ME (on_me), or on the main CPU when simulated, where
there is no ME cache to look after and no ME cycle counter to read.
look_cycles: how long it can have waited to be picked up */
static __attribute__((noinline)) void ring_run(unsigned int index, int on_me, unsigned int look_cycles)
{
	struct me_ring_request *request = &ring_requests[index];
	struct me_ring_result *result = &ring_results[index];
	unsigned int args[ME_RING_ARGS], results[ME_RING_RESULTS], start = 0, job_start = 0, job_end = 0, inputs = 0;
	unsigned int status = RING_OK, k;
	volatile unsigned int *sentinel;

	if (on_me)
	{
		start = me_cycles();
		/* the request as the main CPU wrote it back, not a copy from the last
		time this entry was used */
		lines_drop((unsigned int)request, sizeof(*request));
		for (k = 0; k < ME_RING_INPUTS; k++)
			inputs += request->input_bytes[k];
		if (inputs > RING_WHOLE_CACHE_BYTES)
			meLibDcacheWritebackInvalidateAll();
		else
			for (k = 0; k < ME_RING_INPUTS; k++)
				if (request->input_bytes[k])
					lines_drop(request->input[k], request->input_bytes[k]);
	}
	for (k = 0; k < ME_RING_ARGS; k++)
		args[k] = request->args[k];
	for (k = 0; k < ME_RING_RESULTS; k++)
		results[k] = 0;
	sentinel = ring_sentinel();
	*sentinel = RING_SENTINEL;
	if (on_me)
		job_start = me_cycles();
	switch (request->type)
	{
	case ME_JOB_SLANT:
		psp_ge_me_slant(args, results);
		break;
	case ME_JOB_RINGTEST:
		ring_test_job(args, results);
		break;
	default:
		status = RING_BAD_TYPE;
		break;
	}
	if (on_me)
		job_end = me_cycles();
	/* a job that ran past its stack allowance may have written over whatever
	lies below it: reported, and the ring stops */
	if (*sentinel != RING_SENTINEL)
		status = RING_STACK;
	if (on_me && request->output_bytes)
	{
		if (request->output_bytes > RING_WHOLE_CACHE_BYTES)
			meLibDcacheWritebackInvalidateAll();
		else
			lines_writeback(request->output, request->output_bytes);
	}
	result->ticket = request->ticket;
	result->status = status;
	result->job_cycles = job_end - job_start;
	result->look_cycles = look_cycles;
	for (k = 0; k < ME_RING_RESULTS; k++)
		result->results[k] = results[k];
	result->all_cycles = on_me ? me_cycles() - start : 0;
	if (on_me)
		lines_writeback((unsigned int)result, sizeof(*result));
	__asm__ __volatile__("sync");
}

/* the ME: every job pushed since its last look, in order. last_look: the
cycle count when the ring was last seen with nothing waiting past popped */
static unsigned int me_ring_drain(unsigned int popped, unsigned int *last_look)
{
	unsigned int looked = me_cycles(), pushed = RING_PUSHED;

	while (popped != pushed)
	{
		ring_run(popped & RING_MASK, 1, me_cycles() - *last_look);
		popped++;
		RING_POPPED = popped;
		__asm__ __volatile__("sync");
	}
	/* anything pushed after the read above came after this moment */
	*last_look = looked;
	return popped;
}

/* after a ring job, the ME looks at the ring every ~1.5 us instead of once
an idle pass (~36 us on the PSP, b86): the renderer pushes its jobs in a
burst (a level's draws), and each microsecond a job waits to be picked up is
one the main CPU may wait at the end. A look is one uncached read; a linger
pass is as long as an idle pass, and the slots are still looked at every
pass. RING_LINGER_PASSES passes (~2 ms) after the last job, the idle is as
before */
#define RING_LINGER_PASSES 64
#define RING_LINGER_LOOKS 16
#define RING_LINGER_NOPS 125
static void me_ring_linger(unsigned int popped, unsigned int *last_look)
{
	int look, k;

	for (look = 0; look < RING_LINGER_LOOKS; look++)
	{
		unsigned int looked = me_cycles();

		if (RING_PUSHED != popped)
			return;
		*last_look = looked;
		for (k = 0; k < RING_LINGER_NOPS; k++)
			__asm__ __volatile__("nop");
	}
}

__attribute__((noinline, aligned(4)))
void meLibOnProcess(void)
{
	volatile struct me_control *control = CONTROL;
	int sums[MIX_FRAMES_MAX * 2];
	int halt_ready = 0; /* mehalt: the exception handler is set up */
	int ring_ready = 0; /* the main CPU has turned the ring on */
	unsigned int ring_popped = 0, ring_last_look = 0, ring_quiet = RING_LINGER_PASSES;

	/* build 88: the cycle counter (coprocessor 0 register 9) stood still on
	Sheepdog01's PSP-1000 (b87: "runs at 0.0 a microsecond"), so every ME time read
	0. MIPS32r2's Cause bit 27 (DC, "disable count") stops it; the firmware
	may leave it set on the ME. Cleared here; Cause before and after go to
	the log either way */
	{
		unsigned int cause;

		__asm__ __volatile__("mfc0 %0, $13" : "=r"(cause));
		control->cause_before = cause;
		cause &= ~(1u << 27);
		__asm__ __volatile__("mtc0 %0, $13\n\tsync" : : "r"(cause));
		/* build 89: and the counter written, as me-core's own timer set-up
		does before relying on it (mtc0 $0, $9) */
		__asm__ __volatile__("mtc0 $0, $9\n\tsync");
		__asm__ __volatile__("mfc0 %0, $13" : "=r"(cause));
		control->cause_after = cause;
	}
	meLibDcacheWritebackInvalidateAll();
	for (;;)
	{
		int k;

		/* the features word is looked at every 256th pass (it is set once,
		after start), so the polling idle costs what it would without it */
		if ((control->alive & 255) == 0 && (!halt_ready || !ring_ready))
		{
			unsigned int features = control->features;

			if ((features & ME_FEATURE_HALT) && !halt_ready)
			{
				meLibExceptionHandlerInit(0);
				halt_ready = 1;
			}
			if ((features & ME_FEATURE_RING) && !ring_ready)
			{
				ring_ready = 1;
				ring_last_look = me_cycles();
			}
		}
		control->alive++;
		control->clock = me_cycles();
		/* build 89: the stop word is looked at after the slots (below), so a
		job asked for just before a stop is still served: b86-b88 halted with
		it pending and its caller waited 40 ms holding the mixer's lock.
		The ring after the audio slot: a ring job is small and the main CPU
		may be waiting for it; audio is due on time */
		for (k = 0; k < ME_SLOTS; k++)
		{
			volatile struct me_slot *slot = SLOT(k);

			if (slot->request != slot->done)
				me_serve(slot, sums);
			if (k == ME_SLOT_AUDIO && ring_ready)
			{
				unsigned int before = ring_popped;

				ring_popped = me_ring_drain(ring_popped, &ring_last_look);
				if (ring_popped != before)
					ring_quiet = 0;
			}
		}
		if (control->stop)
			break;
		if (ring_ready && ring_quiet < RING_LINGER_PASSES)
		{
			ring_quiet++;
			me_ring_linger(ring_popped, &ring_last_look);
		}
		else if (halt_ready)
			me_halt_idle(control, ring_ready, ring_popped);
		else
			me_idle();
	}
	control->stop = 2;
	__asm__ __volatile__("sync");
	meLibHalt();
}

/* the library's sleep handler (its replacement for the firmware's own ME one)
calls these in kernel mode: the ME held in reset through the sleep and after
it; the main CPU runs the jobs from then on */
void meLibOnSleep(void)
{
	HW_SYS_RESET_ENABLE = SC_HW_RESET;
	meLibSync();
	me_state = -1;
}

void meLibOnWake(void)
{
	HW_SYS_RESET_ENABLE = SC_HW_RESET;
	meLibSync();
	me_state = -1;
}

/* ---------- the main CPU's side */

/* the log's figures, per slot, since the last report */
static struct
{
	unsigned long jobs, failures, simulated;
	unsigned long long me_cycles, me_job_cycles, wait_us, handover_us;
	unsigned long me_worst_us, wait_worst_us;
} stats[ME_SLOTS];
static unsigned long long report_mark = 0;
static float busy_percent_last = 0.0f;

/* "mehalt" on this side. A wake is a kernel call (me-core's
meLibEmitSoftwareInterrupt), so it is sent only to a halted ME, and again
on each look while a job waits (me_halt_idle says why). The first 256 are
timed, for the kernel call's cost */
static int me_halting = 0;
static int ring_on = 0; /* "meslant": the job ring, decided at start */
static unsigned long wakes_sent, wakes_timed;
static unsigned long long wake_us;

static void me_wake(void)
{
	if (!me_halting || !CONTROL->halted)
		return;
	if (wakes_timed < 256)
	{
		unsigned long long start = sceKernelGetSystemTimeWide();

		meLibEmitSoftwareInterrupt();
		wake_us += sceKernelGetSystemTimeWide() - start;
		wakes_timed++;
	}
	else
		meLibEmitSoftwareInterrupt();
	wakes_sent++;
}

static int me_hold_reset(void)
{
	HW_SYS_RESET_ENABLE = SC_HW_RESET;
	meLibSync();
	return 0;
}

static int emulator(void)
{
	int result = 0;

	/* PPSSPP answers its own "kemulator:" device (3: is this an emulator) */
	return sceIoDevctl("kemulator:", 3, NULL, 0, &result, sizeof(result)) == 0 && result;
}

/* build 105: for the rest of the platform layer (the bus probe) */
int psp_is_emulator(void)
{
	return emulator();
}

void psp_me_stop(const char *why)
{
	volatile struct me_control *control = CONTROL;
	int k;

	if (me_state == 1)
	{
		/* build 89: no new jobs from here; one already asked for is served
		(the ME runs its slots before it looks at the stop word) */
		me_state = -1;
		control->stop = 1;
		me_wake();
		for (k = 0; k < 100 && control->stop != 2; k++)
		{
			sceKernelDelayThread(1000);
			me_wake();
		}
		if (control->stop == 2)
			platform_log("ME: stopped (%s); halted itself after %d ms; the main CPU runs its jobs from here", why, k);
		else
		{
			kcall_2(me_hold_reset, 0);
			platform_log("ME: stopped (%s): NO answer in 100 ms, held in reset; the main CPU runs its jobs from here", why);
		}
	}
	else if (me_state == 2)
		platform_log("ME (simulated): stopped (%s)", why);
	if (me_state > 0)
		me_state = -1;
}

/* the old name (psp_platform.c's HOME and sleep callbacks) */
void psp_meaudio_stop(const char *why)
{
	psp_me_stop(why);
}

static void stop_at_exit(void)
{
	psp_me_stop("exit");
}

/* launch (build 87: on unless the launch menu's "nomeaudio"): the ME started,
its first signs of life checked, its clock measured. Non-zero when jobs can
be handed over */
int psp_me_start(void)
{
	volatile struct me_control *control = CONTROL;
	unsigned int before, clock_before;
	unsigned long long mark;
	int table, k;

	/* nomeaudio keeps the ME off unless a ring client asks for it; the
	mixer then stays on the main CPU (psp_dsound.c) */
	if (me_state || (psp_keep("nomeaudio") && !psp_keep("meslant") && !psp_keep("meringtest")))
		return me_state > 0;
	/* no cached copy of them anywhere on this CPU; then the words that
	matter (they are in zeroed memory already) */
	sceKernelDcacheWritebackInvalidateRange(&control_storage, sizeof(control_storage));
	sceKernelDcacheWritebackInvalidateRange(slot_storage, sizeof(slot_storage));
	sceKernelDcacheWritebackInvalidateRange(audio_list_storage, sizeof(audio_list_storage));
	control->alive = control->stop = control->clock = control->jobs = 0;
	for (k = 0; k < ME_SLOTS; k++)
		SLOT(k)->request = SLOT(k)->done = 0;
	sceKernelDcacheWritebackInvalidateRange(&ring_pushed_storage, sizeof(ring_pushed_storage));
	sceKernelDcacheWritebackInvalidateRange(&ring_popped_storage, sizeof(ring_popped_storage));
	sceKernelDcacheWritebackInvalidateRange(ring_requests, sizeof(ring_requests));
	sceKernelDcacheWritebackInvalidateRange(ring_results, sizeof(ring_results));
	RING_PUSHED = RING_POPPED = 0;
	ring_on = psp_keep("meslant") || psp_keep("meringtest");
	report_mark = sceKernelGetSystemTimeWide();
	if (emulator())
	{
		me_state = 2;
		platform_log("ME: the emulator runs no ME code; each job is SIMULATED (the main CPU runs it)%s",
			ring_on ? "; the job ring on (meslant)" : "");
		return 1;
	}
	{
		/* the library writes its kernel helper, kcall.prx, to the current
		folder and loads it from there: ms0:/halo, whatever the game's own
		current folder is */
		char folder[256];
		int have = getcwd(folder, sizeof(folder)) != NULL;

		platform_log("ME: starting the Media Engine (a hang here: hold the power switch up 10 s; launch with nomeaudio)");
		psp_log_flush();
		chdir("ms0:/halo");
		table = meLibDefaultInit();
		if (have)
			chdir(folder);
	}
	if (table < 0)
	{
		me_state = -1;
		platform_log("ME: the library couldn't start the ME (%d: -3/-4 kcall.prx not written or loaded, -5 no ME event handler, -6 unknown firmware image)", table);
		return 0;
	}
	sceKernelDelayThread(2000);
	before = control->alive;
	clock_before = control->clock;
	mark = sceKernelGetSystemTimeWide();
	sceKernelDelayThread(20000);
	if (control->alive == before)
	{
		me_state = -1;
		kcall_2(me_hold_reset, 0);
		platform_log("ME: the ME didn't start (alive %u -> %u in 20 ms; table %d); held in reset", before, control->alive, table);
		return 0;
	}
	{
		unsigned int cycles = control->clock - clock_before;
		unsigned long long us = sceKernelGetSystemTimeWide() - mark;

		me_cycles_per_us = us ? (float)cycles / (float)us : 0.0f;
	}
	me_state = 1;
	atexit(stop_at_exit);
	/* mehalt only now, so the start check above saw the ME polling */
	me_halting = psp_keep("mehalt");
	control->features = (me_halting ? ME_FEATURE_HALT : 0u) | (ring_on ? ME_FEATURE_RING : 0u);
	platform_log("ME: the ME runs (firmware table %d; %u idle polls in 20 ms; its cycle counter runs at %.1f a microsecond%s; Cause %08x, after clearing DC %08x)",
		table, control->alive - before, me_cycles_per_us, me_cycles_per_us < 1.0f ? ": ME times will read 0" : "",
		control->cause_before, control->cause_after);
	if (me_halting)
		platform_log("ME: mehalt: the ME halts when idle and is woken by interrupt");
	if (ring_on)
		platform_log("ME: the job ring on (meslant): %d entries", ME_RING_ENTRIES);
	psp_log_flush();
	return 1;
}

int psp_meaudio_start(void)
{
	return psp_me_start();
}

int psp_me_running(void)
{
	return me_state > 0;
}

int psp_meaudio_running(void)
{
	return me_state > 0;
}

/* the simulated jobs' mixing sums (the emulator; one job at a time there) */
static int simulated_sums[MIX_FRAMES_MAX * 2] __attribute__((aligned(64)));

/* a job in a slot, run to the end: on the ME (the caller sleeps meanwhile),
or on this CPU when simulated. args as the job type takes them; results
filled. Returns 1 when done, 0 if the ME failed (it is stopped then; the
caller runs the job its own way). first_look_us: how long the job is
expected to take, the first sleep before looking (then every 0.1 ms) */
int psp_me_run(int which, unsigned int type, const unsigned int *args, int arg_count, unsigned int *results,
	unsigned int first_look_us)
{
	volatile struct me_slot *slot;
	unsigned long long start;
	unsigned long waited, me_us;
	int k;

	if (me_state <= 0 || which < 0 || which >= ME_SLOTS || arg_count > ME_JOB_ARGS)
		return 0;
	slot = SLOT(which);
	start = sceKernelGetSystemTimeWide();
	slot->type = type;
	for (k = 0; k < arg_count; k++)
		slot->args[k] = args[k];
	if (me_state == 2)
	{
		/* the emulator: the job's own code, on this CPU */
		run_job(slot, simulated_sums, 0);
		me_us = (unsigned long)(sceKernelGetSystemTimeWide() - start);
		stats[which].simulated++;
		waited = me_us;
		last_job_us[which] = (unsigned int)me_us;
	}
	else
	{
		__asm__ __volatile__("sync");
		slot->request = ++slot_request[which];
		__asm__ __volatile__("sync");
		me_wake();
		sceKernelDelayThread(first_look_us ? first_look_us : 100);
		while (slot->done != slot_request[which])
		{
			me_wake();
			/* build 89: the ME halted (a stop) before it saw this job */
			if (CONTROL->stop == 2 && slot->done != slot_request[which])
			{
				stats[which].failures++;
				return 0;
			}
			/* build 113: a render job gets 500 ms. The 40 ms limit was set
			for audio jobs (about 1 ms each); b112's render jobs ran up to
			39.6 ms on the PSP, and the one past 40 ms stopped the ME [M] */
			/* build 115: the eDRAM probe too, which may wait behind a render job */
			if (sceKernelGetSystemTimeWide() - start > (type == ME_JOB_GE || type == ME_JOB_EDRAM ? 500000 : 40000))
			{
				platform_log("ME: a job (type %u) not finished in %u ms (job %u; the ME at job %u, %u idle polls)", type,
					type == ME_JOB_GE || type == ME_JOB_EDRAM ? 500 : 40, slot_request[which], slot->done, CONTROL->alive);
				stats[which].failures++;
				psp_me_stop("a job timed out");
				return 0;
			}
			sceKernelDelayThread(100);
		}
		waited = (unsigned long)(sceKernelGetSystemTimeWide() - start);
		stats[which].me_cycles += slot->cycles;
		stats[which].me_job_cycles += slot->job_cycles;
		last_job_us[which] = me_cycles_per_us > 0.0f ? (unsigned int)((float)slot->job_cycles / me_cycles_per_us) : 0;
		me_us = me_cycles_per_us > 0.0f ? (unsigned long)((float)slot->cycles / me_cycles_per_us) : 0;
	}
	for (k = 0; k < ME_JOB_RESULTS; k++)
		results[k] = slot->results[k];
	stats[which].jobs++;
	stats[which].wait_us += waited;
	if (waited > stats[which].wait_worst_us)
		stats[which].wait_worst_us = waited;
	if (me_us > stats[which].me_worst_us)
		stats[which].me_worst_us = me_us;
	return 1;
}

/* build 118: a job handed over without waiting (L1 step 4, psp_ge.c's
"gemeoverlap"): the main CPU goes on with its own work and finishes the job
later with psp_me_finish. One job a slot at a time. Returns 1 if handed over
(or, simulated, already run here: the emulator runs no ME code), 0 if the ME
can't take it (off, stopped, a job already out): the caller runs it itself.
The caller writes back its data cache first, as for psp_me_run */
static int submitted[ME_SLOTS]; /* 0 none, 1 out with the ME, 2 run here (simulated) */
static unsigned long long submit_mark[ME_SLOTS];

int psp_me_submit(int which, unsigned int type, const unsigned int *args, int arg_count)
{
	volatile struct me_slot *slot;
	int k;

	if (me_state <= 0 || which < 0 || which >= ME_SLOTS || arg_count > ME_JOB_ARGS || submitted[which])
		return 0;
	slot = SLOT(which);
	slot->type = type;
	for (k = 0; k < arg_count; k++)
		slot->args[k] = args[k];
	submit_mark[which] = sceKernelGetSystemTimeWide();
	if (me_state == 2)
	{
		run_job(slot, simulated_sums, 0);
		last_job_us[which] = (unsigned int)(sceKernelGetSystemTimeWide() - submit_mark[which]);
		stats[which].simulated++;
		submitted[which] = 2;
		return 1;
	}
	__asm__ __volatile__("sync");
	slot->request = ++slot_request[which];
	__asm__ __volatile__("sync");
	me_wake();
	submitted[which] = 1;
	return 1;
}

/* the job handed over, waited for: 1 when done (results filled), 0 if the
ME failed (it is stopped then, and the job may be half done). The wait spins
on the slot's uncached done word: hand-overs come every few milliseconds,
and a sleep would cost the PSP's scheduler tick (lever X3). After 2 ms it
sleeps in 50 us steps, so a slow job doesn't hold the CPU from the other
threads.
"mefinishslow" (code_review_b119_me.md, finding 1): each look is
an uncached read on the bus the ME is reading its job's inputs from, and
the clock is a system call. By default a look comes every 64 nops (~0.2 us)
with the clock and the stop word every look; spaced, a look every ~3 us and
the clock and stop word every 16th look, so a wake is still a few us late
at most */
#define FINISH_SPIN_NOPS 64        /* between looks, by default */
#define FINISH_SPIN_NOPS_SLOW 1000 /* "mefinishslow": ~3 us at 333 MHz */
#define FINISH_SLOW_CLOCK_EVERY 16 /* "mefinishslow": the clock every 16th look */

int psp_me_finish(int which, unsigned int *results)
{
	static int slow = -1;
	volatile struct me_slot *slot;
	unsigned long long start, now;
	unsigned long waited, looks = 0;
	int k, state;

	if (which < 0 || which >= ME_SLOTS || !submitted[which])
		return 0;
	if (slow < 0)
		slow = psp_keep("mefinishslow");
	state = submitted[which];
	submitted[which] = 0;
	slot = SLOT(which);
	start = now = sceKernelGetSystemTimeWide();
	if (state == 1)
	{
		while (slot->done != slot_request[which])
		{
			int spin = slow ? FINISH_SPIN_NOPS_SLOW : FINISH_SPIN_NOPS;

			if (!slow || looks % FINISH_SLOW_CLOCK_EVERY == 0)
			{
				now = sceKernelGetSystemTimeWide();
				/* the ME halted (a stop: HOME, sleep) before it finished this job */
				if (CONTROL->stop == 2 && slot->done != slot_request[which])
				{
					stats[which].failures++;
					return 0;
				}
				if (now - submit_mark[which] > 500000)
				{
					platform_log("ME: a handed-over job (type %u) not finished in 500 ms (job %u; the ME at job %u, %u idle polls)",
						slot->type, slot_request[which], slot->done, CONTROL->alive);
					stats[which].failures++;
					psp_me_stop("a handed-over job timed out");
					return 0;
				}
				/* mehalt: a wake that may have been lost (me_halt_idle) */
				me_wake();
			}
			looks++;
			if (now - start < 2000)
			{
				for (k = 0; k < spin; k++)
					__asm__ __volatile__("nop");
			}
			else
				sceKernelDelayThread(50);
		}
		stats[which].me_cycles += slot->cycles;
		stats[which].me_job_cycles += slot->job_cycles;
		last_job_us[which] = me_cycles_per_us > 0.0f ? (unsigned int)((float)slot->job_cycles / me_cycles_per_us) : 0;
		{
			unsigned long me_us = me_cycles_per_us > 0.0f ? (unsigned long)((float)slot->cycles / me_cycles_per_us) : 0;

			if (me_us > stats[which].me_worst_us)
				stats[which].me_worst_us = me_us;
		}
		waited = (unsigned long)(sceKernelGetSystemTimeWide() - start);
	}
	else
		waited = last_job_us[which]; /* simulated: the job's own time, run at the hand-over */
	for (k = 0; k < ME_JOB_RESULTS; k++)
		results[k] = slot->results[k];
	stats[which].jobs++;
	stats[which].wait_us += waited;
	if (waited > stats[which].wait_worst_us)
		stats[which].wait_worst_us = waited;
	return 1;
}

/* ---------- the job ring, the main CPU's side */

/* a job that isn't done this long after its push is reported, and the main
CPU does its work instead (psp_me_wait returns 0). The ring is then off for
the session: the ME may still finish the job late and write its output,
which nobody reads again. A slant job is ~0.1-2 ms of work (~1 us a
triangle, b90) and may wait behind one audio block (~0.6 ms) */
#define RING_DEADLINE_US 10000
/* between two looks at popped while waiting: ~1 us, so the look costs the
bus little while the ME reads its inputs (code_review_b119_me.md, 1) */
#define RING_WAIT_NOPS 250
#define RING_WAIT_CLOCK_EVERY 16

extern volatile unsigned int psp_me_window[16];
static unsigned int ring_next = 0;        /* the next ticket */
static unsigned int ring_popped_seen = 0; /* popped, as last read */
static int ring_dead = 0;                 /* a job failed: the ring takes no more */
static unsigned int ring_push_mark[ME_RING_ENTRIES]; /* the push's time, a ticket's */
static unsigned int ring_thread = 0;      /* simulated: the producer's thread, for psp_me_context */
static struct
{
	unsigned long jobs, at_once, failures, refused_busy, refused_full, refused_check;
	unsigned long long job_cycles, all_cycles, look_cycles, push_us, wait_us, round_us;
	unsigned long long simulated_us; /* the emulator: the jobs' own time, run inside the push */
	unsigned long look_worst_cycles, wait_worst_us;
} ring_stats;

int psp_me_ring_ready(void)
{
	return ring_on && !ring_dead && me_state > 0;
}

/* what the main CPU wrote that a job will read, out of its cache to main
RAM: the lines of the range, or past 16 KB the whole cache */
void psp_me_writeback(const void *start, unsigned int bytes)
{
	if (bytes > RING_WHOLE_CACHE_BYTES)
		sceKernelDcacheWritebackAll();
	else if (bytes)
		lines_writeback((unsigned int)start, bytes);
}

/* in main RAM, where the ME can reach it (not VRAM, not the scratchpad);
cached or uncached view alike */
static int ring_in_ram(unsigned int address, unsigned int bytes)
{
	unsigned int at = address & 0x1FFFFFFFu;

	return !bytes || (at >= 0x08000000u && at + bytes <= 0x0C000000u && at + bytes > at);
}

/* a job handed to the ring: 1, with its ticket for psp_me_wait; 0 if the
ring can't take it (off, full, the ME busy with a long slot job, or a
transfer that breaks the rules above): the caller does the work itself */
int psp_me_push(const struct psp_me_job *job, unsigned int *ticket)
{
	struct me_ring_request *request;
	unsigned int start, t, index, k;

	if (!psp_me_ring_ready())
		return 0;
	start = sceKernelGetSystemTimeLow();
	/* a render or probe job holds the ME for milliseconds (gemeon's are
	~10 ms on the PSP, review_b120.md 5): a ring job behind it would be
	waited for longer than its work is worth */
	for (k = 0; k < ME_SLOTS; k++)
		if (k != ME_SLOT_AUDIO && submitted[k])
		{
			ring_stats.refused_busy++;
			return 0;
		}
	if (ring_next - ring_popped_seen >= ME_RING_ENTRIES)
	{
		ring_popped_seen = RING_POPPED;
		if (ring_next - ring_popped_seen >= ME_RING_ENTRIES)
		{
			ring_stats.refused_full++;
			return 0;
		}
	}
	/* the transfer rules: the output is whole lines (a line written back
	carries all 64 bytes), everything in main RAM */
	if (((unsigned int)job->output & 63u) || (job->output_bytes & 63u) ||
		!ring_in_ram((unsigned int)job->output, job->output_bytes) || (job->output_bytes && ((unsigned int)job->output & 0x40000000u)))
	{
		ring_stats.refused_check++;
		return 0;
	}
	for (k = 0; k < ME_RING_INPUTS; k++)
		if (!ring_in_ram((unsigned int)job->input[k], job->input_bytes[k]))
		{
			ring_stats.refused_check++;
			return 0;
		}
	t = ring_next;
	index = t & RING_MASK;
	request = &ring_requests[index];
	request->type = job->type;
	request->ticket = t;
	for (k = 0; k < ME_RING_INPUTS; k++)
	{
		request->input[k] = (unsigned int)job->input[k];
		request->input_bytes[k] = job->input_bytes[k];
	}
	request->output = (unsigned int)job->output;
	request->output_bytes = job->output_bytes;
	for (k = 0; k < ME_RING_ARGS; k++)
		request->args[k] = job->args[k];
	ring_push_mark[index] = start;
	ring_next = t + 1;
	if (me_state == 2)
	{
		/* the emulator: the job's code here and now, marked as the ME's for
		the ME-strict check and the write trace (emu.sh's gemesim and
		mewrites); the window stays open until the wait */
		unsigned int job_start;

		if (!ring_thread)
			ring_thread = (unsigned int)sceKernelGetThreadId();
		psp_me_window[0] = 1;
		job_start = sceKernelGetSystemTimeLow();
		psp_me_context = ring_thread;
		ring_run(index, 0, 0);
		psp_me_context = 0;
		ring_stats.simulated_us += sceKernelGetSystemTimeLow() - job_start;
		RING_POPPED = t + 1;
	}
	else
	{
		lines_writeback((unsigned int)request, sizeof(*request));
		RING_PUSHED = t + 1;
		__asm__ __volatile__("sync");
		me_wake();
	}
	*ticket = t;
	ring_stats.push_us += sceKernelGetSystemTimeLow() - start;
	return 1;
}

/* a pushed job's answer: 1 when done (results filled, its output readable
on this CPU), 0 if the ME failed it (stopped, too late, or the job broke a
rule): the caller does the work itself, and must not read the output */
int psp_me_wait(unsigned int ticket, unsigned int *results)
{
	unsigned int index = ticket & RING_MASK, start = sceKernelGetSystemTimeLow(), now, looks = 0, k;
	struct me_ring_request *request = &ring_requests[index];
	struct me_ring_result *result = &ring_results[index];
	unsigned long waited;

	if (ring_next - ticket - 1 >= ME_RING_ENTRIES)
		return 0;
	if (me_state == 2)
		psp_me_window[0] = 0;
	if ((int)(RING_POPPED - ticket) > 0)
		ring_stats.at_once++;
	else
	{
		while ((int)(RING_POPPED - ticket) <= 0)
		{
			if (looks++ % RING_WAIT_CLOCK_EVERY == 0)
			{
				now = sceKernelGetSystemTimeLow();
				/* stopped (HOME, sleep, a slot job's timeout) before this job */
				if (me_state <= 0 || CONTROL->stop == 2)
				{
					ring_stats.failures++;
					return 0;
				}
				if (now - ring_push_mark[index] > RING_DEADLINE_US)
				{
					platform_log("ME: ring job %u (type %u) not done %u ms after its push (popped %u); the main CPU does it, and the ring is off from here",
						ticket, request->type, RING_DEADLINE_US / 1000, RING_POPPED);
					ring_dead = 1;
					ring_stats.failures++;
					return 0;
				}
				/* mehalt: a wake that may have been lost (me_halt_idle) */
				me_wake();
			}
			for (k = 0; k < RING_WAIT_NOPS; k++)
				__asm__ __volatile__("nop");
		}
	}
	/* this CPU's copies of what the ME wrote are stale: dropped (it never
	writes these lines itself, so dropping loses nothing of its own) */
	lines_drop((unsigned int)result, sizeof(*result));
	if (request->output_bytes > RING_WHOLE_CACHE_BYTES)
		sceKernelDcacheWritebackInvalidateAll();
	else if (request->output_bytes)
		lines_drop(request->output, request->output_bytes);
	if (result->ticket != ticket || result->status != RING_OK)
	{
		platform_log("ME: ring job %u (type %u) came back %s (ticket %u); the main CPU does it, and the ring is off from here",
			ticket, request->type, result->status == RING_STACK ? "past its stack allowance" :
			result->status == RING_BAD_TYPE ? "as an unknown type" : "with another job's answer", result->ticket);
		ring_dead = 1;
		ring_stats.failures++;
		return 0;
	}
	for (k = 0; k < ME_RING_RESULTS; k++)
		results[k] = result->results[k];
	now = sceKernelGetSystemTimeLow();
	waited = now - start;
	ring_stats.jobs++;
	ring_stats.wait_us += waited;
	if (waited > ring_stats.wait_worst_us)
		ring_stats.wait_worst_us = waited;
	ring_stats.round_us += now - ring_push_mark[index];
	ring_stats.job_cycles += result->job_cycles;
	ring_stats.all_cycles += result->all_cycles;
	ring_stats.look_cycles += result->look_cycles;
	if (result->look_cycles > ring_stats.look_worst_cycles)
		ring_stats.look_worst_cycles = result->look_cycles;
	return 1;
}

/* "meringtest": the ring's own check, a burst of ME_RING_ENTRIES jobs at each
frame's end from the renderer's thread (the ring's one producer). Each job
reads an input the main CPU has just rewritten with a new pattern and writes
an output the main CPU read a frame before, so a missing write-back or drop
on either CPU shows as a wrong word; the burst fills the ring, so the index
wraps every frame, and the answers are collected last first */
static unsigned int ringtest_in[ME_RING_ENTRIES][RINGTEST_WORDS] __attribute__((aligned(64)));
static unsigned int ringtest_out[ME_RING_ENTRIES][RINGTEST_WORDS] __attribute__((aligned(64)));
static int ringtest_on = -1;
static unsigned int ringtest_seq = 0;
static unsigned long ringtest_bursts, ringtest_jobs, ringtest_wrong, ringtest_short, ringtest_failed;

void psp_me_ring_test(void)
{
	unsigned int tickets[ME_RING_ENTRIES], results[ME_RING_RESULTS], k, i, pushed = 0;
	struct psp_me_job job;

	if (PSP_ME_WORK())
		return;
	if (ringtest_on < 0)
		ringtest_on = psp_keep("meringtest");
	if (!ringtest_on || !psp_me_ring_ready())
		return;
	ringtest_seq++;
	for (k = 0; k < ME_RING_ENTRIES; k++)
	{
		for (i = 0; i < RINGTEST_WORDS; i++)
			ringtest_in[k][i] = (ringtest_seq * 2654435761u) ^ (k << 24) ^ (i * 40503u);
		psp_me_writeback(ringtest_in[k], sizeof(ringtest_in[k]));
		memset(&job, 0, sizeof(job));
		job.type = ME_JOB_RINGTEST;
		job.args[0] = (unsigned int)ringtest_in[k];
		job.args[1] = (unsigned int)ringtest_out[k];
		job.args[2] = ringtest_seq;
		job.input[0] = ringtest_in[k];
		job.input_bytes[0] = sizeof(ringtest_in[k]);
		job.output = ringtest_out[k];
		job.output_bytes = sizeof(ringtest_out[k]);
		if (!psp_me_push(&job, &tickets[k]))
			break;
		pushed++;
	}
	if (pushed < ME_RING_ENTRIES)
		ringtest_short++;
	for (k = pushed; k-- > 0;)
	{
		unsigned int sum = 0, wrong = 0;

		if (!psp_me_wait(tickets[k], results))
		{
			ringtest_failed++;
			continue;
		}
		for (i = 0; i < RINGTEST_WORDS; i++)
		{
			sum += ringtest_in[k][i];
			if (ringtest_out[k][i] != (ringtest_in[k][i] ^ 0x5A5A5A5Au) + i)
				wrong++;
		}
		if (results[0] != sum || results[1] != ringtest_seq)
			wrong++;
		ringtest_wrong += wrong;
		ringtest_jobs++;
	}
	ringtest_bursts++;
}

/* build 87: the last job's own code time on the ME, in microseconds (its
cycle counter, without the cache hand-overs); simulated, the main CPU's time
for the whole job */

unsigned int psp_me_last_job_us(int which)
{
	return which >= 0 && which < ME_SLOTS ? last_job_us[which] : 0;
}

/* the audio job's list: up to 256 voices, written through the uncached view */
int psp_me_audio(void *const *list, unsigned long count, short *output, unsigned long frames, unsigned long *voices,
	unsigned long *clipped, unsigned long *waited_us)
{
	unsigned int args[3], results[ME_JOB_RESULTS];
	unsigned long long start = sceKernelGetSystemTimeWide();
	unsigned long k;

	if (me_state <= 0 || count > AUDIO_LIST_MAX || frames > MIX_FRAMES_MAX)
		return 0;
	for (k = 0; k < count; k++)
		AUDIO_LIST[k] = list[k];
	args[0] = (unsigned int)count;
	args[1] = (unsigned int)frames;
	args[2] = (unsigned int)output;
	/* about 0.1-1 ms of work: a first look after 0.2 ms */
	if (!psp_me_run(ME_SLOT_AUDIO, ME_JOB_AUDIO, args, 3, results, 200))
		return 0;
	*voices = results[0];
	*clipped = results[1];
	*waited_us = (unsigned long)(sceKernelGetSystemTimeWide() - start);
	return 1;
}

/* build 86's entry, kept for psp_dsound.c */
int psp_meaudio_run(void *const *list, unsigned long count, short *output, unsigned long frames, unsigned long *voices,
	unsigned long *clipped, unsigned long *waited_us)
{
	return psp_me_audio(list, count, output, frames, voices, clipped, waited_us);
}

/* the main CPU's own work around a job (prepare, cache write-back and
invalidation), told by the client, for the log */
void psp_me_handover(int which, unsigned long us)
{
	if (which >= 0 && which < ME_SLOTS)
		stats[which].handover_us += us;
}

float psp_me_busy_percent(void)
{
	return busy_percent_last;
}

/* every heartbeat (psp_platform.c): per slot, the ME's own time a job and its
share of the ME, the main CPU's hand-over and wait */
/* build 115: "edramcheck" (big_levers.md section 0, "The ME's eDRAM,
checked"): whether anything but our own ME code writes the ME's eDRAM during
play. The ME fills the stretch from 256 KB to 1.5 MB (its stack starts at the
top, 2 MB, and grows down: 512 KB are left to it) with a pattern, then checks
it piece by piece, and the log says what it found. Only with the ME really
running (not simulated, not stopped).
Build 116: a 64 KB piece a heartbeat, for the fill as for the checks. b115
filled the whole 1.25 MB in one job: 56 ms on the ME, so the audio job queued
behind it passed its 40 ms limit and the ME was stopped for the session
(hardware_b115.md, test 1). A piece is about 3 ms of the ME's time */
#define EDRAM_PROBE_FIRST 0x40000u
#define EDRAM_PROBE_END 0x180000u
#define EDRAM_PROBE_PIECE 0x10000u
void psp_me_edram_probe(void)
{
	static int asked = -1;
	static unsigned int fill_next = EDRAM_PROBE_FIRST, next = EDRAM_PROBE_FIRST, checked = 0, bad_pieces = 0;
	unsigned int args[3], results[ME_JOB_RESULTS] = { 0 };

	if (asked < 0)
		asked = psp_keep("edramcheck") != 0;
	if (!asked || me_state != 1)
		return;
	if (fill_next < EDRAM_PROBE_END)
	{
		args[0] = 0;
		args[1] = fill_next;
		args[2] = EDRAM_PROBE_PIECE;
		if (psp_me_run(ME_SLOT_PROBE, ME_JOB_EDRAM, args, 3, results, 2000) && results[0] != 0xFFFFFFFFu)
		{
			fill_next += EDRAM_PROBE_PIECE;
			if (fill_next >= EDRAM_PROBE_END)
				platform_log("ME: edramcheck: eDRAM %u to %u KB filled with the pattern, %u KB a heartbeat",
					EDRAM_PROBE_FIRST / 1024, EDRAM_PROBE_END / 1024, EDRAM_PROBE_PIECE / 1024);
		}
		return;
	}
	args[0] = 1;
	args[1] = next;
	args[2] = EDRAM_PROBE_PIECE;
	if (!psp_me_run(ME_SLOT_PROBE, ME_JOB_EDRAM, args, 3, results, 2000) || results[0] == 0xFFFFFFFFu)
		return;
	checked++;
	if (results[0])
	{
		bad_pieces++;
		platform_log("ME: edramcheck: %u words changed in eDRAM %u to %u KB, the first at %08x (now %08x)",
			results[0], next / 1024, (next + EDRAM_PROBE_PIECE) / 1024, results[1], results[2]);
	}
	if ((checked % 40) == 0)
		platform_log("ME: edramcheck: %u pieces of %u KB checked, %u with changes", checked, EDRAM_PROBE_PIECE / 1024,
			bad_pieces);
	next += EDRAM_PROBE_PIECE;
	if (next >= EDRAM_PROBE_END)
		next = EDRAM_PROBE_FIRST;
}

void psp_me_report(void)
{
	static const char *const names[ME_SLOTS] = { "audio", "render", "probe" };
	unsigned long long now = sceKernelGetSystemTimeWide(), window = now - report_mark, busy_us_all = 0;
	char line[900], ring_line[400];
	int length = 0, k;

	if (!me_state)
		return;
	report_mark = now;
	for (k = 0; k < ME_SLOTS; k++)
	{
		unsigned long long me_us = me_cycles_per_us > 0.0f ? (unsigned long long)((float)stats[k].me_cycles / me_cycles_per_us) : 0;
		/* build 87: the job's own code, apart from the cache hand-overs */
		unsigned long long job_us = me_cycles_per_us > 0.0f ? (unsigned long long)((float)stats[k].me_job_cycles / me_cycles_per_us) : 0;

		if (stats[k].simulated)
			me_us = job_us = stats[k].wait_us;
		busy_us_all += me_us;
		if (!stats[k].jobs && !stats[k].failures)
			continue;
		length += snprintf(line + length, sizeof(line) - length,
			"; %s: %lu jobs, the ME's own time %lu us a job (the job's code %lu, cache hand-overs the rest; worst %lu), %.1f%% of the ME; the main CPU's hand-over %lu us a job, waiting %lu us a job (worst %lu); %lu failed",
			names[k], stats[k].jobs, stats[k].jobs ? (unsigned long)(me_us / stats[k].jobs) : 0,
			stats[k].jobs ? (unsigned long)(job_us / stats[k].jobs) : 0, stats[k].me_worst_us,
			window ? 100.0 * (double)me_us / (double)window : 0.0,
			stats[k].jobs ? (unsigned long)(stats[k].handover_us / stats[k].jobs) : 0,
			stats[k].jobs ? (unsigned long)(stats[k].wait_us / stats[k].jobs) : 0, stats[k].wait_worst_us, stats[k].failures);
		if (length >= (int)sizeof(line))
			length = (int)sizeof(line) - 1;
	}
	ring_line[0] = 0;
	if (ring_stats.jobs || ring_stats.failures || ring_stats.refused_busy || ring_stats.refused_full || ring_stats.refused_check)
	{
		/* the ring's plumbing: on the ME, the whole job against its code
		(the rest is the cache work around it) and how long a job can have
		waited to be picked up; on the main CPU, the push (the request and
		its write-back) and the wait at the end, and push to answer */
		unsigned long jobs = ring_stats.jobs ? ring_stats.jobs : 1;
		float rate = me_cycles_per_us > 0.0f ? me_cycles_per_us : 1.0f;
		unsigned long long all_us = (unsigned long long)((float)ring_stats.all_cycles / rate);

		if (me_state == 2)
			all_us = ring_stats.simulated_us;
		busy_us_all += all_us;
		/* a line of its own: the log's lines have a length limit */
		snprintf(ring_line, sizeof(ring_line),
				"ME: ring: %lu jobs (%lu done before the wait), %s %lu us a job (the job's code %lu; picked up within %lu us, worst %lu); the main CPU's push %lu us a job, wait %lu us a job (worst %lu), push to answer %lu us; refused %lu (ME busy), %lu (full), %lu (transfer rules); %lu failed",
				ring_stats.jobs, ring_stats.at_once, me_state == 2 ? "SIMULATED inside the push," : "the ME's own time",
				(unsigned long)(all_us / jobs), (unsigned long)((float)ring_stats.job_cycles / rate / (float)jobs),
				(unsigned long)((float)ring_stats.look_cycles / rate / (float)jobs), (unsigned long)((float)ring_stats.look_worst_cycles / rate),
				(unsigned long)(ring_stats.push_us / jobs), (unsigned long)(ring_stats.wait_us / jobs), ring_stats.wait_worst_us,
				(unsigned long)(ring_stats.round_us / jobs), ring_stats.refused_busy, ring_stats.refused_full, ring_stats.refused_check,
				ring_stats.failures);
		memset(&ring_stats, 0, sizeof(ring_stats));
	}
	busy_percent_last = window ? (float)(100.0 * (double)busy_us_all / (double)window) : 0.0f;
	if (me_halting && length < (int)sizeof(line) - 160)
	{
		/* mehalt's own figures since the last report. Wakes
		taken far above halts: the ME is interrupted while busy. Halts
		without wakes taken, with jobs failing: wakes don't arrive. Wakes
		taken with jobs failing: halt resumes at itself */
		static unsigned int halts_last, wakes_last;
		unsigned int halts = CONTROL->halts, wakes = CONTROL->wakes;

		length += snprintf(line + length, sizeof(line) - length,
			"; mehalt: %u halts, %lu wakes sent (a kernel call, %lu us each), %u taken",
			halts - halts_last, wakes_sent, wakes_timed ? (unsigned long)(wake_us / wakes_timed) : 0, wakes - wakes_last);
		halts_last = halts;
		wakes_last = wakes;
		wakes_sent = 0;
	}
	platform_log("ME: %s, %.1f%% busy over %.1f s%s", me_state == 1 ? "running" : me_state == 2 ? "SIMULATED (emulator)" :
		"stopped", busy_percent_last, (double)window / 1e6, length ? line : "; no jobs");
	if (ring_line[0])
		platform_log("%s", ring_line);
	if (ringtest_bursts)
	{
		platform_log("ME: ringtest: %lu bursts, %lu jobs checked, %lu wrong (words or sums), %lu bursts short of a full ring (a push refused), %lu jobs failed",
			ringtest_bursts, ringtest_jobs, ringtest_wrong, ringtest_short, ringtest_failed);
		ringtest_bursts = ringtest_jobs = ringtest_wrong = ringtest_short = ringtest_failed = 0;
	}
	memset(stats, 0, sizeof(stats));
}
