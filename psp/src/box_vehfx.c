/*
The vehicle effects box: the decals' grouped view test (slice 1) and the
packed walk of a group in the view (slice 2) (box_vehfx.h; docs/box_vehfx.md).
Built only with PSP_VEHFX=1; keep words vehfxgroup and vehfxpack turn them
on, vehfxgroupcheck and vehfxpackcheck run their proofs. Main CPU only: the
decal walk never runs on the ME, so nothing here needs to avoid the VFPU or
system calls, and none are used.
*/
#include <math.h>
#include <string.h>
#include "box_vehfx.h"

/* 32 bytes, two to a cache line: a group out of the view costs the walk one
read here in place of every member's datum, vertex block and kept sphere */
struct vehfx_group
{
	long first, after;
	float centre[3], radius;
	unsigned short count;
	unsigned char turn, live;
	unsigned char pad[4];
};

static struct vehfx_group groups[BOX_VEHFX_GROUPS] __attribute__((aligned(64)));
/* slice 2: each group's members in walk order, its own five lines. A member
walked from here costs a fifth of a line's fill in place of its datum (one or
two lines), its vertex block (one) and its kept sphere (one or two), read
from scattered slots. The spheres are the kept ones, copied bit for bit, so
the test on them is today's verdict */
struct vehfx_pack
{
	long index[BOX_VEHFX_PACK_MAX];
	float sphere[BOX_VEHFX_PACK_MAX][4];
};
static struct vehfx_pack packs[BOX_VEHFX_GROUPS] __attribute__((aligned(64)));
/* each decal slot's group, (group << 8) | its turn, or 0xFFFF: the touch's
way from a decal to its group. A slot left pointing at a group let go and
made again names an older turn, so it's taken for no group; one pointing at
a group it isn't in (a turn come round again) only lets that group go early */
static unsigned short member[BOX_VEHFX_SLOTS];
static unsigned char free_ids[BOX_VEHFX_GROUPS];
static int free_count = -1; /* -1: not set up */

int psp_vehfx_walk = 0;
int psp_vehfx_known = 0;
static int on = 0, check = 0, run_size = 16, pack = 0, pack_check = 0;
static struct box_vehfx_view view;

/* the run being laid down: consecutive decals of this walk, each with its
kept sphere, not in a group yet */
static int run_count = 0;
static long run_first;
static float run_centre[BOX_VEHFX_RUN_MAX][3], run_radius[BOX_VEHFX_RUN_MAX];
static unsigned short run_slot[BOX_VEHFX_RUN_MAX];
static long run_index[BOX_VEHFX_RUN_MAX];

/* the group in the view whose members the walk is going through (slice 2):
its turn, so a group let go meanwhile isn't followed, and the next member's
place. Under vehfxpackcheck, the verdict the decal left to the engine's body
should get */
static struct vehfx_group *walk_g = NULL;
static unsigned char walk_turn;
static int walk_pos, pack_expect = -1;

/* the check: a group that tested out is walked anyway; its members must all
be out by today's test and the walk must then reach the group's after */
static int expecting = 0, expect_left = 0;
static long expect_after;

static struct box_vehfx_stats stats;

#define MEMBER_NONE 0xFFFFu

static void set_up(void)
{
	int k;

	memset(member, 0xFF, sizeof(member));
	for (k = 0; k < BOX_VEHFX_GROUPS; k++)
	{
		groups[k].live = 0;
		free_ids[k] = (unsigned char)(BOX_VEHFX_GROUPS - 1 - k);
	}
	free_count = BOX_VEHFX_GROUPS;
	stats.groups_live = 0;
}

void box_vehfx_config(int on_word, int check_word, int run_word)
{
	on = on_word || check_word;
	check = check_word;
	run_size = run_word < 2 ? 2 : run_word > BOX_VEHFX_RUN_MAX ? BOX_VEHFX_RUN_MAX : run_word;
	if (free_count < 0)
		set_up();
}

void box_vehfx_config_pack(int pack_word, int pack_check_word)
{
	pack = pack_word || pack_check_word;
	pack_check = pack_check_word;
	if (pack)
	{
		on = 1;
		if (run_size > BOX_VEHFX_PACK_MAX)
			run_size = BOX_VEHFX_PACK_MAX;
	}
	if (free_count < 0)
		set_up();
}

static struct vehfx_group *group_of(unsigned int slot)
{
	unsigned int m;
	struct vehfx_group *g;

	if (slot >= BOX_VEHFX_SLOTS || free_count < 0)
		return NULL;
	m = member[slot];
	if (m == MEMBER_NONE)
		return NULL;
	g = &groups[m >> 8];
	return g->live && g->turn == (m & 0xFF) ? g : NULL;
}

static void let_go(struct vehfx_group *g)
{
	g->live = 0;
	free_ids[free_count++] = (unsigned char)(g - groups);
	stats.groups_live--;
	stats.groups_let_go++;
}

