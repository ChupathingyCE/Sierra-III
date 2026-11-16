/* The biped pill sweep: its adapter (docs/box_collision.md §9). The only code
that knows both the box's tables (psp/src/box_sweep.h) and the capture's
records (psp_fixups.py, FIXES_CAPTURE_SWEEP):

  static records: each BSP the sweeps met (the level's and the models'), as
      collbsp's capture keeps them: key = the BSP's address, u32 8 then for
      each of its eight arrays its count, element size and key; each array by
      its address
  entity (kind 1, a sweep), input: u32 flags, f32 position[3], velocity[3],
      height, radius, s32 ignored object, s32 most collisions, u32 the sphere
      walk's early-out, u32 the objects' debug skip, u32 the level BSP's key,
      u32 breakable count, u32 breakable flag bytes; the flags' bytes; then the
      walk's records, each a u32 tag and its words:
        1 leaf: s32 leaf, s32 cluster
        2 cluster: s32 cluster (the objects its walk gave follow as 3s)
        3 a cluster's object: s32 index, f32 centre[3], radius (its sphere copy)
        4 object: s32 index, u32 flags, u32 damage flags, s32 type, f32 centre[3],
          radius, s32 parent, next, first child
        5 biped: s32 index, u32 biped flags, s32 player, s32 seat, f32 pill
          base[3], height, width
        6 model: s32 index, s32 node count (its nodes that reach a BSP follow as 7s)
        7 node: u32 node, u32 BSP key, f32 matrix[13]
        8 no model: s32 index
        9 a vehicle's physics: s32 index
  output: s32 count, f32 position[3], velocity[3], then count collision planes
      of 44 bytes (t, point[3], plane[4], object, surface, flags, breakable,
      material)
*/
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#ifdef SWEEP_DEBUG
#include <stdio.h>
#endif
#include "../box.h"
#include "box_sweep.h"

static const char *const kinds[] = { "none", "sweep", 0 };

static const unsigned int element_sizes[8] = { 12, 16, 8, 4, 20, 8, 12, 16 };

static struct bsp_entry { unsigned int key; struct box_collbsp bsp; } *bsps;
static unsigned int bsp_count;

static const struct box_static *find(const struct box_static *statics, unsigned int count, unsigned int key)
{
	unsigned int k;

	for (k = 0; k < count; k++)
		if (statics[k].key == key)
			return &statics[k];
	return 0;
}

static int form, part;
static float margin;
static struct box_sweep_kept kept_saved[BOX_SWEEP_KEPT];
static unsigned int kept_used_saved[BOX_SWEEP_KEPT], kept_clock_saved, b_last_tick;
static const struct box_collbsp *kept_level_saved;
static short kept_breakable_count_saved;
static unsigned int kept_breakable_saved[BOX_SWEEP_BREAKABLE_WORDS];
static int b_have_tick;

static int sweep_setup(const struct box_static *statics, unsigned int count)
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
		if (form && !box_collbsp_make_bounds(b))
			return -1;
		if (form)
			owned += (unsigned int)counts[5] * 16u;
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

/* a sweep's tables, filed from its records; the arrays are the tick's and
grow as a tick needs */
struct sweep
{
	struct box_sweep_world world;
	struct box_sweep_query query;
	unsigned int breakable_copy[64];
	int bad;          /* records the adapter couldn't read: the sweep isn't run */
	int unsupported;  /* a vehicle's physics: not in v0 */
	unsigned int output_length;
	unsigned int *out;
};

static struct sweep *sweeps;
static unsigned int sweep_room;
static unsigned int *outputs;
static unsigned int output_room;
#define POOL(type, name) static type *name; static unsigned int name##_count, name##_room
POOL(struct box_sweep_leaf, leaves);
POOL(struct box_sweep_cluster, clusters);
POOL(struct box_sweep_member, members);
POOL(struct box_sweep_object, objects);
POOL(struct box_sweep_node, nodes);

static void *grow(void *pool, unsigned int *room, unsigned int need, unsigned int size)
{
	void *bigger;

	if (need <= *room)
		return pool;
	bigger = realloc(pool, (size_t)(need * 2) * size);
	if (!bigger)
		return 0;
	*room = need * 2;
	return bigger;
}

static struct box_sweep_scratch scratch;
#ifdef SWEEP_COUNT
static unsigned int count_tick;
static int count_pass;
#endif
static struct box_sweep_answer answer;
static unsigned int sweeps_run, sweeps_unsupported, sweeps_bad;

