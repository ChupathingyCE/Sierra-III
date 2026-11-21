/*
halo_dmacbench: what a DMACplus transfer into or out of the main CPU's
scratchpad costs, and how much of it is the start. The question behind it:
of the 3-5 us a KB that halo_spadprobe v2/v2.1 saw for 1 KB (findings
spad_dmacplus_into_v2_1000, spad_dmacplus_me_v21_3000), how much is a fixed
start cost, and how much do chained descriptors and the burst code take
back? It decides whether handing the sweep records over (up to ~60 KB a
tick, made ahead) is worth a box.

What it doesn't repeat: halo_spadprobe already showed which channels reach
the scratchpad (Me2Sc 0x1A0 and Sc2Me 0x180 with the ME's eDRAM, Sc2Sc 0x1C0
with main RAM, the user forms 0x0001xxxx/0x4001xxxx only), and bench15's
DMACplus rows gave rates by length into eDRAM from main RAM with ~28 us
fixed, most of which was the kernel call's own instruction-cache walk
(docs/hardware_facts.md), not the engine's. So here every transfer is timed
inside the kernel call, from the first register write to the poll that sees
the channel idle, and no kernel call's cost is in any figure but its own
cell's.

The clock: the main CPU's own cycle counter (CP0 Count, readable here because
every timed span runs in kernel mode through me-core's kcall). Its rate isn't
in hardware_facts for the main CPU (the ME's is 333 MHz), so the first cell
measures it against the 1 MHz system timer and every figure is printed in
cycles, with us beside it from that rate; each transfer's span is also read
on the system timer, so a counter that doesn't run still leaves a figure.

The cells, in the order they run (the proven forms first, the chains last,
since the lab's one earlier DMACplus chain hung a PSP (docs/plan_sound_edram.md;
its form wasn't recorded), so a hang there leaves every other cell behind it.
Each writes a "calling" line before its kernel calls):
  0. the counter's rate; a null kernel call's cost (what a start and a poll
     made in separate calls would add);
  5. main RAM -> the scratchpad (Sc2Sc) at 1, 2, 4 and 8 KB: the comparison;
  1. eDRAM -> the scratchpad (Me2Sc) and the scratchpad -> eDRAM (Sc2Me) at
     1, 2, 4 and 8 KB; a straight line through the medians gives the start
     cost (its intercept) and the rate (its slope);
  4. a start not waited on: the cycles the CPU spends starting it, then a
     poll after the CPU has spun for no time, about half and about twice the
     transfer's waited time (4 KB Me2Sc);
  3. the burst code (4 KB Me2Sc): mcidclan's 1 on both sides (the probe's
     default), 0 on both, and 2 on the scratchpad's side with 1 kept on the
     ME's (bench15 saw 2 on both sides stall a main RAM -> eDRAM channel,
     stopped by its bound with no hang; its likely cause was the ME's side,
     which this keeps at 1);
  2. four 1 KB blocks eDRAM -> the scratchpad: four starts in a row against
     one chain of four descriptors, both contiguous and gathering four
     scattered eDRAM blocks into one contiguous 4 KB.
Every transfer runs three times; each cell's line gives all three.

The rules kept: only user-segment addresses (0x0xxxxxxx, 0x4xxxxxxx) reach
an engine, every descriptor included, refused before any register is written
(v2's first run pointed DMACplus at 0x8001xxxx and hung a PSP-1000); this
CPU's interrupts are off from the first register write to the end of each
transfer, and the channel's interrupt bits are cleared before they're back
on, so the kernel's handler never sees ours; every wait is bounded (a
transfer 2 ms on the system timer, then the channel is stopped; the ME's
answer 2 s); the CPU's cache is written back before an engine reads what it
wrote and dropped before it reads what an engine wrote. The ME's code is plain
loads and stores: no VFPU, no system call, no library call. No power or clock
call is made.

Sources: the channel and control-word layout are halo_spadprobe's (mcidclan's
dmacplus.h in psp-media-engine-custom-core, psdevwiki's Hardware Registers
page, docs/dma_engines.md section 3). The descriptor is mcidclan's
me-dmacplus-me2sc sample (MIT): 16-byte aligned {source, destination, next,
control, status}, the channel loaded with the first one's words, each next
the following descriptor's address (0 ends it), status 1, the end interrupt
bit clear; it ran a Me2Sc chain. Its next words are main-RAM addresses with
bit 0 clear, which in the controller's PL080 order loads each descriptor over
the main bus.

Results go to ms0:/halo/log/dmacbench.txt, one line a cell or row. At the end
it writes "exiting" and quits by itself, as halo_box does; the rig's launcher
mode brings its agent back, and the "exiting" line tells that end from a crash.
*/

