/*
store_coll.c: the world store's collision gather table (slice 1's task 4)

docs/box_world_store.md section 18, 'The code plan' (the lead 2026-11-25 13:3x: the intent and the solve stay the
engine's cut behind a boundary until task 5; the gather table, the gathers and the take are the store's). Piece 1
moved bipedstage2's block here out of collisions.c, verbatim, under six new names at every call (store_coll_begin,
_gather, _gather_margin, _take, _end, the static _pack). The gather table is now the store's own code, written new
over its own table (section 18, 'The gather and the take rewritten'): struct store_coll_entry, the table's names
static, the take's map by absolute index with no scan. The moved copy is the control,
psp/control/store_coll_moved.txt, not built (psp_fixups.py tick2_collnew_move_cut refuses the build if the tree's
block and the control differ). Piece 2 (after the gather table) is the store's biped
pass in place of the run (objects.c). Piece 3 (after the pass's markers' block) is the physics' two entries:
store_coll_move, collision_move_pill as the cuts before left it with the take, moved verbatim (tick2_collnew_movecall_cut
refuses the build if it differs), and store_coll_stand, the stand test's two engine calls; the view's type is the
header's.

What the table is, as section 18 asks: dense in gather order (store_coll_entries, psp_bs2_slots), a biped-to-position
map by absolute index (psp_bs2_slot_of), the reach guard's fields on positions 0-31 (each entry's sr_clusters, sr_seq and sr_met, and
psp_bs2_sr's near lists); the guard's 512-cluster slot mask stays collisions.c's, set at the gather
(psp_sr_slot_watch_set) and read at the take (psp_sr_slot_dead). A slot holds the world as the gather phase saw it
until its move takes it, by the four checks (this biped and unused, the flags, the pill, the sphere inside); a refusal
gathers live, as old-only does.

Built only with PSP_TICK2_COLLNEW=1 (psp_build.py), as collisions.c is: its flags, LTO with it.
*/

#include <string.h>
#include "cseries.h"
#include "math/real_math.h"
#include "physics/collisions.h"
#include "physics/collision_features.h"
#include "physics/collision_usage.h" /* the move's collision user stack (piece 3) */
#include "objects/objects.h"
#include "units/bipeds.h"
#include "psp_sweepme.h" /* the ME's gather record the exact gather names (FIXES_SWEEPME) */
#include "store_coll.h"

