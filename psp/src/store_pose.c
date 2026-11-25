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
- NMSKIP's tables (FIXES_NMSKIP's block, read from objects.c as every cut before left it, since six later fixups edit
  inside it; the cut tick2_posenew_nms_move_cut): the owed computes' entries, free list and bits, the root table, the
  ME's copy (psp_nms_me, made by psp_nms_pending_for_me), vislazy's drawn sets and site table, the skip, the drop,
  the note, the make (store_pose_make_skipped / _at), the close, forget, and the game state's save and load. The lines
  whose names objects.c still reads lose their static; objects.c keeps the PSP_NMS_TYPES line, FIXES_TP1's psp_nms_may
  (always-inline in the objects' loop) and the names declared. Still keyed by absolute index.
- The last piece (section 17 'last piece'; the cut tick2_posenew_pose_move_cut): store_pose_settle / _in verbatim
  (with PSP_NMS_READ's definition, which objects.c keeps too, the same text), the two accessors keyed by record
  (tick2_record_of_index, the header's datum's root, Q1 (b)), and store_pose_box's store half: means B's release,
  the box on the record's words and means B's fill. objects.c keeps the engine half, store_pose_box_gather (the
  plan, the flags, the want), and the census (psp_nmr_on, psp_nmr_read), which lose their static. The ME's names are
  kept; B's slot is still keyed by object_index & 255.
- Item 2's third piece (section 17; psp_fixups.py tick2_posenew_copy_reads): the copies' block and count reads are
  the record's, as item 2 made every read left in the tree: B's make, the skip check's root and NMSKIP's catch-up
  snapshot read store_pose_matrices / _orientations(object->object.root) and store_node_count, not the block getter
  and node_matrices.size. These six lines are the only ones here not verbatim from objects.c; the cuts make the same
  edits to the text they compare, each found once.
*/

#include <stdio.h>
#include "cseries.h"
#include "math/real_math.h"
#include "objects/objects.h"
/* NMSKIP's text reads game_time_get, the animation graph's tags and MAXIMUM_NODES_PER_MODEL, as objects.c does */
#include "game/game.h"
#include "models/model_animation_definitions.h"
#include "models/model_definitions.h"
/* the last piece's accessors read the header's datum by datum_get's found-it path inline (psp_datum_get_inline),
as objects.c does, and log by error() as objects.c's did */
#include "memory/data.h"
#include "cseries/errors.h"

/* the port's log (port/linux/src/sdl_platform.c), as objects.c declares it */
void platform_psp_log(const char *format, ...);
/* the port's clock, as objects.c declares it at its head (NMSKIP's check times the computes it would leave out) */
unsigned long psp_microseconds(void);
/* objects.c (FIXES_LD1, defined there): this tick is the last before a frame; NMSKIP's close reads it */
extern int psp_lights_link_tick;

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
extern int psp_boxb_count; /* defined in objects.c (the lead 2026-11-25 09:0x) */
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
		(float const *)store_pose_orientations(object->object.root),
		(float *)store_pose_matrices(object->object.root), before, want & slot->all);
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

/* ---- moved from objects.c (FIXES_NMSKIP's block as every cut before this one left it, read from the tree): begin ---- */
/* psp_fixups.py (FIXES_NMSKIP): the computes left out until the frame's
last tick. A bit an absolute index marks an object whose compute is owed;
its entry keeps what that compute would have read. Each object's last
compute's root inputs are kept in a small direct-mapped table, so a skip
can see the root (and the sphere) wouldn't move */
#define PSP_NMS_ENTRIES 256
#define PSP_NMS_ROOTS 256
struct psp_nms_inputs
{
	real_point3d position;
	real_vector3d forward, up;
	real scale;
	unsigned long flags;
	struct object_animation_state animation;
	long parent_object_index;
	short parent_node_index;
	real functions[NUMBER_OF_OUTGOING_OBJECT_FUNCTIONS];
};
struct psp_nms_entry
{
	long object_index;
	long time;
	struct psp_nms_inputs inputs;
};
struct psp_nms_root
{
	long object_index;
	real_point3d position;
	real_vector3d forward, up;
	real scale;
	unsigned long mirrored;
	long graph;
	short state;
};
long psp_nms_settle_time = NONE;
/* read in line by the accessors: no left-out computes, no test */
extern int psp_nms_count; /* defined in objects.c (the lead 2026-11-25 09:0x) */
unsigned long psp_nms_pending[(MAXIMUM_OBJECTS_PER_MAP + 31) / 32];
/* the Media Engine's rays' copy of the two (collisions.c, vislazy), made at
each hand-over (psp_nms_pending_for_me): the count in [0], the bits from
[16], on whole lines of their own. The main CPU writes it only then, before
the hand-over writes its cache back */
unsigned long psp_nms_me[16 + ((((MAXIMUM_OBJECTS_PER_MAP + 31) / 32) + 15) & ~15)] __attribute__((aligned(64)));

#ifdef PSP_BOX_POSING
void psp_boxb_for_me(void);
#endif
void psp_nms_pending_for_me(void)
{
	psp_nms_me[0] = (unsigned long)psp_nms_count;
	if (psp_nms_count)
		csmemcpy(&psp_nms_me[16], psp_nms_pending, sizeof(psp_nms_pending));
#ifdef PSP_BOX_POSING
	psp_boxb_for_me();
#endif
}

int psp_nms_on = -1, psp_nms_check = 0, psp_nms_open = 0;
/* vislazy: the skip on every tick for what the last frame didn't draw */
int psp_vl_on = 0, psp_vl_ticked = 0;
/* the object whose compute the skip just left out while its animation plays
(the sleep check, psp_sleepy_update) */
long psp_vl_moving = NONE;
unsigned long psp_vl_drawn[(MAXIMUM_OBJECTS_PER_MAP + 31) / 32];
/* the renderer's list goes here, and becomes psp_vl_drawn at the next tick's
start: the AI's pass on the ME reads psp_vl_drawn while the frame is
recorded, and when the record wrote psp_vl_drawn itself which actors owed a
node block depended on which got there first, so two PSP runs handed
different actors back to the tick and played differently (sierra-hw
42475844519, 42477229781). "vislazyraceold": the list written in place */
static unsigned long psp_vl_drawn_next[(MAXIMUM_OBJECTS_PER_MAP + 31) / 32];
static int psp_vl_next_fresh = 0, psp_vl_in_place = -1;
int psp_keep(const char *name);
static unsigned long psp_vl_peak, psp_vl_frames, psp_vl_sites_other;
#define PSP_VL_SITES 16
static void *psp_vl_site[PSP_VL_SITES];
static unsigned long psp_vl_site_n[PSP_VL_SITES];
static void *psp_nms_reader;

/* the renderer's list for a view (render_objects.c): the first list after a
tick starts the frame's set afresh, and a frame's views add to it */
void psp_vl_listed(long const *indices, short count)
{
	short k;

	if (psp_vl_on <= 0)
		return;
	unsigned long *set;

	if (psp_vl_in_place < 0)
		psp_vl_in_place = psp_keep("vislazyraceold") != 0;
	set = psp_vl_in_place ? psp_vl_drawn : psp_vl_drawn_next;
	if (psp_vl_ticked)
	{
		csmemset(set, 0, sizeof(psp_vl_drawn));
		psp_vl_ticked = 0;
		psp_vl_frames++;
	}
	psp_vl_next_fresh = 1;
	for (k = 0; k < count; k++)
	{
		unsigned long absolute = (unsigned long)indices[k] & 0xFFFFUL;

		if (absolute < MAXIMUM_OBJECTS_PER_MAP)
			set[absolute >> 5] |= 1ul << (absolute & 31);
	}
}

/* which code read an object whose compute was owed */
static void psp_vl_site_note(void)
{
	int s;

	for (s = 0; s < PSP_VL_SITES && psp_vl_site[s] && psp_vl_site[s] != psp_nms_reader; s++)
		;
	if (s == PSP_VL_SITES)
	{
		psp_vl_sites_other++;
		return;
	}
	psp_vl_site[s] = psp_nms_reader;
	psp_vl_site_n[s]++;
}
static short *psp_nms_entry_of = NULL;
static struct psp_nms_entry *psp_nms_entries = NULL;
static short psp_nms_free[PSP_NMS_ENTRIES];
static int psp_nms_free_count = 0;
static struct psp_nms_root *psp_nms_roots = NULL;
static real_matrix4x3 *psp_nms_kept = NULL;
unsigned long psp_nms_ticks, psp_nms_last_ticks, psp_nms_seen, psp_nms_skipped, psp_nms_superseded,
	psp_nms_read, psp_nms_frame_end, psp_nms_stale, psp_nms_refused[7], psp_nms_gate_wrong,
	psp_nms_made_differs, psp_nms_made_checked, psp_nms_skip_us, psp_nms_check_reports;
static void store_pose_make_skipped_at(long absolute, int at_frame_end);

static void psp_nms_inputs_get(struct object_datum const *object, struct psp_nms_inputs *inputs)
{
	csmemset(inputs, 0, sizeof(*inputs));
	inputs->position = object->object.root->position;
	inputs->forward = object->object.root->forward;
	inputs->up = object->object.root->up;
	inputs->scale = object->object.scale;
	inputs->flags = object->object.flags;
	inputs->animation = (*tick2_anim_of(object->object.root));
	inputs->parent_object_index = object->object.parent_object_index;
	inputs->parent_node_index = object->object.parent_node_index;
	csmemcpy(inputs->functions, object->object.outgoing_function_values, sizeof(inputs->functions));
}

static void psp_nms_inputs_put(struct object_datum *object, struct psp_nms_inputs const *inputs)
{
	object->object.root->position = inputs->position;
	object->object.root->forward = inputs->forward;
	object->object.root->up = inputs->up;
	object->object.scale = inputs->scale;
	object->object.flags = inputs->flags;
	(*tick2_anim_of(object->object.root)) = inputs->animation;
	object->object.parent_object_index = inputs->parent_object_index;
	object->object.parent_node_index = inputs->parent_node_index;
	csmemcpy(object->object.outgoing_function_values, inputs->functions, sizeof(inputs->functions));
}

static int psp_nms_waits(long absolute)
{
	return (psp_nms_pending[absolute >> 5] >> (absolute & 31)) & 1;
}

static void psp_nms_release(long absolute)
{
	psp_nms_pending[absolute >> 5] &= ~(1ul << (absolute & 31));
	psp_nms_free[psp_nms_free_count++] = psp_nms_entry_of[absolute];
	psp_nms_count--;
}

/* the root inputs each compute read (object_compute_node_matrices' end) */
__attribute__((noinline)) void psp_nms_note(long object_index, struct object_datum const *object)
{
	struct psp_nms_root *root = &psp_nms_roots[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index) & (PSP_NMS_ROOTS - 1)];

	root->object_index = object_index;
	root->position = object->object.root->position;
	root->forward = object->object.root->forward;
	root->up = object->object.root->up;
	root->scale = object->object.scale;
	root->mirrored = object->object.flags & FLAG(_object_mirrored_bit);
	root->graph = (*tick2_anim_of(object->object.root)).animation_graph_index;
	root->state = (*tick2_anim_of(object->object.root)).state.index;
}

