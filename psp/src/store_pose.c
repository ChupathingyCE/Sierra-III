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
- Means B's slot table (psp_fixups.py FIXES_BOXPOSING's means-B block, after step 4's renames; the cut
  tick2_posenew_boxb_move_cut): the slots and their count, the ME's copy and its two reads, the site note, the make
  (store_pose_make_nodes), the whole read, the settle-all, forget, the make ahead of the IK and the tick's end. The
  lines whose names objects.c still reads lose their static; objects.c keeps the slot struct, the ME copy's struct
  and the two readers' macros (the same text) with the names declared. Still keyed by object_index & 255.
To move (the same section's list): NMSKIP's tables and their ME copy, store_pose_box, store_pose_make_skipped / _at,
store_pose_settle / _in and the two accessors, keyed by record (the record found at the boundary from the header's
datum's root, Q1 (b)), the ME's names kept.
*/

#include <stdio.h>
#include "cseries.h"
#include "math/real_math.h"
#include "objects/objects.h"

/* the port's log (port/linux/src/sdl_platform.c), as objects.c declares it */
void platform_psp_log(const char *format, ...);

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

/* ---- moved from objects.c (psp_fixups.py FIXES_BOXPOSING means B's slot table, after step 4's renames): begin ---- */
/* psp_fixups.py (FIXES_BOXPOSING, means B; docs/box_posing.md §1b). A slot an
object, by its absolute index's low bits: a pose that owes takes its slot
when no other owing pose holds it, and the next pose of the object, or the
make of its last owed node, gives it back */
#define PSP_BOXB_SLOTS 256
struct psp_boxb_slot
{
	unsigned long long made; /* the nodes made for the object's pose, by the pose and since */
	unsigned long long all;  /* every node of its model */
	long object_index;       /* NONE: the slot owes nothing */
	void const *plan;
};
struct psp_boxb_slot psp_boxb[PSP_BOXB_SLOTS];
/* the slots owing, read in line by every reader: 0, no test */
int psp_boxb_count = 0;
int psp_boxb_on = -1, psp_boxb_check = 0, psp_boxb_changed = 1;
/* the ME's copy (the AI's pass, the rays), made at each hand-over and at the
objects' loop's end, on whole lines of its own: a node made after it isn't
the ME's to read, as the main CPU may be making it while the ME reads. A
copy's `made` has every bit past the model's nodes set */
struct psp_boxb_me_copy
{
	long count;
	long pad[15];
	long object_index[PSP_BOXB_SLOTS];
	unsigned long long made[PSP_BOXB_SLOTS];
};
struct psp_boxb_me_copy psp_boxb_me __attribute__((aligned(64)));
/* reads on the ME of an owed node with no actor in hand to give back: a
count only the ME writes, for the log (none is expected) */
unsigned long psp_boxb_me_stray = 0;
unsigned long psp_boxb_ticks, psp_boxb_poses, psp_boxb_owing_poses, psp_boxb_nodes_all, psp_boxb_nodes_made,
	psp_boxb_late, psp_boxb_late_nodes, psp_boxb_ik, psp_boxb_ik_nodes, psp_boxb_busy, psp_boxb_drawn_whole,
	psp_boxb_dead_or_held, psp_boxb_settled_all;
int psp_boxb_in_ik = 0;
#define PSP_BOXB_SITES 24
static void *psp_boxb_site[PSP_BOXB_SITES];
static unsigned long psp_boxb_site_n[PSP_BOXB_SITES], psp_boxb_site_nodes[PSP_BOXB_SITES], psp_boxb_sites_other;
unsigned long long psp_box_posing_make(const void *plan, const float *orientations, float *nodes,
	unsigned long long made, unsigned long long want);
unsigned long long psp_box_posing_below(const void *plan, int node);

void psp_boxb_for_me(void)
{
	int k;

	if (!psp_boxb_changed)
		return;
	psp_boxb_changed = 0;
	psp_boxb_me.count = psp_boxb_count;
	if (!psp_boxb_count)
		return;
	for (k = 0; k < PSP_BOXB_SLOTS; k++)
	{
		psp_boxb_me.object_index[k] = psp_boxb[k].object_index;
		psp_boxb_me.made[k] = psp_boxb[k].made | ~psp_boxb[k].all;
	}
}

/* on the ME: whether the copy says a node of `want` is owed */
int psp_boxb_me_lacks(long object_index, unsigned long long want)
{
	long slot = object_index & (PSP_BOXB_SLOTS - 1);

	return psp_boxb_me.count && psp_boxb_me.object_index[slot] == object_index && (want & ~psp_boxb_me.made[slot]) != 0;
}

/* on the ME: an owed node is the main CPU's to make, so the actor goes back
to think there, as an owed vislazy compute's (FIXES_NMSKIP) */
__attribute__((noinline)) void psp_boxb_me_read(long object_index, unsigned long long want)
{
	extern int psp_aime_arm;
	extern void psp_aime_handback(int which);

	if (!psp_boxb_me_lacks(object_index, want))
		return;
	if (psp_aime_arm)
		psp_aime_handback(26);
	psp_boxb_me_stray++;
}

static void psp_boxb_site_note(void *site, unsigned long nodes)
{
	int s;

	for (s = 0; s < PSP_BOXB_SITES && psp_boxb_site[s] && psp_boxb_site[s] != site; s++)
		;
	if (s == PSP_BOXB_SITES)
	{
		psp_boxb_sites_other++;
		return;
	}
	psp_boxb_site[s] = site;
	psp_boxb_site_n[s]++;
	psp_boxb_site_nodes[s] += nodes;
}

/* a reader wants nodes of an owed pose: they're made now (the main CPU) */
__attribute__((noinline)) void store_pose_make_nodes(long object_index, unsigned long long want, void *site)
{
	struct psp_boxb_slot *slot = &psp_boxb[object_index & (PSP_BOXB_SLOTS - 1)];
	struct object_datum *object;
	unsigned long long before = slot->made;

	{
		/* psp_fixups.py (FIXES_AIME2A, aimefpu): the main CPU's like-for-like
		pass stops where the ME's would */
		extern int psp_aime_arm, psp_aime_fpu_main;
		extern void psp_aime_handback(int which);

		if (psp_aime_arm && psp_aime_fpu_main && psp_boxb_me_lacks(object_index, want))
			psp_aime_handback(26);
	}
	object = object_get(object_index);
	slot->made = psp_box_posing_make(slot->plan,
		(float const *)object_header_block_get(object_index, &object->object.node_orientations),
		(float *)object_header_block_get(object_index, &object->object.node_matrices), before, want & slot->all);
	psp_boxb_changed = 1;
	if (psp_boxb_in_ik)
	{
		psp_boxb_ik++;
		psp_boxb_ik_nodes += __builtin_popcountll(slot->made & ~before);
	}
	else
	{
		psp_boxb_late++;
		psp_boxb_late_nodes += __builtin_popcountll(slot->made & ~before);
		psp_boxb_site_note(site, __builtin_popcountll(slot->made & ~before));
	}
	if ((slot->made & slot->all) == slot->all)
	{
		slot->object_index = NONE;
		psp_boxb_count--;
	}
}

/* every reader's test, in line: an owed node of `want` is made first. On the
ME, the copy decides */
#define PSP_BOXB_READ(index, want) do { \
		if (psp_boxb_count | psp_boxb_me.count) \
		{ \
			unsigned long long psp_boxb_w = (want); \
			if (psp_work_on_me()) \
				psp_boxb_me_read((index), psp_boxb_w); \
			else \
			{ \
				struct psp_boxb_slot const *psp_boxb_s = &psp_boxb[(unsigned long)(index) & (PSP_BOXB_SLOTS - 1)]; \
				if (psp_boxb_s->object_index == (long)(index) && (psp_boxb_w & ~psp_boxb_s->made)) \
					store_pose_make_nodes((index), psp_boxb_w, __builtin_return_address(0)); \
			} \
		} \
	} while (0)

/* whether the object owes nodes (on the ME, as the copy has it): a reader
whose wanted nodes take work to name asks this first */
#define PSP_BOXB_OWES(index) ((psp_boxb_count | psp_boxb_me.count) && (psp_work_on_me() ? \
		psp_boxb_me.count && psp_boxb_me.object_index[(unsigned long)(index) & (PSP_BOXB_SLOTS - 1)] == (long)(index) : \
		psp_boxb[(unsigned long)(index) & (PSP_BOXB_SLOTS - 1)].object_index == (long)(index)))

