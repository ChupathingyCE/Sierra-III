/* psp_me.c - jobs on the Media Engine (build 86: the sound mixer; build 87:
job slots and the ME's own timing).

The PSP's second CPU, the Media Engine (ME), does nothing for the game: its
firmware runs the XMB's music and video decoders, which Halo doesn't use.
Build 44's test (halo_me, a PSP-1000, 2026-10-03) ran our C on it with
mcidclan's me-core-mapper library; build 86 gave it the game's sound mixer
(b86's run 2: ~2,500 blocks, none failed, a clean HOME exit).

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
or sleeps (an ME left running crashed the PSP on HOME). Stop asks, waits for
the ME to acknowledge and halt itself; with no answer it holds the ME in
reset through the kernel. The library's sleep handler (it replaces the
firmware's own ME one) holds it in reset too. After a stop every job runs on
the main CPU instead.

After a sleep the ME stays stopped unless "mewake" asks for it to be
started again ("the wake" below): a reset release from the main thread at a
frame's end, once no job of any slot or ring ticket can still be out. On
the PSP-1000 the first such restart found no ME answering, and the HOME exit
after it hung the PSP, so it is opt-in until a PSP shows it working.

The ME is the firmware's until the next reboot of the PSP once taken: XMB
music and video stay off until then. PPSSPP doesn't run ME code at all:
there, each job runs on the main CPU ("simulated"), to exercise everything
but the ME itself.

Built on mcidclan's me-core-mapper (psp-media-engine-custom-core), MIT
licence, copyright (c) 2025-2026 mcidclan (m-c/d, m-cid); LICENSE-mecore.md
ships beside the EBOOT. */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <me-core-mapper/me-core.h>
#include "psp_me.h"
#include "psp_sleep.h"
#include "psp_lanetrace.h"
#include "psp_paths.h"   /* ms0:/halo or ms0:/halomp, and the maps */

void platform_log(const char *format, ...);
void psp_log_flush(void);
int psp_keep(const char *name);
int psp_keep_number(const char *prefix, int fallback);

/* the jobs (their own files): run on the ME, or on the main CPU when
simulated or after a stop */
unsigned long psp_dsound_mix_list(void *const *list, unsigned long count, int *sums, short *output,
	unsigned long frames, unsigned long *clipped);
unsigned long psp_dsound_mix_split(void *const *list, unsigned long count, int *sums, short *output,
	unsigned long frames, unsigned long *clipped, unsigned int *split);
unsigned int psp_ge_me_classify(const unsigned int *args, unsigned int *results);
unsigned int psp_ge_me_angle(const unsigned int *args, unsigned int *results);
unsigned int psp_ge_me_replay(const unsigned int *args, unsigned int *results);
unsigned int psp_ge_me_slant(const unsigned int *args, unsigned int *results);
void psp_perception_me_rays(const unsigned int *args, unsigned int *results);
void psp_pose_me_make(const unsigned int *args, unsigned int *results); /* objects.c (psp_fixups.py FIXES_131) */
void psp_points_me_make(const unsigned int *args, unsigned int *results); /* point_physics.c (psp_fixups.py FIXES_MEJOBS) */
void psp_mevis_me_job(const unsigned int *args, unsigned int *results); /* structure_visibility.c (psp_fixups.py mevis_copies) */

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
	unsigned int begun;    /* the ME: the number of the last job it started,
	                       so a wait to be served is told from a job's own run */
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
	/* the ME at its start: its coprocessor 0 CPU number, and 1 once it's in */
	unsigned int cpu_number, cpu_seen;
	/* MEXEC (docs/mexec.md), the ME at each install: 1 copied, 2 refused
	(no table), its bytes, the words it patched, and the copy's word sum */
	unsigned int mexec_state, mexec_bytes, mexec_patches, mexec_sum;
	/* "ticksched": the replay's slice in ME cycles (the main CPU, set once
	after start); a look reads it only when ring jobs wait */
	unsigned int tick_slice_cycles;
} __attribute__((aligned(64)));
#define ME_FEATURE_HALT 1u
#define ME_FEATURE_RING 2u
/* audio first: a waiting audio block is mixed before each ring job and
between the steps of a long one ("meaudiofirst"; off until a rig A/B shows
what it costs the ring jobs it is mixed inside) */
#define ME_FEATURE_AUDIO_FIRST 4u
/* the audio job's cache hand-over by ranges (psp_dsound_me_lines), off
unless "meaudioranges": on the rig it didn't shorten the ME's mix, and c40's
cinematic ticks were longer with it among the words on */
#define ME_FEATURE_AUDIO_RANGES 8u
/* the debug stash kept, when "mestash" asks for it (a hang to diagnose) */
#define ME_FEATURE_STASH 16u
/* "meyieldoff": a slot job doesn't serve the ring between its steps, and
the ring refuses pushes while one is out, as before batches */
#define ME_FEATURE_NO_YIELD 32u
/* "meedramstate": the ME's private state (below) kept in its eDRAM. Off
until a PSP run shows that stretch of the eDRAM is ours: edramcheck watches
0-192 KB and patterns from 256 KB, not between */
#define ME_FEATURE_EDRAM_STATE 64u
/* "meclaimold": a batch job claims its record before it looks at the limit,
as before the stale-claim fix (batch_job says why that order holds the main
CPU) */
#define ME_FEATURE_CLAIM_OLD 128u
/* "mebcc1": the ME's 0xBCC00000 unit's word 0x40 set to 1, as m-cid
(mcidclan) found fastest for code on the ME (psp-media-engine-reload's
README), where me-core's minimal VME set-up leaves it at 2. A test until a
PSP-1000 A/B shows whether the ME's jobs run faster */
#define ME_FEATURE_BCC 256u
/* "mefaultoff": the ME's exception vector left as me-core leaves it, not
pointed at psp_me_fault_vector */
#define ME_FEATURE_FAULT_OFF 512u
/* "mefaulttest": a test of the record, never for play: about 5 s after
start the ME runs a break instruction (an exception on any MIPS: an odd
address's load was let through on the ME, in a run on the PSP), so the
main CPU's next deadline stops it and the log shows the record */
#define ME_FEATURE_FAULT_TEST 1024u
/* "mewakeprobe": at a restart the ME tries each kind of work a job does
before its loop (me_wake_probe), marking each step, so after a real sleep
the log says which kind hangs or reads wrong */
#define ME_FEATURE_WAKE_PROBE 2048u
/* "menestrender": a slot job's yields also run a waiting render job whole
(the old way). Off, the render job waits for the slot loop instead: a render
job runs up to ~124 ms on b30 [M], and while one runs nested the slot job
can't read its abort word, so the main CPU's bounded wait for it ran out and
stopped the ME (the lab's sierra-hw 41164514273). The AI job it waits behind
is short and ends at the word */
#define ME_FEATURE_NEST_RENDER 4096u
/* "aimevisleft": inside the AI job a drain stops at a visibility job
(ME_JOB_VISIBILITY) and leaves it, and the ring behind it, to the ME's loop:
it can't be stopped once begun, and on b30 one ran ~17.8 ms with the AI job's
word unanswerable (sierra-hw 41880162180) */
#define ME_FEATURE_AIME_VIS_LEFT 8192u
/* "aimeaudiolook" (on; aimeaudiolookoff): an audio block that has waited
past AUDIO_LOOK_DUE_CYCLES is mixed at the looks inside a long job (the AI
job's inside an actor, the light pass's), where only the ring was served:
one actor's thinking can run past the audio job's 40 ms bound on its own
(me_audio_due) */
#define ME_FEATURE_AUDIO_LOOK 16384u
/* "mexec" (PSP_MEXEC builds; docs/mexec.md): the ME copies its hot code into
its eDRAM at start and runs the GE replay from the copy */
#define ME_FEATURE_MEXEC 32768u
/* "ticksched" (the tick schedule box, slice 1; docs/box_tick_schedule.md):
inside the frame's replay the ring is served at pick-up points a slice of
the ME's time apart, not at every record, and a pick-up makes the waiting
batches' records before it runs the ring in order. The replay's code and a
batch's (~13 KB with the ring's) don't fit the 16 KB I-cache together, so
each job taken between two records cost both a refill (c20: the replay
+0.3-0.4 ms a job beyond the batches' own time, sweepme_claims_c20_replay) */
#define ME_FEATURE_TICKSCHED 65536u
/* "audiohold" (the lead 2026-11-20 22:5x, Sheepdog01 22:50: one more block of
delay is fine): the audio thread keeps one more block ahead of the speaker
(psp_dsound.c), so an audio block can wait for the replay's end instead of
being mixed at its looks: the replay's look and the AI job's look mix one
only once it has waited AUDIO_HOLD_DUE_CYCLES (a block that would run dry
otherwise), and the main CPU's wait for an audio job is AUDIO_HOLD_WAIT_US */
#define ME_FEATURE_AUDIO_HOLD 131072u

#if PSP_MEXEC
/* The copy's table: tools/mexec.py writes it into the linked program
(psp_build.py, before prxgen); 'MEX0' until then. Its layout is the tool's.
Sized to the lists in use with room to grow (list rb fills 1,040 words): it sits
in .data, so every word is heap the game doesn't get, and the tool refuses a
list whose table outgrows it */
#if defined(PSP_MEXEC_TABLE_WORDS)
/* the list's "# @table" line (psp_build.py): a list that fills a raised room */
#define MEXEC_TABLE_WORDS PSP_MEXEC_TABLE_WORDS
#elif PSP_MEXEC_JOBS
/* a list with the think and the mixer (psp/layout/mexec_rbj.txt) has about
twice rb's functions and calls between them */
#define MEXEC_TABLE_WORDS 4096
#else
#define MEXEC_TABLE_WORDS 1536
#endif
#define MEXEC_MAGIC 0x4D455843u
#define MEXEC_HEADER 16
#define MEXEC_AT 0x40000u   /* eDRAM 256 KB: the resident room's start (psp_me.h) */
#ifdef PSP_MEXEC_ROOM_KB
/* the list's "# @room" line (psp_build.py); the sound tier starts at 800 KB */
#define MEXEC_ROOM (PSP_MEXEC_ROOM_KB * 1024u)
#else
#define MEXEC_ROOM 0x30000u
#endif
/* the copy ends flush against the sound tier at most: a raised room that
crossed it would have the tier's loads overwrite the ME's code */
typedef char mexec_room_below_sndtier[(MEXEC_AT + MEXEC_ROOM <= ME_EDRAM_SNDTIER) ? 1 : -1];
unsigned int psp_mexec_table[MEXEC_TABLE_WORDS] = { 0x4D455830u };
/* the replay's copy, 0 until the copy is in place; cleared at every start,
since the eDRAM doesn't survive a sleep */
static PSP_AIME_ME_SIDE unsigned int mexec_entry = 0;
#if PSP_MEXEC_JOBS
/* run_job's copy (0 when the list hasn't it): me_serve enters every slot job
through it, so the think and the mixer run from the copy as the replay does;
a function pointer would leave the copy for main RAM, a call made from the
copy stays in it */
static PSP_AIME_ME_SIDE unsigned int mexec_run = 0;
#endif
#endif

/* The debug stash: where the ME is, in words of its own (two cache lines,
only ever used through the uncached view), so a main CPU that gives up on a
job can say what the ME was doing (me_stash_dump). The ME writes it, the
main CPU only reads it */
#define STASH_AT 0        /* STASH_IDLE, ... */
#define STASH_TICKET 1    /* the ring job in hand */
#define STASH_TYPE 2      /* its type (or the slot job's) */
#define STASH_STEP 3      /* how far into a long job (psp_me_ring_step) */
#define STASH_CLOCK 4     /* the ME's cycle counter at the last note */
#define STASH_RING_BEGUN 5
#define STASH_RING_ENDED 6
#define STASH_AUDIO_INSIDE 7 /* audio blocks mixed before or inside a ring job */
#define STASH_SLOT_BEGUN 8
#define STASH_SLOT_ENDED 9
#define STASH_PUSHED 10   /* the ring's pushed count as the ME last read it */
#define STASH_AUDIO_RANGES 11 /* audio blocks the ME handed over by ranges */
#define STASH_SLOTS_IN 12 /* the slot jobs in hand, a bit per type: nested ones show beside their host */
#define STASH_LOOK 13     /* the ME's clock at its last look at the ring */
#define STASH_PLACE 14    /* PSP_RECON: where in a long slot job the ME is (psp_me_stash_place) */
#define STASH_PLACE_CLOCK 15 /* the ME's clock when it got there */
#define STASH_WORDS 32
#define STASH_IDLE 1
#define STASH_SLOT_DROP 2 /* a slot job's cache work before it */
#define STASH_SLOT_JOB 3
#define STASH_SLOT_WRITE 4 /* and after it */
#define STASH_RING_DROP 5
#define STASH_RING_JOB 6
#define STASH_RING_WRITE 7
#define STASH_HALTED 8
#define STASH_STOPPED 9
static unsigned int stash_storage[STASH_WORDS] __attribute__((aligned(64)));

/* whole cache lines of their own; only ever used through the uncached view */
static struct me_control control_storage __attribute__((aligned(64)));

/* B31: what the ME's exception vector saw, written by psp_me_fault_vector
on the ME and read by the main CPU when it stops an ME that didn't answer:
a fault in a job (a bad or unaligned address, a firmware call, a trap) stops
the ME with nothing to say where, since me-core installs no vector unless
mehalt asks. Uncached on both sides; a line of its own */
struct me_fault
{
	unsigned int count, cause, epc, bad_address, sp, ra, status, pad[9];
} __attribute__((aligned(64)));
struct me_fault psp_me_fault_storage __attribute__((aligned(64)));
static struct me_slot slot_storage[ME_SLOTS] __attribute__((aligned(64)));
static void *audio_list_storage[AUDIO_LIST_MAX] __attribute__((aligned(64)));
#define UNCACHED(p) ((volatile __typeof__(*(p)) *)(0x40000000u | (unsigned int)(p)))
#define CONTROL UNCACHED(&control_storage)
#define SLOT(k) UNCACHED(&slot_storage[k])
#define AUDIO_LIST ((void *volatile *)(0x40000000u | (unsigned int)audio_list_storage))
#define STASH ((volatile unsigned int *)(0x40000000u | (unsigned int)stash_storage))

/* the ME's note in the stash; the features word is read each time (one
uncached read) so the main CPU's choice holds without a copy on the ME */
static inline void me_note(unsigned int at)
{
	unsigned int count;

	if (!(CONTROL->features & ME_FEATURE_STASH))
		return;
	__asm__ __volatile__("mfc0 %0, $9" : "=r"(count));
	STASH[STASH_AT] = at;
	STASH[STASH_CLOCK] = count;
}

static inline void me_count(unsigned int which)
{
	if (CONTROL->features & ME_FEATURE_STASH)
		STASH[which]++;
}

/* the ME code's audio hand-over by ranges (psp_dsound.c): 1 done, 0 over the
size where the whole cache is cheaper (the caller does the whole cache) */
int psp_dsound_me_lines(void *const *list, unsigned long count, unsigned long frames, int after);

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
/* a ray walks the level's BSP and each object's recursively: its frames and
results take more (actor_perception.c, psp_perception_me_rays) */
#define RING_RAYS_STACK_BYTES (24 * 1024)
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
	                          long the job can have waited to be picked up (under mehalt the time halted
	                          before the wake counts too, so only the polling ME's figure is tight) */
	unsigned int who;         /* "mewaitwho" (PSP_RECON): the job type the ME ran at the wait's start (high byte) and at the pick-up (low) */
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
/* counted by the kernel's sleep and wake handlers: a restart that sees it
move slept again partway, and the ME may be in reset under it */
static volatile unsigned int me_sleeps = 0;
/* The reset register holds every block's reset, not only the ME's: written
whole, a hold or a release also releases whatever the firmware's drivers
hold in reset at that moment (mid-sleep, mid-wake). So the ME's two bits are
set and cleared alone, as the firmware's own sysreg driver does.
"meresetwhole": the whole-register writes again */
static int me_reset_whole = 0;
/* The segment every kcall below runs its kernel function in. kcall jumps to
seg | f, and kernel-mode code run at its user address (seg 0) switches the
PSP off at random, about once per ~5 ms spent there (bench-hw 41954147841 to
41955337648, docs/hw_capabilities.md section 6.5); the same code at the
kernel's cached alias ran 3,245 calls clean, which is the segment the
library's own kcalls pass (me-lib.h). "kcallseg0" brings back seg 0 */
static unsigned int me_kseg = CACHED_KERNEL_MASK;

/* meLibDefaultInit's steps (psp-media-engine-custom-core, m-c/d, MIT: its
eventHandler, meLibInit and meLibDefaultInit), so that the ME's reset runs
in me_kseg too: the library's own kcall(meLibInit, 0) runs the copy of the
ME's code, both cache flushes and the reset at the user address, long enough
to switch the PSP off now and then at "starting the Media Engine" (B47).
"melibinit" calls the library's own instead */
__attribute__((noinline, aligned(4)))
static int me_lib_event(int event)
{
	static int last = 0;

	if (event == 0x00000402 && last != 0x00000402)
	{
		meLibOnSleep();
		last = event;
	}
	else if (event == 0x00010005 && last != 0x00010005)
	{
		meLibOnWake();
		last = event;
	}
	return 0;
}

static int me_lib_reset(void)
{
	const int table = meCoreGetTableIdFromWitnessWord();

	/* tables 0 and 1 are images the library has no mapping for */
	if (table < 2)
		return -6;
	meCoreSelectSystemTable(table);
	meLibReset();
	return table;
}

static int me_lib_init(void)
{
	int error;

	if (psp_keep("melibinit"))
		return meLibDefaultInit();
	error = meLibLoadPrx();
	if (error < 0)
		return error;
	if (kinit((void *)me_lib_event) < 0)
		return -5;
	return kcall_2(me_lib_reset, me_kseg);
}

/* The system registers that decide whether the ME can run, as the main CPU
sees them: resets (0xbc10004c), bus clocks (0xbc100050: the ME's is bit 0),
clocks (0xbc100058) and AVC power (0xbc100070); with them, the ME's start
marks (below). Read at launch, in the kernel's sleep and wake handlers,
around a restart's release and at exit, so the log shows what a sleep
changes */
struct me_sysregs
{
	unsigned int reset, bus_clock, clock, avc, marks[4]; /* 2-3: mewakeprobe's step and fails */
};
static struct me_sysregs regs_launch, regs_sleep, regs_wake, regs_exit;

/* kernel mode only, like the registers */
static inline void reset_hold(void)
{
	if (me_reset_whole)
		HW_SYS_RESET_ENABLE = SC_HW_RESET;
	else
	{
		u32 interrupts;

		meLibSuspendCpuIntr(interrupts);
		HW_SYS_RESET_ENABLE |= SC_HW_RESET;
		meLibResumeCpuIntr(interrupts);
	}
	meLibSync();
}

static inline void reset_release(void)
{
	if (me_reset_whole)
		HW_SYS_RESET_ENABLE = 0;
	else
	{
		u32 interrupts;

		meLibSuspendCpuIntr(interrupts);
		HW_SYS_RESET_ENABLE &= ~SC_HW_RESET;
		meLibResumeCpuIntr(interrupts);
	}
	meLibSync();
}

/* Two words of the ME's reset-vector SRAM, written by the ME as its start
goes on, so a start that never answers still says how far it got. Away from
the library's shared words (0xbfc00600) and the firmware's RPC words, which
are idle while the ME runs our code */
#define ME_MARK_START   0xbfc006c0 /* the wake's start stub (psp_me_wake_stub) */
#define ME_MARK_PROCESS 0xbfc006c4 /* meLibOnProcess */
#define ME_MARK_STUB_IN     0x57a90001u
#define ME_MARK_STUB_DONE   0x57a90002u
#define ME_MARK_PROCESS_IN  0x57a90003u
#define ME_MARK_PROCESS_RUN 0x57a90004u
#define ME_MARK_PROBE      0xbfc006c8 /* me_wake_probe's step */
#define ME_MARK_PROBE_FAIL 0xbfc006cc /* its steps that read back wrong, a bit each */
#define ME_MARK_PROBE_BASE 0x57a90010u /* + the step begun; + 0xf when all are done */

/* kernel mode only: the registers and the SRAM are out of a user thread's
reach */
static inline void sysregs_read(struct me_sysregs *regs)
{
	regs->reset = HW_SYS_RESET_ENABLE;
	regs->bus_clock = HW_SYS_BUS_CLOCK_ENABLE;
	regs->clock = hw(0xbc100058);
	regs->avc = HW_SYS_AVC_POWER;
	regs->marks[0] = hw(ME_MARK_START);
	regs->marks[1] = hw(ME_MARK_PROCESS);
	regs->marks[2] = hw(ME_MARK_PROBE);
	regs->marks[3] = hw(ME_MARK_PROBE_FAIL);
}

/* 1 while a client thread is inside a slot's job (psp_me_run, or from
psp_me_submit to the end of psp_me_finish). Each slot has one client thread,
the flag's only writer; it is raised before that thread looks at me_state,
so the wake's restart, which finds every flag down while me_state is still
-1, knows no thread can be inside a slot's words until it sets me_state */
static volatile int slot_busy[ME_SLOTS];
static unsigned int slot_request[ME_SLOTS];
static unsigned int last_job_us[ME_SLOTS]; /* build 87: psp_me_last_job_us */
/* the ME's cycle counter, counts a microsecond (measured at start) */
static float me_cycles_per_us = 0.0f;
/* the ME's stack top in its eDRAM, set by the main CPU before the ME starts
(ME_EDRAM_STACK_TOP is the 1000's) */
static unsigned int me_stack_top = ME_EDRAM_STACK_TOP;

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

/* B31: the ME's exception vector, unless "mefaultoff": the exception's
Cause, EPC, BadVAddr, $sp, $ra and Status, and a count, into
psp_me_fault_storage through the uncached view, then a spin. Only k0 and k1
are used, so the record holds the faulting code's own $sp and $ra. A fault
is fatal either way: the main CPU's 40 ms deadline stops the ME and reports
the record. Interrupts stay off on the ME without mehalt, so nothing else
comes here; mehalt installs me-core's vector instead */
void psp_me_fault_vector(void);
__asm__(
	".text\n"
	".align 4\n"
	".globl psp_me_fault_vector\n"
	".ent psp_me_fault_vector\n"
	"psp_me_fault_vector:\n"
	".set push\n"
	".set noreorder\n"
	".set noat\n"
	"	la $k0, psp_me_fault_storage\n"
	"	li $k1, 0x40000000\n"
	"	or $k0, $k0, $k1\n"
	"	mfc0 $k1, $13\n"
	"	sw $k1, 4($k0)\n"
	"	mfc0 $k1, $14\n"
	"	sw $k1, 8($k0)\n"
	"	mfc0 $k1, $8\n"
	"	sw $k1, 12($k0)\n"
	"	sw $sp, 16($k0)\n"
	"	sw $ra, 20($k0)\n"
	"	mfc0 $k1, $12\n"
	"	sw $k1, 24($k0)\n"
	"	lw $k1, 0($k0)\n"
	"	addiu $k1, $k1, 1\n"
	"	sw $k1, 0($k0)\n"
	"	sync\n"
	"1:	b 1b\n"
	"	nop\n"
	".set pop\n"
	".end psp_me_fault_vector\n");

static void me_fault_vector_install(void)
{
	unsigned int vector = 0x80000000u | (unsigned int)psp_me_fault_vector;

	__asm__ __volatile__("cache 0x8, 0(%0)\n\tsync\n\tmtc0 %0, $25\n\tsync" : : "r"(vector) : "memory");
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
		me_note(STASH_HALTED);
		meLibHalt();
	}
	control->halted = 0;
	__asm__ __volatile__("sync");
}

/* one job (either CPU; on_me: this is the ME) */
static void aime_simulated(volatile struct me_slot *slot);
static void aime_on_me(volatile struct me_slot *slot);

static void run_job(volatile struct me_slot *slot, int *sums, int on_me)
{
	switch (slot->type)
	{
	case ME_JOB_AIME:
		if (on_me)
			aime_on_me(slot);
		else
			aime_simulated(slot);
		break;
	case ME_JOB_AUDIO:
	{
		unsigned long clipped = 0, voices;
		/* on the ME, where its cycles went (results 2-5); the main CPU
		can't read the ME's counter, so it leaves them 0 */
		unsigned int split[4] = { 0, 0, 0, 0 }, k;

		/* args: count, frames, output */
		voices = psp_dsound_mix_split((void *const *)AUDIO_LIST, slot->args[0], sums, (short *)slot->args[2],
			slot->args[1], &clipped, on_me ? split : NULL);
#if PSP_MUSIC
		/* args[3]: the block's music decodes here, after its mix */
		if (slot->args[3])
		{
			extern void psp_dsound_music_job(unsigned long frames);

			psp_dsound_music_job(slot->args[1]);
		}
#endif
		slot->results[0] = (unsigned int)voices;
		slot->results[1] = (unsigned int)clipped;
		for (k = 0; k < 4; k++)
			slot->results[2 + k] = split[k];
		/* args 4-6: the block's 44.1 kHz work after its mix (MUSADPCM step 3:
		the full-rate voices and audio44's filter, off the main CPU) */
		slot->results[7] = 0;
		if (slot->args[5])
		{
			extern void psp_dsound_full_job(void *const *list, unsigned long count, unsigned long frames, short *output22,
				short *out44, short *full, unsigned int *heard, unsigned int *clipped);
			unsigned int heard = 0, over = 0;

			psp_dsound_full_job((void *const *)AUDIO_LIST + slot->args[0], slot->args[4], slot->args[1],
				(short *)slot->args[2], (short *)slot->args[5], (short *)slot->args[6], &heard, &over);
			slot->results[1] |= over;
			slot->results[6] = heard;
			slot->results[7] = 1;
		}
		break;
	}
#if PSP_MUSIC
	case ME_JOB_MUSIC:
	{
		extern void psp_music_job(volatile unsigned int *args, volatile unsigned int *results, int on_me);

		psp_music_job(slot->args, slot->results, on_me);
		break;
	}
#endif
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
#if PSP_MEXEC
		else if (on_me && mexec_entry)
			((unsigned int (*)(const unsigned int *, unsigned int *))mexec_entry)(args, results);
#endif
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
		unsigned int hash = 0x811C9DC5u, sp;

		if (!on_me)
		{
			slot->results[0] = 0xFFFFFFFFu;
			break;
		}
		/* mode 2 hashes a stretch it must not write (the bottom of eDRAM,
		nothing of ours); a fill never comes within 16 KB of this job's own
		stack, so the stretch below the stack can be patterned while the ME
		runs on it, and a later check finds how far down the stack reached */
		__asm__ ("move %0, $sp" : "=r"(sp));
		sp = (sp & 0x1FFFFFFFu) - 16 * 1024;
		for (at = first; at < first + bytes; at += 4)
		{
			volatile unsigned int *word = (volatile unsigned int *)(0x80000000u + at);
			unsigned int want = at ^ 0x5A5AA5A5u;

			if (slot->args[0] == 2)
				hash = (hash ^ *word) * 16777619u;
			else if (slot->args[0] == 0)
			{
				if (at < sp)
					*word = want;
			}
			else if (*word != want)
			{
				if (!bad++)
				{
					first_bad = at;
					found = *word;
				}
			}
		}
		if (slot->args[0] == 2)
			bad = hash;
		slot->results[3] = sp + 16 * 1024;
		slot->results[0] = bad;
		slot->results[1] = first_bad;
		slot->results[2] = found;
		break;
	}
	case ME_JOB_VME:
		psp_vme_job(slot->args, slot->results, on_me);
		break;
	case ME_JOB_SOUNDCOPY:
	{
		/* "soundedram": up to four pieces (source, destination, bytes in
		whole 16s), staged in main RAM by the stick reader, copied into the
		sound cache. me_serve drops the ME's cache before the job (so the
		staging is read fresh) and writes it back after (into the eDRAM). Off
		the ME only when simulated, where the cache is in main RAM; an eDRAM
		destination there is refused unless the emulator maps it. "sndedram"'s tier fills its eDRAM
		pages with the same job; a piece whose byte count has bit 0 set
		("sndedramcheck") is also hashed as read and, once the ME's cache is
		written back, as the eDRAM holds it, so the main CPU can compare
		both with its own hash of the staging (results 1 and 2) */
		unsigned int k, copied = 0, check = 0, read_hash = PSP_ME_HASH_START, held_hash = PSP_ME_HASH_START;

		for (k = 0; k + 2 < ME_JOB_ARGS && slot->args[k + 2]; k += 3)
		{
			const unsigned int *from = (const unsigned int *)slot->args[k];
			unsigned int *to = (unsigned int *)slot->args[k + 1], words = slot->args[k + 2] >> 2, w;

			if (!on_me && (slot->args[k + 1] & 0x1FFFFFFFu) < 0x08000000u
				&& !psp_me_sim_edram_holds(slot->args[k + 1] & 0x1FFFFFFFu, words << 2))
			{
				copied = 0xFFFFFFFFu;
				break;
			}
			if (slot->args[k + 2] & 1)
			{
				check = 1;
				for (w = 0; w < words; w++)
				{
					unsigned int a = from[w];

					to[w] = a;
					read_hash = psp_me_hash(read_hash, a);
				}
			}
			else
				for (w = 0; w < words; w += 4)
				{
					unsigned int a = from[w], b = from[w + 1], c = from[w + 2], d = from[w + 3];

					to[w] = a;
					to[w + 1] = b;
					to[w + 2] = c;
					to[w + 3] = d;
				}
			copied += words << 2;
		}
		if (check && copied != 0xFFFFFFFFu)
		{
			if (on_me)
				meLibDcacheWritebackInvalidateAll();
			for (k = 0; k + 2 < ME_JOB_ARGS && slot->args[k + 2]; k += 3)
				if (slot->args[k + 2] & 1)
				{
					const volatile unsigned int *held = (const volatile unsigned int *)slot->args[k + 1];
					unsigned int words = slot->args[k + 2] >> 2, w;

					for (w = 0; w < words; w++)
						held_hash = psp_me_hash(held_hash, held[w]);
				}
		}
		slot->results[0] = copied;
		slot->results[1] = read_hash;
		slot->results[2] = held_hash;
		break;
	}
	default:
		break;
	}
}

