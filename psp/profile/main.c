/*
HALO_PROFILE.PRX (build 37; phases build 46)

The PSP's hardware profiler: counters in the CPU for its cycles, the cycles
it spent waiting (on memory, on its own pipeline, on the VFPU and the other
coprocessors) and its cache misses, at 0xBC400000, where only kernel mode
reaches. The game loads this module when keep.txt says "hwprofile" and hands
it the address of a table in its own memory (psp_platform.c); a thread here
adds the counters into that table ten times a second (they are 32 bits and
the CPU count wraps in 13 s at 333 MHz), and the game logs the differences.
*/

#include <pspkernel.h>
#include <pspsdk.h>
#include <pspsysmem_kernel.h>
#include <pspsystimer.h>
#include <string.h>
#include "framepic.h"
#ifdef PSP_KSAMPLE
#include "ksample.h"
#endif

PSP_MODULE_INFO("halo_profile", PSP_MODULE_KERNEL, 1, 0);
PSP_MAIN_THREAD_ATTR(0);

#define COUNTERS 19

/* the game's table: a sequence number, then 64-bit totals of each counter
after "enable", in PspDebugProfilerRegs order */
/* build 44: and, once at the start, the memory partitions' free space, as
only kernel mode may ask (1 is the kernel's own, 2 the game's 24 MB, 5 the
4 MB "volatile" block the texture cache borrows, the others the firmware's) */
#define PARTITIONS 8
#define PHASES 4
struct totals
{
	volatile unsigned int sequence;
	volatile unsigned int pad;
	volatile unsigned long long count[COUNTERS];
	volatile int partition_total[PARTITIONS];
	volatile int partition_largest[PARTITIONS];
	volatile unsigned int partitions_surveyed;
	/* build 46: the counters split by what the game was doing (0 between
	the parts below, 1 the game tick, 2 drawing, 3 the frame end: the GE's
	finish, the buffer swap, the texture cache's frame end). The game names
	each change through halo_profile_phase, a system call this module exports
	(exports.exp); the counters see the whole CPU, so the sound thread's
	work lands in whichever phase it interrupted */
	volatile unsigned long long phase_count[PHASES][COUNTERS];
	volatile unsigned int phase_changes;
	/* bit 0 of coprocessor 0's register 24: the firmware's own test for
	the 04g+ boards' "L2" cache (uofw reboot/hw.c); 0 expected on a 1000 */
	volatile unsigned int cop0_24;
	/* the split by thread ("hwthreads"): the counters above see the whole
	CPU, and its cycle counter stops while the idle thread has it halted, so
	a phase's share of the cycles says nothing of who ran in it. With
	threads_wanted set by the game before the module starts, each phase also
	gets microseconds of the clock, of every other thread's running (the
	threads' own run clocks) and of the idle thread, and the thread switches */
	volatile unsigned int threads_wanted;
	volatile unsigned int threads_tracked;
	volatile unsigned long long phase_threads[PHASES][4]; /* clock, other threads, idle, switches */
	/* "pcsample": where the CPU is, by the program's code. The game sets
	pc_wanted, pc_counts (an array in its own memory: the tick's bins, then
	the drawing's), pc_base, pc_bins and pc_shift before the module starts;
	a system timer's interrupt here then reads the interrupted PC (cop0's
	EPC) at each period and adds one to its bin, a bin being 1 << pc_shift
	bytes of code from pc_base. PCs elsewhere are counted, not binned */
	volatile unsigned int pc_wanted, pc_counts, pc_base, pc_bins, pc_shift, pc_period;
	volatile unsigned int pc_samples, pc_binned, pc_other_phase, pc_kernel, pc_outside, pc_timer;
	/* pc_tables 3: a third table of bins for the other phases (other, frame
	end), so the time outside the tick and the drawing is named too; the
	kernel's samples by the phase they fell in (system calls, firmware) */
	volatile unsigned int pc_tables, pc_kernel_by_phase[4];
	/* "framepic" (docs/frame_picture.md): the game's struct framepic_block,
	0 without it; read at each halo_profile_box call */
	volatile unsigned int framepic_block;
#ifdef PSP_KSAMPLE
	/* "ksample" (ksample.h): the game's struct ksample_block, 0 without it */
	volatile unsigned int ksample_block;
#endif
};

