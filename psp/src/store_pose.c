/*
store_pose.c: the world store's posing code (slice 1's task 3, step 5)

docs/box_world_store.md section 17, step 5 (accepted by the lead 2026-11-25 07:5x, Q1 (b)): POSE's arena has held
every object's node blocks since spec section 35 (PSP_TICK2=pose), and the store's posing code moves here, out of
objects.c, so it reaches the arena by the record's runs and not by object_get and the engine's node blocks. It moves
piece by piece, each piece verbatim, so each is exact by construction, and each piece's cut in psp_fixups.py
(tick2_posenew_move_cut) refuses the build if the text here and the text it takes out of objects.c differ.

Built only with PSP_TICK2_POSENEW=1 (psp_build.py), as objects.c is: its flags and the fixed-up objects.h.

Moved so far:
- POSE's arena allocator (psp_fixups.py TICK2_POSE_CODE): the node runs' best-fit allocator, their overflow chunks,
  the block getter by record (tick2_pose_block) and the collector's spare count. Four functions are called from
  objects.c (tick2_pose_take, tick2_pose_give, tick2_pose_block, tick2_pose_spare_bytes), so they lose their static;
  nothing else changes.
To move (the same section's list): B's slot table and its ME copy, NMSKIP's tables and their ME copy, store_pose_box,
store_pose_make_nodes, store_pose_make_skipped / _at, store_pose_settle / _in and the two accessors, keyed by record
(the record found at the boundary from the header's datum's root, Q1 (b)), the ME's names kept.
*/

#include "cseries.h"
#include "math/real_math.h"
#include "objects/objects.h"

/* objects.c (psp_fixups.py tick2_chunk_cut): an overflow chunk of any section, from the object pool (the heap in the
proof form); not static there once step 5 builds, since the allocator here takes POSE's chunks */
boolean tick2_chunk_take(void **reference, long size);
void tick2_chunk_give(void **reference);

/* ---- moved from objects.c (psp_fixups.py TICK2_POSE_CODE, after tick2_chunk_calls): begin ---- */

struct tick2_pose_store *tick2_pose_store; /* TICK2 pose */

/* a node's element in one of a run's arrays: the arena's, or an overflow chunk's */
static void *tick2_pose_at(struct tick2_pose_run const *run, long first, long array)
{
	if (first < run->nodes)
		return run->base + (array * run->nodes + first) * run->stride;
	first -= run->nodes;
	return run->chunk[first / TICK2_POSE_CHUNK] + (array * run->chunk_cap[first / TICK2_POSE_CHUNK] + first % TICK2_POSE_CHUNK) * run->stride;
}

/* the arena's allocator (the lead, 2026-11-23 17:5x): best fit, the remainder listed again, and a freed run merged
with its free neighbours before it's listed, so freed space isn't held for one length (tested on the host over 2,000,000
random takes and gives: no overlap, every free run listed once and merged, the counts consistent) */
static unsigned short *tick2_pose_word(struct tick2_pose_run const *run, long node)
{
	return (unsigned short *)(run->base + node * run->stride);
}
static unsigned short *tick2_pose_list(struct tick2_pose_run *run, long length)
{
	return length <= TICK2_POSE_RUN_MOST ? &run->free_head[length] : &run->big_head;
}
static void tick2_pose_bits(struct tick2_pose_run *run, long first, long count, int set)
{
	long k;

	for (k = first; k < first + count; k++)
		if (set)
			run->free_bits[k >> 3] |= (unsigned char)(1 << (k & 7));
		else
			run->free_bits[k >> 3] &= (unsigned char)~(1 << (k & 7));
}
static int tick2_pose_is_free(struct tick2_pose_run const *run, long node)
{
	return node >= 0 && node < run->bump && ((run->free_bits[node >> 3] >> (node & 7)) & 1);
}
/* a free run's first element holds next, previous and length; its last holds the length again */
static void tick2_pose_link(struct tick2_pose_run *run, long first, long length)
{
	unsigned short *head = tick2_pose_list(run, length), *w = tick2_pose_word(run, first);

	w[0] = *head;
	w[1] = TICK2_POSE_NONE;
	w[2] = (unsigned short)length;
	tick2_pose_word(run, first + length - 1)[4] = (unsigned short)length;
	if (*head != TICK2_POSE_NONE)
		tick2_pose_word(run, *head)[1] = (unsigned short)first;
	*head = (unsigned short)first;
	tick2_pose_bits(run, first, length, 1);
}
static void tick2_pose_unlink(struct tick2_pose_run *run, long first)
{
	unsigned short *w = tick2_pose_word(run, first);
	long length = w[2];

	if (w[1] != TICK2_POSE_NONE)
		tick2_pose_word(run, w[1])[0] = w[0];
	else
		*tick2_pose_list(run, length) = w[0];
	if (w[0] != TICK2_POSE_NONE)
		tick2_pose_word(run, w[0])[1] = w[1];
	tick2_pose_bits(run, first, length, 0);
}
/* the smallest free run at least as long, its remainder listed again; else the tail */
static unsigned short tick2_pose_take_arena(struct tick2_pose_run *run, long count)
{
	long length, best = TICK2_POSE_NONE, best_length = 0;

	for (length = count; length <= TICK2_POSE_RUN_MOST && best == TICK2_POSE_NONE; length++)
		if (run->free_head[length] != TICK2_POSE_NONE)
			best = run->free_head[length], best_length = length;
	if (best == TICK2_POSE_NONE)
	{
		unsigned short at;

		for (at = run->big_head; at != TICK2_POSE_NONE; at = tick2_pose_word(run, at)[0])
			if (tick2_pose_word(run, at)[2] >= count && (best == TICK2_POSE_NONE || tick2_pose_word(run, at)[2] < best_length))
				best = at, best_length = tick2_pose_word(run, at)[2];
	}
	if (best != TICK2_POSE_NONE)
	{
		tick2_pose_unlink(run, best);
		if (best_length > count)
			tick2_pose_link(run, best + count, best_length - count);
		run->live += count;
		return (unsigned short)best;
	}
	if (run->bump + count <= run->nodes)
	{
		unsigned short first = (unsigned short)run->bump;

		run->bump += count;
		run->live += count;
		return first;
	}
	return TICK2_POSE_NONE;
}
/* a run given back, merged with its free neighbours; one that reaches the tail shortens it */
static void tick2_pose_give_arena(struct tick2_pose_run *run, long first, long count)
{
	run->live -= count;
	if (tick2_pose_is_free(run, first - 1))
	{
		long left = tick2_pose_word(run, first - 1)[4];

		tick2_pose_unlink(run, first - left);
		first -= left;
		count += left;
	}
	if (first + count == run->bump)
	{
		run->bump = first;
		return;
	}
	if (tick2_pose_is_free(run, first + count))
	{
		long right = tick2_pose_word(run, first + count)[2];

		tick2_pose_unlink(run, first + count);
		count += right;
	}
	tick2_pose_link(run, first, count);
}

