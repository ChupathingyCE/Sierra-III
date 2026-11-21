/* box_near_vme.c - the near loop's corner verdicts on the VME (box near-vme,
docs/box_near_vme.md section 9; built in by PSP_NEARVME=1, keep word
"nearvme").

The near corner loop works out each bending corner's verdict, the corner
skinned into camera space and tested against five planes, on the ME's FPU
(~390 ME cycles a corner on b30's ride). Here the ME hands a batch's corners
to the VME a tile at a time instead: it packs the positions in fixed point
and the weights, makes each slot's five plane rows once a batch, and runs the
kernels of psp/src/vme (near_verdicts.vmeg.c for 2-4 slots, near_verdicts_g1
for one), whose words come back as each corner's five plane values. The ME
then tests their signs; a value inside the band (2^-10 units, the kernel's
largest error at the safe32 scales with room) is left to the loop's float
verdict, as is every corner of a batch the box declines. So the verdicts are
the float verdict's wherever the two could differ.

The VME's side is a state machine the packing drives: while one tile runs
its passes the ME packs the next into the other set of buffers, stepping the
machine every few vertices, and waits only when both sets are full.

What runs where: everything here runs on the ME inside the replay job (the
VME is reached only there), or, under gemesim in the emulator, on the main
CPU, where nvme_model stands in for the VME with the same arithmetic (the
lab's nearvme_ref_stream.c, which vmee matched word for word). No VFPU, no
system call and no clock read off the ME: the cycle counter is read only in
a job on the ME (nvme_timing). The only power-side writes are psp_vme.c's two
bus-clock enables. */
#ifdef PSP_NEARVME
#include <string.h>
#include <math.h>
#include "psp_me.h"
#include "box_near_vme.h"
#include "vme/vme-lib.h"

int psp_keep(const char *name);
void platform_log(const char *format, ...);
/* psp_vme.c */
void psp_vme_wipe_counted(unsigned int *waits, unsigned int *bounded);

/* ---------- the kernels' layout (psp/src/vme/near_verdicts*.vmeg.c) */

/* the scales (docs/box_near_vme.md 6.2, safe32): positions at 2^-11, the
rows' rotation parts at 2^-15 and translations at 2^-17, the w word 2^9, so
every product is at 2^-26 and the plane values at 2^-18 */
#define FIX_POSITION 2048.0f
#define FIX_ROTATION 32768.0f
#define FIX_TRANSLATION 131072.0f
#define FIX_W 512u
#define FIX_OUT 262144.0f
/* the band: 2^-10 units, above the kernel's largest error at these scales
(2^-10.34 against doubles, vmee) and the PSP's carry across passes (an LSB) */
#define BAND_LSB 256
/* a plane value's room: 24 bits at 2^-18 is +-32 units; a tile whose bound
passes this is left to the float verdict */
#define OUT_ROOM 31.5f

/* 2-4 slots (G = 4): a row's ring of 16 words, the vertices once a slot */
#define K4_STREAM1 (16 + NVME_TILE * 16)
#define K4_STREAM2 (16 + NVME_TILE * 4)
#define K4_PLANE_AT (48 + K4_STREAM1 + 16 + 16 + 3)
/* one slot (G = 1): a row's ring of 4 words, the vertices once */
#define K1_STREAM1 (4 + NVME_TILE * 4)
#define K1_PLANE_AT (48 + 4 + 3)

/* the length of a context load: the round-4 bench's loads, the ones that
ended, moved 256 words (vme_capability.md section 10; recon/vme-r4) */
#define CONTEXT_LOAD_WORDS 256
/* a wait's bound in the ME's cycles (6 ms): far over any pass or transfer
here, far under the replay's own watchdog */
#define WAIT_BOUND 2000000u

/* ---------- the box's data (the ME's, while its job runs: psp_build.py puts
this unit's variables with the GE layer's) */

/* the context images, built once (near_contexts.h), each a whole load long */
static u32 ctx_dots4[NVME_ROWS][CONTEXT_LOAD_WORDS] __attribute__((aligned(64)));
static u32 ctx_sum4[CONTEXT_LOAD_WORDS] __attribute__((aligned(64)));
static u32 ctx_dots1[NVME_ROWS][CONTEXT_LOAD_WORDS] __attribute__((aligned(64)));

/* the rows of the batch (row, slot, component), the lead-ins' zeros, the
cancel mask (the group's last product, three words ahead of the lag) */
static u32 rows[NVME_ROWS * 16] __attribute__((aligned(64)));
static u32 zeros[32] __attribute__((aligned(64)));
static u32 mask[16] __attribute__((aligned(64)));