static struct totals *table = 0;
#ifdef PSP_KSAMPLE
/* the block, once module_start has checked it; 0 when not asked for */
static struct ksample_block *ks = 0;
static void ksample_intr_account(void);
#endif

/* the registers themselves (libpspdebug's profiler functions, without the
screen printer that library drags in) */
#define PROFILER ((volatile unsigned int *)0xBC400000)

static void profiler_read(unsigned int *out)
{
	unsigned int k;

	for (k = 0; k < COUNTERS; k++)
		out[k] = PROFILER[1 + k];
}

/* build 46: the phase accounting. The counters since the last change go to
the phase that was running; the sampler thread does the same ten times a
second, so a phase longer than the 13 s the cycle counter takes to wrap
(a map load) still adds up. Interrupts are off while it runs: the game's
thread and the sampler both use it, and on one core nothing else can come
between them then */
static unsigned int phase_last[COUNTERS];
static int phase_current = 0;

/* the split by thread: the threads' ids, refreshed by the sampler, and each
one's run clock when last read. Every thread but the game's is idle or
waiting at the moment of a snapshot (the game's thread is the one calling,
or the sampler is, and then the game's thread waits), so their run clocks
are up to date; the game's own is the clock less the others and the idle */
#define THREADS 96
static SceUID thread_ids[THREADS];
static unsigned long long thread_last[THREADS];
static int thread_count = 0;
static SceUID game_thread = -1;
static unsigned long long clock_last, idle_last;
static unsigned int switches_last;

/* a failed refer returns -1 and leaves the last reading standing: taking
it as 0 would turn the next good reading into the thread's whole lifetime */
static int run_clock(SceUID id, unsigned long long *run)
{
	SceKernelThreadRunStatus status;

	memset(&status, 0, sizeof(status));
	status.size = sizeof(status);
	if (sceKernelReferThreadRunStatus(id, &status) < 0)
		return -1;
	*run = ((unsigned long long)status.runClocks.hi << 32) | status.runClocks.low;
	return 0;
}

static int system_clocks(unsigned long long *idle, unsigned int *switches)
{
	SceKernelSystemStatus status;

	memset(&status, 0, sizeof(status));
	status.size = sizeof(status);
	if (sceKernelReferSystemStatus(&status) < 0)
		return -1;
	*idle = ((unsigned long long)status.idleClocks.hi << 32) | status.idleClocks.low;
	*switches = status.threadSwitchCount;
	return 0;
}

/* the time since the last snapshot, to the phase that was running; called
with interrupts off */
static void threads_account(void)
{
	unsigned long long now = sceKernelGetSystemTimeWide(), idle = idle_last, others = 0;
	unsigned int switches = switches_last;
	int k;

	for (k = 0; k < thread_count; k++)
	{
		unsigned long long run;

		if (run_clock(thread_ids[k], &run) < 0)
			continue;
		if (thread_ids[k] != game_thread && run >= thread_last[k])
			others += run - thread_last[k];
		thread_last[k] = run;
	}
	if (system_clocks(&idle, &switches) < 0 || idle < idle_last)
	{
		idle = idle_last;
		switches = switches_last;
	}
	table->phase_threads[phase_current][0] += now - clock_last;
	table->phase_threads[phase_current][1] += others;
	table->phase_threads[phase_current][2] += idle - idle_last;
	table->phase_threads[phase_current][3] += switches - switches_last;
	clock_last = now;
	idle_last = idle;
	switches_last = switches;
}