static struct box_sweep_object *object_entry(struct sweep *s, unsigned int first, int index)
{
	unsigned int k;

	for (k = first; k < objects_count; k++)
		if (objects[k].index == index)
			return &objects[k];
	(void)s;
	return 0;
}

static void sweep_tick(const struct box_tick *tick)
{
	unsigned int k, total = 0, at = 0;

	if (tick->entities > sweep_room)
	{
		free(sweeps);
		sweep_room = tick->entities * 2;
		sweeps = calloc(sweep_room, sizeof(*sweeps));
	}
	for (k = 0; k < tick->entities; k++)
		total += tick->entity[k].output_length / 4 + 1;
	if (total > output_room)
	{
		free(outputs);
		output_room = total * 2;
		outputs = calloc(output_room, sizeof(*outputs));
	}
	/* B's kept walks: each pass of a tick starts from what the ticks before
	left; a walk over the capture again starts with none */
	if (margin > 0.f)
	{
		if (b_have_tick && tick->tick < b_last_tick)
		{
			memset(scratch.kept, 0, sizeof(scratch.kept));
			memset(scratch.kept_used, 0, sizeof(scratch.kept_used));
			scratch.kept_clock = 0;
			scratch.kept_level = 0;
		}
		if (b_have_tick && tick->tick == b_last_tick)
		{
			memcpy(scratch.kept, kept_saved, sizeof(kept_saved));
			memcpy(scratch.kept_used, kept_used_saved, sizeof(kept_used_saved));
			scratch.kept_clock = kept_clock_saved;
			scratch.kept_level = kept_level_saved;
			scratch.kept_breakable_count = kept_breakable_count_saved;
			memcpy(scratch.kept_breakable, kept_breakable_saved, sizeof(kept_breakable_saved));
		}
		else
		{
			memcpy(kept_saved, scratch.kept, sizeof(kept_saved));
			memcpy(kept_used_saved, scratch.kept_used, sizeof(kept_used_saved));
			kept_clock_saved = scratch.kept_clock;
			kept_level_saved = scratch.kept_level;
			kept_breakable_count_saved = scratch.kept_breakable_count;
			memcpy(kept_breakable_saved, scratch.kept_breakable, sizeof(kept_breakable_saved));
		}
		b_have_tick = 1;
		b_last_tick = tick->tick;
	}
	leaves_count = clusters_count = members_count = objects_count = nodes_count = 0;
#ifdef SWEEP_COUNT
	count_pass = tick->tick == count_tick;
	count_tick = tick->tick;
#endif
	/* two passes: count each table's rows, then file them, so the tables
	don't move under the sweeps' pointers */
	{
		unsigned int need[5] = { 0, 0, 0, 0, 0 };

		for (k = 0; k < tick->entities; k++)
		{
			const struct box_entity *e = &tick->entity[k];
			unsigned int pos = 64, flag_bytes;

			if (e->input_length < 64)
				continue;
			memcpy(&flag_bytes, e->input + 60, 4);
			pos += (flag_bytes + 3u) & ~3u;
			while (pos + 4 <= e->input_length)
			{
				unsigned int tag;

				memcpy(&tag, e->input + pos, 4);
				pos += 4;
				switch (tag)
				{
				case 1: need[0]++; pos += 8; break;
				case 2: need[1]++; pos += 4; break;
				case 3: need[2]++; pos += 20; break;
				case 4: need[3]++; pos += 44; break;
				case 5: pos += 36; break;
				case 6: pos += 8; break;
				case 7: need[4]++; pos += 60; break;
				case 8: pos += 4; break;
				case 9: pos += 4; break;
				default: pos = e->input_length; break;
				}
			}
		}
		leaves = grow(leaves, &leaves_room, need[0] + 1, sizeof(*leaves));
		clusters = grow(clusters, &clusters_room, need[1] + 1, sizeof(*clusters));
		members = grow(members, &members_room, need[2] + 1, sizeof(*members));
		objects = grow(objects, &objects_room, need[3] + 1, sizeof(*objects));
		nodes = grow(nodes, &nodes_room, need[4] + 1, sizeof(*nodes));
	}
	for (k = 0; k < tick->entities; k++)
	{
		const struct box_entity *e = &tick->entity[k];
		struct sweep *s = &sweeps[k];
		unsigned int head[16], pos, flag_bytes;
		unsigned int first_leaf = leaves_count, first_cluster = clusters_count, first_object = objects_count;
		struct box_sweep_cluster *open_cluster = 0;
		struct box_sweep_object *open_model = 0;

		memset(s, 0, offsetof(struct sweep, breakable_copy));
		s->bad = s->unsupported = 0;
		s->out = outputs + at;
		s->output_length = e->output_length;
		at += (e->output_length + 3) / 4;
		if (!leaves || !clusters || !members || !objects || !nodes || e->kind != 1 || e->input_length < sizeof(head))
		{
			s->bad = 1;
			continue;
		}
		memcpy(head, e->input, sizeof(head));
		s->query.flags = head[0];
		memcpy(s->query.position, &head[1], 12);
		memcpy(s->query.velocity, &head[4], 12);
		memcpy(&s->query.height, &head[7], 4);
		memcpy(&s->query.radius, &head[8], 4);
		s->query.ignore = (int)head[9];
		s->query.most = (int)head[10];
		s->world.any = (int)head[11];
		s->world.part = part;
		s->world.kept_margin = margin;
		s->world.skip_objects = (int)head[12];
		s->world.level = bsp_of(head[13]);
		s->world.breakable_count = (short)head[14];
		flag_bytes = head[15];
		if (!s->world.level || flag_bytes > sizeof(s->breakable_copy) || e->input_length < sizeof(head) + flag_bytes)
		{
			s->bad = 1;
			continue;
		}
		memcpy(s->breakable_copy, e->input + sizeof(head), flag_bytes);
		s->world.breakable = s->breakable_copy;
		pos = sizeof(head) + ((flag_bytes + 3u) & ~3u);
		while (pos + 4 <= e->input_length && !s->bad)
		{
			unsigned int tag, w[16];

			memcpy(&tag, e->input + pos, 4);
			pos += 4;
			switch (tag)
			{
			case 1: /* leaf */
				memcpy(w, e->input + pos, 8);
				pos += 8;
				leaves[leaves_count].leaf = (int)w[0];
				leaves[leaves_count].cluster = (int)w[1];
				leaves_count++;
				break;
			case 2: /* cluster */
				memcpy(w, e->input + pos, 4);
				pos += 4;
				open_cluster = &clusters[clusters_count++];
				open_cluster->cluster = (int)w[0];
				open_cluster->first = (int)members_count;
				open_cluster->count = 0;
				break;
			case 3: /* a cluster's object */
				memcpy(w, e->input + pos, 20);
				pos += 20;
				if (!open_cluster)
				{
					s->bad = 1;
					break;
				}
				members[members_count].object = (int)w[0];
				memcpy(members[members_count].center, &w[1], 12);
				memcpy(&members[members_count].radius, &w[4], 4);
				members_count++;
				open_cluster->count++;
				break;
			case 4: /* object */
			{
				struct box_sweep_object *o;

				memcpy(w, e->input + pos, 44);
				pos += 44;
				o = object_entry(s, first_object, (int)w[0]);
				if (!o)
				{
					o = &objects[objects_count++];
					memset(o, 0, sizeof(*o));
					o->first_node = -1;
				}
				o->index = (int)w[0];
				o->flags = w[1];
				o->damage_flags = w[2];
				o->type = (int)w[3];
				memcpy(o->center, &w[4], 12);
				memcpy(&o->radius, &w[7], 4);
				o->parent = (int)w[8];
				o->next = (int)w[9];
				o->first_child = (int)w[10];
				break;
			}
			case 5: /* biped */
			{
				struct box_sweep_object *o;

				memcpy(w, e->input + pos, 36);
				pos += 36;
				o = object_entry(s, first_object, (int)w[0]);
				if (!o)
				{
					s->bad = 1;
					break;
				}
				o->seen = BOX_SWEEP_SEEN_BIPED;
				o->biped_flags = w[1];
				o->player = (int)w[2];
				o->seat = (int)w[3];
				memcpy(o->pill_base, &w[4], 12);
				memcpy(&o->pill_height, &w[7], 4);
				memcpy(&o->pill_width, &w[8], 4);
				break;
			}
			case 6: /* model */
				memcpy(w, e->input + pos, 8);
				pos += 8;
				open_model = object_entry(s, first_object, (int)w[0]);
				if (!open_model)
				{
					s->bad = 1;
					break;
				}
				/* an object met twice in a sweep (two clusters) lists its nodes again: the last list stands */
				open_model->seen = BOX_SWEEP_SEEN_MODEL;
				open_model->first_node = (int)nodes_count;
				open_model->node_count = 0;
				break;
			case 7: /* node */
				memcpy(w, e->input + pos, 60);
				pos += 60;
				if (!open_model)
				{
					s->bad = 1;
					break;
				}
				nodes[nodes_count].bsp = bsp_of(w[1]);
				memcpy(nodes[nodes_count].m, &w[2], 52);
				nodes_count++;
				open_model->node_count++;
				break;
			case 8: /* no model */
			{
				struct box_sweep_object *o;

				memcpy(w, e->input + pos, 4);
				pos += 4;
				o = object_entry(s, first_object, (int)w[0]);
				if (!o)
				{
					s->bad = 1;
					break;
				}
				o->seen = BOX_SWEEP_SEEN_NO_MODEL;
				break;
			}
			case 9: /* a vehicle's physics */
				pos += 4;
				s->unsupported = 1;
				break;
			default:
				s->bad = 1;
				break;
			}
		}
		s->world.leaves = leaves + first_leaf;
		s->world.leaf_count = (int)(leaves_count - first_leaf);
		s->world.clusters = clusters + first_cluster;
		s->world.cluster_count = (int)(clusters_count - first_cluster);
		s->world.members = members;
		s->world.objects = objects + first_object;
		s->world.object_count = (int)(objects_count - first_object);
		s->world.nodes = nodes;
	}
}