/* two sets, so one is packed while the VME reads the other. Each array is
whole lines, so the ME's write-back and drop touch nothing else */
struct nvme_set
{
	u32 vertex[4 * NVME_TILE];
	u32 weight[4 * NVME_TILE];
	u32 plane[NVME_ROWS][NVME_TILE];
	unsigned short index[NVME_TILE];
	int count;
	unsigned int reach; /* an upper bound of the positions' fixed values */
} __attribute__((aligned(64)));
static struct nvme_set sets[2] __attribute__((aligned(64)));

/* one step of a tile on the VME */
enum { OP_LOAD, OP_LOAD_GROUP, OP_CONTEXT, OP_RUN, OP_STORE };
struct nvme_op
{
	unsigned char kind;
	const void *memory;
	unsigned int spad, dims, floor;
};
#define MAX_OPS 40

static struct
{
	int on, check, model_anywhere, fast, acc, keep_read;
	int job_on_me, ready, broken, layout, clocks;
	/* the batch */
	struct nvme_batch in;
	nvme_sink sink;
	void *sink_context;
	int group, model, rows_loaded;
	float reach_l1, reach_t;
	unsigned int batch_at;
	/* the tile in flight */
	struct nvme_set *filling, *flying;
	struct nvme_op ops[MAX_OPS];
	int op_count, op_at, waiting;
	unsigned int wait_mask, wait_at, wait_floor, wait_seen;
} box;

/* the ledger (cycles on the ME; counts everywhere) */
static struct
{
	unsigned long batches, batches_g1, batches_g4[NVME_SLOTS + 1], declined_slots, declined_rows, declined_cpu,
		tiles, tiles_unfit, tiles_failed, vertices, settled, band, not128, waits, never_seen, bounded, wipes;
	unsigned long long cycles_batch, cycles_rows, cycles_issue, cycles_wait, cycles_tests, cycles_runs, cycles_check;
	unsigned long float_corners, float_corners_box;
	unsigned long long float_cycles, float_cycles_box;
	unsigned long check_compared, check_differ, check_band, check_band_differ;
	unsigned long words_compared, words_off_one, words_off_more;
	unsigned long acc_tiles, acc_words, acc_differ, acc_off_one;
	unsigned int acc_bits;
} led;

/* ---------- the ME's cache: what the DMAC reads is written back first, what
it wrote is dropped before the ME reads it */

static void box_writeback(const void *start, unsigned int bytes)
{
	unsigned int line = (unsigned int)start & ~63u, end = (unsigned int)start + bytes;

	if (!box.job_on_me)
		return;
	for (; line < end; line += 64)
		__asm__ __volatile__("cache 0x1b, 0(%0)" : : "r"(line) : "memory");
	__asm__ __volatile__("sync" : : : "memory");
}

static void box_drop(const void *start, unsigned int bytes)
{
	unsigned int line = (unsigned int)start & ~63u, end = (unsigned int)start + bytes;

	if (!box.job_on_me)
		return;
	for (; line < end; line += 64)
		__asm__ __volatile__("cache 0x19, 0(%0)" : : "r"(line) : "memory");
	__asm__ __volatile__("sync" : : : "memory");
}

/* what near_contexts.h's builders call */
static void meCoreMemset(void *dst, int value, unsigned int size)
{
	unsigned int k, word = ((unsigned int)value & 0xffu) * 0x01010101u;

	for (k = 0; k < size / 4; k++)
		((volatile unsigned int *)dst)[k] = word;
}

static void meCoreDcacheWritebackRange(void *start, unsigned int bytes)
{
	box_writeback(start, bytes);
}

#include "vme/near_contexts.h"

/* ---------- keep words */

static void keep_read(void)
{
	if (box.keep_read)
		return;
	box.keep_read = 1;
	box.check = psp_keep("nearvmecheck") != 0;
	box.on = box.check || psp_keep("nearvme") != 0;
	box.model_anywhere = psp_keep("nearvmemodel") != 0;
	/* the DMAC's fast mode (config bits 8-9, vme_capability.md section 9): the
	staging priced in section 8 is at it; "nearvmerest" keeps the rest mode */
	box.fast = !psp_keep("nearvmerest");
	box.acc = psp_keep("nearvmeacc") ? 2 : 0;
}

int nvme_on(void)
{
	keep_read();
	return box.on;
}

int nvme_checking(void)
{
	keep_read();
	return box.check;
}

int nvme_timing(void)
{
	return box.job_on_me;
}

/* ---------- the VME's side, a step at a time */