/* the sampler's: threads made since the last list join it from their run
clock now, and those gone leave it */
static void threads_list(void)
{
	SceUID ids[THREADS];
	unsigned long long last[THREADS];
	int count = 0, k, j;
	int interrupts;

	if (sceKernelGetThreadmanIdList(SCE_KERNEL_TMID_Thread, ids, THREADS, &count) < 0)
		return;
	if (count > THREADS)
		count = THREADS;
	interrupts = sceKernelCpuSuspendIntr();
	threads_account();
	for (k = 0; k < count; k++)
	{
		last[k] = 0;
		for (j = 0; j < thread_count; j++)
			if (thread_ids[j] == ids[k])
				break;
		if (j < thread_count)
			last[k] = thread_last[j];
		else
			run_clock(ids[k], &last[k]);
	}
	memcpy(thread_ids, ids, sizeof(SceUID) * count);
	memcpy(thread_last, last, sizeof(unsigned long long) * count);
	thread_count = count;
	table->threads_tracked = count;
	sceKernelCpuResumeIntr(interrupts);
}

static void phase_account(int next)
{
	unsigned int now[COUNTERS], k;
	int interrupts = sceKernelCpuSuspendIntr();

	profiler_read(now);
	for (k = 0; k < COUNTERS; k++)
	{
		table->phase_count[phase_current][k] += (unsigned int)(now[k] - phase_last[k]);
		phase_last[k] = now[k];
	}
	if (table->threads_wanted && thread_count)
		threads_account();
	if (next >= 0)
	{
		phase_current = next & (PHASES - 1);
		table->phase_changes++;
	}
	sceKernelCpuResumeIntr(interrupts);
}

/* The bus probe: the system controller's clock and reset words, read at
start-up and in play (hwprofile, benchrun). It once read the DDR controller,
the GE and the GE's eDRAM block too, looking for a bus-priority setting;
the register snapshot bench settled that (none moves with load) and showed
that reading the GE's register space (0xBD4xxxxx, 0xBD5xxxxx) while the
Media Engine runs can switch the PSP off (docs/hw_capabilities.md section
6.4), so only the words read safely in every condition stay. The list
lives here, not in the caller, so nothing else can be read through this
call */
static const unsigned int bus_registers[] =
{
	0xBC100040, 0xBC10004C, 0xBC100050, /* system: reset, clock enables, bus clocks */
};
#define BUS_REGISTERS (int)(sizeof(bus_registers) / sizeof(bus_registers[0]))

/* the system call (exports.exp): pairs of (address, value) into the game's
memory; returns how many pairs, or -1 for a buffer outside user RAM */
int halo_profile_bus_probe(unsigned int *pairs, int max_pairs)
{
	unsigned int start = (unsigned int)pairs & 0x1FFFFFFF;
	int k, count = max_pairs < BUS_REGISTERS ? max_pairs : BUS_REGISTERS;

	if (count <= 0 || start < 0x08800000 || start + (unsigned int)count * 8 > 0x0A000000)
		return -1;
	for (k = 0; k < count; k++)
	{
		pairs[2 * k] = bus_registers[k];
		pairs[2 * k + 1] = *(volatile unsigned int *)bus_registers[k];
	}
	return count;
}

/* the system call (exports.exp): the game's thread changes phase. A system
call arrives with k1 marking a user-mode caller, and the firmware then
refuses the refer calls' buffers (they're on this call's kernel stack) and
kernel threads' status; with k1 cleared they answer as they do the sampler,
which is a kernel thread. The emulator doesn't check k1, so only the PSP
showed it */
int halo_profile_phase(int phase)
{
	unsigned int k1;

	if (!table)
		return -1;
	k1 = pspSdkSetK1(0);
	if (game_thread < 0)
		game_thread = sceKernelGetThreadId();
#ifdef PSP_KSAMPLE
	if (ks)
	{
		if (ks->game_uid <= 0)
			ks->game_uid = game_thread;
		ksample_intr_account();
		ks->phase_entries[phase & (PHASES - 1)]++;
	}
#endif
	phase_account(phase);
	pspSdkSetK1(k1);
	return 0;
}