static int psp_nms_root_same(long object_index, struct object_datum const *object)
{
	struct psp_nms_root const *root = &psp_nms_roots[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index) & (PSP_NMS_ROOTS - 1)];

	return root->object_index == object_index &&
		!csmemcmp(&root->position, &object->object.root->position, sizeof(root->position)) &&
		!csmemcmp(&root->forward, &object->object.root->forward, sizeof(root->forward)) &&
		!csmemcmp(&root->up, &object->object.root->up, sizeof(root->up)) &&
		!csmemcmp(&root->scale, &object->object.scale, sizeof(root->scale)) &&
		root->mirrored == (object->object.flags & FLAG(_object_mirrored_bit)) &&
		root->graph == (*tick2_anim_of(object->object.root)).animation_graph_index &&
		root->state == (*tick2_anim_of(object->object.root)).state.index;
}

/* 1 when the animation leaves the root node where its default puts it: no
channel of node 0 in the animation playing, and no overlays (an overlay is
weighted by a function value, which moves even a constant channel) */
static int psp_nms_root_still(struct object_datum const *object, struct object_definition const *definition)
{
	if (definition->object.animation_graph.index != NONE &&
		animation_graph_definition_get(definition->object.animation_graph.index)->object_overlays.count > 0)
		return 0;
	if ((*tick2_anim_of(object->object.root)).animation_graph_index != NONE && (*tick2_anim_of(object->object.root)).state.index != NONE)
	{
		struct animation_graph *graph = animation_graph_definition_get((*tick2_anim_of(object->object.root)).animation_graph_index);
		struct animation const *animation;

		if (!VALID_INDEX((*tick2_anim_of(object->object.root)).state.index, graph->animations.count))
			return 0;
		animation = TAG_BLOCK_GET_ELEMENT(&graph->animations, (*tick2_anim_of(object->object.root)).state.index, struct animation);
		if ((animation->nodes_with_translation_flags[0] | animation->nodes_with_rotation_flags[0] |
			animation->nodes_with_scale_flags[0]) & 1)
			return 0;
	}
	return 1;
}

