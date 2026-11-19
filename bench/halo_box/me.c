/*
halo_box on the Media Engine (bench/halo_box/README.md, "On the Media
Engine"): a box's per-entity pass run by the ME, from main RAM or from a copy
of its code in the ME's eDRAM, and the main CPU's own loop timed beside it;
and each entity handed to the ME as a job of its own (mejob), timed from the
main CPU over the hand-over and the wait.

The ME runs mcidclan's me-core-mapper library (MIT, copyright (c) 2026
mcidclan (m-c/d, m-cid), https://github.com/mcidclan/psp-media-engine-custom-core),
as halo_bench10 and halo_bench12 do. Its code is copied into eDRAM by MEXEC's
tool and installer (tools/mexec.py --entry box_me_pass, psp/src/psp_mexec_copy.h).

The ME's side makes no system call and reaches no VFPU instruction: neither
exists there. In the emulator (which runs no ME code) the same pass runs on
the main CPU, timed by the clock, so the hand-over, the places' set-up and the
checks are proven there; nothing about time or the copy is.

This unit is built with -fno-builtin -fno-tree-loop-distribute-patterns
(CMakeLists.txt): no loop of the ME's may become a library call.
*/
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <malloc.h>
#include <string.h>
#include <me-core-mapper/me-core.h>
#include "harness.h"
#include "../../psp/src/psp_mexec_copy.h"

/* The copy's table: tools/mexec.py writes it into the linked program
(CMakeLists.txt, after the link, before prxgen); 'MEX0' until then. Sized
as the game's (psp_me.c) */
#define MEXEC_TABLE_WORDS 1536
unsigned int psp_mexec_table[MEXEC_TABLE_WORDS] = { 0x4D455830u };

/* The ME's eDRAM as its cached addresses (psp_me.h's map, the bench alone on
the PSP): the copy at 256 KB, the data room at 512-1,280 KB, the stack at the
top */
#define EDRAM 0x80000000u
#define COPY_AT (EDRAM + 0x40000u)
#define COPY_ROOM 0x30000u
#define ROOM_AT (EDRAM + 0x80000u)
#define ROOM_BYTES 0xC0000u

/* the bounded wait: a pass that doesn't end in this long is a box the ME
can't run (a system call, a VFPU instruction, a fault), named and stopped */
#define PASS_MOST_US 5000000u

/* shared, uncached: the main CPU writes an order's words, then its sequence;
the ME answers in ack, then in done when the order is carried out */
/* 64-byte aligned and alone on its lines, read through the uncached alias
by both CPUs, as psp_me.c's control block (me-core's meLibSetSharedUncached32
is C++ only) */
static unsigned int shared_storage[32] __attribute__((aligned(64)));
#define SHARED ((volatile unsigned int *)(0x40000000u | (unsigned int)shared_storage))
#define sh_sequence (SHARED[0])
#define sh_op       (SHARED[1])
#define sh_entry    (SHARED[2])  /* box_me_pass, or its copy */
#define sh_run      (SHARED[3])  /* the adapter's run, or its copy */
#define sh_calls    (SHARED[4])
#define sh_mode     (SHARED[5])
#define sh_cycles   (SHARED[6])  /* one word a call, main RAM */
#define sh_place    (SHARED[7])  /* the adapter's me_place, or its copy, or 0 */
#define sh_ack      (SHARED[8])
#define sh_done     (SHARED[9])
#define sh_alive    (SHARED[10])
#define sh_at       (SHARED[11]) /* the call in progress, for a stopped pass's report */
#define sh_copy     (SHARED[12]) /* 1 copied, 2 the table refused */
#define sh_copy_bytes (SHARED[13])
#define sh_copy_sum (SHARED[14])
#define sh_copy_at  (SHARED[15]) /* the copy's entry */
#define sh_placed   (SHARED[16]) /* me_place's answer: bytes used, or negative */
#define sh_crowd_data (SHARED[17])
#define sh_crowd_code (SHARED[18])
#define sh_fcsr     (SHARED[19]) /* the ME's FPU control, as set */
#define sh_counting (SHARED[20]) /* 1 when the ME's cycle counter moves */
#define sh_phase    (SHARED[21]) /* mode x BUSY_PHASES + the pass's phase, or ~0 outside one */
#define sh_dma_ram  (SHARED[22]) /* medma: the main-RAM buffer's physical address, 0 none */
#define sh_dma_bytes (SHARED[23]) /* the bytes a way (the copy in's, to DMA_ROOM), a multiple of 16 */
#define sh_dma_in   (SHARED[24]) /* the ME's cycles for the copy in, 0 failed */
#define sh_dma_back (SHARED[25])
#define sh_dma_err  (SHARED[26]) /* 0, or the failure (-1 busy, -2 controller off, -3 timed out) */
#define sh_first    (SHARED[27]) /* OP_JOB: the entity to run */
#define sh_spacing  (SHARED[28]) /* the idle ME's nops between polls */