/* "mewaitwho" (PSP_RECON; the lab's ME lane, step 1's census): what the ME is running, as a slot job's type
(ME_OCC_LOOP for its own loop), so a ring job's wait can be charged to the job that held the ME when the
wait began and the one that picked it up */
#define ME_OCC_LOOP 31u
#if PSP_RECON
static PSP_AIME_ME_SIDE unsigned int me_occ_now = ME_OCC_LOOP, me_occ_at_look = ME_OCC_LOOP;
#endif

/* build 113: a slot's job run on the ME, with the cache hand-overs around it
(meLibOnProcess's loop, and psp_me_serve_audio below) */
static void me_serve(volatile struct me_slot *slot, int *sums)
{
#if PSP_RECON
	unsigned int occ_was = me_occ_now;
#endif
	volatile struct me_control *control = CONTROL;
	unsigned int request = slot->request, start, job_start, job_end, type = slot->type;
	int ranges;

	start = me_cycles();
	slot->begun = request;
#if PSP_RECON
	me_occ_now = type & 31u;
#endif
	me_count(STASH_SLOT_BEGUN);
	if (control->features & ME_FEATURE_STASH)
		STASH[STASH_SLOTS_IN] |= 1u << (type & 31);
	me_note(STASH_SLOT_DROP);
	/* what the main CPU wrote: drop any copy left from the last job. The
	audio job by ranges, so a ring job it is mixed between or inside keeps
	what it has in the cache: the voices it reads (psp_dsound_me_lines says
	why that is all of them), or the whole cache past 16 KB */
	ranges = type == ME_JOB_AUDIO && (control->features & ME_FEATURE_AUDIO_RANGES) &&
#if PSP_MUSIC
		!slot->args[3] &&
#endif
		!slot->args[5] && /* the 44.1 kHz work's buffers lie past the voices' ranges */
		psp_dsound_me_lines((void *const *)AUDIO_LIST, slot->args[0], slot->args[1], 0);
	if (!ranges)
		meLibDcacheWritebackInvalidateAll();
	else
		me_count(STASH_AUDIO_RANGES);
	job_start = me_cycles();
	me_note(STASH_SLOT_JOB);
#if PSP_MEXEC_JOBS
	if (mexec_run)
		((void (*)(volatile struct me_slot *, int *, int))mexec_run)(slot, sums, 1);
	else
#endif
	run_job(slot, sums, 1);
	job_end = me_cycles();
	me_note(STASH_SLOT_WRITE);
	/* what this job wrote, to main RAM, before saying so: the voices' lines
	it changed and the output block, or the whole cache */
	if (ranges)
	{
		psp_dsound_me_lines((void *const *)AUDIO_LIST, slot->args[0], slot->args[1], 1);
		lines_drop(slot->args[2], slot->args[1] * 2 * sizeof(short));
	}
	else
		meLibDcacheWritebackInvalidateAll();
	slot->cycles = me_cycles() - start;
	slot->job_cycles = job_end - job_start;
	control->jobs++;
	me_count(STASH_SLOT_ENDED);
	if (control->features & ME_FEATURE_STASH)
		STASH[STASH_SLOTS_IN] &= ~(1u << (type & 31));
	me_note(STASH_IDLE);
	__asm__ __volatile__("sync");
	slot->done = request;
	__asm__ __volatile__("sync");
#if PSP_RECON
	me_occ_now = occ_was;
#endif
}

/* The ME's own state: with "meedramstate" in its eDRAM (psp_me.h's map), so
what only the ME reads and writes stays off the shared bus; otherwise in main
RAM on lines of its own, which the main CPU never touches. Only ever touched
on the ME */
struct me_private
{
	/* the ring as the ME's loop last left it, for a slot job that serves the
	ring between its steps (psp_me_yield); ring_ready 0 outside such a job */
	unsigned int ring_ready, ring_popped, ring_last_look;
	/* 1 inside a ring job: a yield there mixes audio but runs no ring job
	(the ring's jobs run in order, and this one isn't popped yet) */
	unsigned int in_ring;
	unsigned int pad[12];
};
static struct me_private me_private_main __attribute__((aligned(64)));
/* the sums of audio blocks mixed inside other jobs (4 KB in the eDRAM) */
static int me_audio_sums_main[MIX_FRAMES_MAX * 2] __attribute__((aligned(64)));
#define ME_EDRAM_STATE_ON (CONTROL->features & ME_FEATURE_EDRAM_STATE)
#define ME_PRIVATE (ME_EDRAM_STATE_ON ? (volatile struct me_private *)(ME_EDRAM + ME_EDRAM_PRIVATE) : &me_private_main)
#define ME_AUDIO_SUMS (ME_EDRAM_STATE_ON ? (int *)(ME_EDRAM + ME_EDRAM_PRIVATE + 0x1000) : me_audio_sums_main)

/* the audio slot's job, if one is waiting: 1 when one was mixed. Only on
the ME (a long job's step, psp_me_yield) */
static int me_audio_now(void)
{
	volatile struct me_slot *slot = SLOT(ME_SLOT_AUDIO);

	if (slot->request == slot->done)
		return 0;
	me_serve(slot, ME_AUDIO_SUMS);
	return 1;
}

/* "aimeaudiolook": at a look inside a long job, 1 when the audio slot's job
has waited AUDIO_LOOK_DUE_CYCLES since a look first saw it, so it's mixed
there (me_audio_now). The ME can't read when the main CPU pushed it, so the
wait counts from the first look that saw it, at most a stretch between
looks after the push. Its bound: two stretches (~10 ms at worst on d20), the due time and
the block's own mix (~2.5 ms on c10, 5-7 on d20) stay inside the main CPU's
40 ms. Most blocks are still mixed between actors or records, so a look
mixes one only inside a long stretch of one job. No mix starts once the
collection's word is up, and the word is tested right after the look (at
perception's props, at its next look, ~2 ms on), so a mix delays its answer
by about the mix alone, inside its 20 ms. Only on the ME */
#define AUDIO_LOOK_DUE_CYCLES (333u * 4000u) /* ~4 ms of the ME's clock */
/* audiohold's: two blocks (46 ms at 512 frames, 22,050 Hz) less the mix (up
to ~7 ms on d20) and the stretch to the next look; the main CPU waits 60 ms
for the job then, so a block mixed at the due time can't trip its deadline */
#define AUDIO_HOLD_DUE_CYCLES (333u * 30000u)
#define AUDIO_HOLD_WAIT_US 60000u
/* audiohold's wait: the audio thread sleeps on this flag, which the render
thread sets when it collects the render job (psp_me_finish; the held block
is mixed right after the replay, as a rule), and looks every 4 ms in case: a
poll every 1 ms still cost b30's flight tick +0.5 ms (sierra-hw
44874847574). Made in psp_me_start with the feature, so the setter and the
waiter can't see two different flags; volatile, as two threads read it */
static PSP_MAIN_SIDE volatile SceUID audio_hold_flag = -1;
#define AUDIO_HOLD_LOOK_US 4000u
static PSP_AIME_ME_SIDE unsigned int audio_look_request = 0, audio_look_seen = 0;

static int me_audio_due(unsigned int now)
{
	volatile struct me_slot *slot = SLOT(ME_SLOT_AUDIO);
	unsigned int request = slot->request;

	if (request == slot->done)
		return 0;
	if (request != audio_look_request)
	{
		audio_look_request = request;
		audio_look_seen = now;
		return 0;
	}
	return now - audio_look_seen >= ((CONTROL->features & ME_FEATURE_AUDIO_HOLD) ? AUDIO_HOLD_DUE_CYCLES : AUDIO_LOOK_DUE_CYCLES);
}

static unsigned int me_ring_drain(unsigned int popped, unsigned int *last_look, volatile const unsigned int *stop);
static void psp_me_inner_reset(void);

/* called by a long job on the ME (the GE layer's replay, psp_ge.c's
defer_replay) every few records: an audio block asked for meanwhile is mixed
now, between two records, instead of waiting behind the whole render job.
b112's Test 3 on the PSP: audio blocks waited up to 38 ms behind render jobs,
and 7 of 109 were late [M] (b113_candidates.md, step 4b). Only ever called on
the ME (the replay's on-the-ME argument) */
void psp_me_serve_audio(void)
{
	psp_me_yield();
}

/* the jobs a yield runs aren't the AI job, even inside it: its stops act
only in its own work (psp_post.h's psp_aime_here) */
extern int psp_aime_job_now[16];
void psp_aime_step(int step); /* actors.c (psp_fixups.py FIXES_AIME2A) */
void psp_aime_answer_point(int step); /* actors.c: a place the word could be answered */
#if PSP_MUSIC
int psp_me_music_yield(void)
{
	int k;

	if (!psp_on_me())
		return 0;
	psp_me_yield();
	for (k = 0; k < ME_SLOTS; k++)
		if (k != ME_SLOT_AUDIO && k != ME_SLOT_MUSIC && SLOT(k)->request != SLOT(k)->done)
			return 1;
	return 0;
}
#endif

/* the ME's cycles spent inside a slot job's yields on other work (the ring
drained, a render job served): that time is counted in the slot job's own
time and again in the ring's or the render slot's, so the busy share takes
it off once (psp_me_report). Read through the uncached view, as nested_features is */
static PSP_AIME_ME_SIDE unsigned int nested_cycles_total = 0;

void psp_me_yield(void)
{
	volatile struct me_private *p;
	int aime_now;

	if (!psp_on_me())
		return;
	aime_now = psp_aime_job_now[0];
	psp_aime_job_now[0] = 0;
	me_audio_now();
	p = ME_PRIVATE;
#ifdef PSP_RECON
	/* a look that finds the ring empty is a look too */
	if (p->ring_ready && !p->in_ring && (CONTROL->features & ME_FEATURE_STASH))
		STASH[STASH_LOOK] = me_cycles();
#endif
	if (p->ring_ready && !p->in_ring && RING_PUSHED != p->ring_popped)
	{
		unsigned int last_look = p->ring_last_look;

		/* the AI job's census: its yield's ring jobs timed apart from its audio */
		if (aime_now)
			psp_aime_step(7);

		/* inside the AI job, the ring stops at its word (the rest wait for
		the ME's loop): a burst of ring jobs drained here ran 23 ms on b30
		(41178529227), past the bound the collection gives the word */
		{
			unsigned int begun = me_cycles();

			p->ring_popped = me_ring_drain(p->ring_popped, &last_look,
				aime_now ? &SLOT(ME_SLOT_AI)->args[ME_JOB_ARGS - 1] : NULL);
			nested_cycles_total += me_cycles() - begun;
		}
		p->ring_last_look = last_look;
		if (aime_now)
			psp_aime_step(0);
	}
	psp_aime_job_now[0] = aime_now;
}

/* the render jobs served inside a slot job's yields, and the longest: while
one runs, the slot job can't read its abort word, so the main CPU's wait for
it is read against these (RW1's counters; the ME writes them, its job takes
them into its results) */
static PSP_AIME_ME_SIDE unsigned int nested_renders = 0, nested_worst = 0, nested_left = 0;
/* the feature word the ME read when it served one (menestrender's bit is
the only way in, yet b30 ran one with it off: 41178529227 run 2) */
static PSP_AIME_ME_SIDE unsigned int nested_features = 0;

/* the ME's clock at the AI job's last yield, between actors or inside one
(psp_me_yield_inner) */
static PSP_AIME_ME_SIDE unsigned int inner_mark = 0;

void psp_me_yield_slots(void)
{
	volatile struct me_slot *slot;
	unsigned int request;
	int aime_now;

	if (!psp_on_me())
		return;
	inner_mark = me_cycles();
	psp_me_yield();
	aime_now = psp_aime_job_now[0];
	psp_aime_job_now[0] = 0;
	slot = SLOT(ME_SLOT_RENDER);
	/* one read of the request: read twice, a render job pushed between the
	two reads ran here with menestrender off (the nested render R3's census
	caught: 74 ms in the AI job's yield, 41281400407 run 2) */
	request = slot->request;
	if (request != slot->done && !(CONTROL->features & ME_FEATURE_NEST_RENDER))
		nested_left++;
	else if (request != slot->done)
	{
		unsigned int begun = me_cycles(), ran;

		nested_features = CONTROL->features | 0x80000000u;
		me_serve(slot, ME_AUDIO_SUMS);
		ran = me_cycles() - begun;
		nested_cycles_total += ran;
		nested_renders++;
		if (ran > nested_worst)
			nested_worst = ran;
	}
	psp_aime_job_now[0] = aime_now;
}

void psp_me_nested_take(
	unsigned int *count,
	unsigned int *worst_cycles)
{
	/* the render jobs run, and (above 16 bits) those left to the slot loop */
	*count = (nested_renders & 0xffffu) | (nested_left << 16);
	*worst_cycles = nested_worst;
	nested_renders = 0;
	nested_worst = 0;
	nested_left = 0;
}

unsigned int psp_me_nested_features(void)
{
	return *(volatile unsigned int *)((unsigned int)&nested_features | 0x40000000u);
}

/* on the ME: lines of main RAM it wrote, out to main RAM now (a copy it
must be able to undo from if it is stopped). Nothing elsewhere */
void psp_me_lines_out(const void *start, unsigned int bytes)
{
	if (psp_on_me())
		lines_writeback((unsigned int)start, bytes);
}

volatile unsigned int *psp_me_abort_word(int which)
{
	return which >= 0 && which < ME_SLOTS ? &SLOT(which)->args[ME_JOB_ARGS - 1] : NULL;
}

/* a ring job's step (psp_me.h): the slant job every 64 triangles (~1 us
each, b90), the rays' job every ray (~27 us: a job of 16 is ~430 us [M]), so
an audio block waits that long at most for the ME instead of a whole job, or
eight of them queued (2026-10-14: audio comes before everything else on
the ME). Mixing here is exact: the audio job reads and writes only the
voices, its output and its own sums, none of which a ring job touches */
void psp_me_ring_step(unsigned int step)
{
	unsigned int features;

	if (!psp_on_me())
		return;
	features = CONTROL->features;
	if (features & ME_FEATURE_STASH)
		STASH[STASH_STEP] = step;
	if ((features & ME_FEATURE_AUDIO_FIRST) && me_audio_now())
	{
		me_count(STASH_AUDIO_INSIDE);
		me_note(STASH_RING_JOB);
	}
}

#ifdef PSP_RECON
/* "mebusN" (keep.txt; PSP_RECON builds; docs/recon_134.md): how much the two
CPUs slow each other when both wait on main RAM at once, the question behind
running the display list's building on the ME beside the next tick. This
job loads main RAM from the ME until the main CPU raises the stop word: a
line at a scattered address, then N steps of arithmetic (mebus0: misses
only; ~mebus50: about the main CPU's own mix, a miss every ~120 cycles), then
a store into its 16 KB output, so its lines are written back as the GE
layer's list would be. It reads memory no job writes into (user RAM, read
only) and writes only its own output, so the game never sees it.
"mebusedram" (with mebusN): the same load on the ME's own eDRAM, reads and
stores both, so the line says what work that never leaves the ME costs the
main CPU, the question behind putting ME jobs' data there. Its reads span
1 MB of the stretch edramcheck patterns (reads only), its stores a 16 KB
scratch below that stretch: not to be run with edramcheck's watch of it */
static struct me_ring_counter bus_stop_storage;
#define BUS_STOP (UNCACHED(&bus_stop_storage)->value)
#define BUS_OUT_WORDS 4096
/* from the heap, only once "mebusN" is on (psp_recon_table); handed to the
job in its args, so the ME never reads the pointer from the main CPU's data */
static unsigned int *bus_out;
void *psp_recon_table(const char *census, unsigned long bytes);
#define BUS_BASE 0x08900000u
#define BUS_LINES (0x01000000u / 64)
#define BUS_EDRAM_BASE (ME_EDRAM + ME_EDRAM_RESIDENT)
#define BUS_EDRAM_LINES (0x00100000u / 64)
#define BUS_EDRAM_OUT (ME_EDRAM + ME_EDRAM_SCRATCH)

static void bus_load_job(const unsigned int *args, unsigned int *results)
{
	unsigned int work = args[0], limit = args[1], x = args[2] | 1u, sink = 0, n = 0, k, start = me_cycles();
	/* args[3]: 1 for the eDRAM; the main CPU can't reach it, so a simulated
	run (the emulator) keeps to main RAM */
	int edram = args[3] && psp_on_me();
	unsigned int base = edram ? BUS_EDRAM_BASE : BUS_BASE, lines = edram ? BUS_EDRAM_LINES : BUS_LINES;
	unsigned int *out = edram ? (unsigned int *)BUS_EDRAM_OUT : (unsigned int *)args[4];

	/* the stop word is an uncached main-RAM read: polled every load, it put a
	bus trip into every eDRAM load and hid the eDRAM's rate behind it, so it
	is read every 16th */
	while (((n & 15u) || !BUS_STOP) && me_cycles() - start < limit)
	{
		x = x * 1664525u + 1013904223u;
		sink += *(volatile const unsigned int *)(base + ((x >> 8) % lines) * 64u);
		for (k = 0; k < work; k++)
			sink = sink * 31u + k;
		out[(n * 16u) % BUS_OUT_WORDS] = sink;
		/* an audio block waiting is mixed now, whether or not "meaudiofirst"
		is on: the load holds the ME for a whole tick, and an audio block
		left 100 ms stops the ME for the session (the first
		runs) */
		if ((++n & 63u) == 0)
		{
			psp_me_ring_step(n);
			me_audio_now();
		}
	}
	results[0] = n;
	results[1] = me_cycles() - start;
	results[2] = sink;
}
#endif

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

/* ---------- batches (psp_me.h): the ME's side

One ring job makes records [first, first + count) of a batch, in order. Before
each it claims the record (started), then reads the main CPU's limit: a
record at or past it has been taken back, so the claim is withdrawn and the
job ends. The main CPU writes the limit before it reads started, and the ME
writes started before it reads the limit, all uncached with a sync between,
so for any record either the ME sees the limit or the main CPU sees the claim
(psp_me_batch_need waits out the second case for that one record). A record
is written back before done passes it. args: 0 the batch, 1 first, 2 count,
3 the main CPU's FPU control word, 4 make, 5 the records, 6 a record's bytes,
7-14 make's own.

done is one word for the whole batch, so psp_me_batch_need trusts every
handed-over record below it. That holds because a job ends early only at the
limit, which only falls: every job after it withdraws at its first record, and
done never passes a record the ME didn't make */
static int batch_sim_half = 0; /* "mebatchsimhalf": the main CPU's, read by the simulated ME */
/* the batch whose job the simulated ME stopped partway: an ME that is behind,
which a real one is while the main CPU takes records back. The jobs after
it wait behind it, as they would in the ring, so they make nothing; jobs that
made records after the stop would carry done past the ones never made */
static const struct psp_me_batch *batch_sim_behind = NULL;
/* whole jobs before the stop, 0-3 by turns, so the ME's records, the ones
taken back and the ones never handed over fall in different places each pass */
static unsigned int batch_sim_passes = 0, batch_sim_whole = 0;
/* "meclaimold", for the simulated ME (the real one reads ME_FEATURE_CLAIM_OLD) */
static int me_claim_old = 0;

#if PSP_MEXEC
/* On the ME: a record function MEXEC copied runs from its copy in eDRAM. A
call through a pointer is never re-aimed by the copy (tools/mexec.py), so
without this a batch's make ran from main RAM even with its code copied */
static psp_me_record_fn me_mexec_aim(psp_me_record_fn fn)
{
	const unsigned int *t = psp_mexec_table;
	unsigned int base, at = MEXEC_HEADER, k;

	if (!mexec_entry)
		return fn;
	base = (unsigned int)psp_ge_me_replay - t[2];
	for (k = 0; k < t[4]; k++, at += 3)
		if (base + t[at] == (unsigned int)fn)
			return (psp_me_record_fn)(ME_EDRAM + MEXEC_AT + t[at + 2]);
	return fn;
}
#endif

static void batch_job(const unsigned int *args, unsigned int *results, int on_me)
{
	struct psp_me_batch *b = (struct psp_me_batch *)args[0];
	volatile unsigned int *limit = &UNCACHED(&b->limit)->value, *started = &UNCACHED(&b->started)->value;
	volatile unsigned int *done = &UNCACHED(&b->done)->value, *withdrawn = &UNCACHED(&b->withdrawn)->value;
	psp_me_record_fn make = (psp_me_record_fn)args[4];
	unsigned char *records = (unsigned char *)args[5];
#if PSP_MEXEC
	if (on_me)
		make = me_mexec_aim(make);
#endif
	unsigned int r = args[1], end = args[1] + args[2], bytes = args[6] & ~63u, made = 0, control = 0, user[ME_BATCH_ARGS], k;
	int claim_old = on_me ? (CONTROL->features & ME_FEATURE_CLAIM_OLD) != 0 : me_claim_old;
	/* claims a record (bit 0 of the record size, which is whole lines) */
	int claims = (args[6] & 1u) != 0, sim_alternate = 0;

	for (k = 0; k < ME_BATCH_ARGS; k++)
		user[k] = args[7 + k];
	/* the emulator runs a pushed job inside the push: making only half of
	it there leaves the rest for psp_me_batch_need to take back, so that path
	is proven where play can be compared */
	if (!on_me && batch_sim_half)
	{
		/* with claims, an ME that is behind still makes what the main
		CPU hasn't reached: here every other record, so records made by
		each CPU interleave */
		if (batch_sim_behind == b && claims)
			sim_alternate = 1;
		else if (batch_sim_behind == b)
			end = r;
		else if (batch_sim_whole)
			batch_sim_whole--;
		else if (!batch_sim_behind && args[2] > 1)
		{
			end = args[1] + args[2] / 2;
			batch_sim_behind = b;
		}
	}
	if (on_me)
	{
		__asm__ __volatile__("cfc1 %0, $31" : "=r"(control));
		__asm__ __volatile__("ctc1 %0, $31" : : "r"(args[3]));
	}
	if (claims)
	{
		volatile unsigned int *me_on = UNCACHED(&b->me_on[0]), *made_word = UNCACHED(&b->made[0]);
		volatile unsigned int *here_on = UNCACHED(&b->here_on[0]);

		/* Each record its own claim: the ME writes its claim and then
		reads the main CPU's, the main CPU the other way round (both
		uncached, a sync between), so at least one of them sees the
		other's. A record the main CPU claimed is passed, and the job goes
		on to the next: the gather reaching one record first no longer
		hands it every record after */
		for (; r < end; r++)
		{
			unsigned char *record = records + r * bytes;

			/* made already, by a pick-up that ran this job's records ahead
			of the ring (ticksched): only the ME marks made, and never for
			a record it hasn't finished */
			if (made_word[r])
				continue;
			if (here_on[r] || (sim_alternate && !(r & 1u)))
			{
				*withdrawn = *withdrawn + 1;
				continue;
			}
			me_on[r] = 1;
			__asm__ __volatile__("sync");
			if (here_on[r])
			{
				me_on[r] = 0;
				*withdrawn = *withdrawn + 1;
				__asm__ __volatile__("sync");
				continue;
			}
			make(record, user);
			if (on_me)
				lines_writeback((unsigned int)record, bytes);
			__asm__ __volatile__("sync");
			made_word[r] = 1;
			__asm__ __volatile__("sync");
			made++;
			psp_me_ring_step(r);
		}
		r = end;
	}
	for (; r < end; r++)
	{
		unsigned char *record = records + r * bytes;

		/* A record at the limit is declined before it is claimed. started
		is one word for every job of the batch: a later job that claimed its
		first record and then withdrew would leave started at that record,
		past one an earlier job had withdrawn at, and the main CPU would wait
		for that one as if the ME still held it (until its deadline) */
		if (!claim_old && r >= *limit)
		{
			*withdrawn = *withdrawn + 1;
			__asm__ __volatile__("sync");
			break;
		}
		*started = r + 1;
		__asm__ __volatile__("sync");
		if (r >= *limit)
		{
			*started = r;
			*withdrawn = *withdrawn + 1;
			__asm__ __volatile__("sync");
			break;
		}
		make(record, user);
		if (on_me)
			lines_writeback((unsigned int)record, bytes);
		__asm__ __volatile__("sync");
		*done = r + 1;
		__asm__ __volatile__("sync");
		made++;
		psp_me_ring_step(r);
	}
	if (on_me)
		__asm__ __volatile__("ctc1 %0, $31" : : "r"(control));
	results[0] = made;
}

/* the word a job's stack allowance below the caller's stack pointer */
static inline volatile unsigned int *ring_sentinel(unsigned int allowance)
{
	unsigned int sp;

	__asm__ __volatile__("move %0, $sp" : "=r"(sp));
	return (volatile unsigned int *)((sp - allowance) & ~3u);
}

/* "ticksched" (below): a batch made ahead of the ring broke its stack check */
static PSP_AIME_ME_SIDE unsigned int tick_first_stack_broken = 0;

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
		me_count(STASH_RING_BEGUN);
		me_note(STASH_RING_DROP);
		/* the request as the main CPU wrote it back, not a copy from the last
		time this entry was used */
		lines_drop((unsigned int)request, sizeof(*request));
		if (CONTROL->features & ME_FEATURE_STASH)
		{
			STASH[STASH_TICKET] = request->ticket;
			STASH[STASH_TYPE] = request->type;
			STASH[STASH_STEP] = 0;
		}
		/* a continuing job's inputs: fresh since its hand-over's first job
		dropped them, and unchanged since (ME_RING_ARG_CONTINUES) */
		if (!request->args[ME_RING_ARG_CONTINUES])
		{
			for (k = 0; k < ME_RING_INPUTS; k++)
				inputs += request->input_bytes[k];
			if (inputs > RING_WHOLE_CACHE_BYTES)
				meLibDcacheWritebackInvalidateAll();
			else
				for (k = 0; k < ME_RING_INPUTS; k++)
					if (request->input_bytes[k])
						lines_drop(request->input[k], request->input_bytes[k]);
		}
		me_note(STASH_RING_JOB);
	}
	for (k = 0; k < ME_RING_ARGS; k++)
		args[k] = request->args[k];
	for (k = 0; k < ME_RING_RESULTS; k++)
		results[k] = 0;
	sentinel = ring_sentinel(request->type == ME_JOB_PERC_RAYS || request->type == ME_JOB_POINTS ?
		RING_RAYS_STACK_BYTES : RING_JOB_STACK_BYTES);
	*sentinel = RING_SENTINEL;
	if (on_me)
	{
		ME_PRIVATE->in_ring = 1;
		job_start = me_cycles();
	}
	switch (request->type)
	{
	case ME_JOB_SLANT:
		psp_ge_me_slant(args, results);
		break;
	case ME_JOB_RINGTEST:
		ring_test_job(args, results);
		break;
	case ME_JOB_PERC_RAYS:
		psp_perception_me_rays(args, results);
		break;
	case ME_JOB_POSES:
		psp_pose_me_make(args, results);
		break;
	case ME_JOB_BATCH:
		batch_job(args, results, on_me);
		break;
	case ME_JOB_POINTS:
		psp_points_me_make(args, results);
		break;
#if defined(PSP_WALKAPART) || defined(PSP_MEVISLAND)
	case ME_JOB_VISIBILITY:
		psp_mevis_me_job(args, results);
		break;
#endif
#ifdef PSP_RECON
	case ME_JOB_BUSLOAD:
		bus_load_job(args, results);
		break;
#endif
	default:
		status = RING_BAD_TYPE;
		break;
	}
	if (on_me)
	{
		job_end = me_cycles();
		ME_PRIVATE->in_ring = 0;
	}
	/* a job that ran past its stack allowance may have written over whatever
	lies below it: reported, and the ring stops */
	if (*sentinel != RING_SENTINEL)
		status = RING_STACK;
	/* a batch made ahead of the ring at a pick-up that broke its stack
	allowance is reported by the next job the ring finishes */
	if (on_me && tick_first_stack_broken)
	{
		tick_first_stack_broken = 0;
		status = RING_STACK;
	}
	if (on_me)
		me_note(STASH_RING_WRITE);
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
#if PSP_RECON
	result->who = on_me ? (me_occ_at_look << 8) | me_occ_now : 0;
#else
	result->who = 0;
#endif
	for (k = 0; k < ME_RING_RESULTS; k++)
		result->results[k] = results[k];
	result->all_cycles = on_me ? me_cycles() - start : 0;
	if (on_me)
	{
		lines_writeback((unsigned int)result, sizeof(*result));
		me_count(STASH_RING_ENDED);
		me_note(STASH_IDLE);
	}
	__asm__ __volatile__("sync");
}

/* the ME: every job pushed since its last look, in order. last_look: the
cycle count when the ring was last seen with nothing waiting past popped.
Audio first: an audio block asked for meanwhile is mixed before each job,
so it waits for one job at most, not for all eight the ring holds */
/* the ring jobs run in the AI job's drains (stop set): the longest, its
type, and the visibility jobs left to the ME's loop (aimevisleft); the ME
writes them, the main CPU reads them uncached (psp_me_report) */
static PSP_AIME_ME_SIDE unsigned int aime_drain_worst = 0, aime_drain_worst_type = 0, aime_drain_vis_left = 0,
	aime_drain_vis_run = 0;

static unsigned int me_ring_drain(unsigned int popped, unsigned int *last_look, volatile const unsigned int *stop)
{
	unsigned int looked = me_cycles(), pushed = RING_PUSHED;
	int audio_first = (CONTROL->features & ME_FEATURE_AUDIO_FIRST) != 0;

	if (CONTROL->features & ME_FEATURE_STASH)
	{
		STASH[STASH_PUSHED] = pushed;
		STASH[STASH_LOOK] = looked;
	}
	while (popped != pushed && !(stop && *stop))
	{
		/* inside the AI job the word was just tested: a place it could be
		answered, so the census's stretch is one ring job, not the drain */
		unsigned int type = 0, began = 0;

		if (stop)
		{
			struct me_ring_request *request = &ring_requests[popped & RING_MASK];

			psp_aime_answer_point(7);
			/* the request as the main CPU wrote it (ring_run drops it again) */
			lines_drop((unsigned int)request, sizeof(*request));
			type = request->type;
			if (type == ME_JOB_VISIBILITY)
			{
				if (CONTROL->features & ME_FEATURE_AIME_VIS_LEFT)
				{
					aime_drain_vis_left++;
					break;
				}
				aime_drain_vis_run++;
			}
		}
		if (audio_first && me_audio_now())
			me_count(STASH_AUDIO_INSIDE);
		if (stop)
			began = me_cycles();
		ring_run(popped & RING_MASK, 1, me_cycles() - *last_look);
		if (stop && me_cycles() - began > aime_drain_worst)
		{
			aime_drain_worst = me_cycles() - began;
			aime_drain_worst_type = type;
		}
		popped++;
		RING_POPPED = popped;
		__asm__ __volatile__("sync");
	}
	if (stop)
		psp_aime_answer_point(7);
	/* anything pushed after the read above came after this moment */
	*last_look = looked;
#if PSP_RECON
	me_occ_at_look = me_occ_now; /* "mewaitwho": the next job's wait begins under this occupant */
#endif
	return popped;
}

