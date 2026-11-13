/* The posing box: see box_posing.h and docs/box_posing.md. */
#include "box_posing.h"

int box_posing_plan_make(struct box_posing_plan *plan, const struct box_posing_node *nodes, int count)
{
	unsigned char stack[BOX_POSING_NODES];
	int top = 0, filled = 1, k;

	if (count > BOX_POSING_NODES)
		count = BOX_POSING_NODES;
	if (count <= 0)
	{
		plan->nodes = 0;
		plan->whole = 0;
		return 0;
	}
	for (k = 0; k < BOX_POSING_NODES; k++)
		plan->parent[k] = 0;
	stack[0] = 0;
	/* the engine's order exactly: a node's matrix needs its parent's, and the
	walk reaches parents first only because it follows the tag's links */
	while (top < filled)
	{
		int at = stack[top++];
		const struct box_posing_node *node = &nodes[at];

		plan->order[top - 1] = (unsigned char)at;
		if (at != 0)
			plan->parent[at] = (unsigned char)(node->parent >= 0 && node->parent < count ? node->parent : 0);
		if (node->next_sibling >= 0 && node->next_sibling < count && filled < BOX_POSING_NODES)
			stack[filled++] = (unsigned char)node->next_sibling;
		if (node->first_child >= 0 && node->first_child < count && filled < BOX_POSING_NODES)
			stack[filled++] = (unsigned char)node->first_child;
	}
	plan->nodes = (unsigned short)filled;
	{
		/* every node once: only then may the node block's lines be claimed */
		unsigned long long seen = 0;

		plan->whole = filled == count;
		for (k = 0; k < filled && plan->whole; k++)
		{
			if (seen >> plan->order[k] & 1)
				plan->whole = 0;
			seen |= 1ull << plan->order[k];
		}
	}
	return filled;
}

/* result = a x b, the engine's matrix4x3_multiply term for term: b's rows
combine a's, b's position is scaled by a's scale before a's own is added.
`result` may be `b` */
static inline void multiply(const float *a, const float *b, float *result)
{
	float product[12];
	int row, column;

	for (row = 0; row < 4; row++)
		for (column = 0; column < 3; column++)
		{
			float value = b[1 + row * 3 + 0] * a[1 + column] + b[1 + row * 3 + 1] * a[1 + 3 + column] +
				b[1 + row * 3 + 2] * a[1 + 6 + column];

			if (row == 3)
				value = value * a[0] + a[1 + 9 + column];
			product[row * 3 + column] = value;
		}
	result[0] = a[0] * b[0];
	for (row = 0; row < 12; row++)
		result[1 + row] = product[row];
}

/* the engine's matrix4x3_from_orientation (matrix4x3_rotation_from_quaternion,
then the orientation's scale and translation) */
static inline void from_orientation(const float *o, float *m)
{
	float s = o[0] * o[0] + o[1] * o[1] + o[2] * o[2] + o[3] * o[3];
	float x, y, z, wx, wy, wz, xx, xy, xz, yy, yz, zz;

	s = s != 0.f ? 2.f / s : 0.f;
	x = s * o[0];
	y = s * o[1];
	z = s * o[2];
	wx = o[3] * x;
	wy = o[3] * y;
	wz = o[3] * z;
	xx = o[0] * x;
	xy = o[0] * y;
	xz = o[0] * z;
	yy = o[1] * y;
	yz = o[1] * z;
	zz = o[2] * z;
	m[0] = o[7];
	m[1] = 1.f - (yy + zz);
	m[2] = xy - wz;
	m[3] = xz + wy;
	m[4] = xy + wz;
	m[5] = 1.f - (xx + zz);
	m[6] = yz - wx;
	m[7] = xz - wy;
	m[8] = yz + wx;
	m[9] = 1.f - (xx + yy);
	m[10] = o[4];
	m[11] = o[5];
	m[12] = o[6];
}

static inline void translation(const float *p, float *m)
{
	int k;

	for (k = 0; k < 13; k++)
		m[k] = 0.f;
	m[0] = m[1] = m[5] = m[9] = 1.f;
	m[10] = p[0];
	m[11] = p[1];
	m[12] = p[2];
}