static void op_issue(const struct nvme_op *op)
{
	switch (op->kind)
	{
	case OP_LOAD:
		vme_dma(MEMORY, ADDR, useg_mem((u32)op->memory));
		vme_dma(ITERATION, DIMS, op->dims);
		vme_dma(SPAD, OFFSET, op->spad);
		vme_dma(CTRL, VALUE, 0x40);
		box.wait_mask = 0x200;
		break;
	case OP_LOAD_GROUP:
		/* a slot's copy of each vertex: groups of 4 words, 16 words apart */
		vme_dma(MEMORY, ADDR, useg_mem((u32)op->memory));
		vme_dma(ITERATION, DIMS, op->dims, 0);
		vme_dma(ITERATION, STEP, 4);
		vme_dma(GROUP, SIZE, 3);
		vme_dma(GROUP, STEPS, 16, 1);
		vme_dma(SPAD, OFFSET, op->spad);
		vme_dma(CTRL, VALUE, 0x50);
		box.wait_mask = 0x200;
		break;
	case OP_CONTEXT:
		/* vme2-next-level's vmeLibSendCustomContext without its trigger (the
		next step's), the length set first as the round-4 bench's good loads */
		vmeLibSetFlow(VME_DEFAULT);
		__asm__ __volatile__("sync" : : : "memory");
		vmeLibConfigTransfer(0x1c);
		hw(0x440ff014) = CONTEXT_LOAD_WORDS - 1;
		hw(0x440ff018) = 0;
		hw(0x440ff010) = useg_mem((u32)op->memory);
		hw(0x440ff008) = 0x0c;
		__asm__ __volatile__("sync" : : : "memory");
		box.wait_mask = 0x200;
		break;
	case OP_RUN:
		vmeLibTrigger();
		box.wait_mask = 0x800;
		break;
	default: /* OP_STORE: a row's plane value on each group's last word */
		vme_dma(MEMORY, ADDR, useg_mem((u32)op->memory));
		vme_dma(ITERATION, DIMS, NVME_TILE - 1, 3);
		vme_dma(ITERATION, STEP, 4);
		vme_dma(GROUP, SIZE, 0);
		vme_dma(GROUP, STEPS, 0, 0);
		vme_dma(SPAD, OFFSET, op->spad);
		vme_dma(CTRL, VALUE, 0x58);
		box.wait_mask = 0x200;
		break;
	}
	box.wait_at = nvme_cycles();
	box.wait_floor = op->floor;
	box.wait_seen = 0;
	box.waiting = 1;
}

/* 1 done, 0 not yet, -1 past its bound. The busy bit may come and go before
the first look (vme_round5.md: 7 of 8 waits never saw it), so a wait that
never saw it ends only once the step's least time has passed */
static int op_done(void)
{
	unsigned int now = nvme_cycles(), status = hw(0x440ff000);

	if (status & box.wait_mask)
	{
		box.wait_seen = 1;
		return now - box.wait_at > WAIT_BOUND ? -1 : 0;
	}
	if (!box.wait_seen && now - box.wait_at < box.wait_floor)
		return 0;
	led.waits++;
	if (!box.wait_seen)
		led.never_seen++;
	/* the firmware's closing write (psp_vme.c's vme_wait) */
	hw(0x440ff000) = (box.wait_mask >> 8) | 0x80;
	__asm__ __volatile__("sync" : : : "memory");
	box.waiting = 0;
	return 1;
}

static void ops_add(int kind, const void *memory, unsigned int spad, unsigned int dims, unsigned int floor)
{
	struct nvme_op *op = &box.ops[box.op_count++];

	op->kind = (unsigned char)kind;
	op->memory = memory;
	op->spad = spad;
	op->dims = dims;
	op->floor = floor;
}