/* every node an owed object has, for a reader outside this unit (the limp
body) */
void psp_boxb_whole(long object_index)
{
	PSP_BOXB_READ(object_index, ~0ull);
}

/* every owed node of every object (a game state saved: it holds the node
matrices; "boxposebcheck": before each tick's state hash) */
void psp_boxb_settle_all(void)
{
	int k;

	for (k = 0; k < PSP_BOXB_SLOTS && psp_boxb_count; k++)
	{
		struct psp_boxb_slot *slot = &psp_boxb[k];

		if (slot->object_index == NONE)
			continue;
		if (object_header_try_and_get(slot->object_index))
			store_pose_make_nodes(slot->object_index, ~0ull, (void *)psp_boxb_settle_all);
		else
		{
			slot->object_index = NONE;
			psp_boxb_count--;
			psp_boxb_changed = 1;
		}
	}
	psp_boxb_settled_all++;
}

void psp_boxb_hash_settle(void)
{
	if (psp_boxb_check)
		psp_boxb_settle_all();
}

/* a map's objects go, or a game state is loaded: nothing of theirs is owed */
void psp_boxb_forget(void)
{
	int k;

	for (k = 0; k < PSP_BOXB_SLOTS; k++)
		psp_boxb[k].object_index = NONE;
	psp_boxb_count = 0;
	psp_boxb_changed = 1;
	psp_boxb_for_me();
}

