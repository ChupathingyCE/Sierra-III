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
	/* "sleepmem" (memory_stones row 1): the ME partition (0x08300000-0x083FFFFF)
	watched read-only from here, as user mode can't read it. With
	sleepmem_wanted set before the module starts, the sampler hashes its 16
	64 KB chunks in turn, one every 100 ms (sleepmem_step), counts each
	chunk's changes from one watch to the next, keeps word +0x18 (the image's witness), the
	two words at 0xBC000000/04 as the first watch saw them, and the kernel
	partition's free space, now and its low */
	volatile unsigned int sleepmem_wanted, sleepmem_chunk_us, sleepmem_watches, sleepmem_us;
	volatile unsigned int sleepmem_hash[16], sleepmem_changes[16];
	volatile unsigned int sleepmem_word18, sleepmem_bc0, sleepmem_bc4;
	volatile int sleepmem_kernel_free, sleepmem_kernel_free_low;
	/* "partmap" (Sheepdog01 2026-11-16 21:55: who touches the ME partition, and
	when): the kernel writes its upper 512 KB with the ME probe's pattern
	(word = offset ^ 0xC3A55A3C) at the first step and hashes the lower half's
	pages (Sony's ME image, not written unless partmap_all), then re-reads 16
	4 KB pages a step; for each page, the first change it saw (the word's
	offset in the partition, its value, the system clock in ms, the phase and
	the game time the game wrote) and how many reads found it changed */
	volatile unsigned int partmap_wanted, partmap_all, partmap_filled, partmap_passes, partmap_step_us, partmap_game_time;
	volatile unsigned int partmap_seen[256], partmap_word[256], partmap_value[256], partmap_ms[256], partmap_when[256];
	volatile unsigned int partmap_hash[256];
};

static struct totals *table = 0;

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

/* the system call (exports.exp, "meddrprobe"'s cross-check): one piece of the
ME partition read from the kernel through its uncached window and compared
with the pattern the ME filled it with (word = offset ^ 0xC3A55A3C), so a
wrong word the ME reads back can be told from a word that really changed.
out: [0] words wrong, [1] the first's offset, [2] its value. Returns 0, or -1
for a piece outside the partition or a buffer outside user RAM */
int halo_profile_ddr_check(unsigned int offset, unsigned int bytes, unsigned int *out)
{
	unsigned int start = (unsigned int)out & 0x1FFFFFFF, k, wrong = 0, first = 0, value = 0;
	const volatile unsigned int *word = (const volatile unsigned int *)(0xA8300000u + offset);

	if ((offset & 3) || bytes > 0x100000u || offset > 0x100000u - bytes || start < 0x08800000 ||
		start + 12 > 0x0A000000)
		return -1;
	for (k = 0; k < bytes; k += 4, word++)
	{
		unsigned int got = *word;

		if (got != ((offset + k) ^ 0xC3A55A3Cu) && !wrong++)
		{
			first = offset + k;
			value = got;
		}
	}
	out[0] = wrong;
	out[1] = first;
	out[2] = value;
	return 0;
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
	phase_account(phase);
	pspSdkSetK1(k1);
	return 0;
}

/* the PC sampler's interrupt: the interrupted PC is still in cop0's EPC
while the handler runs (the dispatcher returns through it); on the PSP the
samples land in the game's known hot code, the kernel's share small */
static int pc_handler(void)
{
	unsigned int epc, offset;

	__asm__ volatile ("mfc0 %0, $14" : "=r"(epc));
	table->pc_samples++;
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
	}
	else
		table->pc_outside++;
	return -1;
}

/* "sleepmem": the ME partition hashed one 64 KB chunk a sampler tick, so a
whole watch takes 1.6 s and no tick holds the game long. Read uncached
(0xA8300000), with no cache operation: nothing here can write the range. A
chunk's hash is a rotate-and-add over its words: any changed word changes it
but for a vanishing chance */
#define SLEEPMEM_BASE 0xA8300000u
#define SLEEPMEM_CHUNK (64 * 1024)
static unsigned int sleepmem_chunk;
static unsigned long long sleepmem_started;