/* ---- the store's gather table, written new (docs/box_world_store.md section 18, 'The gather and the take rewritten';
its control, the moved copy, is psp/control/store_coll_moved.txt): begin ---- */
/* bipedstage2's slice 2 (docs/box_biped_motion.md §3b): every living staged biped's move gathered before any biped
moves, each into its own packed list, and the move takes its list in place of walking the map again. The gather's
sphere is the move's own grown by a margin (the margin gather) or by 1e-4 wu (the exact gather, from the intent's
kept call), so this tick's change of velocity stays inside it. A move whose flags or pill differ, or whose sphere
isn't inside, gathers live, counted. A tick's lists fit 32 KB (16 KB filled on b30, the lab's stage3); one that
doesn't fit gathers live. An empty gather is kept as empty: the move's own sphere, inside it, finds nothing either.
Nothing outside this unit names the table; the view is the engine's reader's (collision_features.c) until task 5 */
#include "units/biped_definitions.h"
boolean network_coop_player_collisions(void);
#define PSP_BS2_SLOTS 256 /* the table's cap: entries, dense in gather order */
#define PSP_BS2_ARENA 32768 /* the packed lists' bytes */
struct store_coll_entry
{
	long biped_index; /* the handle it was gathered for */
	unsigned long flags; /* the move's test flags */
	real height, width, radius; /* the pill, and the gather's sphere */
	real_point3d center;
	short count[NUMBER_OF_COLLISION_FEATURE_TYPES]; /* the packed list's counts by kind */
	unsigned short offset; /* its place in the arena */
	short used, empty; /* taken this tick; the gather found nothing */
	short sr_clusters[PSP_SR_CLUSTERS], sr_cluster_count; /* SWEEPREUSE: the gather's clusters */
	unsigned long sr_seq; /* TICK2 stamps: the gather's order */
	int sr_met;
};
static struct store_coll_entry store_coll_entries[PSP_BS2_SLOTS];
/* SWEEPREUSE: the near lists of the first 32 entries only (a tick uses ~14-28; an entry past them keeps none, so its
moves aren't kept) */
#define PSP_BS2_SR 32
static struct { short count; struct psp_sr_near near[PSP_SR_NEAR]; } psp_bs2_sr[PSP_BS2_SR];
static unsigned char psp_bs2_arena[PSP_BS2_ARENA] __attribute__((aligned(16)));
static int psp_bs2_on = -1, psp_bs2_slots, psp_bs2_fill, psp_bs2_peak;
static real psp_bs2_margin;
/* gathered, taken, no slot, flags differ, sphere outside, arena full, empty, pill differs */
static unsigned long psp_bs2_stat[8], psp_bs2_ticks;
/* the take's map: each entry's position, keyed by its biped's absolute index (DATUM_INDEX_TO_ABSOLUTE_INDEX, under
MAXIMUM_OBJECTS_PER_MAP, so MAXIMUM_OBJECTS_PER_MAP bytes; positions 0-255 fit a byte; two live bipeds never share a
key), so the take reads it once, with no scan. The pack writes it first, before the gather, so before the empty
outcome and the arena-full return, as the moved copy wrote its low-bits map: an arena-full entry's position is the
count. The take trusts it only when the position is under the count, holds this handle and is unused (!used): a stale
position from an earlier tick, an arena-full one and a second take in the tick all read no entry, as the moved copy's
scan did, so the map is exact and no clearing is needed */
static unsigned char psp_bs2_slot_of[MAXIMUM_OBJECTS_PER_MAP];
extern struct collision_feature_list *psp_stackhot_list[2];
/* the move's packed list read where it lies (collision_features.c's collision_features_test_vector reads through it
for that list only), not copied back into the shared one; keep word bipedstage2copy copies as before */
struct psp_bs2_view psp_bs2_view; /* its type: psp/include/store_coll.h (TICK2 collnew, piece 3) */
static int psp_bs2_copy = -1;

/* the tick's start: the three keep words, read once; the table emptied. Nonzero when the table is on */
int store_coll_begin(void)
{
	int psp_keep(const char *name);
	int psp_keep_number(const char *prefix, int fallback);

	if (psp_bs2_on < 0)
	{
		psp_bs2_on = psp_keep("bipedstage2");
		/* in hundredths of a world unit; a tick's velocity change is far under it */
		psp_bs2_margin = psp_keep_number("bipedstage2margin", 3) * 0.01f;
		psp_bs2_copy = psp_keep("bipedstage2copy");
	}
	psp_bs2_slots = 0;
	psp_bs2_fill = 0;
	return psp_bs2_on > 0;
}

