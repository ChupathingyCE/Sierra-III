/*
The biped pill sweep (box_sweep.h says what it is). Every float step is
written in the engine's order (the build compiles without fused products), so
each test lands on the same side and each answer is the engine's to the bit.
*/
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "box_sweep.h"

/* three floats copied as floats: a 12-byte memcpy of float pointers is a call
to the library's memcpy at -O2 (the code census, bench-emu 43991181316: memcpy
the fifth-most missed code in a sweep), and these are all float triples */
static inline void copy3(float *to, const float *from)
{
	to[0] = from[0];
	to[1] = from[1];
	to[2] = from[2];
}
#ifdef SWEEP_DEBUG
#include <stdio.h>

int box_sweep_debug;
#define DBG(...) do { if (box_sweep_debug) printf(__VA_ARGS__); } while (0)
#else
#define DBG(...) ((void)0)
#endif

#define EPSILON 0.0001f
#define REAL_BIG 3.4028235e38f

/* the projections a prism's points are kept in: for each axis left out and
each sign, the two kept */
static const short projection_kept[3][2][2] = { { { 2, 1 }, { 1, 2 } }, { { 0, 2 }, { 2, 0 } }, { { 1, 0 }, { 0, 1 } } };

/* ---------- the arithmetic, each step as the engine takes it */

static inline float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static inline float length_squared3(const float *v) { return v[0] * v[0] + v[1] * v[1] + v[2] * v[2]; }

static inline void scale3(const float *a, float c, float *out)
{
	out[0] = c * a[0];
	out[1] = c * a[1];
	out[2] = c * a[2];
}

/* b - a */
static inline void from_to3(const float *a, const float *b, float *out)
{
	out[0] = b[0] - a[0];
	out[1] = b[1] - a[1];
	out[2] = b[2] - a[2];
}

static inline void along3(const float *p, const float *v, float t, float *out)
{
	float x = v[0] * t + p[0], y = v[1] * t + p[1], z = v[2] * t + p[2];

	out[0] = x;
	out[1] = y;
	out[2] = z;
}

static inline float normalize3(float *v)
{
	float length = sqrtf(length_squared3(v));

	if (!(EPSILON > fabsf(length - 0.f)))
		scale3(v, 1.f / length, v);
	else
		length = 0.f;
	return length;
}

static inline float normalize2(float *v)
{
	float length = sqrtf(v[0] * v[0] + v[1] * v[1]);

	if (!(EPSILON > fabsf(length - 0.f)))
	{
		float c = 1.f / length;

		v[0] = c * v[0];
		v[1] = c * v[1];
	}
	else
		length = 0.f;
	return length;
}

static inline void cross3(const float *a, const float *b, float *out)
{
	float k = a[0] * b[1] - a[1] * b[0];
	float j = a[2] * b[0] - a[0] * b[2];
	float i = a[1] * b[2] - a[2] * b[1];

	out[0] = i;
	out[1] = j;
	out[2] = k;
}

static inline float cross2(const float *a, const float *b) { return a[0] * b[1] - a[1] * b[0]; }

static inline short axis_left_out(const float *n)
{
	float i = fabsf(n[0]), j = fabsf(n[1]), k = fabsf(n[2]);

	if (k >= j && k >= i)
		return 2;
	if (j >= i)
		return 1;
	return 0;
}

static inline void project(const float *p, short axis, int sign, float *out)
{
	float x = p[projection_kept[axis][sign][0]], y = p[projection_kept[axis][sign][1]];

	out[0] = x;
	out[1] = y;
}

/* a node's matrix (scale, forward, left, up, position) on a point, a vector,
a normal and a plane */
static inline void matrix_point(const float *m, const float *p, float *out)
{
	float x = p[0], y = p[1], z = p[2];
	float rx, ry, rz;

	if (m[0] != 1.f)
	{
		x *= m[0];
		y *= m[0];
		z *= m[0];
	}
	rx = m[7] * z + m[4] * y + m[1] * x + m[10];
	ry = m[8] * z + m[5] * y + m[2] * x + m[11];
	rz = m[9] * z + m[6] * y + m[3] * x + m[12];
	out[0] = rx;
	out[1] = ry;
	out[2] = rz;
}

static inline void matrix_vector(const float *m, const float *v, float *out)
{
	float i = v[0], j = v[1], k = v[2];
	float ri, rj, rk;

	if (m[0] != 1.f)
	{
		i *= m[0];
		j *= m[0];
		k *= m[0];
	}
	ri = i * m[1] + j * m[4] + k * m[7];
	rj = i * m[2] + j * m[5] + k * m[8];
	rk = i * m[3] + j * m[6] + k * m[9];
	out[0] = ri;
	out[1] = rj;
	out[2] = rk;
}

static inline void matrix_plane(const float *m, const float *n, float d, float *out_n, float *out_d)
{
	float i = n[0], j = n[1], k = n[2];

	out_n[0] = i * m[1] + j * m[4] + k * m[7];
	out_n[1] = i * m[2] + j * m[5] + k * m[8];
	out_n[2] = i * m[3] + j * m[6] + k * m[9];
	*out_d = d * m[0] + dot3(&m[10], out_n);
}

/* the inverse of a rigid matrix with a uniform scale */
static void matrix_inverse(const float *m, float *out)
{
	if (m[0] != 0.f)
	{
		float x = -m[10], y = -m[11], z = -m[12];

		if (m[0] != 1.f)
		{
			float scale = 1.f / m[0];

			out[0] = scale;
			x *= scale;
			y *= scale;
			z *= scale;
		}
		else
			out[0] = 1.f;
		/* the rotation transposed: row r, column c is out[1 + 3 r + c] */
		out[1] = m[1];
		out[5] = m[5];
		out[9] = m[9];
		out[4] = m[2];
		out[2] = m[4];
		out[7] = m[3];
		out[3] = m[7];
		out[8] = m[6];
		out[6] = m[8];
		out[10] = (x * out[1]) + y * out[4] + (z * out[7]);
		out[11] = (x * out[2]) + y * out[5] + (z * out[8]);
		out[12] = (x * out[3]) + y * out[6] + (z * out[9]);
	}
	else
		memset(out, 0, 13 * sizeof(float));
}

/* ---------- the features */

static inline void feature_set(struct box_sweep_feature *f, int object, int surface, unsigned char flags,
	unsigned char breakable, short material)
{
	f->object = object;
	f->surface = surface;
	f->flags = flags;
	f->breakable = breakable;
	f->material = material;
}