#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <psputils.h>
#include <pspiofilemgr.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <me-core-mapper/me-core.h>

PSP_MODULE_INFO("halo_dmacbench", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(4 * 1024);

static const char *log_path = "ms0:/halo/log/dmacbench.txt";

static void out(const char *format, ...)
{
	char line[480];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line) - 2, format, arguments);
	va_end(arguments);
	pspDebugScreenPrintf("%s\n", line);
	SceUID file = sceIoOpen(log_path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
	if (file >= 0)
	{
		strcat(line, "\n");
		sceIoWrite(file, line, strlen(line));
		sceIoClose(file);
	}
}

/* an untaken FPU trap switches the PSP off, so they're masked as the game
masks them (psp_platform.c) */
static void fpu_mask_traps(void)
{
	unsigned int control;

	__asm__ __volatile__("cfc1 %0, $31" : "=r"(control));
	control &= ~0x00000F80u;
	control |= 0x01000000u;
	__asm__ __volatile__("ctc1 %0, $31" : : "r"(control));
}

/* ============================================================ places and patterns */
/* the scratchpad's upper half: the probe's 1 KB at 0x11000 stays clear of it,
and 8 KB is the largest size asked */
#define SPAD_WIN 0x00012000u
#define WIN_BYTES 8192u
#define WIN_WORDS (WIN_BYTES / 4)
#define BLOCK 1024u

/* eDRAM the bench uses: inside the range halo_spadprobe used (the game's old
sound-cache range, psp_me.h), clear of the ME's stack at the top */
#define EDRAM_SRC 0x100000u /* 8 KB, Me2Sc's source */
#define EDRAM_DST 0x104000u /* 8 KB, Sc2Me's destination */
/* four scattered 1 KB sources, out of order in eDRAM, for the gather */
static const u32 scattered[4] = {0x10B000u, 0x108400u, 0x111C00u, 0x10D800u};

static inline u32 pattern(u32 seed, u32 k)
{
	return (seed * 0x9E3779B1u) ^ (k * 0x85EBCA6Bu) ^ 0x5A5A0000u;
}

static inline volatile u32 *spad(void)
{
	return (volatile u32 *)SPAD_WIN;
}

static void spad_fill(u32 seed, u32 words)
{
	u32 k;

	for (k = 0; k < words; k++)
		spad()[k] = pattern(seed, k);
	__asm__ __volatile__("sync");
}

static u32 spad_matches(u32 seed, u32 from, u32 words)
{
	u32 k, right = 0;

	for (k = from; k < from + words; k++)
		right += spad()[k] == pattern(seed, k);
	return right;
}

/* only these reach an engine: halo_spadprobe v2 gave DMACplus 0x8001xxxx and
the PSP-1000 froze, as the engine doesn't refuse a kernel segment itself */
static int engine_address_ok(u32 address)
{
	return (address >> 28) == 0x0u || (address >> 28) == 0x4u;
}

static u32 phys(const void *p)
{
	return (u32)p & 0x1FFFFFFFu;
}

/* ============================================================ the Media Engine */
/* shared, uncached: the main CPU sets the op and its arguments, then the
sequence number; the ME does the op and puts the number in done */
meLibSetSharedUncached32(32);
#define me_sequence (meLibSharedMemory[0])
#define me_op (meLibSharedMemory[1])
#define me_arg(n) (meLibSharedMemory[2 + (n)]) /* 0-5 */
#define me_done (meLibSharedMemory[8])
#define me_alive (meLibSharedMemory[9])
#define me_halted (meLibSharedMemory[10])
#define me_res(n) (meLibSharedMemory[12 + (n)]) /* 0-15 */

enum
{
	OP_FILL = 1, /* eDRAM a0, a2 words <- pattern(a1, a3 + k), or zeros when a1 is 0 */
	OP_MATCH,    /* eDRAM a0, a2 words against pattern(a1, a3 + k): res 0 matches, 1 zeros */
	OP_HALT
};

static int me_ok = 0, kernel_ok = 0;

static inline volatile u32 *edram(u32 offset)
{
	/* the ME's uncached view of its eDRAM */
	return (volatile u32 *)(0x40000000u | offset);
}

