/*
The collision service's BSP half (box_collbsp.h says what it is). The walks
keep the engine's arithmetic in its order (the build compiles without fused
products), so every test lands on the same side of every plane.
*/
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "box_collbsp.h"

#ifdef BOX_COLLBSP_COUNT
/* a desktop count of the walks' steps over a capture (the host build only) */
#include <stdio.h>
int box_collbsp_walk_id;
static unsigned long long n_kept, n_kept_nodes, n_kept_leaves, n_repeat, n_spheres, n_nodes3, n_leaves, n_refs, n_stack_cmp, n_match, n_nodes2, n_surfaces, n_edges_walked;
static unsigned long long r_rays, r_hits, r_nodes, r_split, r_leaves, r_tests, r_refs, r_matched, r_nodes2, r_surface_points;
static void count_report(void) __attribute__((destructor));
static void count_report(void)
{
	if (n_spheres)
		printf("collbsp count: %llu spheres; a sphere: 3D nodes %.1f, leaves %.1f, references %.1f, stack compares %.1f, matched %.1f, 2D nodes %.1f, surfaces tested %.1f, edges walked %.1f; surface tests repeated in the walk %.1f\n",
			n_spheres, (double)n_nodes3 / n_spheres, (double)n_leaves / n_spheres, (double)n_refs / n_spheres,
			(double)n_stack_cmp / n_spheres, (double)n_match / n_spheres, (double)n_nodes2 / n_spheres,
			(double)n_surfaces / n_spheres, (double)n_edges_walked / n_spheres, (double)n_repeat / n_spheres);
	if (n_kept)
		printf("collbsp count: %llu kept sweeps; a kept sweep's leaf walk: 3D nodes %.1f, leaves %.1f\n", n_kept, (double)n_kept_nodes / n_kept, (double)n_kept_leaves / n_kept);
	if (r_rays)
		printf("collbsp count: %llu rays (%.1f%% hit); a ray: 3D nodes %.1f (split %.1f), leaves reached %.1f, leaf tests %.1f, references scanned %.1f, matched %.1f, 2D nodes %.1f, surface point tests %.1f\n",
			r_rays, 100.0 * r_hits / r_rays, (double)r_nodes / r_rays, (double)r_split / r_rays, (double)r_leaves / r_rays,
			(double)r_tests / r_rays, (double)r_refs / r_rays, (double)r_matched / r_rays, (double)r_nodes2 / r_rays,
			(double)r_surface_points / r_rays);
}
#define COUNT(x) ((x)++)
/* the lines a walk's nodes touch: as laid out now (two 32-byte nodes a
64-byte line, by index), and as they would be clustered (a node and its two
child nodes on one line, as 20-byte nodes: plane and two 16-bit children,
each cluster's grandchildren rooting the next) */
static const struct box_collbsp *line_bsp;
static int *line_cluster, line_cluster_count;
static int seen_now[256], seen_now_count, seen_cl[256], seen_cl_count;
static unsigned long long ln_walks[2], ln_now[2], ln_cl[2];
static void line_add(int *set, int *count, int line)
{
	int k;

	for (k = 0; k < *count; k++)
		if (set[k] == line)
			return;
	if (*count < 256)
		set[(*count)++] = line;
}
static void line_layout(const struct box_collbsp *bsp)
{
	int *stack, top = 0, next = 0;

	free(line_cluster);
	line_bsp = bsp;
	line_cluster_count = bsp->node_count;
	line_cluster = (int *)malloc(sizeof(int) * (bsp->node_count + 1));
	stack = (int *)malloc(sizeof(int) * (bsp->node_count + 1));
	if (!line_cluster || !stack || bsp->node_count <= 0)
	{
		free(stack);
		return;
	}
	stack[top++] = 0;
	while (top)
	{
		int root = stack[--top], c, g, line = next++;

		line_cluster[root] = line;
		for (c = 0; c < 2; c++)
		{
			int child = bsp->nodes[root].child[c];

			if (child < 0 || child >= bsp->node_count)
				continue;
			line_cluster[child] = line;
			for (g = 0; g < 2; g++)
			{
				int grand = bsp->nodes[child].child[g];

				if (grand >= 0 && grand < bsp->node_count && top < bsp->node_count)
					stack[top++] = grand;
			}
		}
	}
	free(stack);
}
#define LINE_WALK_BEGIN(b) (line_bsp != (b) ? line_layout(b) : (void)0, seen_now_count = seen_cl_count = 0, path_begin())
#define LINE_NODE(i) (line_add(seen_now, &seen_now_count, (i) >> 1), line_add(seen_cl, &seen_cl_count, line_cluster ? line_cluster[i] : 0))
#define LINE_WALK_END(kind) (ln_walks[kind]++, ln_now[kind] += (unsigned)seen_now_count, ln_cl[kind] += (unsigned)seen_cl_count, path_end(kind))
#define PATH_STEP(n, t) path_step(n, t)
/* the descent a walk shares with its asker's last walk of the same kind:
the leading nodes where both took the same single child (the bench sets the
asker) */
unsigned int box_collbsp_count_asker;
int box_collbsp_count_on = 1; /* the bench's first pass of a tick only */
#define PATH_MOST 64
static struct path_memo { unsigned int asker; int length; int node[PATH_MOST]; signed char took[PATH_MOST]; } path_memo[2][1024], path_now;
static unsigned long long pm_askers, pm_walks[2], pm_had[2], pm_prefix[2], pm_length[2], pm_prefix_lines[2];
static void path_begin(void) { path_now.length = 0; }
static void path_step(int node, int took) /* took: 0 or 1 the one child, 2 both */
{
	if (path_now.length < PATH_MOST)
	{
		path_now.node[path_now.length] = node;
		path_now.took[path_now.length++] = (signed char)took;
	}
}
static void path_end(int kind)
{
	struct path_memo *m = &path_memo[kind][(box_collbsp_count_asker * 2654435761u) >> 22];
	int k = 0;

	if (!box_collbsp_count_on)
		return;
	pm_walks[kind]++;
	/* the leading one-child steps of this walk */
	while (k < path_now.length && path_now.took[k] != 2)
		k++;
	pm_length[kind] += (unsigned)k;
	{
		static unsigned int seen_askers[4096], seen_count;
		unsigned int a;

		for (a = 0; a < seen_count && seen_askers[a] != box_collbsp_count_asker; a++)
			;
		if (a == seen_count && seen_count < 4096)
			seen_askers[seen_count++] = box_collbsp_count_asker, pm_askers++;
	}
	if (m->asker == box_collbsp_count_asker && m->length > 0)
	{
		int j = 0;

		pm_had[kind]++;
		while (j < k && j < m->length && m->node[j] == path_now.node[j] && m->took[j] == path_now.took[j])
			j++;
		pm_prefix[kind] += (unsigned)j;
		{
			int set[256], count = 0, n;

			for (n = 0; n < j; n++)
				line_add(set, &count, path_now.node[n] >> 1);
			pm_prefix_lines[kind] += (unsigned)count;
		}
	}
	m->asker = box_collbsp_count_asker;
	m->length = path_now.length;
	memcpy(m->node, path_now.node, sizeof(int) * (size_t)path_now.length);
	memcpy(m->took, path_now.took, (size_t)path_now.length);
}
static void line_report(void) __attribute__((destructor));
static void line_report(void)
{
	int k;

	printf("collbsp paths: %llu askers seen\n", pm_askers);
	for (k = 0; k < 2; k++)
		if (ln_walks[k])
		{
			printf("collbsp lines: %s: node lines a walk, laid out now %.2f, clustered %.2f\n", k ? "rays" : "spheres",
				(double)ln_now[k] / ln_walks[k], (double)ln_cl[k] / ln_walks[k]);
			printf("collbsp paths: %s: %.1f%% follow a walk of their asker's; one-child descent %.2f nodes a walk, the same as the asker's last %.2f nodes (%.2f lines) a walk that follows one\n",
				k ? "rays" : "spheres", 100.0 * pm_had[k] / pm_walks[k], (double)pm_length[k] / pm_walks[k],
				pm_had[k] ? (double)pm_prefix[k] / pm_had[k] : 0, pm_had[k] ? (double)pm_prefix_lines[k] / pm_had[k] : 0);
		}
}
#else
#define COUNT(x) ((void)0)
#define LINE_WALK_BEGIN(b) ((void)0)
#define LINE_NODE(i) ((void)0)
#define LINE_WALK_END(kind) ((void)0)
#define PATH_STEP(n, t) ((void)0)
#endif