/* a point: a sphere, and when it has height a second sphere below and the
cylinder between */
static void from_point(const float *point, float height, float width, int object, int surface, unsigned char flags,
	unsigned char breakable, short material, struct box_sweep_features *features)
{
	if (features->count[0] < BOX_SWEEP_FEATURES)
	{
		struct box_sweep_sphere *s = &features->spheres[features->count[0]++];

		feature_set(&s->f, object, surface, flags, breakable, material);
		s->center[0] = point[0];
		s->center[1] = point[1];
		s->center[2] = point[2];
		s->radius = width;
	}
	if (height > 0.0f)
	{
		float below = point[2] - height;

		if (features->count[0] < BOX_SWEEP_FEATURES)
		{
			struct box_sweep_sphere *s = &features->spheres[features->count[0]++];

			feature_set(&s->f, object, surface, flags, breakable, material);
			s->center[0] = point[0];
			s->center[1] = point[1];
			s->center[2] = below;
			s->radius = width;
		}
		if (features->count[1] < BOX_SWEEP_FEATURES)
		{
			struct box_sweep_cylinder *c = &features->cylinders[features->count[1]++];

			feature_set(&c->f, object, surface, flags, breakable, material);
			c->base[0] = point[0];
			c->base[1] = point[1];
			c->base[2] = below;
			c->height[0] = 0.0f;
			c->height[1] = 0.0f;
			c->height[2] = height;
			c->width = width;
		}
	}
}

static void prism_from_quad(struct box_sweep_prism *p, const float *n, float d, float width, float (*points)[3])
{
	int k;

	p->n[0] = n[0];
	p->n[1] = n[1];
	p->n[2] = 0.0f;
	p->d = d;
	p->height = width;
	p->axis = axis_left_out(p->n);
	p->sign = p->n[p->axis] > 0.f;
	p->point_count = 4;
	for (k = 0; k < 4; k++)
		project(points[k], p->axis, p->sign, p->points[k]);
}

/* a line (an edge): a cylinder, and when it has height a second cylinder
below and the two faces of the wall between */
static void from_line(const float *point, const float *vector, float height, float width, int object, int surface,
	unsigned char flags, unsigned char breakable, short material, struct box_sweep_features *features)
{
	if (features->count[1] < BOX_SWEEP_FEATURES)
	{
		struct box_sweep_cylinder *c = &features->cylinders[features->count[1]++];

		feature_set(&c->f, object, surface, flags, breakable, material);
		copy3(c->base, point);
		copy3(c->height, vector);
		c->width = width;
	}
	if (height > 0.0f)
	{
		float n[2];

		if (features->count[1] < BOX_SWEEP_FEATURES)
		{
			struct box_sweep_cylinder *c = &features->cylinders[features->count[1]++];

			feature_set(&c->f, object, surface, flags, breakable, material);
			c->base[0] = point[0];
			c->base[1] = point[1];
			c->base[2] = point[2] - height;
			copy3(c->height, vector);
			c->width = width;
		}
		n[0] = -vector[1];
		n[1] = vector[0];
		if (normalize2(n) != 0.0f)
		{
			float d = point[0] * n[0] + point[1] * n[1];
			float points[4][3];

			copy3(points[0], point);
			points[1][0] = point[0] + vector[0];
			points[1][1] = point[1] + vector[1];
			points[1][2] = point[2] + vector[2];
			copy3(points[2], points[1]);
			points[2][2] -= height;
			copy3(points[3], point);
			points[3][2] -= height;
			if (features->count[2] < BOX_SWEEP_FEATURES)
			{
				struct box_sweep_prism *p = &features->prisms[features->count[2]++];

				feature_set(&p->f, object, surface, flags, breakable, material);
				prism_from_quad(p, n, d, width, points);
			}
			{
				float swap[3];

				copy3(swap, points[1]);
				copy3(points[1], points[3]);
				copy3(points[3], swap);
			}
			if (features->count[2] < BOX_SWEEP_FEATURES)
			{
				struct box_sweep_prism *p = &features->prisms[features->count[2]++];
				float back[2];

				back[0] = -n[0];
				back[1] = -n[1];
				feature_set(&p->f, object, surface, flags, breakable, material);
				prism_from_quad(p, back, -d, width, points);
			}
		}
	}
}

/* a polygon (a surface): its prism, lowered by the height under a downward face */
static void from_polygon(int point_count, float (*points)[3], const float *n, float d, float height, float width,
	int object, int surface, unsigned char flags, unsigned char breakable, short material, struct box_sweep_features *features)
{
	struct box_sweep_prism *p;
	int k;

	if (point_count > BOX_SWEEP_PRISM_POINTS)
		point_count = BOX_SWEEP_PRISM_POINTS;
	if (point_count < 3 || features->count[2] >= BOX_SWEEP_FEATURES)
		return;
	p = &features->prisms[features->count[2]++];
	feature_set(&p->f, object, surface, flags, breakable, material);
	copy3(p->n, n);
	p->d = d;
	p->height = width;
	p->axis = axis_left_out(p->n);
	p->sign = p->n[p->axis] > 0.f;
	p->point_count = point_count;
	for (k = 0; k < point_count; k++)
		project(points[k], p->axis, p->sign, p->points[k]);
	if (height > 0.0f && n[2] < 0.0f)
	{
		p->d -= height * p->n[2];
		if (p->axis != 2)
		{
			int component = projection_kept[p->axis][p->sign][1] == 2;

			for (k = 0; k < p->point_count; k++)
				p->points[k][component] -= height;
		}
	}
}

/* the BSP's arrays as the engine reads them for features */
static inline int ring_continues(const struct box_collbsp *bsp, int edge, int count)
{
	return edge >= 0 && edge < bsp->edge_count && count < 8;
}

static inline int surface_valid(const struct box_collbsp *bsp, int surface)
{
	return surface >= 0 && surface < bsp->surface_count;
}

static inline int designator(short d) { return d < 0 ? (int)(0x80000000u | ((unsigned int)d & 0x7FFFu)) : d; }

static void plane_of(const struct box_collbsp *bsp, int designated, float *n, float *d)
{
	const float *plane = bsp->planes + 4 * (designated & 0x7FFFFFFF);

	if (designated & 0x80000000)
	{
		n[0] = -plane[0];
		n[1] = -plane[1];
		n[2] = -plane[2];
		*d = -plane[3];
	}
	else
	{
		copy3(n, plane);
		*d = plane[3];
	}
}

