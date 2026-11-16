/* The collision service's BSP half: its adapter (docs/box_collision.md §6b).
The only code that knows both the box's own data (psp/src/box_collbsp.h) and
the capture's records (psp_fixups.py, FIXES_CAPTURE_COLLBSP):

  static record, key = the BSP's address: u32 8, then for each of its arrays
      (3D nodes, planes, leaves, 2D references, 2D nodes, surfaces, edges,
      vertices) u32 count, u32 element size, u32 key (the array's address)
  static record, key = an array's address: its elements as the port packs them
  entity (kind 1 sphere, 2 ray, 3 ray from a node; flags 1 the level's BSP),
      input: u32 BSP key, s32 breakable count, u32 asker, u32 flag bytes,
      u32 early-out (spheres) in bit 0 and the object updating's absolute
      index + 1 above it (0 unknown), then the arguments (sphere: centre[3], radius;
      ray: u32 flags, point[3], vector[3], maximum t, and from a node u32
      start node, u32 start depth), then the breakable flags' bytes
  output, sphere: u32 hit, surfaces, edges, vertices, leaves (counts), then the
      four lists by index
  output, ray: u32 hit, f32 t, u32 plane index (0xFFFFFFFF none), surface,
      designator, flags | breakable << 8, material, leaf count (a miss's hit
      fields 0xFFFFFFFF), then the leaves
*/
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "../box.h"
#include "box_collbsp.h"

static const char *const kinds[] = { "none", "sphere", "ray", "ray from a node", 0 };

static const unsigned int element_sizes[8] = { 12, 16, 8, 4, 20, 8, 12, 16 };

static struct bsp_entry { unsigned int key; struct box_collbsp bsp; } *bsps;
static unsigned int bsp_count;

struct query
{
	const struct box_collbsp *bsp;
	unsigned int kind;
	short breakable_count;
	const unsigned int *breakable;
	unsigned int breakable_copy[64]; /* the flags, 4-byte aligned as the walk reads them */
	int any;
	unsigned int asker;
	unsigned int key;
	int object; /* the top-level object updating, -1 unknown (captures before 2026-11-12 21:4x) */
	float args[10];
	unsigned int output_length;
	unsigned int *out;
};
static struct query *queries;
static unsigned int query_room;
static unsigned int *outputs;
static unsigned int output_room;
static struct box_collbsp_sphere_result sphere_answer;
static struct box_collbsp_ray_result ray_answer;

static const struct box_static *find(const struct box_static *statics, unsigned int count, unsigned int key)
{
	unsigned int k;

	for (k = 0; k < count; k++)
		if (statics[k].key == key)
			return &statics[k];
	return 0;
}

static int form;
/* form B: blocks of the BSP kept around earlier queries (box_collision.md §6b) */
static int form_b, form_m;
static struct box_collbsp_memos memos, memos_saved;
static struct box_collbsp_cache cache;

static int collbsp_setup(const struct box_static *statics, unsigned int count)
{
	unsigned int k, owned = 0;

	for (k = 0; k < bsp_count; k++)
		box_collbsp_free(&bsps[k].bsp);
	free(bsps);
	bsps = calloc(count ? count : 1, sizeof(*bsps));
	if (!bsps)
		return -1;
	bsp_count = 0;
	for (k = 0; k < count; k++)
	{
		unsigned int table[25], a;
		const void *arrays[8];
		int counts[8];
		struct box_collbsp *b;

		if (statics[k].length != sizeof(table))
			continue;
		memcpy(table, statics[k].bytes, sizeof(table));
		if (table[0] != 8)
			continue;
		for (a = 0; a < 8; a++)
		{
			const struct box_static *array = table[1 + 3 * a] ? find(statics, count, table[3 + 3 * a]) : 0;

			counts[a] = (int)table[1 + 3 * a];
			if (table[2 + 3 * a] != element_sizes[a])
				return -1;
			if (counts[a] && (!array || array->length < counts[a] * element_sizes[a]))
				return -1;
			arrays[a] = array ? array->bytes : 0;
		}
		b = &bsps[bsp_count].bsp;
		bsps[bsp_count].key = statics[k].key;
		b->planes = (const float *)arrays[1];
		b->leaves = (const struct box_collbsp_leaf *)arrays[2];
		b->references = (const struct box_collbsp_reference *)arrays[3];
		b->nodes2d = (const struct box_collbsp_node2d *)arrays[4];
		b->surfaces = (const struct box_collbsp_surface *)arrays[5];
		b->edges = (const struct box_collbsp_edge *)arrays[6];
		b->vertices = (const struct box_collbsp_vertex *)arrays[7];
		b->plane_count = counts[1];
		b->leaf_count = counts[2];
		b->reference_count = counts[3];
		b->node2d_count = counts[4];
		b->surface_count = counts[5];
		b->edge_count = counts[6];
		b->vertex_count = counts[7];
		if (!box_collbsp_make(b, arrays[0], counts[0]))
			return -1;
		owned += (unsigned int)counts[0] * (unsigned int)sizeof(struct box_collbsp_node);
		b->form = form;
		if (form)
		{
			if (!box_collbsp_make_bounds(b))
				return -1;
			owned += (unsigned int)counts[5] * 16u;
		}
		bsp_count++;
	}
	return (int)owned;
}