void psp_vehfx_touch(long index)
{
	struct vehfx_group *g;

	if (index == BOX_VEHFX_NONE)
		return;
	g = group_of((unsigned int)index & 0xFFFFu);
	if (g)
		let_go(g);
}

void psp_vehfx_forget(void)
{
	if (free_count < 0)
		return;
	set_up();
	run_count = 0;
	expecting = 0;
	walk_g = NULL;
	pack_expect = -1;
	stats.forgets++;
}

/* today's test (psp_ge_decal_visible) on one sphere, in its float order */
static int sphere_out(const float *centre, float radius)
{
	float d0 = centre[0] - view.eye[0], d1 = centre[1] - view.eye[1], d2 = centre[2] - view.eye[2];
	float x = d0 * view.right[0] + d1 * view.right[1] + d2 * view.right[2];
	float y = d0 * view.up[0] + d1 * view.up[1] + d2 * view.up[2];
	float z = d0 * view.forward[0] + d1 * view.forward[1] + d2 * view.forward[2];

	return z < -radius ||
		x - view.half_x * z > radius * view.side_x || -x - view.half_x * z > radius * view.side_x ||
		y - view.half_y * z > radius * view.side_y || -y - view.half_y * z > radius * view.side_y;
}

/* The run ends; after is the decal the walk reached past its last member.
The run's spheres, and the group after it when the run runs into one with
room, are bound by one sphere. Containment holds by the triangle inequality
(each plane's test is distance against radius, and the camera's directions
are unit length); the margin, 0.1% and 0.01 units as the decals' own
spheres take, covers the rounding of the test's sums at the map's distances */
static void close_run(long after)
{
	struct vehfx_group *g = NULL, *grow = NULL;
	float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f }, centre[3], reach = 0.0f;
	int i, k, n = run_count;

	run_count = 0;
	if (n <= 0)
		return;
	if (after != BOX_VEHFX_NONE)
	{
		grow = group_of((unsigned int)after & 0xFFFFu);
		if (grow && (grow->first != after || grow->count + n > run_size))
			grow = NULL;
	}
	if (!grow && n < 2)
		return; /* one decal alone saves nothing; it may grow the next group later */
	for (i = 0; i < n; i++)
		for (k = 0; k < 3; k++)
		{
			if (run_centre[i][k] - run_radius[i] < lo[k])
				lo[k] = run_centre[i][k] - run_radius[i];
			if (run_centre[i][k] + run_radius[i] > hi[k])
				hi[k] = run_centre[i][k] + run_radius[i];
		}
	if (grow)
		for (k = 0; k < 3; k++)
		{
			if (grow->centre[k] - grow->radius < lo[k])
				lo[k] = grow->centre[k] - grow->radius;
			if (grow->centre[k] + grow->radius > hi[k])
				hi[k] = grow->centre[k] + grow->radius;
		}
	for (k = 0; k < 3; k++)
		centre[k] = (lo[k] + hi[k]) * 0.5f;
	for (i = 0; i <= n; i++)
	{
		const float *c = i < n ? run_centre[i] : grow ? grow->centre : NULL;
		float r = i < n ? run_radius[i] : grow ? grow->radius : 0.0f, d2 = 0.0f;

		if (!c)
			break;
		for (k = 0; k < 3; k++)
			d2 += (c[k] - centre[k]) * (c[k] - centre[k]);
		if (sqrtf(d2) + r > reach)
			reach = sqrtf(d2) + r;
	}
	if (grow)
	{
		g = grow;
		g->count = (unsigned short)(g->count + n);
		stats.groups_grown++;
	}
	else
	{
		if (!free_count)
		{
			stats.table_full++;
			return;
		}
		g = &groups[free_ids[--free_count]];
		g->turn++;
		g->live = 1;
		g->count = (unsigned short)n;
		g->after = after;
		stats.groups_live++;
		stats.groups_made++;
	}
	g->first = run_first;
	for (k = 0; k < 3; k++)
		g->centre[k] = centre[k];
	g->radius = reach * 1.001f + 0.01f;
	for (i = 0; i < n; i++)
		member[run_slot[i]] = (unsigned short)(((g - groups) << 8) | g->turn);
	if (pack)
	{
		/* the run comes before the group it grows, in the walk */
		struct vehfx_pack *p = &packs[g - groups];
		int old = g->count - n;

		if (old > 0)
		{
			memmove(&p->index[n], &p->index[0], old * sizeof(p->index[0]));
			memmove(&p->sphere[n][0], &p->sphere[0][0], old * sizeof(p->sphere[0]));
		}
		for (i = 0; i < n; i++)
		{
			p->index[i] = run_index[i];
			p->sphere[i][0] = run_centre[i][0];
			p->sphere[i][1] = run_centre[i][1];
			p->sphere[i][2] = run_centre[i][2];
			p->sphere[i][3] = run_radius[i];
		}
	}
}

