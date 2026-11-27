/* The world store's first slice (docs/box_world_store.md §9): the one-tick-
   old copy of the pill sweep's object inputs and of what biped motion reads
   of other bipeds. Built with PSP_WSOLD=1; keep word wsold takes the copy,
   wsoldcheck also proves it complete and counts how far it parts from the
   live world at the sweep's reads.

   The copy is refreshed once a tick, where the object walk begins, from the
   objects marked as written since (the walk's updates, the sleep's wake
   marks at the outside writers, creation, node matrices made on demand, the
   child lists) and from every biped held. Nothing writes it between two
   refreshes, so a reader on the other core needs no lock: it reads the world
   as the walk began. The engine's side (psp_fixups.py FIXES_WSOLD) makes each
   record from its datum; this file owns where it goes. */
#include <stdio.h>
#include <string.h>
#if defined(__psp__)
#include <pspkernel.h>
#else
/* the host's syntax check: no cache to write back */
#define sceKernelDcacheWritebackAll() ((void)0)
#define sceKernelDcacheWritebackRange(p, n) ((void)(p), (void)(n))
#endif
#include "box_ws_old.h"

void platform_psp_log(const char *format, ...);
int psp_keep(const char *name);

struct box_ws_old_object box_ws_old_objects[BOX_WS_OLD_SLOTS] __attribute__((aligned(64)));
struct box_ws_old_biped box_ws_old_bipeds[BOX_WS_OLD_BIPEDS] __attribute__((aligned(64)));
int16_t box_ws_old_biped_of[BOX_WS_OLD_SLOTS];
int box_ws_old_on = -1;
int box_ws_old_checking;
int box_ws_old_parts;

/* the clusters' runs, two buffers: the refresh writes the one the readers
   don't hold, then flips (a run of an unmarked cluster is copied across) */
struct box_ws_old_cluster box_ws_old_clusters[BOX_WS_OLD_CLUSTERS] __attribute__((aligned(64)));
static int32_t box_ws_old_member_runs[2][BOX_WS_OLD_MEMBERS] __attribute__((aligned(64)));
static struct box_ws_old_cluster box_ws_old_clusters_next[BOX_WS_OLD_CLUSTERS];
const int32_t *box_ws_old_members = box_ws_old_member_runs[0];
int box_ws_old_cluster_count;
const void *box_ws_old_cluster_level;
static int box_ws_old_member_side, box_ws_old_cluster_all = 1, box_ws_old_cluster_overflow;
static unsigned box_ws_old_member_fill;
static uint32_t box_ws_old_cluster_bits[BOX_WS_OLD_CLUSTERS / 32];
static int box_ws_old_forgotten;

/* the models' runs: given out in order and kept while an object's node count
   stays; a pool given out to its end is packed again on the next refresh
   (every model taken anew), so runs left by deleted objects come back */
struct box_ws_old_model box_ws_old_models[BOX_WS_OLD_SLOTS] __attribute__((aligned(64)));
struct box_ws_old_node box_ws_old_nodes[BOX_WS_OLD_NODES] __attribute__((aligned(64)));
static unsigned box_ws_old_node_fill, box_ws_old_node_live;
static int box_ws_old_repack;
/* the models NOT_HELD, taken again at every refresh until held: an owed
   node is made later by means B's fill, which marks nothing */
static uint32_t box_ws_old_unheld_bits[BOX_WS_OLD_SLOTS / 32];

static int16_t box_ws_old_biped_owner[BOX_WS_OLD_BIPEDS];
uint32_t box_ws_old_bits[BOX_WS_OLD_SLOTS / 32];

/* the made word on a line of its own: its write-back carries nothing else,
   and no other variable's write-back can put an old made word back */
volatile uint32_t box_ws_old_made[16] __attribute__((aligned(64)));
/* the jobs holding the copy, by the function that joins each */
static void (*box_ws_old_holders[BOX_WS_OLD_HOLDERS])(void);
static unsigned long box_ws_old_posts, box_ws_old_holds_refused, box_ws_old_held_at_refresh, box_ws_old_made_behind;
static unsigned long box_ws_old_refusals[BOX_WS_OLD_REFUSALS];
static uint16_t box_ws_old_list[BOX_WS_OLD_SLOTS];
static unsigned box_ws_old_listed;