#define LONG_MIN_ ((int)0x80000000u)
#define LONG_MAX_ 0x7FFFFFFF
#define NONE_ (-1)
#define DEPTH_MOST 128 /* the engine's MAXIMUM_BSP3D_DEPTH and its 2D walks' depth */
/* the port's packed designators: the flag at bit 15 (PSP_DESIGNATOR) */
#define DESIGNATOR(x) ((int)(x) < 0 ? (int)(LONG_MIN_ | ((x) & 0x7FFF)) : (int)(x))
#define BREAKABLE_BIT 3 /* _collision_surface_breakable_bit */
#define INVISIBLE_BIT 1 /* _collision_surface_invisible_bit */
#define TWO_SIDED_LEAF_BIT 0
enum { FRONT_FACING = 1, BACK_FACING = 2, IGNORE_TWO_SIDED = 4, IGNORE_INVISIBLE = 8, IGNORE_BREAKABLE = 16 };
enum { CONTENTS_UNKNOWN, CONTENTS_EMPTY, CONTENTS_SEMI_EMPTY, CONTENTS_SOLID };

/* global_projection3d_mappings: the two kept axes by the dropped axis and sign */
static const short mappings[3][2][2] = { { { 2, 1 }, { 1, 2 } }, { { 0, 2 }, { 2, 0 } }, { { 1, 0 }, { 0, 1 } } };

int box_collbsp_make(struct box_collbsp *bsp, const void *tag_nodes, int node_count)
{
	const unsigned char *in = (const unsigned char *)tag_nodes;
	int k;

	bsp->nodes = node_count > 0 ? (struct box_collbsp_node *)malloc((size_t)node_count * sizeof(*bsp->nodes)) : 0;
	if (node_count > 0 && !bsp->nodes)
		return 0;
	bsp->node_count = node_count;
	for (k = 0; k < node_count; k++)
	{
		int record[3];
		const float *plane;

		memcpy(record, in + 12 * k, 12);
		plane = bsp->planes + 4 * record[0];
		bsp->nodes[k].n[0] = plane[0];
		bsp->nodes[k].n[1] = plane[1];
		bsp->nodes[k].n[2] = plane[2];
		bsp->nodes[k].d = plane[3];
		bsp->nodes[k].child[0] = record[1];
		bsp->nodes[k].child[1] = record[2];
		bsp->nodes[k].designator = record[0];
		bsp->nodes[k].pad = 0;
	}
	return 1;
}

/* a surface's centre and the furthest of its ring's vertices, widened so a
rounding never takes a touch away: the reject (surface_sphere) only drops a
surface the sphere is clearly clear of */
int box_collbsp_make_bounds(struct box_collbsp *bsp)
{
	int s;

	bsp->bounds = bsp->surface_count > 0 ? (float *)malloc((size_t)bsp->surface_count * 4 * sizeof(float)) : 0;
	if (bsp->surface_count > 0 && !bsp->bounds)
		return 0;
	for (s = 0; s < bsp->surface_count; s++)
	{
		const struct box_collbsp_surface *surface = &bsp->surfaces[s];
		float c[3] = { 0.f, 0.f, 0.f }, radius = 0.f;
		int e = surface->first_edge, count = 0, k;
		int ring[8];

		do
		{
			const struct box_collbsp_edge *edge = &bsp->edges[e];
			int reverse = edge->surface[1] == s;

			ring[count] = edge->vertex[reverse];
			e = edge->edge[reverse];
		} while (e != surface->first_edge && ++count < 8);
		if (count < 8)
			count++;
		for (k = 0; k < count; k++)
		{
			c[0] += bsp->vertices[ring[k]].p[0];
			c[1] += bsp->vertices[ring[k]].p[1];
			c[2] += bsp->vertices[ring[k]].p[2];
		}
		c[0] /= (float)count;
		c[1] /= (float)count;
		c[2] /= (float)count;
		for (k = 0; k < count; k++)
		{
			const float *p = bsp->vertices[ring[k]].p;
			float dx = p[0] - c[0], dy = p[1] - c[1], dz = p[2] - c[2];
			float d = sqrtf(dx * dx + dy * dy + dz * dz);

			if (d > radius)
				radius = d;
		}
		bsp->bounds[4 * s] = c[0];
		bsp->bounds[4 * s + 1] = c[1];
		bsp->bounds[4 * s + 2] = c[2];
		bsp->bounds[4 * s + 3] = radius * 1.001f + 1e-3f;
	}
	return 1;
}

void box_collbsp_free(struct box_collbsp *bsp)
{
	free(bsp->nodes);
	bsp->nodes = 0;
	free(bsp->bounds);
	bsp->bounds = 0;
}

static int broken(short count, const unsigned int *flags, const struct box_collbsp_surface *surface)
{
	return (surface->flags & (1u << BREAKABLE_BIT)) && surface->breakable < count &&
		!(flags[surface->breakable >> 5] & (1u << (surface->breakable & 31)));
}

/* ---------- the sphere */

#define BUCKET(designator) ((unsigned int)(designator) * 0x9E3779B1u >> 26)

struct sphere
{
	const struct box_collbsp *bsp;
	short breakable_count;
	const unsigned int *breakable;
	const float *center;
	float radius;
	int any, found;
	struct box_collbsp_sphere_result *result;
	int stack[DEPTH_MOST];
	/* form A: the stack's designators by bucket, so a reference whose plane
	isn't on the stack is passed by without scanning it */
	unsigned char buckets[64];
	int depth;
	short axis;
	int sign;
	float center2d[2];
	/* form A: the surfaces tested this walk, with the projection they were tested in */
	int seen[32], seen_count;
	int designator; /* the plane the surface under test is projected by */
	int walk_only;  /* the 3D walk's leaves alone (B's kept spheres) */
};

static void add(int *count, int *list, int index)
{
	int k;

	for (k = 0; k < *count; k++)
		if (list[k] == index)
			return;
	if (*count < BOX_COLLBSP_LIST)
		list[(*count)++] = index;
}

static void project(const float *p, short axis, int sign, float *out)
{
	out[0] = p[mappings[axis][sign][0]];
	out[1] = p[mappings[axis][sign][1]];
}

/* fast_vector_intersects_sphere as the port has it (psp_vector_intersects_sphere) */
static int edge_touches(const float *point, const float *vector, const float *center, float radius)
{
	float p[3] = { point[0] - center[0], point[1] - center[1], point[2] - center[2] };
	float c = (p[0] * p[0]) + (p[1] * p[1]) + (p[2] * p[2]) - (radius * radius);
	float a, b, disc, neg_a_minus_b;

	if (c < 0.f)
		return 1;
	b = vector[0] * p[0] + vector[1] * p[1] + vector[2] * p[2];
	if (b >= 0.f)
		return 0;
	a = vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2];
	disc = b * b - a * c;
	if (disc <= 0.f)
		return 0;
	neg_a_minus_b = -a - b;
	if (neg_a_minus_b < 0.f)
		return 1;
	return neg_a_minus_b * neg_a_minus_b < disc;
}

/* the edge after `edge` round `surface`, and whether it's walked backward */
#define REVERSE(e, s) ((int)(e)->surface[1] == (s))