/* the system call (exports.exp): a box opened (box >= 0) or the one on top
closed (FRAMEPIC_POP). The counts since the last mark go to the box that
was on top (0, "outside any box", when none was), so each box's are its own
less the boxes it opened. Its own entry point, not halo_profile_phase: that
one masks its argument into the four phases and, with hwthreads, walks the
thread list at each change */
static int framepic_stack[FRAMEPIC_STACK];
static unsigned int framepic_last[FRAMEPIC_MEASURES];
static unsigned int framepic_switches_last;
static int framepic_started = 0;
/* the sampler's last read of each counter, kept here so halo_profile_totals
can add what has run since */
static unsigned int sampler_last[COUNTERS];

static const unsigned char framepic_registers[FRAMEPIC_MEASURES] = FRAMEPIC_REGISTERS;

int halo_profile_box(int box)
{
	struct framepic_block *block;
	unsigned int now[FRAMEPIC_MEASURES], k1, k;
	int interrupts, top;

	if (!table || !table->framepic_block)
		return -1;
	block = (struct framepic_block *)table->framepic_block;
	if (block->magic != FRAMEPIC_MAGIC || box >= FRAMEPIC_BOXES)
		return -1;
	k1 = pspSdkSetK1(0);
	interrupts = sceKernelCpuSuspendIntr();
	for (k = 0; k < FRAMEPIC_MEASURES; k++)
		now[k] = PROFILER[1 + framepic_registers[k]];
	top = block->depth ? framepic_stack[block->depth - 1] : 0;
	if (framepic_started)
		for (k = 0; k < FRAMEPIC_MEASURES; k++)
		{
			unsigned int delta = now[k] - framepic_last[k];

			block->acc[top][k] += delta;
			block->sum[top][k] += delta;
		}
	else
	{
		/* the identity check's start: the module's totals at the read the
		boxes start from, so nothing before the first mark is counted
		against them */
		for (k = 0; k < FRAMEPIC_MEASURES; k++)
		{
			unsigned int r = framepic_registers[k];

			block->start[k] = table->count[r] + (unsigned int)(now[k] - sampler_last[r]);
		}
		block->started = 1;
	}
	framepic_started = 1;
	block->marks[top]++;
	if (box == FRAMEPIC_POP)
	{
		if (block->depth)
			block->depth--;
		else
			block->unbalanced++;
	}
	else if (box >= 0 && block->depth < FRAMEPIC_STACK)
		framepic_stack[block->depth++] = box;
	else
		block->unbalanced++;
	sceKernelCpuResumeIntr(interrupts);
	if (block->want_switch)
	{
		unsigned long long idle;
		unsigned int switches;

		if (system_clocks(&idle, &switches) == 0)
		{
			if (switches != framepic_switches_last)
				block->switched[top] = 1;
			framepic_switches_last = switches;
		}
	}
	/* the counters again after the bookkeeping, so the next box isn't
	charged for this mark's own work beyond the reads above */
	for (k = 0; k < FRAMEPIC_MEASURES; k++)
	{
		framepic_last[k] = PROFILER[1 + framepic_registers[k]];
		block->self[k] += framepic_last[k] - now[k];
	}
	pspSdkSetK1(k1);
	return 0;
}

/* the system call (exports.exp): every counter's total since the module
started, at this instant: the sampler's total plus what has run since its
last beat. The frame picture's identity check (docs/frame_picture.md) reads
it at its window's two ends, against the boxes' own sums. out holds COUNTERS
values, in the profiler's order */
int halo_profile_totals(unsigned long long *out)
{
	unsigned int now[COUNTERS], k1, k;
	int interrupts;

	if (!table || !out || ((unsigned int)out & 0x1FFFFFFFu) < 0x08800000u ||
		((unsigned int)out & 0x1FFFFFFFu) + COUNTERS * sizeof(*out) > 0x0A000000u)
		return -1;
	k1 = pspSdkSetK1(0);
	interrupts = sceKernelCpuSuspendIntr();
	profiler_read(now);
	for (k = 0; k < COUNTERS; k++)
		out[k] = table->count[k] + (unsigned int)(now[k] - sampler_last[k]);
	/* the span since the last mark, from the same reads: the module has
	counted it and no box has yet */
	if (table->framepic_block && framepic_started)
	{
		struct framepic_block *block = (struct framepic_block *)table->framepic_block;

		if (block->magic == FRAMEPIC_MAGIC)
			for (k = 0; k < FRAMEPIC_MEASURES; k++)
				block->open[k] = now[framepic_registers[k]] - framepic_last[k];
	}
	sceKernelCpuResumeIntr(interrupts);
	pspSdkSetK1(k1);
	return 0;
}