/* the counts behind the log line, since the first refresh */
static unsigned long box_ws_old_flips, box_ws_old_refreshed, box_ws_old_refreshed_bipeds, box_ws_old_cleared;
static unsigned long box_ws_old_past_slots, box_ws_old_bipeds_full;
static unsigned box_ws_old_high_water, box_ws_old_most_refreshed, box_ws_old_most_bipeds;
static unsigned long box_ws_old_clusters_taken, box_ws_old_cluster_refreshes, box_ws_old_cluster_marks;
static unsigned box_ws_old_members_most;
static unsigned long box_ws_old_models_put, box_ws_old_nodes_put, box_ws_old_models_owed, box_ws_old_models_roomless,
	box_ws_old_repacks, box_ws_old_unheld_taken;
static unsigned box_ws_old_nodes_most;

enum { WSO_SPHERE, WSO_FLAGS, WSO_LINKS, WSO_TYPE, WSO_DEAD, WSO_NONE, WSO_PILL, WSO_MOTION, WSO_UNIT, WSO_NODES,
	WSO_CLUSTER, WSO_FIELDS };
static const char *const box_ws_old_field_name[WSO_FIELDS] = {
	"sphere", "flags", "links", "type", "dead", "no record", "pill", "position/velocity", "parent/player/seat/passes",
	"model nodes", "cluster list"
};
static unsigned long box_ws_old_check_objects, box_ws_old_check_missed[WSO_FIELDS];
static unsigned box_ws_old_check_told, box_ws_old_posed_told;
/* the spheres of objects with pose nodes that changed with no mark: kept out
   of the 'sphere' field, the crossing's own count (store_pose.c's writers) */
static unsigned long box_ws_old_check_posed, box_ws_old_check_posed_spheres;
/* models the copy had as NOT_HELD at the check while the live one differs:
   the copy's own refusal (a reader's fallback), kept out of 'model nodes' */
static unsigned long box_ws_old_check_unheld, box_ws_old_check_unheld_roomless;
static unsigned box_ws_old_unheld_told;
static unsigned long box_ws_old_visits, box_ws_old_visit_parted[WSO_FIELDS], box_ws_old_visits_parted;
static unsigned long box_ws_old_check_clusters, box_ws_old_model_visits, box_ws_old_model_visits_parted;

static void box_ws_old_models_reset(void);
static void box_ws_old_model_drop(unsigned absolute);

/* ---------- the hand-over: the fence, the made word, the readers' counts */

uint32_t box_ws_old_refresh_number(void)
{
	return (uint32_t)box_ws_old_flips;
}

int box_ws_old_hold(void (*join)(void))
{
	int k;

	if (box_ws_old_on <= 0 || !join)
		return 0;
	for (k = 0; k < BOX_WS_OLD_HOLDERS; k++)
		if (!box_ws_old_holders[k])
		{
			box_ws_old_holders[k] = join;
			box_ws_old_posts++;
			return 1;
		}
	box_ws_old_holds_refused++;
	return 0;
}

void box_ws_old_release(void (*join)(void))
{
	int k;

	for (k = 0; k < BOX_WS_OLD_HOLDERS; k++)
		if (box_ws_old_holders[k] == join)
		{
			box_ws_old_holders[k] = NULL;
			return;
		}
}

void box_ws_old_refused(const unsigned refused[BOX_WS_OLD_REFUSALS])
{
	int k;

	for (k = 0; k < BOX_WS_OLD_REFUSALS; k++)
		box_ws_old_refusals[k] += refused[k];
}

/* the refresh's start: no job may still hold the copy it rewrites. One that
   does is joined here and counted (a post the tick's order failed to join
   first); then the made word says no copy stands until the refresh ends.
   wsoldcheck: the word left by the last refresh is its number */
static void box_ws_old_fence(void)
{
	int k;

	for (k = 0; k < BOX_WS_OLD_HOLDERS; k++)
	{
		void (*join)(void) = box_ws_old_holders[k];

		if (!join)
			continue;
		box_ws_old_held_at_refresh++;
		join();
		box_ws_old_holders[k] = NULL;
	}
	if (box_ws_old_checking && box_ws_old_flips && box_ws_old_made[0] != (uint32_t)box_ws_old_flips)
		box_ws_old_made_behind++;
	box_ws_old_made[0] = 0;
	sceKernelDcacheWritebackRange((const void *)box_ws_old_made, sizeof(box_ws_old_made));
}

/* the refresh's end, after the check's put-backs: the copy's lines written
   back (all of the 16 KB D-cache: at most 256 lines whatever the copy's
   size, §4's choice (a)), then the made word, written back last */
