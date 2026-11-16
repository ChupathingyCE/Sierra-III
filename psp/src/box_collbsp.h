/*
The collision service's BSP half (docs/box_collision.md §6b): the sphere walk
and the ray against a collision BSP, the level's or a model's, as the port's
own code against its own copy of the BSP. No engine header comes in, so the
box builds into the harness (bench/halo_box) as it does into the game.

v0 restates the engine's walks as the port ships them (a trusted map: no
node or depth limits on the 3D walk, the surface index checked, the 2D point
test's own limits kept), over the BSP made once (box_collbsp_make): each 3D
node with its plane inline, so a step reads one record where the tags make
it read two arrays. The answers are the engine's to the bit: the same lists
in the same order, the same hit.
*/
#ifndef BOX_COLLBSP_H
#define BOX_COLLBSP_H

#define BOX_COLLBSP_LIST 256 /* the engine's result lists hold 256 */

/* a 3D node with its plane: 32 bytes, two to a cache line */
struct box_collbsp_node
{
	float n[3], d;
	int child[2];
	int designator;
	int pad;
};

/* the tags' records as the port packs them (psp_fixups.py, build 70) */
struct box_collbsp_leaf { unsigned short flags; short reference_count; int first_reference; };
struct box_collbsp_reference { short designator, root; };
struct box_collbsp_node2d { float n[2], d; int child[2]; };
struct box_collbsp_surface
{
	short designator;
	unsigned short first_edge;
	unsigned char flags, breakable;
	short material;
};
struct box_collbsp_edge { unsigned short vertex[2], edge[2], surface[2]; };
struct box_collbsp_vertex { float p[3]; int first_edge; };

struct box_collbsp
{
	struct box_collbsp_node *nodes; /* made by box_collbsp_make, the box's own */
	const float *planes;            /* four floats each */
	const struct box_collbsp_leaf *leaves;
	const struct box_collbsp_reference *references;
	const struct box_collbsp_node2d *nodes2d;
	const struct box_collbsp_surface *surfaces;
	const struct box_collbsp_edge *edges;
	const struct box_collbsp_vertex *vertices;
	int node_count, plane_count, leaf_count, reference_count, node2d_count, surface_count, edge_count, vertex_count;
	/* form A (box_collision.md §6b): each surface's bounding sphere (centre,
	radius, made by box_collbsp_make_bounds), so a sphere that can't reach a
	surface skips its edge ring, and a surface tested once a walk isn't tested
	again. 0: v0, the old walk restated */
	int form;
#define BOX_COLLBSP_FORM_WALK 8 /* diagnostic: the 3D walk alone */
#define BOX_COLLBSP_FORM_REFS 9 /* diagnostic: and the references, no surface tested */
	float *bounds;
};

/* the 3D nodes made from the tags' nodes (plane designator, two children:
12 bytes each) and planes; 0 if there's no room. The other arrays are read
where they lie */
int box_collbsp_make(struct box_collbsp *bsp, const void *tag_nodes, int node_count);
/* form A's surface bounds; 0 if there's no room */
int box_collbsp_make_bounds(struct box_collbsp *bsp);
void box_collbsp_free(struct box_collbsp *bsp);

struct box_collbsp_sphere_result
{
	int surface_count, surfaces[BOX_COLLBSP_LIST];
	int edge_count, edges[BOX_COLLBSP_LIST];
	int vertex_count, vertices[BOX_COLLBSP_LIST];
	int leaf_count, leaves[BOX_COLLBSP_LIST];
};

/* the sphere: surfaces, edges and vertices it touches, leaves it is in.
`any`: stop at the first surface or edge (the port's early-out walk, whose
callers read only whether it found one, and the leaves when it didn't).
Returns whether it found a surface or an edge */
int box_collbsp_sphere(const struct box_collbsp *bsp, short breakable_count, const unsigned int *breakable,
	const float center[3], float radius, int any, struct box_collbsp_sphere_result *result);

struct box_collbsp_ray_result
{
	float t;
	int plane;      /* the hit's plane, by index; -1 none */
	int surface, designator;
	unsigned char flags, breakable;
	short material;
	int leaf_count, leaves[BOX_COLLBSP_LIST];
};