/* the ME's side: no kernel calls, no VFPU, no library code but me-core's */
__attribute__((noinline, aligned(4)))
void meLibOnProcess(void)
{
	u32 seen = 0;

	meLibDcacheWritebackInvalidateAll();
	for (;;)
	{
		u32 sequence = me_sequence, k;

		if (sequence == seen)
		{
			/* each poll is an uncached read on the bus: spaced about 60 us apart
			(bench15's ME polling every ~1 us made a 16 KB transfer 61 -> 82 us),
			so the idle ME is rarely on the bus while a timed transfer runs */
			me_alive++;
			for (k = 0; k < 20000; k++)
				__asm__ volatile("nop");
			continue;
		}
		seen = sequence;
		{
			u32 op = me_op, a0 = me_arg(0), a1 = me_arg(1), a2 = me_arg(2), a3 = me_arg(3);

			for (k = 0; k < 16; k++)
				me_res(k) = 0;
			if (a2 > WIN_WORDS)
				a2 = WIN_WORDS;
			if (op == OP_HALT)
			{
				me_halted = 1;
				me_done = sequence;
				meLibHalt();
				continue;
			}
			if (op == OP_FILL)
				for (k = 0; k < a2; k++)
					edram(a0)[k] = a1 ? pattern(a1, a3 + k) : 0;
			else if (op == OP_MATCH)
				for (k = 0; k < a2; k++)
				{
					u32 w = edram(a0)[k];

					me_res(0) += w == pattern(a1, a3 + k);
					me_res(1) += w == 0;
				}
		}
		meLibDcacheWritebackInvalidateAll();
		me_done = sequence;
	}
}

/* the main CPU's side */
static u32 me_sequence_sent = 0;

/* 1 when the ME has done the op; 0 after 2 s, and the ME is taken as gone */
static int me_call(u32 op, u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 start = sceKernelGetSystemTimeLow();

	if (!me_ok)
		return 0;
	me_op = op;
	me_arg(0) = a0;
	me_arg(1) = a1;
	me_arg(2) = a2;
	me_arg(3) = a3;
	__asm__ volatile("sync");
	me_sequence = ++me_sequence_sent;
	while (me_done != me_sequence_sent)
	{
		if (sceKernelGetSystemTimeLow() - start > 2000000u)
		{
			out("the ME didn't answer op %lu in 2 s: the cells that need it stop here", (unsigned long)op);
			me_ok = 0;
			return 0;
		}
		sceKernelDelayThread(100);
	}
	return 1;
}

/* ============================================================ kernel side (kcall) */
#define DMACP 0xBC800000u
#define DMACP_SC2ME 0x180u
#define DMACP_ME2SC 0x1A0u
#define DMACP_SC2SC 0x1C0u
#define WIDTH16 4u
#define SYSTIME hw(0xBC600000u) /* 1 MHz once the kernel's prescaler is set */
#define DMACP_BOUND_US 2000u
#define POLL_BOUND 2000000u
#define SPIN_BOUND 4000000u

/* kernel mode only: user mode can't read CP0 */
static inline u32 cpu_cycles(void)
{
	u32 count;

	__asm__ __volatile__("mfc0 %0, $9" : "=r"(count));
	return count;
}

static u32 dmacp_bit(u32 channel)
{
	return channel == DMACP_SC2ME ? 0x04u : channel == DMACP_ME2SC ? 0x08u : 0x10u;
}

/* the control word in halo_spadprobe's layout, with the bursts and the end
interrupt open: dst_burst sits at 15, src_burst at 12 */
static u32 dmacp_control(int dst_me, int src_me, int increments, int end_interrupt, u32 dst_burst, u32 src_burst, u32 count)
{
	return (end_interrupt ? 1u << 31 : 0) | (increments ? (1u << 27) | (1u << 26) : 0) | ((u32)dst_me << 25) |
		((u32)src_me << 24) | (WIDTH16 << 21) | (WIDTH16 << 18) | ((dst_burst & 7u) << 15) | ((src_burst & 7u) << 12) |
		(count & 0xFFFu);
}

/* the probe's default: the end interrupt on, burst 1 both sides */
static u32 control_default(u32 channel, u32 bytes)
{
	if (channel == DMACP_ME2SC)
		return dmacp_control(0, 1, 0, 1, 1, 1, bytes / 16);
	if (channel == DMACP_SC2ME)
		return dmacp_control(1, 0, 0, 1, 1, 1, bytes / 16);
	return dmacp_control(0, 0, 1, 1, 1, 1, bytes / 16);
}

struct DDesc
{
	u32 src, dst, next, ctrl, status;
} __attribute__((aligned(16)));