enum { OP_NONE, OP_INSTALL, OP_PASS, OP_SYNC_IN, OP_PREP, OP_JOB, OP_SYNC_OUT };

/* the idle ME's poll spacing: wide between passes, so it stays off the bus
the main CPU's figures are taken on; narrow while jobs are handed over, so a
job's wait isn't mostly the poll's */
#define SPACING_IDLE 2000u
#define SPACING_JOBS 16u

#define CROWD_BYTES 32768u

static inline unsigned int me_count(void)
{
	unsigned int count;

	__asm__ __volatile__("mfc0 %0, $9" : "=r"(count));
	return count;
}

/* DMACplus driven from the ME (kernel mode, plain register stores): psp_me.c's
registers and control word, measured from the main CPU by
bench/halo_bench15; one descriptor at a time (a chain hung the PSP), the
ME's side the eDRAM's offset. Bounded by the ME's own counter */
#define DMACP 0xBC800000u
#define DMACP_REG(at) (*(volatile unsigned int *)(DMACP + (at)))
#define DMACP_SC2ME 0x180u
#define DMACP_ME2SC 0x1A0u
#define DMA_AT 0x140000u        /* eDRAM 1,280-1,504 KB: past the room, below the stack */
#define DMA_ROOM 0x38000u
#define DMA_MOST 65520u
#define DMA_MOST_CYCLES (333u * 100000u)

static int me_dma(int into_edram, unsigned int ram, unsigned int edram, unsigned int bytes)
{
	unsigned int channel = into_edram ? DMACP_SC2ME : DMACP_ME2SC, bit = into_edram ? 4u : 8u;

	if (!(DMACP_REG(0x30) & 1) && !DMACP_REG(0x1C))
		return -2;
	while (bytes)
	{
		unsigned int part = bytes < DMA_MOST ? bytes : DMA_MOST, start;

		if (DMACP_REG(channel + 0x10) & 1)
			return -1;
		DMACP_REG(0x08) = bit;
		DMACP_REG(0x10) = bit;
		DMACP_REG(channel + 0x00) = into_edram ? ram : edram;
		DMACP_REG(channel + 0x04) = into_edram ? edram : ram;
		DMACP_REG(channel + 0x08) = 0;
		DMACP_REG(channel + 0x0C) = (1u << 31) | ((into_edram ? 1u : 0u) << 25) | ((into_edram ? 0u : 1u) << 24) |
			(4u << 21) | (4u << 18) | (1u << 15) | (1u << 12) | (part / 16);
		DMACP_REG(channel + 0x10) = 1;
		__asm__ __volatile__("sync");
		start = me_count();
		while (DMACP_REG(channel + 0x10) & 1)
			if (me_count() - start > DMA_MOST_CYCLES)
			{
				DMACP_REG(channel + 0x10) = 0;
				__asm__ __volatile__("sync");
				return -3;
			}
		DMACP_REG(0x08) = bit;
		DMACP_REG(0x10) = bit;
		ram += part;
		edram += part;
		bytes -= part;
	}
	return 0;
}

/* the ME's own caches emptied: the data written back and dropped, and the
instructions displaced by 32 KB of other code (the crowd code, run without
its data walk). No index cache operation on the ME's I-cache: none of the
port's code has run one there (B77) */
static void me_empty(void)
{
	meLibDcacheWritebackInvalidateAll();
	((void (*)(void))sh_crowd_code)();
	meLibDcacheWritebackInvalidateAll();
}

/* other work's lines in every set of the ME's caches, the data lines dirty:
the main CPU's crowding walk and code, run by the ME */
static void me_crowd(void)
{
	unsigned int *word = (unsigned int *)sh_crowd_data, *end = word + CROWD_BYTES / 4;

	for (; word < end; word += 16)
		*word = (unsigned int)word;
	((void (*)(void))sh_crowd_code)();
}