static int surface_polygon(const struct box_collbsp *bsp, int surface, float (*points)[3])
{
	int count = 0, first, edge;

	if (!surface_valid(bsp, surface))
		return 0;
	first = bsp->surfaces[surface].first_edge;
	edge = first;
	if (!ring_continues(bsp, edge, 0))
		return 0;
	do
	{
		const struct box_collbsp_edge *e = &bsp->edges[edge];
		int reverse = e->surface[1] == surface;

		copy3(points[count++], bsp->vertices[e->vertex[reverse]].p);
		edge = e->edge[reverse];
	}
	while (edge != first && ring_continues(bsp, edge, count));
	return count;
}

static void from_vertex(const struct box_collbsp *bsp, int vertex, const float *m, float height, float width, int object,
	struct box_sweep_features *features)
{
	const struct box_collbsp_vertex *v;
	const struct box_collbsp_edge *e;
	const struct box_collbsp_surface *s;
	int surface;
	float point[3];

	if (vertex < 0 || vertex >= bsp->vertex_count)
		return;
	v = &bsp->vertices[vertex];
	if (!ring_continues(bsp, v->first_edge, 0))
		return;
	e = &bsp->edges[v->first_edge];
	surface = e->surface[0];
	if (!surface_valid(bsp, surface))
		surface = e->surface[1];
	if (!surface_valid(bsp, surface))
		return;
	s = &bsp->surfaces[surface];
	if (m)
		matrix_point(m, v->p, point);
	else
		copy3(point, v->p);
	from_point(point, height, width, object, object != -1 ? -1 : surface, s->flags, s->breakable, s->material, features);
}

static void from_edge(const struct box_collbsp *bsp, int edge, const float *m, float height, float width, int object,
	struct box_sweep_features *features)
{
	const struct box_collbsp_edge *e;
	const struct box_collbsp_surface *s0, *s1;
	int d0, d1, plane0, plane1, valid;
	const float *n0, *n1;
	float vector[3], point[3], c[3];

	if (!ring_continues(bsp, edge, 0))
		return;
	e = &bsp->edges[edge];
	if (!surface_valid(bsp, e->surface[0]) || !surface_valid(bsp, e->surface[1]))
		return;
	s0 = &bsp->surfaces[e->surface[0]];
	s1 = &bsp->surfaces[e->surface[1]];
	d0 = designator(s0->designator);
	d1 = designator(s1->designator);
	if (d0 == d1)
		return;
	plane0 = d0 & 0x7FFFFFFF;
	plane1 = d1 & 0x7FFFFFFF;
	n0 = bsp->planes + 4 * plane0;
	n1 = bsp->planes + 4 * plane1;
	from_to3(bsp->vertices[e->vertex[0]].p, bsp->vertices[e->vertex[1]].p, vector);
	if (plane0 == plane1)
		valid = 1;
	else
	{
		cross3(n0, n1, c);
		valid = ((d0 & 0x80000000) != 0) == ((d1 & 0x80000000) != 0) ? dot3(c, vector) > -EPSILON : dot3(c, vector) < EPSILON;
	}
	if (!valid)
		return;
	if (m)
	{
		matrix_vector(m, vector, vector);
		matrix_point(m, bsp->vertices[e->vertex[0]].p, point);
	}
	else
		copy3(point, bsp->vertices[e->vertex[0]].p);
	from_line(point, vector, height, width, object, object == -1 ? e->surface[0] : -1, s0->flags, s0->breakable,
		s0->material, features);
}

static void from_surface(const struct box_collbsp *bsp, int surface, const float *m, float height, float width, int object,
	struct box_sweep_features *features)
{
	const struct box_collbsp_surface *s = &bsp->surfaces[surface];
	float points[8][3], n[3], d;
	int count = surface_polygon(bsp, surface, points), k;

	plane_of(bsp, designator(s->designator), n, &d);
	if (m)
	{
		for (k = 0; k < count; k++)
			matrix_point(m, points[k], points[k]);
		matrix_plane(m, n, d, n, &d);
	}
	from_polygon(count, points, n, d, height, width, object, object != -1 ? -1 : surface, s->flags, s->breakable,
		s->material, features);
}

/* the sphere's vertices, edges and surfaces, in that order, as features */
static void bsp_features(const struct box_collbsp *bsp, const struct box_collbsp_sphere_result *r, const float *m,
	float height, float width, int object, struct box_sweep_features *features)
{
	int k;

	for (k = 0; k < r->vertex_count; k++)
		from_vertex(bsp, r->vertices[k], m, height, width, object, features);
	for (k = 0; k < r->edge_count; k++)
		from_edge(bsp, r->edges[k], m, height, width, object, features);
	for (k = 0; k < r->surface_count; k++)
		from_surface(bsp, r->surfaces[k], m, height, width, object, features);
}

/* BOX_LINECOUNT: the objects' half's lookups, calls and the entries each scans
(a linear search over the world's tables) */
#ifdef BOX_LINECOUNT
unsigned long long box_sweep_lcs_calls[4], box_sweep_lcs_scanned[4];
#define LCS(kind, n) (box_sweep_lcs_calls[kind]++, box_sweep_lcs_scanned[kind] += (unsigned long long)(n))
#else
#define LCS(kind, n) ((void)0)
#endif

/* ---------- the objects' half */

static const struct box_sweep_object *object_of(const struct box_sweep_world *w, int index)
{
	int k;

	LCS(0, w->object_count);
	for (k = 0; k < w->object_count; k++)
		if (w->objects[k].index == index)
			return &w->objects[k];
	return 0;
}

static int cluster_of(const struct box_sweep_world *w, int leaf)
{
	int k;

	/* the map: one read, where the list is a search (the objects' half's
	census: ~9 lookups a sweep scanning ~170 entries on b30) */
	if (w->cluster_by_leaf && leaf >= 0 && leaf < w->cluster_by_leaf_count && w->cluster_by_leaf[leaf] != BOX_SWEEP_NO_CLUSTER)
		return w->cluster_by_leaf[leaf];
	LCS(1, w->leaf_count);
	for (k = 0; k < w->leaf_count; k++)
		if (w->leaves[k].leaf == leaf)
			return w->leaves[k].cluster;
	return -2;
}

static const struct box_sweep_cluster *cluster_members(const struct box_sweep_world *w, int cluster)
{
	int k;

	LCS(2, w->cluster_count);
	for (k = 0; k < w->cluster_count; k++)
		if (w->clusters[k].cluster == cluster)
			return &w->clusters[k];
	return 0;
}

static int mark(int *set, int *count, int most, int value)
{
	int k;

	LCS(3, *count);
	for (k = 0; k < *count; k++)
		if (set[k] == value)
			return 0;
	if (*count < most)
		set[(*count)++] = value;
	return 1;
}