static struct DDesc chain[4] __attribute__((aligned(64)));

#define MAX_STARTS 4
struct DJob
{
	u32 channel, starts;
	u32 src[MAX_STARTS], dst[MAX_STARTS], next[MAX_STARTS], control[MAX_STARTS];
	u32 spin; /* cycles from the first start to its first poll: 0 polls at once */
	/* out */
	int result; /* 0 ended, -1 busy before, -2 the controller off, -3 past the bound (stopped), -4 the error bit, -5 refused */
	u32 c_start;      /* the first start's register writes, to their sync */
	u32 c_total;      /* the first write to the poll that saw the last start idle */
	u32 c_after_spin; /* the spin's end to that poll */
	u32 polls, us;
	u32 status_after, lli_after, raw_tc, raw_err, enabled, config;
};

static struct DJob job __attribute__((aligned(64)));

/* the transfers, start to end, with interrupts off: the starts run in a row,
each waited to its end before the next, as a caller without chains would */
static int djob_kernel(void *param)
{
	struct DJob *j = (struct DJob *)param;
	u32 base = DMACP + j->channel, bit = dmacp_bit(j->channel);
	u32 c0, c_spin = 0, c_end, t0, s, n;
	int interrupts;

	j->config = hw(DMACP + 0x30);
	j->enabled = hw(DMACP + 0x1C);
	/* the enable bit reads 0 while the display's channel runs: a channel
	running is the proof it's on (psp_me.c) */
	if (!(j->config & 1) && !j->enabled)
		return j->result = -2;
	if (hw(base + 0x10) & 1)
		return j->result = -1;
	interrupts = sceKernelCpuSuspendIntr();
	hw(DMACP + 0x08) = bit;
	hw(DMACP + 0x10) = bit;
	__asm__ __volatile__("sync");
	j->result = 0;
	j->polls = 0;
	t0 = SYSTIME;
	c0 = cpu_cycles();
	for (s = 0; s < j->starts && !j->result; s++)
	{
		hw(base + 0x00) = j->src[s];
		hw(base + 0x04) = j->dst[s];
		hw(base + 0x08) = j->next[s];
		hw(base + 0x0C) = j->control[s];
		hw(base + 0x10) = 1;
		__asm__ __volatile__("sync");
		if (s == 0)
		{
			j->c_start = cpu_cycles() - c0;
			/* work that keeps off the bus (a CP0 read is the CPU's own), so
			the transfer runs as it would beside the CPU's cached work */
			n = 0;
			while (j->spin && cpu_cycles() - c0 < j->spin && ++n < SPIN_BOUND)
				;
			c_spin = cpu_cycles();
		}
		n = 0;
		while (hw(base + 0x10) & 1)
		{
			n++;
			if ((!(n & 255) && SYSTIME - t0 > DMACP_BOUND_US) || n > POLL_BOUND)
			{
				hw(base + 0x10) = 0;
				__asm__ __volatile__("sync");
				j->result = -3;
				break;
			}
		}
		j->polls += n;
	}
	c_end = cpu_cycles();
	j->us = SYSTIME - t0;
	j->c_total = c_end - c0;
	j->c_after_spin = c_end - c_spin;
	j->status_after = hw(base + 0x10);
	j->lli_after = hw(base + 0x08);
	j->raw_tc = hw(DMACP + 0x14);
	j->raw_err = hw(DMACP + 0x18);
	if (!j->result && (j->raw_err & bit))
		j->result = -4;
	hw(DMACP + 0x08) = bit;
	hw(DMACP + 0x10) = bit;
	__asm__ __volatile__("sync");
	sceKernelCpuResumeIntrWithSync(interrupts);
	return 0;
}

static int run_job(void)
{
	u32 s;

	for (s = 0; s < job.starts; s++)
		if (!engine_address_ok(job.src[s]) || !engine_address_ok(job.dst[s]) || !engine_address_ok(job.next[s]))
			return job.result = -5;
	sceKernelDcacheWritebackInvalidateAll();
	kcall_3(djob_kernel, CACHED_KERNEL_MASK, &job);
	sceKernelDcacheWritebackInvalidateAll();
	return job.result;
}

static void job_single(u32 channel, u32 src, u32 dst, u32 control, u32 spin)
{
	memset(&job, 0, sizeof(job));
	job.channel = channel;
	job.starts = 1;
	job.src[0] = src;
	job.dst[0] = dst;
	job.control[0] = control;
	job.spin = spin;
}