static void surface_sphere(struct sphere *w, int s)
{
	const struct box_collbsp *bsp = w->bsp;
	const struct box_collbsp_surface *surface;
	float radius_squared;
	int e;
	int hit = 0, count;

	if (s < 0 || s >= bsp->surface_count)
		return;
	if (bsp->form)
	{
		/* tested already this walk, in the same projection: the same answer, and
		the lists keep their first entries (add() takes no twice) */
		int key = s << 3 | w->axis << 1 | w->sign, k;
		const float *b = bsp->bounds + 4 * s;
		float dx = b[0] - w->center[0], dy = b[1] - w->center[1], dz = b[2] - w->center[2], reach = b[3] + w->radius * 1.001f + 1e-3f;

		for (k = 0; k < w->seen_count; k++)
			if (w->seen[k] == key)
				return;
		if (w->seen_count < 32)
			w->seen[w->seen_count++] = key;
		/* clear of the surface's bounding sphere: no vertex or edge in reach,
		and the centre's projection outside the polygon */
		if (dx * dx + dy * dy + dz * dz > reach * reach)
			return;
	}
	COUNT(n_surfaces);
#ifdef BOX_COLLBSP_COUNT
	{
		static int seen_s[512], seen_a[512], seen_n, walk_id;
		extern int box_collbsp_walk_id;
		int k;

		if (walk_id != box_collbsp_walk_id)
			walk_id = box_collbsp_walk_id, seen_n = 0;
		for (k = 0; k < seen_n; k++)
			if (seen_s[k] == s && seen_a[k] == (w->axis << 1 | w->sign))
			{
				n_repeat++;
				break;
			}
		if (k == seen_n && seen_n < 512)
			seen_s[seen_n] = s, seen_a[seen_n++] = w->axis << 1 | w->sign;
	}
#endif
	surface = &bsp->surfaces[s];
	if (broken(w->breakable_count, w->breakable, surface))
		return;
	radius_squared = w->radius * w->radius;
	e = surface->first_edge;
	count = 0;
	do
	{
		const struct box_collbsp_edge *edge = &bsp->edges[e];
		int reverse = REVERSE(edge, s);
		int v = edge->vertex[reverse];

		COUNT(n_edges_walked);
		const float *p = bsp->vertices[v].p;
		float dx = p[0] - w->center[0], dy = p[1] - w->center[1], dz = p[2] - w->center[2];

		if (dx * dx + dy * dy + dz * dz <= radius_squared)
		{
			add(&w->result->vertex_count, w->result->vertices, v);
			hit = 1;
		}
		e = edge->edge[reverse];
	} while (e != surface->first_edge && ++count < 8);
	e = surface->first_edge;
	count = 0;
	do
	{
		const struct box_collbsp_edge *edge = &bsp->edges[e];
		int reverse = REVERSE(edge, s);
		const float *p0 = bsp->vertices[edge->vertex[reverse]].p, *p1 = bsp->vertices[edge->vertex[!reverse]].p;
		float along[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };

		if (edge_touches(p0, along, w->center, w->radius))
		{
			int before = w->result->edge_count;

			add(&w->result->edge_count, w->result->edges, e);
			if (w->result->edge_count > before)
				w->result->edge_reverse[before] = (unsigned char)reverse;
			hit = 1;
		}
		e = edge->edge[reverse];
	} while (e != surface->first_edge && ++count < 8);
	if (!hit)
	{
		e = surface->first_edge;
		count = 0;
		do
		{
			const struct box_collbsp_edge *edge = &bsp->edges[e];
			int reverse = REVERSE(edge, s);
			float q0[2], q1[2], v0[2], v1[2];

			project(bsp->vertices[edge->vertex[reverse]].p, w->axis, w->sign, q0);
			project(bsp->vertices[edge->vertex[!reverse]].p, w->axis, w->sign, q1);
			v0[0] = q0[0] - w->center2d[0];
			v0[1] = q0[1] - w->center2d[1];
			v1[0] = q1[0] - w->center2d[0];
			v1[1] = q1[1] - w->center2d[1];
			if (v0[0] * v1[1] - v0[1] * v1[0] < 0.f)
				return;
			e = edge->edge[reverse];
		} while (e != surface->first_edge && ++count < 8);
	}
	{
		int before = w->result->surface_count;

		add(&w->result->surface_count, w->result->surfaces, s);
		if (w->result->surface_count > before)
			w->result->surface_designator[before] = w->designator;
	}
}

static void sphere2d(struct sphere *w, int child)
{
	while (!(child & LONG_MIN_))
	{
		const struct box_collbsp_node2d *node;
		float distance;

		if (w->found)
			return;
		COUNT(n_nodes2);
		node = &w->bsp->nodes2d[child];
		distance = (node->n[0] * w->center2d[0] + node->n[1] * w->center2d[1]) - node->d;
		if (distance <= w->radius)
			sphere2d(w, node->child[0]);
		if (!(distance >= -w->radius))
			return;
		child = node->child[1];
	}
	if (w->found)
		return;
	surface_sphere(w, child & LONG_MAX_);
	if (w->any && (w->result->surface_count > 0 || w->result->edge_count > 0))
		w->found = 1;
}

static void sphere3d(struct sphere *w, int index)
{
	const struct box_collbsp *bsp = w->bsp;
	int r, end;
	const struct box_collbsp_leaf *leaf;

	while (!(index & LONG_MIN_))
	{
		const struct box_collbsp_node *node = &bsp->nodes[index];
		float distance = (w->center[0] * node->n[0] + w->center[1] * node->n[1] + w->center[2] * node->n[2]) - node->d;
		int second = distance < w->radius, first_side = distance > -w->radius;

		if (w->found)
			return;
		COUNT(n_nodes3);
		LINE_NODE(index);
		PATH_STEP(index, first_side && second ? 2 : first_side);
		if (first_side && second)
		{
			int front = node->designator | LONG_MIN_, back = node->designator & LONG_MAX_;

			w->stack[w->depth++] = front;
			w->buckets[BUCKET(front)]++;
			sphere3d(w, node->child[0]);
			w->buckets[BUCKET(front)]--;
			w->depth--;
			w->stack[w->depth++] = back;
			w->buckets[BUCKET(back)]++;
			sphere3d(w, node->child[1]);
			w->buckets[BUCKET(back)]--;
			w->depth--;
			return;
		}
		index = node->child[first_side];
	}
	if (index == NONE_ || w->found)
		return;
	index &= LONG_MAX_;
	leaf = &bsp->leaves[index];
	COUNT(n_leaves);
	if (w->result->leaf_count < BOX_COLLBSP_LIST)
		w->result->leaves[w->result->leaf_count++] = index;
	/* the diagnostic forms (BOX_COLLBSP_FORM_WALK, _REFS: a price of the walk's
	parts on the PSP, their answers incomplete): the 3D walk alone, or with the
	references matched and projected but no surface tested */
	if (bsp->form == BOX_COLLBSP_FORM_WALK || w->walk_only)
		return;
	end = leaf->first_reference + leaf->reference_count;
	for (r = leaf->first_reference; r < end && !w->found; r++)
	{
		const struct box_collbsp_reference *reference = &bsp->references[r];
		int designator = DESIGNATOR(reference->designator);
		int k;

		COUNT(n_refs);
		if (bsp->form && !w->buckets[BUCKET(designator)])
			continue;
		for (k = 0; k < w->depth; k++)
			if ((COUNT(n_stack_cmp), w->stack[k] == designator))
			{
				COUNT(n_match);
				const float *plane = bsp->planes + 4 * (designator & LONG_MAX_);
				float along = -((w->center[0] * plane[0] + w->center[1] * plane[1] + w->center[2] * plane[2]) - plane[3]);
				float projected[3];
				float ai = fabsf(plane[0]), aj = fabsf(plane[1]), ak = fabsf(plane[2]);
				short axis;

				projected[0] = plane[0] * along + w->center[0];
				projected[1] = plane[1] * along + w->center[1];
				projected[2] = plane[2] * along + w->center[2];
				if (ak >= aj && ak >= ai)
					axis = 2;
				else
					axis = aj >= ai ? 1 : 0;
				w->axis = axis;
				w->sign = (plane[axis] > 0.f) != ((designator & LONG_MIN_) ? 1 : 0);
				w->designator = designator;
				project(projected, w->axis, w->sign, w->center2d);
				if (bsp->form != BOX_COLLBSP_FORM_REFS)
					sphere2d(w, DESIGNATOR(reference->root));
				break;
			}
	}
}

static int sphere_from(const struct box_collbsp *bsp, int start, short breakable_count, const unsigned int *breakable,
	const float center[3], float radius, int any, struct box_collbsp_sphere_result *result);

/* where a sphere's 3D walk first splits (for box_collbsp_sphere_result.start_node) */
int box_collbsp_split_node(const struct box_collbsp *bsp, const float center[3], float radius)
{
	int index = 0;

	while (!(index & LONG_MIN_))
	{
		const struct box_collbsp_node *node = &bsp->nodes[index];
		float distance = (center[0] * node->n[0] + center[1] * node->n[1] + center[2] * node->n[2]) - node->d;
		int second = distance < radius, first_side = distance > -radius;

		if (first_side && second)
			break;
		index = node->child[first_side];
	}
	return index;
}

int box_collbsp_sphere(const struct box_collbsp *bsp, short breakable_count, const unsigned int *breakable,
	const float center[3], float radius, int any, struct box_collbsp_sphere_result *result)
{
	return sphere_from(bsp, 0, breakable_count, breakable, center, radius, any, result);
}

