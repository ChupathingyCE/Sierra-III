/*
The vehicle effects box, slice 1: the decals' view test made a run of the
draw's walk at a time (docs/box_vehfx.md). On b40's warthog drive the decal
walk tests ~810-1,200 decals a frame and ~75% fail; the tyre marks that fail
lie in trails, laid one after another, and the engine keeps each cluster's
decals in a list in the order they were laid. So the walk's consecutive
decals are kept as runs (a group), each with one sphere holding every
member's own test sphere: a group out of the view is passed over whole, list
links and all, and a group in it is walked decal by decal as before.

Exact by construction: a group is tested only at its first member, its
sphere contains each member's kept sphere with a margin over the test's
rounding, and the walk jumps to the decal that followed the group's last
member when it was made. That jump is the walk's own path as long as no
member's link has been written and no member deleted since: the engine's
list writes and deletes call psp_vehfx_touch, which lets the member's group
go. So a decal is drawn iff today's test passes it, in the same order.

No engine header comes in, so the box builds into a host harness as it does
into the game. psp_ge.c gives it the view and each decal's kept sphere.
*/
#ifndef BOX_VEHFX_H
#define BOX_VEHFX_H

#define BOX_VEHFX_SLOTS 2048    /* the engine's MAXIMUM_DECALS_PER_MAP */
#define BOX_VEHFX_GROUPS 255    /* a member's group and its turn fit 16 bits */
#define BOX_VEHFX_RUN_MAX 32
#define BOX_VEHFX_NONE (-1L)    /* the engine's NONE */

/* the window's camera as psp_ge.c's main-view copy holds it */
struct box_vehfx_view
{
	float eye[3], right[3], up[3], forward[3];
	float half_x, half_y, side_x, side_y;
};

struct box_vehfx_stats
{
	unsigned long walks, groups_tested, groups_out, decals_passed_over;
	unsigned long groups_made, groups_grown, groups_let_go, forgets, table_full;
	unsigned long check_members, check_differ, check_broken;
	int groups_live;
};

/* 1 while the walk under way may take groups (set by box_vehfx_walk_begin) */
extern int psp_vehfx_walk;

/* the keep words, once: on, the check (walks every member of a group that
tests out and counts any today's test would draw), the longest run */
void box_vehfx_config(int on, int check, int run_size);
/* a cluster's list is about to be walked; view is NULL when the walk may not
take groups (the GE not drawing, today's test switched off) */
void box_vehfx_walk_begin(const struct box_vehfx_view *view);
/* at each decal the walk reaches, before anything of it is read: 1 when it
starts a group that is out of the view, *index then the decal after it */
int psp_vehfx_at(long *index);
/* after today's test on a decal: its kept sphere when it has one (joinable),
the decal after it, and today's verdict (1 drawn, 0 out, -1 not tested) */
void box_vehfx_seen(long index, long next, const float centre[3], float radius, int joinable, int verdict);
/* the engine wrote this decal's link to the next or is deleting it */
void psp_vehfx_touch(long index);
/* every list may have changed (a map, a game state loaded, the level's BSP) */
void psp_vehfx_forget(void);
void box_vehfx_stats_take(struct box_vehfx_stats *out);

#endif