__attribute__((noinline)) int psp_nms_begin(void)
{
	extern int psp_lights_link_tick; /* FIXES_LD1: the frame's last tick */

	if (psp_nms_on < 0)
	{
		int k;

		psp_nms_check = psp_keep("nmskipcheck") != 0;
		psp_nms_on = psp_nms_check || psp_keep("nmskip");
		if (!psp_nms_on && !psp_keep("vislazyoff"))
		{
			psp_vl_on = 1;
			psp_nms_check = psp_keep("vislazycheck") != 0;
			psp_nms_on = 1;
		}
		if (psp_nms_on)
		{
			psp_nms_entry_of = (short *)malloc(MAXIMUM_OBJECTS_PER_MAP * sizeof(*psp_nms_entry_of));
			psp_nms_entries = (struct psp_nms_entry *)malloc(PSP_NMS_ENTRIES * sizeof(*psp_nms_entries));
			psp_nms_roots = (struct psp_nms_root *)calloc(PSP_NMS_ROOTS, sizeof(*psp_nms_roots));
			if (psp_nms_check)
				psp_nms_kept = (real_matrix4x3 *)malloc(MAXIMUM_NODES_PER_MODEL * sizeof(*psp_nms_kept));
			if (!psp_nms_entry_of || !psp_nms_entries || !psp_nms_roots || (psp_nms_check && !psp_nms_kept))
				psp_nms_on = 0;
			for (k = 0; k < PSP_NMS_ROOTS && psp_nms_roots; k++)
				psp_nms_roots[k].object_index = NONE;
			for (k = 0; k < PSP_NMS_ENTRIES; k++)
				psp_nms_free[k] = (short)k;
			psp_nms_free_count = PSP_NMS_ENTRIES;
		}
	}
	if (psp_nms_on <= 0)
	{
		psp_vl_on = 0;
		return 0;
	}
	psp_nms_ticks++;
	if (psp_vl_next_fresh && psp_vl_in_place == 0)
		csmemcpy(psp_vl_drawn, psp_vl_drawn_next, sizeof(psp_vl_drawn));
	psp_vl_next_fresh = 0;
	psp_vl_ticked = 1;
	psp_vl_moving = NONE;
	if (psp_lights_link_tick)
		psp_nms_last_ticks++;
	return psp_vl_on || !psp_lights_link_tick;
}

