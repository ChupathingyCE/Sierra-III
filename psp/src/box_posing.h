/*
The posing box (docs/box_posing.md): node orientations in, node matrices out,
walked from a per-model plan in place of the model tag's links. The port's own
code against its own data: no engine header comes in, so the box builds into
the harness (bench/halo_box) as it does into the game.

v0 is the walk: the root's matrix from the object's place and its parent's
node, then each node's matrix from its orientation and its parent's matrix.
The orientations it takes are the engine's after the decode, the overlays,
the object's scale, the type's preprocess and the interpolation (docs/
box_posing.md §1, "v0's cut").
*/
#ifndef BOX_POSING_H
#define BOX_POSING_H

#define BOX_POSING_NODES 64 /* the engine's MAXIMUM_NODES_PER_MODEL */

/* a real_matrix4x3 as the engine lays it out: scale, then forward, left, up
and position (13 floats, 52 bytes) */
typedef float box_matrix[13];
/* a real_orientation: the rotation's i, j, k, w, the translation, the scale */
typedef float box_orientation[8];

/* a model's walk, made once a map from its node table */
struct box_posing_plan
{
	unsigned short nodes;               /* in the walk (the root first) */
	unsigned char order[BOX_POSING_NODES];  /* node indices in the walk's order */
	unsigned char parent[BOX_POSING_NODES]; /* each node's parent, by node index (the root's is 0) */
};

/* one node of the model tag's table: as the engine reads it */
struct box_posing_node
{
	short parent, first_child, next_sibling;
};

/* the plan the engine's walk takes: its node stack, a node's next sibling
pushed before its first child, links past the model's count or past 64 left
out, a parent the model doesn't have read as the root. Returns the nodes in
the walk */
int box_posing_plan_make(struct box_posing_plan *plan, const struct box_posing_node *nodes, int count);

enum
{
	BOX_POSE_WORLD_RELATIVE = 1, /* the animation is world relative: the root is its orientation alone */
	BOX_POSE_MIRRORED = 2,
	BOX_POSE_PARENT = 4,         /* attached: `parent` is the parent object's node matrix */
	BOX_POSE_PARENT_MIRRORED = 8,
	BOX_POSE_PHYSICS = 16,       /* `center` is the physics' centre of mass, negated */
	BOX_POSE_NO_MODEL = 32       /* placed with no model: the root from forward, up and position alone */
};

/* one object's place, as the engine's walk reads it */
struct box_posing_object
{
	unsigned int flags;
	float position[3], forward[3], up[3];
	float center[3], origin_offset[3];
	float bounding_offset[3], bounding_radius, scale;
	box_matrix parent;
};

/* poses one object: `nodes` (52 bytes each, by node index) and its bounding
sphere (centre, radius) from its place and its orientations (by node index) */
void box_posing_pose(const struct box_posing_plan *plan, const struct box_posing_object *object,
	const box_orientation *orientations, box_matrix *nodes, float sphere[4]);

#endif