static inline void cross(const float *a, const float *b, float *out)
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

/* the root's matrix: the object's translation and rotation (with its
centre of mass and origin offset), under its parent's node if attached */
/* one object's place, by reference: the harness's records and the game's
objects both come in through it */
struct place
{
	unsigned int flags;
	const float *position, *forward, *up, *center, *origin_offset, *bounding_offset, *parent;
	float bounding_radius, scale;
};

static void root(const struct place *object, const float *node, float *out)
{
	float t[13], r[13], c[13];
	int k;

	translation(object->position, t);
	r[0] = 1.f;
	for (k = 0; k < 3; k++)
	{
		r[1 + k] = object->forward[k];
		r[7 + k] = object->up[k];
		r[10 + k] = 0.f;
	}
	cross(object->up, object->forward, r + 4);
	if (object->flags & BOX_POSE_MIRRORED)
		for (k = 4; k < 7; k++)
			r[k] = -r[k];
	if (object->flags & BOX_POSE_PHYSICS)
	{
		translation(object->center, c);
		multiply(r, c, r);
	}
	translation(object->origin_offset, c);
	multiply(r, c, r);
	if (object->flags & BOX_POSE_PARENT)
	{
		float p[13];

		/* the parent's scale and mirror are taken out of a copy: the parent's
		own block isn't ours to change */
		for (k = 0; k < 13; k++)
			p[k] = object->parent[k];
		if (p[0] != 1.f)
		{
			t[10] *= p[0];
			t[11] *= p[0];
			t[12] *= p[0];
			p[0] = 1.f;
		}
		if (object->flags & BOX_POSE_PARENT_MIRRORED)
			for (k = 4; k < 7; k++)
				p[k] = -p[k];
		multiply(p, t, out);
		multiply(out, r, out);
		multiply(out, node, out);
	}
	else
	{
		multiply(t, r, out);
		multiply(out, node, out);
	}
}

/* the node block's lines that lie wholly inside it, made ours without the
fetch a first store would cost (cache 0x18, create dirty exclusive). A
claimed line holds no defined bytes until it is stored to: the walk stores
every byte of a whole plan's block, and reads only nodes it has stored */
static void claim(box_matrix *nodes, int count)
{
#if defined(__psp__) || defined(_PSP) || defined(__mips__)
	unsigned int at = ((unsigned int)nodes + 63u) & ~63u;
	unsigned int end = ((unsigned int)nodes + (unsigned int)count * sizeof(box_matrix)) & ~63u;

	if ((unsigned int)nodes < 0x08800000u || end > 0x0A000000u)
		return;
	for (; at < end; at += 64u)
		__asm__ __volatile__("cache 0x18, 0(%0)" : : "r"(at) : "memory");
#else
	(void)nodes;
	(void)count;
#endif
}

unsigned long long box_posing_make(const struct box_posing_plan *plan, const box_orientation *orientations,
	box_matrix *nodes, unsigned long long made, unsigned long long want)
{
	unsigned long long need = want & ~made;
	int k;

	if (!need)
		return made;
	/* a node's matrix is its parent's times its own: each wanted node takes
	its parents with it, back to one made. The walk's order has every parent
	before its children, so a pass from its end carries each want up */
	for (k = plan->nodes - 1; k > 0; k--)
	{
		int at = plan->order[k];

		if (need >> at & 1)
			need |= 1ull << plan->parent[at];
	}
	need &= ~made;
	/* then in the walk's own order and with its own products, so a node made
	here is the bits the whole walk makes */
	for (k = 1; k < plan->nodes; k++)
	{
		int at = plan->order[k];

		if (need >> at & 1)
		{
			float node[13];

			from_orientation(orientations[at], node);
			multiply(nodes[plan->parent[at]], node, nodes[at]);
		}
	}
	return made | need;
}