#ifdef SWEEP_COUNT
/* a desktop count of B's reach (box_collision.md Open 2): sweeps whose query
(position, velocity, size, flags) is the same as their asker's last, and of
those whose level sphere is the same */
#include <stdio.h>
#include <math.h>
static struct { int asker; unsigned int tick; struct box_sweep_query q; } last_seen[64];
static unsigned long long n_sweeps, n_same_query, n_same_still, n_still, n_zero_move;
static void count_report(void) __attribute__((destructor));
static void count_report(void)
{
	if (n_sweeps)
		printf("sweep count: %llu sweeps; the asker's query unchanged from its last sweep %.1f%%; at rest (velocity only gravity's step) %.1f%%, of them unchanged %.1f%%\n",
			n_sweeps, 100.0 * n_same_query / n_sweeps, 100.0 * n_still / n_sweeps, n_still ? 100.0 * n_same_still / n_still : 0.0);
}
/* stage 3's census: each asker's level walk widened by a margin, kept; a later
sweep whose sphere lies inside it could test only those candidates. Counted:
how often the sphere lies inside (by margin), and whether its own walk's lists
are the kept lists' subsequences in order (so a filter could give them exactly) */
#define MARGINS 3
static const float margins[MARGINS] = { 0.25f, 0.5f, 1.0f };
static struct kept { int asker; float c[3], r; struct box_collbsp_sphere_result res; } kept[MARGINS][64];
static unsigned long long n_level, n_inside[MARGINS], n_ordered[MARGINS];
static struct box_collbsp_sphere_result own, wide;
static int subsequence(const int *a, int na, const int *b, int nb)
{
	int i = 0, j = 0;

	for (; i < na; i++)
	{
		while (j < nb && b[j] != a[i])
			j++;
		if (j == nb)
			return 0;
		j++;
	}
	return 1;
}
static void stage3_report(void) __attribute__((destructor));
static void stage3_report(void)
{
	int m;

	if (scratch.kept_hits + scratch.kept_walks)
		printf("sweepb count: margin %.2f: %u level spheres from a kept walk, %u walks kept (%.1f%% from a kept walk; the bench's passes counted each)\n",
			margin, scratch.kept_hits, scratch.kept_walks, 100.0 * scratch.kept_hits / (scratch.kept_hits + scratch.kept_walks));
		printf("sweepb count: %u drops of every kept walk (a BSP switch or a breakable surface broken)\n", scratch.kept_drops);

	if (!n_level)
		return;
	for (m = 0; m < MARGINS; m++)
		printf("stage3 count: margin %.2f: %llu of %llu sweeps (%.1f%%) inside their asker's kept walk; of them %.1f%% with every list a subsequence in order\n",
			margins[m], n_inside[m], n_level, 100.0 * n_inside[m] / n_level, n_inside[m] ? 100.0 * n_ordered[m] / n_inside[m] : 0.0);
}
static void stage3_count(const struct sweep *s)
{
	const float *p = s->query.position, *v = s->query.velocity;
	float c[3], r;
	int m, k;

	c[0] = p[0] + v[0] * 0.5f;
	c[1] = p[1] + v[1] * 0.5f;
	c[2] = s->query.height * 0.5f + (p[2] + v[2] * 0.5f);
	r = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) * 0.5f + s->query.height * 0.5f + s->query.radius + 0.0625f;
	box_collbsp_sphere(s->world.level, s->world.breakable_count, s->world.breakable, c, r, s->world.any, &own);
	n_level++;
	for (m = 0; m < MARGINS; m++)
	{
		struct kept *slot = 0;

		for (k = 0; k < 64; k++)
			if (kept[m][k].asker == s->query.ignore || (!slot && !kept[m][k].asker))
			{
				slot = &kept[m][k];
				if (kept[m][k].asker == s->query.ignore)
					break;
			}
		if (!slot)
			slot = &kept[m][0];
		if (slot->asker == s->query.ignore)
		{
			float d[3] = { c[0] - slot->c[0], c[1] - slot->c[1], c[2] - slot->c[2] };

			if (sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + r <= slot->r)
			{
				n_inside[m]++;
				if (subsequence(own.surfaces, own.surface_count, slot->res.surfaces, slot->res.surface_count) &&
					subsequence(own.edges, own.edge_count, slot->res.edges, slot->res.edge_count) &&
					subsequence(own.vertices, own.vertex_count, slot->res.vertices, slot->res.vertex_count) &&
					subsequence(own.leaves, own.leaf_count, slot->res.leaves, slot->res.leaf_count))
					n_ordered[m]++;
				continue;
			}
		}
		/* a new kept walk at the widened radius */
		box_collbsp_sphere(s->world.level, s->world.breakable_count, s->world.breakable, c, r + margins[m], s->world.any, &wide);
		slot->asker = s->query.ignore;
		memcpy(slot->c, c, 12);
		slot->r = r + margins[m];
		slot->res = wide;
	}
}