static const struct box_collbsp *bsp_of(unsigned int key)
{
	unsigned int k;

	for (k = 0; k < bsp_count; k++)
		if (bsps[k].key == key)
			return &bsps[k].bsp;
	return 0;
}

static unsigned int last_tick, totals[7];
static int pass_of_tick;
#ifdef BOX_COLLBSP_COUNT
extern unsigned int box_collbsp_count_asker;
extern int box_collbsp_count_on;
#endif
static int have_tick;

static void collbsp_tick(const struct box_tick *tick)
{
	unsigned int k, total = 0, at = 0;

	if (form_b)
	{
		/* each walk over the capture starts with no blocks (the ticks start again) */
		if (have_tick && tick->tick < last_tick)
		{
			totals[0] += cache.made, totals[1] += cache.reused, totals[2] += cache.refused, totals[3] += cache.made_nodes, totals[4] += cache.made_surfaces;
			totals[5] += cache.ray_reused, totals[6] += cache.ray_full;
			box_collbsp_cache_free(&cache);
			box_collbsp_cache_init(&cache, 0.5f);
		}
		/* the harness runs a tick's calls several times (the check, the warm
		passes, emptied, crowded): each starts from the blocks the tick before
		left, so a pass doesn't reuse what an earlier pass of the same tick
		made, and the counts are one pass's */
		if (have_tick && tick->tick == last_tick)
			box_collbsp_cache_restore(&cache);
		else
			box_collbsp_cache_save(&cache);
	}
	pass_of_tick = have_tick && tick->tick == last_tick ? pass_of_tick + 1 : 0;
	/* form M: as B, each of a tick's passes starts from the memos the ticks
	before left (a walk over the capture again starts with none) */
	if (form_m)
	{
		if (have_tick && tick->tick < last_tick)
			memset(&memos, 0, sizeof(memos));
		if (have_tick && tick->tick == last_tick)
			memos = memos_saved;
		else
			memos_saved = memos;
	}
	have_tick = 1;
	last_tick = tick->tick;

	if (tick->entities > query_room)
	{
		free(queries);
		query_room = tick->entities * 2;
		queries = calloc(query_room, sizeof(*queries));
	}
	for (k = 0; k < tick->entities; k++)
		total += tick->entity[k].output_length / 4 + 1;
	if (total > output_room)
	{
		free(outputs);
		output_room = total * 2;
		outputs = calloc(output_room, sizeof(*outputs));
	}
	for (k = 0; k < tick->entities; k++)
	{
		const struct box_entity *e = &tick->entity[k];
		struct query *q = &queries[k];
		unsigned int head[5], args = 0, flag_bytes;

		memset(q, 0, offsetof(struct query, breakable_copy));
		q->out = outputs + at;
		q->kind = e->kind;
		q->output_length = e->output_length; /* the box writes as much: a different answer differs in its words */
		at += (e->output_length + 3) / 4;
		if (e->input_length < sizeof(head))
			continue;
		memcpy(head, e->input, sizeof(head));
		args = e->kind == 1 ? 4 : e->kind == 2 ? 8 : e->kind == 3 ? 10 : 0;
		flag_bytes = head[3];
		if (!args || e->input_length < sizeof(head) + 4 * args + flag_bytes || flag_bytes > sizeof(q->breakable_copy))
			continue;
		q->bsp = bsp_of(head[0]);
		q->breakable_count = (short)head[1];
		q->any = (int)(head[4] & 1);
		q->asker = head[2];
		q->object = (int)(head[4] >> 1) - 1;
		/* who asks: the object and the call site (the call site alone in
		captures before the object was carried) */
		q->key = q->object >= 0 ? (unsigned int)q->object << 16 ^ (q->asker & 0xFFFF) : q->asker;
		memcpy(q->args, e->input + sizeof(head), 4 * args);
		if (flag_bytes)
		{
			memcpy(q->breakable_copy, e->input + sizeof(head) + 4 * args, flag_bytes);
			q->breakable = q->breakable_copy;
		}
	}
}