/* The timed pass, the copy's entry: called on the ME through its eDRAM copy
for the eDRAM places, so every jal it reaches among the copied functions
stays there. A batching box isn't run here (README) */
__attribute__((noinline))
void box_me_pass(void (*run)(int), unsigned int first, unsigned int calls, unsigned int mode, unsigned int *cycles)
{
	unsigned int i;

	for (i = first; i < first + calls; i++)
	{
		unsigned int before, after;

		if (mode == MODE_EMPTIED)
			me_empty();
		else if (mode == MODE_CROWDED)
			me_crowd();
		sh_at = i;
		before = me_count();
		run((int)i);
		after = me_count();
		cycles[i] = after - before;
	}
}

/* ---------- the ME's side */
__attribute__((noinline, aligned(4)))
void meLibOnProcess(void)
{
	unsigned int seen = 0, cause, fcsr;

	/* the cycle counter: Cause.DC cleared and Count written, or it reads 0
	(hardware_facts.md; psp_me.c does the same) */
	__asm__ __volatile__("mfc0 %0, $13" : "=r"(cause));
	cause &= ~(1u << 27);
	__asm__ __volatile__("mtc0 %0, $13\n\tsync" : : "r"(cause));
	__asm__ __volatile__("mtc0 $0, $9\n\tsync");
	/* the FPU's traps masked and FS set, as main.c does for the main CPU:
	an untaken trap, or a denormal with FS clear, freezes the PSP (B75) */
	__asm__ __volatile__("cfc1 %0, $31" : "=r"(fcsr));
	fcsr = (fcsr & ~0x00000F80u) | 0x01000000u;
	__asm__ __volatile__("ctc1 %0, $31" : : "r"(fcsr));
	__asm__ __volatile__("cfc1 %0, $31" : "=r"(fcsr));
	sh_fcsr = fcsr;
	meLibDcacheWritebackInvalidateAll();
	for (;;)
	{
		unsigned int sequence = sh_sequence, op;

		if (sequence == seen)
		{
			unsigned int k, c0 = me_count(), spacing = sh_spacing;

			sh_alive++;
			for (k = 0; k < spacing; k++)
				__asm__ __volatile__("nop");
			sh_counting = me_count() != c0;
			continue;
		}
		seen = sequence;
		op = sh_op;
		sh_ack = sequence;
		if (op == OP_INSTALL)
		{
			struct mexec_copied copied;
			unsigned int at = mexec_copy(psp_mexec_table, (unsigned int)box_me_pass, COPY_AT, COPY_ROOM, &copied);

			sh_copy_at = at;
			sh_copy_bytes = at ? copied.bytes : 0;
			sh_copy_sum = at ? copied.sum : 0;
			sh_copy = at ? 1 : 2;
		}
		else if (op == OP_PASS)
		{
			int (*place)(void *, unsigned int, int) = (int (*)(void *, unsigned int, int))sh_place;
			int placed;

			/* the main CPU wrote its cache back before the order: nothing
			stale of the tick's inputs stays here */
			meLibDcacheWritebackInvalidateAll();
			sh_phase = sh_mode * BUSY_PHASES + BUSY_COPY_IN;
			placed = place ? place((void *)ROOM_AT, ROOM_BYTES, 0) : 0;

			sh_placed = (unsigned int)placed;
			if (placed >= 0)
			{
				meLibDcacheWritebackInvalidateAll();
				sh_phase = sh_mode * BUSY_PHASES + BUSY_CALLS;
				((void (*)(void (*)(int), unsigned int, unsigned int, unsigned int, unsigned int *))sh_entry)(
					(void (*)(int))sh_run, 0, sh_calls, sh_mode, (unsigned int *)sh_cycles);
				sh_phase = sh_mode * BUSY_PHASES + BUSY_COPY_BACK;
				if (place)
					place((void *)ROOM_AT, ROOM_BYTES, 1);
			}
			meLibDcacheWritebackInvalidateAll();
			sh_dma_in = sh_dma_back = 0;
			sh_dma_err = 0;
			/* the same bytes the copy in moved, as far as the DMA's room goes */
			sh_dma_bytes = placed > 0 ? ((unsigned int)placed < DMA_ROOM ? (unsigned int)placed : DMA_ROOM) & ~15u : 0;
			if (sh_dma_ram && sh_dma_bytes)
			{
				unsigned int c0 = me_count(), c1;
				int r;

				sh_phase = sh_mode * BUSY_PHASES + BUSY_DMA_IN;
				r = me_dma(1, sh_dma_ram, DMA_AT, sh_dma_bytes);
				c1 = me_count();
				if (!r)
				{
					sh_dma_in = c1 - c0;
					sh_phase = sh_mode * BUSY_PHASES + BUSY_DMA_BACK;
					r = me_dma(0, sh_dma_ram, DMA_AT, sh_dma_bytes);
					if (!r)
						sh_dma_back = me_count() - c1;
				}
				sh_dma_err = (unsigned int)r;
			}
			sh_phase = ~0u;
		}
		/* the per-entity jobs (mejob): the inputs read fresh once before a
		pass's jobs, the cache emptied or crowded before each job untimed, the
		job alone, and what the jobs wrote back after them */
		else if (op == OP_SYNC_IN || op == OP_SYNC_OUT)
			meLibDcacheWritebackInvalidateAll();
		else if (op == OP_PREP)
		{
			if (sh_mode == MODE_EMPTIED)
				me_empty();
			else if (sh_mode == MODE_CROWDED)
				me_crowd();
		}
		else if (op == OP_JOB)
			((void (*)(void (*)(int), unsigned int, unsigned int, unsigned int, unsigned int *))sh_entry)(
				(void (*)(int))sh_run, sh_first, 1, MODE_WARM, (unsigned int *)sh_cycles);
		sh_done = sequence;
	}
}