static void sweep_count(const struct sweep *s)
{
	stage3_count(s);
	int k, free_slot = -1, still;

	still = s->query.velocity[0] == 0.0f && s->query.velocity[1] == 0.0f;
	n_sweeps++;
	n_still += still;
	for (k = 0; k < 64; k++)
	{
		if (last_seen[k].asker == s->query.ignore && last_seen[k].tick)
		{
			if (!memcmp(&last_seen[k].q, &s->query, sizeof(s->query)))
			{
				n_same_query++;
				n_same_still += still;
			}
			last_seen[k].q = s->query;
			last_seen[k].tick = count_tick;
			return;
		}
		if (free_slot < 0 && !last_seen[k].tick)
			free_slot = k;
	}
	if (free_slot >= 0)
	{
		last_seen[free_slot].asker = s->query.ignore;
		last_seen[free_slot].tick = count_tick;
		last_seen[free_slot].q = s->query;
	}
}
#endif

static __attribute__((noinline)) void sweep_run(int i)
{
	struct sweep *s = &sweeps[i];
	unsigned int *out = s->out, used, room = s->output_length / 4;
	int count, k;

	if (s->bad || s->unsupported)
	{
		for (used = 0; used < room; used++)
			out[used] = 0;
		return;
	}
#ifdef SWEEP_COUNT
	if (!count_pass)
		sweep_count(s);
#endif
	count = box_sweep(&s->world, &s->query, &scratch, &answer);
	used = 0;
	if (room > 0)
		out[used++] = (unsigned int)count;
	if (room >= 7)
	{
		memcpy(&out[used], answer.position, 12);
		memcpy(&out[used + 3], answer.velocity, 12);
		used += 6;
	}
	for (k = 0; k < count && used + 11 <= room; k++)
	{
		memcpy(&out[used], &answer.planes[k], 44);
		used += 11;
	}
	for (; used < room; used++)
		out[used] = 0;
}