static const char *dresult(int result)
{
	return result == 0 ? "ended" : result == -1 ? "the channel busy before" : result == -2 ? "the controller off" :
		result == -3 ? "DID NOT END in 2 ms (stopped)" : result == -4 ? "ended with the error bit" :
		result == -5 ? "refused: a kernel-segment address" : "?";
}

/* the counter's rate, against the system timer over 2 ms with interrupts off */
struct Calib
{
	u32 cycles, us;
};
static struct Calib calib __attribute__((aligned(64)));

static int calib_kernel(void *param)
{
	struct Calib *c = (struct Calib *)param;
	int interrupts = sceKernelCpuSuspendIntr();
	u32 t0 = SYSTIME, c0 = cpu_cycles(), t;

	while ((t = SYSTIME) - t0 < 2000u)
		;
	c->cycles = cpu_cycles() - c0;
	c->us = t - t0;
	sceKernelCpuResumeIntrWithSync(interrupts);
	return 0;
}

static int null_kernel(void *param)
{
	(void)param;
	return 0;
}

static double cycles_per_us = 0; /* 0 when the counter doesn't run: then us only */

static double to_us(u32 cycles)
{
	return cycles_per_us > 0 ? cycles / cycles_per_us : 0;
}

/* ============================================================ the rows */
static u32 *ram; /* 8 KB, 64-aligned: Sc2Sc's source */
static u32 seed_next = 1;

static u32 median3(const u32 *v)
{
	u32 a = v[0], b = v[1], c = v[2];

	if (a > b)
	{
		u32 t = a;
		a = b;
		b = t;
	}
	if (b > c)
		b = c;
	return a > b ? a : b;
}

#define REPEATS 3

struct Row
{
	u32 cycles[REPEATS], us[REPEATS], polls[REPEATS], start[REPEATS];
	u32 right, words, overrun; /* summed over the repeats */
	int result;                /* the first that wasn't 0 */
};

static void row_take(struct Row *r, int n)
{
	r->cycles[n] = job.c_total;
	r->us[n] = job.us;
	r->polls[n] = job.polls;
	r->start[n] = job.c_start;
	if (!r->result)
		r->result = job.result;
}

static void row_print(const char *label, const struct Row *r)
{
	u32 m = median3(r->cycles);

	out("%s: %s; cycles %lu %lu %lu (median %lu, %.2f us), system timer %lu %lu %lu us, polls %lu %lu %lu, start %lu %lu %lu cycles; "
		"%lu of %lu words right, %lu past the end changed%s",
		label, dresult(r->result), (unsigned long)r->cycles[0], (unsigned long)r->cycles[1], (unsigned long)r->cycles[2],
		(unsigned long)m, to_us(m), (unsigned long)r->us[0], (unsigned long)r->us[1], (unsigned long)r->us[2],
		(unsigned long)r->polls[0], (unsigned long)r->polls[1], (unsigned long)r->polls[2], (unsigned long)r->start[0],
		(unsigned long)r->start[1], (unsigned long)r->start[2], (unsigned long)r->right, (unsigned long)r->words,
		(unsigned long)r->overrun, r->right == r->words && !r->overrun && !r->result ? ": EXACT" : ": NOT EXACT");
}

/* one transfer of bytes into the scratchpad's window from eDRAM (Me2Sc) or
main RAM (Sc2Sc), checked word by word and past its end */
static int into_spad(u32 channel, u32 bytes, u32 control, u32 spin, struct Row *r, int n)
{
	u32 seed = seed_next += 2, words = bytes / 4, src;

	spad_fill(seed + 1000, WIN_WORDS);
	if (channel == DMACP_ME2SC)
	{
		if (!me_call(OP_FILL, EDRAM_SRC, seed, words, 0))
			return 0;
		src = EDRAM_SRC;
	}
	else
	{
		u32 k;

		for (k = 0; k < words; k++)
			ram[k] = pattern(seed, k);
		src = phys(ram);
	}
	job_single(channel, src, SPAD_WIN, control, spin);
	run_job();
	row_take(r, n);
	r->right += spad_matches(seed, 0, words);
	r->words += words;
	r->overrun += (WIN_WORDS - words) - spad_matches(seed + 1000, words, WIN_WORDS - words);
	return 1;
}