/* ----- "ticksched": the replay's pick-up points (docs/box_tick_schedule.md).
Today's replay serves the ring before every record, so each batch job pushed
during the tick runs alone between two records: its code (the sweep's ~9 KB
copy, the ring's ~4 KB) and the replay's evict each other in the 16 KB
I-cache once a job. Held to a pick-up a slice apart, the jobs pushed in that
slice run back to back, one refill a slice. A job the main CPU is already
waiting for (ring_urgent) is picked up at the next record, so a wait grows
by one record at most; and a pick-up makes the waiting batches' records
first (ring_batch_first), so a long frame job ahead of them in the ring
(mevisland's visibility) doesn't hold the tick's work */

/* the main CPU while it waits in psp_me_wait: the ticket plus one, else 0.
One writer (the main CPU), read by the ME through the uncached view */
static struct me_ring_counter ring_urgent_storage;
#define RING_URGENT (UNCACHED(&ring_urgent_storage)->value)

/* the slice in ME cycles: "tickschedusN" (default 500 us), "tickschedrecN"
(default 0, none) caps the records between two pick-ups as well */
#define TICK_SLICE_US_DEFAULT 500
/* one word to the ME: the slice's cycles in the low 24 bits (to ~50 ms),
the record cap in the top 8 */
static void tick_slice_set(volatile struct me_control *control)
{
	int us = psp_keep_number("tickschedus", TICK_SLICE_US_DEFAULT), records = psp_keep_number("tickschedrec", 0);
	unsigned int cycles;

	if (us < 0 || us > 50000)
		us = TICK_SLICE_US_DEFAULT;
	cycles = (unsigned int)((float)us * (me_cycles_per_us > 0.0f ? me_cycles_per_us : 333.0f));
	if (cycles > 0x00FFFFFFu)
		cycles = 0x00FFFFFFu;
	control->tick_slice_cycles = cycles | ((unsigned int)(records > 255 ? 255 : records > 0 ? records : 0) << 24);
	if (control->features & ME_FEATURE_TICKSCHED)
		platform_log("ME: ticksched: the replay serves the ring at pick-up points %d us apart%s, waiting batches first",
			us, records > 0 ? " (or a record cap)" : "");
}

/* the replay's looks, counted on the ME (lines of their own, written only
there; the main CPU reads them uncached in psp_me_report): both modes count
the time served inside the replay, so each side of an A/B has the replay's
own time (its job less what it served) */
struct tick_look_stats
{
	unsigned int served_cycles; /* inside the replay's looks: audio and the ring */
	unsigned int looks;         /* the replay's looks (one a record) */
	unsigned int pickups;       /* looks that ran ring jobs */
	unsigned int ring_jobs;     /* ring jobs those ran */
	unsigned int held;          /* looks that saw jobs waiting and left them to the slice */
	unsigned int urgent;        /* pick-ups made early for a waiting main CPU */
	unsigned int batch_first;   /* batch jobs whose records a pick-up made first */
	unsigned int audio_inside;  /* audio blocks mixed at the replay's looks */
	unsigned int audio_held;    /* of them, audiohold's: blocks that had waited past its due time */
	unsigned int pad[7];
} __attribute__((aligned(64)));
static struct tick_look_stats tick_look PSP_AIME_ME_SIDE = { 0 };
/* the ME's own: the clock at its last pick-up and the records since */
static PSP_AIME_ME_SIDE unsigned int tick_pick_mark = 0, tick_records_since = 0;

/* on the ME, at a pick-up: each waiting batch job's records made now, in
the ring's order, by the job's own code; the job, when the drain reaches it,
finds them made and passes them (batch_job). Only claims' batches: under the
one limit word a record isn't marked made on its own */
static void ring_batch_first(unsigned int popped, unsigned int pushed)
{
	volatile struct me_private *p = ME_PRIVATE;

	for (; popped != pushed; popped++)
	{
		struct me_ring_request *request = &ring_requests[popped & RING_MASK];
		unsigned int args[ME_RING_ARGS], results[ME_RING_RESULTS], k;
		volatile unsigned int *sentinel;

		lines_drop((unsigned int)request, sizeof(*request));
		if (request->type != ME_JOB_BATCH || !(request->args[6] & 1u))
			continue;
		if (!request->args[ME_RING_ARG_CONTINUES])
			for (k = 0; k < ME_RING_INPUTS; k++)
				if (request->input_bytes[k])
					lines_drop(request->input[k], request->input_bytes[k]);
		for (k = 0; k < ME_RING_ARGS; k++)
			args[k] = request->args[k];
		sentinel = ring_sentinel(RING_JOB_STACK_BYTES);
		*sentinel = RING_SENTINEL;
		p->in_ring = 1;
		batch_job(args, results, 1);
		p->in_ring = 0;
		if (*sentinel != RING_SENTINEL)
			tick_first_stack_broken = 1;
		tick_look.batch_first++;
	}
}