/* the entry's list gathered (the engine's query into level 1's shared list, the boundary until slice 2) and packed
into the arena by value. Around the query, the sweep reuse's four gather words are one saved block, opened before it
and closed after it in today's order: settle, order, query, clusters, met, watch, near list. Mapped first (the take's
map, above); kept (counted) unless the arena is full, which leaves the entry at the count, unkept */
static void store_coll_pack(struct store_coll_entry *entry, long biped_index)
{
	struct collision_feature_list *list = psp_stackhot_list[1];
	int position = (int)(entry - store_coll_entries);
	unsigned long absolute = (unsigned long)DATUM_INDEX_TO_ABSOLUTE_INDEX(biped_index);
	unsigned long bytes[NUMBER_OF_COLLISION_FEATURE_TYPES];
	short k;

	if (absolute < MAXIMUM_OBJECTS_PER_MAP) /* always, for a biped's handle */
		psp_bs2_slot_of[absolute] = (unsigned char)position;
	{ /* SWEEPREUSE: the gather's clusters and objects, kept with the entry */
		int on = psp_sr_on_pill, met = psp_sr_met;
		short count = psp_sr_cluster_count, near = psp_sr_near_count;

		psp_sr_on_pill = 1;
		psp_sr_met = 0;
		psp_sr_cluster_count = 0;
		psp_sr_near_count = 0;
		tick2_cs_settle();
		entry->sr_seq = tick2_cs_seq; /* TICK2 stamps */
		entry->empty = !collision_get_features_in_sphere(entry->flags, &entry->center, entry->radius, entry->height, entry->width, biped_index, list);
		entry->sr_cluster_count = psp_sr_cluster_count;
		if (psp_sr_cluster_count > 0)
			memcpy(entry->sr_clusters, psp_sr_cluster_list, psp_sr_cluster_count * sizeof(short));
		entry->sr_met = psp_sr_met;
		psp_sr_slot_watch_set(position, entry->sr_clusters, psp_sr_cluster_count, biped_index, &entry->center, entry->radius); /* PSP_SR_REACH */
		if (position < PSP_BS2_SR)
		{
			psp_bs2_sr[position].count = psp_sr_near_count;
			if (psp_sr_near_count > 0)
				memcpy(psp_bs2_sr[position].near, psp_sr_near_list, psp_sr_near_count * sizeof(struct psp_sr_near));
		}
		psp_sr_on_pill = on;
		psp_sr_met = met;
		psp_sr_cluster_count = count;
		psp_sr_near_count = near;
	}
	if (!entry->empty)
	{
		bytes[_collision_feature_sphere] = list->count[_collision_feature_sphere] * sizeof(struct collision_sphere);
		bytes[_collision_feature_cylinder] = list->count[_collision_feature_cylinder] * sizeof(struct collision_cylinder);
		bytes[_collision_feature_prism] = list->count[_collision_feature_prism] * sizeof(struct collision_prism);
		if (psp_bs2_fill + bytes[0] + bytes[1] + bytes[2] > PSP_BS2_ARENA)
		{
			psp_bs2_stat[5]++;
			return;
		}
		entry->offset = (unsigned short)psp_bs2_fill;
		for (k = 0; k < NUMBER_OF_COLLISION_FEATURE_TYPES; k++)
			entry->count[k] = list->count[k];
		memcpy(psp_bs2_arena + psp_bs2_fill, list->spheres, bytes[_collision_feature_sphere]);
		psp_bs2_fill += bytes[_collision_feature_sphere];
		memcpy(psp_bs2_arena + psp_bs2_fill, list->cylinders, bytes[_collision_feature_cylinder]);
		psp_bs2_fill += bytes[_collision_feature_cylinder];
		memcpy(psp_bs2_arena + psp_bs2_fill, list->prisms, bytes[_collision_feature_prism]);
		psp_bs2_fill += bytes[_collision_feature_prism];
		/* rounded to 4 bytes: the rounding decides "arena full", a take verdict */
		psp_bs2_fill = (psp_bs2_fill + 3) & ~3;
	}
	entry->biped_index = biped_index;
	entry->used = 0;
	psp_bs2_slots++;
	psp_bs2_stat[entry->empty ? 6 : 0]++;
}