void box_vehfx_walk_begin(const struct box_vehfx_view *v)
{
	if (expecting && (expect_left || expect_after != BOX_VEHFX_NONE))
		stats.check_broken++;
	expecting = 0;
	/* the last list ended inside a group's members, or short of its after */
	if (walk_g && walk_g->live && walk_g->turn == walk_turn &&
		(walk_pos < walk_g->count || walk_g->after != BOX_VEHFX_NONE))
		stats.pack_broken++;
	if (pack_expect >= 0)
		stats.pack_broken++;
	walk_g = NULL;
	pack_expect = -1;
	psp_vehfx_known = 0;
	run_count = 0;
	psp_vehfx_walk = on && v && free_count >= 0;
	if (psp_vehfx_walk)
	{
		view = *v;
		stats.walks++;
	}
}

/* Slice 2: the members of the group in the view from walk_pos on. Each is
tested on its own kept sphere, as today's test would; the failing ones are
passed over unread, and the walk is left on the first that passes, its
verdict known. Under the check, one member at a time is left to the engine's
body, with the verdict it should get */
static int pack_walk(long *index)
{
	struct vehfx_group *g = walk_g;
	const struct vehfx_pack *p = &packs[g - groups];

	if (pack_check)
	{
		pack_expect = !sphere_out(p->sphere[walk_pos], p->sphere[walk_pos][3]);
		*index = p->index[walk_pos++];
		stats.pack_members++;
		return 0;
	}
	while (walk_pos < g->count)
	{
		const float *s = p->sphere[walk_pos];

		if (!sphere_out(s, s[3]))
		{
			*index = p->index[walk_pos++];
			psp_vehfx_known = 1;
			stats.members_known++;
			return 0;
		}
		walk_pos++;
		stats.members_passed_over++;
	}
	*index = g->after;
	walk_g = NULL;
	return 1;
}

int psp_vehfx_at(long *index)
{
	long x = *index;
	struct vehfx_group *g;
	int ahead;

	psp_vehfx_known = 0;
	if (pack_expect >= 0)
	{
		/* the engine's body didn't report the member it was left */
		stats.pack_broken++;
		pack_expect = -1;
	}
	if (expecting && !expect_left)
	{
		if (x != expect_after)
			stats.check_broken++;
		expecting = 0;
	}
	if (walk_g)
	{
		/* the engine's body walked a member that passes and took its link:
		that must be the next member, or the group's after past the last */
		g = walk_g;
		if (!g->live || g->turn != walk_turn)
		{
			stats.pack_broken++;
			walk_g = NULL;
		}
		else if (walk_pos < g->count)
		{
			if (x == packs[g - groups].index[walk_pos])
				return pack_walk(index);
			stats.pack_broken++;
			walk_g = NULL;
		}
		else
		{
			if (x != g->after)
				stats.pack_broken++;
			walk_g = NULL;
		}
	}
	g = group_of((unsigned int)x & 0xFFFFu);
	if (!g || g->first != x)
		return 0;
	/* the members from x on; a run just walked may grow the group backwards
	(its sphere then holds them too), but those are behind the walk */
	ahead = g->count;
	close_run(x);
	stats.groups_tested++;
	if (!sphere_out(g->centre, g->radius))
	{
		if (!pack)
			return 0;
		walk_g = g;
		walk_turn = g->turn;
		walk_pos = g->count - ahead;
		return pack_walk(index);
	}
	stats.groups_out++;
	stats.decals_passed_over += ahead;
	if (check)
	{
		if (expecting)
			stats.check_broken++;
		expecting = 1;
		expect_left = ahead;
		expect_after = g->after;
		return 0;
	}
	*index = g->after;
	return 1;
}

void box_vehfx_seen(long index, long next, const float centre[3], float radius, int joinable, int verdict)
{
	unsigned int slot = (unsigned int)index & 0xFFFFu;

	if (pack_expect >= 0)
	{
		if (verdict != pack_expect)
			stats.pack_differ++;
		pack_expect = -1;
	}
	if (expecting && expect_left)
	{
		expect_left--;
		stats.check_members++;
		if (verdict > 0)
			stats.check_differ++;
	}
	if (!joinable || slot >= BOX_VEHFX_SLOTS || group_of(slot))
	{
		close_run(index);
		return;
	}
	if (!run_count)
		run_first = index;
	run_centre[run_count][0] = centre[0];
	run_centre[run_count][1] = centre[1];
	run_centre[run_count][2] = centre[2];
	run_radius[run_count] = radius;
	run_slot[run_count] = (unsigned short)slot;
	run_index[run_count] = index;
	run_count++;
	if (run_count >= run_size || next == BOX_VEHFX_NONE)
		close_run(next);
}

void box_vehfx_view_differs(void)
{
	stats.pack_view_differ++;
}

void box_vehfx_stats_take(struct box_vehfx_stats *out)
{
	int live = stats.groups_live;

	*out = stats;
	memset(&stats, 0, sizeof(stats));
	stats.groups_live = live;
}