static void box_ws_old_publish(void)
{
	sceKernelDcacheWritebackAll();
	box_ws_old_made[0] = (uint32_t)box_ws_old_flips;
	sceKernelDcacheWritebackRange((const void *)box_ws_old_made, sizeof(box_ws_old_made));
}

int box_ws_old_begin(void)
{
	if (box_ws_old_on > 0)
		box_ws_old_fence();
	if (box_ws_old_on < 0)
	{
		box_ws_old_checking = psp_keep("wsoldcheck") != 0;
		/* the clusters (a reader's, and the check proves them too) */
		box_ws_old_parts = box_ws_old_checking || psp_keep("wsoldparts");
		box_ws_old_on = box_ws_old_checking || box_ws_old_parts || psp_keep("wsold");
		if (!box_ws_old_on)
			return 0;
		memset(box_ws_old_biped_of, 0xff, sizeof(box_ws_old_biped_of));
		memset(box_ws_old_biped_owner, 0xff, sizeof(box_ws_old_biped_owner));
		platform_psp_log("wsold: the one-tick-old copy on (%u object slots of %u B, %u bipeds of %u B, %u KB)%s%s",
			(unsigned)BOX_WS_OLD_SLOTS, (unsigned)sizeof(struct box_ws_old_object), (unsigned)BOX_WS_OLD_BIPEDS,
			(unsigned)sizeof(struct box_ws_old_biped),
			(unsigned)((sizeof(box_ws_old_objects) + sizeof(box_ws_old_bipeds) + sizeof(box_ws_old_biped_of)) / 1024),
			box_ws_old_parts ? ", with the clusters' lists and the models' nodes" : "",
			box_ws_old_checking ? ", checked" : "");
		if (box_ws_old_parts)
		{
			platform_psp_log("wsold: the clusters' lists %u B (%u clusters, %u members, two buffers)",
				(unsigned)(sizeof(box_ws_old_clusters) + sizeof(box_ws_old_clusters_next) + sizeof(box_ws_old_member_runs) +
					sizeof(box_ws_old_cluster_bits)),
				(unsigned)BOX_WS_OLD_CLUSTERS, (unsigned)BOX_WS_OLD_MEMBERS);
			platform_psp_log("wsold: the models' nodes %u B (%u model slots of %u B, %u nodes of %u B; %u B of not-held bits)",
				(unsigned)(sizeof(box_ws_old_models) + sizeof(box_ws_old_nodes)), (unsigned)BOX_WS_OLD_SLOTS,
				(unsigned)sizeof(struct box_ws_old_model), (unsigned)BOX_WS_OLD_NODES, (unsigned)sizeof(struct box_ws_old_node),
				(unsigned)sizeof(box_ws_old_unheld_bits));
		}
		return 2;
	}
	if (box_ws_old_on && box_ws_old_forgotten)
	{
		/* another world: every slot filled again (the first fill's path) */
		box_ws_old_forgotten = 0;
		box_ws_old_repack = 0;
		box_ws_old_models_reset();
		return 2;
	}
	if (box_ws_old_on && box_ws_old_repack)
	{
		/* every model taken anew from the pool's start: the first fill's path */
		box_ws_old_repack = 0;
		box_ws_old_repacks++;
		box_ws_old_models_reset();
		return 2;
	}
	return box_ws_old_on;
}

void box_ws_old_forget(void)
{
	if (box_ws_old_on <= 0)
		return;
	box_ws_old_forgotten = 1;
	box_ws_old_cluster_all = 1;
}

void box_ws_old_mark(long datum_index)
{
	unsigned absolute = (unsigned)datum_index & 0xffff;

	if (box_ws_old_on <= 0)
		return;
	if (absolute >= BOX_WS_OLD_SLOTS)
	{
		box_ws_old_past_slots++;
		return;
	}
	if (box_ws_old_bits[absolute >> 5] & (1u << (absolute & 31)))
		return;
	box_ws_old_bits[absolute >> 5] |= 1u << (absolute & 31);
	box_ws_old_list[box_ws_old_listed++] = (uint16_t)absolute;
}