void store_coll_gather_margin(long biped_index)
{
	struct store_coll_entry *slot;
	real_point3d base;
	real_vector3d const *velocity;
	real height, width;

	if (psp_bs2_slots >= PSP_BS2_SLOTS)
		return;
	slot = &store_coll_entries[psp_bs2_slots];
	biped_get_physics_pill(biped_index, &base, &height, &width);
	velocity = &object_get(biped_index)->object.root->translational_velocity;
	slot->center.x = base.x + velocity->i * 0.5f;
	slot->center.y = base.y + velocity->j * 0.5f;
	slot->center.z = height * 0.5f + (base.z + velocity->k * 0.5f);
	slot->radius = magnitude3d(velocity) * 0.5f + height * 0.5f + width + psp_bs2_margin;
	{
		/* the flags biped_update_moving gives a living biped's move */
		struct biped_datum *biped = biped_get(biped_index);
		struct biped_definition *definition = biped_definition_get(biped->definition_index);

		if (TEST_FLAG(biped->biped.flags, _biped_no_collision_bit))
			return;
		slot->flags = TEST_FLAG(definition->biped.flags, _biped_passes_through_bipeds_bit) ?
			_collision_test_for_bipeds_passthrough_living_flags : _collision_test_for_bipeds_living_flags;
		if (biped->unit.player_index != NONE && !network_coop_player_collisions())
			slot->flags |= FLAG(_collision_test_skip_player_bipeds_bit);
	}
	slot->height = height;
	slot->width = width;
	store_coll_pack(slot, biped_index);
}

#ifndef PSP_TICK2_MOTIONNEW
/* off the switch only: task 5's delete cut deletes bipedstage3 (psp_bipedstage3_call is poisoned) and the pass's G is
store_coll_gather_intent below; this gather's one caller is the pass's #else side */
/* bipedstage3: the move's own call, kept by its intent (bipeds.c), gathered as collision_move_pill would make its
sphere, with no margin for the velocity's change (1e-4 wu covers rounding only). The sphere's four expressions are
psp_sweepme_after_intent's (collisions.c), character for character, since the ME's answer is matched on those floats.
1, handled (gathered, or no gather wanted: the update stopped short of a move, or the move has no collision); 0, no
record, gather by the margin */
int psp_bipedstage3_call(long biped_index, unsigned long *flags, real_point3d *position,
	real_vector3d *velocity, real *height, real *width); /* bipeds.c (FIXES_BIPEDSTAGE) */
int store_coll_gather(long biped_index)
{
	struct store_coll_entry *entry;
	real_point3d position;
	real_vector3d velocity;
	unsigned long flags;
	real height, width;
	int stop = psp_bipedstage3_call(biped_index, &flags, &position, &velocity, &height, &width);

	if (!stop)
		return FALSE;
	if (stop == 2 || !flags || psp_bs2_slots >= PSP_BS2_SLOTS)
		return TRUE;
	entry = &store_coll_entries[psp_bs2_slots];
	entry->center.x = position.x + velocity.i * 0.5f;
	entry->center.y = position.y + velocity.j * 0.5f;
	entry->center.z = height * 0.5f + (position.z + velocity.k * 0.5f);
	entry->radius = magnitude3d(&velocity) * 0.5f + height * 0.5f + width + 1e-4f;
	entry->flags = flags;
	entry->height = height;
	entry->width = width;
	psp_sweepme_pending = psp_sweepme_find(biped_index, &entry->center.x, entry->radius, height, width,
		(flags & FLAG(_collision_test_structure_bit)) != 0); /* (FIXES_SWEEPME) */
	store_coll_pack(entry, biped_index);
	psp_sweepme_pending = -1;
	return TRUE;
}
#endif

#ifdef PSP_TICK2_MOTIONNEW
#include "store_motion.h"
/* task 5's G (docs/box_world_store.md section 19, step 3): staged biped k's gather from the INTENT entry I kept, in
place of store_coll_gather's read of bipedstage3's record. The tests are store_coll_gather's in its order; the sphere
is the entry's, made in I by the same four expressions on the same floats (Part 7); the ME's record is found at the
place its add gave (Part 9) */
int store_coll_gather_intent(short k)
{
	struct store_intent const *intent = store_intent_of(k);
	struct store_coll_entry *entry;

	if (!intent)
		return FALSE;
	if (intent->stop == 2 || !intent->flags || psp_bs2_slots >= PSP_BS2_SLOTS)
		return TRUE;
	entry = &store_coll_entries[psp_bs2_slots];
	entry->center = intent->center;
	entry->radius = intent->radius;
	entry->flags = intent->flags;
	entry->height = intent->height;
	entry->width = intent->width;
	psp_sweepme_pending = psp_sweepme_find_at(intent->me, intent->biped_index, &entry->center.x, entry->radius,
		entry->height, entry->width, (intent->flags & FLAG(_collision_test_structure_bit)) != 0); /* (FIXES_SWEEPME) */
	store_coll_pack(entry, intent->biped_index);
	psp_sweepme_pending = -1;
	return TRUE;
}
#endif