/* 1 when this compute (and the postprocess after it, which these types
don't have) is left to the frame's last tick */

__attribute__((noinline)) int psp_nms_skip(long object_index, struct object_datum *object)
{
	struct object_animation_state *tick2_h_anim = NULL; /* TICK2 hoist: the section's record, worked out at its first use */
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	/* fetched for the reasons that read it (FIXES_TP1) */
	struct object_definition const *definition = NULL;
	struct psp_nms_entry *entry;
	extern unsigned long psp_object_epoch;
	extern int psp_merays_out;
	void psp_merays_settle(void);
	int reason;

	psp_nms_seen++;
	if (object->object.parent_object_index != NONE || object->object.first_child_object_index != NONE)
		reason = 0;
	else if (psp_vl_on && ((psp_vl_drawn[absolute >> 5] >> (absolute & 31)) & 1))
		reason = 6;
	else if ((definition = object_definition_get(object->definition_index)),
		!psp_vl_on && definition->object.collision_model.index != NONE)
		reason = 1;
	else if ((*(tick2_h_anim ? tick2_h_anim : (tick2_h_anim = tick2_anim_of(object->object.root)))).interpolation_frame_count)
		reason = 2;
	else if (!psp_nms_root_still(object, definition))
		reason = 3;
	else if (!psp_nms_root_same(object_index, object))
		reason = 4;
	else if (!psp_nms_free_count && !psp_nms_waits(absolute))
		reason = 5;
	else
		reason = -1;
	if (reason >= 0)
	{
		psp_nms_refused[reason]++;
		return 0;
	}
	if (psp_nms_check)
	{
		/* made as before; the skip's claims checked against it */
		real_matrix4x3 root = ((real_matrix4x3 *)store_pose_matrices(object->object.root))[0];
		real_point3d center = object->object.root->bounding_sphere_center;
		real radius = object->object.root->bounding_sphere_radius;
		unsigned long start = psp_microseconds();

		store_pose_now(object_index);
		psp_nms_skip_us += psp_microseconds() - start;
		if (csmemcmp(&root, store_pose_matrices(object->object.root), sizeof(root)) ||
			csmemcmp(&center, &object->object.root->bounding_sphere_center, sizeof(center)) ||
			radius != object->object.root->bounding_sphere_radius)
		{
			psp_nms_gate_wrong++;
			if (psp_nms_check_reports++ < 20)
				platform_psp_log("nmskipcheck: t=%ld %s (#%08lx): the root or sphere moved though the skip's test passed",
					game_time_get(), tag_get_name(object->definition_index), (unsigned long)object_index);
		}
	}
	else
	{
		/* what the compute counts on entry (FIXES_121, FIXES_122) */
		if (psp_merays_out)
			psp_merays_settle();
		psp_object_epoch++;
	}
	if (psp_nms_waits(absolute))
	{
		/* an earlier tick's, which this one's replaces */
		psp_nms_release(absolute);
		psp_nms_superseded++;
	}
	psp_nms_entry_of[absolute] = psp_nms_free[--psp_nms_free_count];
	entry = &psp_nms_entries[psp_nms_entry_of[absolute]];
	entry->object_index = object_index;
	entry->time = game_time_get();
	psp_nms_inputs_get(object, &entry->inputs);
	psp_nms_pending[absolute >> 5] |= 1ul << (absolute & 31);
	psp_nms_count++;
	psp_nms_skipped++;
	/* an animation playing moves the nodes the compute would have made, so
	the eager compute changes the object's datum (its node matrices) though
	the skip leaves it as it was: the sleep check (psp_sleepy_update) must
	see it changed, or the object falls asleep, stops updating and keeps
	its pose where the eager build would have kept it awake and moving */
	if (psp_vl_on && (*(tick2_h_anim ? tick2_h_anim : (tick2_h_anim = tick2_anim_of(object->object.root)))).animation_graph_index != NONE &&
		(*(tick2_h_anim ? tick2_h_anim : (tick2_h_anim = tick2_anim_of(object->object.root)))).state.index != NONE)
		psp_vl_moving = object_index;
	if ((unsigned long)psp_nms_count > psp_vl_peak)
		psp_vl_peak = psp_nms_count;
	return 1;
}