static int sphere_from(const struct box_collbsp *bsp, int start, short breakable_count, const unsigned int *breakable,
	const float center[3], float radius, int any, struct box_collbsp_sphere_result *result)
{
	struct sphere w;

	w.bsp = bsp;
	w.breakable_count = breakable ? breakable_count : 0;
	w.breakable = breakable;
	w.center = center;
	w.radius = radius;
	w.any = any;
	w.found = 0;
	w.result = result;
	w.depth = 0;
	w.seen_count = 0;
	w.walk_only = 0;
	w.designator = 0;
	memset(w.buckets, 0, sizeof(w.buckets));
	COUNT(n_spheres);
#ifdef BOX_COLLBSP_COUNT
	box_collbsp_walk_id++;
#endif
	result->surface_count = result->edge_count = result->vertex_count = result->leaf_count = 0;
	LINE_WALK_BEGIN(bsp);
	sphere3d(&w, start);
	LINE_WALK_END(0);
	return result->surface_count > 0 || result->edge_count > 0;
}

/* ---------- B: a sphere answered from a wider one's lists */

int box_collbsp_sphere_kept(const struct box_collbsp *bsp, short breakable_count, const unsigned int *breakable,
	const struct box_collbsp_sphere_result *kept, const float center[3], float radius,
	struct box_collbsp_sphere_result *result)
{
	struct sphere w;
	float radius_squared = radius * radius;
	int k;

	w.bsp = bsp;
	w.breakable_count = breakable ? breakable_count : 0;
	w.breakable = breakable;
	w.center = center;
	w.radius = radius;
	w.any = 0;
	w.found = 0;
	w.result = result;
	w.depth = 0;
	w.seen_count = 0;
	w.walk_only = 1;
	w.designator = 0;
	memset(w.buckets, 0, sizeof(w.buckets));
	result->surface_count = result->edge_count = result->vertex_count = result->leaf_count = 0;
#ifdef BOX_COLLBSP_COUNT
	{
		unsigned long long nodes = n_nodes3, leaves = n_leaves;

		sphere3d(&w, kept->start_node);
		n_kept++;
		n_kept_nodes += n_nodes3 - nodes;
		n_kept_leaves += n_leaves - leaves;
		n_nodes3 = nodes;
		n_leaves = leaves;
	}
#else
	sphere3d(&w, kept->start_node);
#endif
	for (k = 0; k < kept->vertex_count; k++)
	{
		const float *p = bsp->vertices[kept->vertices[k]].p;
		float dx = p[0] - center[0], dy = p[1] - center[1], dz = p[2] - center[2];

		if (dx * dx + dy * dy + dz * dz <= radius_squared)
			result->vertices[result->vertex_count++] = kept->vertices[k];
	}
	for (k = 0; k < kept->edge_count; k++)
	{
		const struct box_collbsp_edge *edge = &bsp->edges[kept->edges[k]];
		int reverse = kept->edge_reverse[k];
		const float *p0 = bsp->vertices[edge->vertex[reverse]].p, *p1 = bsp->vertices[edge->vertex[!reverse]].p;
		float along[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };

		if (edge_touches(p0, along, center, radius))
		{
			result->edge_reverse[result->edge_count] = (unsigned char)reverse;
			result->edges[result->edge_count++] = kept->edges[k];
		}
	}
	for (k = 0; k < kept->surface_count; k++)
	{
		int s = kept->surfaces[k], designator = kept->surface_designator[k], e, count, hit = 0;
		const struct box_collbsp_surface *surface = &bsp->surfaces[s];
		const float *plane = bsp->planes + 4 * (designator & LONG_MAX_);
		float along = -((center[0] * plane[0] + center[1] * plane[1] + center[2] * plane[2]) - plane[3]);
		float projected[3], ai = fabsf(plane[0]), aj = fabsf(plane[1]), ak = fabsf(plane[2]);
		short axis;
		int sign;
		float c2[2];

		e = surface->first_edge;
		count = 0;
		do
		{
			const struct box_collbsp_edge *edge = &bsp->edges[e];
			int reverse = REVERSE(edge, s);
			const float *p = bsp->vertices[edge->vertex[reverse]].p;
			const float *p1 = bsp->vertices[edge->vertex[!reverse]].p;
			float dx = p[0] - center[0], dy = p[1] - center[1], dz = p[2] - center[2];
			float v[3] = { p1[0] - p[0], p1[1] - p[1], p1[2] - p[2] };

			if (dx * dx + dy * dy + dz * dz <= radius_squared || edge_touches(p, v, center, radius))
			{
				hit = 1;
				break;
			}
			e = edge->edge[reverse];
		} while (e != surface->first_edge && ++count < 8);
		if (!hit)
		{
			projected[0] = plane[0] * along + center[0];
			projected[1] = plane[1] * along + center[1];
			projected[2] = plane[2] * along + center[2];
			if (ak >= aj && ak >= ai)
				axis = 2;
			else
				axis = aj >= ai ? 1 : 0;
			sign = (plane[axis] > 0.f) != ((designator & LONG_MIN_) ? 1 : 0);
			project(projected, axis, sign, c2);
			e = surface->first_edge;
			count = 0;
			hit = 1;
			do
			{
				const struct box_collbsp_edge *edge = &bsp->edges[e];
				int reverse = REVERSE(edge, s);
				float q0[2], q1[2], v0[2], v1[2];

				project(bsp->vertices[edge->vertex[reverse]].p, axis, sign, q0);
				project(bsp->vertices[edge->vertex[!reverse]].p, axis, sign, q1);
				v0[0] = q0[0] - c2[0];
				v0[1] = q0[1] - c2[1];
				v1[0] = q1[0] - c2[0];
				v1[1] = q1[1] - c2[1];
				if (v0[0] * v1[1] - v0[1] * v1[0] < 0.f)
				{
					hit = 0;
					break;
				}
				e = edge->edge[reverse];
			} while (e != surface->first_edge && ++count < 8);
		}
		if (hit)
		{
			result->surface_designator[result->surface_count] = designator;
			result->surfaces[result->surface_count++] = s;
		}
	}
	return result->surface_count > 0 || result->edge_count > 0;
}

/* ---------- the ray */

struct ray
{
	const struct box_collbsp *bsp;
	unsigned int flags;
	short breakable_count;
	const unsigned int *breakable;
	const float *point, *vector;
	struct box_collbsp_ray_result *result;
	int last_leaf, last_plane;
	int last_contents;
};

/* bsp2d_test_point, with its limits */
static int point2d(const struct box_collbsp *bsp, const float *p, int index)
{
	int depth = 0;

	while (!(index & LONG_MIN_))
	{
		const struct box_collbsp_node2d *node;
		float distance;

		if (index >= bsp->node2d_count || depth++ >= DEPTH_MOST)
			return NONE_;
		node = &bsp->nodes2d[index];
		distance = (node->n[0] * p[0] + node->n[1] * p[1]) - node->d;
		index = node->child[distance >= 0.0f];
		COUNT(r_nodes2);
	}
	return index != NONE_ ? index & LONG_MAX_ : NONE_;
}

/* collision_surface_test_point */
static int surface_point(const struct ray *w, int s, short axis, int sign, const float *p)
{
	const struct box_collbsp *bsp = w->bsp;
	const struct box_collbsp_surface *surface;
	int e;
	int count = 0;

	if (s < 0 || s >= bsp->surface_count)
		return 0;
	COUNT(r_surface_points);
	surface = &bsp->surfaces[s];
	if (broken(w->breakable_count, w->breakable, surface))
		return 0;
	e = surface->first_edge;
	do
	{
		const struct box_collbsp_edge *edge = &bsp->edges[e];
		int reverse = REVERSE(edge, s);
		float q0[2], q1[2], v0[2], v1[2];

		project(bsp->vertices[edge->vertex[reverse]].p, axis, sign, q0);
		project(bsp->vertices[edge->vertex[!reverse]].p, axis, sign, q1);
		v0[0] = p[0] - q0[0];
		v0[1] = p[1] - q0[1];
		v1[0] = q1[0] - q0[0];
		v1[1] = q1[1] - q0[1];
		if (v0[0] * v1[1] - v0[1] * v1[0] > 0.f)
			return 0;
		e = edge->edge[reverse];
	} while (e != surface->first_edge && ++count < 8);
	return 1;
}

