/* The posing box's adapter (docs/box_posing.md, "The capture"). The only code
that knows both the box's own data (psp/src/box_posing.h) and the capture's
records:

  static record, key = the model's key:
      u16 node count, u16 0, then per node of the model tag's table
      { s16 parent, s16 first child, s16 next sibling, s16 0 }
  entity (kind = the object's type), input:
      u32 model key, u32 flags (BOX_POSE_*), f32 position[3] forward[3] up[3]
      centre[3] origin offset[3] bounding offset[3], bounding radius, scale,
      parent node matrix[13], u32 node count, then node count orientations
      (i, j, k, w, translation[3], scale: 32 bytes each, by node index)
  entity, output (the old code's): node count matrices (52 bytes each, by
      node index), then the bounding sphere's centre[3] and radius
*/
#include <stdlib.h>
#include <string.h>
#include "../box.h"
#include "box_posing.h"

#define INPUT_FIXED (2 * 4 + 18 * 4 + 2 * 4 + 13 * 4 + 4)

static const char *const kinds[] = { "biped", "vehicle", "weapon", "equipment", "garbage", "projectile", "scenery",
	"machine", "control", "light_fixture", "placeholder", "sound_scenery", 0 };

static struct { unsigned int key, nodes; struct box_posing_plan plan; } *plans;
static unsigned int plan_count;

/* this tick's entities, with what the box reads made ready (not timed) */
struct pose_job
{
	const struct box_posing_plan *plan;
	struct box_posing_object object;
	const box_orientation *orientations;
	box_matrix *nodes;
	float *sphere;
	unsigned int output_length;
};
static struct pose_job *jobs;
static unsigned int job_room;
static float *outputs;
static unsigned int output_room;
static const struct box_posing_plan empty_plan;

static int posing_setup(const struct box_static *statics, unsigned int count)
{
	unsigned int k;

	free(plans);
	plans = calloc(count ? count : 1, sizeof(*plans));
	if (!plans)
		return -1;
	plan_count = 0;
	for (k = 0; k < count; k++)
	{
		struct box_posing_node table[BOX_POSING_NODES];
		const unsigned char *b = statics[k].bytes;
		unsigned int nodes, n;

		if (statics[k].length < 4)
			return -1;
		nodes = b[0] | b[1] << 8;
		if (statics[k].length < 4 + 8 * nodes)
			return -1;
		for (n = 0; n < nodes && n < BOX_POSING_NODES; n++)
		{
			const unsigned char *r = b + 4 + 8 * n;

			table[n].parent = (short)(r[0] | r[1] << 8);
			table[n].first_child = (short)(r[2] | r[3] << 8);
			table[n].next_sibling = (short)(r[4] | r[5] << 8);
		}
		plans[plan_count].key = statics[k].key;
		plans[plan_count].nodes = nodes;
		box_posing_plan_make(&plans[plan_count].plan, table, (int)(nodes < BOX_POSING_NODES ? nodes : BOX_POSING_NODES));
		plan_count++;
	}
	return (int)(plan_count * sizeof(*plans));
}

/* a model's plan, if the capture's node count is the model's (the plan's
indices are the model's nodes, which the orientations must hold) */
static const struct box_posing_plan *plan_of(unsigned int key, unsigned int nodes)
{
	unsigned int k;

	for (k = 0; k < plan_count; k++)
		if (plans[k].key == key)
			return plans[k].nodes == nodes ? &plans[k].plan : 0;
	return nodes == 0 ? &empty_plan : 0;
}

static void posing_tick(const struct box_tick *tick)
{
	unsigned int k, total = 0, at = 0;

	if (tick->entities > job_room)
	{
		free(jobs);
		job_room = tick->entities * 2;
		jobs = calloc(job_room, sizeof(*jobs));
	}
	for (k = 0; k < tick->entities; k++)
		total += tick->entity[k].output_length / 4 + 4;
	if (total > output_room)
	{
		free(outputs);
		output_room = total * 2;
		outputs = calloc(output_room, sizeof(*outputs));
	}
	for (k = 0; k < tick->entities; k++)
	{
		const struct box_entity *e = &tick->entity[k];
		struct pose_job *j = &jobs[k];
		unsigned int key, nodes;

		memset(j, 0, sizeof(*j));
		j->plan = &empty_plan;
		j->nodes = (box_matrix *)(outputs + at);
		if (e->input_length < INPUT_FIXED)
			continue;
		memcpy(&key, e->input, 4);
		memcpy(&j->object.flags, e->input + 4, 4);
		memcpy(j->object.position, e->input + 8, 18 * 4);
		memcpy(&j->object.bounding_radius, e->input + 8 + 18 * 4, 4);
		memcpy(&j->object.scale, e->input + 12 + 18 * 4, 4);
		memcpy(j->object.parent, e->input + 16 + 18 * 4, 13 * 4);
		memcpy(&nodes, e->input + INPUT_FIXED - 4, 4);
		if (nodes > BOX_POSING_NODES || e->input_length < INPUT_FIXED + 32 * nodes)
			continue;
		j->plan = plan_of(key, nodes);
		if (!j->plan)
		{
			j->plan = &empty_plan;
			continue;
		}
		/* the capture's records are 4-byte aligned, which is all the FPU needs */
		j->orientations = (const box_orientation *)(e->input + INPUT_FIXED);
		j->sphere = outputs + at + 13 * (nodes ? nodes : 1);
		j->output_length = 52 * (nodes ? nodes : 1) + 16;
		at += j->output_length / 4;
	}
}

static __attribute__((noinline)) void posing_run(int i)
{
	struct pose_job *j = &jobs[i];

	if (j->sphere)
		box_posing_pose(j->plan, &j->object, j->orientations, j->nodes, j->sphere);
}

static void posing_output(int i, const void **bytes, unsigned int *length)
{
	*bytes = jobs[i].nodes;
	*length = jobs[i].output_length;
}

/* v0's budget: 2.3 ms a tick on b30's beach (docs/box_posing.md §4) over the
~91 objects a tick b30's flight poses: ~25 us an object. The tolerance: the
old code's product is the VFPU's on the PSP, which differs from the FPU's in
the last bits, and a node's error grows down the chain */
const struct box_adapter box_posing = {
	"posing", kinds, 0, 0, BOX_RELATIVE, 1e-3f, 1e-4f, 2.3f, 25.f,
	posing_setup, posing_tick, posing_run, posing_output, 0,
};