unsigned long long box_posing_below(const struct box_posing_plan *plan, int node)
{
	unsigned long long below = 1ull << node;
	int k;

	for (k = 1; k < plan->nodes; k++)
		if (below >> plan->parent[plan->order[k]] & 1)
			below |= 1ull << plan->order[k];
	return below;
}

static unsigned long long pose(const struct box_posing_plan *plan, const struct place *object,
	const box_orientation *orientations, box_matrix *nodes, float sphere[4], int claimed, unsigned long long want)
{
	float *root_matrix = nodes[0];
	unsigned long long made = 1;
	int k;

	/* a line claimed holds nothing defined until it is stored: only a walk
	that stores every node may claim */
	if (claimed && plan->whole && plan->nodes > 1 && !(object->flags & BOX_POSE_NO_MODEL) && !~want)
		claim(nodes, plan->nodes);

	if ((object->flags & BOX_POSE_NO_MODEL) || plan->nodes == 0)
	{
		root_matrix[0] = 1.f;
		for (k = 0; k < 3; k++)
		{
			root_matrix[1 + k] = object->forward[k];
			root_matrix[7 + k] = object->up[k];
			root_matrix[10 + k] = object->position[k];
		}
		cross(object->up, object->forward, root_matrix + 4);
		made = ~0ull;
	}
	else
	{
		float node[13];

		from_orientation(orientations[0], node);
		if (object->flags & BOX_POSE_WORLD_RELATIVE)
			for (k = 0; k < 13; k++)
				root_matrix[k] = node[k];
		else
			root(object, node, root_matrix);
		/* the walk: each node from its orientation, under its parent, which
		the plan's order has made already */
		if (!~want)
		{
			for (k = 1; k < plan->nodes; k++)
			{
				int at = plan->order[k];

				from_orientation(orientations[at], node);
				multiply(nodes[plan->parent[at]], node, nodes[at]);
			}
			made = ~0ull;
		}
		else
			made = box_posing_make(plan, orientations, nodes, 1, want);
	}
	{
		/* the engine's matrix4x3_transform_point of the bounding offset by the root */
		float x = object->bounding_offset[0], y = object->bounding_offset[1], z = object->bounding_offset[2];

		if (root_matrix[0] != 1.f)
		{
			x *= root_matrix[0];
			y *= root_matrix[0];
			z *= root_matrix[0];
		}
		for (k = 0; k < 3; k++)
			sphere[k] = root_matrix[7 + k] * z + root_matrix[4 + k] * y + root_matrix[1 + k] * x + root_matrix[10 + k];
		sphere[3] = object->scale > 0.f ? object->bounding_radius * object->scale : object->bounding_radius;
	}
	return made;
}

void box_posing_pose(const struct box_posing_plan *plan, const struct box_posing_object *object,
	const box_orientation *orientations, box_matrix *nodes, float sphere[4])
{
	struct place place;

	place.flags = object->flags;
	place.position = object->position;
	place.forward = object->forward;
	place.up = object->up;
	place.center = object->center;
	place.origin_offset = object->origin_offset;
	place.bounding_offset = object->bounding_offset;
	place.bounding_radius = object->bounding_radius;
	place.scale = object->scale;
	place.parent = object->parent;
	pose(plan, &place, orientations, nodes, sphere, 0, ~0ull);
}

unsigned long long box_posing_pose_ref(const struct box_posing_plan *plan, const struct box_posing_ref *ref,
	const box_orientation *orientations, box_matrix *nodes, float sphere[4], int claim, unsigned long long want)
{
	struct place place;

	place.flags = (ref->flags & ~(unsigned int)BOX_POSE_PHYSICS) | (ref->place->flags & BOX_POSE_PHYSICS);
	place.position = ref->position;
	place.forward = ref->forward;
	place.up = ref->up;
	place.center = ref->place->center;
	place.origin_offset = ref->place->origin_offset;
	place.bounding_offset = ref->place->bounding_offset;
	place.bounding_radius = ref->place->bounding_radius;
	place.scale = ref->scale;
	place.parent = ref->parent;
	return pose(plan, &place, orientations, nodes, sphere, claim, want);
}