static void sweep_output(int i, const void **bytes, unsigned int *length)
{
	*bytes = sweeps[i].out;
	*length = sweeps[i].output_length;
}

/* exact, word for word; a sweep the box can't take (a vehicle's physics) or
whose records didn't read is reported as such, by its element */
static int sweep_compare(int i, const void *got, const void *want, unsigned int length, float *largest_absolute,
	float *largest_relative, unsigned int *element)
{
	const unsigned int *a = (const unsigned int *)got, *b = (const unsigned int *)want;
	unsigned int k, words = length / 4;

	*largest_absolute = *largest_relative = 0.f;
	if (sweeps[i].unsupported)
	{
		sweeps_unsupported++;
		*element = 0xFFFFFFFDu;
		return 1;
	}
	if (sweeps[i].bad)
	{
		sweeps_bad++;
		*element = 0xFFFFFFFEu;
		return 1;
	}
	sweeps_run++;
	for (k = 0; k < words; k++)
		if (a[k] != b[k])
		{
			*element = k;
			*largest_absolute = 1.f;
#ifdef SWEEP_DEBUG
			{
				const struct sweep *s = &sweeps[i];
				unsigned int w;

				printf("sweep differs at word %u: flags %08x pos %g %g %g vel %g %g %g h %g r %g ignore %d most %d; leaves %d clusters %d objects %d\n",
					k, s->query.flags, s->query.position[0], s->query.position[1], s->query.position[2], s->query.velocity[0],
					s->query.velocity[1], s->query.velocity[2], s->query.height, s->query.radius, s->query.ignore, s->query.most,
					s->world.leaf_count, s->world.cluster_count, s->world.object_count);
				{
					extern int box_sweep_debug;
					int f;

					box_sweep_debug = 1;
					box_sweep(&s->world, &s->query, &scratch, &answer);
					box_sweep_debug = 0;
					for (f = 0; f < scratch.features.count[0]; f++)
						printf("  sphere %d: obj %d surf %d fl %d br %d mat %d c %.9g %.9g %.9g r %.9g\n", f, scratch.features.spheres[f].f.object, scratch.features.spheres[f].f.surface,
							scratch.features.spheres[f].f.flags, scratch.features.spheres[f].f.breakable, scratch.features.spheres[f].f.material,
							scratch.features.spheres[f].center[0], scratch.features.spheres[f].center[1], scratch.features.spheres[f].center[2], scratch.features.spheres[f].radius);
					for (f = 0; f < scratch.features.count[1]; f++)
						printf("  cylinder %d: obj %d surf %d fl %d br %d mat %d base %.9g %.9g %.9g h %.9g %.9g %.9g w %.9g\n", f, scratch.features.cylinders[f].f.object, scratch.features.cylinders[f].f.surface,
							scratch.features.cylinders[f].f.flags, scratch.features.cylinders[f].f.breakable, scratch.features.cylinders[f].f.material,
							scratch.features.cylinders[f].base[0], scratch.features.cylinders[f].base[1], scratch.features.cylinders[f].base[2],
							scratch.features.cylinders[f].height[0], scratch.features.cylinders[f].height[1], scratch.features.cylinders[f].height[2], scratch.features.cylinders[f].width);
					for (f = 0; f < scratch.features.count[2]; f++)
						printf("  prism %d: obj %d surf %d fl %d br %d mat %d n %.9g %.9g %.9g d %.9g h %.9g pts %d\n", f, scratch.features.prisms[f].f.object, scratch.features.prisms[f].f.surface,
							scratch.features.prisms[f].f.flags, scratch.features.prisms[f].f.breakable, scratch.features.prisms[f].f.material,
							scratch.features.prisms[f].n[0], scratch.features.prisms[f].n[1], scratch.features.prisms[f].n[2], scratch.features.prisms[f].d, scratch.features.prisms[f].height, scratch.features.prisms[f].point_count);
				}
				for (w = 0; w < words && w < 7 + 11 * 4; w++)
					printf("  w%-3u box %08x %-12g engine %08x %-12g%s\n", w, a[w], *(const float *)&a[w], b[w], *(const float *)&b[w], a[w] != b[w] ? "  <" : "");
				for (w = 0; w < (unsigned int)s->world.object_count; w++)
					printf("  object %d type %d flags %08x seen %d nodes %d sphere %g %g %g r %g\n", s->world.objects[w].index, s->world.objects[w].type,
						s->world.objects[w].flags, s->world.objects[w].seen, s->world.objects[w].node_count, s->world.objects[w].center[0],
						s->world.objects[w].center[1], s->world.objects[w].center[2], s->world.objects[w].radius);
			}
#endif
			return 1;
		}
	return 0;
}