/* a tile's steps: the batch's rows and the lead-ins' zeros only when the
buffers don't hold them, then the vertices and weights, then a context, a
pass and a store a row (two contexts and passes a row at 2-4 slots). The
order is the vmeg tile's that vmee proved, a transfer at a time */
static void tile_program(struct nvme_set *set)
{
	int r, s;

	box.op_count = box.op_at = 0;
	if (box.group == 4)
	{
		if (!box.rows_loaded)
			ops_add(OP_LOAD, rows, VME_TOP_BUFF0_WOFF, NVME_ROWS * 16 - 1, 2 * NVME_ROWS * 16);
		if (box.layout != 4)
		{
			ops_add(OP_LOAD, mask, VME_TOP_BUFF2_WOFF, 7, 16);
			ops_add(OP_LOAD, zeros, VME_TOP_BUFF1_WOFF, 15, 32);
			ops_add(OP_LOAD, zeros, VME_TOP_BUFF3_WOFF, 15, 32);
		}
		ops_add(OP_LOAD, set->weight, VME_TOP_BUFF3_WOFF + 0x10, 4 * NVME_TILE - 1, 8 * NVME_TILE);
		/* a spare slot's copy isn't sent: its rows are 0, so whatever its
		words hold gives 0, and its weight multiplies 0 */
		for (s = 0; s < box.in.slots; s++)
			ops_add(OP_LOAD_GROUP, set->vertex, VME_TOP_BUFF1_WOFF + 0x10 + 4 * s, 4 * NVME_TILE - 1, 8 * NVME_TILE);
		for (r = 0; r < NVME_ROWS; r++)
		{
			ops_add(OP_CONTEXT, ctx_dots4[r], 0, 0, 2 * VME_CONTEXT_WORD_COUNT);
			ops_add(OP_RUN, NULL, 0, 0, 72 + 2 * K4_STREAM1);
			ops_add(OP_CONTEXT, ctx_sum4, 0, 0, 2 * VME_CONTEXT_WORD_COUNT);
			ops_add(OP_RUN, NULL, 0, 0, 72 + 2 * K4_STREAM2);
			ops_add(OP_STORE, set->plane[r], VME_BASE_BUFF3_WOFF + K4_PLANE_AT, 0, 2 * NVME_TILE);
		}
	}
	else
	{
		if (!box.rows_loaded)
			ops_add(OP_LOAD, rows, VME_TOP_BUFF0_WOFF, NVME_ROWS * 4 - 1, 2 * NVME_ROWS * 4);
		if (box.layout != 1)
		{
			ops_add(OP_LOAD, mask, VME_TOP_BUFF2_WOFF, 7, 16);
			ops_add(OP_LOAD, zeros, VME_TOP_BUFF1_WOFF, 3, 8);
		}
		ops_add(OP_LOAD, set->vertex, VME_TOP_BUFF1_WOFF + 4, 4 * NVME_TILE - 1, 8 * NVME_TILE);
		for (r = 0; r < NVME_ROWS; r++)
		{
			ops_add(OP_CONTEXT, ctx_dots1[r], 0, 0, 2 * VME_CONTEXT_WORD_COUNT);
			ops_add(OP_RUN, NULL, 0, 0, 72 + 2 * K1_STREAM1);
			ops_add(OP_STORE, set->plane[r], VME_BASE_BUFF3_WOFF + K1_PLANE_AT, 0, 2 * NVME_TILE);
		}
	}
	box.rows_loaded = 1;
	box.layout = box.group;
}

/* the tile in flight, moved on as far as it goes without waiting: 1 when it
has finished (or failed: box.broken) */
static int tile_step(void)
{
	unsigned int start = nvme_cycles();
	int done = 0;

	for (;;)
	{
		if (box.waiting)
		{
			int d = op_done();

			if (d < 0)
			{
				led.bounded++;
				led.tiles_failed++;
				box.broken = 1;
				box.ready = 0; /* wiped again at the next job */
				box.waiting = 0;
				done = 1;
				break;
			}
			if (!d)
				break;
			box.op_at++;
		}
		if (box.op_at >= box.op_count)
		{
			done = 1;
			break;
		}
		op_issue(&box.ops[box.op_at]);
	}
	led.cycles_issue += nvme_cycles() - start;
	return done;
}

/* ---------- the stand-in: the kernels' arithmetic on the CPU (the lab's
nearvme_ref_stream.c: running sums from 0 each pass, floored, 24-bit words) */

static int word24(long long x)
{
	unsigned int low = (unsigned int)x & 0xffffffu;

	return (low & 0x800000u) ? (int)low - 0x1000000 : (int)low;
}

static long long floor_shift(long long x, int k)
{
	return x >= 0 ? x >> k : -((-x + ((long long)1 << k) - 1) >> k);
}

static int model_dots[NVME_TILE][NVME_SLOTS];
static int model_out[NVME_ROWS][NVME_TILE];

static unsigned int model_tile(const struct nvme_set *set, int group, int count)
{
	int r, j, s, c;
	unsigned int most = 0;

	for (r = 0; r < NVME_ROWS; r++)
	{
		long long sum = 0, before;

		for (j = 0; j < count; j++)
			for (s = 0; s < group; s++)
			{
				before = sum;
				for (c = 0; c < 4; c++)
					sum += (long long)word24(rows[r * 4 * group + 4 * s + c]) * word24(set->vertex[4 * j + c]);
				model_dots[j][s] = word24((long long)word24(floor_shift(sum, 8)) - word24(floor_shift(before, 8)));
				{
					unsigned long long a = (unsigned long long)(sum < 0 ? -sum : sum);
					unsigned int bits = 0;

					while (a >> bits)
						bits++;
					if (bits > most)
						most = bits;
				}
			}
		if (group == 1)
		{
			for (j = 0; j < count; j++)
				model_out[r][j] = model_dots[j][0];
			continue;
		}
		sum = 0;
		for (j = 0; j < count; j++)
		{
			before = sum;
			for (s = 0; s < group; s++)
				sum += (long long)model_dots[j][s] * word24(set->weight[4 * j + s]);
			model_out[r][j] = word24((long long)word24(floor_shift(sum, 7)) - word24(floor_shift(before, 7)));
		}
	}
	return most;
}

/* ---------- a tile's words to verdicts */

static inline int plane_value(u32 word)
{
	return (int)(word << 8) >> 8;
}