static inline int in_sphere(const float *point, const float *center, float reach)
{
	float v[3];

	from_to3(point, center, v);
	return length_squared3(v) <= reach * reach;
}

/* a model's node BSPs, each walked in its node's space */
static int model_features(const struct box_sweep_world *w, const struct box_sweep_object *o, const float *center,
	float radius, float height, float width, struct box_sweep_scratch *scratch)
{
	int k;

	for (k = 0; k < o->node_count; k++)
	{
		const struct box_sweep_node *node = &w->nodes[o->first_node + k];
		float inverse[13], local[3];

		if (!node->bsp)
			return 0;
		matrix_inverse(node->m, inverse);
		matrix_point(inverse, center, local);
		if (box_collbsp_sphere(node->bsp, 0, 0, local, inverse[0] * radius, w->any, &scratch->sphere))
			bsp_features(node->bsp, &scratch->sphere, node->m, height, width, o->index, &scratch->features);
	}
	return 1;
}

/* object_get_features_in_sphere: the object, its children, then its next siblings */
static int object_features(const struct box_sweep_world *w, unsigned int flags, int index, const float *center,
	float radius, float height, float width, int ignore, struct box_sweep_scratch *scratch)
{
	do
	{
		const struct box_sweep_object *o = object_of(w, index);

		if (!o)
			return 0;
		if (index != ignore && !(o->flags & (1u << BOX_SWEEP_OBJECT_INVISIBLE)) &&
			!(o->flags & (1u << BOX_SWEEP_OBJECT_NO_COLLISIONS)) &&
			(!(o->damage_flags & (1u << BOX_SWEEP_OBJECT_DEAD)) || o->type != BOX_SWEEP_TYPE_BIPED))
		{
			float reach = o->radius;

			reach += radius;
			if (in_sphere(center, o->center, reach))
			{
				if (flags & (1u << (o->type + BOX_SWEEP_TEST_FIRST_TYPE)))
				{
					switch (o->type)
					{
					case BOX_SWEEP_TYPE_BIPED:
						if (o->seen != BOX_SWEEP_SEEN_BIPED)
							return 0;
						if ((!(flags & (1u << BOX_SWEEP_TEST_SKIP_PASSTHROUGH_BIPEDS)) ||
								!(o->biped_flags & (1u << BOX_SWEEP_BIPED_PASSES_THROUGH))) &&
							(!(flags & (1u << BOX_SWEEP_TEST_SKIP_PLAYER_BIPEDS)) || o->player == -1) &&
							(o->parent == -1 || o->seat == -1))
						{
							float base[3];

							copy3(base, o->pill_base);
							base[2] += o->pill_height;
							from_point(base, o->pill_height + height, o->pill_width + width, o->index, -1, 0, 0xFF, -1,
								&scratch->features);
						}
						break;
					case BOX_SWEEP_TYPE_VEHICLE:
					case BOX_SWEEP_TYPE_SCENERY:
					case BOX_SWEEP_TYPE_MACHINE:
					case BOX_SWEEP_TYPE_CONTROL:
						if (o->type == BOX_SWEEP_TYPE_VEHICLE && (flags & (1u << BOX_SWEEP_TEST_USE_VEHICLE_PHYSICS)))
							return 0; /* a vehicle's physics: not in v0 */
						if (o->seen == BOX_SWEEP_SEEN_MODEL)
						{
							if (!model_features(w, o, center, radius, height, width, scratch))
								return 0;
						}
						else if (o->seen != BOX_SWEEP_SEEN_NO_MODEL)
							return 0;
						break;
					}
				}
				if (o->first_child != -1 &&
					!object_features(w, flags, o->first_child, center, radius, height, width, ignore, scratch))
					return 0;
			}
		}
		index = o->next;
	}
	while (index != -1);
	return 1;
}

/* B's invalidation: the same BSP and the same breakable flags as the kept
walks were made against; a change (a surface broken, a BSP switch) is noted
for the next walks. More flags than the copy holds never match, so B walks
fresh every sweep there. */
static int level_same(const struct box_sweep_world *w, struct box_sweep_scratch *scratch)
{
	int words = (w->breakable_count + 31) >> 5;
	int same;

	if (words < 0)
		words = 0;
	if (words > BOX_SWEEP_BREAKABLE_WORDS)
	{
		scratch->kept_level = 0;
		return 0;
	}
	same = scratch->kept_level == w->level && scratch->kept_breakable_count == w->breakable_count &&
		(!words || !memcmp(scratch->kept_breakable, w->breakable, words * 4));
	if (!same)
	{
		scratch->kept_level = w->level;
		scratch->kept_breakable_count = w->breakable_count;
		if (words)
			memcpy(scratch->kept_breakable, w->breakable, words * 4);
	}
	return same;
}

/* B packed: the asker's block, or a free one given to it */
static struct box_sweep_packed *packed_block(struct box_sweep_scratch *scratch, int asker)
{
	struct box_sweep_packed *free_block = 0;
	int k;

	if (!scratch->packed_ready)
	{
		for (k = 0; k < BOX_SWEEP_PACKED_BLOCKS; k++)
			scratch->packed[k].owner = -1;
		scratch->packed_ready = 1;
	}
	for (k = 0; k < BOX_SWEEP_PACKED_BLOCKS; k++)
	{
		if (scratch->packed[k].owner == asker)
			return &scratch->packed[k];
		if (scratch->packed[k].owner == -1 && !free_block)
			free_block = &scratch->packed[k];
	}
	if (free_block)
		free_block->owner = asker;
	return free_block;
}

static void packed_release(struct box_sweep_scratch *scratch, int asker)
{
	int k;

	if (!scratch->packed_ready)
		return;
	for (k = 0; k < BOX_SWEEP_PACKED_BLOCKS; k++)
		if (scratch->packed[k].owner == asker)
			scratch->packed[k].owner = -1;
}

static void packed_release_all(struct box_sweep_scratch *scratch)
{
	int k;

	for (k = 0; k < BOX_SWEEP_PACKED_BLOCKS; k++)
		scratch->packed[k].owner = -1;
	scratch->packed_ready = 1;
}