#ifdef PSP_KSAMPLE
/* who holds the CPU, from the game's waiter slots (ksample.h): the kernel
won't name the running thread inside an interrupt, the game's threads say
when they block. A slot read mid-change is one sample's error at most */
static unsigned int ksample_ctx(unsigned int *main_site)
{
	unsigned int k, n = ks->waiters, other_awake = 0, main_found = 0;
	int game = ks->game_uid;

	*main_site = 0;
	if (!ks->want_ctx || game <= 0)
		return 3;
	if (n > KSAMPLE_WAITERS)
		n = KSAMPLE_WAITERS;
	for (k = 0; k < n; k++)
	{
		int uid = ks->waiter[k].uid;

		if (uid <= 0)
			continue;
		if (uid == game)
		{
			main_found = 1;
			*main_site = ks->waiter[k].site;
		}
		else if (!ks->waiter[k].site)
			other_awake = 1;
	}
	if (!main_found)
		return 3;
	if (!*main_site)
		return 0;
	return other_awake ? 1 : 2;
}

/* the main thread's wait site's slot, added on first sight; -1 when the
table is full. Only the interrupt adds, so no lock */
static int ksample_site(unsigned int pc)
{
	unsigned int k, n = ks->sites;

	for (k = 0; k < n; k++)
		if (ks->site_pc[k] == pc)
			return (int)k;
	if (n >= KSAMPLE_SITES)
	{
		ks->sites_full++;
		return -1;
	}
	ks->site_pc[n] = pc;
	ks->sites = n + 1;
	return (int)n;
}

/* every sample: its phase and who held the CPU */
static void ksample_every(unsigned int phase, unsigned int ctx, int site)
{
	ks->phase_samples[phase]++;
	ks->ctx_samples[phase][ctx]++;
	if (site >= 0)
		ks->site_all[phase][site]++;
}

/* the kernel's sample: binned by the game's phase and by who held the CPU */
static void ksample_count(unsigned int epc, unsigned int phase, unsigned int ctx, int site)
{
	unsigned int physical = epc & 0x1FFFFFFFu;

	ks->phase_kernel[phase]++;
	if (site >= 0)
		ks->site_kernel[phase][site]++;
	/* asked once, to keep the refusal's code: the first run's 17,943
	refusals are taken to be the kernel's interrupt-context check */
	if (!ks->kid_interrupt)
		ks->kid_interrupt = sceKernelGetThreadId();
	if (physical - KSAMPLE_LOW < KSAMPLE_SPAN)
		((volatile unsigned int *)ks->counts)[(phase * KSAMPLE_CTX + ctx) * ks->bins + ((physical - KSAMPLE_LOW) >> ks->shift)]++;
	else
	{
		ks->far++;
		ks->far_pc[ks->far_next++ % KSAMPLE_FAR] = epc;
	}
}

/* the interrupt numbers pspintrman.h names; those with a handler at start
are kept */
static const unsigned char ksample_intr_candidates[] =
{
	4, 5, 6, 7, 8, 10, 12, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 30, 31, 36, 60, 61, 65, 66,
};
static unsigned int intr_last_calls[KSAMPLE_INTRS];
static unsigned long long intr_last_clock[KSAMPLE_INTRS];

/* the interrupt manager's switch for a handler's calls and clocks
(ksample.h): 1 turns them on. Declared here: pspintrman_kernel.h has no
prototype, though libpspkernel has its import */
int sceKernelSetIntrLogging(int intno, int on);