/* the ray from point along vector, up to maximum_t, from the root (or from
start_node at start_depth, as the engine's batch asks: the same answer) */
int box_collbsp_ray(const struct box_collbsp *bsp, unsigned int flags, short breakable_count,
	const unsigned int *breakable, const float point[3], const float vector[3], float maximum_t, int start_node,
	short start_depth, struct box_collbsp_ray_result *result);

/* B (box_collision.md §6b): a sphere answered over a local copy of the BSP
around an earlier, wider sphere W when it lies inside W; a block made with
W = the query widened by `margin` when none holds it. The same answer as
box_collbsp_sphere, bit for bit */
#define BOX_COLLBSP_BLOCKS 64
#define BOX_COLLBSP_RETIRED 1024
#define BOX_COLLBSP_RAY_MAKE 0.0f /* a ray's half length under which it makes a block: none (rays that made blocks pushed out the spheres': b30's sphere reuse 84% -> 58%) */
struct box_collbsp_block;
struct box_collbsp_cache
{
	struct box_collbsp_block *slot[BOX_COLLBSP_BLOCKS];
	float margin;
	unsigned int clock;
	unsigned int made, reused, refused, made_nodes, made_surfaces, ray_reused, ray_full;
	/* the bench's, which runs one tick's calls several times: the state
	saved at the tick's start, put back before each run, and the blocks let
	go since, kept while the saved state holds them */
	int saved_valid, retired_count;
	struct box_collbsp_block *saved[BOX_COLLBSP_BLOCKS];
	unsigned int saved_used[BOX_COLLBSP_BLOCKS], saved_counts[8];
	struct box_collbsp_block *retired[BOX_COLLBSP_RETIRED];
};
int box_collbsp_cache_init(struct box_collbsp_cache *cache, float margin);
void box_collbsp_cache_free(struct box_collbsp_cache *cache);
int box_collbsp_ray_b(struct box_collbsp_cache *cache, const struct box_collbsp *bsp, unsigned int flags,
	short breakable_count, const unsigned int *breakable, const float point[3], const float vector[3], float maximum_t,
	int start_node, short start_depth, struct box_collbsp_ray_result *result);
/* ---------- the descent kept (box_collision.md §6b, "B for the descent"):
each asker's (object and call site) last one-child descent from the root, a
node's plane and children inline, replayed for its next query with the same
tests on the same floats until it straddles or turns, then walked on: the
same start node the root's walk would reach, so the same answer */
#define BOX_COLLBSP_MEMOS 256
#define BOX_COLLBSP_MEMO_STEPS 16
struct box_collbsp_memo_step { float n[3], d; int child[2]; int node; };
struct box_collbsp_memo
{
	unsigned int key;
	const struct box_collbsp *bsp;
	int length;
	struct box_collbsp_memo_step step[BOX_COLLBSP_MEMO_STEPS]; /* the header and the first steps share a line */
};
struct box_collbsp_memos
{
	struct box_collbsp_memo memo[BOX_COLLBSP_MEMOS];
	unsigned int replayed, walked; /* steps taken from the memos, and from the tree */
};
/* asker: a key for who asks (the bench: the object updating and the call site) */
int box_collbsp_sphere_m(struct box_collbsp_memos *memos, unsigned int asker, const struct box_collbsp *bsp,
	short breakable_count, const unsigned int *breakable, const float center[3], float radius, int any,
	struct box_collbsp_sphere_result *result);
int box_collbsp_ray_m(struct box_collbsp_memos *memos, unsigned int asker, const struct box_collbsp *bsp, unsigned int flags,
	short breakable_count, const unsigned int *breakable, const float point[3], const float vector[3], float maximum_t,
	int start_node, short start_depth, struct box_collbsp_ray_result *result);
/* the bench's: save the state as the tick's start, or put it back */
void box_collbsp_cache_save(struct box_collbsp_cache *cache);
void box_collbsp_cache_restore(struct box_collbsp_cache *cache);
int box_collbsp_sphere_b(struct box_collbsp_cache *cache, const struct box_collbsp *bsp, short breakable_count,
	const unsigned int *breakable, const float center[3], float radius, int any, struct box_collbsp_sphere_result *result);

#endif