/* ---------- the main CPU's side */
/* the main CPU writes this while the ME runs a box, so it has a line of its
own: a line it shared with anything the ME writes would be written back over
the ME's bytes */
static struct { unsigned int count, pad[15]; } sent_line __attribute__((aligned(64)));
#define sent (sent_line.count)
static int me_up = 0;       /* 1 the ME runs passes; 2 the emulator: passes run here */
static int me_copied = 0;   /* 1 the eDRAM copy is in place (the emulator: the room stands in) */
static void *sim_room = 0;  /* the emulator's stand-in for the eDRAM room */

static int emulator(void)
{
	int result = 0;

	return sceIoDevctl("kemulator:", 3, NULL, 0, &result, sizeof(result)) == 0 && result;
}

static int order(unsigned int op, unsigned int most_us, void (*meanwhile)(void *), void *context)
{
	unsigned int start = plat_now();

	sh_op = op;
	__asm__ __volatile__("sync");
	sh_sequence = ++sent;
	while (sh_done != sent)
	{
		if (plat_now() - start > most_us)
			return -1;
		if (meanwhile)
			meanwhile(context);
		else
			sceKernelDelayThread(100);
	}
	return 0;
}

int plat_me_start(void *crowd_data, void (*crowd_code)(void), int want_copy)
{
	unsigned int alive;

	if (emulator())
	{
		/* the eDRAM places run too, their room in main RAM: the adapter's
		me_place is proven here, over the real capture */
		sim_room = memalign(64, ROOM_BYTES);
		me_copied = sim_room != 0;
		plat_out("ME: the emulator runs no ME code; each pass runs on the main CPU, timed by the clock, and the eDRAM places with their room in main RAM%s (the hand-over, the data's move and the checks are proven here; no time and no copy)",
			sim_room ? "" : " (no memory for it: left out)");
		me_up = 2;
		return me_up;
	}
	sh_phase = ~0u;
	sh_spacing = SPACING_IDLE;
	sh_crowd_data = (unsigned int)crowd_data;
	sh_crowd_code = (unsigned int)crowd_code;
	sceKernelDcacheWritebackAll();
	plat_step("ME: meLibDefaultInit (the ME started, its loop waiting)");
	{
		int table = meLibDefaultInit();

		alive = sh_alive;
		sceKernelDelayThread(100000);
		if (sh_alive == alive)
		{
			plat_out("ME: meLibDefaultInit %d, and the ME isn't running: its places are left out", table);
			return 0;
		}
	}
	me_up = 1;
	plat_out("ME: running (FPU control %08x, its cycle counter %s)", sh_fcsr, sh_counting ? "moving" : "STILL: its times would read 0");
	if (!sh_counting)
	{
		me_up = 0;
		return 0;
	}
	if (!want_copy)
		plat_out("ME: the eDRAM places weren't asked for (meedram, mebusy): no copy made");
	else if (psp_mexec_table[0] != MEXEC_MAGIC)
		plat_out("ME: no copy's table in this build (tools/mexec.py didn't run?): the eDRAM places are left out");
	else if (plat_step("ME: the copy into eDRAM (%u bytes, %u functions)", psp_mexec_table[3], psp_mexec_table[4]),
		order(OP_INSTALL, PASS_MOST_US, 0, 0) < 0)
		plat_out("ME: the copy didn't finish: the eDRAM places are left out");
	else if (sh_copy != 1)
		plat_out("ME: the table was refused (%u bytes over the room's %u?): the eDRAM places are left out", psp_mexec_table[3], COPY_ROOM);
	else
	{
		me_copied = 1;
		plat_out("ME: the copy in place: %u bytes at eDRAM %u KB, sum %08x, %u functions, %u calls and %u branches re-aimed, entry %08x",
			sh_copy_bytes, (COPY_AT - EDRAM) / 1024, sh_copy_sum, psp_mexec_table[4], psp_mexec_table[5], psp_mexec_table[7], sh_copy_at);
	}
	return me_up;
}