/* the IK moves the arm's nodes after the walk (unit_postprocess_node_matrices):
the nodes under them are made first, from the walk's matrices, so a later
make of them reads what the whole walk's postprocess read */
void psp_boxb_before_ik(long object_index, short node)
{
	struct psp_boxb_slot const *slot = &psp_boxb[(unsigned long)object_index & (PSP_BOXB_SLOTS - 1)];

	if (psp_boxb_count && slot->object_index == object_index && node >= 0 && node < 64)
		PSP_BOXB_READ(object_index, psp_box_posing_below(slot->plan, node));
}

/* each tick's end (the objects' loop's), and the log every 900 ticks */
void psp_boxb_tick_end(void)
{
	psp_boxb_for_me();
	if (psp_boxb_on <= 0 || ++psp_boxb_ticks < 900)
		return;
	{
		char sites[PSP_BOXB_SITES * 28];
		int at = 0, s;

		sites[0] = 0;
		for (s = 0; s < PSP_BOXB_SITES && psp_boxb_site[s]; s++)
			at += snprintf(sites + at, sizeof(sites) - at, " %08lx:%lu/%lu", (unsigned long)psp_boxb_site[s],
				psp_boxb_site_n[s], psp_boxb_site_nodes[s]);
		platform_psp_log("boxposeb: %lu ticks: %lu biped poses, %lu owing; nodes %lu of %lu made by the poses; "
			"late makes %lu (%lu nodes), IK makes %lu (%lu nodes); posed whole: drawn on a frame's last tick %lu, "
			"dead or attached %lu, slot taken %lu; owing now %d; settled whole %lu; ME reads with no actor to give back %lu",
			psp_boxb_ticks, psp_boxb_poses, psp_boxb_owing_poses, psp_boxb_nodes_made, psp_boxb_nodes_all,
			psp_boxb_late, psp_boxb_late_nodes, psp_boxb_ik, psp_boxb_ik_nodes, psp_boxb_drawn_whole,
			psp_boxb_dead_or_held, psp_boxb_busy, psp_boxb_count, psp_boxb_settled_all, psp_boxb_me_stray);
		platform_psp_log("boxposeb: late makes by reader (site:makes/nodes):%s; other readers %lu", sites, psp_boxb_sites_other);
	}
	psp_boxb_ticks = psp_boxb_poses = psp_boxb_owing_poses = psp_boxb_nodes_all = psp_boxb_nodes_made = 0;
	psp_boxb_late = psp_boxb_late_nodes = psp_boxb_ik = psp_boxb_ik_nodes = psp_boxb_busy = 0;
	psp_boxb_drawn_whole = psp_boxb_dead_or_held = psp_boxb_settled_all = psp_boxb_sites_other = 0;
	csmemset(psp_boxb_site, 0, sizeof(psp_boxb_site));
	csmemset(psp_boxb_site_n, 0, sizeof(psp_boxb_site_n));
	csmemset(psp_boxb_site_nodes, 0, sizeof(psp_boxb_site_nodes));
}
/* ---- moved from objects.c: means B's slot table: finish ---- */