static int ksample_intr_query(unsigned int intno, PspIntrHandlerOptionParam *info)
{
	memset(info, 0, sizeof(*info));
	info->size = sizeof(*info);
	return QueryIntrHandlerInfo((SceUID)intno, -1, info);
}

static int ksample_intr_read(unsigned int intno, unsigned int *calls, unsigned long long *clock)
{
	PspIntrHandlerOptionParam info;
	int result = ksample_intr_query(intno, &info);

	if (result < 0)
		return result;
	*calls = info.calls;
	*clock = ((unsigned long long)info.total_clock_hi << 32) | info.total_clock_lo;
	return 0;
}

/* from module_start: the sources with a handler (the query answers every
number below 68, handler or not, so the handler's address decides), each
one's record before its logging is turned on (calls 0 and least clock
ffffffff show it was off), then logging on and the counts from there */
static void ksample_intr_start(void)
{
	unsigned int k, n = 0;

	for (k = 0; k < sizeof(ksample_intr_candidates) && n < KSAMPLE_INTRS; k++)
	{
		PspIntrHandlerOptionParam info;
		unsigned int intno = ksample_intr_candidates[k];
		int result = ksample_intr_query(intno, &info);

		if (result < 0)
		{
			if (!ks->intr_error)
				ks->intr_error = result;
			continue;
		}
		if (!info.entry)
		{
			ks->intr_no_handler++;
			continue;
		}
		ks->intr_entry[n] = info.entry;
		ks->intr_calls_start[n] = info.calls;
		ks->intr_min_start[n] = info.min_clock_lo;
		result = sceKernelSetIntrLogging((int)intno, 1);
		if (result < 0)
		{
			if (!ks->intr_log_error)
				ks->intr_log_error = result;
		}
		else
			ks->intr_logged++;
		if (ksample_intr_read(intno, &intr_last_calls[n], &intr_last_clock[n]) < 0)
			continue;
		ks->intr_no[n++] = intno;
	}
	ks->intr_sources = n;
}

/* from module_stop: logging back off, as the firmware keeps it */
static void ksample_intr_stop(void)
{
	unsigned int k;

	if (!ks || !ks->want_intr)
		return;
	for (k = 0; k < ks->intr_sources && k < KSAMPLE_INTRS; k++)
		sceKernelSetIntrLogging((int)ks->intr_no[k], 0);
}

/* each source's calls since the last read, to the phase that was running;
from the game's thread (halo_profile_phase) and the sampler, with k1
cleared, and interrupts off so the two never interleave */
static void ksample_intr_account(void)
{
	unsigned int k, phase;
	int interrupts;

	if (!ks || !ks->want_intr || !ks->intr_sources)
		return;
	interrupts = sceKernelCpuSuspendIntr();
	phase = (unsigned int)phase_current & 3;
	for (k = 0; k < ks->intr_sources; k++)
	{
		unsigned int calls;
		unsigned long long clock;

		if (ksample_intr_read(ks->intr_no[k], &calls, &clock) < 0)
			continue;
		ks->intr_calls[phase][k] += calls - intr_last_calls[k];
		if (clock >= intr_last_clock[k])
			ks->intr_clock[phase][k] += clock - intr_last_clock[k];
		intr_last_calls[k] = calls;
		intr_last_clock[k] = clock;
	}
	sceKernelCpuResumeIntr(interrupts);
}