static void tile_verdicts(struct nvme_set *set, int failed)
{
	unsigned int start = box.job_on_me ? nvme_cycles() : 0;
	int near_fx = (int)(box.in.near_clip * FIX_OUT + (box.in.near_clip >= 0.0f ? 0.5f : -0.5f));
	int limit_fx = (int)(box.in.limit * FIX_OUT + (box.in.limit >= 0.0f ? 0.5f : -0.5f));
	int j, r;

	if (!failed && !box.model)
	{
		box_drop(set->plane, sizeof(set->plane));
		if (box.check)
		{
			/* the VME's words against the stand-in's: the PSP's carry across
			passes may move a word by one. Its cycles are the check's, not the
			box's */
			unsigned int check_at = nvme_cycles();

			model_tile(set, box.group, set->count);
			for (r = 0; r < NVME_ROWS; r++)
				for (j = 0; j < set->count; j++)
				{
					int d = plane_value(set->plane[r][j]) - model_out[r][j];

					led.words_compared++;
					if (d == 1 || d == -1)
						led.words_off_one++;
					else if (d)
						led.words_off_more++;
				}
			led.cycles_check += nvme_cycles() - check_at;
		}
	}
	else if (!failed)
	{
		model_tile(set, box.group, set->count);
		for (r = 0; r < NVME_ROWS; r++)
			for (j = 0; j < set->count; j++)
				set->plane[r][j] = (u32)model_out[r][j];
	}
	for (j = 0; j < set->count; j++)
	{
		int z, v1, v2, v3, v4, bits, code;

		if (failed)
		{
			led.band++;
			box.sink(box.sink_context, set->index[j], NVME_BAND);
			continue;
		}
		z = plane_value(set->plane[0][j]);
		v1 = plane_value(set->plane[1][j]);
		v2 = plane_value(set->plane[2][j]);
		v3 = plane_value(set->plane[3][j]);
		v4 = plane_value(set->plane[4][j]);
		/* inside the band of any tested plane: the float verdict decides */
		if ((unsigned int)(z - near_fx + BAND_LSB - 1) < 2 * BAND_LSB - 1 ||
			(unsigned int)(z - limit_fx + BAND_LSB - 1) < 2 * BAND_LSB - 1 ||
			(unsigned int)(v1 + BAND_LSB - 1) < 2 * BAND_LSB - 1 || (unsigned int)(v2 + BAND_LSB - 1) < 2 * BAND_LSB - 1 ||
			(unsigned int)(v3 + BAND_LSB - 1) < 2 * BAND_LSB - 1 || (unsigned int)(v4 + BAND_LSB - 1) < 2 * BAND_LSB - 1)
		{
			led.band++;
			box.sink(box.sink_context, set->index[j], NVME_BAND);
			continue;
		}
		/* outside()'s bits and camera_corner_out()'s 32, from the signs: the
		side rows are the planes' distances inside them (psp_ge.c, plane_distance) */
		bits = (z < near_fx) | ((v1 < 0) << 1) | ((v2 < 0) << 2) | ((v3 < 0) << 3) | ((v4 < 0) << 4);
		code = bits | ((z < limit_fx || (bits & 30)) ? 32 : 0);
		led.settled++;
		box.sink(box.sink_context, set->index[j], code);
	}
	set->count = 0;
	set->reach = 0;
	if (box.job_on_me)
		led.cycles_tests += nvme_cycles() - start;
}

/* ---------- tiles */

/* the tile just filled goes to the VME (or the stand-in), waiting first for
the one in flight */
static void tile_finish_flying(void)
{
	if (!box.flying)
		return;
	if (!box.model && !box.broken)
	{
		/* the ME has nothing left to pack: what it spins here is the wait,
		less the steps it issues meanwhile (counted as steps) */
		unsigned long long issued = led.cycles_issue;
		unsigned int start = nvme_cycles();

		while (!tile_step())
			;
		led.cycles_wait += (nvme_cycles() - start) - (led.cycles_issue - issued);
	}
	tile_verdicts(box.flying, box.broken && !box.model);
	box.flying = NULL;
}

static void tile_launch(void)
{
	struct nvme_set *set = box.filling;
	float reach = (float)set->reach * (1.0f / FIX_POSITION);

	if (!set->count)
		return;
	tile_finish_flying();
	led.tiles++;
	if (set->reach >= (1u << 23) || box.reach_l1 * reach + box.reach_t >= OUT_ROOM)
	{
		/* a plane value could pass the words' +-32 units */
		led.tiles_unfit++;
		{
			int j;

			for (j = 0; j < set->count; j++)
			{
				led.band++;
				box.sink(box.sink_context, set->index[j], NVME_BAND);
			}
		}
		set->count = 0;
		set->reach = 0;
		return;
	}
	box.flying = set;
	box.filling = set == &sets[0] ? &sets[1] : &sets[0];
	if (box.model || box.broken)
		return;
	if (!box.rows_loaded)
		box_writeback(rows, sizeof(rows));
	box_writeback(set->vertex, sizeof(set->vertex));
	if (box.group == 4)
		box_writeback(set->weight, sizeof(set->weight));
	tile_program(set);
	tile_step();
}