/* B: the level's lists for this sphere from the asker's kept wider walk, a new
one kept when the sphere leaves it */
static int level_kept(const struct box_sweep_world *w, const float *center, float radius, struct box_sweep_scratch *scratch)
{
	struct box_sweep_kept *slot = 0;
	int k, oldest = 0;

	scratch->kept_clock++;
	if (!level_same(w, scratch))
	{
		/* the walks were made against a level that is no longer this one */
		memset(scratch->kept_used, 0, sizeof(scratch->kept_used));
		scratch->kept_drops++;
		packed_release_all(scratch);
	}
	for (k = 0; k < BOX_SWEEP_KEPT; k++)
	{
		if (scratch->kept[k].asker == scratch->asker && scratch->kept_used[k])
		{
			slot = &scratch->kept[k];
			break;
		}
		if (scratch->kept_used[k] < scratch->kept_used[oldest])
			oldest = k;
	}
	if (slot)
	{
		float d[3];

		from_to3(slot->center, center, d);
		if (sqrtf(length_squared3(d)) + radius > slot->radius)
			slot = 0;
		else
			scratch->kept_hits++;
	}
	if (!slot)
	{
		slot = &scratch->kept[oldest];
		for (k = 0; k < BOX_SWEEP_KEPT; k++)
			if (scratch->kept[k].asker == scratch->asker && scratch->kept_used[k])
				slot = &scratch->kept[k];
		if (scratch->kept_used[slot - scratch->kept] && slot->asker != scratch->asker)
			packed_release(scratch, slot->asker);
		slot->asker = scratch->asker;
		copy3(slot->center, center);
		slot->radius = radius + w->kept_margin;
		box_collbsp_sphere(w->level, w->breakable_count, w->breakable, center, slot->radius, 0, &slot->walk);
		slot->walk.start_node = box_collbsp_split_node(w->level, center, slot->radius);
		if (w->kept_packed)
		{
			struct box_sweep_packed *block = packed_block(scratch, scratch->asker);

			if (!block)
				scratch->packed_no_block++;
			else
			{
				int n = box_collbsp_pack(w->level, slot->walk.start_node, center, slot->radius, block->nodes,
					BOX_SWEEP_PACKED_NODES, block->leaves, BOX_SWEEP_PACKED_LEAVES, &block->leaf_count);

				block->node_count = n > 0 ? n : 0;
				if (n < 0)
					scratch->packed_too_big++;
				else
					scratch->packed_made++;
			}
		}
#ifdef SWEEP_COUNT
		{
			extern unsigned long long sweep_subtree[4];
			int nodes = 0, leaves = 0;

			box_collbsp_subtree_count(w->level, slot->walk.start_node, center, slot->radius, &nodes, &leaves);
			sweep_subtree[0]++;
			sweep_subtree[1] += (unsigned long long)nodes;
			if ((unsigned long long)nodes > sweep_subtree[2])
				sweep_subtree[2] = (unsigned long long)nodes;
			sweep_subtree[3] += (unsigned long long)leaves;
		}
#endif
		scratch->kept_walks++;
#ifdef SWEEP_COUNT
		{
			extern unsigned long long sweep_kept_sizes[5];
			const struct box_collbsp_sphere_result *r = &slot->walk;
			int n = r->surface_count + r->edge_count + r->vertex_count + r->leaf_count;

			sweep_kept_sizes[0]++;
			sweep_kept_sizes[1] += (unsigned long long)n;
			if ((unsigned long long)n > sweep_kept_sizes[2])
				sweep_kept_sizes[2] = (unsigned long long)n;
			sweep_kept_sizes[3] += (unsigned long long)r->surface_count + r->edge_count;
		}
#endif
	}
	scratch->kept_used[slot - scratch->kept] = scratch->kept_clock;
	scratch->kept_last = slot;
#ifdef SWEEP_COUNT
	{
		extern unsigned long long sweep_kept_tested[3];
		int r = box_collbsp_sphere_kept(w->level, w->breakable_count, w->breakable, &slot->walk, center, radius, &scratch->sphere);
		const struct box_collbsp_sphere_result *k = &slot->walk, *o = &scratch->sphere;

		sweep_kept_tested[0]++;
		sweep_kept_tested[1] += (unsigned long long)(k->surface_count + k->edge_count + k->vertex_count);
		sweep_kept_tested[2] += (unsigned long long)(o->surface_count + o->edge_count + o->vertex_count);
		return r;
	}
#else
	if (w->kept_packed && scratch->packed_ready)
	{
		int k, owned = 0;

		for (k = 0; k < BOX_SWEEP_PACKED_BLOCKS; k++)
			if (scratch->packed[k].owner == scratch->asker)
			{
				owned = 1;
				if (scratch->packed[k].node_count > 0)
				{
					scratch->packed_used++;
					return box_collbsp_sphere_packed(w->level, w->breakable_count, w->breakable, &slot->walk,
						scratch->packed[k].nodes, scratch->packed[k].leaves, center, radius, &scratch->sphere);
				}
			}
		if (owned)
			scratch->packed_sweep_big++;
		else
			scratch->packed_sweep_noblock++;
	}
	return box_collbsp_sphere_kept(w->level, w->breakable_count, w->breakable, &slot->walk, center, radius, &scratch->sphere);
#endif
}

/* collision_get_features_in_sphere: the level's, then the objects' in the clusters the sphere reaches */
static int features_in_sphere(const struct box_sweep_world *w, unsigned int flags, const float *center, float radius,
	float height, float width, int ignore, struct box_sweep_scratch *scratch)
{
	struct box_sweep_features *features = &scratch->features;
	unsigned int structure = flags & (1u << BOX_SWEEP_TEST_STRUCTURE);

	features->count[0] = features->count[1] = features->count[2] = 0;
	if (structure || (flags & ((1u << BOX_SWEEP_TEST_MEDIA) | (1u << BOX_SWEEP_TEST_OBJECTS))))
	{
		int objects = (flags & (1u << BOX_SWEEP_TEST_OBJECTS)) != 0;
		struct box_collbsp_sphere_result *r = &scratch->sphere;
		int leaves[BOX_COLLBSP_LIST], leaf_count, k;

		if (w->skip_objects)
			objects = 0;
		radius += 0.0625f;
		if (w->kept_margin > 0.f && !w->any ? level_kept(w, center, radius, scratch) && structure
			: box_collbsp_sphere(w->level, w->breakable_count, w->breakable, center, radius, w->any, r) && structure)
			bsp_features(w->level, r, 0, height, width, -1, features);
		/* the leaves kept apart: a model's walk below reuses the result */
		leaf_count = r->leaf_count;
		memcpy(leaves, r->leaves, sizeof(int) * (unsigned int)leaf_count);
#ifdef SWEEP_COUNT
		if (objects && w->kept_margin > 0.f && !w->any && scratch->kept_last && !w->skip_objects)
		{
			extern void sweep_leaves_census(const struct box_sweep_world *, const int *, int, const int *, int, const float *, float);

			sweep_leaves_census(w, scratch->kept_last->walk.leaves, scratch->kept_last->walk.leaf_count, leaves, leaf_count, center, radius);
		}
		scratch->kept_last = 0;
#endif
		if (w->part == 2)
			objects = 0;
		if (objects && leaf_count > 0)
		{
			unsigned int all_types = ((1u << (BOX_SWEEP_TEST_LAST_TYPE + 1)) - (1u << BOX_SWEEP_TEST_FIRST_TYPE));

			if (!(flags & all_types))
				flags |= all_types;
			scratch->marked_cluster_count = scratch->marked_object_count = 0;
			for (k = 0; k < leaf_count; k++)
			{
				int cluster = cluster_of(w, leaves[k]);
				const struct box_sweep_cluster *c;
				int m;

				if (cluster == -2)
					return 0;
				if (!mark(scratch->marked_clusters, &scratch->marked_cluster_count, BOX_COLLBSP_LIST, cluster))
					continue;
				c = cluster_members(w, cluster);
				if (!c)
					return 0;
				for (m = 0; m < c->count; m++)
				{
					const struct box_sweep_member *member = &w->members[c->first + m];
					float reach = member->radius;

					reach += radius;
					if (in_sphere(center, member->center, reach) &&
						mark(scratch->marked_objects, &scratch->marked_object_count, 512, member->object) &&
						!object_features(w, flags, member->object, center, radius, height, width, ignore, scratch))
						return 0;
				}
			}
		}
	}
	return 1;
}