int box_ws_old_marked(uint16_t *absolute, unsigned capacity)
{
	unsigned n = 0, i;

	for (i = 0; i < box_ws_old_listed && n < capacity; i++)
		absolute[n++] = box_ws_old_list[i];
	for (i = 0; i < BOX_WS_OLD_BIPEDS && n < capacity; i++)
	{
		int16_t owner = box_ws_old_biped_owner[i];

		if (owner >= 0 && !(box_ws_old_bits[owner >> 5] & (1u << (owner & 31))))
			absolute[n++] = (uint16_t)owner;
	}
	/* a model not held, not marked: none is a biped's (the models are
	   vehicles, scenery, machines and controls), so the three sets are
	   disjoint slots and fit the list */
	for (i = 0; box_ws_old_parts && i < BOX_WS_OLD_SLOTS / 32; i++)
	{
		uint32_t bits = box_ws_old_unheld_bits[i] & ~box_ws_old_bits[i];

		while (bits && n < capacity)
		{
			unsigned bit = (unsigned)__builtin_ctz(bits);

			bits &= bits - 1;
			absolute[n++] = (uint16_t)(i * 32 + bit);
			box_ws_old_unheld_taken++;
		}
	}
	box_ws_old_listed = 0;
	memset(box_ws_old_bits, 0, sizeof(box_ws_old_bits));
	return (int)n;
}

static void box_ws_old_free_biped(unsigned absolute)
{
	int16_t slot = box_ws_old_biped_of[absolute];

	if (slot >= 0)
	{
		box_ws_old_biped_owner[slot] = -1;
		box_ws_old_biped_of[absolute] = -1;
	}
}

void box_ws_old_put(unsigned absolute, struct box_ws_old_object const *object, struct box_ws_old_biped const *biped)
{
	if (absolute >= BOX_WS_OLD_SLOTS)
		return;
	if (!object)
	{
		if (box_ws_old_objects[absolute].identifier)
			box_ws_old_cleared++;
		box_ws_old_objects[absolute].identifier = 0;
		box_ws_old_free_biped(absolute);
		if (box_ws_old_parts)
			box_ws_old_model_drop(absolute);
		return;
	}
	box_ws_old_objects[absolute] = *object;
	box_ws_old_refreshed++;
	if (!biped)
	{
		box_ws_old_free_biped(absolute);
		return;
	}
	if (box_ws_old_biped_of[absolute] < 0)
	{
		int slot;

		for (slot = 0; slot < BOX_WS_OLD_BIPEDS && box_ws_old_biped_owner[slot] >= 0; slot++)
			;
		if (slot == BOX_WS_OLD_BIPEDS)
		{
			box_ws_old_bipeds_full++;
			return;
		}
		box_ws_old_biped_owner[slot] = (int16_t)absolute;
		box_ws_old_biped_of[absolute] = (int16_t)slot;
	}
	box_ws_old_bipeds[box_ws_old_biped_of[absolute]] = *biped;
	box_ws_old_refreshed_bipeds++;
}

/* ---------- the models' nodes */

static void box_ws_old_model_drop(unsigned absolute)
{
	struct box_ws_old_model *m = &box_ws_old_models[absolute];

	if (m->state == BOX_WS_OLD_MODEL_HELD)
		box_ws_old_node_live -= m->count;
	m->state = BOX_WS_OLD_MODEL_NONE;
	m->count = 0;
	box_ws_old_unheld_bits[absolute >> 5] &= ~(1u << (absolute & 31));
}

/* the pool from its start, every model's run given up: the full fill that
   follows takes each model again */
static void box_ws_old_models_reset(void)
{
	memset(box_ws_old_models, 0, sizeof(box_ws_old_models));
	memset(box_ws_old_unheld_bits, 0, sizeof(box_ws_old_unheld_bits));
	box_ws_old_node_fill = box_ws_old_node_live = 0;
}

static void box_ws_old_model_unheld(unsigned absolute)
{
	box_ws_old_models[absolute].state = BOX_WS_OLD_MODEL_NOT_HELD;
	box_ws_old_unheld_bits[absolute >> 5] |= 1u << (absolute & 31);
}

