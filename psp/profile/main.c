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
#include <pspsysmem_kernel.h>
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
	if (next >= 0)
	{
		phase_current = next & (PHASES - 1);
		table->phase_changes++;
	}
	sceKernelCpuResumeIntr(interrupts);
}

/* build 105: the bus-priority probe (psp_hardware_reference.md section 9.1).
Read-only: the registers psdevwiki and uofw name in the DDR controller, the
GE and its EDRAM, and the system controller, whose meaning nobody has
documented. A value that differs between the GE idle (start-up) and busy
(in play) marks a candidate for the arbiter's priority. The list lives here,
not in the caller, so nothing else can be read through this call; registers
known to act when read or written (the DDR flush, the hardware mutex, the
soft interrupt) are left out */
static const unsigned int bus_registers[] =
{
	0xBD000020, 0xBD000024, 0xBD00002C, 0xBD000030, 0xBD000034, 0xBD000038, 0xBD000040, 0xBD000044, /* DDR */
	0xBD400004, 0xBD400104, 0xBD400300,                                                             /* GE */
	0xBD500040, 0xBD500050, 0xBD500060, 0xBD500090, 0xBD5000A0,                                     /* EDRAM */
	0xBC100040, 0xBC10004C, 0xBC100050,                                                             /* system */
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

/* the system call (exports.exp): the game's thread changes phase */
int halo_profile_phase(int phase)
{
	if (!table)
		return -1;
	phase_account(phase);
	return 0;
}

static int sampler(SceSize args, void *argp)
{
	unsigned int last[COUNTERS], now[COUNTERS], k;

	(void)args; (void)argp;
	profiler_read(last);
	for (;;)
	{
		sceKernelDelayThread(100000);
		profiler_read(now);
		for (k = 0; k < COUNTERS; k++)
		{
			table->count[k] += (unsigned int)(now[k] - last[k]);
			last[k] = now[k];
		}
		phase_account(-1);
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
	return 0;
}