/* ---------- the batch's rows: each slot's camera rows dotted with the five
planes (P M_s), in fixed point */

static int rows_make(void)
{
	const struct nvme_batch *in = &box.in;
	float lx = 1.0f / in->guard_x_length, ly = 1.0f / in->guard_y_length, gx = in->guard_x, gy = in->guard_y;
	float most_l1 = 0.0f, most_t = 0.0f;
	int g = box.group, s, r, c;

	for (s = 0; s < g; s++)
	{
		float l1[NVME_ROWS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

		for (c = 0; c < 4; c++)
		{
			float v[NVME_ROWS], scale = c < 3 ? FIX_ROTATION : FIX_TRANSLATION;

			if (s < in->slots)
			{
				float x = in->depth_row[s][0][c], y = in->depth_row[s][1][c], z = in->depth_row[s][2][c];

				/* z, then (g z -+ x) / |(g, 1)| and (g z -+ y) / |(g, 1)|: the
				planes' distances, as the census measured them */
				v[0] = z;
				v[1] = (gx * z - x) * lx;
				v[2] = (gx * z + x) * lx;
				v[3] = (gy * z - y) * ly;
				v[4] = (gy * z + y) * ly;
			}
			else
				v[0] = v[1] = v[2] = v[3] = v[4] = 0.0f;
			for (r = 0; r < NVME_ROWS; r++)
			{
				float f = v[r] * scale;

				if (fabsf(f) > 8388000.0f)
					return 0;
				rows[r * 4 * g + 4 * s + c] = (u32)(int)(f + (f >= 0.0f ? 0.5f : -0.5f));
				if (c < 3)
					l1[r] += fabsf(v[r]);
				else if (fabsf(v[r]) > most_t)
					most_t = fabsf(v[r]);
			}
		}
		for (r = 0; r < NVME_ROWS; r++)
			if (l1[r] > most_l1)
				most_l1 = l1[r];
	}
	box.reach_l1 = most_l1;
	box.reach_t = most_t;
	return 1;
}

/* ---------- the accumulator check ("nearvmeacc"): two tiles whose running
sums pass 2^31 by far, the VME's words against the stand-in's. The safe32
words are right whether the sum wraps or is wide (they need only its low
24 + k bits); a sum that saturates gets them wrong */

static unsigned int acc_seed = 0x2468ace1u;

static int acc_word(int positive)
{
	int w;

	acc_seed = acc_seed * 1664525u + 1013904223u;
	w = (int)(acc_seed >> 9) - (1 << 22);
	if (positive)
		w = (int)(acc_seed >> 9) & 0x7fffff;
	return w;
}

static void acc_check(void)
{
	unsigned long long issued = led.cycles_issue;
	unsigned long waits = led.waits;
	int t;

	for (t = 0; t < 2; t++)
	{
		struct nvme_set *set = &sets[0];
		unsigned int bits;
		int k, r, j;

		for (k = 0; k < NVME_ROWS * 16; k++)
			rows[k] = (u32)acc_word(t);
		for (k = 0; k < 4 * NVME_TILE; k++)
		{
			set->vertex[k] = (u32)acc_word(t);
			set->weight[k] = (u32)(acc_word(1) & 0x7f);
		}
		set->count = NVME_TILE;
		box.group = 4;
		box.in.slots = 4;
		box.rows_loaded = 0;
		box_writeback(rows, sizeof(rows));
		box_writeback(set->vertex, sizeof(set->vertex));
		box_writeback(set->weight, sizeof(set->weight));
		tile_program(set);
		while (!tile_step())
			;
		if (box.broken)
			return;
		box_drop(set->plane, sizeof(set->plane));
		bits = model_tile(set, 4, NVME_TILE);
		if (bits > led.acc_bits)
			led.acc_bits = bits;
		led.acc_tiles++;
		for (r = 0; r < NVME_ROWS; r++)
			for (j = 0; j < NVME_TILE; j++)
			{
				int d = plane_value(set->plane[r][j]) - model_out[r][j];

				led.acc_words++;
				if (d)
					led.acc_differ++;
				if (d == 1 || d == -1)
					led.acc_off_one++;
			}
		set->count = 0;
	}
	/* the rows aren't the next batch's, and its steps aren't the batches' */
	box.rows_loaded = 0;
	led.cycles_issue = issued;
	led.waits = waits;
}

/* ---------- the job */

void nvme_job_begin(int on_me)
{
	keep_read();
	box.job_on_me = on_me && psp_on_me();
	box.broken = 0;
	box.layout = 0;
	box.clocks = 0;
	if (!box.on || !box.job_on_me)
		return;
	vmeLibEnable();
	box.clocks = 1;
	if (!box.ready)
	{
		unsigned int waits = 0, bounded = 0;
		int r;

		/* vmetest's set-up (psp_vme.c), once, and again after a failure */
		psp_vme_wipe_counted(&waits, &bounded);
		led.wipes++;
		led.waits += waits;
		led.bounded += bounded;
		/* every word the DMAC reads is written here, on the ME, and written
		back: a line the main CPU zeroed at start could still be in its cache */
		meCoreMemset(ctx_dots4, 0, sizeof(ctx_dots4));
		meCoreMemset(ctx_dots1, 0, sizeof(ctx_dots1));
		meCoreMemset(ctx_sum4, 0, sizeof(ctx_sum4));
		for (r = 0; r < NVME_ROWS; r++)
		{
			nearDotsBuild(ctx_dots4[r], r);
			nearDots1Build(ctx_dots1[r], r);
		}
		nearSumBuild(ctx_sum4);
		box_writeback(ctx_dots4, sizeof(ctx_dots4));
		box_writeback(ctx_dots1, sizeof(ctx_dots1));
		box_writeback(ctx_sum4, sizeof(ctx_sum4));
		meCoreMemset(zeros, 0, sizeof(zeros));
		meCoreMemset(mask, 0, sizeof(mask));
		mask[2] = 0xffffffffu;
		box_writeback(mask, sizeof(mask));
		box_writeback(zeros, sizeof(zeros));
		box.ready = 1;
	}
	if (box.fast)
		hw(0x440ff004) |= 0x300;
	if (box.acc > 0)
	{
		box.acc--;
		acc_check();
		box.layout = 0;
	}
}

void nvme_job_end(void)
{
	if (box.clocks)
	{
		if (box.fast)
			hw(0x440ff004) &= ~0x300u;
		vmeLibDisable();
	}
	box.clocks = 0;
	box.job_on_me = 0;
}

/* ---------- the batch */

int nvme_batch_begin(const struct nvme_batch *in, nvme_sink sink, void *context)
{
	unsigned int start;

	keep_read();
	if (!box.on)
		return 0;
	if (in->slots < 1 || in->slots > NVME_SLOTS)
	{
		led.declined_slots++;
		return 0;
	}
	/* the VME in the ME's job; the stand-in on the main CPU under gemesim (the
	emulator), or anywhere off the ME with "nearvmemodel" */
	box.model = !box.job_on_me;
	if (box.model && (psp_on_me() || !(PSP_ME_WORK() || box.model_anywhere)))
	{
		led.declined_cpu++;
		return 0;
	}
	start = box.job_on_me ? nvme_cycles() : 0;
	box.in = *in;
	box.sink = sink;
	box.sink_context = context;
	box.group = in->slots == 1 ? 1 : 4;
	if (!rows_make())
	{
		led.declined_rows++;
		return 0;
	}
	box.rows_loaded = 0;
	box.filling = &sets[0];
	box.flying = NULL;
	sets[0].count = sets[1].count = 0;
	sets[0].reach = sets[1].reach = 0;
	led.batches++;
	if (box.group == 1)
		led.batches_g1++;
	else
		led.batches_g4[in->slots]++;
	if (box.job_on_me)
	{
		unsigned int now = nvme_cycles();

		led.cycles_rows += now - start;
		box.batch_at = start;
	}
	return 1;
}

void nvme_vertex(unsigned short index)
{
	struct nvme_set *set = box.filling;
	const unsigned char *record = box.in.vertices + box.in.stride * index;
	const float *p = (const float *)(record + box.in.position_at);
	u32 *v = set->vertex + 4 * set->count;
	int x, y, z, n = set->count;

	led.vertices++;
	if (box.group == 1 && record[0] != 128)
	{
		/* one slot's plane value is its dot product only at weight 128 */
		led.not128++;
		led.band++;
		box.sink(box.sink_context, index, NVME_BAND);
		return;
	}
	x = (int)(p[0] * FIX_POSITION + (p[0] >= 0.0f ? 0.5f : -0.5f));
	y = (int)(p[1] * FIX_POSITION + (p[1] >= 0.0f ? 0.5f : -0.5f));
	z = (int)(p[2] * FIX_POSITION + (p[2] >= 0.0f ? 0.5f : -0.5f));
	v[0] = (u32)x;
	v[1] = (u32)y;
	v[2] = (u32)z;
	v[3] = FIX_W;
	set->reach |= (unsigned int)(x < 0 ? -x : x) | (unsigned int)(y < 0 ? -y : y) | (unsigned int)(z < 0 ? -z : z);
	if (box.group == 4)
	{
		u32 *w = set->weight + 4 * n;
		int s;

		for (s = 0; s < 4; s++)
			w[s] = s < box.in.slots ? record[s] : 0;
	}
	set->index[n] = index;
	set->count = n + 1;
	if (set->count == NVME_TILE)
		tile_launch();
	else if ((n & 7) == 7 && box.flying && !box.model && !box.broken && box.waiting)
	{
		/* the VME moved on while the ME packed */
		tile_step();
	}
}

void nvme_batch_end(void)
{
	tile_launch();
	tile_finish_flying();
	if (box.job_on_me)
		led.cycles_batch += nvme_cycles() - box.batch_at;
}

/* ---------- the ledger's other entries */

void nvme_note_runs(unsigned int cycles)
{
	led.cycles_runs += cycles;
}

void nvme_note_float(unsigned int cycles, int in_box_batch)
{
	led.float_corners++;
	led.float_cycles += cycles;
	if (in_box_batch)
	{
		led.float_corners_box++;
		led.float_cycles_box += cycles;
	}
}

void nvme_note_check(int box_code, int float_code)
{
	if (box_code == NVME_BAND)
	{
		led.check_band++;
		return;
	}
	led.check_compared++;
	if (box_code != float_code)
		led.check_differ++;
}

void nvme_note_check_cycles(unsigned int cycles)
{
	led.cycles_check += cycles;
}

void nvme_report(double frames)
{
	double v = led.vertices ? (double)led.vertices : 1.0;
	/* the box's cycles a vertex, all in: the batch from its rows to its last
	verdict (less the runs the walk works out for the loop, which the loop
	no longer does, and nearvmecheck's own work), and the band's float
	verdicts in the loop */
	double box_cycles = (double)(led.cycles_batch - led.cycles_runs - led.cycles_check) + (double)led.float_cycles_box;
	double float_only = (double)(led.float_cycles - led.float_cycles_box);
	double float_n = (double)(led.float_corners - led.float_corners_box);

	if (frames <= 0.0)
		frames = 1.0;
	keep_read();
	platform_log("near-vme: %s; batches %.1f a frame (one slot %.1f, 2/3/4 slots %.1f/%.1f/%.1f), declined %.1f (5-8 slots %.1f, rows past the words %.1f, off the ME %.1f); tiles %.1f a frame (%.1f past the words, %lu failed); vertices %.1f a frame: settled %.1f, to the float verdict %.1f (%.2f%%; weight not 128 %.1f)",
		box.check ? "on, nearvmecheck" : box.on ? "on" : "off", led.batches / frames, led.batches_g1 / frames,
		led.batches_g4[2] / frames, led.batches_g4[3] / frames, led.batches_g4[4] / frames,
		(led.declined_slots + led.declined_rows + led.declined_cpu) / frames, led.declined_slots / frames,
		led.declined_rows / frames, led.declined_cpu / frames, led.tiles / frames, led.tiles_unfit / frames,
		led.tiles_failed, led.vertices / frames, led.settled / frames, led.band / frames, 100.0 * led.band / v,
		led.not128 / frames);
	platform_log("near-vme: ME cycles a vertex, all in %.1f (rows %.1f, walk and packing %.1f, the VME's steps %.1f, waiting %.1f, signs and band %.1f, the band's float verdicts %.1f); the walk's runs %.1f a vertex (the loop's before); the float verdict %.1f cycles a corner outside the box's batches (%.0f corners), %.1f a frame of them; waits %lu (busy never seen %lu, past their bound %lu), wipes %lu",
		box_cycles / v, (double)led.cycles_rows / v,
		(double)(led.cycles_batch - led.cycles_rows - led.cycles_issue - led.cycles_wait - led.cycles_tests - led.cycles_runs) / v,
		(double)led.cycles_issue / v, (double)led.cycles_wait / v, (double)(led.cycles_tests - led.cycles_check) / v,
		(double)led.float_cycles_box / v, (double)led.cycles_runs / v, float_n > 0.0 ? float_only / float_n : 0.0, float_n,
		float_n / frames, led.waits, led.never_seen, led.bounded, led.wipes);
	if (box.check)
		platform_log("near-vme: nearvmecheck: %lu verdicts outside the band compared with the float verdict, %lu DISAGREED; %lu in the band; the VME's words against the stand-in's: %lu, %lu off by one, %lu off by more; the check's own %.1f cycles a vertex, kept out of the figures above",
			led.check_compared, led.check_differ, led.check_band, led.words_compared, led.words_off_one, led.words_off_more,
			(double)led.cycles_check / v);
	if (led.acc_tiles)
		platform_log("near-vme: nearvmeacc: %lu tiles, running sums to 2^%u; %lu words, %lu differ (%lu by one)",
			led.acc_tiles, led.acc_bits, led.acc_words, led.acc_differ, led.acc_off_one);
}

#endif