/* ---------- the clip */

static int sphere_vector(const struct box_sweep_sphere *s, const float *point, const float *vector, float *t, float *n, float *d)
{
	float w[3], on[3], distance, projection, vector_squared, discriminant;
	int result = 0;

	from_to3(point, s->center, w);
	distance = length_squared3(w) - s->radius * s->radius;
	if (distance <= 0.0f)
	{
		*t = 0.0f;
		result = 1;
	}
	else
	{
		projection = dot3(w, vector);
		if (projection > 0.0f)
		{
			vector_squared = length_squared3(vector);
			discriminant = projection * projection - vector_squared * distance;
			if (discriminant >= 0.0f)
			{
				projection -= sqrtf(discriminant);
				if (projection <= vector_squared)
				{
					*t = projection / vector_squared;
					result = 1;
				}
			}
		}
	}
	if (result)
	{
		scale3(vector, *t, on);
		n[0] = on[0] - w[0];
		n[1] = on[1] - w[1];
		n[2] = on[2] - w[2];
		if (normalize3(n) == 0.0f)
		{
			n[0] = 0.0f;
			n[1] = 0.0f;
			n[2] = 1.0f;
		}
		*d = dot3(s->center, n) + s->radius;
	}
	return result;
}

static int cylinder_vector(const struct box_sweep_cylinder *c, const float *point, const float *vector, float *t, float *n, float *d)
{
	float w[3], on[3], height_squared, height_vector, height_w, vector_squared, vector_w, w_squared, width_squared;
	float a, b, cc, discriminant, inverse_a, minimum_t, maximum_t, bottom_t, top_t, height_t;

	height_squared = length_squared3(c->height);
	height_vector = dot3(c->height, vector);
	vector_squared = length_squared3(vector);
	a = height_squared * vector_squared - height_vector * height_vector;
	if (a == 0.0f)
		return 0;
	from_to3(c->base, point, w);
	vector_w = dot3(vector, w);
	height_w = dot3(c->height, w);
	w_squared = length_squared3(w);
	width_squared = c->width * c->width;
	b = height_w * height_vector - height_squared * vector_w;
	cc = (w_squared - width_squared) * height_squared - height_w * height_w;
	discriminant = b * b - a * cc;
	if (discriminant < 0.0f)
		return 0;
	discriminant = sqrtf(discriminant);
	inverse_a = 1.0f / a;
	minimum_t = (b - discriminant) * inverse_a;
	maximum_t = (b + discriminant) * inverse_a;
	if (minimum_t > 1.0f || maximum_t < 0.0f)
		return 0;
	if (minimum_t < 0.0f)
		minimum_t = 0.0f;
	if (maximum_t > 1.0f)
		maximum_t = 1.0f;
	if (height_vector != 0.0f)
	{
		bottom_t = -height_w / height_vector;
		top_t = (height_squared - height_w) / height_vector;
		if (height_vector > 0.0f)
		{
			if (minimum_t < bottom_t)
				minimum_t = bottom_t;
			maximum_t = maximum_t < top_t ? maximum_t : top_t;
		}
		else
		{
			if (minimum_t < top_t)
				minimum_t = top_t;
			maximum_t = maximum_t < bottom_t ? maximum_t : bottom_t;
		}
		if (minimum_t > maximum_t)
			return 0;
	}
	else if (height_w < 0.0f || height_w > height_squared)
		return 0;
	*t = minimum_t;
	along3(w, vector, minimum_t, on);
	height_t = -(dot3(on, c->height) / height_squared);
	along3(on, c->height, height_t, n);
	if (normalize3(n) == 0.0f)
	{
		n[0] = 1.0f;
		n[1] = 0.0f;
		n[2] = 0.0f;
	}
	*d = dot3(c->base, n) + c->width;
	return 1;
}

static int prism_vector(const struct box_sweep_prism *p, const float *point, const float *vector, float *t, float *n, float *d)
{
	float t_out = 0.0f, t_in = 1.0f;
	float distance = dot3(point, p->n) - p->d;
	float vn = p->n[2] * vector[2];
	float p3[3], v3[3], p2[2], v2[2];
	int k;

	vn += p->n[1] * vector[1];
	vn += p->n[0] * vector[0];
	if (vn != 0.0f)
	{
		float inverse = 1.0f / vn;
		float t0 = -inverse * distance;
		float t1 = -inverse * (distance - p->height);

		if (vn > 0.0f)
		{
			if (t_out < t0)
				t_out = t0;
			t_in = t_in < t1 ? t_in : t1;
		}
		else
		{
			if (t_out < t1)
				t_out = t1;
			t_in = t_in < t0 ? t_in : t0;
		}
		if (t_out > t_in)
			return 0;
	}
	else if (distance < 0.0f || distance >= p->height)
		return 0;
	along3(point, p->n, -distance, p3);
	along3(vector, p->n, -vn, v3);
	project(p3, p->axis, p->sign, p2);
	project(v3, p->axis, p->sign, v2);
	for (k = 0; k < p->point_count; k++)
	{
		int next = k + 1 >= p->point_count ? 0 : k + 1;
		const float *p0 = p->points[k], *p1 = p->points[next];
		float edge[2], x[2], vw, wx;

		edge[0] = p1[0] - p0[0];
		edge[1] = p1[1] - p0[1];
		x[0] = p2[0] - p0[0];
		x[1] = p2[1] - p0[1];
		vw = cross2(v2, edge);
		wx = cross2(edge, x);
		if (vw != 0.0f)
		{
			float t_edge = wx / vw;

			if (vw < 0.0f)
			{
				if (t_out < t_edge)
					t_out = t_edge;
			}
			else
			{
				if (t_in > t_edge)
					t_in = t_edge;
			}
			if (t_out > t_in)
				return 0;
		}
		else if (wx < 0.0f)
			return 0;
	}
	*t = t_out;
	copy3(n, p->n);
	*d = p->d + p->height;
	return 1;
}

