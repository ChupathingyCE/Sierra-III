/*
store_coll.c: the world store's collision gather table (slice 1's task 4)

docs/box_world_store.md section 18, 'The code plan' (the lead 2026-11-25 13:3x: the intent and the solve stay the
engine's cut behind a boundary until task 5; the gather table, the gathers and the take are the store's). This is
piece 1: bipedstage2's block moved here out of collisions.c, verbatim, read from the tree as every cut before left it
(psp_fixups.py tick2_collnew_move_cut refuses the build if the text here and the text it takes out differ). The six
names it defines are renamed at every call (store_coll_begin, _gather, _gather_margin, _take, _end, the static
_pack); nothing else changes, so play is exact by construction. Piece 2 (after the moved block) is the store's biped
pass in place of the run (objects.c); the move's take (collision_move_pill) stays where it is until piece 3.

What the table is, already as section 18 asks: dense in gather order (psp_bs2_slot, psp_bs2_slots), a biped-to-position
map (psp_bs2_slot_of), the reach guard's fields on positions 0-31 (each entry's sr_clusters, sr_seq and sr_met, and
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
#include "objects/objects.h"
#include "units/bipeds.h"
#include "psp_sweepme.h" /* the ME's gather record the exact gather names (FIXES_SWEEPME) */
#include "store_coll.h"

/* ---- moved from collisions.c (FIXES_BIPEDSTAGE's slice 2 block, as every cut before left it): begin ---- */
/* (FIXES_BIPEDSTAGE) slice 2 (docs/box_biped_motion.md §3b, a gather and a
solve): every living staged biped's move gathered before any biped moves,
each into its own packed list, so the gather's code runs once over all of
them; the move then takes its list in place of walking the map again. The
gather's sphere is the move's own grown by a margin, so this tick's change of
velocity stays inside it: a move whose sphere isn't inside, or whose flags or
pill differ, gathers as before, counted. A gather averages ~0.2 KB (the lab's
findings gather1_features); a tick's fit 32 KB (16 KB filled on b30, the lab's
stage3), one that doesn't fit gathers as before. An empty gather is kept as
empty: the move's own sphere, inside it, finds nothing either */
#include "units/biped_definitions.h"
boolean network_coop_player_collisions(void);
#define PSP_BS2_SLOTS 256
#define PSP_BS2_ARENA 32768
struct psp_bs2_slot
{
	long biped_index;
	unsigned long flags;
	real height, width, radius;
	real_point3d center;
	short count[NUMBER_OF_COLLISION_FEATURE_TYPES];
	unsigned short offset;
	short used, empty;
	short sr_clusters[PSP_SR_CLUSTERS], sr_cluster_count; /* SWEEPREUSE: the gather's clusters */
	unsigned long sr_seq; /* TICK2 stamps: the gather's order */
	int sr_met;
};
static struct psp_bs2_slot psp_bs2_slot[PSP_BS2_SLOTS];
/* SWEEPREUSE: the near lists of the first 32 slots only (a tick uses ~14-28; a slot past them keeps none, so its moves aren't kept) */
#define PSP_BS2_SR 32
static struct { short count; struct psp_sr_near near[PSP_SR_NEAR]; } psp_bs2_sr[PSP_BS2_SR];
static unsigned char psp_bs2_arena[PSP_BS2_ARENA] __attribute__((aligned(16)));
int psp_bs2_on = -1, psp_bs2_slots, psp_bs2_fill, psp_bs2_peak;
static real psp_bs2_margin;
/* gathered, taken, no slot, flags differ, sphere outside, arena full, empty, pill differs */
static unsigned long psp_bs2_stat[8], psp_bs2_ticks;
extern struct collision_feature_list *psp_stackhot_list[2];
/* the move's packed list read where it lies (collision_features.c's
collision_features_test_vector reads through it for that list only), not
copied back into the shared one; keep word bipedstage2copy copies as before */
struct psp_bs2_view
{
	void const *list;
	struct collision_sphere const *spheres;
	struct collision_cylinder const *cylinders;
	struct collision_prism const *prisms;
} psp_bs2_view;
static int psp_bs2_copy = -1;

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