/* a compute anywhere replaces the one left out (object_compute_node_matrices) */
__attribute__((noinline)) void psp_nms_drop(long object_index)
{
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);

	if (absolute < MAXIMUM_OBJECTS_PER_MAP && psp_nms_waits(absolute) &&
		psp_nms_entries[psp_nms_entry_of[absolute]].object_index == object_index)
	{
		psp_nms_release(absolute);
		psp_nms_superseded++;
	}
}

/* the left-out compute, made now from its entry's inputs and game time; the
object's inputs as they are now are put back after, but for any the compute
itself changed (a facing it mended), as nwbatch's list did */
static void psp_nms_make(long object_index, struct psp_nms_entry const *entry)
{
	struct object_datum *object = object_get(object_index);
	struct psp_nms_inputs now, after;
	extern unsigned long psp_object_epoch;
	unsigned long epoch = psp_object_epoch;
	unsigned char *n = (unsigned char *)&now;
	unsigned char const *p = (unsigned char const *)&entry->inputs, *a = (unsigned char const *)&after;
	unsigned int k;

	psp_nms_inputs_get(object, &now + PSP_MEPROBE_EXPR(9));
	psp_nms_inputs_put(object, &entry->inputs);
	psp_nms_settle_time = entry->time;
	store_pose_now(object_index);
	psp_nms_settle_time = NONE;
	/* the skip counted it already */
	psp_object_epoch = epoch;
	psp_nms_inputs_get(object, &after);
	for (k = 0; k < sizeof(now); k++)
		if (a[k] != p[k])
			n[k] = a[k];
	psp_nms_inputs_put(object, &now);
}

__attribute__((noinline)) static void store_pose_make_skipped_at(long absolute, int at_frame_end)
{
	struct psp_nms_entry entry = psp_nms_entries[psp_nms_entry_of[absolute]];
	long now = game_time_get();
	struct object_datum *object;
	real_matrix4x3 *nodes;
	long size;
	real_point3d center;
	real radius;

	psp_nms_release(absolute);
	/* its object gone, or a game state from another time (a revert) */
	if (!object_header_try_and_get(entry.object_index) || entry.time > now || (!psp_vl_on && now - entry.time > 8))
	{
		psp_nms_stale++;
		return;
	}
	if (at_frame_end)
		psp_nms_frame_end++;
	else
	{
		psp_nms_read++;
		if (psp_vl_on)
			psp_vl_site_note();
	}
	if (!psp_nms_check)
	{
		psp_nms_make(entry.object_index, &entry);
		return;
	}
	/* the compute already made where the skip would have been is what the
	reader sees; the catch-up is made, compared, and taken back */
	object = object_get(entry.object_index);
	nodes = (real_matrix4x3 *)store_pose_matrices(object->object.root);
	size = MIN((long)store_node_count(object->object.root) * (long)sizeof(real_matrix4x3), (long)(MAXIMUM_NODES_PER_MODEL * sizeof(real_matrix4x3)));
	csmemcpy(psp_nms_kept, nodes, size);
	center = object->object.root->bounding_sphere_center;
	radius = object->object.root->bounding_sphere_radius;
	psp_nms_make(entry.object_index, &entry);
	psp_nms_made_checked++;
	if (csmemcmp(psp_nms_kept, nodes, size) || csmemcmp(&center, &object->object.root->bounding_sphere_center, sizeof(center)) ||
		radius != object->object.root->bounding_sphere_radius)
	{
		psp_nms_made_differs++;
		if (psp_nms_check_reports++ < 20)
			platform_psp_log("%s: t=%ld %s (#%08lx): the catch-up from tick %ld differs from the compute it stands in for (reader %08lx)",
				psp_vl_on ? "vislazycheck" : "nmskipcheck",
				now, tag_get_name(object->definition_index), (unsigned long)entry.object_index, entry.time,
				(unsigned long)psp_nms_reader);
	}
	csmemcpy(nodes, psp_nms_kept, size);
	object->object.root->bounding_sphere_center = center;
	object->object.root->bounding_sphere_radius = radius;
	if (psp_object_spheres && DATUM_INDEX_TO_ABSOLUTE_INDEX(entry.object_index) < MAXIMUM_OBJECTS_PER_MAP)
	{
		psp_object_spheres[DATUM_INDEX_TO_ABSOLUTE_INDEX(entry.object_index)].center = center;
		psp_object_spheres[DATUM_INDEX_TO_ABSOLUTE_INDEX(entry.object_index)].radius = radius;
	}
}