/* collision_leaf_test_vector */
static int leaf_ray(const struct ray *w, int leaf_index, int plane_index, float t, int test_surface)
{
	const struct box_collbsp *bsp = w->bsp;
	const struct box_collbsp_leaf *leaf = &bsp->leaves[leaf_index];
	int r, end = leaf->first_reference + leaf->reference_count;

	COUNT(r_tests);
	for (r = leaf->first_reference; r < end; r++)
	{
		const struct box_collbsp_reference *reference = &bsp->references[r];

		COUNT(r_refs);
		if ((DESIGNATOR(reference->designator) & LONG_MAX_) == plane_index)
		{
			COUNT(r_matched);
			const float *plane = bsp->planes + 4 * plane_index;
			float ai = fabsf(plane[0]), aj = fabsf(plane[1]), ak = fabsf(plane[2]);
			short axis;
			int sign;
			float hit[3], p2[2];
			int s;

			if (ak >= aj && ak >= ai)
				axis = 2;
			else
				axis = aj >= ai ? 1 : 0;
			sign = (plane[axis] > 0.f) != ((DESIGNATOR(reference->designator) & LONG_MIN_) ? 1 : 0);
			hit[0] = w->vector[0] * t + w->point[0];
			hit[1] = w->vector[1] * t + w->point[1];
			hit[2] = w->vector[2] * t + w->point[2];
			project(hit, axis, sign, p2);
			s = point2d(bsp, p2, DESIGNATOR(reference->root));
			if (s >= 0 && s < bsp->surface_count && (!test_surface || surface_point(w, s, axis, sign, p2)))
				return s;
		}
	}
	return NONE_;
}

static int ray3d(struct ray *w, int index, float t0, float t1)
{
	const struct box_collbsp *bsp = w->bsp;

	if (!(index & LONG_MIN_))
	{
		const struct box_collbsp_node *node = &bsp->nodes[index];
		float distance;

		COUNT(r_nodes);
		LINE_NODE(index);
		distance = (w->point[0] * node->n[0] + w->point[1] * node->n[1] + w->point[2] * node->n[2]) - node->d;
		float dot = w->vector[0] * node->n[0] + w->vector[1] * node->n[1] + w->vector[2] * node->n[2];
		float distance0 = dot * t0 + distance, distance1 = dot * t1 + distance;
		int back = distance0 < 0.f || distance1 < 0.f, front_reach = distance0 >= 0.f || distance1 >= 0.f;

		PATH_STEP(index, back && front_reach ? 2 : front_reach);
		if (back && front_reach)
		{
			int front = dot > 0.f;

			COUNT(r_split);
			float t = -(distance / dot);

			if (ray3d(w, node->child[!front], t0, t))
				return 1;
			if (w->result->t <= t)
				return 0;
			w->last_plane = node->designator;
			if (ray3d(w, node->child[front], t, t1))
				return 1;
		}
		else if (ray3d(w, node->child[front_reach], t0, t1))
			return 1;
	}
	else
	{
		int leaf_index = NONE_, test_leaf;
		int contents = CONTENTS_SOLID, test_surface = 0;

		if (index != NONE_)
		{
			leaf_index = index & LONG_MAX_;
			contents = (bsp->leaves[leaf_index].flags & (1u << TWO_SIDED_LEAF_BIT)) ? CONTENTS_SEMI_EMPTY : CONTENTS_EMPTY;
		}
		if ((w->flags & FRONT_FACING) && (w->last_contents == CONTENTS_EMPTY || w->last_contents == CONTENTS_SEMI_EMPTY) &&
			contents == CONTENTS_SOLID)
			test_leaf = w->last_leaf;
		else if ((w->flags & BACK_FACING) && w->last_contents == CONTENTS_SOLID &&
			(contents == CONTENTS_EMPTY || contents == CONTENTS_SEMI_EMPTY))
			test_leaf = leaf_index;
		else if (!(w->flags & IGNORE_TWO_SIDED) && w->last_contents == CONTENTS_SEMI_EMPTY &&
			contents == CONTENTS_SEMI_EMPTY)
		{
			test_leaf = (w->flags & FRONT_FACING) ? w->last_leaf : leaf_index;
			test_surface = 1;
		}
		else
			test_leaf = NONE_;
		if (test_leaf != NONE_)
		{
			int s = leaf_ray(w, test_leaf, w->last_plane, t0, test_surface);

			if (s != NONE_)
			{
				const struct box_collbsp_surface *surface = &bsp->surfaces[s];

				if ((!(surface->flags & (1u << INVISIBLE_BIT)) || !(w->flags & IGNORE_INVISIBLE)) &&
					(!(surface->flags & (1u << BREAKABLE_BIT)) || !(w->flags & IGNORE_BREAKABLE)))
				{
					w->result->t = t0;
					w->result->plane = w->last_plane;
					w->result->surface = s;
					w->result->designator = DESIGNATOR(surface->designator);
					w->result->flags = surface->flags;
					w->result->breakable = surface->breakable;
					w->result->material = surface->material;
					return 1;
				}
			}
		}
		if (leaf_index != NONE_)
		{
			COUNT(r_leaves);
			if (w->result->leaf_count < BOX_COLLBSP_LIST)
				w->result->leaves[w->result->leaf_count++] = leaf_index;
			else
				w->result->leaves[BOX_COLLBSP_LIST - 1] = leaf_index;
		}
		w->last_leaf = leaf_index;
		w->last_contents = contents;
	}
	return 0;
}

int box_collbsp_ray(const struct box_collbsp *bsp, unsigned int flags, short breakable_count,
	const unsigned int *breakable, const float point[3], const float vector[3], float maximum_t, int start_node,
	short start_depth, struct box_collbsp_ray_result *result)
{
	struct ray w;
	float t;

	(void)start_depth;
	w.bsp = bsp;
	w.flags = flags;
	w.breakable_count = breakable ? breakable_count : 0;
	w.breakable = breakable;
	w.point = point;
	w.vector = vector;
	w.result = result;
	w.last_leaf = NONE_;
	w.last_plane = NONE_;
	w.last_contents = CONTENTS_UNKNOWN;
	result->t = maximum_t < 0.f ? 0.f : maximum_t;
	result->leaf_count = 0;
	result->plane = -1;
	t = maximum_t < 0.f ? 0.f : (maximum_t > 1.f ? 1.f : maximum_t);
	COUNT(r_rays);
#ifdef BOX_COLLBSP_COUNT
	LINE_WALK_BEGIN(bsp);
	{
		int hit = ray3d(&w, start_node, 0.f, t);

		LINE_WALK_END(1);
		if (hit)
			return COUNT(r_hits), 1;
	}
	return 0;
#else
	return ray3d(&w, start_node, 0.f, t);
#endif
}

/* ---------- B: local copies of the BSP (box_collision.md §6b, "B, designed")

A block is a small box_collbsp made from what a wider sphere W's walk reads:
the nodes W straddled (both children kept), the nodes it lay on one side of
collapsed into the child it takes (a sphere inside W takes the same one, and a
one-sided node pushes nothing on the plane stack), every leaf reached with all
its references, each reference's plane and 2D tree, and every surface those
trees name, with its ring (edges, vertices). Indices are the block's own; the
answers' lists are mapped back to the BSP's when a query ends. */

#define BLOCK_MAP 4096 /* each map's slots, a power of two */

struct grow { void *at; int count, room, size; };

static int grow_add(struct grow *g)
{
	if (g->count == g->room)
	{
		int room = g->room ? g->room * 2 : 32;
		void *at = realloc(g->at, (size_t)room * (size_t)g->size);

		if (!at)
			return -1;
		g->at = at;
		g->room = room;
	}
	return g->count++;
}

/* a map from the BSP's indices to the block's, emptied by a new stamp, not
by clearing its slots (a block is made far more often than its map fills) */
struct map { int key[BLOCK_MAP], value[BLOCK_MAP]; unsigned int stamp[BLOCK_MAP], now; };

static void map_clear(struct map *m)
{
	if (++m->now == 0)
	{
		memset(m->stamp, 0, sizeof(m->stamp));
		m->now = 1;
	}
}

static int *map_slot(struct map *m, int key)
{
	unsigned int h = (unsigned int)key * 0x9E3779B1u >> 20;
	int probes;

	for (probes = 0; probes < BLOCK_MAP; probes++, h++)
	{
		h &= BLOCK_MAP - 1;
		if (m->stamp[h] != m->now)
		{
			m->stamp[h] = m->now;
			m->key[h] = key;
			m->value[h] = -1;
			return &m->value[h];
		}
		if (m->key[h] == key)
			return &m->value[h];
	}
	return 0;
}

struct builder
{
	const struct box_collbsp *g;
	const float *center;
	float radius;
	struct grow nodes, planes, leaves, refs, nodes2d, surfaces, edges, vertices;
	struct grow leaf_g, surface_g, edge_g, vertex_g, plane_g;
	struct map plane_map, node2d_map, surface_map, edge_map, vertex_map;
	int failed;
};