/* the move's list from its entry: 1, a list (the view, or with bipedstage2copy unpacked into the caller's list); 2,
the gather found nothing; 0, gather live. The entry is found in one read of the map; the four checks keep their order
and their counters (no entry, flags, pill, sphere outside) */
int store_coll_take(unsigned long flags, real_point3d const *center, real radius,
	real height, real width, long biped_index, struct collision_feature_list *list)
{
	struct store_coll_entry *entry;
	unsigned char const *p;
	real_vector3d d;
	unsigned long absolute = (unsigned long)DATUM_INDEX_TO_ABSOLUTE_INDEX(biped_index);
	int position = absolute < MAXIMUM_OBJECTS_PER_MAP ? psp_bs2_slot_of[absolute] : PSP_BS2_SLOTS;
	int k;

	/* a biped packs at most once a tick, so the unused entry holding this handle is this one or none */
	if (position >= psp_bs2_slots || store_coll_entries[position].biped_index != biped_index ||
		store_coll_entries[position].used)
	{
		psp_bs2_stat[2]++;
		return FALSE;
	}
	entry = &store_coll_entries[position];
	if (entry->flags != flags)
	{
		psp_bs2_stat[3]++;
		return FALSE;
	}
	if (entry->height != height || entry->width != width)
	{
		psp_bs2_stat[7]++;
		return FALSE;
	}
	vector_from_points3d(center, &entry->center, &d);
	if (magnitude3d(&d) + radius > entry->radius)
	{
		psp_bs2_stat[4]++;
		return FALSE;
	}
	entry->used = 1;
	psp_bs2_stat[1]++;
	if (psp_sr_on_pill) /* SWEEPREUSE: the move's walk is the entry's; the guard's hand-over words */
	{
		psp_sr_cluster_count = entry->sr_cluster_count;
		if (entry->sr_cluster_count > 0)
			memcpy(psp_sr_cluster_list, entry->sr_clusters, entry->sr_cluster_count * sizeof(short));
		psp_sr_met += entry->sr_met;
		psp_sr_gather_seq = entry->sr_seq; /* TICK2 stamps */
		psp_sr_reach_inherit = position < 32 ? psp_sr_slot_dead[position] : 1; /* PSP_SR_REACH */
		psp_sr_center = entry->center;
		psp_sr_radius = entry->radius;
		psp_sr_near_count = position < PSP_BS2_SR ? psp_bs2_sr[position].count : -1;
		if (psp_sr_near_count > 0)
			memcpy(psp_sr_near_list, psp_bs2_sr[position].near, psp_sr_near_count * sizeof(struct psp_sr_near));
	}
	if (entry->empty)
		return 2;
	p = psp_bs2_arena + entry->offset;
	for (k = 0; k < NUMBER_OF_COLLISION_FEATURE_TYPES; k++)
		list->count[k] = entry->count[k];
	if (psp_bs2_copy <= 0)
	{
		psp_bs2_view.spheres = (struct collision_sphere const *)p;
		p += entry->count[_collision_feature_sphere] * sizeof(struct collision_sphere);
		psp_bs2_view.cylinders = (struct collision_cylinder const *)p;
		p += entry->count[_collision_feature_cylinder] * sizeof(struct collision_cylinder);
		psp_bs2_view.prisms = (struct collision_prism const *)p;
		psp_bs2_view.list = list;
		return 1;
	}
	memcpy(list->spheres, p, entry->count[_collision_feature_sphere] * sizeof(struct collision_sphere));
	p += entry->count[_collision_feature_sphere] * sizeof(struct collision_sphere);
	memcpy(list->cylinders, p, entry->count[_collision_feature_cylinder] * sizeof(struct collision_cylinder));
	p += entry->count[_collision_feature_cylinder] * sizeof(struct collision_cylinder);
	memcpy(list->prisms, p, entry->count[_collision_feature_prism] * sizeof(struct collision_prism));
	return 1;
}