/* the slot's list gathered and packed into the arena (the gather at the
tick's start and bipedstage3's exact call share it) */
/* each slot's place by its biped's datum index (low bits), so the move's take
finds it without a scan; an entry is trusted only when the slot it names holds
this biped in this tick (a collision or an old tick's entry falls back to the
scan) */
#define PSP_BS2_SLOT_OF 1024
static short psp_bs2_slot_of[PSP_BS2_SLOT_OF];
static void store_coll_pack(struct psp_bs2_slot *slot, long biped_index)
{
	psp_bs2_slot_of[biped_index & (PSP_BS2_SLOT_OF - 1)] = (short)(slot - psp_bs2_slot);
	struct collision_feature_list *list = psp_stackhot_list[1];
	unsigned long bytes[NUMBER_OF_COLLISION_FEATURE_TYPES];
	short k;

	{ /* SWEEPREUSE: the gather's clusters and objects, kept with the slot */
		int on = psp_sr_on_pill, met = psp_sr_met;
		short count = psp_sr_cluster_count, near = psp_sr_near_count;

		psp_sr_on_pill = 1;
		psp_sr_met = 0;
		psp_sr_cluster_count = 0;
		psp_sr_near_count = 0;
		tick2_cs_settle();
		slot->sr_seq = tick2_cs_seq; /* TICK2 stamps */
		slot->empty = !collision_get_features_in_sphere(slot->flags, &slot->center, slot->radius, slot->height, slot->width, biped_index, list);
		slot->sr_cluster_count = psp_sr_cluster_count;
		if (psp_sr_cluster_count > 0)
			memcpy(slot->sr_clusters, psp_sr_cluster_list, psp_sr_cluster_count * sizeof(short));
		slot->sr_met = psp_sr_met;
		psp_sr_slot_watch_set((int)(slot - psp_bs2_slot), slot->sr_clusters, psp_sr_cluster_count, biped_index, &slot->center, slot->radius); /* PSP_SR_REACH */
		if (slot - psp_bs2_slot < PSP_BS2_SR)
		{
			psp_bs2_sr[slot - psp_bs2_slot].count = psp_sr_near_count;
			if (psp_sr_near_count > 0)
				memcpy(psp_bs2_sr[slot - psp_bs2_slot].near, psp_sr_near_list, psp_sr_near_count * sizeof(struct psp_sr_near));
		}
		psp_sr_on_pill = on;
		psp_sr_met = met;
		psp_sr_cluster_count = count;
		psp_sr_near_count = near;
	}
	if (slot->empty)
	{
		slot->biped_index = biped_index;
		slot->used = 0;
		psp_bs2_slots++;
		psp_bs2_stat[6]++;
		return;
	}
	bytes[_collision_feature_sphere] = list->count[_collision_feature_sphere] * sizeof(struct collision_sphere);
	bytes[_collision_feature_cylinder] = list->count[_collision_feature_cylinder] * sizeof(struct collision_cylinder);
	bytes[_collision_feature_prism] = list->count[_collision_feature_prism] * sizeof(struct collision_prism);
	if (psp_bs2_fill + bytes[0] + bytes[1] + bytes[2] > PSP_BS2_ARENA)
	{
		psp_bs2_stat[5]++;
		return;
	}
	slot->biped_index = biped_index;
	slot->offset = (unsigned short)psp_bs2_fill;
	slot->used = 0;
	for (k = 0; k < NUMBER_OF_COLLISION_FEATURE_TYPES; k++)
		slot->count[k] = list->count[k];
	memcpy(psp_bs2_arena + psp_bs2_fill, list->spheres, bytes[_collision_feature_sphere]);
	psp_bs2_fill += bytes[_collision_feature_sphere];
	memcpy(psp_bs2_arena + psp_bs2_fill, list->cylinders, bytes[_collision_feature_cylinder]);
	psp_bs2_fill += bytes[_collision_feature_cylinder];
	memcpy(psp_bs2_arena + psp_bs2_fill, list->prisms, bytes[_collision_feature_prism]);
	psp_bs2_fill += bytes[_collision_feature_prism];
	psp_bs2_fill = (psp_bs2_fill + 3) & ~3;
	psp_bs2_slots++;
	psp_bs2_stat[0]++;
}

