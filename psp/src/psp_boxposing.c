/*
The posing box cut into the game (PSP_BOXPOSING=1; docs/box_posing.md, "The
cut-in"). The engine's side (psp_fixups.py, FIXES_BOXPOSING) gathers an
object's place and orientations as the capture does and calls here; this side
keeps each model's plan for the map and runs box_posing_pose. It knows the box
and no engine header: models are only keys, their links come in as numbers.

A map's plans: up to PSP_BOX_PLANS, found through PSP_BOX_SLOTS slots by the
model's address. POSEK measured 32-63 posed models a map on c20, b30 and a30
(the lab's posek1). A model past the room still poses through the box, from a
plan made for that one call, and the log counts it: the build's path stays
whole.
*/
#include <string.h>
#include "box_posing.h"

void platform_psp_log(const char *format, ...);

#define PSP_BOX_PLANS 96
#define PSP_BOX_SLOTS 256

struct psp_box_slot
{
	const void *model;
	short plan; /* an index into psp_box_plans */
};

static struct psp_box_slot psp_box_slots[PSP_BOX_SLOTS];
static struct box_posing_plan psp_box_plans[PSP_BOX_PLANS];
static struct box_posing_plan psp_box_spare; /* a plan made for one call, when there's no room */
static int psp_box_plan_count, psp_box_spares, psp_box_poses;

/* at each map's start: the plans name the last map's models */
void psp_box_posing_forget(void)
{
	if (psp_box_plan_count)
		platform_psp_log("boxposing: %d plans of %d, %d poses, %d poses past the room",
			psp_box_plan_count, PSP_BOX_PLANS, psp_box_poses, psp_box_spares);
	memset(psp_box_slots, 0, sizeof(psp_box_slots));
	psp_box_plan_count = psp_box_spares = psp_box_poses = 0;
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
node, as the tag has them), kept for the map when there's room */
const void *psp_box_posing_plan_add(const void *model, const short *links, int count)
{
	struct box_posing_node nodes[BOX_POSING_NODES];
	struct box_posing_plan *plan = psp_box_plan_count < PSP_BOX_PLANS ? &psp_box_plans[psp_box_plan_count] : &psp_box_spare;
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
	box_posing_plan_make(plan, nodes, count);
	if (plan == &psp_box_spare)
	{
		psp_box_spares++;
		return plan;
	}
	for (probe = 0; probe < 8; probe++, k = (k + 1) & (PSP_BOX_SLOTS - 1))
		if (!psp_box_slots[k].model)
		{
			psp_box_slots[k].model = model;
			psp_box_slots[k].plan = (short)psp_box_plan_count++;
			return plan;
		}
	/* eight slots taken in a row: the plan serves this call only */
	psp_box_spare = *plan;
	psp_box_spares++;
	return &psp_box_spare;
}

/* one object: `place` as the capture records it (position, forward, up, the
centre of mass negated, origin offset, bounding offset, bounding radius,
scale: 20 floats), `parent` the parent's node matrix (13 floats) or NULL;
`plan` NULL for an object placed with no model. Writes the node matrices by
node index and the bounding sphere */
void psp_box_posing_pose(const void *plan, unsigned int flags, const float *place, const float *parent,
	const float *orientations, float *nodes, float *sphere)
{
	static const struct box_posing_plan none; /* nodes 0: the root from the place alone */
	struct box_posing_object object;

	object.flags = flags;
	memcpy(object.position, place, sizeof(object.position));
	memcpy(object.forward, place + 3, sizeof(object.forward));
	memcpy(object.up, place + 6, sizeof(object.up));
	memcpy(object.center, place + 9, sizeof(object.center));
	memcpy(object.origin_offset, place + 12, sizeof(object.origin_offset));
	memcpy(object.bounding_offset, place + 15, sizeof(object.bounding_offset));
	object.bounding_radius = place[18];
	object.scale = place[19];
	if (parent)
		memcpy(object.parent, parent, sizeof(object.parent));
	psp_box_poses++;
	box_posing_pose(plan ? (const struct box_posing_plan *)plan : &none, &object,
		(const box_orientation *)orientations, (box_matrix *)nodes, sphere);
}
