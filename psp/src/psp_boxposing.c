/*
The posing box cut into the game (PSP_BOXPOSING=1; docs/box_posing.md, "The
cut-in"). The engine's side (psp_fixups.py, FIXES_BOXPOSING) gathers an
object's place and orientations as the capture does and calls here; this side
keeps each model's plan for the map and runs box_posing_pose. It knows the box
and no engine header: models are only keys, their links come in as numbers.

A map's plans: up to PSP_BOX_PLANS, found through PSP_BOX_SLOTS slots by the
object definition's address (means A, docs/box_posing.md §6d: the plan carries
what the definition fixes, so a pose reads nothing of the definition's). POSEK measured 32-63 posed models a map on c20, b30 and a30
(the lab's posek1). A model past the room still poses through the box, from a
plan made for that one call, and the log counts it: the build's path stays
whole.
*/
#include <string.h>
#include "box_posing.h"

void platform_psp_log(const char *format, ...);

#define PSP_BOX_PLANS 128
#define PSP_BOX_SLOTS 256

struct psp_box_slot
{
	const void *model; /* the key: the object definition */
	short plan; /* an index into psp_box_plans */
};

/* a definition's walk and its place's constants side by side, read together */
struct psp_box_plan
{
	struct box_posing_plan plan;
	struct box_posing_place place;
	/* means B (docs/box_posing.md §1b): the nodes every pose of the
	definition makes for its standing readers (0 until the engine's side has
	named them), and every node of its model (0 when its walk isn't whole:
	such a model owes nothing) */
	unsigned long long standing, all;
};

int psp_keep(const char *name);

static struct psp_box_slot psp_box_slots[PSP_BOX_SLOTS];
static struct psp_box_plan psp_box_plans[PSP_BOX_PLANS];
static struct psp_box_plan psp_box_spare; /* a plan made for one call, when there's no room */
static int psp_box_plan_count, psp_box_spares, psp_box_poses, psp_box_claim = -1;

/* at each map's start: the plans name the last map's models */
void psp_box_posing_forget(void)
{
	if (psp_box_plan_count)
		platform_psp_log("boxposing: %d plans of %d, %d poses, %d poses past the room",
			psp_box_plan_count, PSP_BOX_PLANS, psp_box_poses, psp_box_spares);
	memset(psp_box_slots, 0, sizeof(psp_box_slots));
	psp_box_plan_count = psp_box_spares = psp_box_poses = 0;
	psp_box_claim = -1;
}

/* the model's plan, or NULL when it hasn't one yet (psp_box_posing_plan_add) */
const void *psp_box_posing_plan_find(const void *model)
{
	unsigned long k = (((unsigned long)model >> 4) * 2654435761u) >> 24;
	int probe;

	for (probe = 0; probe < 8; probe++, k = (k + 1) & (PSP_BOX_SLOTS - 1))
	{
		if (psp_box_slots[k].model == model)
			return &psp_box_plans[psp_box_slots[k].plan];
		if (!psp_box_slots[k].model)
			return NULL;
	}
	return NULL;
}

/* a plan from the model's node table (parent, first child, next sibling a
node, as the tag has them; none for an object placed with no model) and the
definition's constants (the centre of mass negated, the origin offset, the
bounding offset and radius: 10 floats), kept for the map when there's room */
const void *psp_box_posing_plan_add(const void *model, const short *links, int count, const float *constants, int physics)
{
	struct box_posing_node nodes[BOX_POSING_NODES];
	struct psp_box_plan *whole = psp_box_plan_count < PSP_BOX_PLANS ? &psp_box_plans[psp_box_plan_count] : &psp_box_spare;
	struct box_posing_plan *plan = &whole->plan;
	int c;
	unsigned long k = (((unsigned long)model >> 4) * 2654435761u) >> 24;
	int n, probe;

	if (count > BOX_POSING_NODES)
		count = BOX_POSING_NODES;
	for (n = 0; n < count; n++)
	{
		nodes[n].parent = links[n * 3];
		nodes[n].first_child = links[n * 3 + 1];
		nodes[n].next_sibling = links[n * 3 + 2];
	}
	box_posing_plan_make(plan, nodes, count < 0 ? 0 : count);
	whole->place.flags = physics ? BOX_POSE_PHYSICS : 0;
	for (c = 0; c < 3; c++)
	{
		whole->place.center[c] = constants[c];
		whole->place.origin_offset[c] = constants[3 + c];
		whole->place.bounding_offset[c] = constants[6 + c];
	}
	whole->place.bounding_radius = constants[9];
	whole->standing = 0;
	whole->all = plan->whole && count > 1 ? (count >= 64 ? ~0ull : (1ull << count) - 1) : 0;
	/* a plan for one call is made over by the next: what it would owe
	couldn't be made from it later */
	if (whole == &psp_box_spare)
	{
		psp_box_spares++;
		whole->all = 0;
		return whole;
	}
	for (probe = 0; probe < 8; probe++, k = (k + 1) & (PSP_BOX_SLOTS - 1))
		if (!psp_box_slots[k].model)
		{
			psp_box_slots[k].model = model;
			psp_box_slots[k].plan = (short)psp_box_plan_count++;
			return whole;
		}
	/* eight slots taken in a row: the plan serves this call only */
	psp_box_spare = *whole;
	psp_box_spare.all = 0;
	psp_box_spares++;
	return &psp_box_spare;
}

/* one object by reference (means A): its position, forward and up where the
object holds them, its parent's node matrix (13 floats) or NULL; the sphere
straight into the object's centre and radius (4 floats). Nothing is copied:
`plan` carries what the definition fixes. Means B: only `want`'s nodes (and
their parents, and the root) are made; returns the nodes made */
unsigned long long psp_box_posing_pose(const void *plan, unsigned int flags, const float *position, const float *forward,
	const float *up, float scale, const float *parent, const float *orientations, float *nodes, float *sphere,
	unsigned long long want)
{
	const struct psp_box_plan *whole = (const struct psp_box_plan *)plan;
	struct box_posing_ref ref;

	if (psp_box_claim < 0)
		psp_box_claim = !psp_keep("boxposeclaimoff");
	ref.flags = flags;
	ref.position = position;
	ref.forward = forward;
	ref.up = up;
	ref.parent = parent;
	ref.scale = scale;
	ref.place = &whole->place;
	psp_box_poses++;
	return box_posing_pose_ref(&whole->plan, &ref, (const box_orientation *)orientations, (box_matrix *)nodes, sphere,
		psp_box_claim, want);
}

/* means B's late make of an object posed from `plan` (box_posing_make) */
unsigned long long psp_box_posing_make(const void *plan, const float *orientations, float *nodes,
	unsigned long long made, unsigned long long want)
{
	return box_posing_make(&((const struct psp_box_plan *)plan)->plan, (const box_orientation *)orientations,
		(box_matrix *)nodes, made, want);
}

/* the node and the nodes under it, in `plan`'s model */
unsigned long long psp_box_posing_below(const void *plan, int node)
{
	return box_posing_below(&((const struct psp_box_plan *)plan)->plan, node);
}

/* every node of the plan's model, or 0 when it may owe none */
unsigned long long psp_box_posing_all(const void *plan)
{
	return ((const struct psp_box_plan *)plan)->all;
}

/* the definition's standing readers' nodes, kept with its plan for the map
(0: not named yet) */
unsigned long long *psp_box_posing_standing(const void *plan)
{
	return &((struct psp_box_plan *)plan)->standing;
}