/* v0's budget: ~1.1 ms a tick for the sweep (box_collision.md §9: half of v0's
2.2 for every asker), ~30 us a sweep at b30's ~36 a tick */
static int sweep0_setup(const struct box_static *statics, unsigned int count)
{
	margin = 0.f;
	form = 0;
	part = 0;
	return sweep_setup(statics, count);
}

const struct box_adapter box_sweep0 = {
	"sweep", kinds, 0, 0, BOX_EXACT, 0.f, 0.f, 1.1f, 30.f,
	sweep0_setup, sweep_tick, sweep_run, sweep_output, sweep_compare,
};

/* form A's level walk under the same sweep (box_collision.md §9, stage 3) */
static int sweepa_setup(const struct box_static *statics, unsigned int count)
{
	margin = 0.f;
	form = 1;
	part = 0;
	return sweep_setup(statics, count);
}

const struct box_adapter box_sweepa = {
	"sweepa", kinds, 0, 0, BOX_EXACT, 0.f, 0.f, 1.1f, 30.f,
	sweepa_setup, sweep_tick, sweep_run, sweep_output, sweep_compare, "sweep",
};

/* the diagnostic parts, over form A: the features alone (no clip), and the
level's features alone (no objects, no clip). Their checks fail by design;
only their times count */
static int sweepf_setup(const struct box_static *statics, unsigned int count)
{
	margin = 0.f;
	form = 1;
	part = 1;
	return sweep_setup(statics, count);
}