/* collision_features_test_vector: the nearest feature the move enters, facing it */
static int features_vector(const struct box_sweep_features *features, const float *point, const float *vector,
	struct box_sweep_plane *collision)
{
	const struct box_sweep_feature *nearest_feature = 0;
	float nearest_t = REAL_BIG, nearest_n[3], nearest_d = 0.f;
	int type, k;

	for (type = 0; type < 3; type++)
		for (k = 0; k < features->count[type]; k++)
		{
			float t, n[3], d;
			int hit = type == 0 ? sphere_vector(&features->spheres[k], point, vector, &t, n, &d)
				: type == 1 ? cylinder_vector(&features->cylinders[k], point, vector, &t, n, &d)
				: prism_vector(&features->prisms[k], point, vector, &t, n, &d);

			if (hit && nearest_t > t && dot3(vector, n) < -EPSILON)
			{
				nearest_t = t;
				nearest_feature = type == 0 ? &features->spheres[k].f : type == 1 ? &features->cylinders[k].f
					: &features->prisms[k].f;
				copy3(nearest_n, n);
				nearest_d = d;
			}
		}
	if (nearest_feature)
	{
		collision->t = nearest_t;
		along3(point, vector, nearest_t, collision->point);
		copy3(collision->n, nearest_n);
		collision->d = nearest_d;
		collision->object = nearest_feature->object;
		collision->surface = nearest_feature->surface;
		collision->flags = nearest_feature->flags;
		collision->breakable = nearest_feature->breakable;
		collision->material = nearest_feature->material;
		return 1;
	}
	collision->t = 1.0f;
	collision->point[0] = point[0] + vector[0];
	collision->point[1] = point[1] + vector[1];
	collision->point[2] = point[2] + vector[2];
	return 0;
}

static inline void clip_velocity_to_plane(const float *velocity, const float *n, float *out)
{
	float distance = -dot3(velocity, n);
	float i = n[0] * distance + velocity[0], j = n[1] * distance + velocity[1], k = n[2] * distance + velocity[2];

	out[0] = i;
	out[1] = j;
	out[2] = k;
}

static inline void clip_position_to_plane(const float *position, const float *n, float d, float *out)
{
	along3(position, n, -(dot3(position, n) - d), out);
}

static inline void clip_position_to_line(const float *position, const float *point, const float *vector, float *out)
{
	float offset[3];

	from_to3(point, position, offset);
	along3(point, vector, dot3(offset, vector) / length_squared3(vector), out);
}

static inline void clip_velocity_to_line(const float *velocity, const float *vector, float *out)
{
	scale3(vector, dot3(velocity, vector) / length_squared3(vector), out);
}

static int line_from_planes(const struct box_sweep_plane *a, const struct box_sweep_plane *b, float *point, float *direction)
{
	float local[3], determinant, distance, inverse;

	cross3(a->n, b->n, direction);
	determinant = length_squared3(direction);
	if (fabsf(determinant) < EPSILON)
		return 0;
	cross3(b->n, direction, local);
	distance = a->d;
	point[0] = local[0] * distance;
	point[1] = local[1] * distance;
	point[2] = local[2] * distance;
	cross3(direction, a->n, local);
	distance = b->d;
	inverse = 1.0f / determinant;
	point[0] = (local[0] * distance + point[0]) * inverse;
	point[1] = (local[1] * distance + point[1]) * inverse;
	point[2] = (local[2] * distance + point[2]) * inverse;
	return 1;
}

static int point_from_planes(const struct box_sweep_plane *a, const struct box_sweep_plane *b, const struct box_sweep_plane *c,
	float *point)
{
	float cross[3], determinant, distance;

	cross3(a->n, b->n, cross);
	determinant = dot3(cross, c->n);
	if (fabsf(determinant) < EPSILON)
		return 0;
	cross3(b->n, c->n, cross);
	distance = a->d;
	point[0] = cross[0] * distance;
	point[1] = cross[1] * distance;
	point[2] = cross[2] * distance;
	cross3(c->n, a->n, cross);
	distance = b->d;
	point[0] = cross[0] * distance + point[0];
	point[1] = cross[1] * distance + point[1];
	point[2] = cross[2] * distance + point[2];
	cross3(a->n, b->n, cross);
	distance = c->d;
	determinant = 1.0f / determinant;
	point[0] = (cross[0] * distance + point[0]) * determinant;
	point[1] = (cross[1] * distance + point[1]) * determinant;
	point[2] = (cross[2] * distance + point[2]) * determinant;
	return 1;
}

static inline int still(const float *v)
{
	return fabsf(v[0]) < EPSILON && fabsf(v[1]) < EPSILON && fabsf(v[2]) < EPSILON;
}