/* the scratchpad's window to eDRAM (Sc2Me), checked by the ME */
static int out_of_spad(u32 bytes, struct Row *r, int n)
{
	u32 seed = seed_next += 2, words = bytes / 4;

	if (!me_call(OP_FILL, EDRAM_DST, 0, WIN_WORDS, 0))
		return 0;
	spad_fill(seed, words);
	job_single(DMACP_SC2ME, SPAD_WIN, EDRAM_DST, control_default(DMACP_SC2ME, bytes), 0);
	run_job();
	row_take(r, n);
	if (!me_call(OP_MATCH, EDRAM_DST, seed, words, 0))
		return 0;
	r->right += me_res(0);
	r->words += words;
	if (words < WIN_WORDS)
	{
		if (!me_call(OP_MATCH, EDRAM_DST + bytes, seed, WIN_WORDS - words, words))
			return 0;
		r->overrun += (WIN_WORDS - words) - me_res(1);
	}
	return 1;
}

static const u32 sizes[4] = {1024u, 2048u, 4096u, 8192u};

/* a straight line through the four medians: the start cost is where it
meets 0 bytes, the rate its slope */
static void fit_print(const char *label, const u32 *median)
{
	double sx = 0, sy = 0, sxx = 0, sxy = 0, slope, intercept;
	int i;

	for (i = 0; i < 4; i++)
	{
		double x = sizes[i] / 1024.0, y = median[i];

		sx += x;
		sy += y;
		sxx += x * x;
		sxy += x * y;
	}
	slope = (4 * sxy - sx * sy) / (4 * sxx - sx * sx);
	intercept = (sy - slope * sx) / 4;
	out("%s fit: start %.0f cycles (%.2f us), then %.0f cycles a KB (%.2f us a KB, %.0f MB/s); medians %lu %lu %lu %lu for 1/2/4/8 KB",
		label, intercept, cycles_per_us > 0 ? intercept / cycles_per_us : 0.0, slope, cycles_per_us > 0 ? slope / cycles_per_us : 0.0,
		cycles_per_us > 0 && slope > 0 ? 1024.0 * cycles_per_us / slope : 0.0, (unsigned long)median[0], (unsigned long)median[1],
		(unsigned long)median[2], (unsigned long)median[3]);
}

/* ============================================================ the cells */
static void cell_clock(void)
{
	u32 k, t0, t1;

	out("cell 0, the counter's rate against the system timer: calling");
	kcall_3(calib_kernel, CACHED_KERNEL_MASK, &calib);
	if (calib.cycles && calib.us)
		cycles_per_us = (double)calib.cycles / calib.us;
	out("cell 0, the counter's rate: %lu cycles over %lu us of the system timer: %.2f cycles a us%s", (unsigned long)calib.cycles,
		(unsigned long)calib.us, cycles_per_us,
		cycles_per_us > 0 ? "" : ": THE COUNTER DOESN'T RUN; every figure below is the system timer's us only");
	out("cell 0, a null kernel call (200 in a row, the user side's clock): calling");
	t0 = sceKernelGetSystemTimeLow();
	for (k = 0; k < 200; k++)
		kcall_3(null_kernel, CACHED_KERNEL_MASK, &calib);
	t1 = sceKernelGetSystemTimeLow();
	out("cell 0, a null kernel call: %.2f us each (200 in %lu us): what a start and a later poll in separate calls add, each",
		(t1 - t0) / 200.0, (unsigned long)(t1 - t0));
}

static void cell_sizes(int cell, u32 channel, const char *name, u32 *medians)
{
	int i, n;

	for (i = 0; i < 4; i++)
	{
		struct Row r;
		char label[120];

		memset(&r, 0, sizeof(r));
		snprintf(label, sizeof(label), "cell %d, %s, %lu KB", cell, name, (unsigned long)(sizes[i] / 1024));
		out("%s: calling", label);
		for (n = 0; n < REPEATS; n++)
			if (!(channel == DMACP_SC2ME ? out_of_spad(sizes[i], &r, n) :
					into_spad(channel, sizes[i], control_default(channel, sizes[i]), 0, &r, n)))
			{
				out("%s: stopped (the ME's side didn't answer)", label);
				return;
			}
		row_print(label, &r);
		medians[i] = median3(r.cycles);
	}
	fit_print(name, medians);
}