static int local_plane(struct builder *b, int designator)
{
	int index = designator & LONG_MAX_, *slot = map_slot(&b->plane_map, index);

	if (!slot)
		return b->failed = 1, 0;
	if (*slot < 0)
	{
		int k = grow_add(&b->planes), j = grow_add(&b->plane_g);

		if (k < 0 || j < 0)
			return b->failed = 1, 0;
		memcpy((float *)b->planes.at + 4 * k, b->g->planes + 4 * index, 16);
		((int *)b->plane_g.at)[j] = index;
		*slot = k;
	}
	return *slot | (designator & LONG_MIN_);
}

static int local_vertex(struct builder *b, int v)
{
	int *slot = map_slot(&b->vertex_map, v);

	if (!slot)
		return b->failed = 1, 0;
	if (*slot < 0)
	{
		int k = grow_add(&b->vertices), j = grow_add(&b->vertex_g);

		if (k < 0 || j < 0)
			return b->failed = 1, 0;
		((struct box_collbsp_vertex *)b->vertices.at)[k] = b->g->vertices[v];
		((int *)b->vertex_g.at)[j] = v;
		*slot = k;
	}
	return *slot;
}

/* an edge: its vertices copied when it's first met, its next edges and its
surfaces filled in by the rings of the surfaces the block holds (0xFFFF where
none does: a ring of a held surface never reads those) */
static int local_edge(struct builder *b, int e)
{
	int *slot = map_slot(&b->edge_map, e);

	if (!slot)
		return b->failed = 1, 0;
	if (*slot < 0)
	{
		int k = grow_add(&b->edges), j = grow_add(&b->edge_g), n;
		struct box_collbsp_edge out;

		if (k < 0 || j < 0)
			return b->failed = 1, 0;
		((int *)b->edge_g.at)[j] = e;
		*slot = k;
		for (n = 0; n < 2; n++)
		{
			out.vertex[n] = (unsigned short)local_vertex(b, b->g->edges[e].vertex[n]);
			out.edge[n] = 0xFFFF;
			out.surface[n] = 0xFFFF;
		}
		((struct box_collbsp_edge *)b->edges.at)[k] = out;
	}
	return *slot;
}

static int local_surface(struct builder *b, int s)
{
	int *slot, k, e, first, count = 0;
	struct box_collbsp_surface copy;

	if (s < 0 || s >= b->g->surface_count)
		return 0x7FFF; /* out of range in the block too: the walk passes it by */
	slot = map_slot(&b->surface_map, s);
	if (!slot)
		return b->failed = 1, 0;
	if (*slot >= 0)
		return *slot;
	k = grow_add(&b->surfaces);
	if (k < 0 || grow_add(&b->surface_g) < 0)
		return b->failed = 1, 0;
	((int *)b->surface_g.at)[k] = s;
	*slot = k;
	first = b->g->surfaces[s].first_edge;
	copy = b->g->surfaces[s];
	copy.first_edge = (unsigned short)local_edge(b, first);
	((struct box_collbsp_surface *)b->surfaces.at)[k] = copy;
	e = first;
	do
	{
		const struct box_collbsp_edge *edge = &b->g->edges[e];
		int reverse = edge->surface[1] == s, le = local_edge(b, e), next = local_edge(b, edge->edge[reverse]), n;
		struct box_collbsp_edge *have;

		if (b->failed)
			return 0;
		have = &((struct box_collbsp_edge *)b->edges.at)[le];
		have->edge[reverse] = (unsigned short)next;
		for (n = 0; n < 2; n++)
			if (edge->surface[n] == s)
				have->surface[n] = (unsigned short)k;
		e = edge->edge[reverse];
	} while (e != first && ++count < 8);
	return k;
}

static int local_node2d(struct builder *b, int index)
{
	int *slot, k, n;
	struct box_collbsp_node2d copy;

	if (index & LONG_MIN_)
	{
		int s = index & LONG_MAX_;

		if (index == NONE_)
			return NONE_;
		return LONG_MIN_ | local_surface(b, s);
	}
	if (index >= b->g->node2d_count)
		return b->failed = 1, 0;
	slot = map_slot(&b->node2d_map, index);
	if (!slot)
		return b->failed = 1, 0;
	if (*slot >= 0)
		return *slot;
	k = grow_add(&b->nodes2d);
	if (k < 0)
		return b->failed = 1, 0;
	*slot = k;
	copy = b->g->nodes2d[index];
	for (n = 0; n < 2; n++)
		copy.child[n] = local_node2d(b, b->g->nodes2d[index].child[n]);
	((struct box_collbsp_node2d *)b->nodes2d.at)[k] = copy;
	return k;
}

/* W's walk over the BSP, copying: returns the block's index for what the walk
reaches at `index` (a node, or a leaf with the high bit) */
static int build3d(struct builder *b, int index)
{
	while (!(index & LONG_MIN_))
	{
		const struct box_collbsp_node *node = &b->g->nodes[index];
		float distance = (b->center[0] * node->n[0] + b->center[1] * node->n[1] + b->center[2] * node->n[2]) - node->d;
		int second = distance < b->radius, first_side = distance > -b->radius;

		if (first_side && second)
		{
			int k = grow_add(&b->nodes), c0, c1;
			struct box_collbsp_node *out;

			if (k < 0)
				return b->failed = 1, 0;
			c0 = build3d(b, node->child[0]);
			c1 = build3d(b, node->child[1]);
			out = &((struct box_collbsp_node *)b->nodes.at)[k];
			*out = *node;
			out->designator = local_plane(b, node->designator);
			out->child[0] = c0;
			out->child[1] = c1;
			return k;
		}
		index = node->child[first_side];
	}
	if (index == NONE_)
		return NONE_;
	{
		int g = index & LONG_MAX_, k = grow_add(&b->leaves), r, first;
		const struct box_collbsp_leaf *leaf = &b->g->leaves[g];
		struct box_collbsp_leaf out;

		if (k < 0 || grow_add(&b->leaf_g) < 0)
			return b->failed = 1, 0;
		((int *)b->leaf_g.at)[k] = g;
		first = b->refs.count;
		for (r = leaf->first_reference; r < leaf->first_reference + leaf->reference_count; r++)
		{
			const struct box_collbsp_reference *reference = &b->g->references[r];
			int j = grow_add(&b->refs), designator, root;

			if (j < 0)
				return b->failed = 1, 0;
			designator = local_plane(b, DESIGNATOR(reference->designator));
			root = local_node2d(b, DESIGNATOR(reference->root));
			if (b->failed || (designator & LONG_MAX_) > 0x7FFF || (!(root & LONG_MIN_) && root > 0x7FFF) ||
				((root & LONG_MIN_) && (root & LONG_MAX_) > 0x7FFF))
				return b->failed = 1, 0;
			/* the packed form: the flag at bit 15 (DESIGNATOR reads it back) */
			((struct box_collbsp_reference *)b->refs.at)[j].designator =
				(short)((designator & LONG_MIN_) ? (0x8000 | (designator & 0x7FFF)) : designator);
			((struct box_collbsp_reference *)b->refs.at)[j].root =
				(short)((root & LONG_MIN_) ? (0x8000 | (root & 0x7FFF)) : root);
		}
		out = *leaf;
		out.first_reference = first;
		((struct box_collbsp_leaf *)b->leaves.at)[k] = out;
		return LONG_MIN_ | k;
	}
}

struct box_collbsp_block
{
	struct box_collbsp local;
	const struct box_collbsp *global;
	float center[3], radius;
	unsigned int flags_key;
	int *leaf_g, *surface_g, *edge_g, *vertex_g, *plane_g;
	int start; /* where a walk begins: W's root, collapsed past the nodes it lay on one side of */
	unsigned int used;
	int live;
};

static void block_free(struct box_collbsp_block *k)
{
	free(k->local.nodes);
	free((void *)k->local.planes);
	free((void *)k->local.leaves);
	free((void *)k->local.references);
	free((void *)k->local.nodes2d);
	free((void *)k->local.surfaces);
	free((void *)k->local.edges);
	free((void *)k->local.vertices);
	free(k->local.bounds);
	free(k->leaf_g);
	free(k->surface_g);
	free(k->edge_g);
	free(k->vertex_g);
	free(k->plane_g);
	memset(k, 0, sizeof(*k));
}

static unsigned int flags_key(short count, const unsigned int *flags)
{
	unsigned int h = 2166136261u, k, words = flags && count > 0 ? ((unsigned int)count + 31u) / 32u : 0;

	for (k = 0; k < words; k++)
		h = (h ^ flags[k]) * 16777619u;
	return h ^ (unsigned int)count;
}