void box_ws_old_model_put(unsigned absolute, int state, const struct box_ws_old_node *nodes, unsigned count)
{
	struct box_ws_old_model *m;
	unsigned k;

	if (absolute >= BOX_WS_OLD_SLOTS || !box_ws_old_parts)
		return;
	m = &box_ws_old_models[absolute];
	if (state != BOX_WS_OLD_MODEL_HELD)
	{
		box_ws_old_model_drop(absolute);
		m->state = (uint8_t)state;
		if (state == BOX_WS_OLD_MODEL_NOT_HELD)
		{
			box_ws_old_model_unheld(absolute);
			box_ws_old_models_owed++;
		}
		return;
	}
	box_ws_old_models_put++;
	box_ws_old_nodes_put += count;
	/* a run keeps its place while the node count stays (the permutations
	   that choose the live nodes rarely change) */
	if (m->state != BOX_WS_OLD_MODEL_HELD || m->count != count)
	{
		box_ws_old_model_drop(absolute);
		if (box_ws_old_node_fill + count > BOX_WS_OLD_NODES)
		{
			box_ws_old_model_unheld(absolute);
			/* the cause, for wsoldcheck: a NOT_HELD model's count is no run
			   (readers and the drop read it only when HELD), so the nodes
			   it wanted say past the nodes, 0 says owed */
			m->count = (uint8_t)count;
			box_ws_old_models_roomless++;
			/* a repack helps only when deleted objects' runs left room */
			if (box_ws_old_node_fill - box_ws_old_node_live >= count)
				box_ws_old_repack = 1;
			return;
		}
		m->first = (uint16_t)box_ws_old_node_fill;
		m->count = (uint8_t)count;
		box_ws_old_node_fill += count;
		box_ws_old_node_live += count;
		if (box_ws_old_node_fill > box_ws_old_nodes_most)
			box_ws_old_nodes_most = box_ws_old_node_fill;
	}
	m->state = BOX_WS_OLD_MODEL_HELD;
	for (k = 0; k < count; k++)
		box_ws_old_nodes[m->first + k] = nodes[k];
}

/* whether the model differs from what's held: the state, the count, each
   node's BSP and matrix bit for bit */
static int box_ws_old_model_differs(unsigned absolute, int state, const struct box_ws_old_node *nodes, unsigned count)
{
	struct box_ws_old_model const *m = &box_ws_old_models[absolute];

	if (m->state != state)
		return 1;
	if (state != BOX_WS_OLD_MODEL_HELD)
		return 0;
	return m->count != count || memcmp(&box_ws_old_nodes[m->first], nodes, count * sizeof(*nodes)) != 0;
}

void box_ws_old_model_check(unsigned absolute, int state, const struct box_ws_old_node *nodes, unsigned count)
{
	struct box_ws_old_model const *m;

	if (absolute >= BOX_WS_OLD_SLOTS || !box_ws_old_parts || !box_ws_old_model_differs(absolute, state, nodes, count))
		return;
	m = &box_ws_old_models[absolute];
	/* the copy's own NOT_HELD (past the nodes, or owed and not yet made):
	   its reader goes to the fallback, so nothing is misread and no mark
	   was missed. Told apart, and not put back: the not-held bit retakes it
	   at the next refresh, as in the copy unchecked */
	if (m->state == BOX_WS_OLD_MODEL_NOT_HELD)
	{
		box_ws_old_check_unheld++;
		if (m->count)
			box_ws_old_check_unheld_roomless++;
		if (box_ws_old_unheld_told < 8)
		{
			box_ws_old_unheld_told++;
			platform_psp_log("wsoldcheck: refresh %lu: slot %u's model not held at the check (%s; live state %d, %u nodes)",
				box_ws_old_flips, absolute, m->count ? "past the nodes" : "owed", state, count);
		}
		return;
	}
	box_ws_old_check_missed[WSO_NODES]++;
	if (box_ws_old_check_told < 24)
	{
		box_ws_old_check_told++;
		platform_psp_log("wsoldcheck: refresh %lu: slot %u's model nodes changed with no mark (state %d, %u nodes; held state %u, %u)",
			box_ws_old_flips, absolute, state, count, (unsigned)box_ws_old_models[absolute].state,
			(unsigned)box_ws_old_models[absolute].count);
	}
	box_ws_old_model_put(absolute, state, nodes, count);
}

void box_ws_old_model_visit(unsigned absolute, int state, const struct box_ws_old_node *nodes, unsigned count)
{
	if (absolute >= BOX_WS_OLD_SLOTS || !box_ws_old_parts)
		return;
	box_ws_old_model_visits++;
	if (box_ws_old_model_differs(absolute, state, nodes, count))
		box_ws_old_model_visits_parted++;
}

/* ---------- the clusters' lists */

void box_ws_old_mark_cluster(int cluster)
{
	if (box_ws_old_on <= 0 || !box_ws_old_parts || cluster < 0 || cluster >= BOX_WS_OLD_CLUSTERS)
		return;
	box_ws_old_cluster_bits[cluster >> 5] |= 1u << (cluster & 31);
	box_ws_old_cluster_marks++;
}