/* a run of nodes: the arena's best free run or its tail, else an overflow chunk's */
unsigned short tick2_pose_take(struct tick2_pose_run *run, long count)
{
	long c;

	if (count <= 0 || count > TICK2_POSE_RUN_MOST)
		return TICK2_POSE_NONE;
	{
		unsigned short first = tick2_pose_take_arena(run, count);

		if (first != TICK2_POSE_NONE)
		{
			if (run->live > run->live_peak)
				run->live_peak = run->live;
			return first;
		}
	}
	for (c = 0; c < TICK2_POSE_CHUNKS; c++)
		if (run->chunk[c] && run->chunk_bump[c] + count <= run->chunk_cap[c])
			break;
	if (c == TICK2_POSE_CHUNKS)
	{
		long cap = (count + TICK2_POSE_GRAIN - 1) & ~(long)(TICK2_POSE_GRAIN - 1);

		for (c = 0; c < TICK2_POSE_CHUNKS && run->chunk[c]; c++)
			;
		if (c == TICK2_POSE_CHUNKS ||
			!tick2_chunk_take((void **)&run->chunk[c], run->arrays * cap * run->stride))
			return TICK2_POSE_NONE;
		run->chunk_bump[c] = run->chunk_live[c] = 0;
		run->chunk_cap[c] = (short)cap;
	}
	run->chunk_bump[c] += (short)count;
	run->chunk_live[c] += (short)count;
	run->overflow_taken += count;
	run->overflow_live += count;
	if (run->overflow_live > run->overflow_peak)
		run->overflow_peak = run->overflow_live;
	return (unsigned short)(run->nodes + c * TICK2_POSE_CHUNK + run->chunk_bump[c] - count);
}

static void tick2_pose_give_run(struct tick2_pose_run *run, long first, long count)
{
	if (first == TICK2_POSE_NONE || count <= 0)
		return;
	if (first < run->nodes)
	{
		tick2_pose_give_arena(run, first, count);
		return;
	}
	first = (first - run->nodes) / TICK2_POSE_CHUNK;
	run->overflow_live -= count;
	if ((run->chunk_live[first] -= (short)count) <= 0)
	{ /* a chunk with no live run goes back to the pool (its tail is only reused once it's empty) */
		tick2_chunk_give((void **)&run->chunk[first]);
		run->chunk[first] = NULL;
		run->chunk_bump[first] = run->chunk_live[first] = run->chunk_cap[first] = 0;
	}
}

void tick2_pose_give(struct tick2_root *record)
{
	tick2_pose_give_run(&tick2_pose_store->matrices, record->pose_first, record->pose_count);
	tick2_pose_give_run(&tick2_pose_store->orientations, record->pose_orient, record->pose_count);
	record->pose_first = record->pose_orient = TICK2_POSE_NONE;
	record->pose_count = 0;
	record->pose_blocks = 0;
}

/* a node block's place */
void *tick2_pose_block(struct tick2_root const *record, int which)
{
	if (which == TICK2_POSE_MATRICES)
		return tick2_pose_at(&tick2_pose_store->matrices, record->pose_first, 0);
	return tick2_pose_at(&tick2_pose_store->orientations, record->pose_orient, which == TICK2_POSE_ORIGINALS);
}

static long tick2_pose_run_spare(struct tick2_pose_run const *run)
{
	long c, spare = (run->nodes - run->live) * run->arrays * run->stride;

	for (c = 0; c < TICK2_POSE_CHUNKS; c++)
		if (run->chunk[c])
			spare += (run->chunk_cap[c] - run->chunk_live[c]) * run->arrays * run->stride;
	return spare;
}

long tick2_pose_spare_bytes(void)
{
	return tick2_pose_store ? tick2_pose_run_spare(&tick2_pose_store->matrices) + tick2_pose_run_spare(&tick2_pose_store->orientations) : 0;
}
/* ---- moved from objects.c: end ---- */
