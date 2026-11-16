/*
The collision service's first slice: the biped pill sweep (docs/box_collision.md
§9). A biped's move as a pill through the world: the features in a sphere
around the move (the level's surfaces, edges and vertices, and the objects
there: a biped's pill, a model's node BSPs), then the move clipped against
them until it stops. The port's own code against its own tables; no engine
header comes in, so the box builds into the harness (bench/halo_box) as it
does into the game.

v0 restates the engine's sweep (collision_move_pill and what it calls) in the
engine's float order, so its answer is the engine's to the bit. The world it
reads is a table set (struct box_sweep_world): the level's BSP and breakable
flags, each leaf's cluster, each cluster's collideable objects in the order
the clusters keep them, and each object's state. The capture fills them from
the sweep's own walk; in the game the world store will.
*/
#ifndef BOX_SWEEP_H
#define BOX_SWEEP_H

#include "box_collbsp.h"

#define BOX_SWEEP_FEATURES 256 /* the engine's lists hold 256 of each kind */
#define BOX_SWEEP_PRISM_POINTS 8

/* the engine's collision flags and object fields the sweep reads, as numbers
(the cut-in checks them against the engine's at compile time) */
enum
{
	BOX_SWEEP_TEST_STRUCTURE = 5,
	BOX_SWEEP_TEST_MEDIA = 6,
	BOX_SWEEP_TEST_OBJECTS = 7,
	BOX_SWEEP_TEST_FIRST_TYPE = 8,
	BOX_SWEEP_TEST_LAST_TYPE = 19,
	BOX_SWEEP_TEST_SKIP_PASSTHROUGH_BIPEDS = 21,
	BOX_SWEEP_TEST_USE_VEHICLE_PHYSICS = 22,
	BOX_SWEEP_TEST_SKIP_PLAYER_BIPEDS = 23,
	BOX_SWEEP_OBJECT_INVISIBLE = 0,     /* object flags */
	BOX_SWEEP_OBJECT_NO_COLLISIONS = 24,
	BOX_SWEEP_OBJECT_DEAD = 2,          /* damage flags */
	BOX_SWEEP_BIPED_PASSES_THROUGH = 4, /* biped flags */
	BOX_SWEEP_TYPE_BIPED = 0,
	BOX_SWEEP_TYPE_VEHICLE = 1,
	BOX_SWEEP_TYPE_SCENERY = 6,
	BOX_SWEEP_TYPE_MACHINE = 7,
	BOX_SWEEP_TYPE_CONTROL = 8,
};

/* a collision as the engine lays it out (struct collision_plane, 44 bytes) */
struct box_sweep_plane
{
	float t, point[3], n[3], d;
	int object, surface;
	unsigned char flags, breakable;
	short material;
};

/* the features (the engine's collision_sphere, _cylinder, _prism) */
struct box_sweep_feature { int object, surface; unsigned char flags, breakable; short material; };
struct box_sweep_sphere { struct box_sweep_feature f; float center[3], radius; };
struct box_sweep_cylinder { struct box_sweep_feature f; float base[3], height[3], width; };
struct box_sweep_prism
{
	struct box_sweep_feature f;
	float n[3], d, height;
	short axis;
	unsigned char sign;
	int point_count;
	float points[BOX_SWEEP_PRISM_POINTS][2];
};
struct box_sweep_features
{
	int count[3];
	struct box_sweep_sphere spheres[BOX_SWEEP_FEATURES];
	struct box_sweep_cylinder cylinders[BOX_SWEEP_FEATURES];
	struct box_sweep_prism prisms[BOX_SWEEP_FEATURES];
};

/* the world the sweep reads */
enum
{
	BOX_SWEEP_SEEN_NONE = 0, /* the walk never needed what kind of object it is */
	BOX_SWEEP_SEEN_BIPED,
	BOX_SWEEP_SEEN_MODEL,
	BOX_SWEEP_SEEN_NO_MODEL,
	BOX_SWEEP_SEEN_PHYSICS,  /* a vehicle walked by its physics: not in v0 */
};
struct box_sweep_object
{
	int index;
	unsigned int flags, damage_flags;
	int type;
	float center[3], radius;
	int parent, next, first_child;
	int seen;
	/* a biped's */
	unsigned int biped_flags;
	int player, seat;
	float pill_base[3], pill_height, pill_width;
	/* a model's: its nodes that reach a BSP, in the world's node table */
	int first_node, node_count;
};
struct box_sweep_node { const struct box_collbsp *bsp; float m[13]; /* scale, forward, left, up, position */ };
struct box_sweep_leaf { int leaf, cluster; };
struct box_sweep_cluster { int cluster, first, count; };      /* its objects in `members` */
struct box_sweep_member { int object; float center[3], radius; }; /* the cluster walk's sphere copy */