int box_ws_old_clusters_begin(const void *level, int cluster_count)
{
	if (cluster_count > BOX_WS_OLD_CLUSTERS)
		cluster_count = BOX_WS_OLD_CLUSTERS;
	if (level != box_ws_old_cluster_level || cluster_count != box_ws_old_cluster_count)
		box_ws_old_cluster_all = 1;
	box_ws_old_cluster_level = level;
	box_ws_old_cluster_count = cluster_count;
	box_ws_old_member_fill = 0;
	box_ws_old_cluster_overflow = 0;
	box_ws_old_cluster_refreshes++;
	return box_ws_old_cluster_all;
}

int box_ws_old_cluster_marked(int cluster)
{
	return box_ws_old_cluster_all || ((box_ws_old_cluster_bits[cluster >> 5] >> (cluster & 31)) & 1);
}

void box_ws_old_cluster_put(int cluster, const int32_t *objects, unsigned count)
{
	int32_t *to = box_ws_old_member_runs[!box_ws_old_member_side];
	unsigned k;

	if (box_ws_old_member_fill + count > BOX_WS_OLD_MEMBERS)
	{
		box_ws_old_cluster_overflow = 1;
		count = 0;
	}
	box_ws_old_clusters_next[cluster].first = (uint16_t)box_ws_old_member_fill;
	box_ws_old_clusters_next[cluster].count = (uint16_t)count;
	for (k = 0; k < count; k++)
		to[box_ws_old_member_fill + k] = objects[k];
	box_ws_old_member_fill += count;
}

void box_ws_old_cluster_keep(int cluster)
{
	struct box_ws_old_cluster const *held = &box_ws_old_clusters[cluster];

	box_ws_old_cluster_put(cluster, box_ws_old_members + held->first, held->count);
}

void box_ws_old_clusters_end(unsigned taken)
{
	box_ws_old_clusters_taken += taken;
	memset(box_ws_old_cluster_bits, 0, sizeof(box_ws_old_cluster_bits));
	if (box_ws_old_cluster_overflow)
	{
		/* the partition holds no more references than the members, so this
		is a list read wrong: no clusters held, the readers take their
		serial paths, and every list is taken again next time */
		box_ws_old_cluster_count = 0;
		box_ws_old_cluster_all = 1;
		return;
	}
	box_ws_old_cluster_all = 0;
	if (box_ws_old_member_fill > box_ws_old_members_most)
		box_ws_old_members_most = box_ws_old_member_fill;
	memcpy(box_ws_old_clusters, box_ws_old_clusters_next, sizeof(box_ws_old_clusters));
	box_ws_old_member_side = !box_ws_old_member_side;
	box_ws_old_members = box_ws_old_member_runs[box_ws_old_member_side];
}

void box_ws_old_cluster_check(int cluster, const int32_t *objects, unsigned count)
{
	struct box_ws_old_cluster const *held;

	if (cluster < 0 || cluster >= box_ws_old_cluster_count)
		return;
	box_ws_old_check_clusters++;
	held = &box_ws_old_clusters[cluster];
	if (held->count == count && !memcmp(box_ws_old_members + held->first, objects, count * sizeof(*objects)))
		return;
	box_ws_old_check_missed[WSO_CLUSTER]++;
	if (box_ws_old_check_told < 24)
	{
		box_ws_old_check_told++;
		platform_psp_log("wsoldcheck: refresh %lu: cluster %d's list changed with no mark (%u objects, held %u)",
			box_ws_old_flips, cluster, count, (unsigned)held->count);
	}
	/* taken again next time, so one missed writer counts once a tick */
	box_ws_old_cluster_bits[cluster >> 5] |= 1u << (cluster & 31);
}