/* the tick's end: the fill's peak, the table emptied, and the log line every 1,000 ticks (its text and its seven
counters' order are what the readers parse, on both sides) */
void store_coll_end(void)
{
	void platform_psp_log(const char *format, ...);

	if (psp_bs2_fill > psp_bs2_peak)
		psp_bs2_peak = psp_bs2_fill;
	psp_bs2_slots = 0;
	if (psp_bs2_on > 0 && ++psp_bs2_ticks % 1000 == 0)
		platform_psp_log("bipedstage2: %lu staged ticks: gathered %lu (%lu empty), taken %lu; gathered again: no slot %lu, flags %lu, pill %lu, outside the sphere %lu; arena full %lu; arena peak %d of %d bytes",
			psp_bs2_ticks, psp_bs2_stat[0] + psp_bs2_stat[6], psp_bs2_stat[6], psp_bs2_stat[1], psp_bs2_stat[2], psp_bs2_stat[3],
			psp_bs2_stat[7], psp_bs2_stat[4], psp_bs2_stat[5], psp_bs2_peak, PSP_BS2_ARENA);
}

/* ---- the store's gather table: finish ---- */

#ifdef PSP_TICK2_MOTIONNEW
/* the store's biped pass (slice 1's task 5; docs/box_world_store.md section 19, M's design part 4), called at the
walk's end as task 4's was: the unit parts, then I (each living biped's update to its move's call, kept as its
intent, the ME's level half handed over at each keep), G (each intent's gather, from the world as it stands before
any biped moves; no margin gather: clause 10's branch (i), the lead 2026-11-26 00:2x (2)), the ME's end, M (each
staged biped's datum_update, store_motion_update, with store_motion_cur its staged position: it resumes the biped's
intent, or runs its whole update), the settle, and the tick's ends, I's last because M reads its entries. The phase
is old-only's with bipedstage3 on: 4, or 0 for a dead biped with no intent, whose unit part runs whole (object_types.c) */
int store_biped_units(void); /* objects.c (TICK2 motionnew): the staged list's four */
long store_biped_at(short k, int *dead);
void store_biped_update(short k, int phase);
void store_biped_settle(void);
void psp_sweepme_tick_begin(void); /* collisions.c (FIXES_SWEEPME) */

void store_biped_pass(void)
{
	long biped_index;
	int count, dead;
	short k;

	count = store_biped_units();
	if (!count)
		return;
	store_intent_begin();
	psp_sweepme_tick_begin(); /* (FIXES_SWEEPME) */
	for (k = 0; k < count; k++)
	{
		biped_index = store_biped_at(k, &dead);
		if (biped_index != NONE && !dead)
			store_intent_run(k, biped_index);
	}
	psp_sweepme_close(); /* (FIXES_SWEEPME) */
	/* slice 2: every living biped's move gathered as one pass, from the
	world as it stands before any biped moves */
	if (store_coll_begin())
	{
		for (k = 0; k < count; k++)
		{
			biped_index = store_biped_at(k, &dead);
			if (biped_index != NONE && !dead)
				store_coll_gather_intent(k);
		}
	}
	psp_sweepme_end(); /* (FIXES_SWEEPME) */
	for (k = 0; k < count; k++)
	{
		biped_index = store_biped_at(k, &dead);
		if (biped_index == NONE)
			continue;
		store_motion_cur = k;
		store_biped_update(k, dead && !store_intent_of(k) ? 0 : 4);
		store_motion_cur = -1;
	}
	store_biped_settle();
	store_coll_end();
	store_intent_end();
}
#else
/* piece 2: the store's biped pass, in place of psp_bipedstage_run at the walk's end (psp_fixups.py
tick2_collnew_pass_cut). The intent and the solve are the engine's, until task 5: the run's text before and after
its gather, verbatim in objects.c. The gather loop between them is the run's, over the same list in the same order,
with the run's own test (store_biped_alive_until_task5); a biped whose intent left no record (store_coll_gather
returns 0), or every biped when bipedstage3 is off, gathers by the margin, as old-only does */
int store_biped_intent_until_task5(int *intents); /* objects.c (TICK2 collnew), until task 5 */
long store_biped_alive_until_task5(short k);
void store_biped_solve_until_task5(void);