/* a reader of an object whose compute was left out: made first */
__attribute__((noinline)) void store_pose_make_skipped(long object_index, void *site)
{
	{
		/* psp_fixups.py (FIXES_AIME2A, "aime2a") */
		extern int psp_aime_arm, psp_aime_fpu_main;
		extern void psp_aime_handback(int which);

		/* (aimefpu: 1b's pass stops alike) */
		if (psp_aime_arm && (psp_work_on_me() || psp_aime_fpu_main))
			psp_aime_handback(26);
	}
	psp_nms_reader = (void *)((char *)site + PSP_MEPROBE_EXPR(6));
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);

	if (absolute < MAXIMUM_OBJECTS_PER_MAP && psp_nms_waits(absolute))
		store_pose_make_skipped_at(absolute, 0);
}

/* the objects' loop is over: on the frame's last tick, what is left is made */
__attribute__((noinline)) void psp_nms_close(void)
{
	long word;

#ifdef PSP_BOX_POSING
	psp_boxb_tick_end();
#endif
	psp_nms_open = 0;
	if (psp_nms_on <= 0)
		return;
	if (psp_nms_count && psp_lights_link_tick && !psp_vl_on)
	{
		for (word = 0; word < (MAXIMUM_OBJECTS_PER_MAP + 31) / 32; word++)
			while (psp_nms_pending[word])
				store_pose_make_skipped_at(word * 32 + __builtin_ctz(psp_nms_pending[word]), 1);
	}
	if (psp_nms_ticks >= 900)
	{
		platform_psp_log("%s: %lu ticks (%lu a frame's last): %lu computes of scenery and its kin on the others, %lu left out "
			"(%lu replaced by a later compute, %lu made for a reader, %lu at the frame's end, %lu stale); kept: attached %lu, "
			"collision model %lu, interpolating %lu, root animated %lu, root moved or unknown %lu, no room %lu, drawn last frame %lu",
			psp_vl_on ? "vislazy" : "nmskip",
			psp_nms_ticks, psp_nms_last_ticks, psp_nms_seen, psp_nms_skipped, psp_nms_superseded, psp_nms_read,
			psp_nms_frame_end, psp_nms_stale, psp_nms_refused[0], psp_nms_refused[1], psp_nms_refused[2],
			psp_nms_refused[3], psp_nms_refused[4], psp_nms_refused[5], psp_nms_refused[6]);
		if (psp_vl_on)
		{
			char sites[PSP_VL_SITES * 20];
			int at = 0, s;

			sites[0] = 0;
			for (s = 0; s < PSP_VL_SITES && psp_vl_site[s]; s++)
				at += snprintf(sites + at, sizeof(sites) - at, " %08lx:%lu", (unsigned long)psp_vl_site[s], psp_vl_site_n[s]);
			platform_psp_log("vislazy: %lu frames listed; computes owed now %d, peak %lu; catch-ups by reader (site:n):%s; other readers %lu",
				psp_vl_frames, psp_nms_count, psp_vl_peak, sites, psp_vl_sites_other);
			csmemset(psp_vl_site, 0, sizeof(psp_vl_site));
			csmemset(psp_vl_site_n, 0, sizeof(psp_vl_site_n));
			psp_vl_frames = psp_vl_peak = psp_vl_sites_other = 0;
		}
		if (psp_nms_check)
			platform_psp_log("%s: the skips' test wrong %lu of %lu; catch-ups differing %lu of %lu; "
				"the computes it would leave out %.3f ms a tick",
				psp_vl_on ? "vislazycheck" : "nmskipcheck",
				psp_nms_gate_wrong, psp_nms_skipped, psp_nms_made_differs, psp_nms_made_checked,
				(float)psp_nms_skip_us / (float)psp_nms_ticks / 1000.0f);
		psp_nms_ticks = psp_nms_last_ticks = psp_nms_seen = psp_nms_skipped = psp_nms_superseded = 0;
		psp_nms_read = psp_nms_frame_end = psp_nms_stale = psp_nms_skip_us = 0;
		csmemset(psp_nms_refused, 0, sizeof(psp_nms_refused));
	}
}

/* a map's objects go: nothing of theirs is owed */
void psp_nms_forget(void)
{
	long k;

	if (psp_nms_on <= 0)
		return;
	csmemset(psp_nms_pending, 0, sizeof(psp_nms_pending));
	psp_nms_count = 0;
	for (k = 0; k < PSP_NMS_ENTRIES; k++)
		psp_nms_free[k] = (short)k;
	psp_nms_free_count = PSP_NMS_ENTRIES;
	for (k = 0; k < PSP_NMS_ROOTS; k++)
		psp_nms_roots[k].object_index = NONE;
}