static void box_ws_old_log(void)
{
	char line[420];
	int n = 0, k;
	double flips = box_ws_old_flips ? (double)box_ws_old_flips : 1.0;

	platform_psp_log("wsold: %lu refreshes: %.1f objects and %.1f bipeds a refresh (%.0f B; most %u and %u), %lu records cleared; slots used to %u of %u, %lu marks past them, %lu bipeds without a record",
		box_ws_old_flips, box_ws_old_refreshed / flips, box_ws_old_refreshed_bipeds / flips,
		(box_ws_old_refreshed * sizeof(struct box_ws_old_object) + box_ws_old_refreshed_bipeds * sizeof(struct box_ws_old_biped)) / flips,
		box_ws_old_most_refreshed, box_ws_old_most_bipeds, box_ws_old_cleared, box_ws_old_high_water,
		(unsigned)BOX_WS_OLD_SLOTS, box_ws_old_past_slots, box_ws_old_bipeds_full);
	if (box_ws_old_parts)
	{
		double refreshes = box_ws_old_cluster_refreshes ? (double)box_ws_old_cluster_refreshes : 1.0;

		platform_psp_log("wsold: clusters %.1f lists taken a refresh (%lu marks), members most %u of %u",
			box_ws_old_clusters_taken / refreshes, box_ws_old_cluster_marks, box_ws_old_members_most, (unsigned)BOX_WS_OLD_MEMBERS);
		platform_psp_log("wsold: models %.1f runs and %.1f nodes put a refresh, %lu not held (owed), %lu past the nodes, %.1f not held taken again a refresh; nodes used to %u of %u (live %u), %lu repacks",
			box_ws_old_models_put / flips, box_ws_old_nodes_put / flips, box_ws_old_models_owed, box_ws_old_models_roomless,
			box_ws_old_unheld_taken / flips, box_ws_old_nodes_most, (unsigned)BOX_WS_OLD_NODES, box_ws_old_node_live,
			box_ws_old_repacks);
	}
	platform_psp_log("wsold: readers %lu posts (%lu refused a hold), %lu whole not made, %lu refresh while held; objects taken %lu, not held: past the slots %lu, no record %lu, marked since %lu, no biped record %lu, model past the nodes %lu, model owed %lu",
		box_ws_old_posts, box_ws_old_holds_refused, box_ws_old_refusals[BOX_WS_OLD_NOT_MADE], box_ws_old_held_at_refresh,
		box_ws_old_refusals[BOX_WS_OLD_TAKEN], box_ws_old_refusals[BOX_WS_OLD_PAST_SLOTS],
		box_ws_old_refusals[BOX_WS_OLD_NO_RECORD], box_ws_old_refusals[BOX_WS_OLD_MARKED],
		box_ws_old_refusals[BOX_WS_OLD_NO_BIPED], box_ws_old_refusals[BOX_WS_OLD_PAST_NODES],
		box_ws_old_refusals[BOX_WS_OLD_OWED]);
	if (!box_ws_old_checking)
		return;
	platform_psp_log("wsoldcheck: the made word behind its refresh at %lu refreshes (0: every copy published)",
		box_ws_old_made_behind);
	for (k = 0; k < WSO_FIELDS; k++)
		n += snprintf(line + n, sizeof(line) - n, "%s%s %lu", k ? ", " : "", box_ws_old_field_name[k], box_ws_old_check_missed[k]);
	platform_psp_log("wsoldcheck: %lu live records and %lu cluster lists checked after the refresh; changes no mark caught: %s",
		box_ws_old_check_objects, box_ws_old_check_clusters, line);
	platform_psp_log("wsoldcheck: posed objects' spheres (store_pose.c's writers): %lu of %lu posed records checked changed with no mark",
		box_ws_old_check_posed_spheres, box_ws_old_check_posed);
	platform_psp_log("wsoldcheck: not held at the check (the copy's own NOT_HELD, a reader's fallback, not a missed mark): %lu, %lu past the nodes and %lu owed",
		box_ws_old_check_unheld, box_ws_old_check_unheld_roomless, box_ws_old_check_unheld - box_ws_old_check_unheld_roomless);
	n = 0;
	for (k = 0; k < WSO_FIELDS; k++)
		n += snprintf(line + n, sizeof(line) - n, "%s%s %lu", k ? ", " : "", box_ws_old_field_name[k], box_ws_old_visit_parted[k]);
	platform_psp_log("wsoldcheck: the engine's sweep visited %lu objects; the copy parted from the live world at %lu (%.2f%%): %s; models met %lu, nodes parted at %lu",
		box_ws_old_visits, box_ws_old_visits_parted,
		box_ws_old_visits ? 100.0 * box_ws_old_visits_parted / box_ws_old_visits : 0.0, line,
		box_ws_old_model_visits, box_ws_old_model_visits_parted);
}

void box_ws_old_flipped(unsigned objects, unsigned bipeds, unsigned high_water)
{
	box_ws_old_flips++;
	if (objects > box_ws_old_most_refreshed)
		box_ws_old_most_refreshed = objects;
	if (bipeds > box_ws_old_most_bipeds)
		box_ws_old_most_bipeds = bipeds;
	if (high_water > box_ws_old_high_water)
		box_ws_old_high_water = high_water;
	box_ws_old_publish();
	if (box_ws_old_flips % 300 == 0)
		box_ws_old_log();
}