/* the kernel's module list, for naming the bins offline: from a kernel
thread (module_start, the sampler), with k1 cleared so the kernel's own
modules answer too */
static void ksample_modules(void)
{
	SceUID ids[KSAMPLE_MODULES];
	int count = 0, k, n = 0, result;
	unsigned int k1 = pspSdkSetK1(0);

	result = sceKernelGetModuleIdList(ids, sizeof(ids), &count);
	if (result < 0)
		ks->module_error = result;
	else
	{
		if (count > KSAMPLE_MODULES)
			count = KSAMPLE_MODULES;
		for (k = 0; k < count; k++)
		{
			SceKernelModuleInfo info;

			memset(&info, 0, sizeof(info));
			info.size = sizeof(info);
			if ((result = sceKernelQueryModuleInfo(ids[k], &info)) < 0)
			{
				ks->module_error = result;
				continue;
			}
			ks->module[n].text_addr = info.text_addr;
			ks->module[n].text_size = info.text_size;
			memcpy(ks->module[n].name, info.name, sizeof(ks->module[n].name));
			ks->module[n].name[sizeof(ks->module[n].name) - 1] = 0;
			n++;
		}
		ks->modules = n;
	}
	ks->module_reads++;
	pspSdkSetK1(k1);
}

/* the sampler's: sceKernelGetThreadId's answer outside the interrupt, for
the head line beside the interrupt's */
static void ksample_kid_thread(void)
{
	if (!ks->kid_thread)
		ks->kid_thread = sceKernelGetThreadId();
}
#endif

/* the PC sampler's interrupt: the interrupted PC is still in cop0's EPC
while the handler runs (the dispatcher returns through it); on the PSP the
samples land in the game's known hot code, the kernel's share small */
static int pc_handler(void)
{
	unsigned int epc, offset;

	__asm__ volatile ("mfc0 %0, $14" : "=r"(epc));
	table->pc_samples++;
#ifdef PSP_KSAMPLE
	unsigned int ks_phase = (unsigned int)phase_current & 3, ks_ctx = 3, ks_site_pc = 0;
	int ks_site = -1;

	if (ks)
	{
		ks_ctx = ksample_ctx(&ks_site_pc);
		if (ks_site_pc)
			ks_site = ksample_site(ks_site_pc);
		ksample_every(ks_phase, ks_ctx, ks_site);
	}
#endif
	offset = epc - table->pc_base;
	if ((offset >> table->pc_shift) < table->pc_bins)
	{
		if (phase_current == 1 || phase_current == 2)
		{
			((volatile unsigned int *)table->pc_counts)[(phase_current - 1) * table->pc_bins + (offset >> table->pc_shift)]++;
			table->pc_binned++;
		}
		else
		{
			if (table->pc_tables >= 3)
				((volatile unsigned int *)table->pc_counts)[2 * table->pc_bins + (offset >> table->pc_shift)]++;
			table->pc_other_phase++;
		}
	}
	else if (epc >= 0x80000000u)
	{
		table->pc_kernel++;
		table->pc_kernel_by_phase[phase_current & 3]++;
#ifdef PSP_KSAMPLE
		if (ks)
			ksample_count(epc, ks_phase, ks_ctx, ks_site);
#endif
	}
	else
		table->pc_outside++;
	return -1;
}

static int sampler(SceSize args, void *argp)
{
	unsigned int now[COUNTERS], k;
	int interrupts;

	(void)args; (void)argp;
	profiler_read(sampler_last);
	for (;;)
	{
		sceKernelDelayThread(100000);
		/* interrupts off, so halo_profile_totals never reads a total and its
		last read from two different beats */
		interrupts = sceKernelCpuSuspendIntr();
		profiler_read(now);
		for (k = 0; k < COUNTERS; k++)
		{
			table->count[k] += (unsigned int)(now[k] - sampler_last[k]);
			sampler_last[k] = now[k];
		}
		sceKernelCpuResumeIntr(interrupts);
		phase_account(-1);
		if (table->threads_wanted)
			threads_list();
#ifdef PSP_KSAMPLE
		/* every 5 s: modules loaded since (a map's codecs) join the list */
		if (ks)
		{
			unsigned int k1 = pspSdkSetK1(0);

			ksample_intr_account();
			pspSdkSetK1(k1);
			ksample_kid_thread();
			if (table->sequence % 50 == 0)
				ksample_modules();
		}
#endif
		table->sequence++;
	}
	return 0;
}