int plat_me_copied(void) { return me_copied; }

void *plat_me_where(void *function, int place)
{
	unsigned int at;

	if (place == PLACE_ME_RAM || !me_copied || !function || me_up == 2)
		return function;
	at = mexec_copy_of(psp_mexec_table, (unsigned int)box_me_pass, COPY_AT, (unsigned int)function);
	return at ? (void *)at : function;
}

/* the main CPU's loop beside the ME: one word a line over 256 KB of main
RAM, so every read misses its cache and goes out on the bus the ME shares.
Timed per 64 lines; the figure is µs per 1,000 lines */
#define BUSY_BYTES (256u * 1024u)
static unsigned int *busy_data;
struct busy { struct me_busy *into; unsigned int at, sum; };

static void busy_step(void *context)
{
	struct busy *b = (struct busy *)context;
	unsigned int k, phase = sh_phase, before = plat_now(), us;
	const volatile unsigned int *line = busy_data + b->at / 4;

	for (k = 0; k < 64; k++)
		b->sum += line[k * 16];
	b->at = (b->at + 64 * 64) % BUSY_BYTES;
	us = plat_now() - before;
	b->into->us += us;
	b->into->lines += 64;
	if (phase == sh_phase && phase < MODES * BUSY_PHASES)
	{
		b->into->phase_us[phase] += us;
		b->into->phase_lines[phase] += 64;
	}
}

void plat_me_busy_alone(struct me_busy *into, unsigned int lines)
{
	struct busy b = { into, 0, 0 };

	if (!busy_data)
		busy_data = (unsigned int *)memalign(64, BUSY_BYTES);
	if (!busy_data)
		return;
	while (into->lines < lines)
	{
		sceKernelDcacheWritebackInvalidateRange(busy_data + b.at / 4, 64 * 64);
		busy_step(&b);
	}
}

int plat_me_dma = 0;
static void *dma_buffer;

int plat_me_pass(void (*run)(int), int (*place)(void *, unsigned int, int), unsigned int calls, int mode, int where,
	unsigned int *cycles, struct me_busy *busy)
{
	/* the null box's busy passes keep no figure, but its loop still needs
	somewhere to count: without one it wrote through a null pointer (B77) */
	struct me_busy unkept = { 0, 0 };
	struct busy b = { busy ? busy : &unkept, 0, 0 };

	if (me_up == 2)
	{
		/* the emulator: the pass here, timed by the clock (333 cycles a µs) */
		unsigned int i;
		int placed = where != PLACE_ME_RAM && place ? place(sim_room, ROOM_BYTES, 0) : 0;

		if (placed < 0)
		{
			sh_placed = (unsigned int)placed;
			return -3;
		}
		for (i = 0; i < calls; i++)
		{
			unsigned int before;

			if (mode == MODE_EMPTIED)
				plat_empty();
			else if (mode == MODE_CROWDED)
				plat_crowd();
			before = plat_now();
			run((int)i);
			cycles[i] = (plat_now() - before) * 333u;
		}
		if (where != PLACE_ME_RAM && place)
			place(sim_room, ROOM_BYTES, 1);
		return 0;
	}
	if (!me_up || (where != PLACE_ME_RAM && !me_copied))
		return -2;
	sh_entry = (unsigned int)plat_me_where((void *)box_me_pass, where);
	sh_run = (unsigned int)plat_me_where((void *)run, where);
	sh_place = where == PLACE_ME_RAM ? 0 : (unsigned int)plat_me_where((void *)place, where);
	sh_calls = calls;
	sh_mode = (unsigned int)mode;
	sh_cycles = (unsigned int)cycles;
	sh_at = 0;
	/* the tick's inputs and the adapter's statics, where the ME reads them */
	sceKernelDcacheWritebackAll();
	if (where == PLACE_ME_BUSY && plat_me_dma && !dma_buffer)
		dma_buffer = memalign(64, DMA_ROOM);
	sh_dma_ram = where == PLACE_ME_BUSY && plat_me_dma && dma_buffer ? (unsigned int)dma_buffer & 0x1FFFFFFFu : 0;
	if (where == PLACE_ME_BUSY && busy_data)
	{
		if (order(OP_PASS, PASS_MOST_US, busy_step, &b) < 0)
			return -1;
	}
	else if (order(OP_PASS, PASS_MOST_US, 0, 0) < 0)
		return -1;
	/* what the ME wrote, read fresh */
	sceKernelDcacheWritebackInvalidateAll();
	if (sh_dma_ram && sh_dma_bytes && mode >= 0 && mode < MODES)
	{
		if ((int)sh_dma_err)
		{
			b.into->dma_failed++;
			b.into->dma_error = (int)sh_dma_err;
		}
		else
		{
			b.into->dma_cycles[mode * 2] += sh_dma_in;
			b.into->dma_cycles[mode * 2 + 1] += sh_dma_back;
			b.into->dma_bytes[mode] += sh_dma_bytes;
			b.into->dma_passes[mode]++;
		}
	}
	if ((int)sh_placed < 0)
		return -3;
	return 0;
}