/* the fields a record holds that differ, as WSO_ bits */
static unsigned box_ws_old_differ(struct box_ws_old_object const *a, struct box_ws_old_object const *b)
{
	unsigned bits = 0;

	if (memcmp(a->center, b->center, sizeof(a->center)) || memcmp(&a->radius, &b->radius, sizeof(a->radius)))
		bits |= 1u << WSO_SPHERE;
	if (a->flags != b->flags)
		bits |= 1u << WSO_FLAGS;
	if (a->next != b->next || a->first_child != b->first_child)
		bits |= 1u << WSO_LINKS;
	if (a->type != b->type)
		bits |= 1u << WSO_TYPE;
	if (a->dead != b->dead)
		bits |= 1u << WSO_DEAD;
	return bits;
}

static unsigned box_ws_old_biped_differ(struct box_ws_old_biped const *a, struct box_ws_old_biped const *b, int motion)
{
	unsigned bits = 0;

	if (a->pill_held != b->pill_held || (a->pill_held &&
		(memcmp(a->pill_base, b->pill_base, sizeof(a->pill_base)) || memcmp(&a->pill_height, &b->pill_height, 8))))
		bits |= 1u << WSO_PILL;
	if (motion && (memcmp(a->position, b->position, sizeof(a->position)) || memcmp(a->velocity, b->velocity, sizeof(a->velocity))))
		bits |= 1u << WSO_MOTION;
	if (a->parent != b->parent || a->player != b->player || a->seat != b->seat || a->passes_through != b->passes_through)
		bits |= 1u << WSO_UNIT;
	return bits;
}

void box_ws_old_check(unsigned absolute, struct box_ws_old_object const *object, struct box_ws_old_biped const *biped,
	int posed)
{
	struct box_ws_old_object const *held;
	unsigned bits = 0, k;

	if (absolute >= BOX_WS_OLD_SLOTS)
		return;
	box_ws_old_check_objects++;
	if (posed)
		box_ws_old_check_posed++;
	held = &box_ws_old_objects[absolute];
	if (held->identifier != object->identifier)
		bits = 1u << WSO_NONE;
	else
	{
		bits = box_ws_old_differ(held, object);
		if (biped)
		{
			if (box_ws_old_biped_of[absolute] < 0)
				bits |= 1u << WSO_NONE;
			else
				bits |= box_ws_old_biped_differ(&box_ws_old_bipeds[box_ws_old_biped_of[absolute]], biped, 1);
		}
	}
	if (!bits)
		return;
	if (posed && (bits & (1u << WSO_SPHERE)))
	{
		/* the crossing's own count; the record's other fields count as ever */
		box_ws_old_check_posed_spheres++;
		bits &= ~(1u << WSO_SPHERE);
		if (!bits && box_ws_old_posed_told < 8)
		{
			box_ws_old_posed_told++;
			platform_psp_log("wsoldcheck: refresh %lu: slot %u (type %u), posed, its sphere changed with no mark",
				box_ws_old_flips, absolute, (unsigned)object->type);
		}
	}
	for (k = 0; k < WSO_FIELDS; k++)
		if (bits & (1u << k))
			box_ws_old_check_missed[k]++;
	if (bits && box_ws_old_check_told < 24)
	{
		box_ws_old_check_told++;
		platform_psp_log("wsoldcheck: refresh %lu: slot %u (type %u)%s changed with no mark (fields %x)",
			box_ws_old_flips, absolute, (unsigned)object->type, posed ? ", posed," : "", bits);
	}
	/* kept true from here, so one missed writer counts once a tick, not on */
	box_ws_old_put(absolute, object, biped);
}

void box_ws_old_visit(long datum_index, struct box_ws_old_object const *now, struct box_ws_old_biped const *biped_now)
{
	struct box_ws_old_object const *held = box_ws_old_object_of(datum_index);
	unsigned bits, k;

	box_ws_old_visits++;
	if (!held)
		bits = 1u << WSO_NONE;
	else
	{
		bits = box_ws_old_differ(held, now);
		if (biped_now)
		{
			struct box_ws_old_biped const *was = box_ws_old_biped_of_index(datum_index);

			bits |= was ? box_ws_old_biped_differ(was, biped_now, 0) : 1u << WSO_NONE;
		}
	}
	if (!bits)
		return;
	box_ws_old_visits_parted++;
	for (k = 0; k < WSO_FIELDS; k++)
		if (bits & (1u << k))
			box_ws_old_visit_parted[k]++;
}