static void cell_unwaited(u32 waited)
{
	const u32 spins[3] = {0, waited / 2, waited * 2};
	int s, n;

	for (s = 0; s < 3; s++)
	{
		struct Row r;
		u32 after[REPEATS];
		char label[160];

		memset(&r, 0, sizeof(r));
		snprintf(label, sizeof(label), "cell 4, Me2Sc 4 KB, a start not waited on, the first poll %lu cycles after it",
			(unsigned long)spins[s]);
		out("%s: calling", label);
		for (n = 0; n < REPEATS; n++)
		{
			if (!into_spad(DMACP_ME2SC, 4096, control_default(DMACP_ME2SC, 4096), spins[s], &r, n))
			{
				out("%s: stopped (the ME's side didn't answer)", label);
				return;
			}
			after[n] = job.c_after_spin;
		}
		row_print(label, &r);
		out("%s: the start %lu %lu %lu cycles; from the spin's end to the poll that saw it idle %lu %lu %lu cycles (%.2f us median), "
			"polls %lu %lu %lu",
			label, (unsigned long)r.start[0], (unsigned long)r.start[1], (unsigned long)r.start[2], (unsigned long)after[0],
			(unsigned long)after[1], (unsigned long)after[2], to_us(median3(after)), (unsigned long)r.polls[0],
			(unsigned long)r.polls[1], (unsigned long)r.polls[2]);
	}
}

static void cell_bursts(void)
{
	/* scratchpad side (the destination, bits 15-17), the ME's side (12-14) */
	const u32 dst_burst[3] = {1, 0, 2}, src_burst[3] = {1, 0, 1};
	const char *const names[3] = {"1 both sides (the probe's default)", "0 both sides",
		"2 on the scratchpad's side, 1 on the ME's"};
	int b, n;

	for (b = 0; b < 3; b++)
	{
		struct Row r;
		char label[160];
		u32 control = dmacp_control(0, 1, 0, 1, dst_burst[b], src_burst[b], 4096 / 16);

		memset(&r, 0, sizeof(r));
		snprintf(label, sizeof(label), "cell 3, Me2Sc 4 KB, burst %s, control %08lx", names[b], (unsigned long)control);
		out("%s: calling", label);
		for (n = 0; n < REPEATS; n++)
			if (!into_spad(DMACP_ME2SC, 4096, control, 0, &r, n))
			{
				out("%s: stopped (the ME's side didn't answer)", label);
				return;
			}
		row_print(label, &r);
	}
}

/* four 1 KB blocks from eDRAM into one contiguous 4 KB of the scratchpad,
as four starts or as one chain; scattered takes them from four places.
mcidclan's sample: the end interrupt clear in every descriptor, status 1 */
static int four_blocks(int as_chain, int scatter, struct Row *r, int n)
{
	u32 seed = seed_next += 2, i, control = dmacp_control(0, 1, 0, 0, 1, 1, BLOCK / 16), src[4];

	spad_fill(seed + 1000, WIN_WORDS);
	for (i = 0; i < 4; i++)
		src[i] = scatter ? scattered[i] : EDRAM_SRC + i * BLOCK;
	/* each block gets its part of one pattern, so 4 KB in order is right only
	if every block landed in its place */
	for (i = 0; i < 4; i++)
		if (!me_call(OP_FILL, src[i], seed, BLOCK / 4, i * (BLOCK / 4)))
			return 0;
	memset(&job, 0, sizeof(job));
	job.channel = DMACP_ME2SC;
	if (as_chain)
	{
		for (i = 0; i < 4; i++)
		{
			chain[i].src = src[i];
			chain[i].dst = SPAD_WIN + i * BLOCK;
			chain[i].next = i < 3 ? phys(&chain[i + 1]) : 0;
			chain[i].ctrl = control;
			chain[i].status = 1;
			if (!engine_address_ok(chain[i].src) || !engine_address_ok(chain[i].dst) || !engine_address_ok(chain[i].next))
			{
				job.result = -5;
				row_take(r, n);
				return 1;
			}
		}
		/* the channel loaded with the first descriptor's words, as the sample
		loads it; run_job writes the descriptors back before the engine reads them */
		job.starts = 1;
		job.src[0] = chain[0].src;
		job.dst[0] = chain[0].dst;
		job.next[0] = chain[0].next;
		job.control[0] = chain[0].ctrl;
	}
	else
	{
		job.starts = 4;
		for (i = 0; i < 4; i++)
		{
			job.src[i] = src[i];
			job.dst[i] = SPAD_WIN + i * BLOCK;
			job.control[i] = control;
		}
	}
	run_job();
	row_take(r, n);
	r->right += spad_matches(seed, 0, 4 * BLOCK / 4);
	r->words += 4 * BLOCK / 4;
	r->overrun += (WIN_WORDS - 4 * BLOCK / 4) - spad_matches(seed + 1000, 4 * BLOCK / 4, WIN_WORDS - 4 * BLOCK / 4);
	return 1;
}