static void sleepmem_step(void)
{
	unsigned int chunk = sleepmem_chunk;
	const volatile unsigned int *word = (const volatile unsigned int *)(SLEEPMEM_BASE + chunk * SLEEPMEM_CHUNK);
	const volatile unsigned int *end = word + SLEEPMEM_CHUNK / 4;
	unsigned int hash = 0x811C9DC5u, us;
	unsigned long long chunk_start = sceKernelGetSystemTimeWide();

	if (chunk == 0)
		sleepmem_started = chunk_start;
	while (word < end)
		hash = ((hash << 5) | (hash >> 27)) + *word++;
	/* (the longest a chunk held this thread: what the game can lose to it) */
	us = (unsigned int)(sceKernelGetSystemTimeWide() - chunk_start);
	if (us > table->sleepmem_chunk_us)
		table->sleepmem_chunk_us = us;
	if (table->sleepmem_watches && hash != table->sleepmem_hash[chunk])
		table->sleepmem_changes[chunk]++;
	table->sleepmem_hash[chunk] = hash;
	if (++sleepmem_chunk < 16)
		return;
	sleepmem_chunk = 0;
	table->sleepmem_word18 = *(volatile unsigned int *)(SLEEPMEM_BASE + 0x18);
	if (!table->sleepmem_watches)
	{
		table->sleepmem_bc0 = *(volatile unsigned int *)0xBC000000;
		table->sleepmem_bc4 = *(volatile unsigned int *)0xBC000004;
		table->sleepmem_kernel_free_low = 0x7FFFFFFF;
	}
	table->sleepmem_kernel_free = sceKernelPartitionTotalFreeMemSize(1);
	if (table->sleepmem_kernel_free < table->sleepmem_kernel_free_low)
		table->sleepmem_kernel_free_low = table->sleepmem_kernel_free;
	/* (the watch's span by the clock, the sampler's 1.5 s of sleeps in it) */
	table->sleepmem_us = (unsigned int)(sceKernelGetSystemTimeWide() - sleepmem_started);
	table->sleepmem_watches++;
}

/* "partmap" (the table's fields say what). A step reads 16 pages, so a pass
of all 256 takes 1.6 s; each page is read uncached, with no cache operation */
#define PARTMAP_PATTERN 0xC3A55A3Cu
static unsigned int partmap_next;

static void partmap_note(unsigned int page, unsigned int word, unsigned int value)
{
	if (!table->partmap_seen[page]++)
	{
		table->partmap_word[page] = word;
		table->partmap_value[page] = value;
		table->partmap_ms[page] = sceKernelGetSystemTimeLow() / 1000; /* 32-bit: the module has no 64-bit divide */
		table->partmap_when[page] = (table->partmap_game_time << 4) | (unsigned int)(phase_current & 15);
	}
}

static void partmap_step(void)
{
	unsigned long long start = sceKernelGetSystemTimeWide();
	unsigned int k, w, us;

	if (!table->partmap_filled)
	{
		/* the upper half (and with partmap_all the lower) written once */
		for (w = table->partmap_all ? 0 : 0x80000; w < 0x100000; w += 4)
			*(volatile unsigned int *)(SLEEPMEM_BASE + w) = w ^ PARTMAP_PATTERN;
		for (k = 0; k < 256; k++)
		{
			const volatile unsigned int *word = (const volatile unsigned int *)(SLEEPMEM_BASE + k * 4096);
			unsigned int hash = 0x811C9DC5u;

			for (w = 0; w < 1024; w++)
				hash = ((hash << 5) | (hash >> 27)) + word[w];
			table->partmap_hash[k] = hash;
		}
		table->partmap_filled = 1;
		return;
	}
	for (k = 0; k < 16; k++)
	{
		unsigned int page = partmap_next, base = page * 4096;
		const volatile unsigned int *word = (const volatile unsigned int *)(SLEEPMEM_BASE + base);

		if (page >= 128 || table->partmap_all)
		{
			for (w = 0; w < 1024; w++)
			{
				unsigned int got = word[w];

				if (got != ((base + w * 4) ^ PARTMAP_PATTERN))
				{
					partmap_note(page, base + w * 4, got);
					break;
				}
			}
		}
		else
		{
			unsigned int hash = 0x811C9DC5u;

			for (w = 0; w < 1024; w++)
				hash = ((hash << 5) | (hash >> 27)) + word[w];
			if (hash != table->partmap_hash[page])
				partmap_note(page, 0xFFFFFFFFu, hash);
		}
		if (++partmap_next == 256)
		{
			partmap_next = 0;
			table->partmap_passes++;
		}
	}
	us = (unsigned int)(sceKernelGetSystemTimeWide() - start);
	if (us > table->partmap_step_us)
		table->partmap_step_us = us;
}

static int sampler(SceSize args, void *argp)
{
	unsigned int last[COUNTERS], now[COUNTERS], k;

	(void)args; (void)argp;
	profiler_read(last);
	for (;;)
	{
		if (table->sleepmem_wanted)
			sleepmem_step();
		if (table->partmap_wanted)
			partmap_step();
		sceKernelDelayThread(100000);
		profiler_read(now);
		for (k = 0; k < COUNTERS; k++)
		{
			table->count[k] += (unsigned int)(now[k] - last[k]);
			last[k] = now[k];
		}
		phase_account(-1);
		if (table->threads_wanted)
			threads_list();
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
		SceSysTimerId timer = sceSTimerAlloc();

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
	if (table && table->pc_wanted && (int)table->pc_timer >= 0 && table->pc_timer)
	{
		sceSTimerStopCount((SceSysTimerId)table->pc_timer);
		sceSTimerFree((SceSysTimerId)table->pc_timer);
	}
	return 0;
}