static __attribute__((noinline)) void collbsp_run(int i)
{
	struct query *q = &queries[i];
	unsigned int *out = q->out, used, room = q->output_length / 4;
	int k;

	if (!q->bsp)
		return;
#ifdef BOX_COLLBSP_COUNT
	box_collbsp_count_asker = q->key;
	box_collbsp_count_on = pass_of_tick == 0;
#endif
	if (q->kind == 1)
	{
		int hit = form_m ? box_collbsp_sphere_m(&memos, q->key, q->bsp, q->breakable_count, q->breakable, q->args, q->args[3],
			q->any, &sphere_answer)
			: form_b ? box_collbsp_sphere_b(&cache, q->bsp, q->breakable_count, q->breakable, q->args, q->args[3], q->any,
			&sphere_answer) : box_collbsp_sphere(q->bsp, q->breakable_count, q->breakable, q->args, q->args[3], q->any, &sphere_answer);
		unsigned int words[5] = { hit ? 1u : 0u, (unsigned int)sphere_answer.surface_count, (unsigned int)sphere_answer.edge_count,
			(unsigned int)sphere_answer.vertex_count, (unsigned int)sphere_answer.leaf_count };
		const int *lists[4] = { sphere_answer.surfaces, sphere_answer.edges, sphere_answer.vertices, sphere_answer.leaves };
		int l;

		for (used = 0; used < 5 && used < room; used++)
			out[used] = words[used];
		for (l = 0; l < 4; l++)
			for (k = 0; k < (int)words[1 + l] && used < room; k++)
				out[used++] = (unsigned int)lists[l][k];
	}
	else
	{
		unsigned int flags;
		int start = 0, depth = 0;
		int hit;
		unsigned int words[8];

		memcpy(&flags, &q->args[0], 4);
		if (q->kind == 3)
		{
			memcpy(&start, &q->args[8], 4);
			memcpy(&depth, &q->args[9], 4);
		}
		hit = form_m ? box_collbsp_ray_m(&memos, q->key, q->bsp, flags, q->breakable_count, q->breakable, &q->args[1], &q->args[4],
			q->args[7], start, (short)depth, &ray_answer)
			: form_b ? box_collbsp_ray_b(&cache, q->bsp, flags, q->breakable_count, q->breakable, &q->args[1], &q->args[4],
			q->args[7], start, (short)depth, &ray_answer)
			: box_collbsp_ray(q->bsp, flags, q->breakable_count, q->breakable, &q->args[1], &q->args[4], q->args[7], start,
			(short)depth, &ray_answer);
		words[0] = hit ? 1u : 0u;
		memcpy(&words[1], &ray_answer.t, 4);
		words[2] = hit && ray_answer.plane >= 0 && ray_answer.plane < q->bsp->plane_count ? (unsigned int)ray_answer.plane : 0xFFFFFFFFu;
		words[3] = hit ? (unsigned int)ray_answer.surface : 0xFFFFFFFFu;
		words[4] = hit ? (unsigned int)ray_answer.designator : 0xFFFFFFFFu;
		words[5] = hit ? (unsigned int)ray_answer.flags | ((unsigned int)ray_answer.breakable << 8) : 0xFFFFFFFFu;
		words[6] = hit ? (unsigned int)(unsigned short)ray_answer.material : 0xFFFFFFFFu;
		words[7] = (unsigned int)ray_answer.leaf_count;
		for (used = 0; used < 8 && used < room; used++)
			out[used] = words[used];
		for (k = 0; k < ray_answer.leaf_count && used < room; k++)
			out[used++] = (unsigned int)ray_answer.leaves[k];
	}
	/* an answer longer than the capture's is cut to it, and one shorter leaves
	its tail zero: either differs from the capture in its counts */
	for (; used < room; used++)
		out[used] = 0;
}

static void collbsp_output(int i, const void **bytes, unsigned int *length)
{
	*bytes = queries[i].out;
	*length = queries[i].output_length;
}

/* exact, word for word; an early-out sphere that found something is read by
its callers only as found, so only that word is compared (one that found
nothing walked the whole way, and its leaves are compared) */
static int collbsp_compare(int i, const void *got, const void *want, unsigned int length, float *largest_absolute,
	float *largest_relative, unsigned int *element)
{
	const unsigned int *a = (const unsigned int *)got, *b = (const unsigned int *)want;
	unsigned int k, words = length / 4;

	*largest_absolute = *largest_relative = 0.f;
	if (!queries[i].bsp)
	{
		*element = 0xFFFFFFFEu; /* a BSP the capture didn't hold */
		return 1;
	}
	if (queries[i].kind == 1 && queries[i].any && words && b[0])
		words = 1;
	for (k = 0; k < words; k++)
		if (a[k] != b[k])
		{
			*element = k;
			*largest_absolute = 1.f;
			return 1;
		}
	return 0;
}