void store_biped_pass(void)
{
	long biped_index;
	int intents = 0, count;
	short k;

	count = store_biped_intent_until_task5(&intents);
	if (!count)
		return;
	/* slice 2: every living biped's move gathered as one pass, from the
	world as it stands before any biped moves */
	if (store_coll_begin())
	{
		for (k = 0; k < count; k++)
		{
			biped_index = store_biped_alive_until_task5(k);
			if (biped_index != NONE && !(intents && store_coll_gather(biped_index)))
				store_coll_gather_margin(biped_index);
		}
	}
	psp_sweepme_end(); /* (FIXES_SWEEPME) */
	store_biped_solve_until_task5();
	store_coll_end();
}
#endif

/* ---- moved from collisions.c (collision_move_pill with the take, as every cut before left it): begin ---- */
short store_coll_move(
	unsigned long flags,
	real_point3d const *position,
	real_vector3d const *velocity,
	real height,
	real radius,
	long ignore_object_index,
	real_point3d *clipped_position,
	real_vector3d *clipped_velocity,
	short maximum_collision_count,
	struct collision_plane *collisions)
{
	short result = FALSE;
	/* psp_fixups.py (FIXES_STACKHOT2): level 1's shared list */
	extern struct collision_feature_list *psp_stackhot_list[2];
#define features (*psp_stackhot_list[1])
	real_point3d center;

	match_assert("c:\\halo\\SOURCE\\physics\\collisions.c", 1214, COLLISION_USER_DEPTH < MAXIMUM_COLLISION_USER_STACK_DEPTH);
	COLLISION_USER_PUSH(7);

	center.x = position->x + velocity->i * 0.5f;
	center.y = position->y + velocity->j * 0.5f;
	center.z = height * 0.5f + (position->z + velocity->k * 0.5f);

	int psp_bs2_took = psp_bs2_slots ? store_coll_take(flags, &center, magnitude3d(velocity) * 0.5f + height * 0.5f + radius,
		height, radius, ignore_object_index, &features) : 0;

