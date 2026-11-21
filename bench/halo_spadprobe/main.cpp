/*
halo_spadprobe v2: can any engine but the main CPU write or read its 16 KB
scratchpad (0x00010000)? The lab's coord/briefs/spad_dma_probe.md, step 2.

v1 answered the system DMAC through sceDmacMemcpy (out of the scratchpad it
works from the low forms; into it nothing usable arrives) and the GE (it reads
vertices there). The firmware read found no setting that opens a bus-side
write path, so v2 asks the other bus masters. Each into-the-scratchpad cell
says whether the pattern arrived (read back by the CPU at 0x0001xxxx, which
the cache never holds: v1 saw 0x4001xxxx read 0 for the CPU), the engine's
registers after, and whether the pattern landed in one of the likely aliases
instead: main RAM at physical 0x08011000 (kernel memory, read, and put back only where an engine changed it, with
interrupts off around the DMACplus and VME cells; sceDmacMemcpy waits on an
interrupt, so its cells run with them on, as v1 did), VRAM at
0x04011000 and the ME's eDRAM at 0x11000 (the ME's own address 0 is eDRAM,
so a ME-side engine given 0x00011000 most likely means eDRAM). Each
out-of-the-scratchpad cell says whether the pattern reached the far side.

The cells, in the order they run (safest first, so a hang that stops the run
leaves the most behind it; each cell writes a "calling" line before it
starts):
  1. the GE reading a sprite's vertices from the scratchpad, four forms, and
     a RAM control (v1's cells, kept as controls);
  2. the ME started through me-core (as halo_bench12 starts it), which also
     loads the kernel-call module the register cells use;
  3. sceDmacMemcpy into and out of the scratchpad, four forms, and a RAM
     control (v1's cells, now with the alias scan and a count of what the
     words that didn't arrive hold);
  4. the VME's DMAC (0x440ff000), run by the ME: a 256-word buffer to
     memory (control 0x48) and memory to buffer (0x40), with the address
     register given each form as it is, and RAM controls both ways;
  5. DMACplus (0xBC800000) through its ME-side channels: eDRAM to the
     scratchpad (Me2Sc, 0x1A0) and the scratchpad to eDRAM (Sc2Me, 0x180),
     after controls RAM to eDRAM and back;
  6. DMACplus main bus to main bus (channel 0x1C0), after a RAM-to-RAM
     control: the scratchpad cells run only if the control passes, since
     this channel hasn't been run in the lab before.
Not built, each saying why on its line: the ME's second DMA unit, and the
system DMAC by its own registers.

A cell that reaches the scratchpad, whole or in part, is run once more in the
same run. Every wait is bounded: a DMACplus transfer 2 ms (then the channel is
stopped), the VME's DMAC ~60 ms of the ME's clock, the ME's answer 2 s; a
cell past its bound says so and the run goes on.

The hardware rules kept: the CPU's cache is written back before any engine
reads what it wrote, and dropped before it reads what an engine wrote. The
ME's code here is plain loads, stores and the VME's registers: no VFPU, no
system call, no library call (the build's -fno-builtin and
-fno-tree-loop-distribute-patterns keep loops from becoming memcpy). Kernel
work goes through me-core's kcall at the kernel's cached alias, as
psp/src/psp_me.c does (a kernel function run at its user address switched the
PSP off now and then). No power or clock call is made by the bench.

Register layouts and sources:
  - DMACplus: channel words +0x00 source, +0x04 destination, +0x08 next
    descriptor, +0x0C control, +0x10 status (bit 0 enables and reads busy);
    the controller's words 0x04/0x0C pending, 0x08/0x10 clear, 0x14/0x18 raw,
    0x1C enabled channels, 0x30 configuration (ARM PL080 order, as
    psp/src/psp_me.c reads them). The control word: bit 31 the end interrupt,
    27/26 the increments (set only main to main), 25/24 the masters (0 the
    main bus, 1 the ME's), the width code at 21 and 18 (4 = 16 bytes), the
    burst code at 15 and 12 (1: 2 stalls), the count in widths in 11-0.
    Raw interrupt bits: 2 for 0x180, 3 for 0x1A0 (measured, bench15); 4 for
    0x1C0 (psdevwiki's order, untested). From mcidclan's dmacplus.h
    (psp-media-engine-custom-core, MIT), psdevwiki's Hardware Registers page
    and docs/dma_engines.md section 3.
  - The VME's DMAC: 0x000 status (0x200 a transfer running), 0x004 config
    (bit 4 before the waits), 0x008 control (writing it starts), 0x010 the
    memory address, 0x014 the count less one, 0x018 the buffer word (0 is
    the first base buffer, at 0x44000000 on the ME). Buffer words are 24 bits
    sign-extended, so its patterns keep bit 23 and up clear. From mcidclan's
    vme-primary-dmac sample and psp-media-engine-cracking-the-unknown's
    0x440ff000 notes (MIT), docs/dma_engines.md section 2, and the waits and
    clocks of psp/src/psp_vme.c.
  - The ME's second DMA unit: psdevwiki's Media Engine page lists "DMA(2)" at
    the ME's 0x04100000 with its size and use unknown; no source gives its
    registers.

Results go to ms0:/halo/log/spadprobe.txt, one line a cell, written as it
ends. When done it writes "exiting" and quits by itself two seconds later (X
quits sooner by hand). The rig's launcher mode brings its agent back after a
program that quits, as it does after halo_box, so the run ends as "exited"
rather than at its wall; the "exiting" line is what tells that clean end
from a crash, since the rig reports both as a program that quit by itself.
*/

#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <psputils.h>
#include <pspgu.h>
#include <pspge.h>
#include <pspdmac.h>
#include <pspiofilemgr.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <me-core-mapper/me-core.h>