struct box_sweep_world
{
	const struct box_collbsp *level;
	short breakable_count;
	const unsigned int *breakable;
	int any;          /* the sphere walk's early-out switch, as the engine's global stands */
	/* diagnostic parts (the bench's price of a sweep's pieces; their answers
	are incomplete by design): 1 the features only, no clip; 2 the level's
	features only, no objects, no clip */
	int part;
	/* B (stage 3): the margin each asker's level walk is widened by and kept
	for its next sweeps; 0 none */
	float kept_margin;
	/* B packed: each kept walk's 3D nodes also copied into a block of its
	asker's (box_collbsp_pack); 0 B's unpacked walk */
	int kept_packed;
	int skip_objects; /* the engine's debug switch */
	const struct box_sweep_leaf *leaves;
	int leaf_count;
	const struct box_sweep_cluster *clusters;
	int cluster_count;
	const struct box_sweep_member *members;
	const struct box_sweep_object *objects;
	int object_count;
	const struct box_sweep_node *nodes;
};

struct box_sweep_query
{
	unsigned int flags;
	float position[3], velocity[3], height, radius;
	int ignore;
	int most; /* the most collisions the asker holds */
};

struct box_sweep_answer
{
	int count;
	float position[3], velocity[3];
	struct box_sweep_plane planes[BOX_SWEEP_FEATURES];
};

/* the sweep's scratch (its feature list and the walks' marks): the asker's
own, so a sweep writes nothing shared */
#ifndef BOX_SWEEP_KEPT
#define BOX_SWEEP_KEPT 32
#endif
#define BOX_SWEEP_BREAKABLE_WORDS 8 /* the engine's 256 breakable surfaces */
struct box_sweep_kept { int asker; float center[3], radius; struct box_collbsp_sphere_result walk; };

/* B packed's blocks: 14 of ~2.3 KB, 32 KB in all (the lead 10:30's cap
against the 124 KB heap floor). A block is given to the first asker that
keeps a walk while one is free and stays with it while that asker's walk is
kept (not LRU: a pool smaller than its askers would miss every time); an
asker without one, or whose subtree doesn't fit, walks unpacked as B */
#ifndef BOX_SWEEP_PACKED_BLOCKS
#define BOX_SWEEP_PACKED_BLOCKS 14
#endif
#define BOX_SWEEP_PACKED_NODES 100 /* b30's census: 55 a keep, 183 the most */
#define BOX_SWEEP_PACKED_LEAVES 64
struct box_sweep_packed
{
	int owner;      /* the asker, or -1 free */
	int node_count; /* 0: its walk didn't fit; the asker walks unpacked */
	int leaf_count;
	struct box_collbsp_packed_node nodes[BOX_SWEEP_PACKED_NODES];
	int leaves[BOX_SWEEP_PACKED_LEAVES];
};

struct box_sweep_scratch
{
	/* B: each asker's widened walk (BOX_SWEEP_KEPT askers, the oldest given up) */
	struct box_sweep_kept kept[BOX_SWEEP_KEPT];
	unsigned int kept_clock, kept_used[BOX_SWEEP_KEPT];
	unsigned int kept_hits, kept_walks, kept_drops;
	struct box_sweep_packed packed[BOX_SWEEP_PACKED_BLOCKS];
	unsigned int packed_ready; /* the blocks' owners set free once */
	unsigned int packed_made, packed_too_big, packed_no_block, packed_used;
	const struct box_sweep_kept *kept_last; /* the walk the last level sphere came from (the census) */
	/* the level the kept walks were made against: a BSP switch or a
	breakable surface broken drops every one */
	const struct box_collbsp *kept_level;
	short kept_breakable_count;
	unsigned int kept_breakable[BOX_SWEEP_BREAKABLE_WORDS];
	int asker;
	struct box_sweep_features features;
	struct box_collbsp_sphere_result sphere;
	int marked_clusters[BOX_COLLBSP_LIST], marked_cluster_count;
	int marked_objects[512], marked_object_count;
};

/* the sweep; returns its collision count, or -1 when the world doesn't hold
what the walk needs (an object or a cluster the capture didn't meet, a
vehicle's physics) */
int box_sweep(const struct box_sweep_world *world, const struct box_sweep_query *query,
	struct box_sweep_scratch *scratch, struct box_sweep_answer *answer);

#endif