/* a game state is saved (a checkpoint): it holds the node matrices, so every
owed compute is made first, and a revert loads what it would have */
void psp_nms_game_state_saving(void)
{
	long word;

#ifdef PSP_BOX_POSING
	psp_boxb_settle_all();
#endif
	if (psp_nms_on <= 0 || !psp_nms_count)
		return;
	psp_nms_reader = (void *)psp_nms_game_state_saving;
	for (word = 0; word < (MAXIMUM_OBJECTS_PER_MAP + 31) / 32; word++)
		while (psp_nms_pending[word])
			store_pose_make_skipped_at(word * 32 + __builtin_ctz(psp_nms_pending[word]), 0);
}

/* a game state is loaded (a revert): its objects' matrices are as it was
saved, so nothing is owed, and no root input noted before stands */
void psp_nms_game_state_loading(void)
{
	psp_nms_forget();
#ifdef PSP_BOX_POSING
	psp_boxb_forget();
#endif
}
/* ---- moved from objects.c: NMSKIP's tables: finish ---- */

/* ---- moved from objects.c (the settle verbatim, the two accessors keyed by record, store_pose_box's store half): begin ---- */
/* objects.c (FIXES_134 #nmreaders, defined there): the readers' census, the accessors' first effect */
extern int psp_nmr_on;
void psp_nmr_read(long object_index, void *site);
/* the port's interpolated pose while a frame is drawn (port/linux/include/halo_linux_source_fixups.h) */
struct real_matrix4x3 *render_interpolation_object_node_matrices(long object_index);

/* objects.c's head (FIXES_NMSKIP), the same text: the settle's whole owed compute */
#define PSP_NMS_READ(index) do { \
		unsigned long psp_nms_absolute = (unsigned long)(index) & 0xFFFFUL; \
		if (psp_nms_absolute < MAXIMUM_OBJECTS_PER_MAP && (psp_work_on_me() ? \
			(psp_nms_me[0] && ((psp_nms_me[16 + (psp_nms_absolute >> 5)] >> (psp_nms_absolute & 31)) & 1)) : \
			(psp_nms_count && ((psp_nms_pending[psp_nms_absolute >> 5] >> (psp_nms_absolute & 31)) & 1)))) \
			store_pose_make_skipped(index, __builtin_return_address(0)); \
	} while (0)

/* TICK2 posenew (slice 1's task 3; docs/box_world_store.md section 17): the store's posing entry points.
A pose is owed two ways, each kept in a side table of the store's (not a word in every record: section 17's note):
- a compute FIXES_NMSKIP left out, with its inputs as the skip found them, so the make reads what the eager compute
  would have read even if they changed since;
- means B's nodes a living biped's pose didn't make, made from the orientations that pose was given.
store_pose_settle is the one place an outside read makes them; the store's own reads (an object leaving
the walk, B ahead of the IK, the settle-all, the marker getter's partial read) call the same makes,
store_pose_make_skipped and store_pose_make_nodes. On the ME the copy taken at the last hand-over decides, and an
owed read hands the actor back to the main CPU (psp_nms_me, psp_boxb_me): the ME never poses */
static inline __attribute__((always_inline)) void store_pose_settle_in(long object_index, unsigned long long want)
{
	PSP_NMS_READ(object_index); /* FIXES_NMSKIP's owed compute, whole */
#ifdef PSP_BOX_POSING
	PSP_BOXB_READ(object_index, want); /* means B's owed nodes, those wanted and their parents */
#endif
}

void store_pose_settle(long object_index, unsigned long long want)
{
	store_pose_settle_in(object_index, want);
}

/* the object's datum and its record by its index (section 17 Q1 (b)): the header's datum and its root, the loads
the block getter makes (objects.c object_header_block_get under HDRBLK, object_get's psp_object_get_all), through
datum_get's found-it path inline as objects.c reads it */
static __inline__ __attribute__((always_inline)) struct object_datum *tick2_datum_of_index(long object_index)
{
	return (struct object_datum *)((struct object_header_datum *)psp_datum_get_inline(object_header_data, object_index))->datum;
}

static __inline__ __attribute__((always_inline)) struct tick2_root *tick2_record_of_index(long object_index)
{
	return tick2_datum_of_index(object_index)->object.root;
}

#ifdef PSP_TICK2_PROOF
/* the proof form (psp_build.py defines PSP_TICK2_PROOF for this unit): while the datum keeps its node_matrices
reference (item 2 takes it out), the record's count is that reference's size in matrices, since
object_header_block_allocate sets both from one count; the first object where they differ is logged */
static void tick2_pose_count_check(long object_index, struct tick2_root const *record)
{
	static boolean logged = FALSE;
	short size = tick2_datum_of_index(object_index)->object.node_matrices.size;

	if (!logged && record->pose_count != size / (short)sizeof(real_matrix4x3))
	{
		logged = TRUE;
		platform_psp_log("tick2 posenew: object %08lx's record counts %d nodes, its node_matrices reference %d",
			(unsigned long)object_index, (int)record->pose_count, (int)(size / (short)sizeof(real_matrix4x3)));
	}
}
#endif