void store_coll_gather_margin(long biped_index)
{
	struct psp_bs2_slot *slot;
	real_point3d base;
	real_vector3d const *velocity;
	real height, width;

	if (psp_bs2_slots >= PSP_BS2_SLOTS)
		return;
	slot = &psp_bs2_slot[psp_bs2_slots];
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

/* bipedstage3: the move's own call, kept by its intent (bipeds.c), gathered as
collision_move_pill would make its sphere, with no margin for the velocity's
change (1e-4 wu covers rounding only). 1, handled (gathered, or no gather
wanted: the update stopped short of a move, or the move has no collision);
0, no record, gather as at the tick's start */
int psp_bipedstage3_call(long biped_index, unsigned long *flags, real_point3d *position,
	real_vector3d *velocity, real *height, real *width); /* bipeds.c (FIXES_BIPEDSTAGE) */
int store_coll_gather(long biped_index)
{
	struct psp_bs2_slot *slot;
	real_point3d position;
	real_vector3d velocity;
	unsigned long flags;
	real height, width;
	int stop = psp_bipedstage3_call(biped_index, &flags, &position, &velocity, &height, &width);

	if (!stop)
		return FALSE;
	if (stop == 2 || !flags || psp_bs2_slots >= PSP_BS2_SLOTS)
		return TRUE;
	slot = &psp_bs2_slot[psp_bs2_slots];
	slot->center.x = position.x + velocity.i * 0.5f;
	slot->center.y = position.y + velocity.j * 0.5f;
	slot->center.z = height * 0.5f + (position.z + velocity.k * 0.5f);
	slot->radius = magnitude3d(&velocity) * 0.5f + height * 0.5f + width + 1e-4f;
	slot->flags = flags;
	slot->height = height;
	slot->width = width;
	psp_sweepme_pending = psp_sweepme_find(biped_index, &slot->center.x, slot->radius, height, width,
		(flags & FLAG(_collision_test_structure_bit)) != 0); /* (FIXES_SWEEPME) */
	store_coll_pack(slot, biped_index);
	psp_sweepme_pending = -1;
	return TRUE;
}

/* the move's list from its slot, unpacked into the shared list it would have
walked into: 1, a list; 2, the gather found nothing; 0, gather as before */
int store_coll_take(unsigned long flags, real_point3d const *center, real radius,
	real height, real width, long biped_index, struct collision_feature_list *list)
{
	struct psp_bs2_slot *slot = NULL;
	unsigned char const *p;
	real_vector3d d;
	int k;

	k = psp_bs2_slot_of[biped_index & (PSP_BS2_SLOT_OF - 1)];
	if (k >= 0 && k < psp_bs2_slots && psp_bs2_slot[k].biped_index == biped_index && !psp_bs2_slot[k].used)
		slot = &psp_bs2_slot[k];
	for (k = 0; !slot && k < psp_bs2_slots; k++)
	{
		if (psp_bs2_slot[k].biped_index == biped_index && !psp_bs2_slot[k].used)
		{
			slot = &psp_bs2_slot[k];
			break;
		}
	}
	if (!slot)
	{
		psp_bs2_stat[2]++;
		return FALSE;
	}
	if (slot->flags != flags)
	{
		psp_bs2_stat[3]++;
		return FALSE;
	}
	if (slot->height != height || slot->width != width)
	{
		psp_bs2_stat[7]++;
		return FALSE;
	}
	vector_from_points3d(center, &slot->center, &d);
	if (magnitude3d(&d) + radius > slot->radius)
	{
		psp_bs2_stat[4]++;
		return FALSE;
	}
	slot->used = 1;
	psp_bs2_stat[1]++;
	if (psp_sr_on_pill) /* SWEEPREUSE: the move's walk is the slot's */
	{
		psp_sr_cluster_count = slot->sr_cluster_count;
		if (slot->sr_cluster_count > 0)
			memcpy(psp_sr_cluster_list, slot->sr_clusters, slot->sr_cluster_count * sizeof(short));
		psp_sr_met += slot->sr_met;
		psp_sr_gather_seq = slot->sr_seq; /* TICK2 stamps */
		psp_sr_reach_inherit = slot - psp_bs2_slot < 32 ? psp_sr_slot_dead[slot - psp_bs2_slot] : 1; /* PSP_SR_REACH */
		psp_sr_center = slot->center;
		psp_sr_radius = slot->radius;
		psp_sr_near_count = slot - psp_bs2_slot < PSP_BS2_SR ? psp_bs2_sr[slot - psp_bs2_slot].count : -1;
		if (psp_sr_near_count > 0)
			memcpy(psp_sr_near_list, psp_bs2_sr[slot - psp_bs2_slot].near, psp_sr_near_count * sizeof(struct psp_sr_near));
	}
	if (slot->empty)
		return 2;
	p = psp_bs2_arena + slot->offset;
	for (k = 0; k < NUMBER_OF_COLLISION_FEATURE_TYPES; k++)
		list->count[k] = slot->count[k];
	if (psp_bs2_copy <= 0)
	{
		psp_bs2_view.spheres = (struct collision_sphere const *)p;
		p += slot->count[_collision_feature_sphere] * sizeof(struct collision_sphere);
		psp_bs2_view.cylinders = (struct collision_cylinder const *)p;
		p += slot->count[_collision_feature_cylinder] * sizeof(struct collision_cylinder);
		psp_bs2_view.prisms = (struct collision_prism const *)p;
		psp_bs2_view.list = list;
		return 1;
	}
	memcpy(list->spheres, p, slot->count[_collision_feature_sphere] * sizeof(struct collision_sphere));
	p += slot->count[_collision_feature_sphere] * sizeof(struct collision_sphere);
	memcpy(list->cylinders, p, slot->count[_collision_feature_cylinder] * sizeof(struct collision_cylinder));
	p += slot->count[_collision_feature_cylinder] * sizeof(struct collision_cylinder);
	memcpy(list->prisms, p, slot->count[_collision_feature_prism] * sizeof(struct collision_prism));
	return 1;
}

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

/* ---- moved from collisions.c: the gather table: finish ---- */

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