PSP_MODULE_INFO("halo_spadprobe", 0, 2, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(4 * 1024);

static const char *log_path = "ms0:/halo/log/spadprobe.txt";

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

/* the FPU's traps masked first, as the game masks them (psp_platform.c,
psp_fpu_mask_traps): an untaken FPU trap switches the PSP off */
static void fpu_mask_traps(void)
{
	unsigned int control;

	__asm__ __volatile__("cfc1 %0, $31" : "=r"(control));
	control &= ~0x00000F80u;
	control |= 0x01000000u;
	__asm__ __volatile__("ctc1 %0, $31" : : "r"(control));
}

/* ============================================================ places and patterns */
#define SPAD_PHYS 0x00010000u
#define SPAD_AT 0x1000u            /* the probe's 1 KB inside the 16 KB, as v1 */
#define PROBE (SPAD_PHYS + SPAD_AT) /* 0x00011000 */
#define CELL_BYTES 1024u
#define CELL_WORDS (CELL_BYTES / 4)

/* the aliases a lost write is looked for in */
#define ALIAS_KRAM_UNCACHED (0xA0000000u | 0x08000000u | PROBE) /* main RAM 0x08011000, kernel */
#define ALIAS_KRAM_CACHED (0x80000000u | 0x08000000u | PROBE)
#define ALIAS_VRAM_UNCACHED (0x40000000u | 0x04000000u | PROBE) /* VRAM 0x04011000 */
#define ALIAS_EDRAM PROBE                                       /* eDRAM 0x11000, the ME's side */

/* eDRAM the bench uses for itself: in the range the game's sound cache took
(0x40000-0x1E0000, psp_me.h), clear of the ME's stack at the top */
#define EDRAM_STAGE 0x100000u
#define EDRAM_OUT 0x110000u
#define EDRAM_SAVE 0x120000u

#define MASK32 0xFFFFFFFFu
#define MASK24 0x007FFFFFu /* the VME's words: bit 23 clear, so they come back as written */

static const u32 forms[4] = {0x00000000u, 0x40000000u, 0x80000000u, 0xA0000000u};
/* the engine cells (DMACplus, the VME's DMAC) take only the user forms: v2's
first run pointed DMACplus at 0x8001xxxx and hung a PSP-1000, and the VME's
DMAC at 0x80011000 hung the ME, as neither refuses a kernel segment */
#define ENGINE_FORMS 2
static int engine_address_ok(u32 address)
{
	return (address >> 28) == 0x0u || (address >> 28) == 0x4u;
}
static const char *const form_names[4] = {"0x0001xxxx", "0x4001xxxx", "0x8001xxxx", "0xA001xxxx"};

static inline u32 pattern(u32 seed, u32 k, u32 mask)
{
	return ((seed * 0x9E3779B1u) ^ (k * 0x85EBCA6Bu) ^ 0x5A5A0000u) & mask;
}

static volatile u32 *spad(void)
{
	return (volatile u32 *)PROBE;
}

static void fill_spad(u32 seed, u32 mask)
{
	u32 k;

	for (k = 0; k < CELL_WORDS; k++)
		spad()[k] = pattern(seed, k, mask);
}

static u32 count_matches(volatile const u32 *words, u32 seed, u32 mask)
{
	u32 k, right = 0;

	for (k = 0; k < CELL_WORDS; k++)
		right += words[k] == pattern(seed, k, mask);
	return right;
}

/* what the scratchpad holds after a write into it: the pattern's words (as
runs of indices when only some arrived), the old pattern's, zeros, and the
first few others by index and value (v1.1 on the PSP-1000: 240 words were
neither) */
static void describe_spad(char *text, int size, u32 seed, u32 mask, u32 old_seed)
{
	u32 k, got = 0, old = 0, zero = 0, ones = 0, other = 0, shown = 0;
	int length;
	char others[120];
	int olen = 0;

	others[0] = 0;
	for (k = 0; k < CELL_WORDS; k++)
	{
		u32 w = spad()[k];

		if (w == pattern(seed, k, mask))
			got++;
		else if (w == pattern(old_seed, k, MASK32))
			old++;
		else if (w == 0)
			zero++;
		else if (w == 0xFFFFFFFFu)
			ones++;
		else
		{
			other++;
			if (shown < 4 && olen < 100)
			{
				olen += snprintf(others + olen, sizeof(others) - olen, " %lu:%08lx", (unsigned long)k, (unsigned long)w);
				shown++;
			}
		}
	}
	length = snprintf(text, size, "the scratchpad: %lu of %u the pattern, %lu the old, %lu zero, %lu ffffffff, %lu other%s%s",
		(unsigned long)got, CELL_WORDS, (unsigned long)old, (unsigned long)zero, (unsigned long)ones, (unsigned long)other,
		other ? " (first:" : "", other ? others : "");
	if (other && length < size - 2)
		length += snprintf(text + length, size - length, ")");
	if (got && got < CELL_WORDS && length < size - 40)
	{
		u32 start = 0, in = 0;

		length += snprintf(text + length, size - length, "; the pattern's words:");
		for (k = 0; k <= CELL_WORDS && length < size - 16; k++)
		{
			int here = k < CELL_WORDS && spad()[k] == pattern(seed, k, mask);

			if (here && !in)
			{
				start = k;
				in = 1;
			}
			else if (!here && in)
			{
				length += snprintf(text + length, size - length, " %lu-%lu", (unsigned long)start, (unsigned long)(k - 1));
				in = 0;
			}
		}
	}
}

/* ============================================================ the Media Engine */

/* shared, uncached: the main CPU sets the op and its arguments, then the
sequence number; the ME does the op and puts the number in done */
meLibSetSharedUncached32(32);
#define me_sequence (meLibSharedMemory[0])
#define me_op (meLibSharedMemory[1])
#define me_arg(n) (meLibSharedMemory[2 + (n)])  /* 0-5 */
#define me_done (meLibSharedMemory[8])
#define me_alive (meLibSharedMemory[9])
#define me_halted (meLibSharedMemory[10])
#define me_res(n) (meLibSharedMemory[12 + (n)]) /* 0-15 */

enum
{
	OP_FILL = 1,     /* eDRAM a0 <- the pattern (seed a1, mask a2) */
	OP_MATCH,        /* eDRAM a0 against the pattern (seed a1, mask a2): res 0 matches, 1 zeros */
	OP_SAVE,         /* eDRAM a0 -> the save area */
	OP_CHECK,        /* eDRAM a0 against the pattern (seed a1, mask a2) and the save: res 0 matches, 1 changed; then put back */
	OP_VME,          /* the VME's DMAC: a0 1 buffer to memory, 0 memory to buffer; a1 the address; a2 the seed; a3 scan eDRAM's alias */
	OP_HALT
};

static int me_ok = 0, kernel_ok = 0;

static inline u32 me_clock(void)
{
	u32 count;

	__asm__ __volatile__("mfc0 %0, $9" : "=r"(count));
	return count;
}

static inline volatile u32 *edram(u32 offset)
{
	/* the ME's uncached view of its eDRAM */
	return (volatile u32 *)(0x40000000u | offset);
}

/* the VME's DMAC (the ME's side). Waits as psp/src/psp_vme.c's: the busy bit
may come and go before the first look, so the first loop is short and the
second is the bound; then the closing write it makes */
#define VDMAC(off) hw(0x440ff000u + (off))
#define VME_SEEN_BOUND 2000u
#define VME_CLEAR_BOUND 20000000u
#define VME_BUFFER ((volatile u32 *)0x44000000u)

static void vme_enable(void)
{
	/* read-modify-write: 0xbc100050 is shared (bit 0 is the ME's own bus clock) */
	hw(0xbc100050) |= 0x20;
	hw(0xbc100054) |= 1;
	__asm__ __volatile__("sync");
}

static void vme_config_transfer(void)
{
	VDMAC(0x000) |= 0x80;
	VDMAC(0x004) |= 0x10;
	__asm__ __volatile__("sync");
}

/* res: 2 status, 3 config, 4 address, 5 count, 6 buffer word (all after),
7 ME cycles, 8 1 if it never ended within the bound, 9 1 if busy never seen */
static void vme_transfer(u32 control, u32 address)
{
	u32 start, took;

	vme_config_transfer();
	VDMAC(0x010) = address;
	VDMAC(0x014) = CELL_WORDS - 1;
	VDMAC(0x018) = 0;
	start = me_clock();
	VDMAC(0x008) = control;
	__asm__ __volatile__("sync");
	while (!(VDMAC(0x000) & 0x200) && me_clock() - start < VME_SEEN_BOUND)
		;
	me_res(9) = (VDMAC(0x000) & 0x200) ? 0 : 1;
	while ((VDMAC(0x000) & 0x200) && me_clock() - start < VME_CLEAR_BOUND)
		;
	took = me_clock() - start;
	me_res(8) = (VDMAC(0x000) & 0x200) ? 1 : 0;
	me_res(2) = VDMAC(0x000);
	me_res(3) = VDMAC(0x004);
	me_res(4) = VDMAC(0x010);
	me_res(5) = VDMAC(0x014);
	me_res(6) = VDMAC(0x018);
	me_res(7) = took;
	VDMAC(0x000) = (0x200 >> 8) | 0x80;
	__asm__ __volatile__("sync");
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
			/* each poll is an uncached read on the bus: spaced, so the idle ME
			stays off it while an engine runs */
			me_alive++;
			for (k = 0; k < 2000; k++)
				__asm__ volatile("nop");
			continue;
		}
		seen = sequence;
		{
			u32 op = me_op, a0 = me_arg(0), a1 = me_arg(1), a2 = me_arg(2), a3 = me_arg(3);

			for (k = 0; k < 16; k++)
				me_res(k) = 0;
			if (op == OP_HALT)
			{
				me_halted = 1;
				me_done = sequence;
				meLibHalt();
				continue;
			}
			if (op == OP_FILL)
				for (k = 0; k < CELL_WORDS; k++)
					edram(a0)[k] = pattern(a1, k, a2);
			else if (op == OP_MATCH)
				for (k = 0; k < CELL_WORDS; k++)
				{
					u32 w = edram(a0)[k];

					me_res(0) += w == pattern(a1, k, a2);
					me_res(1) += w == 0;
				}
			else if (op == OP_SAVE)
				for (k = 0; k < CELL_WORDS; k++)
					edram(EDRAM_SAVE)[k] = edram(a0)[k];
			else if (op == OP_CHECK)
				for (k = 0; k < CELL_WORDS; k++)
				{
					u32 w = edram(a0)[k], was = edram(EDRAM_SAVE)[k];

					me_res(0) += w == pattern(a1, k, a2);
					me_res(1) += w != was;
					edram(a0)[k] = was;
				}
			else if (op == OP_VME)
			{
				vme_enable();
				if (a0)
				{
					/* buffer to memory: the pattern placed in the first base buffer
					by the ME's own stores, read back first so a store path that
					doesn't work isn't taken for the DMAC's */
					for (k = 0; k < CELL_WORDS; k++)
						VME_BUFFER[k] = pattern(a2, k, MASK24);
					for (k = 0; k < CELL_WORDS; k++)
						me_res(1) += VME_BUFFER[k] == pattern(a2, k, MASK24);
					if (a3)
						for (k = 0; k < CELL_WORDS; k++)
							edram(EDRAM_SAVE)[k] = edram(ALIAS_EDRAM)[k];
					vme_transfer(0x48, a1);
					if (a3)
						for (k = 0; k < CELL_WORDS; k++)
						{
							u32 w = edram(ALIAS_EDRAM)[k], was = edram(EDRAM_SAVE)[k];

							me_res(10) += w == pattern(a2, k, MASK24);
							me_res(11) += w != was;
							edram(ALIAS_EDRAM)[k] = was;
						}
				}
				else
				{
					/* memory to buffer: the buffer zeroed; eDRAM's alias given a
					pattern of its own, so a read of eDRAM instead is told apart */
					for (k = 0; k < CELL_WORDS; k++)
						VME_BUFFER[k] = 0;
					if (a3)
						for (k = 0; k < CELL_WORDS; k++)
						{
							edram(EDRAM_SAVE)[k] = edram(ALIAS_EDRAM)[k];
							edram(ALIAS_EDRAM)[k] = pattern(a2 + 2000, k, MASK24);
						}
					vme_transfer(0x40, a1);
					for (k = 0; k < CELL_WORDS; k++)
					{
						u32 w = VME_BUFFER[k];

						me_res(0) += w == pattern(a2, k, MASK24);
						me_res(1) += w == 0;
						me_res(10) += a3 && w == pattern(a2 + 2000, k, MASK24);
					}
					if (a3)
						for (k = 0; k < CELL_WORDS; k++)
							edram(ALIAS_EDRAM)[k] = edram(EDRAM_SAVE)[k];
				}
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

static u32 dmacp_bit(u32 channel)
{
	return channel == DMACP_SC2ME ? 0x04u : channel == DMACP_ME2SC ? 0x08u : 0x10u;
}

static u32 dmacp_control(int dst_me, int src_me, int increments, u32 count)
{
	return (1u << 31) | (increments ? (1u << 27) | (1u << 26) : 0) | ((u32)dst_me << 25) | ((u32)src_me << 24) |
		(WIDTH16 << 21) | (WIDTH16 << 18) | (1u << 15) | (1u << 12) | count;
}

struct KAlias
{
	u32 save[CELL_WORDS];
	u32 seed, mask;
	u32 matched, changed;
};

static inline void kram_writeback(void)
{
	u32 a;

	for (a = ALIAS_KRAM_CACHED; a < ALIAS_KRAM_CACHED + CELL_BYTES; a += 64)
		__asm__ __volatile__("cache 0x1b, 0(%0)" : : "r"(a));
	__asm__ __volatile__("sync");
}

static inline void kram_save(struct KAlias *k)
{
	u32 i;

	kram_writeback();
	for (i = 0; i < CELL_WORDS; i++)
		k->save[i] = ((volatile u32 *)ALIAS_KRAM_UNCACHED)[i];
}

static inline void kram_check_restore(struct KAlias *k)
{
	u32 i;

	k->matched = k->changed = 0;
	for (i = 0; i < CELL_WORDS; i++)
	{
		u32 w = ((volatile u32 *)ALIAS_KRAM_UNCACHED)[i];

		k->matched += w == pattern(k->seed, i, k->mask);
		/* kernel RAM is only read unless an engine changed it: the lead's
		ruling, so a cell that lands nowhere writes nothing there */
		if (w != k->save[i])
		{
			k->changed++;
			((volatile u32 *)ALIAS_KRAM_UNCACHED)[i] = k->save[i];
		}
	}
	__asm__ __volatile__("sync");
}

static int kram_save_kernel(void *param)
{
	int interrupts = sceKernelCpuSuspendIntr();

	kram_save((struct KAlias *)param);
	sceKernelCpuResumeIntrWithSync(interrupts);
	return 0;
}

static int kram_check_kernel(void *param)
{
	int interrupts = sceKernelCpuSuspendIntr();

	kram_check_restore((struct KAlias *)param);
	sceKernelCpuResumeIntrWithSync(interrupts);
	return 0;
}

/* a VME cell's op run with this CPU's interrupts off from the kernel-RAM
save to its check, so nothing here runs on kernel RAM an engine may have
just written; the wait polls the system timer, as no syscall may run */
struct QuietCall
{
	u32 op, a[4];
	int scan_kram;
	int done;
	u32 us;
};
static struct QuietCall quiet;
static struct KAlias kalias __attribute__((aligned(64))); /* the scans' kernel-RAM window */
#define QUIET_BOUND_US 50000u

static int me_call_quiet_kernel(void *param)
{
	struct QuietCall *q = (struct QuietCall *)param;
	int interrupts = sceKernelCpuSuspendIntr();
	u32 start;

	if (q->scan_kram)
		kram_save(&kalias);
	me_op = q->op;
	me_arg(0) = q->a[0];
	me_arg(1) = q->a[1];
	me_arg(2) = q->a[2];
	me_arg(3) = q->a[3];
	__asm__ volatile("sync");
	me_sequence = ++me_sequence_sent;
	start = SYSTIME;
	while (me_done != me_sequence_sent && SYSTIME - start < QUIET_BOUND_US)
		;
	q->done = me_done == me_sequence_sent;
	q->us = SYSTIME - start;
	if (q->scan_kram)
		kram_check_restore(&kalias);
	sceKernelCpuResumeIntrWithSync(interrupts);
	return 0;
}

struct DCell
{
	u32 channel, src, dst, control;
	int scan_kram;
	struct KAlias kram;
	/* out */
	int result; /* 0 done, -1 busy before, -2 the controller off, -3 timed out (stopped), -4 an error raised */
	u32 us;
	u32 src_after, dst_after, lli_after, control_after, status_after;
	u32 pending_tc, pending_err, raw_tc, raw_err, enabled, config;
};

/* one transfer, start to end, with interrupts off: the kernel-RAM alias is
saved, checked and put back before anything else on this CPU can run */
static int dcell_kernel(void *param)
{
	struct DCell *c = (struct DCell *)param;
	u32 base = DMACP + c->channel, bit = dmacp_bit(c->channel), start;
	int interrupts;

	c->config = hw(DMACP + 0x30);
	c->enabled = hw(DMACP + 0x1C);
	/* the enable bit reads 0 while the display's channel runs: a channel
	running is the proof it's on (psp_me.c) */
	if (!(c->config & 1) && !c->enabled)
		return c->result = -2;
	if (hw(base + 0x10) & 1)
		return c->result = -1;
	interrupts = sceKernelCpuSuspendIntr();
	if (c->scan_kram)
		kram_save(&c->kram);
	hw(DMACP + 0x08) = bit;
	hw(DMACP + 0x10) = bit;
	hw(base + 0x00) = c->src;
	hw(base + 0x04) = c->dst;
	hw(base + 0x08) = 0;
	hw(base + 0x0C) = c->control;
	hw(base + 0x10) = 1;
	__asm__ __volatile__("sync");
	start = SYSTIME;
	c->result = 0;
	while (hw(base + 0x10) & 1)
		if (SYSTIME - start > DMACP_BOUND_US)
		{
			hw(base + 0x10) = 0;
			__asm__ __volatile__("sync");
			c->result = -3;
			break;
		}
	c->us = SYSTIME - start;
	c->src_after = hw(base + 0x00);
	c->dst_after = hw(base + 0x04);
	c->lli_after = hw(base + 0x08);
	c->control_after = hw(base + 0x0C);
	c->status_after = hw(base + 0x10);
	c->pending_tc = hw(DMACP + 0x04);
	c->pending_err = hw(DMACP + 0x0C);
	c->raw_tc = hw(DMACP + 0x14);
	c->raw_err = hw(DMACP + 0x18);
	if (!c->result && (c->raw_err & bit))
		c->result = -4;
	hw(DMACP + 0x08) = bit;
	hw(DMACP + 0x10) = bit;
	__asm__ __volatile__("sync");
	if (c->scan_kram)
		kram_check_restore(&c->kram);
	sceKernelCpuResumeIntrWithSync(interrupts);
	return 0;
}

static struct DCell dcell __attribute__((aligned(64)));

static int run_dcell(u32 channel, u32 src, u32 dst, u32 control, int scan_kram, u32 seed, u32 mask)
{
	if (!engine_address_ok(src) || !engine_address_ok(dst))
		return -5; /* refused here: the engine itself wouldn't refuse it */
	memset(&dcell, 0, sizeof(dcell));
	dcell.channel = channel;
	dcell.src = src;
	dcell.dst = dst;
	dcell.control = control;
	dcell.scan_kram = scan_kram;
	dcell.kram.seed = seed;
	dcell.kram.mask = mask;
	sceKernelDcacheWritebackInvalidateAll();
	kcall_3(dcell_kernel, CACHED_KERNEL_MASK, &dcell);
	sceKernelDcacheWritebackInvalidateAll();
	return dcell.result;
}

static const char *dresult(int result)
{
	return result == 0 ? "ended" : result == -1 ? "the channel busy before" : result == -2 ? "the controller off" :
		result == -3 ? "DID NOT END in 2 ms (stopped)" : result == -4 ? "ended with the error bit" :
		result == -5 ? "refused: a kernel-segment address" : "?";
}

static void dregs(char *text, int size)
{
	snprintf(text, size, "src %08lx dst %08lx lli %08lx control %08lx status %08lx; pending tc %08lx err %08lx, raw tc %08lx err %08lx; enabled %08lx config %08lx; %lu us",
		(unsigned long)dcell.src_after, (unsigned long)dcell.dst_after, (unsigned long)dcell.lli_after,
		(unsigned long)dcell.control_after, (unsigned long)dcell.status_after, (unsigned long)dcell.pending_tc,
		(unsigned long)dcell.pending_err, (unsigned long)dcell.raw_tc, (unsigned long)dcell.raw_err,
		(unsigned long)dcell.enabled, (unsigned long)dcell.config, (unsigned long)dcell.us);
}

/* ============================================================ the alias scan around a write */
static u32 vram_save[CELL_WORDS];

struct Scan
{
	int kram_here, edram_here; /* this scan does the alias (else the engine's own call does) */
	int edram_saved;
	u32 vram_pattern, vram_changed;
	u32 kram_pattern, kram_changed;
	int kram_done;
	u32 edram_pattern, edram_changed;
	int edram_done;
};

/* me_call with this CPU's interrupts off (a VME cell); falls back to nothing
if the kernel calls aren't there, so the cell says it didn't run */
static int me_call_quiet(u32 op, u32 a0, u32 a1, u32 a2, u32 a3, int scan_kram)
{
	if (op == OP_VME && !engine_address_ok(a1))
		return 0;
	if (!me_ok || !kernel_ok)
		return 0;
	quiet.op = op;
	quiet.a[0] = a0;
	quiet.a[1] = a1;
	quiet.a[2] = a2;
	quiet.a[3] = a3;
	quiet.scan_kram = scan_kram;
	quiet.done = 0;
	kcall_3(me_call_quiet_kernel, CACHED_KERNEL_MASK, &quiet);
	if (!quiet.done)
	{
		out("the ME didn't answer op %lu in %lu us with interrupts off: the cells that need it stop here", (unsigned long)op,
			(unsigned long)quiet.us);
		me_ok = 0;
	}
	return quiet.done;
}

static void scan_before(struct Scan *s, int kram_here, int edram_here)
{
	u32 k;

	memset(s, 0, sizeof(*s));
	s->kram_here = kram_here && kernel_ok;
	s->edram_here = edram_here && me_ok;
	for (k = 0; k < CELL_WORDS; k++)
		vram_save[k] = ((volatile u32 *)ALIAS_VRAM_UNCACHED)[k];
	if (s->kram_here)
	{
		kcall_3(kram_save_kernel, CACHED_KERNEL_MASK, &kalias);
		s->kram_done = 1;
	}
	if (s->edram_here)
		s->edram_saved = me_call(OP_SAVE, ALIAS_EDRAM, 0, 0, 0);
}

static void scan_after(struct Scan *s, u32 seed, u32 mask)
{
	u32 k;

	for (k = 0; k < CELL_WORDS; k++)
	{
		u32 w = ((volatile u32 *)ALIAS_VRAM_UNCACHED)[k];

		s->vram_pattern += w == pattern(seed, k, mask);
		s->vram_changed += w != vram_save[k];
		((volatile u32 *)ALIAS_VRAM_UNCACHED)[k] = vram_save[k];
	}
	if (s->kram_here)
	{
		kalias.seed = seed;
		kalias.mask = mask;
		kcall_3(kram_check_kernel, CACHED_KERNEL_MASK, &kalias);
		s->kram_pattern = kalias.matched;
		s->kram_changed = kalias.changed;
	}
	if (s->edram_here && s->edram_saved && me_call(OP_CHECK, ALIAS_EDRAM, seed, mask, 0))
	{
		s->edram_pattern = me_res(0);
		s->edram_changed = me_res(1);
		s->edram_done = 1;
	}
}

static void scan_text(const struct Scan *s, char *text, int size)
{
	int n = snprintf(text, size, "aliases (pattern/changed of 256): VRAM 0x04011000 %lu/%lu", (unsigned long)s->vram_pattern,
		(unsigned long)s->vram_changed);

	if (s->kram_done)
		n += snprintf(text + n, size - n, ", RAM 0x08011000 %lu/%lu", (unsigned long)s->kram_pattern, (unsigned long)s->kram_changed);
	else
		n += snprintf(text + n, size - n, ", RAM 0x08011000 not scanned (no kernel)");
	if (s->edram_done)
		snprintf(text + n, size - n, ", eDRAM 0x11000 %lu/%lu", (unsigned long)s->edram_pattern, (unsigned long)s->edram_changed);
	else
		snprintf(text + n, size - n, ", eDRAM 0x11000 not scanned here");
}

static const char *into_verdict(u32 seed, u32 mask)
{
	u32 got = count_matches(spad(), seed, mask);

	return got == CELL_WORDS ? ": REACHED" : got ? ": IN PART" : ": not reached";
}

/* ============================================================ 1. the GE (v1's cells) */
static unsigned int __attribute__((aligned(16))) ge_list[4096];

struct SpriteVertex
{
	u32 color;
	short x, y, z;
	short pad;
};

static void ge_init(void)
{
	sceGuInit();
	sceGuStart(GU_DIRECT, ge_list);
	sceGuDrawBuffer(GU_PSM_8888, (void *)0x88000, 512);
	sceGuDispBuffer(480, 272, (void *)0, 512);
	sceGuDepthBuffer((void *)0x110000, 512);
	sceGuOffset(2048 - 240, 2048 - 136);
	sceGuViewport(2048, 2048, 480, 272);
	sceGuScissor(0, 0, 480, 272);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuDisable(GU_DEPTH_TEST);
	sceGuDisable(GU_TEXTURE_2D);
	sceGuDisable(GU_BLEND);
	sceGuShadeModel(GU_FLAT);
	sceGuFinish();
	sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
}

static volatile u32 *pixel(int x, int y)
{
	return (volatile u32 *)(0x44000000u + 0x88000u + (u32)(y * 512 + x) * 4u);
}

static int ge_draw(const void *vertices)
{
	u32 start;

	sceGuStart(GU_DIRECT, ge_list);
	sceGuClearColor(0xff000000u);
	sceGuClear(GU_COLOR_BUFFER_BIT);
	sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, vertices);
	sceGuFinish();
	start = sceKernelGetSystemTimeLow();
	while (sceGeDrawSync(1) != 0)
	{
		if (sceKernelGetSystemTimeLow() - start > 200000u)
			return 0;
		sceKernelDelayThread(500);
	}
	return 1;
}

static void place_sprite(volatile struct SpriteVertex *v, u32 color)
{
	v[0].color = color;
	v[0].x = 100;
	v[0].y = 100;
	v[0].z = 0;
	v[0].pad = 0;
	v[1].color = color;
	v[1].x = 140;
	v[1].y = 140;
	v[1].z = 0;
	v[1].pad = 0;
}

static void ge_cells(void)
{
	static struct SpriteVertex ram_vertices[2] __attribute__((aligned(16)));
	int f;

	ge_init();
	place_sprite(ram_vertices, 0xff10e020u);
	sceKernelDcacheWritebackInvalidateAll();
	{
		int done = ge_draw(ram_vertices);

		out("GE control, vertices in RAM: list %s; the pixel %08x (wanted %06x, alpha aside)%s", done ? "ended" : "DID NOT END in 200 ms",
			(unsigned)*pixel(120, 120), 0x10e020u, (*pixel(120, 120) & 0xffffffu) == 0x10e020u ? ": drawn" : ": NOT DRAWN, the cells below mean nothing");
	}
	for (f = 0; f < 4; f++)
	{
		u32 color = 0xff000000u | (0x20u << (f * 4)) | 0x0080a0u;
		int done;
		u32 got;

		place_sprite((volatile struct SpriteVertex *)PROBE, color);
		out("GE reading its vertices from the scratchpad as %s: drawing", form_names[f]);
		done = ge_draw((const void *)(forms[f] | PROBE));
		got = *pixel(120, 120);
		out("GE reading its vertices from the scratchpad as %s: list %s; the pixel %08x (wanted %08x)%s", form_names[f],
			done ? "ended" : "DID NOT END in 200 ms", (unsigned)got, (unsigned)color,
			(got & 0xffffffu) == (color & 0xffffffu) ? ": REACHED (the GE read the scratchpad)" :
				(got & 0xffffffu) == 0 ? ": nothing drawn" : ": something else drawn (vertices read from elsewhere)");
		if (!done)
		{
			sceGeBreak(1, NULL);
			ge_init();
		}
	}
}

/* ============================================================ 3. sceDmacMemcpy */
static u32 *ram; /* 2 KB, 64-aligned: [0, 256) a source, [256, 512) a destination */
static u32 seed_next = 1;

static u32 ram_matches(const u32 *at, u32 seed, u32 mask)
{
	sceKernelDcacheWritebackInvalidateRange(at, CELL_BYTES);
	return count_matches((volatile const u32 *)at, seed, mask);
}

static u32 ram_zeros(const u32 *at)
{
	u32 k, zeros = 0;

	for (k = 0; k < CELL_WORDS; k++)
		zeros += ((volatile const u32 *)at)[k] == 0;
	return zeros;
}

static void fill_ram(u32 *at, u32 seed, u32 mask)
{
	u32 k;

	for (k = 0; k < CELL_WORDS; k++)
		at[k] = seed ? pattern(seed, k, mask) : 0;
}

static u32 dmac_into(int f, const char *tag)
{
	u32 seed = seed_next += 2, got;
	struct Scan scan;
	char what[300], aliases[200];
	int result;

	fill_spad(seed + 1000, MASK32);
	fill_ram(ram, seed, MASK32);
	sceKernelDcacheWritebackInvalidateAll();
	out("dmac into the scratchpad as %s%s: calling", form_names[f], tag);
	scan_before(&scan, 1, 1);
	result = sceDmacMemcpy((void *)(forms[f] | PROBE), ram, CELL_BYTES);
	scan_after(&scan, seed, MASK32);
	got = count_matches(spad(), seed, MASK32);
	describe_spad(what, sizeof(what), seed, MASK32, seed + 1000);
	scan_text(&scan, aliases, sizeof(aliases));
	out("dmac into the scratchpad as %s%s: returned %08x; %s; %s%s", form_names[f], tag, (unsigned)result, what, aliases,
		into_verdict(seed, MASK32));
	return got;
}

static u32 dmac_out(int f, const char *tag)
{
	u32 seed = seed_next += 2, got;
	u32 *dst = ram + CELL_WORDS;
	int result;

	fill_spad(seed, MASK32);
	fill_ram(dst, 0, 0);
	sceKernelDcacheWritebackInvalidateAll();
	out("dmac out of the scratchpad as %s%s: calling", form_names[f], tag);
	result = sceDmacMemcpy(dst, (const void *)(forms[f] | PROBE), CELL_BYTES);
	got = ram_matches(dst, seed, MASK32);
	out("dmac out of the scratchpad as %s%s: returned %08x; %lu of %u words arrived in RAM, %lu still zero%s", form_names[f], tag,
		(unsigned)result, (unsigned long)got, CELL_WORDS, (unsigned long)ram_zeros(dst),
		got == CELL_WORDS ? ": REACHED" : got ? ": IN PART" : ": not reached");
	return got;
}

static void dmac_cells(void)
{
	int f;

	{
		u32 seed = seed_next += 2;
		int result;

		fill_ram(ram, seed, MASK32);
		fill_ram(ram + CELL_WORDS, 0, 0);
		sceKernelDcacheWritebackInvalidateAll();
		result = sceDmacMemcpy(ram + CELL_WORDS, ram, CELL_BYTES);
		out("dmac control RAM->RAM: returned %08x, %lu of %u words arrived", (unsigned)result,
			(unsigned long)ram_matches(ram + CELL_WORDS, seed, MASK32), CELL_WORDS);
	}
	for (f = 0; f < 4; f++)
		if (dmac_into(f, ""))
			dmac_into(f, " (repeat)");
	for (f = 0; f < 4; f++)
		if (dmac_out(f, ""))
			dmac_out(f, " (repeat)");
}

/* ============================================================ 4. the VME's DMAC */
static void vme_regs(char *text, int size)
{
	snprintf(text, size, "status %08lx config %08lx address %08lx count %08lx buffer %08lx; %lu ME cycles%s%s",
		(unsigned long)me_res(2), (unsigned long)me_res(3), (unsigned long)me_res(4), (unsigned long)me_res(5),
		(unsigned long)me_res(6), (unsigned long)me_res(7), me_res(8) ? "; DID NOT END within the bound" : "",
		me_res(9) ? "; busy never seen" : "");
}

static u32 vme_into(int f, const char *tag)
{
	u32 seed = seed_next += 2, got;
	struct Scan scan;
	char what[300], aliases[200], regs[200];

	fill_spad(seed + 1000, MASK32);
	sceKernelDcacheWritebackInvalidateAll();
	out("VME DMAC into the scratchpad as %s%s (buffer to memory, 0x48, address %08lx): calling", form_names[f], tag,
		(unsigned long)(forms[f] | PROBE));
	scan_before(&scan, 0, 0); /* the kernel-RAM window is the quiet call's */
	kalias.seed = seed;
	kalias.mask = MASK24;
	if (!me_call_quiet(OP_VME, 1, forms[f] | PROBE, seed, 1, 1))
	{
		scan_after(&scan, seed, MASK24);
		return 0;
	}
	scan.kram_pattern = kalias.matched;
	scan.kram_changed = kalias.changed;
	scan.kram_done = 1;
	{
		u32 buffer_right = me_res(1), edram_pattern = me_res(10), edram_changed = me_res(11);

		vme_regs(regs, sizeof(regs));
		scan_after(&scan, seed, MASK24);
		got = count_matches(spad(), seed, MASK24);
		describe_spad(what, sizeof(what), seed, MASK24, seed + 1000);
		scan_text(&scan, aliases, sizeof(aliases));
		out("VME DMAC into the scratchpad as %s%s: buffer filled %lu of 256; %s; %s; %s, eDRAM 0x11000 %lu/%lu (the ME's scan)%s",
			form_names[f], tag, (unsigned long)buffer_right, regs, what, aliases, (unsigned long)edram_pattern,
			(unsigned long)edram_changed, into_verdict(seed, MASK24));
	}
	return got;
}

static u32 vme_out(int f, const char *tag)
{
	u32 seed = seed_next += 2, got;
	char regs[200];

	fill_spad(seed, MASK24);
	sceKernelDcacheWritebackInvalidateAll();
	out("VME DMAC out of the scratchpad as %s%s (memory to buffer, 0x40, address %08lx): calling", form_names[f], tag,
		(unsigned long)(forms[f] | PROBE));
	if (!me_call_quiet(OP_VME, 0, forms[f] | PROBE, seed, 1, 0))
		return 0;
	got = me_res(0);
	vme_regs(regs, sizeof(regs));
	out("VME DMAC out of the scratchpad as %s%s: %s; the buffer: %lu of 256 the scratchpad's pattern, %lu eDRAM 0x11000's, %lu zero%s",
		form_names[f], tag, regs, (unsigned long)got, (unsigned long)me_res(10), (unsigned long)me_res(1),
		got == CELL_WORDS ? ": REACHED" : got ? ": IN PART" : me_res(10) == CELL_WORDS ? ": read eDRAM instead" : ": not reached");
	return got;
}

static void vme_cells(void)
{
	char regs[200];
	int f;

	if (!me_ok)
	{
		out("VME DMAC: not run (the ME isn't running)");
		return;
	}
	/* controls, main RAM by the form mcidclan's sample gives it (0x40000000 | physical) */
	{
		u32 seed = seed_next += 2;
		u32 *dst = ram + CELL_WORDS;

		fill_ram(dst, 0, 0);
		sceKernelDcacheWritebackInvalidateAll();
		out("VME DMAC control buffer->RAM: calling");
		if (me_call(OP_VME, 1, 0x40000000u | ((u32)dst & 0x1FFFFFFFu), seed, 0))
		{
			vme_regs(regs, sizeof(regs));
			out("VME DMAC control buffer->RAM: buffer filled %lu of 256; %s; %lu of 256 arrived in RAM", (unsigned long)me_res(1), regs,
				(unsigned long)ram_matches(dst, seed, MASK24));
		}
	}
	{
		u32 seed = seed_next += 2;

		fill_ram(ram, seed, MASK24);
		sceKernelDcacheWritebackInvalidateAll();
		out("VME DMAC control RAM->buffer: calling");
		if (me_call(OP_VME, 0, 0x40000000u | ((u32)ram & 0x1FFFFFFFu), seed, 0))
		{
			vme_regs(regs, sizeof(regs));
			out("VME DMAC control RAM->buffer: %s; %lu of 256 arrived in the buffer", regs, (unsigned long)me_res(0));
		}
	}
	for (f = 0; f < ENGINE_FORMS && me_ok; f++)
		if (vme_into(f, ""))
			vme_into(f, " (repeat)");
	for (f = 0; f < ENGINE_FORMS && me_ok; f++)
		if (vme_out(f, ""))
			vme_out(f, " (repeat)");
}

/* ============================================================ 5, 6. DMACplus */
static u32 phys(const void *p)
{
	return (u32)p & 0x1FFFFFFFu;
}

/* the ME's side: eDRAM to the scratchpad (Me2Sc), staged by the ME's stores */
static u32 dplus_me_into(int f, const char *tag)
{
	u32 seed = seed_next += 2, got;
	struct Scan scan;
	char what[300], aliases[200], regs[300];
	int result;

	if (!me_call(OP_FILL, EDRAM_STAGE, seed, MASK32, 0))
		return 0;
	fill_spad(seed + 1000, MASK32);
	out("DMACplus Me2Sc, eDRAM to the scratchpad as %s%s: calling", form_names[f], tag);
	scan_before(&scan, 0, 1);
	result = run_dcell(DMACP_ME2SC, EDRAM_STAGE, forms[f] | PROBE, dmacp_control(0, 1, 0, CELL_BYTES / 16), 1, seed, MASK32);
	scan_after(&scan, seed, MASK32);
	scan.kram_done = 1;
	scan.kram_pattern = dcell.kram.matched;
	scan.kram_changed = dcell.kram.changed;
	got = count_matches(spad(), seed, MASK32);
	dregs(regs, sizeof(regs));
	describe_spad(what, sizeof(what), seed, MASK32, seed + 1000);
	scan_text(&scan, aliases, sizeof(aliases));
	out("DMACplus Me2Sc into the scratchpad as %s%s: %s; %s; %s; %s%s", form_names[f], tag, dresult(result), regs, what, aliases,
		into_verdict(seed, MASK32));
	return got;
}

/* the scratchpad to eDRAM (Sc2Me), read back by the ME */
static u32 dplus_me_out(int f, const char *tag)
{
	u32 seed = seed_next += 2, got;
	char regs[300];
	int result;

	if (!me_call(OP_FILL, EDRAM_OUT, 0, 0, 0))
		return 0;
	fill_spad(seed, MASK32);
	out("DMACplus Sc2Me, the scratchpad as %s to eDRAM%s: calling", form_names[f], tag);
	result = run_dcell(DMACP_SC2ME, forms[f] | PROBE, EDRAM_OUT, dmacp_control(1, 0, 0, CELL_BYTES / 16), 0, 0, 0);
	dregs(regs, sizeof(regs));
	if (!me_call(OP_MATCH, EDRAM_OUT, seed, MASK32, 0))
		return 0;
	got = me_res(0);
	out("DMACplus Sc2Me out of the scratchpad as %s%s: %s; %s; eDRAM: %lu of 256 arrived, %lu zero%s", form_names[f], tag,
		dresult(result), regs, (unsigned long)got, (unsigned long)me_res(1),
		got == CELL_WORDS ? ": REACHED" : got ? ": IN PART" : ": not reached");
	return got;
}

static void dplus_me_cells(void)
{
	char regs[300];
	int f, controls = 1;

	if (!kernel_ok || !me_ok)
	{
		out("DMACplus Sc2Me/Me2Sc: not run (%s)", !kernel_ok ? "no kernel calls" : "the ME's side isn't up");
		return;
	}
	{
		u32 seed = seed_next += 2;
		int result;

		fill_ram(ram, seed, MASK32);
		me_call(OP_FILL, EDRAM_STAGE, 0, 0, 0);
		out("DMACplus control Sc2Me RAM->eDRAM: calling");
		result = run_dcell(DMACP_SC2ME, phys(ram), EDRAM_STAGE, dmacp_control(1, 0, 0, CELL_BYTES / 16), 0, 0, 0);
		dregs(regs, sizeof(regs));
		me_call(OP_MATCH, EDRAM_STAGE, seed, MASK32, 0);
		out("DMACplus control Sc2Me RAM->eDRAM: %s; %s; %lu of 256 arrived", dresult(result), regs, (unsigned long)me_res(0));
		controls &= me_res(0) == CELL_WORDS;
	}
	{
		u32 seed = seed_next += 2, got;
		int result;

		me_call(OP_FILL, EDRAM_STAGE, seed, MASK32, 0);
		fill_ram(ram + CELL_WORDS, 0, 0);
		sceKernelDcacheWritebackInvalidateAll();
		out("DMACplus control Me2Sc eDRAM->RAM: calling");
		result = run_dcell(DMACP_ME2SC, EDRAM_STAGE, phys(ram + CELL_WORDS), dmacp_control(0, 1, 0, CELL_BYTES / 16), 0, 0, 0);
		dregs(regs, sizeof(regs));
		got = ram_matches(ram + CELL_WORDS, seed, MASK32);
		out("DMACplus control Me2Sc eDRAM->RAM: %s; %s; %lu of 256 arrived", dresult(result), regs, (unsigned long)got);
		controls &= got == CELL_WORDS;
	}
	if (!controls)
		out("DMACplus Sc2Me/Me2Sc: a control failed, so the scratchpad cells below would mean nothing; they still run");
	for (f = 0; f < ENGINE_FORMS && me_ok; f++)
		if (dplus_me_into(f, ""))
			dplus_me_into(f, " (repeat)");
	for (f = 0; f < ENGINE_FORMS && me_ok; f++)
		if (dplus_me_out(f, ""))
			dplus_me_out(f, " (repeat)");
}

static u32 dplus_sc_into(int f, const char *tag)
{
	u32 seed = seed_next += 2, got;
	struct Scan scan;
	char what[300], aliases[200], regs[300];
	int result;

	fill_ram(ram, seed, MASK32);
	fill_spad(seed + 1000, MASK32);
	sceKernelDcacheWritebackInvalidateAll();
	out("DMACplus Sc2Sc, RAM to the scratchpad as %s%s: calling", form_names[f], tag);
	scan_before(&scan, 0, 1);
	result = run_dcell(DMACP_SC2SC, phys(ram), forms[f] | PROBE, dmacp_control(0, 0, 1, CELL_BYTES / 16), 1, seed, MASK32);
	scan_after(&scan, seed, MASK32);
	scan.kram_done = 1;
	scan.kram_pattern = dcell.kram.matched;
	scan.kram_changed = dcell.kram.changed;
	got = count_matches(spad(), seed, MASK32);
	dregs(regs, sizeof(regs));
	describe_spad(what, sizeof(what), seed, MASK32, seed + 1000);
	scan_text(&scan, aliases, sizeof(aliases));
	out("DMACplus Sc2Sc into the scratchpad as %s%s: %s; %s; %s; %s%s", form_names[f], tag, dresult(result), regs, what, aliases,
		into_verdict(seed, MASK32));
	return got;
}

static u32 dplus_sc_out(int f, const char *tag)
{
	u32 seed = seed_next += 2, got;
	u32 *dst = ram + CELL_WORDS;
	char regs[300];
	int result;

	fill_spad(seed, MASK32);
	fill_ram(dst, 0, 0);
	sceKernelDcacheWritebackInvalidateAll();
	out("DMACplus Sc2Sc, the scratchpad as %s to RAM%s: calling", form_names[f], tag);
	result = run_dcell(DMACP_SC2SC, forms[f] | PROBE, phys(dst), dmacp_control(0, 0, 1, CELL_BYTES / 16), 0, 0, 0);
	dregs(regs, sizeof(regs));
	got = ram_matches(dst, seed, MASK32);
	out("DMACplus Sc2Sc out of the scratchpad as %s%s: %s; %s; %lu of 256 arrived in RAM, %lu still zero%s", form_names[f], tag,
		dresult(result), regs, (unsigned long)got, (unsigned long)ram_zeros(dst),
		got == CELL_WORDS ? ": REACHED" : got ? ": IN PART" : ": not reached");
	return got;
}

static void dplus_sc_cells(void)
{
	char regs[300];
	int f;

	if (!kernel_ok)
	{
		out("DMACplus Sc2Sc: not run (no kernel calls)");
		return;
	}
	{
		u32 seed = seed_next += 2, got;
		int result;

		fill_ram(ram, seed, MASK32);
		fill_ram(ram + CELL_WORDS, 0, 0);
		sceKernelDcacheWritebackInvalidateAll();
		out("DMACplus control Sc2Sc RAM->RAM (channel 0x1C0, its first run in the lab): calling");
		result = run_dcell(DMACP_SC2SC, phys(ram), phys(ram + CELL_WORDS), dmacp_control(0, 0, 1, CELL_BYTES / 16), 0, 0, 0);
		dregs(regs, sizeof(regs));
		got = ram_matches(ram + CELL_WORDS, seed, MASK32);
		out("DMACplus control Sc2Sc RAM->RAM: %s; %s; %lu of 256 arrived", dresult(result), regs, (unsigned long)got);
		if (result || got != CELL_WORDS)
		{
			out("DMACplus Sc2Sc: the control didn't pass, so its scratchpad cells aren't run");
			return;
		}
	}
	for (f = 0; f < ENGINE_FORMS; f++)
		if (dplus_sc_into(f, ""))
			dplus_sc_into(f, " (repeat)");
	for (f = 0; f < ENGINE_FORMS; f++)
		if (dplus_sc_out(f, ""))
			dplus_sc_out(f, " (repeat)");
}

/* ============================================================ the run */
int main(void)
{
	SceCtrlData pad;
	u32 k;

	fpu_mask_traps();
	pspDebugScreenInit();
	sceIoMkdir("ms0:/halo", 0777);
	sceIoMkdir("ms0:/halo/log", 0777);
	sceIoRemove(log_path);
	out("==== halo_spadprobe v2: which engines reach the main CPU's scratchpad (the probe at 0x%08x, 1 KB) ====", PROBE);
	ram = (u32 *)memalign(64, CELL_BYTES * 2);
	if (!ram)
	{
		out("could not allocate");
		goto done;
	}
	fill_spad(7, MASK32);
	out("the CPU writes and reads the scratchpad: %lu of %u words right (uncached view %08x, direct %08x)",
		(unsigned long)count_matches(spad(), 7, MASK32), CELL_WORDS, (unsigned)*(volatile u32 *)(0x40000000u | PROBE),
		(unsigned)spad()[0]);

	ge_cells();

	out("-- the Media Engine (me-core) --");
	{
		u32 alive;
		int table = meLibDefaultInit();

		/* a table number, or -6 (no table for this ME image): the kernel module loaded and kcall works; -3 to -5 are the module or kinit failing */
		kernel_ok = table >= 0 || table == -6;
		alive = me_alive;
		me_ok = table >= 2;
		sceKernelDelayThread(100000);
		if (me_ok && me_alive == alive)
			me_ok = 0;
		out("meLibDefaultInit: %d; kernel calls %s; the ME %s", table, kernel_ok ? "available" : "NOT available",
			me_ok ? "running" : "NOT running (its cells won't run)");
	}

	dmac_cells();
	vme_cells();
	dplus_me_cells();
	dplus_sc_cells();

	out("the ME's DMA(2) (the ME's 0x04100000): not built: no source gives its registers (psdevwiki's Media Engine page lists the unit with its size and use unknown; mcidclan's and Dink's notes cover only the primary DMAC at 0x440ff000)");
	out("the system DMAC by its registers (0xBC900000/0xBCA00000): not built: the kernel's dmacman owns its channels (the audio's descriptor lists among them), so taking one under it risks the kernel's own transfers; sceDmacMemcpy above is the same engine");

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
	/* a short pause so the last lines are on screen for a hand run; bounded,
	so a rig run never waits on a button that nobody presses */
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