static int sweepl_setup(const struct box_static *statics, unsigned int count)
{
	margin = 0.f;
	form = 1;
	part = 2;
	return sweep_setup(statics, count);
}

const struct box_adapter box_sweepf = {
	"sweepf", kinds, 0, 0, BOX_EXACT, 0.f, 0.f, 1.1f, 30.f,
	sweepf_setup, sweep_tick, sweep_run, sweep_output, sweep_compare, "sweep",
};

const struct box_adapter box_sweepl = {
	"sweepl", kinds, 0, 0, BOX_EXACT, 0.f, 0.f, 1.1f, 30.f,
	sweepl_setup, sweep_tick, sweep_run, sweep_output, sweep_compare, "sweep",
};

/* B (stage 3): form A with each asker's level walk widened by 0.5 and kept */
static int sweepb_margin(const struct box_static *statics, unsigned int count, float m)
{
	form = 1;
	part = 0;
	margin = m;
	b_have_tick = 0;
	memset(scratch.kept, 0, sizeof(scratch.kept));
	memset(scratch.kept_used, 0, sizeof(scratch.kept_used));
	scratch.kept_clock = 0;
	{
		const char *m = getenv("SWEEP_MARGIN");

		if (m)
			margin = (float)atof(m);
	}
	return sweep_setup(statics, count);
}

static int sweepb_setup(const struct box_static *statics, unsigned int count)
{
	return sweepb_margin(statics, count, 0.5f);
}

/* the PSP has no environment, so the margins the census weighed get names */
static int sweepb25_setup(const struct box_static *statics, unsigned int count)
{
	return sweepb_margin(statics, count, 0.25f);
}

static int sweepb100_setup(const struct box_static *statics, unsigned int count)
{
	return sweepb_margin(statics, count, 1.0f);
}

const struct box_adapter box_sweepb25 = {
	"sweepb25", kinds, 0, 0, BOX_EXACT, 0.f, 0.f, 1.1f, 30.f,
	sweepb25_setup, sweep_tick, sweep_run, sweep_output, sweep_compare, "sweep",
};

const struct box_adapter box_sweepb100 = {
	"sweepb100", kinds, 0, 0, BOX_EXACT, 0.f, 0.f, 1.1f, 30.f,
	sweepb100_setup, sweep_tick, sweep_run, sweep_output, sweep_compare, "sweep",
};

const struct box_adapter box_sweepb = {
	"sweepb", kinds, 0, 0, BOX_EXACT, 0.f, 0.f, 1.1f, 30.f,
	sweepb_setup, sweep_tick, sweep_run, sweep_output, sweep_compare, "sweep",
};

void box_sweepb_counts(unsigned int *hits, unsigned int *walks)
{
	*hits = scratch.kept_hits;
	*walks = scratch.kept_walks;
}

void box_sweep_counts(unsigned int *run, unsigned int *unsupported, unsigned int *bad)
{
	*run = sweeps_run;
	*unsupported = sweeps_unsupported;
	*bad = sweeps_bad;
}