	if (psp_bs2_took == 1 || (psp_bs2_took == 0 && collision_get_features_in_sphere(
		flags,
		&center,
		magnitude3d(velocity) * 0.5f + height * 0.5f + radius,
		height,
		radius,
		ignore_object_index,
		&features)))
	{
		/* (FIXES_BIPEDSTAGE) "gathercensus": slice 2's gather sized, the features
		one move's gather finds by kind, so the box's per-biped list can be packed */
		static int psp_gc_on = -1;
		static unsigned long psp_gc_calls, psp_gc_sum[3], psp_gc_max[3], psp_gc_bytes_max, psp_gc_bytes_sum, psp_gc_over[4];
		int psp_keep(const char *name);
		int psp_keep_loaded(void);
		void platform_psp_log(const char *format, ...);

		if (psp_gc_on < 0 && psp_keep_loaded())
			psp_gc_on = psp_keep("gathercensus");
		if (psp_gc_on > 0)
		{
			unsigned long bytes = 0, k;
			for (k = 0; k < 3; k++)
			{
				unsigned long n = (unsigned long)features.count[k];
				psp_gc_sum[k] += n;
				if (n > psp_gc_max[k])
					psp_gc_max[k] = n;
			}
			bytes = features.count[_collision_feature_sphere] * sizeof(struct collision_sphere) +
				features.count[_collision_feature_cylinder] * sizeof(struct collision_cylinder) +
				features.count[_collision_feature_prism] * sizeof(struct collision_prism);
			psp_gc_bytes_sum += bytes;
			if (bytes > psp_gc_bytes_max)
				psp_gc_bytes_max = bytes;
			psp_gc_over[bytes > 4096 ? 3 : bytes > 2048 ? 2 : bytes > 1024 ? 1 : 0]++;
			if (++psp_gc_calls % 2000 == 0)
				platform_psp_log("gathercensus: %lu gathers; a gather's features: spheres %lu.%02lu (max %lu), cylinders %lu.%02lu (max %lu), prisms %lu.%02lu (max %lu); bytes a gather %lu (max %lu); <=1K %lu, <=2K %lu, <=4K %lu, >4K %lu",
					psp_gc_calls,
					psp_gc_sum[0] / psp_gc_calls, psp_gc_sum[0] * 100 / psp_gc_calls % 100, psp_gc_max[0],
					psp_gc_sum[1] / psp_gc_calls, psp_gc_sum[1] * 100 / psp_gc_calls % 100, psp_gc_max[1],
					psp_gc_sum[2] / psp_gc_calls, psp_gc_sum[2] * 100 / psp_gc_calls % 100, psp_gc_max[2],
					psp_gc_bytes_sum / psp_gc_calls, psp_gc_bytes_max,
					psp_gc_over[0], psp_gc_over[1], psp_gc_over[2], psp_gc_over[3]);
		}
		result = collision_move_point(
			position,
			velocity,
			&features,
			clipped_position,
			clipped_velocity,
			maximum_collision_count,
			collisions);
	}
	else
	{
		add_vectors3d(
			(real_vector3d const *)position,
			velocity,
			(real_vector3d *)clipped_position);
		*clipped_velocity = *velocity;
	}

	psp_bs2_view.list = NULL; /* (FIXES_BIPEDSTAGE) the move's packed list is done with */
	match_assert("c:\\halo\\SOURCE\\physics\\collisions.c", 1230, COLLISION_USER_DEPTH > 1);
	COLLISION_USER_POP();

	return result;
}
#undef features /* psp_fixups.py (FIXES_STACKHOT2) */
/* ---- moved from collisions.c: the move: finish ---- */

/* piece 3: the stand test of a crouched biped trying to stand (biped_update_physics), the engine's two calls in the
same order on the same list (level 0's, FIXES_STACKHOT2): the world's features in the standing pill's sphere, then the
standing vector against them. TRUE when it meets something (the caller sets cannot-stand, as the engine did). The
standing height is the caller's expression, collision_height_standing - 2 * collision_radius, passed in; the
engine made it only after a gather found something, which reads nothing it writes. Until task 5 */
boolean store_coll_stand(unsigned long flags, real_point3d const *center, real radius, real width,
	long biped_index, real_point3d const *position, real standing_height)
{
	extern struct collision_feature_list *psp_stackhot_list[2];
#define features (*psp_stackhot_list[0])
	boolean result = FALSE;

	if (collision_get_features_in_sphere(
		flags,
		center,
		radius,
		0.f,
		width,
		biped_index,
		&features))
	{
		real_vector3d standing_vector;
		struct collision_plane collision;

		scale_vector3d(
			global_up3d,
			standing_height,
			&standing_vector);
		if (collision_features_test_vector(
			&features,
			position,
			&standing_vector,
			&collision))
		{
			result = TRUE;
		}
	}
#undef features
	return result;
}