static void cell_chains(void)
{
	const char *const names[4] = {"four starts in a row, contiguous", "four starts in a row, gathering four scattered blocks",
		"one chain of four descriptors, contiguous", "one chain of four descriptors, gathering four scattered blocks"};
	int c, n;

	for (c = 0; c < 4; c++)
	{
		struct Row r;
		char label[160];
		int as_chain = c >= 2, scatter = c & 1;

		memset(&r, 0, sizeof(r));
		snprintf(label, sizeof(label), "cell 2, Me2Sc 4 x 1 KB, %s", names[c]);
		out("%s: calling", label);
		for (n = 0; n < REPEATS; n++)
		{
			if (!four_blocks(as_chain, scatter, &r, n))
			{
				out("%s: stopped (the ME's side didn't answer)", label);
				return;
			}
			if (as_chain && job.result)
				break; /* a chain that didn't end isn't asked again */
		}
		row_print(label, &r);
		if (as_chain)
			out("%s: after the last run the channel's next-descriptor word %08lx, status %08lx, raw tc %08lx err %08lx", label,
				(unsigned long)job.lli_after, (unsigned long)job.status_after, (unsigned long)job.raw_tc,
				(unsigned long)job.raw_err);
	}
}

/* ============================================================ the run */
int main(void)
{
	SceCtrlData pad;
	u32 k, me2sc[4] = {0, 0, 0, 0}, sc2me[4] = {0, 0, 0, 0}, sc2sc[4] = {0, 0, 0, 0};

	fpu_mask_traps();
	pspDebugScreenInit();
	sceIoMkdir("ms0:/halo", 0777);
	sceIoMkdir("ms0:/halo/log", 0777);
	sceIoRemove(log_path);
	out("==== halo_dmacbench: DMACplus to and from the scratchpad (0x%08x, 8 KB): start cost, chains, bursts ====", SPAD_WIN);
	ram = (u32 *)memalign(64, WIN_BYTES);
	if (!ram)
	{
		out("could not allocate");
		goto done;
	}

	{
		u32 alive;
		int table = meLibDefaultInit();

		/* a table number, or -6 (no table for this ME image): the kernel module
		loaded and kcall works; -3 to -5 are the module or kinit failing */
		kernel_ok = table >= 0 || table == -6;
		alive = me_alive;
		me_ok = table >= 2;
		sceKernelDelayThread(200000);
		if (me_ok && me_alive == alive)
			me_ok = 0;
		out("meLibDefaultInit: %d; kernel calls %s; the ME %s", table, kernel_ok ? "available" : "NOT available",
			me_ok ? "running" : "NOT running (the eDRAM cells won't run)");
	}
	if (!kernel_ok)
	{
		out("no kernel calls: no cell can run");
		goto done;
	}

	cell_clock();
	cell_sizes(5, DMACP_SC2SC, "main RAM -> the scratchpad (Sc2Sc)", sc2sc);
	if (me_ok)
		cell_sizes(1, DMACP_ME2SC, "eDRAM -> the scratchpad (Me2Sc)", me2sc);
	if (me_ok)
		cell_sizes(1, DMACP_SC2ME, "the scratchpad -> eDRAM (Sc2Me)", sc2me);
	if (me_ok)
		cell_unwaited(me2sc[2]);
	if (me_ok)
		cell_bursts();
	if (me_ok)
		cell_chains();
	if (!me_ok)
		out("the ME's side wasn't up (or stopped answering): the eDRAM cells that hadn't run didn't");
	out("not built: a chain on Sc2Sc (main RAM gathered into the scratchpad): the one source for the descriptor (mcidclan's sample) ran it on Me2Sc only, and the lab's earlier chain hung a PSP, so this asks the proven channel first");

	if (me_ok)
	{
		me_op = OP_HALT;
		__asm__ volatile("sync");
		me_sequence = ++me_sequence_sent;
		for (k = 0; k < 20 && !me_halted; k++)
			sceKernelDelayThread(50000);
		out("the ME %s", me_halted ? "stopped cleanly" : "did NOT acknowledge the stop");
	}
done:
	out("done");
	/* a short pause so a hand run can read the screen; bounded, so a rig run
	never waits on a button nobody presses */
	for (k = 0; k < 20; k++)
	{
		sceCtrlPeekBufferPositive(&pad, 1);
		if (pad.Buttons & PSP_CTRL_CROSS)
			break;
		sceKernelDelayThread(100000);
	}
	out("exiting");
	sceKernelExitGame();
	return 0;
}