int module_start(SceSize args, void *argp)
{
	unsigned int address;
	SceUID thread;

	if (args < 4 || !argp)
		return 0;
	address = *(unsigned int *)argp;
	if (address < 0x08800000 || address >= 0x0A000000 - sizeof(struct totals))
		return 0;
	table = (struct totals *)address;
	{
		int id;

		/* the free-size calls don't check the partition exists (uofw
		partition.c: MpidToCB(mpid)->firstCtlBlk); a PSP-1000 has no
		partitions 7 and 8, and asking crashed sysmem with a bus error
		(b44). sceKernelQueryMemoryPartitionInfo does check: ask it
		first, and skip empty partitions too */
		for (id = 0; id < PARTITIONS; id++)
		{
			PspSysmemPartitionInfo info;

			memset(&info, 0, sizeof(info));
			info.size = sizeof(info);
			if (sceKernelQueryMemoryPartitionInfo(id + 1, &info) < 0 || info.memsize == 0)
			{
				table->partition_total[id] = -1;
				table->partition_largest[id] = -1;
				continue;
			}
			table->partition_total[id] = sceKernelPartitionTotalFreeMemSize(id + 1);
			table->partition_largest[id] = sceKernelPartitionMaxFreeMemSize(id + 1);
		}
		table->partitions_surveyed = 1;
	}
	{
		unsigned int k;

		unsigned int cop0_24;

		__asm__ volatile ("mfc0 %0, $24" : "=r"(cop0_24));
		table->cop0_24 = cop0_24;
		for (k = 1; k <= COUNTERS; k++)
			PROFILER[k] = 0;
		PROFILER[0] = 1;
		profiler_read(phase_last);
		clock_last = sceKernelGetSystemTimeWide();
		system_clocks(&idle_last, &switches_last);
	}
	if (table->pc_wanted && table->pc_counts >= 0x08800000 && table->pc_bins && table->pc_shift < 16 &&
		table->pc_counts + table->pc_bins * 4 * (table->pc_tables >= 3 ? 3 : 2) <= 0x0A000000)
	{
		SceSysTimerId timer;

#ifdef PSP_KSAMPLE
		/* the block is the game's: checked here once, before the interrupt
		can use it */
		{
			struct ksample_block *block = (struct ksample_block *)table->ksample_block;
			unsigned int at = (unsigned int)block & 0x1FFFFFFFu;

			if (block && at >= 0x08800000u && at + sizeof(*block) <= 0x0A000000u && block->magic == KSAMPLE_MAGIC &&
				block->shift >= 8 && block->shift <= 16 && block->bins == KSAMPLE_SPAN >> block->shift &&
				(block->counts & 0x1FFFFFFFu) >= 0x08800000u &&
				(block->counts & 0x1FFFFFFFu) + KSAMPLE_PHASES * KSAMPLE_CTX * block->bins * 4 <= 0x0A000000u)
			{
				ks = block;
				ksample_modules();
				if (ks->want_intr)
					ksample_intr_start();
			}
		}
#endif
		timer = sceSTimerAlloc();

		table->pc_timer = (unsigned int)timer;
		if (timer >= 0)
		{
			/* at least ~0.1 ms between samples (~48 counts a microsecond): a
			shorter period starves the game and the PSP restarts */
			sceSTimerSetHandler(timer, table->pc_period >= 4194 ? (int)table->pc_period : 41943, pc_handler, 0);
			sceSTimerStartCount(timer);
		}
	}
	thread = sceKernelCreateThread("halo profiler", sampler, 0x10, 0x800, 0, 0);
	if (thread >= 0)
		sceKernelStartThread(thread, 0, 0);
	return 0;
}

int module_stop(SceSize args, void *argp)
{
	(void)args; (void)argp;
	PROFILER[0] = 0;
#ifdef PSP_KSAMPLE
	ksample_intr_stop();
#endif
	if (table && table->pc_wanted && (int)table->pc_timer >= 0 && table->pc_timer)
	{
		sceSTimerStopCount((SceSysTimerId)table->pc_timer);
		sceSTimerFree((SceSysTimerId)table->pc_timer);
	}
	return 0;
}