static struct builder the_builder;

static int block_make(struct box_collbsp_block *k, const struct box_collbsp *g, const float *center, float radius,
	unsigned int key)
{
	struct builder *b = &the_builder;
	int root;

	memset(b, 0, offsetof(struct builder, plane_map));
	b->nodes.size = sizeof(struct box_collbsp_node);
	b->planes.size = 16;
	b->leaves.size = sizeof(struct box_collbsp_leaf);
	b->refs.size = sizeof(struct box_collbsp_reference);
	b->nodes2d.size = sizeof(struct box_collbsp_node2d);
	b->surfaces.size = sizeof(struct box_collbsp_surface);
	b->edges.size = sizeof(struct box_collbsp_edge);
	b->vertices.size = sizeof(struct box_collbsp_vertex);
	b->leaf_g.size = b->surface_g.size = b->edge_g.size = b->vertex_g.size = b->plane_g.size = sizeof(int);
	map_clear(&b->plane_map);
	map_clear(&b->node2d_map);
	map_clear(&b->surface_map);
	map_clear(&b->edge_map);
	map_clear(&b->vertex_map);
	b->g = g;
	b->center = center;
	b->radius = radius;
	root = g->node_count > 0 ? build3d(b, 0) : (b->failed = 1, 0);
	if (b->failed || b->surfaces.count >= 0xFFFF || b->edges.count >= 0xFFFF || b->vertices.count >= 0xFFFF)
	{
		free(b->nodes.at); free(b->planes.at); free(b->leaves.at); free(b->refs.at); free(b->nodes2d.at);
		free(b->surfaces.at); free(b->edges.at); free(b->vertices.at);
		free(b->leaf_g.at); free(b->surface_g.at); free(b->edge_g.at); free(b->vertex_g.at); free(b->plane_g.at);
		return 0;
	}
	/* the edges' copies for every edge mapped (a ring's next edges may name
	edges of surfaces the block doesn't hold: those are never walked) */
	memset(k, 0, sizeof(*k));
	k->local.nodes = (struct box_collbsp_node *)b->nodes.at;
	k->local.planes = (const float *)b->planes.at;
	k->local.leaves = (const struct box_collbsp_leaf *)b->leaves.at;
	k->local.references = (const struct box_collbsp_reference *)b->refs.at;
	k->local.nodes2d = (const struct box_collbsp_node2d *)b->nodes2d.at;
	k->local.surfaces = (const struct box_collbsp_surface *)b->surfaces.at;
	k->local.edges = (const struct box_collbsp_edge *)b->edges.at;
	k->local.vertices = (const struct box_collbsp_vertex *)b->vertices.at;
	k->local.node_count = b->nodes.count;
	k->local.plane_count = b->planes.count;
	k->local.leaf_count = b->leaves.count;
	k->local.reference_count = b->refs.count;
	k->local.node2d_count = b->nodes2d.count;
	k->local.surface_count = b->surfaces.count;
	k->local.edge_count = b->edges.count;
	k->local.vertex_count = b->vertices.count;
	k->local.form = 1;
	k->leaf_g = (int *)b->leaf_g.at;
	k->surface_g = (int *)b->surface_g.at;
	k->edge_g = (int *)b->edge_g.at;
	k->vertex_g = (int *)b->vertex_g.at;
	k->plane_g = (int *)b->plane_g.at;
	if (!box_collbsp_make_bounds(&k->local))
	{
		block_free(k);
		return 0;
	}
	k->global = g;
	k->center[0] = center[0];
	k->center[1] = center[1];
	k->center[2] = center[2];
	k->radius = radius;
	k->flags_key = key;
	k->start = root;
	k->live = 1;
	return 1;
}

/* ---------- B's cache: blocks by space */

int box_collbsp_cache_init(struct box_collbsp_cache *cache, float margin)
{
	memset(cache, 0, sizeof(*cache));
	cache->margin = margin;
	return 1;
}

static int held(const struct box_collbsp_cache *cache, const struct box_collbsp_block *k)
{
	int n;

	for (n = 0; n < BOX_COLLBSP_BLOCKS; n++)
		if (cache->slot[n] == k || (cache->saved_valid && cache->saved[n] == k))
			return 1;
	return 0;
}

static void free_retired(struct box_collbsp_cache *cache)
{
	int r, n = 0;

	for (r = 0; r < cache->retired_count; r++)
		if (held(cache, cache->retired[r]))
			cache->retired[n++] = cache->retired[r];
		else
		{
			block_free(cache->retired[r]);
			free(cache->retired[r]);
		}
	cache->retired_count = n;
}

/* a block let go: freed now, unless the saved state holds it */
static void retire(struct box_collbsp_cache *cache, struct box_collbsp_block *k)
{
	if (!cache->saved_valid || !held(cache, k))
	{
		block_free(k);
		free(k);
		return;
	}
	{
		int r;

		/* a saved block put back and let go again is listed once */
		for (r = 0; r < cache->retired_count; r++)
			if (cache->retired[r] == k)
				return;
	}
	if (cache->retired_count == BOX_COLLBSP_RETIRED)
		free_retired(cache);
	cache->retired[cache->retired_count++] = k; /* at most the saved state's blocks */
}

void box_collbsp_cache_free(struct box_collbsp_cache *cache)
{
	int k;

	cache->saved_valid = 0;
	for (k = 0; k < BOX_COLLBSP_BLOCKS; k++)
		if (cache->slot[k])
		{
			block_free(cache->slot[k]);
			free(cache->slot[k]);
			cache->slot[k] = 0;
		}
	free_retired(cache);
}

void box_collbsp_cache_save(struct box_collbsp_cache *cache)
{
	int n;

	cache->saved_valid = 1;
	for (n = 0; n < BOX_COLLBSP_BLOCKS; n++)
	{
		cache->saved[n] = cache->slot[n];
		cache->saved_used[n] = cache->slot[n] ? cache->slot[n]->used : 0;
	}
	cache->saved_counts[0] = cache->clock, cache->saved_counts[1] = cache->made, cache->saved_counts[2] = cache->reused;
	cache->saved_counts[3] = cache->refused, cache->saved_counts[4] = cache->made_nodes, cache->saved_counts[5] = cache->made_surfaces;
	cache->saved_counts[6] = cache->ray_reused, cache->saved_counts[7] = cache->ray_full;
	free_retired(cache);
}

void box_collbsp_cache_restore(struct box_collbsp_cache *cache)
{
	struct box_collbsp_block *now[BOX_COLLBSP_BLOCKS];
	int n;

	if (!cache->saved_valid)
		return;
	memcpy(now, cache->slot, sizeof(now));
	for (n = 0; n < BOX_COLLBSP_BLOCKS; n++)
	{
		cache->slot[n] = cache->saved[n];
		if (cache->slot[n])
			cache->slot[n]->used = cache->saved_used[n];
	}
	/* blocks the run made that the saved state doesn't hold */
	for (n = 0; n < BOX_COLLBSP_BLOCKS; n++)
		if (now[n] && !held(cache, now[n]))
		{
			block_free(now[n]);
			free(now[n]);
		}
	free_retired(cache);
	cache->clock = cache->saved_counts[0], cache->made = cache->saved_counts[1], cache->reused = cache->saved_counts[2];
	cache->refused = cache->saved_counts[3], cache->made_nodes = cache->saved_counts[4], cache->made_surfaces = cache->saved_counts[5];
	cache->ray_reused = cache->saved_counts[6], cache->ray_full = cache->saved_counts[7];
}

static void map_list(int count, int *list, const int *global)
{
	int k;

	for (k = 0; k < count; k++)
		list[k] = global[list[k]];
}

/* the block whose W holds the sphere (center, radius), with room for the
walks' rounding; else, when `make`, a new one made around it (the least
recently used let go). Null when none holds it and none is made. */
static struct box_collbsp_block *block_for(struct box_collbsp_cache *cache, const struct box_collbsp *bsp, unsigned int key,
	const float *center, float radius, int make)
{
	struct box_collbsp_block *k;
	int n, empty = -1, oldest = -1, at;

	cache->clock++;
	for (n = 0; n < BOX_COLLBSP_BLOCKS; n++)
	{
		k = cache->slot[n];
		if (!k)
		{
			if (empty < 0)
				empty = n;
			continue;
		}
		if (k->global == bsp && k->flags_key == key)
		{
			float dx = center[0] - k->center[0], dy = center[1] - k->center[1], dz = center[2] - k->center[2];
			float reach = k->radius - (k->radius * 1e-3f + 1e-3f) - radius;

			if (reach > 0.f && dx * dx + dy * dy + dz * dz <= reach * reach)
			{
				k->used = cache->clock;
				return k;
			}
		}
		if (oldest < 0 || k->used < cache->slot[oldest]->used)
			oldest = n;
	}
	if (!make)
		return 0;
	at = empty >= 0 ? empty : oldest;
	k = (struct box_collbsp_block *)malloc(sizeof(*k));
	if (!k || !block_make(k, bsp, center, radius + cache->margin, key))
	{
		free(k);
		cache->refused++;
		return 0;
	}
	if (cache->slot[at])
		retire(cache, cache->slot[at]);
	cache->slot[at] = k;
	k->used = cache->clock;
	cache->made++;
	cache->made_nodes += (unsigned int)k->local.node_count;
	cache->made_surfaces += (unsigned int)k->local.surface_count;
	return k;
}