/* collision_move_point: the move clipped at each feature it meets, against up
to three planes at once (a plane, the line two make, the point three make) */
static int move_point(const struct box_sweep_features *features, const float *old_position, const float *old_velocity,
	int most, struct box_sweep_answer *answer)
{
	struct box_sweep_plane *collisions = answer->planes;
	int count = 0, clip_count = 0, clip[3];
	float position[3], velocity[3], clipped_position[3], clipped_velocity[3];
	float clip_n[3] = { 0, 0, 0 }, clip_d = 0.f, line_point[3] = { 0, 0, 0 }, line_vector[3] = { 0, 0, 0 }, clip_point[3];

	copy3(velocity, old_velocity);
	copy3(clipped_position, old_position);
	copy3(clipped_velocity, old_velocity);
	while (!still(clipped_velocity))
	{
		struct box_sweep_plane *collision = &collisions[count];

		DBG("  iter %d: pos %.9g %.9g %.9g vel %a %a %a\n", count, clipped_position[0], clipped_position[1], clipped_position[2],
			clipped_velocity[0], clipped_velocity[1], clipped_velocity[2]);

		if (count >= BOX_SWEEP_FEATURES)
			return -1;
		if (features_vector(features, clipped_position, clipped_velocity, collision))
		{
			int new_clip[3], new_count;
			float s = 1.0f - collision->t;

			count++;
			copy3(position, collision->point);
			velocity[0] = s * velocity[0];
			velocity[1] = s * velocity[1];
			velocity[2] = s * velocity[2];
			new_count = 1;
			new_clip[0] = count - 1;
			copy3(clip_n, collisions[new_clip[0]].n);
			clip_d = collisions[new_clip[0]].d;
			clip_velocity_to_plane(velocity, clip_n, clipped_velocity);
			clip_position_to_plane(position, clip_n, clip_d, clipped_position);
			if (clip_count > 0)
			{
				if (dot3(clipped_velocity, collisions[clip[0]].n) < -EPSILON &&
					line_from_planes(&collisions[new_clip[0]], &collisions[clip[0]], line_point, line_vector))
				{
					new_count = 2;
					new_clip[1] = clip[0];
					clip_velocity_to_line(velocity, line_vector, clipped_velocity);
					clip_position_to_line(position, line_point, line_vector, clipped_position);
					if (clip_count > 1 && dot3(clipped_velocity, collisions[clip[1]].n) < -EPSILON &&
						point_from_planes(&collisions[new_clip[0]], &collisions[new_clip[1]], &collisions[clip[1]], clip_point))
					{
						new_count = 3;
						new_clip[2] = clip[1];
						clipped_velocity[0] = 0.0f;
						clipped_velocity[1] = 0.0f;
						clipped_velocity[2] = 0.0f;
						copy3(clipped_position, clip_point);
					}
				}
				else if (clip_count > 1 && dot3(clipped_velocity, collisions[clip[1]].n) < -EPSILON &&
					line_from_planes(&collisions[new_clip[0]], &collisions[clip[1]], line_point, line_vector))
				{
					new_count = 2;
					new_clip[1] = clip[1];
					clip_velocity_to_line(velocity, line_vector, clipped_velocity);
					clip_position_to_line(position, line_point, line_vector, clipped_position);
				}
			}
			clip_count = new_count;
			memcpy(clip, new_clip, sizeof(int) * (unsigned int)clip_count);
		}
		else
		{
			copy3(clipped_position, collision->point);
			break;
		}
		if (count >= most)
			break;
	}
	DBG("  end: count %d clip %d still %d vel %a %a %a\n", count, clip_count, still(clipped_velocity), clipped_velocity[0], clipped_velocity[1], clipped_velocity[2]);
	copy3(answer->position, clipped_position);
	switch (clip_count)
	{
	case 0:
		copy3(answer->velocity, old_velocity);
		break;
	case 1:
		clip_velocity_to_plane(old_velocity, clip_n, answer->velocity);
		break;
	case 2:
		clip_velocity_to_line(old_velocity, line_vector, answer->velocity);
		break;
	default:
		answer->velocity[0] = 0.0f;
		answer->velocity[1] = 0.0f;
		answer->velocity[2] = 0.0f;
		break;
	}
	/* held against two or three planes: one more collision, the floor they
	make (the crease's or the corner's), for the asker's ground test */
	if (clip_count > 1 && count < most)
	{
		struct box_sweep_plane *floor = &collisions[count++];
		const struct box_sweep_plane *last = &collisions[clip[clip_count - 1]];
		float lowest = 0.0f;
		int steepest = -1, k;
		static const float up[3] = { 0.0f, 0.0f, 1.0f };

		floor->t = last->t;
		copy3(floor->point, last->point);
		floor->object = -1;
		floor->surface = -1;
		floor->flags = 0;
		floor->breakable = 0;
		floor->material = -1;
		for (k = 0; k < clip_count; k++)
			if (collisions[clip[k]].n[2] < lowest)
			{
				lowest = collisions[clip[k]].n[2];
				steepest = k;
			}
		if (clip_count == 2)
		{
			if (steepest != -1)
			{
				if (steepest == 0)
					cross3(line_vector, collisions[clip[0]].n, floor->n);
				else
					cross3(collisions[clip[steepest]].n, line_vector, floor->n);
			}
			else
				along3(up, line_vector, -(line_vector[2] / length_squared3(line_vector)), floor->n);
			if (normalize3(floor->n) != 0.0f)
				floor->d = dot3(line_point, floor->n);
			else
				count--;
		}
		else if (steepest != -1)
		{
			const float *n = collisions[clip[steepest]].n;

			along3(up, n, -n[2], floor->n);
			if (normalize3(floor->n) != 0.0f)
				floor->d = dot3(clip_point, floor->n);
			else
				count--;
		}
		else
		{
			copy3(floor->n, up);
			floor->d = dot3(clip_point, floor->n);
		}
	}
	return count;
}

/* ---------- the sweep */

int box_sweep(const struct box_sweep_world *world, const struct box_sweep_query *query,
	struct box_sweep_scratch *scratch, struct box_sweep_answer *answer)
{
	const float *p = query->position, *v = query->velocity;
	float center[3], reach;

	center[0] = p[0] + v[0] * 0.5f;
	center[1] = p[1] + v[1] * 0.5f;
	center[2] = query->height * 0.5f + (p[2] + v[2] * 0.5f);
	reach = sqrtf(length_squared3(v)) * 0.5f + query->height * 0.5f + query->radius;
	scratch->asker = query->ignore;
	if (!features_in_sphere(world, query->flags, center, reach, query->height, query->radius, query->ignore, scratch))
		return -1;
	if (world->part)
		return answer->count = 0;
#ifdef BOX_LINECOUNT
	{
		/* the scratch's writes (the lead 18:3x (2)): each kind's records a sweep
		makes, and the lines they cover */
		extern unsigned long long box_sweep_lc_records[3], box_sweep_lc_lines[3], box_sweep_lc_sweeps;
		const unsigned int size[3] = { sizeof(struct box_sweep_sphere), sizeof(struct box_sweep_cylinder), sizeof(struct box_sweep_prism) };
		int k;

		box_sweep_lc_sweeps++;
		for (k = 0; k < 3; k++)
		{
			box_sweep_lc_records[k] += (unsigned long long)scratch->features.count[k];
			box_sweep_lc_lines[k] += ((unsigned long long)scratch->features.count[k] * size[k] + 63) / 64;
		}
	}
#endif
	if (scratch->features.count[0] || scratch->features.count[1] || scratch->features.count[2])
		return answer->count = move_point(&scratch->features, p, v, query->most, answer);
	answer->position[0] = p[0] + v[0];
	answer->position[1] = p[1] + v[1];
	answer->position[2] = p[2] + v[2];
	copy3(answer->velocity, v);
	return answer->count = 0;
}