void psp_me_replay_look(void)
{
	volatile struct me_private *p;
	unsigned int began, now, pushed, slice, cap;
	int urgent;

	if (!psp_on_me())
		return;
	began = me_cycles();
	tick_look.looks++;
	if (!(CONTROL->features & ME_FEATURE_TICKSCHED))
	{
		psp_me_yield();
		tick_look.served_cycles += me_cycles() - began;
		return;
	}
	/* audio keeps its every-record look: its deadline is its own. Under
	audiohold a block waits for the replay's end unless it would run dry */
	if (!(CONTROL->features & ME_FEATURE_AUDIO_HOLD))
		tick_look.audio_inside += (unsigned int)me_audio_now();
	else if (me_audio_due(began) && me_audio_now())
	{
		tick_look.audio_inside++;
		tick_look.audio_held++;
	}
	p = ME_PRIVATE;
	pushed = RING_PUSHED;
	tick_records_since++;
	if (!p->ring_ready || p->in_ring || pushed == p->ring_popped)
	{
		tick_look.served_cycles += me_cycles() - began;
		return;
	}
	now = me_cycles();
	slice = CONTROL->tick_slice_cycles;
	cap = slice >> 24;
	urgent = RING_URGENT != 0;
	if (!urgent && now - tick_pick_mark < (slice & 0x00FFFFFFu) && !(cap && tick_records_since >= cap))
	{
		tick_look.held++;
		tick_look.served_cycles += me_cycles() - began;
		return;
	}
	tick_look.urgent += urgent && now - tick_pick_mark < (slice & 0x00FFFFFFu) ? 1u : 0u;
	{
		unsigned int popped = p->ring_popped, last_look = p->ring_last_look, drain_began = me_cycles();

		ring_batch_first(popped, pushed);
		p->ring_popped = me_ring_drain(popped, &last_look, NULL);
		p->ring_last_look = last_look;
		tick_look.ring_jobs += p->ring_popped - popped;
		nested_cycles_total += me_cycles() - drain_began;
	}
	tick_look.pickups++;
	tick_pick_mark = me_cycles();
	tick_records_since = 0;
	tick_look.served_cycles += tick_pick_mark - began;
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

/* mewakeprobe's main-RAM buffers: the first written with a pattern by the
main CPU before the release (psp_me_wake_poll), the second written by the ME */
static unsigned int wake_probe_in[1024] __attribute__((aligned(64)));
static unsigned int wake_probe_out[1024] __attribute__((aligned(64)));
static volatile float wake_probe_sink;

#define WAKE_PROBE_WORD(k) ((unsigned int)(k) * 2654435761u)

static unsigned int wake_probe_deep(unsigned int depth) __attribute__((noinline));
static unsigned int wake_probe_deep(unsigned int depth)
{
	volatile unsigned int frame[256]; /* 1 KB of stack a level */
	unsigned int k, sum = 0;

	for (k = 0; k < 256; k++)
		frame[k] = depth + k;
	if (depth)
		sum = wake_probe_deep(depth - 1);
	for (k = 0; k < 256; k++)
		sum += frame[k];
	return sum;
}

/* What a job does that the idle loop doesn't, a step at a time: the FPU, a
cached read of main RAM, a cached write of main RAM written back, the
eDRAM through the cache (mebusedram's scratch, free in play builds), and 24
KB of stack. The mark names the step begun, so a hang names its step; a step
that reads back wrong sets its bit and the probe goes on */
static void me_wake_probe(void)
{
	volatile unsigned int *edram = (volatile unsigned int *)(ME_EDRAM + ME_EDRAM_SCRATCH);
	unsigned int fails = 0, sum, k;

	hw(ME_MARK_PROBE_FAIL) = 0;
	hw(ME_MARK_PROBE) = ME_MARK_PROBE_BASE + 1;
	meLibSync();
	{
		volatile float a = 1.5f, b = 2.25f;

		wake_probe_sink = a * b + a / b;
		if (wake_probe_sink < 4.0f || wake_probe_sink > 4.1f)
			fails |= 1;
	}
	hw(ME_MARK_PROBE) = ME_MARK_PROBE_BASE + 2;
	meLibSync();
	for (k = 0, sum = 0; k < 1024; k++)
		sum += wake_probe_in[k] ^ WAKE_PROBE_WORD(k);
	if (sum)
		fails |= 2;
	hw(ME_MARK_PROBE) = ME_MARK_PROBE_BASE + 3;
	meLibSync();
	for (k = 0; k < 1024; k++)
		wake_probe_out[k] = ~WAKE_PROBE_WORD(k);
	meLibDcacheWritebackInvalidateRange((u32)wake_probe_out, sizeof(wake_probe_out));
	for (k = 0, sum = 0; k < 1024; k++)
		sum += UNCACHED(&wake_probe_out[k])[0] ^ ~WAKE_PROBE_WORD(k);
	if (sum)
		fails |= 4;
	hw(ME_MARK_PROBE) = ME_MARK_PROBE_BASE + 4;
	meLibSync();
	for (k = 0; k < 2048; k++)
		edram[k] = WAKE_PROBE_WORD(k + 7);
	meLibDcacheWritebackInvalidateRange((u32)edram, 2048 * 4);
	for (k = 0, sum = 0; k < 2048; k++)
		sum += edram[k] ^ WAKE_PROBE_WORD(k + 7);
	if (sum)
		fails |= 8;
	hw(ME_MARK_PROBE) = ME_MARK_PROBE_BASE + 5;
	meLibSync();
	if (wake_probe_deep(24) == 0)
		fails |= 16;
	hw(ME_MARK_PROBE_FAIL) = fails;
	hw(ME_MARK_PROBE) = ME_MARK_PROBE_BASE + 0xf;
	meLibSync();
}

#if PSP_MEXEC
/* On the ME: the table's functions copied from their loaded code into the
eDRAM, the calls between them and the branches out of them re-aimed, the
copy's lines written back and dropped from the I-cache. A call to a function
not copied stands as the loader relocated it: the ME runs its code through
the cached alias, and jal keeps the PC's top four bits */
static void me_mexec_install(volatile struct me_control *control)
{
	const unsigned int *t = psp_mexec_table;
	unsigned int base, copy = ME_EDRAM + MEXEC_AT, at = MEXEC_HEADER, k, sum = 0, tramp_at, line;

	if (t[0] != MEXEC_MAGIC || t[1] != 1 || t[3] > MEXEC_ROOM || !t[4])
	{
		control->mexec_state = 2;
		return;
	}
	base = (unsigned int)psp_ge_me_replay - t[2];
	for (k = 0; k < t[4]; k++, at += 3)
	{
		const unsigned int *from = (const unsigned int *)(base + t[at]);
		unsigned int *to = (unsigned int *)(copy + t[at + 2]), w;

		for (w = 0; w < t[at + 1] / 4; w++)
			to[w] = from[w];
	}
	for (k = 0; k < t[5]; k++, at += 2)
	{
		unsigned int *word = (unsigned int *)(copy + t[at]);

		*word = (*word & 0xFC000000u) | (((copy + t[at + 1]) >> 2) & 0x03FFFFFFu);
	}
	tramp_at = t[11];
	for (k = 0; k < t[6]; k++, at += 2)
	{
		unsigned int *tramp = (unsigned int *)(copy + tramp_at + 8 * k);
		unsigned int to = t[at] ? copy + t[at + 1] : base + t[at + 1];

		tramp[0] = 0x08000000u | ((to >> 2) & 0x03FFFFFFu); /* j to */
		tramp[1] = 0;                                         /* nop */
	}
	for (k = 0; k < t[7]; k++, at += 2)
	{
		unsigned int *word = (unsigned int *)(copy + t[at]);
		unsigned int tramp = copy + tramp_at + 8 * t[at + 1];

		*word = (*word & 0xFFFF0000u) | (((tramp - (copy + t[at] + 4)) >> 2) & 0xFFFFu);
	}
	/* the lui/%lo pairs that load a jump table's address, re-aimed at its copy */
	for (k = 0; k < t[8]; k++, at += 2)
	{
		unsigned int *word = (unsigned int *)(copy + t[at]);

		*word = (*word & 0xFFFF0000u) | (t[at + 1] & 0xFFFFu);
	}
	/* the jump tables, their words the copy's addresses */
	for (k = 0; k < t[10]; k++)
	{
		unsigned int *to = (unsigned int *)(copy + t[at]), words = t[at + 1], w;

		for (w = 0; w < words; w++)
			to[w] = t[at + 2 + w];
		at += 2 + words;
	}
	for (k = 0; k < t[3] / 4; k++)
		sum += ((const unsigned int *)copy)[k];
	__asm__ __volatile__("sync");
	for (line = copy; line < copy + t[3]; line += 64)
		__asm__ __volatile__("cache 0x1b, 0(%0)" : : "r"(line));
	__asm__ __volatile__("sync");
	for (line = copy; line < copy + t[3]; line += 64)
		__asm__ __volatile__("cache 0x08, 0(%0)" : : "r"(line));
	__asm__ __volatile__("sync");
	control->mexec_bytes = t[3];
	control->mexec_patches = t[5] + t[7];
	control->mexec_sum = sum;
	mexec_entry = copy + t[MEXEC_HEADER + 2];
#if PSP_MEXEC_JOBS
	for (k = 0, at = MEXEC_HEADER; k < t[4]; k++, at += 3)
		if (base + t[at] == (unsigned int)run_job)
			mexec_run = copy + t[at + 2];
#endif
	control->mexec_state = 1;
}
#endif

__attribute__((noinline, aligned(4)))
void meLibOnProcess(void)
{
	volatile struct me_control *control = CONTROL;
	int sums[MIX_FRAMES_MAX * 2];
	int halt_ready = 0; /* mehalt: the exception handler is set up */
	int ring_ready = 0; /* the main CPU has turned the ring on */
	unsigned int ring_popped = 0, ring_last_look = 0, ring_quiet = RING_LINGER_PASSES;
	int features_seen = 0; /* the features word has been written (mebcc1 applied) */
	unsigned int fault_test_at = 0; /* mefaulttest: when, by the ME's cycle counter */

#if PSP_SMALLDATA
	/* The engine's units read their small variables through $gp
	(PSP_SMALLDATA), and the ME's reset leaves it unset, where the kernel
	gives each of the main CPU's threads the module's value. The ME's jobs run
	engine code, so it takes the same value here, before any of them; its
	interrupts save and restore it. This unit itself is built without small
	data, so nothing above needs it */
	__asm__ __volatile__("la $gp, _gp");
#endif
	hw(ME_MARK_PROCESS) = ME_MARK_PROCESS_IN;
	meLibSync();
#if PSP_MEXEC
	mexec_entry = 0;
#if PSP_MEXEC_JOBS
	mexec_run = 0;
#endif
#endif
	/* build 88: the cycle counter (coprocessor 0 register 9) stood still on
	a PSP-1000 (b87: "runs at 0.0 a microsecond"), so every ME time read
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
	{
		/* the CPU number, which the ME's kernel mode can read: logged by the
		main CPU, a check that this code runs where psp_me.h says */
		unsigned int number;

		__asm__ __volatile__("sync\n\tmfc0 %0, $22\n\tsync" : "=r"(number));
		control->cpu_number = number;
		control->cpu_seen = 1;
	}
	meLibDcacheWritebackInvalidateAll();
	/* the features aren't set yet: the main-RAM copy; the eDRAM copy is
	written before each job that reads it */
	me_private_main.ring_ready = 0;
	me_private_main.in_ring = 0;
	hw(ME_MARK_PROCESS) = ME_MARK_PROCESS_RUN;
	meLibSync();
	if (control->features & ME_FEATURE_WAKE_PROBE)
		me_wake_probe();
	for (;;)
	{
		int k;

		/* the features word is looked at every 256th pass (it is set once,
		after start), so the polling idle costs what it would without it */
		if ((control->alive & 255) == 0 && (!halt_ready || !ring_ready || !features_seen))
		{
			unsigned int features = control->features;

			if (features && !features_seen)
			{
				features_seen = 1;
				if (!(features & (ME_FEATURE_FAULT_OFF | ME_FEATURE_HALT)))
					me_fault_vector_install();
				if (features & ME_FEATURE_FAULT_TEST)
				{
					fault_test_at = me_cycles() + 333u * 1000000u * 5u;
					UNCACHED(&psp_me_fault_storage)->pad[0] = 1; /* armed */
				}
				if (features & ME_FEATURE_BCC)
				{
					hw(0xBCC00040) = 1;
					meLibSync();
				}
#if PSP_MEXEC
				if (features & ME_FEATURE_MEXEC)
					me_mexec_install(control);
#endif
			}

			if ((features & ME_FEATURE_HALT) && !halt_ready)
			{
				meLibExceptionHandlerInit(0);
				halt_ready = 1;
			}
			if ((features & ME_FEATURE_RING) && !ring_ready)
			{
				ring_ready = 1;
				/* 0 at launch; after a wake the main CPU has moved both
				counters past every ticket handed out before the sleep */
				ring_popped = RING_POPPED;
				ring_last_look = me_cycles();
			}
		}
		control->alive++;
		control->clock = me_cycles();
		if (fault_test_at && (int)(control->clock - fault_test_at) > 0)
		{
			UNCACHED(&psp_me_fault_storage)->pad[0] = 2; /* about to break */
			__asm__ __volatile__("break");
		}
		/* build 89: the stop word is looked at after the slots (below), so a
		job asked for just before a stop is still served: b86-b88 halted with
		it pending and its caller waited 40 ms holding the mixer's lock.
		The ring after the audio slot: a ring job is small and the main CPU
		may be waiting for it; audio is due on time */
		for (k = 0; k < ME_SLOTS; k++)
		{
			volatile struct me_slot *slot = SLOT(k);

#if PSP_MUSIC
			/* the music job only in the ME's idle time: every other slot
			and the ring empty (it yields to them once started) */
			if (k == ME_SLOT_MUSIC && slot->request != slot->done)
			{
				int j, other = ring_ready && RING_PUSHED != ring_popped;

				for (j = 0; j < ME_SLOTS && !other; j++)
					other = j != ME_SLOT_MUSIC && SLOT(j)->request != SLOT(j)->done;
				if (other)
					continue;
			}
#endif
			if (slot->request != slot->done)
			{
				volatile struct me_private *p = ME_PRIVATE;

				if (k != ME_SLOT_AUDIO)
				{
					p->ring_ready = ring_ready && !(control->features & ME_FEATURE_NO_YIELD);
					p->ring_popped = ring_popped;
					p->ring_last_look = ring_last_look;
					p->in_ring = 0;
				}
				me_serve(slot, sums);
				if (k != ME_SLOT_AUDIO)
				{
					if (p->ring_popped != ring_popped)
						ring_quiet = 0;
					ring_popped = p->ring_popped;
					ring_last_look = p->ring_last_look;
					p->ring_ready = 0;
				}
			}
			if (k == ME_SLOT_AUDIO && ring_ready)
			{
				unsigned int before = ring_popped;

				ring_popped = me_ring_drain(ring_popped, &ring_last_look, NULL);
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
	me_note(STASH_STOPPED);
	control->stop = 2;
	__asm__ __volatile__("sync");
	meLibHalt();
}

/* the library's sleep handler (its replacement for the firmware's own ME one)
calls these in kernel mode: the ME held in reset through the sleep and after
it, and the main CPU runs the jobs until the wake's restart (psp_me_wake_poll)
has the ME answering again. Nothing more here: kernel mode is no place to
wait for the ME, and the game's threads may be anywhere in a job */
void meLibOnSleep(void)
{
	sysregs_read(&regs_sleep);
	reset_hold();
	me_state = -1;
	me_sleeps++;
}

static void wake_early_restart(void);

void meLibOnWake(void)
{
	sysregs_read(&regs_wake);
	reset_hold();
	me_state = -1;
	me_sleeps++;
	wake_early_restart();
}

/* ---------- the main CPU's side */

/* the log's figures, per slot, since the last report */
static struct
{
	unsigned long jobs, failures, simulated;
	unsigned long long me_cycles, me_job_cycles, wait_us, handover_us;
	unsigned long me_worst_us, wait_worst_us;
	/* me_run_slot's looks after the first, and the first's planned time */
	unsigned long looks, first_look_us;
} stats[ME_SLOTS];
/* the last job's looks after the first, per slot (psp_me_audio's adaptive
first look reads the audio slot's) */
static unsigned int last_looks[ME_SLOTS];

/* The audio job's first look ("mepoll", design_mepoll.md). The audio thread
sleeps while the ME mixes, looking at the slot after a first sleep and then
every 100 us; each look wakes it above the game's thread (30-60 us of the
game's CPU on the PSP-1000). The job is ~0.7 ms of the ME's time, but it
waits 0.9-2.4 ms behind the ME's other work, so a first look at 200 us is
followed by ~10 more [M]. By default (200) it is as before.
"mepoll" (audiolook0, the default under it): the first look at the last
block's wait less AUDIO_LOOK_MARGIN_US, so a block takes one to three looks.
When the first look already found the job done, the wait isn't known (it may
have ended long before), so the next first look comes AUDIO_LOOK_MARGIN_US
earlier; the measured wait is used only when there were later looks, so
the delay's own overshoot can't ratchet the look later block after block.
At most AUDIO_LOOK_MAX_US: a look later than the job's end holds the mixer's
lock (the game thread may wait for it) and delays the block's output by as
much, against a 23 ms block; the late-blocks count says if it ever costs.
"mepoll audiolookN" (N > 0): a fixed first look at N us */
#define AUDIO_LOOK_DEFAULT_US 200
#define AUDIO_LOOK_MARGIN_US 300
#define AUDIO_LOOK_MAX_US 2000
static int audio_look_adaptive = 0;
static unsigned int audio_first_look_us = AUDIO_LOOK_DEFAULT_US;

static void audio_look_start(void)
{
	int n;

	if (!psp_keep("mepoll"))
		return;
	n = psp_keep_number("audiolook", 0);
	audio_look_adaptive = n <= 0;
	audio_first_look_us = n <= 0 ? AUDIO_LOOK_DEFAULT_US : (unsigned int)(n > AUDIO_LOOK_MAX_US ? AUDIO_LOOK_MAX_US : n);
	platform_log("ME: mepoll: the audio job's first look %s", audio_look_adaptive ? "adaptive" : "fixed");
}

static void audio_look_next(unsigned long waited_us)
{
	unsigned long next;

	if (!audio_look_adaptive)
		return;
	if (!last_looks[ME_SLOT_AUDIO])
		next = audio_first_look_us > AUDIO_LOOK_DEFAULT_US + AUDIO_LOOK_MARGIN_US ?
			audio_first_look_us - AUDIO_LOOK_MARGIN_US : AUDIO_LOOK_DEFAULT_US;
	else
		next = waited_us > AUDIO_LOOK_DEFAULT_US + AUDIO_LOOK_MARGIN_US ? waited_us - AUDIO_LOOK_MARGIN_US : AUDIO_LOOK_DEFAULT_US;
	audio_first_look_us = (unsigned int)(next > AUDIO_LOOK_MAX_US ? AUDIO_LOOK_MAX_US : next);
}
static unsigned long long report_mark = 0;
static float busy_percent_last = 0.0f;
static unsigned long long nested_us_last = 0;

/* "mehalt" on this side. A wake is a kernel call (me-core's
meLibEmitSoftwareInterrupt), so it is sent only to a halted ME, and again
on each look while a job waits (me_halt_idle says why). The first 256 are
timed, for the kernel call's cost */
static int me_halting = 0;
static int ring_on = 0; /* the job ring, decided at start */
/* a push writes back and drops the job's output lines itself, so no line of
the output can be dirty here when the ME writes it ("mepushdrop"; otherwise
the caller's care, as before) */
static int ring_push_drop = 0;
/* perception's rays: one cache hand-over for a tick's jobs, not one a job
("meraysbatch"; otherwise one a job, as before) */
static int rays_batch = 0;
static int me_yield_off = 0;
/* "megraceoff": the ring's and the batches' deadlines as they were, the
first wait past one stopping the ME (see BATCH_RECORD_DEADLINE_US) */
static int me_grace_off = 0;
/* a late ring job's rest (RING_DEADLINE_US says what it is): the first
RING_REST_US; again within RING_AGAIN_US of coming back, twice the last, up
to RING_REST_MAX_US */
#define RING_REST_US 1000000ull
#define RING_REST_MAX_US 16000000ull
#define RING_AGAIN_US 5000000ull
static int ring_give_up = 0;              /* "ringgiveup" */
/* "ringslotdone": a non-yielding slot job the ME has finished no longer
refuses the ring. It stays submitted until the main CPU collects it (the sound
tier's copy, at the next sound_render's end, about a frame later), but the ME
runs nothing more for it, so a ring job pushed then waits only on what is
running */
static int ring_slot_done = 0;
static unsigned long ring_slot_passed = 0;
static unsigned int ring_lates = 0, ring_revivals = 0;
static unsigned long long ring_late_mark = 0, ring_revive_mark = 0, ring_rest_base_us = RING_REST_US, ring_rest_us = RING_REST_US;
#ifdef PSP_RECON
/* "ringlateN" (recon): ring jobs whose ticket is a multiple of N (N > 0)
are taken as late by the wait, so the rest and the revival can be watched
where no job is ever late (the emulator runs a job inside its push).
Simulated, the ME is also behind from that job until the revival check
lets it catch up, as a real late ME is. "ringhang" (with ringlateN): it
never catches up, as a hung ME, and the ring must stay off */
static unsigned int ring_late_every = 0;
static int ring_hang_test = 0, ring_sim_behind = 0;
#endif
/* lines said about waits past a deadline, at most DEADLINE_LINES */
static unsigned int deadline_lines = 0;
#define DEADLINE_LINES 8

/* The slant split is on by default: the rig showed it exact (0 of 414,000
triangles differing, 0 failed jobs) and a little faster (b30 and c20: the
sort 0.6-1.4 ms a frame less). "meslantoff" turns it off; "nomeaudio" keeps
the ME stopped unless "meslant" asks for the ring */
int psp_me_rays_wanted(void);

int psp_me_slant_wanted(void)
{
	if (psp_keep("meslantoff"))
		return 0;
	return psp_keep("meslant") || !psp_keep("nomeaudio");
}

/* Perception's rays on the ME, on by default the same way: the rig showed
the real ME's answers exact (0 of 31,940 checked mismatched, 0 failed jobs)
and a50's tick 1.0-1.8 ms shorter. "meraysoff" turns it off; "nomeaudio"
keeps the ME stopped unless "meraysperc" asks for the ring */
int psp_me_rays_wanted(void)
{
	if (psp_keep("meraysoff"))
		return 0;
	return psp_keep("meraysperc") || !psp_keep("nomeaudio");
}
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
	reset_hold();
	return 0;
}

static int me_sysregs_kernel(void *regs)
{
	sysregs_read((struct me_sysregs *)regs);
	return 0;
}

/* the launch's read, with the ME running: the system registers alone, not
the ME's two words in its own RAM, which it may be running from or writing.
The one hang seen at start-up (the PSP-3000) came after the line before this
read and before its own; reading the ME's RAM under its feet is the one part
of the read no other moment does. "memarksatlaunch" reads them as before */
static int me_sysregs_running_kernel(void *regs)
{
	struct me_sysregs *r = (struct me_sysregs *)regs;

	r->reset = HW_SYS_RESET_ENABLE;
	r->bus_clock = HW_SYS_BUS_CLOCK_ENABLE;
	r->clock = hw(0xbc100058);
	r->avc = HW_SYS_AVC_POWER;
	r->marks[0] = r->marks[1] = 0;
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

/* 1 (the ME's) or 2 (simulated) once started at launch; 0 after a stop for
any other reason than a sleep (a job that timed out, HOME), since an ME that
failed once is not trusted again by a wake */
static int me_restartable = 0;
/* the features word the ME reads at a restart: launch's, so a wake runs as
launch does (before, meaudiofirst, meaudioranges and mestash were left out),
with mewakeprobe's probe */
static unsigned int wake_features = 0;
/* "mewakeearly": the ME released in the kernel's wake handler (meLibOnWake,
the firmware's resume phase 0x10005, where its own ME boot runs and where
me-core's own sleep path releases it), not at a frame end after the resume;
its jobs are still handed back at a frame end (psp_me_wake_poll) */
static int wake_early = 0;
/* "mewakefw": the restart lets go of the blocks around the ME that launch
found running and the wake left in reset. b132 test 7's registers [M]:
the reset register read 0 at launch, and 0x20 after the restart, the AVC
block's reset (uOFW's sysreg driver: 0x08 AW, 0x10 VME, 0x20 AVC) still
held, which the firmware's own ME boot lets go of last
(sceSysregAvcResetDisable, me_wrapper's boot) and ours never did. The ME
then made 91 idle polls and hung in its first job */
static int wake_fw = 0;
#define WAKE_FW_RESETS 0x28u /* AW and AVC; the VME's (0x10) goes with the ME's */
static volatile int wake_released_early = 0;
static unsigned long long wake_early_release;
static void wake_prepare(void);
static int me_restart_kernel(void *unused);

/* 1 from a release of the ME's reset (launch, a restart) until it is seen
halted or is held in reset again: while set, the ME may still be running
our code, and the exit holds it before the firmware takes the PSP back */
static int me_maybe_running = 0;
/* the library's kernel helper is loaded: kernel calls can be made */
static int me_kernel_ready = 0;

volatile unsigned int psp_me_stops = 0;

int psp_me_simulated(void)
{
	return me_state == 2;
}

/* the part of the eDRAM the emulator maps, as offsets [lo, hi): asked once
at the start, so a simulated job never makes the call; none on a PSP */
static unsigned int sim_edram_lo = 0, sim_edram_hi = 0;

int psp_me_sim_edram_holds(unsigned int offset, unsigned int bytes)
{
	return me_state == 2 && offset >= sim_edram_lo && offset < sim_edram_hi && bytes <= sim_edram_hi - offset;
}

/* psp_ge.c's mechunk: the frame's job reads its records as the main CPU
publishes them. On the ME, before each piece, what it has cached written back
and dropped (the line the last piece ended in held words written since);
whether a stop is asked, so a job waiting for more ends; and, on the main
CPU, the job's watchdog started again at each piece: a frame recorded slowly
(a hitch, a load) is no ME late */
void psp_me_cache_refresh(void)
{
	if (psp_on_me())
		meLibDcacheWritebackInvalidateAll();
}

int psp_me_stop_asked(void)
{
	return CONTROL->stop != 0;
}

static void me_stop(const char *why, int for_sleep)
{
	volatile struct me_control *control = CONTROL;
	int k;

	if (me_state > 0)
		psp_me_stops++;
	if (me_state > 0 && !for_sleep)
		me_restartable = 0;
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
		me_maybe_running = 0;
		if (control->stop == 2)
			platform_log("ME: stopped (%s); halted itself after %d ms; the main CPU runs its jobs from here", why, k);
		else
		{
			volatile struct me_fault *fault = UNCACHED(&psp_me_fault_storage);

			kcall_2(me_hold_reset, me_kseg);
			platform_log("ME: stopped (%s): NO answer in 100 ms, held in reset; the main CPU runs its jobs from here", why);
			/* B31: where it faulted (EPC is in kseg0: less 0x88804000, the
			link map's address), or that it didn't */
			if (PSP_MEPROBE_REPORT() || fault->count)
				platform_log("ME: fault: %u exceptions; Cause %08x (code %u), EPC %08x, BadVAddr %08x, sp %08x, ra %08x, Status %08x",
					fault->count, fault->cause, (fault->cause >> 2) & 31, fault->epc, fault->bad_address, fault->sp,
					fault->ra, fault->status);
			else
				platform_log("ME: fault: none recorded (%s): the ME was running, not faulted",
					psp_keep("mefaultoff") ? "the vector off, mefaultoff" : "the vector in");
			if (fault->pad[0])
				platform_log("ME: fault: mefaulttest %s", fault->pad[0] == 2 ? "reached its break" : "armed, not reached");
		}
	}
	else if (me_state == 2)
		platform_log("ME (simulated): stopped (%s)", why);
	if (me_state > 0)
		me_state = -1;
}

static void me_stash_dump(const char *why);
void psp_me_stop(const char *why)
{
	/* where the ME was when the main CPU gave up on it (mestash) */
	if (me_state == 1)
		me_stash_dump(why);
	me_stop(why, 0);
}

/* the power callback's, as the PSP goes to sleep: the wake may start it again */
void psp_me_sleep_stop(void)
{
	me_stop("sleep", 1);
}

static void stop_at_exit(void)
{
	psp_me_stop("exit");
}

static void sysregs_line(char *line, int size, const char *name, const struct me_sysregs *regs)
{
	snprintf(line, size, "%s %08x %08x %08x %08x (marks %08x %08x %08x %08x)", name, regs->reset, regs->bus_clock, regs->clock,
		regs->avc, regs->marks[0], regs->marks[1], regs->marks[2], regs->marks[3]);
}

/* "sndpart"'s claim (psp_me.h). sysmem's user call refuses partition 4
whatever the caller's mode (800200d6 on the PSP-1000, from user code and from
the ME library's kernel helper alike), and only its kernel export takes it.
So the claim is halo_claim.prx's (psp/claim), loaded beside the EBOOT only
for sndpart: it claims the whole megabyte at its start, and the game asks it
what it got and has it give the block back at the exit. Sony's ME image isn't
sysmem's (me_wrapper writes it without a block), so the claim reserves the
partition from its start */
#define ME_PARTITION_ADDRESS 0x08300000u
#define ME_CLAIM_BYTES (960u * 1024u) /* halo_claim.prx's CLAIM_BYTES */
int halo_claim_result(unsigned int *at); /* halo_claim.prx's system calls */
int halo_claim_release(void);
int halo_claim_probe(int *out);
extern char psp_eboot_path[256];
static int partition_claim = -1;

int psp_me_claim_partition(void)
{
	char path[256];
	char *slash;
	unsigned int at = 0;
	int status = 0, result;
	SceUID module;

	if (partition_claim >= 0)
		return partition_claim;
	if (psp_me_simulated())
		return -1;
	snprintf(path, sizeof(path), "%s", psp_eboot_path);
	slash = strrchr(path, '/');
	snprintf(slash ? slash + 1 : path, sizeof(path) - (slash ? (size_t)(slash + 1 - path) : 0), "halo_claim.prx");
	module = sceKernelLoadModule(path, 0, NULL);
	if (module >= 0)
		module = sceKernelStartModule(module, 0, NULL, &status, NULL);
	if (module < 0)
	{
		platform_log("ME: the partition's claim: %s didn't load (%08x)", path, (unsigned int)module);
		return -1;
	}
	result = halo_claim_result(&at);
	if (result < 0)
	{
		platform_log("ME: the partition's claim refused by sysmem (%08x), through halo_claim.prx", (unsigned int)result);
		return -1;
	}
	partition_claim = result;
	if ((at & 0x1FFFFFFFu) != ME_PARTITION_ADDRESS)
	{
		platform_log("ME: the partition's claim came back at %08x, not %08x: given back", at, ME_PARTITION_ADDRESS);
		halo_claim_release();
		partition_claim = -1;
		return -1;
	}
	platform_log("ME: the partition claimed from sysmem (its bottom %u KB at %08x, block %08x, by halo_claim.prx)",
		ME_CLAIM_BYTES / 1024, at, (unsigned int)partition_claim);
	/* given back and claimed again here, before the window holds anything:
	the release proven by its own result and the free sizes, not by the next
	launch (an exit reboots, and the reboot clears sysmem) */
	if (psp_keep("claimprobe"))
	{
		int r[6] = { -1, -1, -1, -1, -1, -1 };

		result = halo_claim_probe(r);
		platform_log("ME: claimprobe: partition 4's largest free block after the claim %d KB, before the probe %d KB; "
			"given back %08x, then %d KB free; claimed again %08x, then %d KB free",
			r[0] / 1024, r[1] / 1024, (unsigned int)r[2], r[3] / 1024, (unsigned int)r[4], r[5] / 1024);
		if (result < 0)
		{
			platform_log("ME: claimprobe: the claim lost (%08x)", (unsigned int)result);
			partition_claim = -1;
			return -1;
		}
		partition_claim = result;
		at = 0;
		halo_claim_result(&at);
		if ((at & 0x1FFFFFFFu) != ME_PARTITION_ADDRESS)
		{
			platform_log("ME: claimprobe: the claim came back at %08x, not %08x: given back", at, ME_PARTITION_ADDRESS);
			halo_claim_release();
			partition_claim = -1;
			return -1;
		}
	}
	return partition_claim;
}

/* HOME, or the game's own exit: the firmware reboots into the XMB and boots
its own ME image there, and that boot waits for the ME without a time limit.
So the ME is left as the firmware leaves it before a boot: halted, or held
in reset, never running our code. Nothing here waits on the ME for longer
than the stop's 100 ms. The registers go to the log, against launch's, for
a hang that comes after this */
void psp_me_exit(const char *why)
{
	char line[96];

	me_stop(why, 0);
	if (!me_kernel_ready)
		return;
	if (me_maybe_running)
	{
		kcall_2(me_hold_reset, me_kseg);
		me_maybe_running = 0;
		platform_log("ME: held in reset for the exit (a restart may have had it running)");
	}
	/* the reboot clears sysmem anyway; given back so a clean exit leaves no trace */
	if (partition_claim >= 0)
	{
		int freed = halo_claim_release();

		platform_log("ME: the partition given back to sysmem%s (%08x)", freed < 0 ? ": refused" : "", (unsigned int)freed);
		partition_claim = -1;
	}
	kcall_3(me_sysregs_kernel, me_kseg, &regs_exit);
	sysregs_line(line, sizeof(line), "exit", &regs_exit);
	platform_log("ME: system registers (reset, bus clocks, clocks, AVC power) at %s; %s %08x %08x %08x %08x", line,
		"launch", regs_launch.reset, regs_launch.bus_clock, regs_launch.clock, regs_launch.avc);
}

/* ---------- DMACplus between main RAM and the eDRAM (psp_me.h)

The registers and the control word are bench/halo_bench15's, which measured
them on the PSP-1000: mcidclan's dmacplus.h layout (psp-media-engine-custom-
core), the controller's own control word (the terminal-count bit, bits 27 and
26 clear for a transfer with the ME's side in it, the masters in 25 and 24,
the width code twice, both bursts) and single descriptors only, since a chain
hung the PSP. The ME's side address is the eDRAM's offset. The channel runs
with its interrupts masked, so the kernel's own handler never sees ours, and
the raw status bits it sets (2 for main RAM to the ME, 3 back, as the bench
saw them) are cleared before each start */
#define DMACP 0xBC800000u
#define DMACP_INT_TC_CLEAR (DMACP + 0x08)
#define DMACP_INT_ERR_CLEAR (DMACP + 0x10)
#define DMACP_RAW_ERR (DMACP + 0x18)
#define DMACP_ENABLED (DMACP + 0x1C)
#define DMACP_CONFIG (DMACP + 0x30)
#define DMACP_SC2ME 0x180u
#define DMACP_ME2SC 0x1A0u
#define DMACP_STATUS_BIT(channel) ((channel) == DMACP_SC2ME ? 4u : 8u)
#define DMACP_WIDTH16 4u
#define DMACP_MAX_COUNT 4095u

struct dmacp_call
{
	unsigned int channel, src, dst, control;
	int result;
};

static int dmacp_start_kernel(void *param)
{
	struct dmacp_call *call = (struct dmacp_call *)param;
	unsigned int base = DMACP + call->channel, bit = DMACP_STATUS_BIT(call->channel);
	int interrupts;

	/* the controller's enable bit reads 0 while the display's channel runs:
	a channel running is the proof it is on */
	if (!(hw(DMACP_CONFIG) & 1) && !hw(DMACP_ENABLED))
		return call->result = -2;
	if (hw(base + 0x10) & 1)
		return call->result = -1;
	if (!psp_dma_address_ok(call->src) || !psp_dma_address_ok(call->dst))
		return call->result = -5; /* a kernel segment: refused before any register is written */
	hw(DMACP_INT_TC_CLEAR) = bit;
	hw(DMACP_INT_ERR_CLEAR) = bit;
	interrupts = sceKernelCpuSuspendIntr();
	hw(base + 0x00) = call->src;
	hw(base + 0x04) = call->dst;
	hw(base + 0x08) = 0;
	hw(base + 0x0C) = call->control;
	hw(base + 0x10) = 1;
	__asm__ __volatile__("sync");
	sceKernelCpuResumeIntrWithSync(interrupts);
	return call->result = 0;
}

/* 1 once the channel has stopped: result -4 if it reported an error */
static int dmacp_poll_kernel(void *param)
{
	struct dmacp_call *call = (struct dmacp_call *)param;
	unsigned int bit = DMACP_STATUS_BIT(call->channel);

	if (hw(DMACP + call->channel + 0x10) & 1)
		return 0;
	call->result = (hw(DMACP_RAW_ERR) & bit) ? -4 : 0;
	hw(DMACP_INT_TC_CLEAR) = bit;
	hw(DMACP_INT_ERR_CLEAR) = bit;
	return 1;
}

static int dmacp_stop_kernel(void *param)
{
	struct dmacp_call *call = (struct dmacp_call *)param;
	unsigned int bit = DMACP_STATUS_BIT(call->channel);

	hw(DMACP + call->channel + 0x10) = 0;
	__asm__ __volatile__("sync");
	hw(DMACP_INT_TC_CLEAR) = bit;
	hw(DMACP_INT_ERR_CLEAR) = bit;
	return 0;
}

int psp_me_dma_edram(int into_edram, void *main_ram, unsigned int edram_offset, unsigned int bytes,
	unsigned int timeout_us, unsigned int *us)
{
	struct dmacp_call call;
	unsigned int ram = (unsigned int)main_ram & 0x1FFFFFFFu, count = bytes / 16, start;

	if (us)
		*us = 0;
	if (me_state != 1 || !count || bytes % 16 || count > DMACP_MAX_COUNT || (ram & 15) || (edram_offset & 15) ||
		edram_offset + bytes > ME_EDRAM_STACK_TOP)
		return -5;
	call.channel = into_edram ? DMACP_SC2ME : DMACP_ME2SC;
	call.src = into_edram ? ram : edram_offset;
	call.dst = into_edram ? edram_offset : ram;
	/* the masters: 1 the ME's side, at the destination going in, at the source coming out */
	call.control = (1u << 31) | ((into_edram ? 1u : 0u) << 25) | ((into_edram ? 0u : 1u) << 24) |
		(DMACP_WIDTH16 << 21) | (DMACP_WIDTH16 << 18) | (1u << 15) | (1u << 12) | count;
	call.result = 0;
	kcall_3(dmacp_start_kernel, me_kseg, &call);
	if (call.result)
		return call.result;
	start = sceKernelGetSystemTimeLow();
	while (!kcall_3(dmacp_poll_kernel, me_kseg, &call))
		if (sceKernelGetSystemTimeLow() - start > timeout_us)
		{
			kcall_3(dmacp_stop_kernel, me_kseg, &call);
			if (us)
				*us = sceKernelGetSystemTimeLow() - start;
			return -3;
		}
	if (us)
		*us = sceKernelGetSystemTimeLow() - start;
	return call.result;
}

/* launch (build 87: on unless the launch menu's "nomeaudio"): the ME started,
its first signs of life checked, its clock measured. Non-zero when jobs can
be handed over */
/* the main CPU's coprocessor 0 CPU number, read in kernel mode (me-core's
meLibGetCpuId reads it so, in its kernel functions) */
static int me_cpu_number_kernel(void *out)
{
	unsigned int number;

	__asm__ __volatile__("sync\n\tmfc0 %0, $22\n\tsync" : "=r"(number));
	*(unsigned int *)out = number;
	return 0;
}

/* the two CPU numbers at the ME's start (and its restarts): the ME's as it
reported it, the main CPU's read in kernel mode. A check of where the ME's
code runs, not a test any route uses (psp_me.h's psp_cpu_is_me) */
static void me_cpu_check(volatile struct me_control *control, const char *when)
{
	static unsigned int main_number = 0xffffffffu;

	if (main_number == 0xffffffffu)
		kcall_3(me_cpu_number_kernel, me_kseg, &main_number);
	platform_log("ME: CPU numbers (coprocessor 0 register 22) at the %s: the ME %s%u, the main CPU %u",
		when, control->cpu_seen ? "" : "(nothing reported) ", control->cpu_seen ? control->cpu_number : 0xffffffffu,
		main_number);
}

/* "mexec" (docs/mexec.md): the copy wanted, its room free, its table filled */
static int me_mexec_wanted(void)
{
#if PSP_MEXEC
	static const char *const sharers[] = { "soundedram", "edramcheck", "mebusedram", "vmetest" };
	unsigned int k;

	if (!psp_keep("mexec"))
		return 0;
	for (k = 0; k < sizeof(sharers) / sizeof(sharers[0]); k++)
		if (psp_keep(sharers[k]))
		{
			platform_log("ME: mexec refused: %s shares eDRAM %u-%u KB, the copy's room", sharers[k],
				MEXEC_AT / 1024, (MEXEC_AT + MEXEC_ROOM) / 1024);
			return 0;
		}
	if (psp_mexec_table[0] != MEXEC_MAGIC)
	{
		platform_log("ME: mexec refused: the build's table is empty (tools/mexec.py didn't run)");
		return 0;
	}
	/* the list's count beside the copy's (word 12, tools/mexec.py), so an
	@fill cut shows in the launch line: printed, not tested, since a test
	would add a branch the profile hasn't counted */
	platform_log("ME: mexec: %u of the list's %u functions, %u bytes into eDRAM at %u KB of a %u KB room (%u calls, %u branches, %u jump-table addresses re-aimed; %u jump tables copied, %u words left in main RAM)",
		psp_mexec_table[4], psp_mexec_table[12], psp_mexec_table[3], MEXEC_AT / 1024, MEXEC_ROOM / 1024, psp_mexec_table[5], psp_mexec_table[7], psp_mexec_table[8],
		psp_mexec_table[10], psp_mexec_table[9]);
	return 1;
#else
	return 0;
#endif
}

int psp_me_start(void)
{
	volatile struct me_control *control = CONTROL;
	unsigned int before, clock_before;
	unsigned long long mark;
	int table, k;

	/* nomeaudio keeps the ME off unless a ring client asks for it; the
	mixer then stays on the main CPU (psp_dsound.c) */
	if (me_state || (psp_keep("nomeaudio") && !psp_keep("meslant") && !psp_keep("meringtest") && !psp_keep("meraysperc") &&
		psp_keep("nomepoints")))
		return me_state > 0;
	/* no cached copy of them anywhere on this CPU; then the words that
	matter (they are in zeroed memory already) */
	sceKernelDcacheWritebackInvalidateRange(&control_storage, sizeof(control_storage));
	sceKernelDcacheWritebackInvalidateRange(&psp_me_fault_storage, sizeof(psp_me_fault_storage));
	memset((void *)UNCACHED(&psp_me_fault_storage), 0, sizeof(psp_me_fault_storage));
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
	sceKernelDcacheWritebackInvalidateRange(stash_storage, sizeof(stash_storage));
	for (k = 0; k < STASH_WORDS; k++)
		STASH[k] = 0;
	psp_me_inner_reset();
	ring_on = psp_me_slant_wanted() || psp_me_rays_wanted() || psp_keep("meringtest") || psp_keep("mebatchtest") ||
		!psp_keep("nomepoints");
#ifdef PSP_RECON
	ring_on = ring_on || psp_keep_number("mebus", -1) >= 0;
#endif
	ring_push_drop = psp_keep("mepushdrop");
	rays_batch = psp_keep("meraysbatch");
	me_yield_off = psp_keep("meyieldoff");
	me_grace_off = psp_keep("megraceoff");
	me_claim_old = psp_keep("meclaimold");
	ring_give_up = psp_keep("ringgiveup");
	ring_slot_done = psp_keep("ringslotdone");
	{
		int rest = psp_keep_number("ringrest", -1);

		if (rest >= 0)
			ring_rest_base_us = ring_rest_us = (unsigned long long)rest * 1000ull;
	}
#ifdef PSP_RECON
	{
		int n = psp_keep_number("ringlate", 0);

		ring_late_every = n > 0 ? (unsigned int)n : 0u;
		ring_hang_test = ring_late_every && psp_keep("ringhang");
		if (ring_late_every)
			platform_log("ME: ringlate: every ring job whose ticket is a multiple of %u taken as late%s",
				ring_late_every, ring_hang_test ? "; ringhang: the ME never catches up" : "");
	}
#endif
	audio_look_start();
	report_mark = sceKernelGetSystemTimeWide();
	if (emulator())
	{
		/* PPSSPP's device names the span it maps (tools/ppsspp_local_patches.diff);
		without the patch it writes nothing and the span stays empty */
		unsigned int span[2] = { 0, 0 };

		if (sceIoDevctl("kemulator:", 0x4D45, NULL, 0, span, sizeof(span)) == 0 && span[0] < span[1])
		{
			sim_edram_lo = span[0];
			sim_edram_hi = span[1];
		}
		me_state = 2;
		me_restartable = 2;
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

		me_reset_whole = psp_keep("meresetwhole");
		if (psp_keep("kcallseg0"))
			me_kseg = 0;
		platform_log("ME: starting the Media Engine (a hang here: hold the power switch up 10 s; launch with nomeaudio)");
		psp_log_flush();
		chdir(PSP_DATA_ROOT);
		table = me_lib_init();
		/* the Slim and 3000's image (table 2) starts the ME's stack at 4 MB, the
		1000's at 2 MB (me-core.h): the AI job's room and its measure sit below
		the real top, so its guard and its depth mean the same on both */
		if (table == 2)
			me_stack_top = 0x400000u;
		if (have)
			chdir(folder);
	}
	/* the library's kernel helper is in from here; the ME's reset was
	released whether or not the start below sees it running */
	me_kernel_ready = table >= 0 || table < -4;
	me_maybe_running = table >= 0;
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
		kcall_2(me_hold_reset, me_kseg);
		me_maybe_running = 0;
		platform_log("ME: the ME didn't start (alive %u -> %u in 20 ms; table %d); held in reset", before, control->alive, table);
		return 0;
	}
	{
		unsigned int cycles = control->clock - clock_before;
		unsigned long long us = sceKernelGetSystemTimeWide() - mark;

		me_cycles_per_us = us ? (float)cycles / (float)us : 0.0f;
	}
	me_state = 1;
	me_restartable = 1;
	atexit(stop_at_exit);
	/* mehalt only now, so the start check above saw the ME polling */
	me_halting = psp_keep("mehalt");
	control->features = (me_halting ? ME_FEATURE_HALT : 0u) | (ring_on ? ME_FEATURE_RING : 0u) |
		(psp_keep("meaudiofirst") ? ME_FEATURE_AUDIO_FIRST : 0u) |
		(psp_keep("meaudioranges") ? ME_FEATURE_AUDIO_RANGES : 0u) | (psp_keep("mestash") ? ME_FEATURE_STASH : 0u) |
		(me_yield_off ? ME_FEATURE_NO_YIELD : 0u) | (psp_keep("meedramstate") ? ME_FEATURE_EDRAM_STATE : 0u) |
		(me_claim_old ? ME_FEATURE_CLAIM_OLD : 0u) | (psp_keep("mebcc1") ? ME_FEATURE_BCC : 0u) |
		(psp_keep("mefaultoff") ? ME_FEATURE_FAULT_OFF : 0u) | (psp_keep("mefaulttest") ? ME_FEATURE_FAULT_TEST : 0u) |
		(psp_keep("menestrender") ? ME_FEATURE_NEST_RENDER : 0u) |
		(psp_keep("aimevisleft") ? ME_FEATURE_AIME_VIS_LEFT : 0u) |
		(psp_keep("aimeaudiolook") ? ME_FEATURE_AUDIO_LOOK : 0u) | (psp_keep("audiohold") ? ME_FEATURE_AUDIO_HOLD : 0u) | (me_mexec_wanted() ? ME_FEATURE_MEXEC : 0u) |
		(psp_keep("ticksched") && ring_on ? ME_FEATURE_TICKSCHED : 0u);
	if ((control->features & ME_FEATURE_AUDIO_HOLD) && audio_hold_flag < 0)
		audio_hold_flag = sceKernelCreateEventFlag("audiohold", 0, 0, NULL);
	tick_slice_set(control);
	wake_features = control->features | (psp_keep("mewakeprobe") ? ME_FEATURE_WAKE_PROBE : 0u);
	wake_early = psp_keep("mewake") && !psp_keep("mewakeoff") && psp_keep("mewakeearly");
	wake_fw = psp_keep("mewakefw");
	platform_log("ME: the ME runs (firmware table %d; %u idle polls in 20 ms; its cycle counter runs at %.1f a microsecond%s; Cause %08x, after clearing DC %08x)",
		table, control->alive - before, me_cycles_per_us, me_cycles_per_us < 1.0f ? ": ME times will read 0" : "",
		control->cause_before, control->cause_after);
	me_cpu_check(control, "launch");
	kcall_3(psp_keep("memarksatlaunch") ? me_sysregs_kernel : me_sysregs_running_kernel, me_kseg, &regs_launch);
	{
		char line[96];

		sysregs_line(line, sizeof(line), "launch", &regs_launch);
		platform_log("ME: system registers (reset, bus clocks, clocks, AVC power) at %s%s", line,
			me_reset_whole ? "; meresetwhole: the reset register written whole" : "");
	}
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

/* how many of the ME's jobs the main CPU has asked for and not yet had
back: the slots' and the ring's, the sound's mixing apart (it reads the
voices only). For a main-CPU check that nothing on the ME can be reading
what is about to change */
int psp_me_jobs_out(void)
{
	int k, out = 0;

	if (me_state <= 0)
		return 0;
	for (k = 0; k < ME_SLOTS; k++)
	{
		if (k == ME_SLOT_AUDIO)
			continue;
#ifdef ME_SLOT_MUSIC
		if (k == ME_SLOT_MUSIC)
			continue;
#endif
		if (SLOT(k)->request != SLOT(k)->done)
			out++;
	}
	if (RING_PUSHED != RING_POPPED)
		out++;
	return out;
}

int psp_meaudio_running(void)
{
	return me_state > 0;
}

static void me_stash_dump(const char *why);

/* the simulated jobs' mixing sums (the emulator; one job at a time there) */
static int simulated_sums[MIX_FRAMES_MAX * 2] __attribute__((aligned(64)));

/* a job in a slot, run to the end: on the ME (the caller sleeps meanwhile),
or on this CPU when simulated. args as the job type takes them; results
filled. Returns 1 when done, 0 if the ME failed (it is stopped then; the
caller runs the job its own way). first_look_us: how long the job is
expected to take, the first sleep before looking (then every 0.1 ms) */
static int me_run_slot(int which, unsigned int type, const unsigned int *args, int arg_count, unsigned int *results,
	unsigned int first_look_us)
{
	volatile struct me_slot *slot;
	unsigned long long start;
	unsigned long waited, me_us;
	unsigned int looks = 0;
	int k;

	if (me_state <= 0 || arg_count > ME_JOB_ARGS)
		return 0;
	slot = SLOT(which);
	last_looks[which] = 0;
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
			/* build 115: the eDRAM probe too, which may wait behind a render job, and the VME probe on its slot */
			if (sceKernelGetSystemTimeWide() - start > (type == ME_JOB_GE || type == ME_JOB_EDRAM || type == ME_JOB_VME ? 500000 :
				type == ME_JOB_AUDIO && (CONTROL->features & ME_FEATURE_AUDIO_HOLD) ? AUDIO_HOLD_WAIT_US : 40000))
			{
				platform_log("ME: a job (type %u) not finished in %u ms (job %u; the ME at job %u, %u idle polls)", type,
					type == ME_JOB_GE || type == ME_JOB_EDRAM || type == ME_JOB_VME ? 500 :
					type == ME_JOB_AUDIO && (CONTROL->features & ME_FEATURE_AUDIO_HOLD) ? AUDIO_HOLD_WAIT_US / 1000 : 40,
					slot_request[which], slot->done, CONTROL->alive);
				me_stash_dump("a slot job late");
				stats[which].failures++;
				psp_me_stop("a job timed out");
				return 0;
			}
			/* under audiohold an audio block waits for the replay's end, up
			to a block (its slack). A poll there wakes the audio thread out of
			the game thread's time (every 100 us, b30's tick +2.5 ms, sierra-hw
			44871030232; every 1 ms, +0.5 ms, 44874847574), so it sleeps on
			the render's end, looking every 4 ms in case; 1 ms polls only if
			the flag couldn't be made */
			if (type == ME_JOB_AUDIO && (CONTROL->features & ME_FEATURE_AUDIO_HOLD))
			{
				SceUInt timeout = AUDIO_HOLD_LOOK_US;

				if (audio_hold_flag < 0 ||
					sceKernelWaitEventFlag(audio_hold_flag, 1, PSP_EVENT_WAITOR | PSP_EVENT_WAITCLEAR, NULL, &timeout) < 0 &&
					timeout != 0)
					sceKernelDelayThread(1000);
			}
			else
				sceKernelDelayThread(100);
			looks++;
		}
		waited = (unsigned long)(sceKernelGetSystemTimeWide() - start);
		last_looks[which] = looks;
		stats[which].looks += looks;
		stats[which].first_look_us += first_look_us ? first_look_us : 100;
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
	/* "lanetrace" (psp_lanetrace.c): handed over, seen done, the ME's own time */
	PSP_LANE(PSP_LANE_ME_SLOT, (unsigned int)start, sceKernelGetSystemTimeLow(), (unsigned int)which << 8 | (type & 0xFF),
		last_job_us[which]);
	return 1;
}

int psp_me_run(int which, unsigned int type, const unsigned int *args, int arg_count, unsigned int *results,
	unsigned int first_look_us)
{
	int done;

	if (which < 0 || which >= ME_SLOTS)
		return 0;
	slot_busy[which] = 1;
	done = me_run_slot(which, type, args, arg_count, results, first_look_us);
	slot_busy[which] = 0;
	return done;
}

/* build 118: a job handed over without waiting (L1 step 4, psp_ge.c's
"gemeoverlap"): the main CPU goes on with its own work and finishes the job
later with psp_me_finish. One job a slot at a time. Returns 1 if handed over
(or, simulated, already run here: the emulator runs no ME code), 0 if the ME
can't take it (off, stopped, a job already out): the caller runs it itself.
The caller writes back its data cache first, as for psp_me_run */
static int submitted[ME_SLOTS]; /* 0 none, 1 out with the ME, 2 run here (simulated) */
/* B69 (the lab's bugs.md): the same marks kept again in whole lines of their
own, read and written only through the uncached view. Twice a halt followed
a wait that never saw a job done that the stop's report found finished, and
only on a build where submitted[] fell on one cache set: the main CPU's own
mark read 0 while the job was out [U]. So the marks the waits read come from
the uncached copy, and a disagreement with the cached one is logged */
static int submitted_storage[(ME_SLOTS + 15) & ~15] __attribute__((aligned(64)));
#define SUBMITTED_UC ((volatile int *)(0x40000000u | (unsigned int)submitted_storage))
static unsigned int mark_disagreements;

static void me_mark_set(int which, int state)
{
	submitted[which] = state;
	SUBMITTED_UC[which] = state;
}

static int me_mark(int which)
{
	int cached = submitted[which], uncached = SUBMITTED_UC[which];

	if (cached != uncached)
	{
		if (mark_disagreements++ < 8 && !PSP_ME_WORK())
			platform_log("ME: slot %d's mark disagrees: cached %d at %p, uncached %d (the uncached one used; %u so far)",
				which, cached, (void *)&submitted[which], uncached, mark_disagreements);
		submitted[which] = uncached;
	}
	return uncached;
}
static unsigned long long submit_mark[ME_SLOTS];
/* the slot job out serves the ring between its steps (the GE layer's
replay yields every 32 records): pushes go on while it runs */
static int slot_yields[ME_SLOTS];

void psp_me_touch(int which)
{
	if (which >= 0 && which < ME_SLOTS && me_mark(which) == 1)
		submit_mark[which] = sceKernelGetSystemTimeWide();
}

int psp_me_submit(int which, unsigned int type, const unsigned int *args, int arg_count)
{
	volatile struct me_slot *slot;
	int k;

	if (which < 0 || which >= ME_SLOTS || arg_count > ME_JOB_ARGS || me_mark(which))
		return 0;
	/* down again at psp_me_finish's end */
	slot_busy[which] = 1;
	if (me_state <= 0)
	{
		slot_busy[which] = 0;
		return 0;
	}
	slot = SLOT(which);
	slot->type = type;
	for (k = 0; k < arg_count; k++)
		slot->args[k] = args[k];
	slot_yields[which] = (type == ME_JOB_GE || type == ME_JOB_AIME
#if PSP_MUSIC
		|| type == ME_JOB_MUSIC
#endif
		) && !me_yield_off;
	if (arg_count < ME_JOB_ARGS)
		slot->args[ME_JOB_ARGS - 1] = 0;
	submit_mark[which] = sceKernelGetSystemTimeWide();
	if (me_state == 2)
	{
		run_job(slot, simulated_sums, 0);
		last_job_us[which] = (unsigned int)(sceKernelGetSystemTimeWide() - submit_mark[which]);
		stats[which].simulated++;
		me_mark_set(which, 2);
		return 1;
	}
	__asm__ __volatile__("sync");
	slot->request = ++slot_request[which];
	__asm__ __volatile__("sync");
	me_wake();
	me_mark_set(which, 1);
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

static int me_finish_slot(int which, unsigned int *results)
{
	static int slow = -1;
	volatile struct me_slot *slot;
	unsigned long long start, now;
	unsigned long waited, looks = 0;
	int k, state;

	if (slow < 0)
		slow = psp_keep("mefinishslow");
	state = me_mark(which);
	me_mark_set(which, 0);
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
					me_stash_dump("a handed-over job late");
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
	/* audiohold: the replay is over, so the ME takes the held audio block
	next; the audio thread looks now rather than at its 4 ms timeout */
	if (which == ME_SLOT_RENDER && audio_hold_flag >= 0)
		sceKernelSetEventFlag(audio_hold_flag, 1);
	stats[which].jobs++;
	stats[which].wait_us += waited;
	if (waited > stats[which].wait_worst_us)
		stats[which].wait_worst_us = waited;
	/* "lanetrace" (psp_lanetrace.c): handed over, seen done, the ME's own time */
	PSP_LANE(PSP_LANE_ME_SLOT, (unsigned int)submit_mark[which], sceKernelGetSystemTimeLow(),
		(unsigned int)which << 8 | (slot->type & 0xFF), last_job_us[which]);
	return 1;
}

/* a job handed over with psp_me_submit is done (or was run here), so
psp_me_finish won't wait; 0 while the ME is still on it, or none is out */
/* audiohold without its two extra blocks (psp_dsound.c couldn't get them,
or has no channel): the ME mustn't hold a block for the replay's end, as
nothing covers the wait, so the feature leaves the control word and the
restart's copy of it. Safe however late it comes, though the probes may
start the ME earlier: only the audio thread hands the ME audio jobs, and it
calls this before its first */
void psp_me_audio_hold_off(void)
{
	wake_features &= ~ME_FEATURE_AUDIO_HOLD;
	CONTROL->features &= ~ME_FEATURE_AUDIO_HOLD;
}

int psp_me_submitted_done(int which)
{
	int state;

	if (which < 0 || which >= ME_SLOTS || !(state = me_mark(which)))
		return 0;
	if (state == 2 || SLOT(which)->done == slot_request[which])
	{
		/* audiohold: the waiting audio block is mixed now the replay is done */
		if (which == ME_SLOT_RENDER && audio_hold_flag >= 0)
			sceKernelSetEventFlag(audio_hold_flag, 1);
		return 1;
	}
	return 0;
}

/* the ME has started the job handed over with psp_me_submit (or it was run
here): the time since is the job's own, not its wait behind the ring's */
int psp_me_submitted_begun(int which)
{
	int state;

	if (which < 0 || which >= ME_SLOTS || !(state = me_mark(which)))
		return 0;
	return state == 2 || SLOT(which)->begun == slot_request[which] ||
		SLOT(which)->done == slot_request[which];
}

int psp_me_finish(int which, unsigned int *results)
{
	int done;

	if (which < 0 || which >= ME_SLOTS || !me_mark(which))
		return 0;
	done = me_finish_slot(which, results);
	slot_busy[which] = 0;
	return done;
}

/* ---------- the job ring, the main CPU's side */

/* a job that isn't done this long after its push is reported, and the main
CPU does its work instead (psp_me_wait returns 0). The ring then rests: the
ME may still finish the job late and write its output, which nobody reads,
so nothing more is pushed until the ME has popped every job pushed before
(then none of its writes is still to come), and not before the rest's end
(RING_REST_US; "ringrestN": N ms). It comes back at a frame end
(ring_revive), where the ring's one producer is between frames. An ME that
never catches up (hung) keeps it off. One late again within
RING_AGAIN_US of coming back rests twice as long each time, up to
RING_REST_MAX_US, so an ME that is late again and again costs the main CPU
at most one deadline's wait a rest. "ringgiveup": the first late job turns
the ring off for the session, as before. A slant job is ~0.1-2 ms of
work (~1 us a triangle, b90) and may wait behind one audio block (~0.6 ms);
a ray job can wait behind a whole render job's stretch between yields */
#define RING_DEADLINE_US 10000
/* between two looks at popped while waiting: ~1 us, so the look costs the
bus little while the ME reads its inputs (code_review_b119_me.md, 1) */
#define RING_WAIT_NOPS 250
#define RING_WAIT_CLOCK_EVERY 16

extern volatile unsigned int psp_me_window[16];
extern volatile unsigned int psp_me_window_split;
static unsigned int ring_next = 0;        /* the next ticket */
static unsigned int ring_popped_seen = 0; /* popped, as last read */
static int ring_dead = 0;                 /* a job failed: 1 the ring takes no more, 2 it rests (RING_REST_US) */
/* the first ticket after the wake's restart; tickets before it went to an ME
that was reset, and their entries hold no answer for them */
static unsigned int ring_epoch = 0;
static int ring_restarted = 0;
static unsigned int ring_push_mark[ME_RING_ENTRIES]; /* the push's time, a ticket's */
/* the emulator: a ticket's job's own time, run inside the push (the polled
jobs' count reads it, psp_me_ring_collected) */
static unsigned int ring_sim_us[ME_RING_ENTRIES];
static unsigned int ring_thread = 0;      /* simulated: the producer's thread, for psp_me_context */
/* simulated: a ring job pushed and not yet waited for (the trace's window) */
static int ring_sim_open = 0;

int psp_me_ring_sim_open(void)
{
	return ring_sim_open;
}
static struct
{
	unsigned long jobs, at_once, failures, refused_busy, refused_full, refused_check;
	unsigned long long job_cycles, all_cycles, look_cycles, push_us, wait_us, round_us;
	unsigned long long simulated_us; /* the emulator: the jobs' own time, run inside the push */
	unsigned long look_worst_cycles, wait_worst_us;
	/* by job type: how many, and the longest one's own code (a ring job
	run inside the AI job's yield holds off its look at the word) */
	unsigned long type_jobs[16], type_worst_cycles[16];
} ring_stats;
#if PSP_RECON
/* "mewaitwho": ring jobs' waits by (the occupant at the wait's start, the one at the pick-up), and by the
waiting job's type; a report each ring window */
#define ME_WAITWHO_PAIRS 24
static struct { unsigned int key; unsigned long jobs; unsigned long long cycles; unsigned int worst; } me_waitwho[ME_WAITWHO_PAIRS];
static unsigned long long me_waitwho_type[16];
static unsigned long me_waitwho_type_jobs[16], me_waitwho_other;
static int me_waitwho_on = -1;
static void psp_me_waitwho_add(unsigned int who, unsigned int type, unsigned int cycles)
{
	int k;

	if (me_waitwho_on < 0)
		me_waitwho_on = psp_keep("mewaitwho");
	if (!me_waitwho_on)
		return;
	me_waitwho_type[type & 15] += cycles;
	me_waitwho_type_jobs[type & 15]++;
	for (k = 0; k < ME_WAITWHO_PAIRS; k++)
		if (me_waitwho[k].jobs == 0 || me_waitwho[k].key == who)
		{
			me_waitwho[k].key = who;
			me_waitwho[k].jobs++;
			me_waitwho[k].cycles += cycles;
			if (cycles > me_waitwho[k].worst)
				me_waitwho[k].worst = cycles;
			return;
		}
	me_waitwho_other++;
}
static void psp_me_waitwho_report(float rate)
{
	char line[512];
	int k, at;

	if (me_waitwho_on <= 0)
		return;
	at = snprintf(line, sizeof(line), "ME: waitwho: the ring's waits by (occupant at the wait's start > at the pick-up), jobs, us a job, worst us:");
	for (k = 0; k < ME_WAITWHO_PAIRS && me_waitwho[k].jobs && at < (int)sizeof(line) - 48; k++)
		at += snprintf(line + at, sizeof(line) - at, " %u>%u %lu %lu %lu;", me_waitwho[k].key >> 8, me_waitwho[k].key & 0xFF, me_waitwho[k].jobs,
			(unsigned long)((float)me_waitwho[k].cycles / rate / (float)me_waitwho[k].jobs), (unsigned long)((float)me_waitwho[k].worst / rate));
	if (me_waitwho_other)
		snprintf(line + at, sizeof(line) - at, " more pairs %lu jobs", me_waitwho_other);
	platform_log("%s", line);
	at = snprintf(line, sizeof(line), "ME: waitwho: by the waiting job's type, jobs and us a job:");
	for (k = 0; k < 16 && at < (int)sizeof(line) - 32; k++)
		if (me_waitwho_type_jobs[k])
			at += snprintf(line + at, sizeof(line) - at, " %d: %lu %lu;", k, me_waitwho_type_jobs[k],
				(unsigned long)((float)me_waitwho_type[k] / rate / (float)me_waitwho_type_jobs[k]));
	platform_log("%s", line);
	memset(me_waitwho, 0, sizeof(me_waitwho));
	memset(me_waitwho_type, 0, sizeof(me_waitwho_type));
	memset(me_waitwho_type_jobs, 0, sizeof(me_waitwho_type_jobs));
	me_waitwho_other = 0;
}
#endif

int psp_me_ring_ready(void)
{
	return ring_on && !ring_dead && me_state > 0;
}

/* what the main CPU wrote that a job will read, out of its cache to main
RAM: the lines of the range, or past 16 KB the whole cache */
void psp_me_writeback(const void *start, unsigned int bytes)
{
	if (bytes > RING_WHOLE_CACHE_BYTES || bytes == ME_RING_ALL_OF_RAM)
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

/* the emulator's stand-in for the ME's own stack (in its eDRAM on the PSP):
a job's locals written through pointers (a ray's leaf list, say) would
otherwise land below the producer's stack pointer, on lines the producer
writes again as soon as the push returns, and the write trace would count
them as written by both CPUs. psp_me_sim_call(index, top, fn) runs fn(index)
with its stack pointer just below top (the words it keeps above the
callee's argument area) */
#define RING_SIM_STACK_BYTES (48 * 1024)
void psp_me_sim_call(unsigned int index, void *top, void (*fn)(unsigned int));
__asm__(
	".text\n"
	".globl psp_me_sim_call\n"
	".ent psp_me_sim_call\n"
	"psp_me_sim_call:\n"
	".set push\n"
	".set noreorder\n"
	"	addiu $a1, $a1, -32\n"
	"	sw $ra, 16($a1)\n"
	"	sw $sp, 20($a1)\n"
	"	jalr $a2\n"
	"	move $sp, $a1\n"
	"	lw $ra, 16($sp)\n"
	"	lw $sp, 20($sp)\n"
	"	jr $ra\n"
	"	nop\n"
	".set pop\n"
	".end psp_me_sim_call\n");

static void ring_run_simulated(unsigned int index)
{
	ring_run(index, 0, 0);
}

/* "aime2a" in the emulator: the pass here and now, marked as the ME's (the
ME-strict check, the write trace, the code's PSP_ME_WORK paths), on a stack
of its own. The thinking keeps 82 KB path searches on its stack and reaches
past 192 KB, more than the ME's eDRAM stack has, so the stack's deepest reach
is measured (a fill pattern) and reported: it sizes the job's stack */
#define AIME_SIM_STACK_BYTES (ME_EDRAM_STACK_TOP - ME_EDRAM_AIME_STACK)
#define AIME_SIM_FILL 0x5AA5C33Cu
/* below the room in the emulator: what a deeper reach lands on, so it's
measured instead of written over the heap's next block (the PSP's room stops
at its guard, which the job reads between actors) */
static unsigned int aime_sim_slack_bytes = 64 * 1024;
#define AIME_SIM_SLACK_BYTES aime_sim_slack_bytes
/* the guard: words at the room's bottom, written before each job and read by
the job between actors (psp_me_aime_guard_touched). A frame that reaches them
has used the room; the job hands its actor back and ends, and the collection
logs it. The fill's scan starts above them */
#define AIME_GUARD_WORD 0xA1E5D00Du
static PSP_AIME_ME_SIDE volatile unsigned int *aime_guard = NULL;
extern unsigned int psp_aime_sp_low, psp_aime_sp_high;

static void aime_guard_set(
	volatile unsigned int *guard)
{
	unsigned int k;

	for (k = 0; k < ME_EDRAM_AIME_GUARD_BYTES / 4; k++)
		guard[k] = AIME_GUARD_WORD;
	aime_guard = guard;
}

/* the AI pass's context (actors.c; psp_post.h's psp_aime_here) */
extern unsigned int psp_aime_sp_low, psp_aime_sp_high;

/* a pass on the main CPU: its thread's stack is the pass's context
(psp_aime_here), found once a thread */
void psp_aime_pass_here(int on)
{
	static SceUID known = -1;
	static unsigned int low = 0, high = 0;
	SceUID thread;

	if (!on)
	{
		psp_aime_sp_low = psp_aime_sp_high = 0;
		return;
	}
	thread = sceKernelGetThreadId();
	if (thread != known)
	{
		SceKernelThreadInfo info;

		memset(&info, 0, sizeof(info));
		info.size = sizeof(info);
		low = high = 0;
		if (sceKernelReferThreadStatus(thread, &info) == 0 && info.stack)
		{
			low = (unsigned int)info.stack;
			high = low + (unsigned int)info.stackSize;
		}
		known = thread;
	}
	psp_aime_sp_low = low;
	psp_aime_sp_high = high;
}

unsigned int psp_me_aime_room_bytes(void)
{
	return ME_EDRAM_STACK_TOP - ME_EDRAM_AIME_STACK;
}

int psp_me_aime_guard_touched(void)
{
	unsigned int k;

	if (!aime_guard)
		return 0;
	for (k = 0; k < ME_EDRAM_AIME_GUARD_BYTES / 4; k++)
	{
		if (aime_guard[k] != AIME_GUARD_WORD)
			return 1;
	}
	return 0;
}

#ifdef PSP_RECON
/* where a long slot job is, for the stash's report of a late ring job */
void psp_me_stash_place(unsigned int place)
{
	if (!psp_on_me() || !(CONTROL->features & ME_FEATURE_STASH))
		return;
	STASH[STASH_PLACE] = place;
	STASH[STASH_PLACE_CLOCK] = me_cycles();
}
#endif

/* "aimeringlook" (B50): the AI job yields only between actors, and one
actor's thinking runs up to ~19 ms on the ME (R3's census), so a ring job
pushed meanwhile (the frame's points, pushed while the job runs) missed its
10 ms deadline and the ring rested for a second. The job's looks at the
collection's word inside an actor, and the head of each prop perception
tests (psp_fixups.py psp_aime_ring_yield), sit where its work holds nothing
shared, so the ring is served there too: at most once every
INNER_GAP_CYCLES, so a path search's looks (one a node) cost one clock read
each, and only with room on the stack for the deepest ring job (a points or
rays job's allowance) above the guard. The ring alone, not audio: an audio
block there (~5-7 ms on d20) held the word's answer past the collection's
20 ms bound and stopped the ME in a PSP pair; audio waits for the next
actor, as before */
#define INNER_GAP_CYCLES (333u * 1000u) /* ~1 ms of the ME's clock */
#define INNER_STACK_ROOM (RING_RAYS_STACK_BYTES + 16 * 1024)
static struct
{
	unsigned int yields, ring_jobs, low, light_looks, light_jobs, light_low, audio, light_audio, pad[8];
} inner_stats_storage __attribute__((aligned(64)));
#define INNER_STATS UNCACHED(&inner_stats_storage)
static unsigned int inner_seen[8];

/* at start: the counters' lines out of this CPU's cache (a dirty line
written back later would put back zeros over the ME's counts) */
static void psp_me_inner_reset(void)
{
	sceKernelDcacheWritebackInvalidateRange(&inner_stats_storage, sizeof(inner_stats_storage));
	INNER_STATS->yields = INNER_STATS->ring_jobs = INNER_STATS->low = 0;
	INNER_STATS->light_looks = INNER_STATS->light_jobs = INNER_STATS->light_low = 0;
	INNER_STATS->audio = INNER_STATS->light_audio = 0;
	memset(inner_seen, 0, sizeof(inner_seen));
}

void psp_me_yield_inner(int ring)
{
	volatile struct me_private *p;
	unsigned int now, sp, popped, last_look;
	int aime_now;

	if (!psp_on_me())
		return;
	now = me_cycles();
	if (now - inner_mark < INNER_GAP_CYCLES)
		return;
	inner_mark = now;
	__asm__ __volatile__("move %0, $sp" : "=r"(sp));
	if (!aime_guard || sp < (unsigned int)aime_guard + ME_EDRAM_AIME_GUARD_BYTES + INNER_STACK_ROOM)
	{
		INNER_STATS->low++;
		return;
	}
	p = ME_PRIVATE;
	INNER_STATS->yields++;
	/* the caller tested the word: it isn't up */
	if ((CONTROL->features & ME_FEATURE_AUDIO_LOOK) && me_audio_due(now))
	{
		/* the census times it as the yield's audio, as between actors */
		aime_now = psp_aime_job_now[0];
		psp_aime_job_now[0] = 0;
		if (aime_now)
			psp_aime_step(7);
		INNER_STATS->audio += me_audio_now();
		psp_aime_job_now[0] = aime_now;
	}
	if (!ring || !p->ring_ready || p->in_ring || RING_PUSHED == p->ring_popped)
		return;
	/* as psp_me_yield's drain: the jobs run here aren't the AI job, and
	the drain stops at the collection's word */
	aime_now = psp_aime_job_now[0];
	psp_aime_job_now[0] = 0;
	if (aime_now)
		psp_aime_step(7);
	popped = p->ring_popped;
	last_look = p->ring_last_look;
	p->ring_popped = me_ring_drain(popped, &last_look, &SLOT(ME_SLOT_AI)->args[ME_JOB_ARGS - 1]);
	p->ring_last_look = last_look;
	INNER_STATS->ring_jobs += p->ring_popped - popped;
	psp_aime_job_now[0] = aime_now;
}

/* "lightringlook" (B50): the render job serves the ring between records,
and one record, the spotlight's light pass over the level's triangles near
the player (DEFER_LIGHT_DRAW, with its cut pieces), runs past the ring's
10 ms deadline on the ME, so a points job pushed meanwhile came back late
and the ring rested for a second (all 12 lates of a census of d20's
flashlight stretch). The pass's triangle loop holds nothing a ring job
reads or writes (its state is the GE layer's: the light, the near plane,
the pieces, the list), so the ring is served there: at most once every
INNER_GAP_CYCLES of the ME's clock, and only with room on the stack for the
deepest ring job above the AI job's guard (a render job can run nested in
the AI job's yields, on its stack). The ring alone: audio is mixed between
records as before, and inside the drain only before a ring job, as it is
there. Inside a nested render job the AI job's mark is down, as in
psp_me_yield, so the drain runs to the ring's end */
static PSP_AIME_ME_SIDE unsigned int light_look_mark = 0;

void psp_me_ring_look_start(void)
{
	if (psp_on_me())
		light_look_mark = me_cycles();
}

void psp_me_ring_look(void)
{
	volatile struct me_private *p;
	unsigned int now, sp, popped, last_look;
	int aime_now;

	if (!psp_on_me())
		return;
	now = me_cycles();
	if (now - light_look_mark < INNER_GAP_CYCLES)
		return;
	light_look_mark = now;
	__asm__ __volatile__("move %0, $sp" : "=r"(sp));
	if (sp < ME_EDRAM + (me_stack_top - (ME_EDRAM_STACK_TOP - ME_EDRAM_AIME_STACK)) + ME_EDRAM_AIME_GUARD_BYTES +
			INNER_STACK_ROOM)
	{
		INNER_STATS->light_low++;
		return;
	}
	p = ME_PRIVATE;
	INNER_STATS->light_looks++;
#ifdef PSP_RECON
	if (CONTROL->features & ME_FEATURE_STASH)
		STASH[STASH_LOOK] = now;
#endif
	/* a nested render job (menestrender) can't see the AI job's mark, so
	the AI job's word is read itself: once it is up, no mix delays it */
	if ((CONTROL->features & ME_FEATURE_AUDIO_LOOK) && me_audio_due(now) && !SLOT(ME_SLOT_AI)->args[ME_JOB_ARGS - 1])
	{
		/* not counted as nested work: audio mixed in a yield isn't
		(psp_me_yield), the audio slot's own time holds it */
		INNER_STATS->light_audio += me_audio_now();
		now = light_look_mark = me_cycles();
	}
	if (!p->ring_ready || p->in_ring || RING_PUSHED == p->ring_popped)
		return;
	aime_now = psp_aime_job_now[0];
	psp_aime_job_now[0] = 0;
	popped = p->ring_popped;
	last_look = p->ring_last_look;
	p->ring_popped = me_ring_drain(popped, &last_look, aime_now ? &SLOT(ME_SLOT_AI)->args[ME_JOB_ARGS - 1] : NULL);
	p->ring_last_look = last_look;
	INNER_STATS->light_jobs += p->ring_popped - popped;
	psp_aime_job_now[0] = aime_now;
	/* counted in the render slot's time and the ring's: the busy share
	takes it off once, as psp_me_yield's drain */
	light_look_mark = me_cycles();
	nested_cycles_total += light_look_mark - now;
}

/* the job on the ME, on the ME's own stack in its eDRAM. Its deepest reach
is measured the same way, every nested job included (audio, the ring and a
render job run inside its yields, on the same stack): the room below the
first job's stack pointer is filled once, and when the main CPU asks
(args[0]) the fill is read from the bottom up to the lowest word written so
far. The figure is the stack's depth from the eDRAM's top, the ME's own
frames included */
static PSP_AIME_ME_SIDE unsigned int aime_me_lowest = 0, aime_me_filled = 0;
#define AIME_STACK_AT (me_stack_top - (ME_EDRAM_STACK_TOP - ME_EDRAM_AIME_STACK))

static void aime_on_me(volatile struct me_slot *slot)
{
	unsigned int *bottom = (unsigned int *)(ME_EDRAM + AIME_STACK_AT + ME_EDRAM_AIME_GUARD_BYTES);
	unsigned int *word;
	unsigned int sp;

	__asm__ __volatile__("move %0, $sp" : "=r"(sp));
	aime_guard_set((volatile unsigned int *)(ME_EDRAM + AIME_STACK_AT));
	if (!aime_me_lowest)
	{
		aime_me_lowest = sp;
		if (sp > (unsigned int)bottom + 256 && sp <= ME_EDRAM + me_stack_top)
		{
			for (word = bottom; (unsigned int)word < sp - 256; word++)
				*word = AIME_SIM_FILL;
			aime_me_filled = 1;
		}
	}
	psp_aime_me_job(slot->args, slot->results);
	aime_guard = NULL;
	if (slot->args[0] && aime_me_filled)
	{
		for (word = bottom; (unsigned int)word < aime_me_lowest && *word == AIME_SIM_FILL; word++)
			;
		if ((unsigned int)word < aime_me_lowest)
			aime_me_lowest = (unsigned int)word;
	}
	slot->results[ME_JOB_RESULTS - 1] = ME_EDRAM + me_stack_top - aime_me_lowest;
}
static unsigned char *aime_sim_stack = NULL;
/* (written while the emulator marks the work as the ME's) */
static PSP_AIME_ME_SIDE unsigned int aime_sim_deepest = 0;

static void aime_run_simulated(unsigned int slot_address)
{
	volatile struct me_slot *slot = (volatile struct me_slot *)slot_address;

	psp_aime_me_job(slot->args, slot->results);
}

static __attribute__((noinline, cold)) void aime_simulated(volatile struct me_slot *slot)
{
	static int tried = 0;
	unsigned int k, deepest;

	if (!tried)
	{
		tried = 1;
		/* On the PSP this stack is the ME's eDRAM; here it is the game's
		heap, so every emulator figure of the heap, the texture caches or
		the sound cache reads a machine that much tighter than the PSP
		(heapmap1 41700604943: 1,184 KB with the old slack, the heap's
		second-largest block). No partition outside the heap gives it to a
		user thread (simstack2 41736845256: 1, 3, 4, 6, 8 and 9 refused), so
		the slack below the room is kept small unless a run measures the
		job's reach: "aimesimslack" for the old 640 KB */
		if (psp_keep("aimesimslack"))
			aime_sim_slack_bytes = 640 * 1024;
		/* An emulator that maps the eDRAM (kemulator:'s span) takes the
		heap's copy out: the room at the PSP's own address, and the slack
		in the job's data reserve below it, which nothing places anything
		in. 64 KB would reach 16 KB into the sound tier's end, so the slack
		is 48 KB there. "aimestackram" keeps the heap's copy, as an
		emulator without the map has it */
		else if (!psp_keep("aimestackram") &&
			psp_me_sim_edram_holds(ME_EDRAM_AIME_DATA, ME_EDRAM_STACK_TOP - ME_EDRAM_AIME_DATA))
		{
			aime_sim_slack_bytes = ME_EDRAM_AIME_STACK - ME_EDRAM_AIME_DATA;
			aime_sim_stack = (unsigned char *)(ME_EDRAM + ME_EDRAM_AIME_DATA);
		}
		if (!aime_sim_stack)
			aime_sim_stack = (unsigned char *)memalign(64, AIME_SIM_SLACK_BYTES + AIME_SIM_STACK_BYTES);
		if (!aime_sim_stack)
			platform_log("ME (simulated): no room for the AI job's stack; it runs on the caller's");
		else
			platform_log("ME (simulated): the AI job's stack %u KB (the PSP's room) and %u KB below it, %p-%p, its guard at %p%s",
				AIME_SIM_STACK_BYTES / 1024, AIME_SIM_SLACK_BYTES / 1024, aime_sim_stack,
				aime_sim_stack + AIME_SIM_SLACK_BYTES + AIME_SIM_STACK_BYTES, aime_sim_stack + AIME_SIM_SLACK_BYTES,
				(unsigned int)aime_sim_stack == ME_EDRAM + ME_EDRAM_AIME_DATA ? "; SIMULATED: in the emulator's mapped eDRAM" : "");
	}
	/* (psp_ge.c's psp_me_window_split: the job's window its own) */
	if (psp_me_window_split)
		psp_me_window[0] = 0;
	psp_me_window[0] = 1;
	psp_me_window_hold = 1;
	psp_me_context = (unsigned int)sceKernelGetThreadId();
	if (aime_sim_stack)
	{
		unsigned int *words = (unsigned int *)aime_sim_stack;
		unsigned int guard_first = AIME_SIM_SLACK_BYTES / 4, guard_end = guard_first + ME_EDRAM_AIME_GUARD_BYTES / 4;

		for (k = 0; k < (AIME_SIM_SLACK_BYTES + AIME_SIM_STACK_BYTES) / 4; k++)
			words[k] = AIME_SIM_FILL;
		aime_guard_set((volatile unsigned int *)(words + guard_first));
		/* the pass's context (psp_post.h's psp_aime_here): this stack */
		psp_aime_sp_low = (unsigned int)aime_sim_stack;
		psp_aime_sp_high = (unsigned int)aime_sim_stack + AIME_SIM_SLACK_BYTES + AIME_SIM_STACK_BYTES;
		psp_me_sim_call((unsigned int)slot, aime_sim_stack + AIME_SIM_SLACK_BYTES + AIME_SIM_STACK_BYTES, aime_run_simulated);
		psp_aime_sp_low = psp_aime_sp_high = 0;
		aime_guard = NULL;
		for (k = 0; k < (AIME_SIM_SLACK_BYTES + AIME_SIM_STACK_BYTES) / 4 &&
			(words[k] == AIME_SIM_FILL || (k >= guard_first && k < guard_end && words[k] == AIME_GUARD_WORD)); k++)
			;
		deepest = AIME_SIM_SLACK_BYTES + AIME_SIM_STACK_BYTES - k * 4;
		/* the whole slack used: the reach may have gone on below it, into
		whatever sits there (the sound tier's end, or the heap's next block) */
		if (deepest == AIME_SIM_SLACK_BYTES + AIME_SIM_STACK_BYTES && aime_sim_deepest < deepest)
			platform_log("ME (simulated): the AI job's stack reached the bottom of its slack (%u KB); its figure is a floor",
				deepest / 1024);
		if (deepest > aime_sim_deepest)
			aime_sim_deepest = deepest;
		slot->results[ME_JOB_RESULTS - 1] = aime_sim_deepest;
	}
	else
		psp_aime_me_job(slot->args, slot->results);
	psp_me_context = 0;
}

/* the emulator: the job's code here and now, marked as the ME's for the
ME-strict check and the write trace (emu.sh's gemesim and mewrites), on a
stack of its own as on the ME; the window stays open until the wait. Cold,
as the failure paths below: the PSP's push and wait stay fewer cache lines */
static __attribute__((noinline, cold)) void ring_push_simulated(unsigned int index, unsigned int t)
{
	static unsigned char *sim_stack = NULL;
	static int sim_stack_tried = 0;
	unsigned int job_start;

	if (!ring_thread)
		ring_thread = (unsigned int)sceKernelGetThreadId();
	if (!sim_stack_tried)
	{
		sim_stack_tried = 1;
		sim_stack = (unsigned char *)memalign(64, RING_SIM_STACK_BYTES);
		if (!sim_stack)
			platform_log("ME (simulated): no room for the job stack; jobs run on the producer's");
	}
	psp_me_window[0] = 1;
	ring_sim_open = 1;
	job_start = sceKernelGetSystemTimeLow();
	psp_me_context = ring_thread;
	if (sim_stack)
		psp_me_sim_call(index, sim_stack + RING_SIM_STACK_BYTES, ring_run_simulated);
	else
		ring_run(index, 0, 0);
	psp_me_context = 0;
	ring_sim_us[index] = sceKernelGetSystemTimeLow() - job_start;
	ring_stats.simulated_us += ring_sim_us[index];
#ifdef PSP_RECON
	if (ring_late_every && t && t % ring_late_every == 0)
		ring_sim_behind = 1;
	if (ring_sim_behind)
		return;
#endif
	RING_POPPED = t + 1;
}

/* the debug stash, to the log, when the main CPU gives up on a job: where
the ME was (STASH_AT: 1 between jobs, 2-4 a slot job's cache work, code and
write-back, 5-7 a ring job's, 8 halted, 9 stopped), the ring job and how far
into it, its cycle counter then and now (the same: the ME has stopped
counting; apart: it runs, and was there that long ago) */
static __attribute__((noinline, cold)) void me_stash_dump(const char *why)
{
	volatile unsigned int *stash = STASH;
	unsigned int now = CONTROL->clock;
	float rate = me_cycles_per_us > 0.0f ? me_cycles_per_us : 1.0f;

	if (!(CONTROL->features & ME_FEATURE_STASH))
		return;

	platform_log("ME: stash (%s): at %u; ring job %u (type %u), step %u; ring jobs begun %u, ended %u, pushed as last seen %u, popped %u; "
		"slot jobs begun %u, ended %u, in hand by type %08x; audio blocks mixed before or inside ring jobs %u; the ME's clock at the note %u, at its last idle pass %u (%d us after the note: below 0, it hasn't been back to its idle loop since), idle polls %u",
		why, stash[STASH_AT], stash[STASH_TICKET], stash[STASH_TYPE], stash[STASH_STEP], stash[STASH_RING_BEGUN],
		stash[STASH_RING_ENDED], stash[STASH_PUSHED], RING_POPPED, stash[STASH_SLOT_BEGUN], stash[STASH_SLOT_ENDED],
		stash[STASH_SLOTS_IN], stash[STASH_AUDIO_INSIDE], stash[STASH_CLOCK], now, (int)((float)(int)(now - stash[STASH_CLOCK]) / rate),
		CONTROL->alive);
	{
		/* the ME's clock can't be read here: its latest mark stands in for
		now, so each age is a lower bound */
		unsigned int marks[4] = { stash[STASH_CLOCK], stash[STASH_PLACE_CLOCK], stash[STASH_LOOK], now }, latest = marks[0], k;

		for (k = 1; k < 4; k++)
			if ((int)(marks[k] - latest) > 0)
				latest = marks[k];
		platform_log("ME: stash (%s), continued: the ME's last look at the ring %d us before its latest mark; a long job's place %08x "
			"(PSP_RECON; the AI job's 0f0000ss with ss its step and the tag above, the render job's 040000kk with kk the record's kind), "
			"reached %d us before it; the note %d us before it",
			why, (int)((float)(int)(latest - stash[STASH_LOOK]) / rate), stash[STASH_PLACE],
			(int)((float)(int)(latest - stash[STASH_PLACE_CLOCK]) / rate), (int)((float)(int)(latest - stash[STASH_CLOCK]) / rate));
	}
	psp_log_flush();
}

/* a failed wait, reported (why: 0 stopped, 1 too late, 2 a bad answer); the
ring stops unless the ME itself was stopped */
static __attribute__((noinline, cold)) int ring_fail(int why, unsigned int ticket, unsigned int index)
{
	const struct me_ring_request *request = &ring_requests[index];
	const struct me_ring_result *result = &ring_results[index];

	if (why == 1 && ring_dead == 0)
	{
		unsigned long long now = sceKernelGetSystemTimeWide();

		/* the jobs pushed with it are late too: one late stretch, one rest */
		ring_lates++;
		if (ring_revivals && now - ring_revive_mark <= RING_AGAIN_US)
			ring_rest_us = ring_rest_us * 2 > RING_REST_MAX_US ? RING_REST_MAX_US : ring_rest_us * 2;
		else
			ring_rest_us = ring_rest_base_us;
		ring_late_mark = now;
		ring_dead = ring_give_up ? 1 : 2;
		if (ring_give_up)
			platform_log("ME: ring job %u (type %u) not done %u ms after its push (popped %u); the main CPU does it, and the ring is off from here (ringgiveup)",
				ticket, request->type, RING_DEADLINE_US / 1000, RING_POPPED);
		else
			platform_log("ME: ring job %u (type %u) not done %u ms after its push (popped %u); the main CPU does it, and the ring rests at least %lu ms, until the ME has caught up (late stretches %u)",
				ticket, request->type, RING_DEADLINE_US / 1000, RING_POPPED, (unsigned long)(ring_rest_us / 1000), ring_lates);
		if (me_state == 1)
			me_stash_dump("a ring job late");
	}
	else if (why == 2)
	{
		platform_log("ME: ring job %u (type %u) came back %s (ticket %u); the main CPU does it, and the ring is off from here",
			ticket, request->type, result->status == RING_STACK ? "past its stack allowance" :
			result->status == RING_BAD_TYPE ? "as an unknown type" : "with another job's answer", result->ticket);
		ring_dead = 1;
	}
	ring_stats.failures++;
	return 0;
}

/* the frame end's look at a resting ring (RING_DEADLINE_US says why it
rests): back on once the ME runs, has popped every job pushed, and the rest
is over. Pushes come only from the game thread, which is here, so none can
land between the look at popped and the ring's return */
static __attribute__((noinline, cold)) void ring_revive(void)
{
	unsigned long long now = sceKernelGetSystemTimeWide();

	if (now - ring_late_mark < ring_rest_us || me_state <= 0 || CONTROL->stop)
		return;
#ifdef PSP_RECON
	if (ring_hang_test)
		return;
	if (ring_sim_behind)
	{
		ring_sim_behind = 0;
		RING_POPPED = ring_next;
	}
#endif
	if (RING_POPPED != ring_next)
		return;
	ring_popped_seen = ring_next;
	ring_dead = 0;
	ring_revivals++;
	ring_revive_mark = now;
	platform_log("ME: the ring back on, %lu ms after its late job (late stretches %u, back on %u times)",
		(unsigned long)((now - ring_late_mark) / 1000), ring_lates, ring_revivals);
}

/* a job collected later than the ring's next ME_RING_ENTRIES pushes would
allow (next tick's poses, collected after the frame's drawing has pushed
its own jobs): its collector runs before the push that would reuse its
entry, so its answer is never written over */
static unsigned int ring_held_ticket;
static void (*ring_held_collect)(void);

void psp_me_hold(unsigned int ticket, void (*collect)(void))
{
	ring_held_ticket = ticket + PSP_MEPROBE_EXPR(3);
	ring_held_collect = collect;
}

/* a job handed to the ring: 1, with its ticket for psp_me_wait; 0 if the
ring can't take it (off, full, the ME busy with a long slot job, or a
transfer that breaks the rules above): the caller does the work itself */
#ifdef PSP_RECON
static void bus_collect(void);
static int bus_running = 0;
#endif

int psp_me_push(const struct psp_me_job *job, unsigned int *ticket)
{
	struct me_ring_request *request;
	unsigned int start, t, index, k;

	if (PSP_MEPROBE_EXPR(2) || !psp_me_ring_ready())
		return 0;
#ifdef PSP_RECON
	/* a real job goes first: "mebus"'s load stops and is collected */
	if (bus_running && job->type != ME_JOB_BUSLOAD)
		bus_collect();
#endif
	if (ring_held_collect && ring_next - ring_held_ticket >= ME_RING_ENTRIES - 1)
	{
		void (*collect)(void) = ring_held_collect;

		ring_held_collect = NULL;
		collect();
	}
	start = sceKernelGetSystemTimeLow();
	/* a render or probe job holds the ME for milliseconds (gemeon's are
	~10 ms on the PSP, review_b120.md 5): a ring job behind it would be
	waited for longer than its work is worth */
	for (k = 0; k < ME_SLOTS; k++)
		if (k != ME_SLOT_AUDIO && me_mark(k) && !slot_yields[k])
		{
			if (ring_slot_done && psp_me_submitted_done((int)k))
			{
				ring_slot_passed++;
				continue;
			}
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
		if (!(job->input[k] == NULL && job->input_bytes[k] == ME_RING_ALL_OF_RAM) &&
			!ring_in_ram((unsigned int)job->input[k], job->input_bytes[k]))
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
		ring_push_simulated(index, t);
	else
	{
		/* the output's lines written back and dropped here: the wait drops
		them with a write-back too (0x1B), and a line the caller had left
		dirty would then land on the ME's answer */
		if (ring_push_drop && job->output_bytes)
		{
			if (job->output_bytes > RING_WHOLE_CACHE_BYTES)
				sceKernelDcacheWritebackInvalidateAll();
			else
				lines_drop((unsigned int)job->output, job->output_bytes);
		}
		lines_writeback((unsigned int)request, sizeof(*request));
		RING_PUSHED = t + 1;
		__asm__ __volatile__("sync");
		me_wake();
	}
	*ticket = t;
	ring_stats.push_us += sceKernelGetSystemTimeLow() - start;
	return 1;
}

/* perception's queued rays (psp_fixups.py FIXES_122, actor_perception.c
#merays). The engine's units don't see this header, so the job is put
together here. A ray reads the level and the objects anywhere in RAM: this
CPU's whole cache is written back first, and the ME drops its whole cache.
Once a hand-over, not once a job: a tick's jobs are pushed one after the
other with nothing written in between (continues: not the first of them), and
nothing a ray reads changes until all of them are collected (the five
functions that count psp_object_epoch wait for them first), so what the first
job made fresh on the ME stays fresh for the rest */
int psp_me_push_perc_rays(const void *rays, unsigned int count, void *answers, unsigned int answer_bytes,
	unsigned int *ticket, int continues)
{
	struct psp_me_job job;

	memset(&job, 0, sizeof(job));
	job.type = ME_JOB_PERC_RAYS;
	job.args[0] = (unsigned int)rays;
	job.args[1] = (unsigned int)answers;
	job.args[2] = count;
	job.args[ME_RING_ARG_CONTINUES] = continues && rays_batch;
	job.input[0] = NULL;
	job.input_bytes[0] = ME_RING_ALL_OF_RAM;
	job.output = answers;
	job.output_bytes = answer_bytes;
	if (!job.args[ME_RING_ARG_CONTINUES])
		psp_me_writeback(NULL, ME_RING_ALL_OF_RAM);
	return psp_me_push(&job, ticket);
}

/* "mevis" (psp_fixups.py mevis_copies, structure_visibility.c): the frame's
visibility made again on the ME from the camera in the record, a measurement
of what option C's walk would cost there. It reads the level and the objects
anywhere in RAM (the whole cache handed over, as for the rays), and writes its
record's answer lines and its own state, on lines nothing else uses. Never
waited for with psp_me_wait: the frame's end looks at psp_me_ring_done, so a
slow answer never makes the ring rest */
#if defined(PSP_WALKAPART) || defined(PSP_MEVISLAND)
int psp_me_push_visibility(void *record, unsigned int record_bytes, unsigned int *ticket)
{
	struct psp_me_job job;

	memset(&job, 0, sizeof(job));
	job.type = ME_JOB_VISIBILITY;
	job.args[0] = (unsigned int)record;
	job.input[0] = NULL;
	job.input_bytes[0] = ME_RING_ALL_OF_RAM;
	job.output = record;
	job.output_bytes = record_bytes;
	psp_me_writeback(NULL, ME_RING_ALL_OF_RAM);
	return psp_me_push(&job, ticket);
}

/* "mevisland": the same job, whose answer the frame uses. Its output is the
copies' whole state block (the clusters, the surfaces' bits, the objects),
past the cache's size, so the ME writes back its whole cache as the job ends
and the record's lines go with it */
int psp_me_push_visibility_into(void *record, void *answer, unsigned int answer_bytes, unsigned int *ticket)
{
	struct psp_me_job job;

	memset(&job, 0, sizeof(job));
	job.type = ME_JOB_VISIBILITY;
	job.args[0] = (unsigned int)record;
	job.input[0] = NULL;
	job.input_bytes[0] = ME_RING_ALL_OF_RAM;
	job.output = answer;
	job.output_bytes = answer_bytes;
	psp_me_writeback(NULL, ME_RING_ALL_OF_RAM);
	return psp_me_push(&job, ticket);
}
#endif

/* a ring job done (its output written back), without waiting */
int psp_me_ring_done(unsigned int ticket)
{
	return (int)(RING_POPPED - ticket) > 0;
}

/* next tick's base poses (psp_fixups.py FIXES_131, objects.c #poseahead): a
decode reads only tag data (the model, the animation, both fixed within a
map), so the whole cache is handed over as for the rays, and the job writes
only the poses, in the arena (output) */
int psp_me_push_poses(const void *entries, unsigned int count, void *arena, unsigned int arena_bytes,
	unsigned int *ticket)
{
	struct psp_me_job job;
	unsigned int control;

	memset(&job, 0, sizeof(job));
	job.type = ME_JOB_POSES;
	job.args[0] = (unsigned int)entries;
	job.args[1] = count;
	/* this CPU's FPU control (rounding, flushing, traps): the ME decodes
	under the same, so its arithmetic is this CPU's bit for bit */
	__asm__ __volatile__("cfc1 %0, $31" : "=r"(control));
	job.args[2] = control;
	job.input[0] = NULL;
	job.input_bytes[0] = ME_RING_ALL_OF_RAM;
	job.output = arena;
	job.output_bytes = arena_bytes;
	psp_me_writeback(NULL, ME_RING_ALL_OF_RAM);
	return psp_me_push(&job, ticket);
}

/* the frame's point physics (psp_fixups.py FIXES_MEJOBS, point_physics.c
#mepoints): a point reads the level, its water, the wind and its tags, so
the whole cache is handed over as for the rays. The ME writes only the
answers (output) and its word; it reads the main CPU's word, which tells it
where to stop, through the uncached view. Both words' lines are the callers' */
int psp_me_push_points(const void *inputs, unsigned int count, void *answers, unsigned int answer_bytes,
	unsigned int generation, void *main_word, void *me_word, unsigned int *ticket)
{
	struct psp_me_job job;
	unsigned int control;

	memset(&job, 0, sizeof(job));
	job.type = ME_JOB_POINTS;
	job.args[0] = (unsigned int)inputs;
	job.args[1] = (unsigned int)answers;
	job.args[2] = count;
	job.args[3] = generation;
	/* this CPU's FPU control: the ME's arithmetic is then this CPU's */
	__asm__ __volatile__("cfc1 %0, $31" : "=r"(control));
	job.args[4] = control;
	job.args[5] = (unsigned int)main_word;
	job.args[6] = (unsigned int)me_word;
	job.input[0] = NULL;
	job.input_bytes[0] = ME_RING_ALL_OF_RAM;
	job.output = answers;
	job.output_bytes = answer_bytes;
	psp_me_writeback(NULL, ME_RING_ALL_OF_RAM);
	return psp_me_push(&job, ticket);
}

/* a pushed job's answer: 1 when done (results filled, its output readable
on this CPU), 0 if the ME failed it (stopped, too late, or the job broke a
rule): the caller does the work itself, and must not read the output */
/* the batches' jobs as the ring collected them: how long each can have
waited to be picked up, and its own time (ME cycles), for the batches'
timing line */
static struct
{
	unsigned long jobs;
	unsigned long long look_cycles, job_cycles;
	unsigned int look_worst, job_worst;
} batch_ring;

int psp_me_wait(unsigned int ticket, unsigned int *results)
{
	unsigned int index = ticket & RING_MASK, start = (PSP_MEPROBE_EXPR(1), sceKernelGetSystemTimeLow()), now, looks = 0, k;
	struct me_ring_request *request = &ring_requests[index];
	struct me_ring_result *result = &ring_results[index];
	unsigned long waited;

	if (ring_next - ticket - 1 >= ME_RING_ENTRIES)
		return 0;
	/* simulated, the push opened the window, whatever has stopped since */
	if (me_restartable == 2 || me_state == 2)
	{
		psp_me_window[0] = psp_me_window_hold;
		ring_sim_open = 0;
	}
	/* without this, popped (moved past it by the restart) would say it was
	done, and another job's answer would stop the ring for the session */
	if (ring_restarted && ticket - ring_epoch >= ring_next - ring_epoch)
		return ring_fail(0, ticket, index);
#ifdef PSP_RECON
	if (ring_late_every && ticket && ticket % ring_late_every == 0)
	{
		while (sceKernelGetSystemTimeLow() - ring_push_mark[index] <= RING_DEADLINE_US)
			for (k = 0; k < RING_WAIT_NOPS; k++)
				__asm__ __volatile__("nop");
		return ring_fail(1, ticket, index);
	}
#endif
	if ((int)(RING_POPPED - ticket) > 0)
		ring_stats.at_once++;
	else
	{
		/* ticksched: the replay picks this job up at its next record */
		RING_URGENT = ticket + 1;
		while ((int)(RING_POPPED - ticket) <= 0)
		{
			if (looks++ % RING_WAIT_CLOCK_EVERY == 0)
			{
				now = sceKernelGetSystemTimeLow();
				/* stopped (HOME, sleep, a slot job's timeout) before this job */
				if (me_state <= 0 || CONTROL->stop == 2)
				{
					RING_URGENT = 0;
					return ring_fail(0, ticket, index);
				}
				if (now - ring_push_mark[index] > RING_DEADLINE_US)
				{
					/* as a batch record's: late only if still not done */
					if (!me_grace_off && (int)(RING_POPPED - ticket) > 0)
					{
						if (deadline_lines < DEADLINE_LINES)
						{
							deadline_lines++;
							platform_log("ME: ring job %u (type %u) past %u ms by the clock, done at the look after it (pushed %u us before)",
								ticket, request->type, RING_DEADLINE_US / 1000, now - ring_push_mark[index]);
						}
						break;
					}
					RING_URGENT = 0;
					return ring_fail(1, ticket, index);
				}
				/* mehalt: a wake that may have been lost (me_halt_idle) */
				me_wake();
			}
			for (k = 0; k < RING_WAIT_NOPS; k++)
				__asm__ __volatile__("nop");
		}
		RING_URGENT = 0;
	}
	/* this CPU's copies of what the ME wrote are stale: dropped (it never
	writes these lines itself, so dropping loses nothing of its own) */
	lines_drop((unsigned int)result, sizeof(*result));
	if (request->output_bytes > RING_WHOLE_CACHE_BYTES)
		sceKernelDcacheWritebackInvalidateAll();
	else if (request->output_bytes)
		lines_drop(request->output, request->output_bytes);
	if (result->ticket != ticket || result->status != RING_OK)
		return ring_fail(2, ticket, index);
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
#if PSP_RECON
	psp_me_waitwho_add(result->who, request->type, result->look_cycles);
#endif
	ring_stats.type_jobs[request->type & 15]++;
	if (result->job_cycles > ring_stats.type_worst_cycles[request->type & 15])
		ring_stats.type_worst_cycles[request->type & 15] = result->job_cycles;
	if (result->look_cycles > ring_stats.look_worst_cycles)
		ring_stats.look_worst_cycles = result->look_cycles;
	if (request->type == ME_JOB_BATCH)
	{
		batch_ring.jobs++;
		batch_ring.look_cycles += result->look_cycles;
		batch_ring.job_cycles += result->job_cycles;
		if (result->look_cycles > batch_ring.look_worst)
			batch_ring.look_worst = result->look_cycles;
		if (result->job_cycles > batch_ring.job_worst)
			batch_ring.job_worst = result->job_cycles;
	}
	/* "lanetrace" (psp_lanetrace.c): pushed, collected, the ME's own time */
	PSP_LANE(PSP_LANE_ME_RING, ring_push_mark[index], now, request->type,
		me_cycles_per_us > 0.0f ? (unsigned int)((float)result->job_cycles / me_cycles_per_us) : 0);
	return 1;
}

/* "mevisring": a ring job the game collects by polling (psp_me_ring_done:
mevisland's visibility) instead of through psp_me_wait, which alone counted
ring jobs, so the ring's figures, the busy share and lanetrace left it out.
The poll that first sees it done calls this: its result is read and counted
on a line of its own, its ME time joins the busy share, and lanetrace gets
its span from the push to that poll. Off, nothing is read or counted */
static int ring_polled_on = -1;
static struct
{
	unsigned long jobs, uncounted;
	unsigned long long job_cycles, all_cycles, round_us, simulated_us;
	unsigned long worst_cycles, worst_simulated_us;
} ring_polled;

void psp_me_ring_collected(unsigned int ticket)
{
	unsigned int index = ticket & RING_MASK, now;
	struct me_ring_request *request = &ring_requests[index];
	struct me_ring_result *result = &ring_results[index];

	if (ring_polled_on < 0)
		ring_polled_on = psp_keep("mevisring") != 0;
	if (!ring_polled_on)
		return;
	/* a ticket the ring has moved past (its entry reused) or not yet done */
	if (ring_next - ticket - 1 >= ME_RING_ENTRIES || (int)(RING_POPPED - ticket) <= 0)
	{
		ring_polled.uncounted++;
		return;
	}
	/* this CPU never writes the result's lines: dropping them loses nothing */
	lines_drop((unsigned int)result, sizeof(*result));
	if (me_state != 2 && (result->ticket != ticket || result->status != RING_OK))
	{
		ring_polled.uncounted++;
		return;
	}
	now = sceKernelGetSystemTimeLow();
	ring_polled.jobs++;
	ring_polled.round_us += now - ring_push_mark[index];
	if (me_state == 2)
	{
		ring_polled.simulated_us += ring_sim_us[index];
		if (ring_sim_us[index] > ring_polled.worst_simulated_us)
			ring_polled.worst_simulated_us = ring_sim_us[index];
		PSP_LANE(PSP_LANE_ME_RING, ring_push_mark[index], now, request->type, ring_sim_us[index]);
		return;
	}
	ring_polled.job_cycles += result->job_cycles;
	ring_polled.all_cycles += result->all_cycles;
	if (result->all_cycles > ring_polled.worst_cycles)
		ring_polled.worst_cycles = result->all_cycles;
	PSP_LANE(PSP_LANE_ME_RING, ring_push_mark[index], now, request->type,
		me_cycles_per_us > 0.0f ? (unsigned int)((float)result->all_cycles / me_cycles_per_us) : 0);
}

/* the polled jobs' line (psp_me_report), and their ME time for the busy
share: on the PSP their own time; in the emulator 0, since the ring's
simulated time, which the busy share takes whole, already holds it */
static __attribute__((noinline, cold)) unsigned long long ring_polled_report(void)
{
	float rate = me_cycles_per_us > 0.0f ? me_cycles_per_us : 1.0f;
	unsigned long jobs = ring_polled.jobs ? ring_polled.jobs : 1;
	unsigned long long all_us = (unsigned long long)((float)ring_polled.all_cycles / rate);

	if (ring_polled_on <= 0 || (!ring_polled.jobs && !ring_polled.uncounted))
		return 0;
	if (me_state == 2)
		platform_log("ME: ring, polled (mevisring): %lu jobs, SIMULATED inside the push, %lu us a job (longest %lu), %llu us in all; push to seen done %lu us a job; %lu not counted",
			ring_polled.jobs, (unsigned long)(ring_polled.simulated_us / jobs), ring_polled.worst_simulated_us,
			ring_polled.simulated_us, (unsigned long)(ring_polled.round_us / jobs), ring_polled.uncounted);
	else
		platform_log("ME: ring, polled (mevisring): %lu jobs, the ME's own time %lu us a job (the job's code %lu; longest %lu), %llu us in all; push to seen done %lu us a job; %lu not counted",
			ring_polled.jobs, (unsigned long)(all_us / jobs), (unsigned long)((float)ring_polled.job_cycles / rate / (float)jobs),
			(unsigned long)((float)ring_polled.worst_cycles / rate), all_us, (unsigned long)(ring_polled.round_us / jobs),
			ring_polled.uncounted);
	memset(&ring_polled, 0, sizeof(ring_polled));
	return me_state == 2 ? 0 : all_us;
}

/* ---------- batches: the main CPU's side (psp_me.h has the contract) */

/* a record's state, the main CPU's own byte */
#define BATCH_ADDED 0   /* filled here, not handed over */
#define BATCH_OUT 1     /* in a job pushed to the ME */
#define BATCH_HERE 2    /* made by the main CPU */
#define BATCH_BY_ME 3   /* made by the ME, its lines dropped here */
/* the most a record the ME is on may hold the main CPU before it looks
again: past it, a record still not done is slow, and one still not done at
BATCH_RECORD_GIVEUP_US stops the ME (the limit a slot's audio job has).
With "megraceoff", the first deadline stops the ME */
#define BATCH_RECORD_DEADLINE_US 10000
#define BATCH_RECORD_GIVEUP_US 40000

static struct
{
	unsigned long passes, by_me, by_main, taken, waits, refused, late;
	/* past the deadline by the clock: done at the look after it (this CPU
	was off its wait meanwhile), or still on the ME (slow) */
	unsigned long second_look, slow;
	/* jobs the ME ended at the limit; the longest wait for a record, and
	the longest stretch between two looks in one (a thread above the game's
	ran meanwhile) */
	unsigned long withdrawn, wait_worst_us, gap_worst_us;
	/* waits ended because every job of the batch had ended with the record
	not made: a claim left behind by a later job's withdrawal */
	unsigned long stale;
	/* the timing line: from a record's hand-over to the gather's ask, by
	what the ask found (made, the ME on it, not begun), sum and longest in
	us; passes whose first ask found the ME already begun on the batch */
	unsigned long ask_n[3], ask_worst[3], passes_begun;
	unsigned long long ask_us[3];
} batch_stats;
static int batch_limit_old = -1; /* "mebatchlimit": the one limit word, as before claims */


int psp_me_batch_init(struct psp_me_batch *b, psp_me_record_fn make, void *records, unsigned char *state,
	unsigned int record_bytes, unsigned int capacity, unsigned int chunk, const unsigned int *args)
{
	unsigned int k;

	if (((unsigned int)b & 63u) || !make || !records || !state || !record_bytes || (record_bytes & 63u) ||
		((unsigned int)records & 63u) || !capacity || !chunk || !ring_in_ram((unsigned int)records, capacity * record_bytes))
		return 0;
	memset(b, 0, sizeof(*b));
	/* the words go through the uncached view only: no cached copy of their
	lines may be left here to be written back over the ME's */
	if (batch_limit_old < 0)
		batch_limit_old = psp_keep("mebatchlimit") != 0;
	b->claims = !batch_limit_old;
	if (b->claims && capacity > ME_BATCH_CLAIM_RECORDS)
		return 0;
	/* the claims' words too: the ME writes them through its uncached view */
	sceKernelDcacheWritebackInvalidateRange(b, (unsigned int)((unsigned char *)&b->hand_us[0] - (unsigned char *)b));
	b->make = make;
	b->records = (unsigned char *)records;
	b->state = state;
	b->record_bytes = record_bytes;
	b->capacity = capacity;
	b->chunk = chunk;
	for (k = 0; k < ME_BATCH_ARGS; k++)
		b->args[k] = args ? args[k] : 0;
	batch_sim_half = psp_keep("mebatchsimhalf");
	psp_me_batch_begin(b);
	return 1;
}

void psp_me_batch_begin(struct psp_me_batch *b)
{
	if (batch_sim_behind == b)
		batch_sim_behind = NULL;
	batch_sim_whole = batch_sim_passes++ & 3u;
	b->added = b->handed = b->ticket_count = 0;
	b->asked = 0;
	b->limit_now = 0xFFFFFFFFu;
	UNCACHED(&b->limit)->value = 0xFFFFFFFFu;
	UNCACHED(&b->started)->value = 0;
	UNCACHED(&b->done)->value = 0;
	UNCACHED(&b->withdrawn)->value = 0;
	__asm__ __volatile__("sync");
}

/* records [b->handed, end) to the ME as one ring job; left here if the ring
can't take it */
static void batch_hand(struct psp_me_batch *b, unsigned int end)
{
	struct psp_me_job job;
	unsigned int first = b->handed, k, ticket, control, now;

	if (end <= first || b->ticket_count >= ME_BATCH_TICKETS || !psp_me_ring_ready())
		return;
	memset(&job, 0, sizeof(job));
	job.type = ME_JOB_BATCH;
	job.args[0] = (unsigned int)b;
	job.args[1] = first;
	job.args[2] = end - first;
	__asm__ __volatile__("cfc1 %0, $31" : "=r"(control));
	job.args[3] = control;
	job.args[4] = (unsigned int)b->make;
	job.args[5] = (unsigned int)b->records;
	job.args[6] = b->record_bytes | (b->claims ? 1u : 0u);
	for (k = 0; k < ME_BATCH_ARGS; k++)
		job.args[7 + k] = b->args[k];
	job.input[0] = b->records + first * b->record_bytes;
	job.input_bytes[0] = (end - first) * b->record_bytes;
	psp_me_writeback(job.input[0], job.input_bytes[0]);
	if (!psp_me_push(&job, &ticket))
	{
		b->refused++;
		return;
	}
	now = sceKernelGetSystemTimeLow();
	for (k = first; k < end; k++)
	{
		b->state[k] = BATCH_OUT;
		if (b->claims)
			b->hand_us[k] = now;
	}
	b->tickets[b->ticket_count++] = ticket;
	b->handed = end;
}

void *psp_me_batch_add(struct psp_me_batch *b, unsigned int *index)
{
	if (b->added >= b->capacity)
		return NULL;
	/* the records before this one are filled: a chunk's worth goes now */
	if (b->added - b->handed >= b->chunk)
		batch_hand(b, b->added);
	*index = b->added;
	b->state[b->added] = BATCH_ADDED;
	/* a new pass's record: no claims yet (every job of the last pass was
	collected at its finish, so nothing on the ME reads these now) */
	if (b->claims)
	{
		UNCACHED(&b->me_on[b->added])[0] = 0;
		UNCACHED(&b->made[b->added])[0] = 0;
		UNCACHED(&b->here_on[b->added])[0] = 0;
		__asm__ __volatile__("sync");
	}
	return b->records + b->added++ * b->record_bytes;
}

void psp_me_batch_close(struct psp_me_batch *b)
{
	batch_hand(b, b->added);
}

static void batch_make_here(struct psp_me_batch *b, unsigned int index)
{
	b->make(b->records + index * b->record_bytes, b->args);
	b->state[index] = BATCH_HERE;
	b->by_main++;
}

/* the timing line's ask: from the record's hand-over to this ask, by what
it found (0 made by the ME, 1 the ME on it, 2 not begun) */
static void batch_ask(struct psp_me_batch *b, unsigned int index, int found)
{
	unsigned int us = sceKernelGetSystemTimeLow() - b->hand_us[index], k;

	batch_stats.ask_n[found]++;
	batch_stats.ask_us[found] += us;
	if (us > batch_stats.ask_worst[found])
		batch_stats.ask_worst[found] = us;
	if (!b->asked)
	{
		b->asked = 1;
		for (k = 0; k < b->handed; k++)
			if (UNCACHED(&b->me_on[k])[0] || UNCACHED(&b->made[k])[0])
			{
				batch_stats.passes_begun++;
				break;
			}
	}
}

/* with claims: a record handed over and not made by the ME is claimed
here; if the ME holds its claim, this CPU waits for that record alone
(bounded as below), else makes it. The ME passes every record claimed here
and makes the rest, so an ME picked up late still makes what the gather
hasn't reached */
static int batch_need_claim(struct psp_me_batch *b, unsigned int index)
{
	volatile unsigned int *me_on = UNCACHED(&b->me_on[index]), *made = UNCACHED(&b->made[index]);
	unsigned char *record = b->records + index * b->record_bytes;
	unsigned int start, now, last, gap, waited;
	int slow = 0;

	if (*made)
	{
		batch_ask(b, index, 0);
		goto by_me;
	}
	UNCACHED(&b->here_on[index])[0] = 1;
	__asm__ __volatile__("sync");
	if (!*me_on)
	{
		batch_ask(b, index, 2);
		b->taken++;
		batch_make_here(b, index);
		return 0;
	}
	batch_ask(b, index, 1);
	b->waits++;
	start = last = sceKernelGetSystemTimeLow();
	/* it ends made, or withdrawn (the ME saw this CPU's claim too) */
	while (!*made && *me_on)
	{
		if (!psp_me_ring_ready())
			break;
		now = sceKernelGetSystemTimeLow();
		gap = now - last;
		if (gap > batch_stats.gap_worst_us)
			batch_stats.gap_worst_us = gap;
		last = now;
		if (now - start <= BATCH_RECORD_DEADLINE_US)
			continue;
		if (!me_grace_off)
		{
			if (*made || !*me_on)
			{
				batch_stats.second_look++;
				if (deadline_lines < DEADLINE_LINES)
				{
					deadline_lines++;
					platform_log("ME: a batch record (%u) past %u ms by the clock (%u us), done at the look after it; this CPU was off its wait %u us just before",
						index, BATCH_RECORD_DEADLINE_US / 1000, now - start, gap);
				}
				continue;
			}
			if (!slow)
			{
				slow = 1;
				batch_stats.slow++;
				if (deadline_lines < DEADLINE_LINES)
				{
					deadline_lines++;
					platform_log("ME: a batch record (%u) still on the ME %u ms after the wait began; waiting to %u ms",
						index, (now - start) / 1000, BATCH_RECORD_GIVEUP_US / 1000);
					me_stash_dump("a batch record slow");
				}
			}
			if (now - start <= BATCH_RECORD_GIVEUP_US)
				continue;
		}
		batch_stats.late++;
		platform_log("ME: a batch record (%u) held past %u ms; the ME is stopped", index,
			(me_grace_off ? BATCH_RECORD_DEADLINE_US : BATCH_RECORD_GIVEUP_US) / 1000);
		me_stash_dump("a batch record late");
		psp_me_stop("a batch record late");
		break;
	}
	waited = sceKernelGetSystemTimeLow() - start;
	if (waited > batch_stats.wait_worst_us)
		batch_stats.wait_worst_us = waited;
	if (!*made)
	{
		b->taken++;
		batch_make_here(b, index);
		return 0;
	}
by_me:
	__asm__ __volatile__("sync");
	lines_drop((unsigned int)record, b->record_bytes);
	b->state[index] = BATCH_BY_ME;
	b->by_me++;
	return 1;
}

int psp_me_batch_need(struct psp_me_batch *b, unsigned int index)
{
	volatile unsigned int *started = &UNCACHED(&b->started)->value, *done = &UNCACHED(&b->done)->value;
	unsigned char *record = b->records + index * b->record_bytes;
	unsigned int start;

	if (index >= b->added)
		return 0;
	switch (b->state[index])
	{
	case BATCH_BY_ME:
		return 1;
	case BATCH_HERE:
		return 0;
	case BATCH_ADDED:
		/* never handed over: what was added before it goes now, if the
		ring takes it, and this one is made here */
		if (index >= b->handed)
		{
			batch_hand(b, index);
			/* this one, and any before it the ring refused, stay here */
			b->handed = index + 1;
		}
		batch_make_here(b, index);
		return 0;
	}
	if (b->claims)
		return batch_need_claim(b, index);
	if (index >= b->limit_now)
	{
		batch_make_here(b, index);
		return 0;
	}
	if (*done <= index)
	{
		/* not finished: taken back unless the ME has claimed it */
		b->limit_now = index;
		UNCACHED(&b->limit)->value = index;
		__asm__ __volatile__("sync");
		if (*started > index)
		{
			unsigned int now, last, gap, waited;
			int slow = 0;

			b->waits++;
			start = last = sceKernelGetSystemTimeLow();
			while (*done <= index && *started > index)
			{
				/* an ME stopped meanwhile: it halts once done, or is held in
				reset, and what it made is used only if it finished */
				if (!psp_me_ring_ready())
					break;
				/* the batch's last job ended: nothing on the ME will make
				this record, whatever started says */
				if (!me_grace_off && (int)(RING_POPPED - b->tickets[b->ticket_count - 1]) > 0)
				{
					if (*done <= index)
						batch_stats.stale++;
					break;
				}
				now = sceKernelGetSystemTimeLow();
				gap = now - last;
				if (gap > batch_stats.gap_worst_us)
					batch_stats.gap_worst_us = gap;
				last = now;
				if (now - start <= BATCH_RECORD_DEADLINE_US)
					continue;
				if (!me_grace_off)
				{
					/* the look before this clock may be from before a
					stretch this CPU spent on a thread above the game's: the
					record counts as held only if it still is */
					if (*done > index || *started <= index)
					{
						batch_stats.second_look++;
						if (deadline_lines < DEADLINE_LINES)
						{
							deadline_lines++;
							platform_log("ME: a batch record (%u) past %u ms by the clock (%u us), done at the look after it; this CPU was off its wait %u us just before",
								index, BATCH_RECORD_DEADLINE_US / 1000, now - start, gap);
						}
						continue;
					}
					if (!slow)
					{
						slow = 1;
						batch_stats.slow++;
						if (deadline_lines < DEADLINE_LINES)
						{
							deadline_lines++;
							platform_log("ME: a batch record (%u) still on the ME %u ms after the wait began; waiting to %u ms",
								index, (now - start) / 1000, BATCH_RECORD_GIVEUP_US / 1000);
							me_stash_dump("a batch record slow");
						}
					}
					if (now - start <= BATCH_RECORD_GIVEUP_US)
						continue;
				}
				batch_stats.late++;
				platform_log("ME: a batch record (%u) held past %u ms; the ME is stopped", index,
					(me_grace_off ? BATCH_RECORD_DEADLINE_US : BATCH_RECORD_GIVEUP_US) / 1000);
				me_stash_dump("a batch record late");
				psp_me_stop("a batch record late");
				break;
			}
			waited = sceKernelGetSystemTimeLow() - start;
			if (waited > batch_stats.wait_worst_us)
				batch_stats.wait_worst_us = waited;
		}
		if (*done <= index)
		{
			b->taken++;
			batch_make_here(b, index);
			return 0;
		}
	}
	/* the ME's answer, past the copy this CPU had from before the hand-over */
	__asm__ __volatile__("sync");
	lines_drop((unsigned int)record, b->record_bytes);
	b->state[index] = BATCH_BY_ME;
	b->by_me++;
	return 1;
}

void psp_me_batch_finish(struct psp_me_batch *b)
{
	unsigned int index, k, results[ME_RING_RESULTS];

	for (index = 0; index < b->added; index++)
		psp_me_batch_need(b, index);
	/* every record is made; the jobs only have their withdrawn claims left,
	and must be done before the arena or the words are used again */
	for (k = 0; k < b->ticket_count; k++)
		if (ring_next - b->tickets[k] - 1 < ME_RING_ENTRIES)
			psp_me_wait(b->tickets[k], results);
	b->ticket_count = 0;
	batch_stats.withdrawn += UNCACHED(&b->withdrawn)->value;
	batch_stats.passes++;
	batch_stats.by_me += b->by_me;
	batch_stats.by_main += b->by_main;
	batch_stats.taken += b->taken;
	batch_stats.waits += b->waits;
	batch_stats.refused += b->refused;
	b->by_me = b->by_main = b->taken = b->waits = b->refused = 0;
}

/* "mebatchtest": a batch's own check, once a frame. Each record's answer is
a function of its inputs, made by the ME or here, and checked here against
the same function. Records are needed in a scrambled order as they are
added, so every path runs: made by the ME, never handed over, taken back,
and (on the PSP) the ME on the record at that moment */
#define BATCHTEST_RECORDS 48
#define BATCHTEST_WORDS 16
static unsigned int batchtest_records[BATCHTEST_RECORDS][BATCHTEST_WORDS] __attribute__((aligned(64)));
static unsigned char batchtest_state[BATCHTEST_RECORDS];
static struct psp_me_batch batchtest_batch;
static int batchtest_on = -1;
static unsigned int batchtest_seq;
static unsigned long batchtest_checked, batchtest_wrong;

/* words 0-7 in, 8-15 out: a few hundred cycles of integer work, so the ME
is often still on a record when the main CPU asks for it */
static void batchtest_make(void *record, const unsigned int *args)
{
	unsigned int *w = (unsigned int *)record, h = args[0], k, j;

	for (k = 0; k < 8; k++)
	{
		h ^= w[k];
		for (j = 0; j < 24; j++)
			h = h * 2654435761u + (h >> 13) + j;
		w[8 + k] = h;
	}
}

void psp_me_batch_test(void)
{
	unsigned int args[ME_BATCH_ARGS] = { 0x9E3779B9u }, index, k, i, scramble;
	unsigned int expect[BATCHTEST_WORDS];
	unsigned int *w;

	if (PSP_ME_WORK())
		return;
	if (batchtest_on < 0)
	{
		batchtest_on = psp_keep("mebatchtest");
		if (batchtest_on && !psp_me_batch_init(&batchtest_batch, batchtest_make, batchtest_records, batchtest_state,
				sizeof(batchtest_records[0]), BATCHTEST_RECORDS, 6, args))
			batchtest_on = 0;
	}
	if (!batchtest_on)
		return;
	batchtest_seq++;
	psp_me_batch_begin(&batchtest_batch);
	scramble = batchtest_seq * 2246822519u;
	for (k = 0; k < BATCHTEST_RECORDS; k++)
	{
		w = (unsigned int *)psp_me_batch_add(&batchtest_batch, &index);
		if (!w)
			break;
		for (i = 0; i < 8; i++)
			w[i] = batchtest_seq * 0x01000193u ^ (index << 16) ^ i;
		/* now and then a record already added is needed, the last one
		(never handed over) among them */
		scramble = scramble * 1103515245u + 12345u;
		if (((scramble >> 16) & 7u) == 0)
			psp_me_batch_need(&batchtest_batch, (scramble >> 20) % (index + 1));
	}
	psp_me_batch_close(&batchtest_batch);
	for (k = 0; k < batchtest_batch.added; k++)
	{
		/* the order scrambled: a stride coprime with the count */
		index = (k * 7u + batchtest_seq) % batchtest_batch.added;
		psp_me_batch_need(&batchtest_batch, index);
		w = batchtest_records[index];
		for (i = 0; i < BATCHTEST_WORDS; i++)
			expect[i] = i < 8 ? (batchtest_seq * 0x01000193u ^ (index << 16) ^ i) : 0;
		batchtest_make(expect, args);
		for (i = 0; i < BATCHTEST_WORDS; i++)
			if (w[i] != expect[i])
			{
				batchtest_wrong++;
				break;
			}
		batchtest_checked++;
	}
	psp_me_batch_finish(&batchtest_batch);
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

/* ---------- the wake: the ME started again after a sleep ("mewake")

The kernel's sleep handler holds the ME in reset from the sleep on. Once the
wake is complete the power callback asks for a restart (psp_me_wake_request),
and the main thread carries it out at a frame's end (psp_me_wake_poll):
- that thread is the ring's one producer, and between frames it has no ring
  job half handed over, and the render slot's half has been waited for;
- the other slots' threads (audio, the heartbeat's probe) are waited for
  through slot_busy, frame end by frame end;
- me_state stays -1 until the ME answers, so until then every client takes
  its job back to the main CPU, as after the sleep.
The ME's code runs from this program in main RAM, which the PSP keeps
through a sleep, and its stack is in its own eDRAM, made anew at each start.
What the sleep does take is the ME's state: a sleep powers it down, and the
library's sleep handler has replaced the firmware's own, which on a wake
would turn the ME's bus clock back on and boot it with its caches cleared
(the firmware's me_wrapper, as uOFW reads it). The restart does those two
things itself, then the library's start (psp_me_wake_stub,
me_restart_kernel). The first try, without them, found no ME answering
within 100 ms on a PSP-1000 [M] */
#define WAKE_ANSWER_US 300000 /* the ME's first idle poll after the release, at most */

/* What the firmware's ME boot code does first (its me_wrapper's boot code),
before any read through a cache: both caches' tags cleared, so nothing in
them is taken for data or written back. A sleep powers the ME down, so its
caches wake holding noise, and meLibOnProcess's first act, a write-back of
the whole data cache, would write every noisy dirty line to wherever its tag
points. At launch the firmware's boot has already cleared them, which is why
the library's start handler can go without. The loops are the firmware's,
sized from the ME's own Config register. The library's handler is copied
straight after this, so the ME runs on into it; the two marks say how far
it got. Copied to the reset vector, never run in place */
__asm__(
	".section .text\n"
	".balign 4\n"
	".set push\n"
	".set noreorder\n"
	".set noat\n"
	".globl psp_me_wake_stub\n"
	"psp_me_wake_stub:\n"
	"	lui	$k0, 0xbfc0\n"
	"	lui	$k1, 0x57a9\n"
	"	ori	$k1, $k1, 1\n"
	"	sw	$k1, 0x6c0($k0)\n"
	"	sync\n"
	"	mtc0	$zero, $28\n"
	"	mtc0	$zero, $29\n"
	"	li	$t9, 2048\n"
	"	mfc0	$t0, $16\n"
	"	ext	$t0, $t0, 9, 3\n"
	"	sllv	$k1, $t9, $t0\n"
	"1:	addi	$k1, $k1, -64\n"
	"	bnez	$k1, 1b\n"
	"	cache	0x1, 0($k1)\n"
	"	mfc0	$t0, $16\n"
	"	ext	$t0, $t0, 6, 3\n"
	"	sllv	$k1, $t9, $t0\n"
	"2:	addi	$k1, $k1, -64\n"
	"	bnez	$k1, 2b\n"
	"	cache	0x11, 0($k1)\n"
	"	sync\n"
	"	lui	$k1, 0x57a9\n"
	"	ori	$k1, $k1, 2\n"
	"	sw	$k1, 0x6c0($k0)\n"
	"	sync\n"
	".globl psp_me_wake_stub_end\n"
	"psp_me_wake_stub_end:\n"
	".set pop\n"
	".previous\n");
extern const char psp_me_wake_stub[], psp_me_wake_stub_end[];

static volatile int wake_pending = 0;
static unsigned long long wake_mark;
static unsigned int wake_frames; /* frame ends spent waiting for the clients */
static char wake_line[240], wake_regs_line[480];
static volatile int wake_line_ready = 0;
/* the registers and marks around the restart's release */
static struct me_sysregs regs_release_before, regs_release_after, regs_answer;

/* for the heartbeat (psp_me_report), which says it once */
static void wake_report(const char *format, ...)
{
	va_list list;
	char before[96], after[96];

	va_start(list, format);
	vsnprintf(wake_line, sizeof(wake_line), format, list);
	va_end(list);
	wake_regs_line[0] = '\0';
	if (me_restartable == 1)
	{
		char launch[96], sleep[96], wake[96];

		sysregs_line(launch, sizeof(launch), "launch", &regs_launch);
		sysregs_line(sleep, sizeof(sleep), "sleep", &regs_sleep);
		sysregs_line(wake, sizeof(wake), "wake", &regs_wake);
		before[0] = after[0] = '\0';
		if (regs_answer.reset | regs_answer.bus_clock)
		{
			sysregs_line(before, sizeof(before), "; release", &regs_release_before);
			sysregs_line(after, sizeof(after), " ->", &regs_answer);
		}
		snprintf(wake_regs_line, sizeof(wake_regs_line),
			"ME: system registers (reset, bus clocks, clocks, AVC power) at %s; %s; %s%s%s", launch, sleep, wake, before, after);
	}
	wake_line_ready = 1;
}

/* in the kernel's wake handler, with mewakeearly: every client has handed
its job back at the sleep's stop, and the game's threads don't run during
the resume, so the words are ours */
static void wake_early_restart(void)
{
	if (!wake_early || me_restartable != 1)
		return;
	wake_prepare();
	wake_early_release = sceKernelGetSystemTimeWide();
	me_maybe_running = 1;
	me_restart_kernel(NULL);
	wake_released_early = 1;
}

/* the power callback, the wake complete */
void psp_me_wake_request(void)
{
	if (me_state == 0)
		return;
	wake_mark = sceKernelGetSystemTimeWide();
	wake_frames = 0;
	wake_pending = 1;
}

/* The release, in kernel mode: as the firmware boots the ME (its
me_wrapper), the ME's bus clock on and its reset held while its reset
vector is written; then the vector written with the cache clear first and
the library's start after it (meLibReset's copy, moved along by the stub's
length; the handler has no address of its own in it: one branch relative,
one jump to meLibOnPreProcess's absolute address); then the caches written
back and the reset's two bits let go */
static int me_restart_kernel(void *unused)
{
	unsigned int stub = (unsigned int)(psp_me_wake_stub_end - psp_me_wake_stub);
	unsigned int handler = (unsigned int)(&__stop__me_section - &__start__me_section);
	u32 interrupts;

	(void)unused;
	sysregs_read(&regs_release_before);
	reset_hold();
	meLibSuspendCpuIntr(interrupts);
	HW_SYS_BUS_CLOCK_ENABLE |= 1;
	meLibResumeCpuIntr(interrupts);
	hw(ME_MARK_START) = 0;
	hw(ME_MARK_PROCESS) = 0;
	hw(ME_MARK_PROBE) = 0;
	hw(ME_MARK_PROBE_FAIL) = 0;
	memcpy((void *)ME_HANDLER_BASE, psp_me_wake_stub, stub);
	memcpy((void *)(ME_HANDLER_BASE + stub), &__start__me_section, handler);
	sceKernelDcacheWritebackInvalidateAll();
	sceKernelIcacheInvalidateAll();
	reset_release();
	if (wake_fw)
	{
		/* only the blocks launch found running, a bit at a time as the
		firmware's driver does */
		unsigned int held = HW_SYS_RESET_ENABLE & WAKE_FW_RESETS & ~regs_launch.reset;

		if (held)
		{
			meLibSuspendCpuIntr(interrupts);
			HW_SYS_RESET_ENABLE &= ~held;
			meLibResumeCpuIntr(interrupts);
		}
	}
	sysregs_read(&regs_release_after);
	return 0;
}

/* how far the ME's start got, from its marks */
static const char *wake_stage(const struct me_sysregs *regs)
{
	static const char *const probe_steps[] = { "", "the FPU", "a cached read of main RAM",
		"a cached write of main RAM, written back", "the eDRAM through the cache", "24 KB of stack" };
	static char probe_line[120];

	if (regs->marks[1] == ME_MARK_PROCESS_RUN && regs->marks[2] > ME_MARK_PROBE_BASE &&
		regs->marks[2] <= ME_MARK_PROBE_BASE + 5)
	{
		snprintf(probe_line, sizeof(probe_line), "into its loop's probe, stopped in step %u: %s",
			regs->marks[2] - ME_MARK_PROBE_BASE, probe_steps[regs->marks[2] - ME_MARK_PROBE_BASE]);
		return probe_line;
	}
	if (regs->marks[1] == ME_MARK_PROCESS_RUN)
		return "into its loop (past its first cache write-back)";
	if (regs->marks[1] == ME_MARK_PROCESS_IN)
		return "into the game's ME code, not past its first cache write-back";
	if (regs->marks[0] == ME_MARK_STUB_DONE)
		return "past the cache clear, not into the game's ME code (the library's start handler: its VME wait, or the jump)";
	if (regs->marks[0] == ME_MARK_STUB_IN)
		return "into the cache clear, not out of it";
	return "nowhere: not its first instruction (power, clock or reset)";
}

void psp_me_wake_poll(void)
{
	volatile struct me_control *control = CONTROL;
	unsigned long long release, now;
	unsigned int sleeps;
	int k;

	if (ring_dead == 2)
		ring_revive();
	if (!wake_pending)
		return;
	if (me_state > 0)
	{
		/* no sleep reached the ME: nothing to start */
		wake_pending = 0;
		return;
	}
	if (!psp_keep("mewake") || psp_keep("mewakeoff") || !me_restartable)
	{
		wake_pending = 0;
		wake_report("ME: stays stopped after the wake (%s); the main CPU runs its jobs",
			!me_restartable ? "it was stopped for a failure before the sleep" :
			psp_keep("mewake") ? "mewakeoff" : "the restart is opt-in: mewake");
		return;
	}
	for (k = 0; k < ME_SLOTS; k++)
		if (slot_busy[k] || me_mark(k))
		{
			wake_frames++;
			return;
		}
	wake_pending = 0;
	sleeps = me_sleeps;
	if (wake_released_early)
	{
		/* released in the kernel's wake handler already */
		wake_released_early = 0;
		release = wake_early_release;
		now = sceKernelGetSystemTimeWide();
		while (control->alive == 0 && sceKernelGetSystemTimeWide() - now < WAKE_ANSWER_US)
			sceKernelDelayThread(500);
		now = sceKernelGetSystemTimeWide();
		goto answered;
	}
	wake_prepare();
	now = sceKernelGetSystemTimeWide();
	if (me_restartable == 2)
	{
		me_state = 2;
		wake_report("ME (simulated): restarted after wake in %lu ms (%u frame ends waiting for its clients)",
			(unsigned long)((now - wake_mark) / 1000), wake_frames);
		return;
	}
	release = now;
	me_maybe_running = 1;
	control->cpu_seen = 0;
	/* B41: a release that never returns (the ME's bus unpowered) is named
	by the heartbeat's stall reporter */
	PSP_WAIT_BEGIN(PSP_WAIT_ME_RESTART);
	kcall_3(me_restart_kernel, me_kseg, 0);
	PSP_WAIT_END(PSP_WAIT_ME_RESTART);
	while (control->alive == 0 && (now = sceKernelGetSystemTimeWide()) - release < WAKE_ANSWER_US)
		sceKernelDelayThread(500);
answered:
	kcall_3(me_sysregs_kernel, me_kseg, &regs_answer);
	if (control->alive == 0 || me_sleeps != sleeps)
	{
		kcall_2(me_hold_reset, me_kseg);
		me_maybe_running = 0;
		wake_report("ME: restart after the wake FAILED (%s; the ME's start got %s); held in reset, the main CPU runs its jobs",
			control->alive == 0 ? "no idle poll within 300 ms of the reset's release" : "the PSP slept again meanwhile",
			wake_stage(&regs_answer));
		return;
	}
	me_state = 1;
	me_cpu_check(control, "restart");
	wake_report("ME: restarted after wake in %lu ms (its first idle poll %lu us after the reset's release%s; %u frame ends waiting for its clients%s)",
		(unsigned long)((now - wake_mark) / 1000), (unsigned long)(now - release),
		release < wake_mark ? ", released in the kernel's wake handler" : "", wake_frames,
		!(wake_features & ME_FEATURE_WAKE_PROBE) ? "" :
		regs_answer.marks[2] != ME_MARK_PROBE_BASE + 0xf ? "; the probe didn't finish" :
		regs_answer.marks[3] ? "; the probe finished, some steps read back wrong (marks)" : "; the probe's 5 steps all right");
}

/* every word the ME reads at its start, as at launch; the slots and the
ring carried on from where the main CPU's counters are, so a ticket or
request number handed out before the sleep can never be taken for one of
the new ME's. The features before the release, where launch sets them after
its start check: the ME reads them on its first pass, so the ring is served
from the first frame (a ring job not done in 10 ms turns the ring off for
the session). With mewakeprobe, the probe's input pattern too */
static void wake_prepare(void)
{
	volatile struct me_control *control = CONTROL;
	int k;

	control->stop = control->alive = control->clock = control->jobs = 0;
	control->halted = 0;
	for (k = 0; k < ME_SLOTS; k++)
		SLOT(k)->request = SLOT(k)->done = slot_request[k];
	ring_epoch = ring_next;
	ring_restarted = 1;
	ring_popped_seen = ring_next;
	RING_PUSHED = RING_POPPED = ring_next;
	control->features = wake_features;
	if (wake_features & ME_FEATURE_WAKE_PROBE)
		for (k = 0; k < 1024; k++)
			wake_probe_in[k] = WAKE_PROBE_WORD(k);
	__asm__ __volatile__("sync");
}

static int me_sleep_kernel(void)
{
	meLibOnSleep();
	return 0;
}

/* "mewaketest": what a sleep does to the ME, without one, so the restart
can be run where the PSP can't sleep (the emulator) and on the PSP without
a hand on the power switch. The wake (psp_me_wake_request) is the caller's */
void psp_me_wake_test_sleep(void)
{
	if (me_state <= 0)
	{
		platform_log("mewaketest: the ME isn't running; nothing to test");
		return;
	}
	psp_me_sleep_stop();
	if (me_restartable == 1)
		kcall_2(me_sleep_kernel, me_kseg);
	else
	{
		me_state = -1;
		me_sleeps++;
	}
	platform_log("mewaketest: the ME stopped and held as a sleep holds it");
}

static int me_wake_kernel(void)
{
	meLibOnWake();
	return 0;
}

/* mewaketest's wake: the kernel's wake handler as a real wake runs it (with
mewakeearly, the release is there), then the power callback's request */
void psp_me_wake_test_wake(void)
{
	if (me_restartable == 1)
		kcall_2(me_wake_kernel, me_kseg);
	psp_me_wake_request();
}

/* build 87: the last job's own code time on the ME, in microseconds (its
cycle counter, without the cache hand-overs); simulated, the main CPU's time
for the whole job */

unsigned int psp_me_last_job_us(int which)
{
	return which >= 0 && which < ME_SLOTS ? last_job_us[which] : 0;
}

/* the ME's cycle counter's counts a microsecond, measured at start (0: not
measured, so ME times can't be given) */
float psp_me_cycles_per_us(void)
{
	return me_cycles_per_us;
}

#if PSP_MUSIC
/* the next audio job decodes the block's music too (psp_dsound.c) */
static unsigned int audio_music;

void psp_me_audio_music(int on)
{
	audio_music = on != 0;
}
#endif

/* the audio job's list: up to 256 voices, written through the uncached view */
/* where the ME's audio time went (its own counter, summed over the blocks
it mixed): the 'ME: audio split' line takes the job apart into decoding,
the voices' mixing loop and the sums' ends; the job's code less these is
its entry and the voice list */
static struct
{
	unsigned long long decode, voices, ends, voice_frames;
	unsigned long blocks;
} audio_split;

/* the next audio job's 44.1 kHz work (psp_dsound_full_job): the filter
into out44, and with full the full-rate voices (their list after the
block's own in AUDIO_LIST); set by the mixer before each job, out44 NULL for none */
static void *const *audio_full_list = NULL;
static unsigned long audio_full_count = 0, audio_full_heard = 0;
static short *audio_out44 = NULL, *audio_full = NULL;
static int audio_full_done = 0;

void psp_me_audio_full(void *const *list, unsigned long count, short *out44, short *full)
{
	audio_full_list = list;
	audio_full_count = count;
	audio_out44 = out44;
	audio_full = full;
}

/* 1 when the last job made out44 whole; heard: the full-rate voices heard */
int psp_me_audio_full_done(unsigned long *heard)
{
	*heard = audio_full_heard;
	return audio_full_done;
}

int psp_me_audio(void *const *list, unsigned long count, short *output, unsigned long frames, unsigned long *voices,
	unsigned long *clipped, unsigned long *waited_us)
{
	unsigned int args[7], results[ME_JOB_RESULTS];
	unsigned long long start = sceKernelGetSystemTimeWide();
	unsigned long k, full_count = audio_out44 ? audio_full_count : 0;

	audio_full_done = 0;
	audio_full_heard = 0;
	if (me_state <= 0 || count + full_count > AUDIO_LIST_MAX || frames > MIX_FRAMES_MAX)
		return 0;
	for (k = 0; k < count; k++)
		AUDIO_LIST[k] = list[k];
	for (k = 0; k < full_count; k++)
		AUDIO_LIST[count + k] = audio_full_list[k];
	args[0] = (unsigned int)count;
	args[1] = (unsigned int)frames;
	args[2] = (unsigned int)output;
#if PSP_MUSIC
	args[3] = audio_music;
#else
	args[3] = 0;
#endif
	args[4] = (unsigned int)full_count;
	args[5] = (unsigned int)audio_out44;
	args[6] = (unsigned int)audio_full;
	/* about 0.1-1 ms of work: a first look after 0.2 ms, or mepoll's
	(audio_look_next) */
	if (!psp_me_run(ME_SLOT_AUDIO, ME_JOB_AUDIO, args, 7, results, audio_first_look_us))
		return 0;
	if (audio_out44)
	{
		audio_full_heard = results[6];
		audio_full_done = results[7] != 0;
	}
	*voices = results[0];
	*clipped = results[1];
	if (results[3] || results[5])
	{
		audio_split.decode += results[2];
		audio_split.voices += results[3];
		audio_split.ends += results[4];
		audio_split.voice_frames += results[5];
		audio_split.blocks++;
	}
	*waited_us = (unsigned long)(sceKernelGetSystemTimeWide() - start);
	if (me_state == 1)
		audio_look_next(*waited_us);
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
/* Three stretches of the PSP-1000's 2 MB, one 64 KB piece a heartbeat each
pass (~30 s a pass):
- 0 to 192 KB, hashed, never written: whatever lies there is left as found,
  and a hash that moves says something else writes it;
- 256 KB to 1.5 MB, patterned: the room the ME's jobs and the sound cache
  (R2) would take; any change is something else writing eDRAM;
- 1.5 MB to 1,984 KB, patterned below the ME's stack: the stack grows down
  from 2 MB, and the lowest word changed is how deep it has reached, the
  room R2 must leave it. A change here is the ME's own, not an alarm, on a
  PSP-1000; a slim's 4 MB put its stack at 4 MB (the job says where its own
  stack is), so there this stretch is patterned room like the one before.
192-256 KB is left out: mebusedram's scratch is there */
#define EDRAM_PROBE_FIRST 0x40000u
#define EDRAM_PROBE_END 0x180000u
#define EDRAM_PROBE_PIECE 0x10000u
#define EDRAM_HASH_END 0x30000u
#define EDRAM_STACK_END 0x1F0000u
#define EDRAM_HASH_PIECES (EDRAM_HASH_END / EDRAM_PROBE_PIECE)
#define EDRAM_PIECES (EDRAM_HASH_PIECES + (EDRAM_STACK_END - EDRAM_PROBE_FIRST) / EDRAM_PROBE_PIECE)

/* the piece's first byte, k counting the hashed pieces first */
static unsigned int edram_piece(unsigned int k)
{
	return k < EDRAM_HASH_PIECES ? k * EDRAM_PROBE_PIECE : EDRAM_PROBE_FIRST + (k - EDRAM_HASH_PIECES) * EDRAM_PROBE_PIECE;
}

void psp_me_edram_probe(void)
{
	static int asked = -1;
	static unsigned int prepared = 0, next = 0, checked = 0, bad_pieces = 0, hash_moved = 0, passes = 0;
	static unsigned int stack_lowest = 0xFFFFFFFFu, hashes[EDRAM_HASH_PIECES];
	unsigned int args[3], results[ME_JOB_RESULTS] = { 0 }, first;

	if (asked < 0)
		asked = psp_keep("edramcheck") != 0;
	if (!asked || me_state != 1)
		return;
	if (prepared < EDRAM_PIECES)
	{
		/* the first pass: the hashes taken, the pattern written */
		first = edram_piece(prepared);
		args[0] = prepared < EDRAM_HASH_PIECES ? 2 : 0;
		args[1] = first;
		args[2] = EDRAM_PROBE_PIECE;
		if (psp_me_run(ME_SLOT_PROBE, ME_JOB_EDRAM, args, 3, results, 2000) && results[0] != 0xFFFFFFFFu)
		{
			if (prepared < EDRAM_HASH_PIECES)
				hashes[prepared] = results[0];
			if (++prepared == EDRAM_PIECES)
				platform_log("ME: edramcheck: eDRAM 0-%u KB hashed, %u-%u KB patterned (the ME's stack's reach from %u KB), %u KB a heartbeat",
					EDRAM_HASH_END / 1024, EDRAM_PROBE_FIRST / 1024, EDRAM_STACK_END / 1024, EDRAM_PROBE_END / 1024,
					EDRAM_PROBE_PIECE / 1024);
		}
		return;
	}
	first = edram_piece(next);
	args[0] = next < EDRAM_HASH_PIECES ? 2 : 1;
	args[1] = first;
	args[2] = EDRAM_PROBE_PIECE;
	if (!psp_me_run(ME_SLOT_PROBE, ME_JOB_EDRAM, args, 3, results, 2000) || results[0] == 0xFFFFFFFFu)
		return;
	checked++;
	if (next < EDRAM_HASH_PIECES)
	{
		if (results[0] != hashes[next])
		{
			hash_moved++;
			platform_log("ME: edramcheck: eDRAM %u to %u KB changed (hash %08x, was %08x): something writes the bottom of eDRAM",
				first / 1024, (first + EDRAM_PROBE_PIECE) / 1024, results[0], hashes[next]);
			hashes[next] = results[0];
		}
	}
	else if (first >= EDRAM_PROBE_END && results[3] < 0x200000u)
	{
		if (results[0] && results[1] < stack_lowest)
		{
			stack_lowest = results[1];
			platform_log("ME: edramcheck: the ME's stack has reached down to %u KB (%u KB deep)", stack_lowest / 1024,
				(0x200000u - stack_lowest) / 1024);
		}
	}
	else if (results[0])
	{
		bad_pieces++;
		platform_log("ME: edramcheck: %u words changed in eDRAM %u to %u KB, the first at %08x (now %08x)",
			results[0], first / 1024, (first + EDRAM_PROBE_PIECE) / 1024, results[1], results[2]);
	}
	if (++next == EDRAM_PIECES)
	{
		next = 0;
		if ((++passes % 4) == 0)
			platform_log("ME: edramcheck: %u passes, %u pieces of %u KB checked; %u patterned pieces changed, %u hashes moved; the stack's deepest %u KB",
				passes, checked, EDRAM_PROBE_PIECE / 1024, bad_pieces, hash_moved,
				stack_lowest == 0xFFFFFFFFu ? 0 : (0x200000u - stack_lowest) / 1024);
	}
}

/* the longest line platform_log writes whole (its buffer, psp_fixups.py),
and the continued lines' length under it */
#define ME_LOG_LINE 510
#define ME_LOG_CONTINUED 500

void psp_me_report(void)
{
#if PSP_MUSIC
	static const char *const names[ME_SLOTS] = { "audio", "render", "probe", "ai", "music" };
#else
	static const char *const names[ME_SLOTS] = { "audio", "render", "probe", "ai" };
#endif
	unsigned long long now = sceKernelGetSystemTimeWide(), window = now - report_mark, busy_us_all = 0;
	char line[1400], ring_line[640];
	int length = 0, k, parts = 0, part_at[ME_SLOTS + 1];

	if (!me_state)
		return;
#if PSP_MEXEC
	{
		static unsigned int mexec_seen = 0;
		volatile struct me_control *control = CONTROL;

		/* each install once (a restart after a sleep installs again) */
		if (control->mexec_state && control->mexec_sum + control->mexec_state != mexec_seen)
		{
			mexec_seen = control->mexec_sum + control->mexec_state;
			if (control->mexec_state == 1)
				platform_log("ME: mexec in place: %u bytes copied, %u words patched, sum %08x",
					control->mexec_bytes, control->mexec_patches, control->mexec_sum);
			else
				platform_log("ME: mexec: the ME found no table and runs from main RAM");
		}
	}
#endif
	if (wake_line_ready)
	{
		wake_line_ready = 0;
		platform_log("%s", wake_line);
		if (wake_regs_line[0])
			platform_log("%s", wake_regs_line);
	}
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
		part_at[parts++] = length;
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
	if (audio_split.blocks && me_cycles_per_us > 0.0f)
	{
		float per_block = me_cycles_per_us * (float)audio_split.blocks;

		platform_log("ME: audio split over %lu blocks: decoding %lu us a block, the voices' mixing %lu (decoding included; %lu cycles a voice-frame, %lu without decoding), the sums' zeroing and output %lu",
			audio_split.blocks, (unsigned long)((float)audio_split.decode / per_block),
			(unsigned long)((float)audio_split.voices / per_block),
			audio_split.voice_frames ? (unsigned long)(audio_split.voices / audio_split.voice_frames) : 0,
			audio_split.voice_frames ? (unsigned long)((audio_split.voices - audio_split.decode) / audio_split.voice_frames) : 0,
			(unsigned long)((float)audio_split.ends / per_block));
		memset(&audio_split, 0, sizeof(audio_split));
	}
	if (ring_slot_done)
	{
		platform_log("ME: ringslotdone: %lu pushes passed a finished slot job not yet collected", ring_slot_passed);
		ring_slot_passed = 0;
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
		{
			int type, at = (int)strlen(ring_line);
			const char *lead = "; by type:";

			for (type = 0; type < 16 && at < (int)sizeof(ring_line) - 48; type++)
				if (ring_stats.type_jobs[type])
				{
					at += snprintf(ring_line + at, sizeof(ring_line) - at, "%s %d: %lu, longest %lu us", lead, type,
						ring_stats.type_jobs[type], (unsigned long)((float)ring_stats.type_worst_cycles[type] / rate));
					lead = ";";
				}
		}
#if PSP_RECON
		psp_me_waitwho_report(rate);
#endif
		memset(&ring_stats, 0, sizeof(ring_stats));
	}
	/* the jobs collected by polling (mevisring): their own time, which a
	drain inside the AI job also counts as nested work, taken off below */
	busy_us_all += ring_polled_report();
	/* the yields' nested work, counted in both the slot job's time and its
	own (nested_cycles_total), taken off once; "mebusyold" keeps the old sum */
	{
		static unsigned int nested_seen = 0;
		static int old_sum = -1;
		unsigned int nested_now = *(volatile unsigned int *)((unsigned int)&nested_cycles_total | 0x40000000u);
		unsigned long long nested_us = me_cycles_per_us > 0.0f ? (unsigned long long)((float)(nested_now - nested_seen) / me_cycles_per_us) : 0;

		if (old_sum < 0)
			old_sum = psp_keep("mebusyold") != 0;
		nested_seen = nested_now;
		if (!old_sum && me_state != 2)
			busy_us_all = busy_us_all > nested_us ? busy_us_all - nested_us : 0;
		nested_us_last = nested_us;
	}
	/* the replay's own time: its job less the cycles its looks served
	(audio, the ring), the figure the tick schedule's slices are judged by,
	counted the same way with ticksched off */
	if (me_state == 1 && stats[ME_SLOT_RENDER].jobs && me_cycles_per_us > 0.0f)
	{
		static struct tick_look_stats seen;
		volatile struct tick_look_stats *t = UNCACHED(&tick_look);
		struct tick_look_stats d;
		unsigned long jobs = stats[ME_SLOT_RENDER].jobs;
		float rate = me_cycles_per_us;
		unsigned long long job_cycles = stats[ME_SLOT_RENDER].me_job_cycles;

		d.served_cycles = t->served_cycles - seen.served_cycles;
		d.looks = t->looks - seen.looks;
		d.pickups = t->pickups - seen.pickups;
		d.ring_jobs = t->ring_jobs - seen.ring_jobs;
		d.held = t->held - seen.held;
		d.urgent = t->urgent - seen.urgent;
		d.batch_first = t->batch_first - seen.batch_first;
		d.audio_inside = t->audio_inside - seen.audio_inside;
		d.audio_held = t->audio_held - seen.audio_held;
		seen.audio_inside += d.audio_inside;
		seen.audio_held += d.audio_held;
		seen.served_cycles += d.served_cycles;
		seen.looks += d.looks;
		seen.pickups += d.pickups;
		seen.ring_jobs += d.ring_jobs;
		seen.held += d.held;
		seen.urgent += d.urgent;
		seen.batch_first += d.batch_first;
		platform_log("ME: replay looks: %lu jobs, the job %lu us, its own %lu us (served inside it %lu us: audio and the ring); %u looks, %u pick-ups running %u ring jobs, %u held to the slice, %u early for a wait, %u batch jobs made first; audio mixed at its looks %u (audiohold's near-dry %u)%s",
			jobs, (unsigned long)((float)job_cycles / rate / (float)jobs),
			(unsigned long)((float)(job_cycles > d.served_cycles ? job_cycles - d.served_cycles : 0) / rate / (float)jobs),
			(unsigned long)((float)d.served_cycles / rate / (float)jobs), d.looks, d.pickups, d.ring_jobs, d.held, d.urgent,
			d.batch_first, d.audio_inside, d.audio_held, (CONTROL->features & ME_FEATURE_TICKSCHED) ? " (ticksched)" : "");
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

		part_at[parts++] = length;
		length += snprintf(line + length, sizeof(line) - length,
			"; mehalt: %u halts, %lu wakes sent (a kernel call, %lu us each), %u taken",
			halts - halts_last, wakes_sent, wakes_timed ? (unsigned long)(wake_us / wakes_timed) : 0, wakes - wakes_last);
		halts_last = halts;
		wakes_last = wakes;
		wakes_sent = 0;
	}
	if (length >= (int)sizeof(line))
		length = (int)sizeof(line) - 1;
	{
		/* platform_log keeps ME_LOG_LINE characters of a line, and four
		slots' figures are longer: the line stays as the readers know it (cut
		where the log cuts it), and each slot it doesn't hold whole follows,
		whole, on "ME, continued" lines (a slot cut on the first line is
		repeated there). "meslotsold" leaves out the continued lines */
		static int one_line = -1;
		char out[ME_LOG_LINE + 1];
		int at, p;

		if (one_line < 0)
			one_line = psp_keep("meslotsold") != 0;
		at = snprintf(out, sizeof(out), "ME: %s, %.1f%% busy over %.1f s (work nested in a job's yields, counted once: %lu us)",
			me_state == 1 ? "running" : me_state == 2 ? "SIMULATED (emulator)" : "stopped", busy_percent_last,
			(double)window / 1e6, (unsigned long)nested_us_last);
		platform_log("%s%s", out, length ? line : "; no jobs");
		if (!one_line && length)
		{
			int kept = ME_LOG_LINE - at, more = 0;

			for (p = 0; p < parts; p++)
			{
				int end = p + 1 < parts ? part_at[p + 1] : length, size = end - part_at[p];

				if (end <= kept)
					continue;
				if (more && at + size > ME_LOG_CONTINUED)
				{
					platform_log("%s", out);
					more = 0;
				}
				if (!more)
				{
					at = snprintf(out, sizeof(out), "ME, continued");
					more = 1;
				}
				if (size > ME_LOG_CONTINUED - at)
					size = ME_LOG_CONTINUED - at;
				memcpy(out + at, line + part_at[p], size);
				at += size;
				out[at] = 0;
			}
			if (more)
				platform_log("%s", out);
		}
	}
	if (ring_line[0])
		platform_log("%s", ring_line);
	{
		/* the AI job's drains since the start: the longest ring job in one, and
		the visibility jobs run in them or left to the ME's loop */
		unsigned int worst = *UNCACHED(&aime_drain_worst), run = *UNCACHED(&aime_drain_vis_run),
			left = *UNCACHED(&aime_drain_vis_left);

		static unsigned int said[3];

		if ((worst || run || left) && (worst != said[0] || run != said[1] || left != said[2]))
			platform_log("ME: the AI job's drains: the longest ring job %lu us (type %u); visibility jobs run in them %u, left to the ME's loop %u (aimevisleft %s)",
				me_cycles_per_us > 0.0f ? (unsigned long)((float)worst / me_cycles_per_us) : 0,
				*UNCACHED(&aime_drain_worst_type), run, left,
				(CONTROL->features & ME_FEATURE_AIME_VIS_LEFT) ? "on" : "off");
		said[0] = worst;
		said[1] = run;
		said[2] = left;
	}
	{
		/* aimeringlook's yields inside an actor's thinking since the last line */
		unsigned int yields = INNER_STATS->yields, jobs = INNER_STATS->ring_jobs, low = INNER_STATS->low;

		if (yields != inner_seen[0] || low != inner_seen[2])
			platform_log("ME: inside the AI job's actors (aimeringlook): %u yields, %u ring jobs run in them, %u looks passed by for the stack",
				yields - inner_seen[0], jobs - inner_seen[1], low - inner_seen[2]);
		inner_seen[0] = yields;
		inner_seen[1] = jobs;
		inner_seen[2] = low;
		/* lightringlook's looks inside the light pass */
		yields = INNER_STATS->light_looks;
		jobs = INNER_STATS->light_jobs;
		low = INNER_STATS->light_low;
		if (yields != inner_seen[3] || low != inner_seen[5])
			platform_log("ME: inside the light pass (lightringlook): %u looks, %u ring jobs run in them, %u looks passed by for the stack",
				yields - inner_seen[3], jobs - inner_seen[4], low - inner_seen[5]);
		inner_seen[3] = yields;
		inner_seen[4] = jobs;
		inner_seen[5] = low;
		/* aimeaudiolook's mixes at those looks */
		yields = INNER_STATS->audio;
		jobs = INNER_STATS->light_audio;
		if (yields != inner_seen[6] || jobs != inner_seen[7])
			platform_log("ME: audio blocks mixed at long jobs' looks, near their bound (aimeaudiolook): %u in the AI job's actors, %u in the light pass",
				yields - inner_seen[6], jobs - inner_seen[7]);
		inner_seen[6] = yields;
		inner_seen[7] = jobs;
	}
	if (me_state == 1 && stats[ME_SLOT_AUDIO].jobs)
	{
		/* how often the audio thread woke to look (mepoll's measure; the
		threads line's wakes count its output's returns too) */
		unsigned long jobs = stats[ME_SLOT_AUDIO].jobs, tenths = stats[ME_SLOT_AUDIO].looks * 10 / jobs;

		platform_log("ME: audio looks: %lu.%lu a job after the first, the first at %lu us on average (%s)", tenths / 10,
			tenths % 10, stats[ME_SLOT_AUDIO].first_look_us / jobs,
			audio_look_adaptive ? "mepoll: adaptive" : audio_first_look_us != AUDIO_LOOK_DEFAULT_US ? "mepoll: fixed" : "fixed");
	}
	if (me_state == 1 && (CONTROL->features & ME_FEATURE_STASH))
	{
		/* the ME's own counts since start (the stash) */
		volatile unsigned int *stash = STASH;

		platform_log("ME: since start: %u audio blocks mixed before or inside a ring job, %u of all its audio blocks handed over by ranges; ring jobs %u begun, %u ended; slot jobs %u begun, %u ended",
			stash[STASH_AUDIO_INSIDE], stash[STASH_AUDIO_RANGES], stash[STASH_RING_BEGUN], stash[STASH_RING_ENDED],
			stash[STASH_SLOT_BEGUN], stash[STASH_SLOT_ENDED]);
	}
	if (batch_stats.passes)
	{
		platform_log("ME: batches: %lu passes; records made by the ME %lu, here %lu (taken back %lu; jobs the ME ended at the limit, or with claims the records it passed, claimed here, %lu), waits for a record the ME was on %lu (longest %lu us, longest between two looks %lu us), hand-overs refused %lu; stale claims taken back %lu; past %u ms: done at a second look %lu, slow %lu, late %lu%s",
			batch_stats.passes, batch_stats.by_me, batch_stats.by_main, batch_stats.taken, batch_stats.withdrawn, batch_stats.waits,
			batch_stats.wait_worst_us, batch_stats.gap_worst_us, batch_stats.refused, batch_stats.stale, BATCH_RECORD_DEADLINE_US / 1000,
			batch_stats.second_look, batch_stats.slow, batch_stats.late, batch_sim_half ? " (mebatchsimhalf: the simulated ME makes half of each job)" : "");
		if (batch_stats.ask_n[0] + batch_stats.ask_n[1] + batch_stats.ask_n[2])
		{
			float rate = me_cycles_per_us > 0.0f ? me_cycles_per_us : 1.0f;
			unsigned long n0 = batch_stats.ask_n[0] ? batch_stats.ask_n[0] : 1, n1 = batch_stats.ask_n[1] ? batch_stats.ask_n[1] : 1;
			unsigned long n2 = batch_stats.ask_n[2] ? batch_stats.ask_n[2] : 1, jobs = batch_ring.jobs ? batch_ring.jobs : 1;

			platform_log("ME: batch timing (claims): from hand-over to the gather's ask, made by then %lu (%lu us on average, longest %lu), the ME on it %lu (%lu, %lu), not begun %lu (%lu, %lu); passes whose first ask found the ME begun %lu of %lu; the batches' jobs %lu, picked up within %lu us a job (worst %lu), their own time %lu us (worst %lu)%s",
				batch_stats.ask_n[0], (unsigned long)(batch_stats.ask_us[0] / n0), batch_stats.ask_worst[0],
				batch_stats.ask_n[1], (unsigned long)(batch_stats.ask_us[1] / n1), batch_stats.ask_worst[1],
				batch_stats.ask_n[2], (unsigned long)(batch_stats.ask_us[2] / n2), batch_stats.ask_worst[2],
				batch_stats.passes_begun, batch_stats.passes, batch_ring.jobs,
				(unsigned long)((float)batch_ring.look_cycles / rate / (float)jobs), (unsigned long)((float)batch_ring.look_worst / rate),
				(unsigned long)((float)batch_ring.job_cycles / rate / (float)jobs), (unsigned long)((float)batch_ring.job_worst / rate),
				me_state == 2 ? " (simulated: the jobs run inside the push)" : "");
		}
		memset(&batch_stats, 0, sizeof(batch_stats));
		memset(&batch_ring, 0, sizeof(batch_ring));
	}
	if (batchtest_checked)
	{
		platform_log("ME: batchtest: %lu records checked, %lu wrong", batchtest_checked, batchtest_wrong);
		batchtest_checked = batchtest_wrong = 0;
	}
	if (ringtest_bursts)
	{
		platform_log("ME: ringtest: %lu bursts, %lu jobs checked, %lu wrong (words or sums), %lu bursts short of a full ring (a push refused), %lu jobs failed",
			ringtest_bursts, ringtest_jobs, ringtest_wrong, ringtest_short, ringtest_failed);
		ringtest_bursts = ringtest_jobs = ringtest_wrong = ringtest_short = ringtest_failed = 0;
	}
	memset(stats, 0, sizeof(stats));
}

#ifdef PSP_RECON
/* "mebusN"'s main CPU side (the load itself: bus_load_job). Each tick is
loaded or not by a coin (not alternately: with two ticks a frame, the first
and second tick of a frame differ), so the loaded and the unloaded ticks
sample the same play. Every 300 ticks the ME's load is also run alone for
8 ms, with the main CPU asleep, for its rate without the main CPU. The log
line: the main CPU's tick with the ME loading against without, and the ME's
loads a microsecond while the tick ran against alone */
#define BUS_LIMIT_US 200000u
static int bus_work = -2, bus_edram = 0;
static unsigned int bus_ticket, bus_coin = 0x2545F491u, bus_tick_start = 0, bus_loaded = 0;
static unsigned long bus_ticks[2], bus_tick_us[2], bus_preempted;
static unsigned long bus_loads, bus_load_cycles, bus_alone_loads[2], bus_alone_cycles[2];

static int bus_start(int edram)
{
	struct psp_me_job job;

	memset(&job, 0, sizeof(job));
	job.type = ME_JOB_BUSLOAD;
	job.args[0] = (unsigned int)bus_work;
	job.args[1] = (unsigned int)(BUS_LIMIT_US * (me_cycles_per_us > 0.0f ? me_cycles_per_us : 333.0f));
	job.args[2] = bus_coin;
	job.args[3] = (unsigned int)edram;
	job.args[4] = (unsigned int)bus_out;
	job.output = bus_out;
	job.output_bytes = BUS_OUT_WORDS * sizeof(*bus_out);
	BUS_STOP = 0;
	if (!psp_me_push(&job, &bus_ticket))
		return 0;
	bus_running = 1;
	return 1;
}

/* the stop raised, then the job's end awaited here before psp_me_wait, which
would take a job pushed a tick ago for one past its deadline */
static void bus_finish(unsigned long *loads, unsigned long *cycles)
{
	unsigned int results[ME_RING_RESULTS], start = sceKernelGetSystemTimeLow(), k;

	bus_running = 0;
	BUS_STOP = 1;
	while ((int)(RING_POPPED - bus_ticket) <= 0 && sceKernelGetSystemTimeLow() - start < BUS_LIMIT_US)
		for (k = 0; k < RING_WAIT_NOPS; k++)
			__asm__ __volatile__("nop");
	if (psp_me_wait(bus_ticket, results))
	{
		*loads += results[0];
		*cycles += results[1];
	}
}

static void bus_collect(void)
{
	bus_preempted++;
	bus_finish(&bus_loads, &bus_load_cycles);
}

/* right after keep.txt is read (psp_recon_tables_start), so the output finds
room before the level's loads fill the heap */
void psp_me_bus_start(void)
{
	if (bus_work != -2)
		return;
	bus_work = emulator() ? -1 : psp_keep_number("mebus", -1);
	bus_edram = psp_keep("mebusedram") != 0;
	if (bus_work >= 0 && !(bus_out = psp_recon_table("mebus", BUS_OUT_WORDS * sizeof(*bus_out))))
		bus_work = -1;
}

void psp_me_bus_phase(int phase)
{
	unsigned int now;

	psp_me_bus_start();
	if (bus_work < 0 || PSP_ME_WORK() || !psp_me_ring_ready())
		return;
	now = sceKernelGetSystemTimeLow();
	if (phase == 1)
	{
		bus_coin = bus_coin * 1103515245u + 12345u;
		bus_loaded = (bus_coin >> 16) & 1u;
		if (bus_loaded && !bus_start(bus_edram))
			bus_loaded = 0;
		bus_tick_start = sceKernelGetSystemTimeLow();
		return;
	}
	if (phase != 0 || !bus_tick_start)
		return;
	if (bus_running)
		bus_finish(&bus_loads, &bus_load_cycles);
	bus_ticks[bus_loaded]++;
	bus_tick_us[bus_loaded] += now - bus_tick_start;
	bus_tick_start = 0;
	if (bus_ticks[0] + bus_ticks[1] >= 300 && bus_ticks[0] && bus_ticks[1])
	{
		float loaded = (float)bus_tick_us[1] / bus_ticks[1], unloaded = (float)bus_tick_us[0] / bus_ticks[0];
		float per_us = me_cycles_per_us > 0.0f ? me_cycles_per_us : 333.0f, during, alone, alone_other;
		int m;

		/* the ME alone, the main CPU asleep: in the memory this run loads
		first, then in the other, so every window also says how the eDRAM
		compares with main RAM for the same work */
		for (m = 0; m < 2; m++)
			if (bus_start(m ? !bus_edram : bus_edram))
			{
				sceKernelDelayThread(8000);
				bus_finish(&bus_alone_loads[m], &bus_alone_cycles[m]);
			}
		during = bus_load_cycles ? bus_loads * per_us / bus_load_cycles : 0.0f;
		alone = bus_alone_cycles[0] ? bus_alone_loads[0] * per_us / bus_alone_cycles[0] : 0.0f;
		alone_other = bus_alone_cycles[1] ? bus_alone_loads[1] * per_us / bus_alone_cycles[1] : 0.0f;
		platform_log("mebus: work %d%s | ticks loaded %lu at %.2f ms, unloaded %lu at %.2f ms: the main CPU %+.1f%% | the ME %.3f loads a us during them (busy %.0f%% of the loaded ticks), %.3f alone: %+.1f%% | alone in %s %.3f | taken over by other ring jobs %lu",
			bus_work, bus_edram ? " eDRAM" : "", bus_ticks[1], loaded / 1000.0f, bus_ticks[0], unloaded / 1000.0f,
			unloaded > 0.0f ? 100.0f * (loaded - unloaded) / unloaded : 0.0f,
			during, bus_tick_us[1] ? 100.0f * (bus_load_cycles / per_us) / bus_tick_us[1] : 0.0f,
			alone, alone > 0.0f ? 100.0f * (during - alone) / alone : 0.0f, bus_edram ? "main RAM" : "eDRAM", alone_other,
			bus_preempted);
		bus_ticks[0] = bus_ticks[1] = bus_tick_us[0] = bus_tick_us[1] = bus_preempted = 0;
		bus_loads = bus_load_cycles = 0;
		bus_alone_loads[0] = bus_alone_loads[1] = bus_alone_cycles[0] = bus_alone_cycles[1] = 0;
	}
}
#endif

#ifdef PSP_MEPROBE
/* "meprobe" (psp_post.h): written through the uncached view, so a record
made on the ME survives the fault that may follow it */
#define MEPROBE_SITES 24
static struct
{
	unsigned int count, reported;
	unsigned int site[MEPROBE_SITES], ra[MEPROBE_SITES], times[MEPROBE_SITES];
} meprobe __attribute__((aligned(64)));

void psp_meprobe_note(int site, void *ra)
{
	volatile typeof(meprobe) *p = (volatile typeof(meprobe) *)((unsigned int)&meprobe | 0x40000000u);
	unsigned int k, n = p->count < MEPROBE_SITES ? p->count : MEPROBE_SITES;

	for (k = 0; k < n; k++)
		if (p->site[k] == (unsigned int)site && p->ra[k] == (unsigned int)ra)
		{
			p->times[k]++;
			return;
		}
	if (n < MEPROBE_SITES)
	{
		p->site[n] = (unsigned int)site;
		p->ra[n] = (unsigned int)ra;
		p->times[n] = 1;
		__asm__ __volatile__("sync");
		p->count = n + 1;
	}
}

/* the main CPU: each new site once, and every site's count each 900th call */
void psp_meprobe_report(void)
{
	volatile typeof(meprobe) *p = (volatile typeof(meprobe) *)((unsigned int)&meprobe | 0x40000000u);
	static unsigned int calls = 0, k;

	if (psp_on_me())
		return;
	for (k = p->reported; k < p->count && k < MEPROBE_SITES; k++)
		platform_log("meprobe: site %u reached on the ME's side from ra %08x (link map: less 0x08804000)", p->site[k], p->ra[k]);
	p->reported = k;
	if (++calls % 900 == 0)
		for (k = 0; k < p->count && k < MEPROBE_SITES; k++)
			platform_log("meprobe: site %u from %08x: %u times", p->site[k], p->ra[k], p->times[k]);
}
#endif