/* v0's budget: ~1.2 ms a tick for the BSP half (docs/box_collision.md §4,
§6b): ~6 us a query at b30's 193 a tick, ~8 at b40's 149 */
static int collbsp0_setup(const struct box_static *statics, unsigned int count)
{
	form = 0;
	form_b = form_m = 0;
	return collbsp_setup(statics, count);
}

const struct box_adapter box_collbsp = {
	"collbsp", kinds, 0, 0, BOX_EXACT, 0.f, 0.f, 1.2f, 7.f,
	collbsp0_setup, collbsp_tick, collbsp_run, collbsp_output, collbsp_compare,
};

/* form A: each surface's bounds and the walk's own list of surfaces tested
(box_collision.md §6b): the same answers, fewer surfaces tested */
static int collbspa_setup(const struct box_static *statics, unsigned int count)
{
	form = 1;
	form_b = form_m = 0;
	return collbsp_setup(statics, count);
}

const struct box_adapter box_collbspa = {
	"collbspa", kinds, 0, 0, BOX_EXACT, 0.f, 0.f, 1.2f, 7.f,
	collbspa_setup, collbsp_tick, collbsp_run, collbsp_output, collbsp_compare, "collbsp",
};

/* the diagnostic forms: a price of the sphere walk's parts on the PSP. Their
answers are incomplete by design (the check fails); only their times count */
static int collbspw_setup(const struct box_static *statics, unsigned int count)
{
	form = BOX_COLLBSP_FORM_WALK;
	form_b = form_m = 0;
	return collbsp_setup(statics, count);
}

static int collbspr_setup(const struct box_static *statics, unsigned int count)
{
	form = BOX_COLLBSP_FORM_REFS;
	form_b = form_m = 0;
	return collbsp_setup(statics, count);
}

const struct box_adapter box_collbspw = {
	"collbspw", kinds, 0, 0, BOX_EXACT, 0.f, 0.f, 1.2f, 7.f,
	collbspw_setup, collbsp_tick, collbsp_run, collbsp_output, collbsp_compare, "collbsp",
};

const struct box_adapter box_collbspr = {
	"collbspr", kinds, 0, 0, BOX_EXACT, 0.f, 0.f, 1.2f, 7.f,
	collbspr_setup, collbsp_tick, collbsp_run, collbsp_output, collbsp_compare, "collbsp",
};

/* form B: form A walked over local copies of the BSP around earlier queries
(blocks made with each query widened by 0.5 units, 16 kept); the same answers */
static int collbspb_setup(const struct box_static *statics, unsigned int count)
{
	int owned;

	form = 1;
	form_b = 1;
	form_m = 0;
	have_tick = 0;
	box_collbsp_cache_free(&cache);
	if (!box_collbsp_cache_init(&cache, 0.5f))
		return -1;
	owned = collbsp_setup(statics, count);
	return owned;
}

const struct box_adapter box_collbspb = {
	"collbspb", kinds, 0, 0, BOX_EXACT, 0.f, 0.f, 1.2f, 7.f,
	collbspb_setup, collbsp_tick, collbsp_run, collbsp_output, collbsp_compare, "collbsp",
};

static int collbspm_setup(const struct box_static *statics, unsigned int count)
{
	form = 1;
	form_b = 0;
	form_m = 1;
	have_tick = 0;
	memset(&memos, 0, sizeof(memos));
	return collbsp_setup(statics, count);
}

/* form A with each asker's one-way descent kept (box_collision.md §6b, "B for
the descent"), over collbsp's capture */
const struct box_adapter box_collbspm = {
	"collbspm", kinds, 0, 0, BOX_EXACT, 0.f, 0.f, 1.2f, 7.f,
	collbspm_setup, collbsp_tick, collbsp_run, collbsp_output, collbsp_compare, "collbsp",
};

void box_collbspm_counts(unsigned int *replayed, unsigned int *walked)
{
	*replayed = memos.replayed;
	*walked = memos.walked;
}

/* the blocks' counts, for the desktop's reading (host builds print them) */
void box_collbspb_counts(unsigned int *made, unsigned int *reused, unsigned int *refused, unsigned int *nodes, unsigned int *surfaces)
{
	*made = totals[0] + cache.made;
	*reused = totals[1] + cache.reused;
	*refused = totals[2] + cache.refused;
	*nodes = totals[3] + cache.made_nodes;
	*surfaces = totals[4] + cache.made_surfaces;
}

void box_collbspb_ray_counts(unsigned int *walked, unsigned int *full)
{
	*walked = totals[5] + cache.ray_reused;
	*full = totals[6] + cache.ray_full;
}