/* the accessors, keyed by record (section 17 'last piece'): the census, the settle, the interpolated pose, then the
block, in objects.c's order. The block is tick2_pose_block(record, TICK2_POSE_MATRICES), the block getter's own
return for &node_matrices, so the pointer is the same; the record is found again after the settle, as objects.c's
object_get was */
real_matrix4x3 const *store_node_matrices(
	long object_index)
{
	/* psp_fixups.py (FIXES_134 #nmreaders) */
	if (psp_nmr_on > 0)
		psp_nmr_read(object_index, __builtin_return_address(0));
	store_pose_settle_in(object_index, ~0ull); /* TICK2 posenew: every owed node */
	/* while a frame is drawn, the pose between the last two ticks
	(port/linux/game/render_interpolation.c) */
	real_matrix4x3 *interpolated = render_interpolation_object_node_matrices(object_index);

	if (interpolated)
		return interpolated;
	{
		struct tick2_root *record = tick2_record_of_index(object_index);

#ifdef PSP_TICK2_PROOF
		tick2_pose_count_check(object_index, record);
#endif
		return (real_matrix4x3 *)tick2_pose_block(record, TICK2_POSE_MATRICES);
	}
}

real_matrix4x3 const *store_node_matrix(
	long object_index,
	short node_index)
{
	/* psp_fixups.py (FIXES_134 #nmreaders) */
	if (psp_nmr_on > 0)
		psp_nmr_read(object_index, __builtin_return_address(0));
	match_assert("c:\\halo\\SOURCE\\objects\\objects.c", 1060, object_has_node(object_index, node_index));
	/* port: a node the object has no matrix for (an index a map's data
	gave) is its first node, which every object has, not whatever lies
	past its matrices. TICK2 posenew step 5: bound by the record's count,
	which is node_matrices.size / 52 (the proof form compares them) */
	{
		struct tick2_root const *record = tick2_record_of_index(object_index);

#ifdef PSP_TICK2_PROOF
		tick2_pose_count_check(object_index, record);
#endif
		if (node_index < 0 ||
			node_index >= record->pose_count)
		{
			static boolean logged = FALSE;

			if (!logged)
			{
				logged = TRUE;
				error(_error_silent, "object %08lx has no node #%d: its first node is used",
					(unsigned long)object_index, node_index);
			}
			node_index = 0;
		}
	}
	store_pose_settle_in(object_index, 1ull << node_index); /* TICK2 posenew: the node and its parents */

	{
		real_matrix4x3 *interpolated = render_interpolation_object_node_matrices(object_index);

		if (interpolated)
			return &interpolated[node_index];
	}
	return &((real_matrix4x3 *)tick2_pose_block(tick2_record_of_index(object_index), TICK2_POSE_MATRICES))[node_index];
}

#ifdef PSP_BOX_POSING
/* the box (psp/src/box_posing.c), as objects.c declares it */
unsigned long long psp_box_posing_pose(const void *plan, unsigned int flags, const float *position, const float *forward,
	const float *up, float scale, const float *parent, const float *orientations, float *nodes, float *sphere,
	unsigned long long want);
unsigned long long psp_box_posing_all(const void *plan);

/* store_pose_box's store half (section 17 'last piece'): objects.c's store_pose_box_gather makes the plan, the flags
and means B's want (read ahead of the release: psp_boxb_want's busy test reads a slot held by another object, what
it saw after the release) and passes the record, its scale and the pose's inputs. Here, in objects.c's order: means
B's release of the object's own slot, the box on the record's words, and means B's fill. No object_get: object_index
rides along only as B's key (absolute index, section 17 'first piece') */
void store_pose_box(struct tick2_root *record, long object_index, void const *plan, unsigned long flags, real scale,
	real_matrix4x3 const *parent, real_orientation const *node_orientations, real_matrix4x3 *object_nodes,
	unsigned long long want)
{
	/* means B: a new pose replaces what the last one owed */
	struct psp_boxb_slot *slot = &psp_boxb[(unsigned long)object_index & (PSP_BOXB_SLOTS - 1)];
	unsigned long long made, all;

	if (slot->object_index == object_index)
	{
		slot->object_index = NONE;
		psp_boxb_count--;
		psp_boxb_changed = 1;
	}
	made = psp_box_posing_pose(plan, flags, &record->position.x, &record->forward.i, &record->up.i,
		scale, (float const *)parent, (float const *)node_orientations, (float *)object_nodes,
		&record->bounding_sphere_center.x, want);
	if (~want)
	{
		all = psp_box_posing_all(plan);
		psp_boxb_nodes_all += __builtin_popcountll(all);
		psp_boxb_nodes_made += __builtin_popcountll(made & all);
		if ((made & all) != all)
		{
			psp_boxb_owing_poses++;
			slot->made = made;
			slot->all = all;
			slot->plan = plan;
			slot->object_index = object_index;
			psp_boxb_count++;
			psp_boxb_changed = 1;
		}
	}
}
#endif
/* ---- moved from objects.c: the settle, the accessors and the box's store half: finish ---- */