unsigned int plat_me_at(void) { return sh_at; }
int plat_me_placed(void) { return (int)sh_placed; }

/* An order waited on by spinning, not sleeping: a job's time is the
hand-over and the wait, and a sleep's wake would be most of it. Bounded */
static int order_spin(unsigned int op)
{
	unsigned int start = plat_now(), k;

	sh_op = op;
	__asm__ __volatile__("sync");
	sh_sequence = ++sent;
	while (sh_done != sent)
	{
		if (plat_now() - start > PASS_MOST_US)
			return -1;
		for (k = 0; k < 8; k++)
			__asm__ __volatile__("nop");
	}
	return 0;
}

int plat_me_jobs(void (*run)(int), unsigned int calls, int mode, int where, unsigned int *us, unsigned int *cycles,
	unsigned int *writeback_us)
{
	unsigned int i;
	int result = 0;

	if (me_up == 2)
	{
		/* the emulator: each job here, timed by the clock */
		for (i = 0; i < calls; i++)
		{
			unsigned int before;

			if (mode == MODE_EMPTIED)
				plat_empty();
			else if (mode == MODE_CROWDED)
				plat_crowd();
			before = plat_now();
			sceKernelDcacheWritebackAll();
			writeback_us[i] = plat_now() - before;
			before = plat_now();
			run((int)i);
			us[i] = plat_now() - before;
			cycles[i] = us[i] * 333u;
		}
		return 0;
	}
	if (!me_up || (where != PLACE_ME_RAM && !me_copied))
		return -2;
	sh_entry = (unsigned int)plat_me_where((void *)box_me_pass, where);
	sh_run = (unsigned int)plat_me_where((void *)run, where);
	sh_mode = (unsigned int)mode;
	sh_cycles = (unsigned int)cycles;
	sh_at = 0;
	sh_spacing = SPACING_JOBS;
	sceKernelDcacheWritebackAll();
	if (order_spin(OP_SYNC_IN) < 0)
		result = -1;
	for (i = 0; i < calls && !result; i++)
	{
		unsigned int before;

		sh_at = i;
		if (mode != MODE_WARM && order_spin(OP_PREP) < 0)
		{
			result = -1;
			break;
		}
		/* the entity's inputs where the ME reads them (the box's tick wrote
		them all; in play each job's own would be written back) */
		before = plat_now();
		sceKernelDcacheWritebackAll();
		writeback_us[i] = plat_now() - before;
		sh_first = i;
		before = plat_now();
		if (order_spin(OP_JOB) < 0)
		{
			result = -1;
			break;
		}
		us[i] = plat_now() - before;
	}
	if (!result && order_spin(OP_SYNC_OUT) < 0)
		result = -1;
	sh_spacing = SPACING_IDLE;
	/* what the ME wrote, read fresh */
	sceKernelDcacheWritebackInvalidateAll();
	return result;
}