int box_collbsp_sphere_b(struct box_collbsp_cache *cache, const struct box_collbsp *bsp, short breakable_count,
	const unsigned int *breakable, const float center[3], float radius, int any, struct box_collbsp_sphere_result *result)
{
	unsigned int made = cache->made;
	struct box_collbsp_block *k = block_for(cache, bsp, flags_key(breakable ? breakable_count : 0, breakable), center, radius, 1);
	int hit;

	if (!k)
		return box_collbsp_sphere(bsp, breakable_count, breakable, center, radius, any, result);
	if (cache->made == made)
		cache->reused++;
	hit = sphere_from(&k->local, k->start, breakable_count, breakable, center, radius, any, result);
	map_list(result->surface_count, result->surfaces, k->surface_g);
	map_list(result->edge_count, result->edges, k->edge_g);
	map_list(result->vertex_count, result->vertices, k->vertex_g);
	map_list(result->leaf_count, result->leaves, k->leaf_g);
	return hit;
}

/* a ray from the root whose segment lies inside a block's W walks the block:
a node one-sided for W is one-sided for every point of the segment, so the
walk takes the same children and the same splits. Short rays (a biped's
ground and step rays) make a block when none holds them; long ones only use
what's there */
int box_collbsp_ray_b(struct box_collbsp_cache *cache, const struct box_collbsp *bsp, unsigned int flags,
	short breakable_count, const unsigned int *breakable, const float point[3], const float vector[3], float maximum_t,
	int start_node, short start_depth, struct box_collbsp_ray_result *result)
{
	struct box_collbsp_block *k = 0;
	float t = maximum_t < 0.f ? 0.f : (maximum_t > 1.f ? 1.f : maximum_t), mid[3], half;
	struct ray w;
	int hit, n;

	if (start_node == 0)
	{
		for (n = 0; n < 3; n++)
			mid[n] = point[n] + vector[n] * (t * 0.5f);
		half = sqrtf(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]) * (t * 0.5f) * 1.001f + 1e-4f;
		k = block_for(cache, bsp, flags_key(breakable ? breakable_count : 0, breakable), mid, half, half <= BOX_COLLBSP_RAY_MAKE);
	}
	if (!k)
	{
		cache->ray_full++;
		return box_collbsp_ray(bsp, flags, breakable_count, breakable, point, vector, maximum_t, start_node, start_depth, result);
	}
	cache->ray_reused++;
	w.bsp = &k->local;
	w.flags = flags;
	w.breakable_count = breakable ? breakable_count : 0;
	w.breakable = breakable;
	w.point = point;
	w.vector = vector;
	w.result = result;
	w.last_leaf = NONE_;
	w.last_plane = NONE_;
	w.last_contents = CONTENTS_UNKNOWN;
	result->t = maximum_t < 0.f ? 0.f : maximum_t;
	result->leaf_count = 0;
	result->plane = -1;
	hit = ray3d(&w, k->start, 0.f, t);
	if (hit)
	{
		result->surface = k->surface_g[result->surface];
		if (result->plane != -1 && result->plane != NONE_)
			result->plane = k->plane_g[result->plane & LONG_MAX_] | (result->plane & LONG_MIN_);
	}
	map_list(result->leaf_count, result->leaves, k->leaf_g);
	return hit;
}

/* ---------- the descent kept */

static unsigned int memo_slot(unsigned int asker, const struct box_collbsp *bsp, int kind)
{
	unsigned int h = (asker ^ (unsigned int)(size_t)bsp ^ (unsigned int)kind * 0x9E3779B9u) * 2654435761u;

	return h >> 24; /* BOX_COLLBSP_MEMOS 256 */
}

/* the start node: the memo replayed while its nodes are taken one way, then
the tree's own nodes while they are; the memo rewritten with the whole
one-way descent. sphere: test(distance) as sphere3d's; ray: as ray3d's over
[0, t] */
static int descend(struct box_collbsp_memos *memos, unsigned int slot, unsigned int key, const struct box_collbsp *bsp,
	int kind, const float *p, const float *v, float radius, float t)
{
	int index = 0, k = 0, matched = 0, length;
	struct box_collbsp_memo *memo = &memos->memo[slot];
	struct box_collbsp_memo_step *step = memo->step;
	int have = memo->key == key && memo->bsp == bsp ? memo->length : 0;

	while (!(index & LONG_MIN_))
	{
		const float *n, *pd;
		float d;
		const int *child;
		int take, split;

		if (k < have && step[k].node == index)
		{
			n = step[k].n;
			pd = &step[k].d;
			child = step[k].child;
			matched++;
		}
		else
		{
			const struct box_collbsp_node *node = &bsp->nodes[index];

			n = node->n;
			pd = &node->d;
			child = node->child;
			have = k; /* the memo ends here */
		}
		d = *pd;
		if (kind == 0)
		{
			float distance = (p[0] * n[0] + p[1] * n[1] + p[2] * n[2]) - d;
			int second = distance < radius, first_side = distance > -radius;

			split = first_side && second;
			take = first_side;
		}
		else
		{
			float distance = (p[0] * n[0] + p[1] * n[1] + p[2] * n[2]) - d;
			float dot = v[0] * n[0] + v[1] * n[1] + v[2] * n[2];
			float distance0 = dot * 0.f + distance, distance1 = dot * t + distance;
			int back = distance0 < 0.f || distance1 < 0.f, front_reach = distance0 >= 0.f || distance1 >= 0.f;

			split = back && front_reach;
			take = front_reach;
		}
		if (split)
			break;
		if (k < BOX_COLLBSP_MEMO_STEPS)
		{
			if (k >= have)
			{
				step[k].n[0] = n[0], step[k].n[1] = n[1], step[k].n[2] = n[2];
				step[k].d = d;
				step[k].child[0] = child[0], step[k].child[1] = child[1];
				step[k].node = index;
			}
			k++;
		}
		else
			break; /* the memo's room: the walk goes on from here */
		index = child[take];
		if (index == NONE_)
			break;
	}
	length = k;
	memo->key = key;
	memo->bsp = bsp;
	memo->length = length;
	memos->replayed += (unsigned int)matched;
	memos->walked += (unsigned int)(length - matched);
	return index;
}

int box_collbsp_sphere_m(struct box_collbsp_memos *memos, unsigned int asker, const struct box_collbsp *bsp,
	short breakable_count, const unsigned int *breakable, const float center[3], float radius, int any,
	struct box_collbsp_sphere_result *result)
{
	int start;

	if (!bsp->node_count)
		return box_collbsp_sphere(bsp, breakable_count, breakable, center, radius, any, result);
	start = descend(memos, memo_slot(asker, bsp, 0), asker, bsp, 0, center, 0, radius, 0.f);
	return sphere_from(bsp, start, breakable_count, breakable, center, radius, any, result);
}

int box_collbsp_ray_m(struct box_collbsp_memos *memos, unsigned int asker, const struct box_collbsp *bsp, unsigned int flags,
	short breakable_count, const unsigned int *breakable, const float point[3], const float vector[3], float maximum_t,
	int start_node, short start_depth, struct box_collbsp_ray_result *result)
{
	float t = maximum_t < 0.f ? 0.f : (maximum_t > 1.f ? 1.f : maximum_t);
	int start;

	if (start_node != 0 || !bsp->node_count)
		return box_collbsp_ray(bsp, flags, breakable_count, breakable, point, vector, maximum_t, start_node, start_depth, result);
	start = descend(memos, memo_slot(asker, bsp, 1), asker, bsp, 1, point, vector, 0.f, t);
	return box_collbsp_ray(bsp, flags, breakable_count, breakable, point, vector, maximum_t, start, start_depth, result);
}
