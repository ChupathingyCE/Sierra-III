/* The world store's first slice (docs/box_world_store.md §9): the pill
   sweep's object inputs and biped motion's reads of other bipeds, as the
   world stood when the object walk began. One copy, refreshed at that point
   from the objects written since the last one, so a reader on the other core
   (the ME's sweep, the parallel phase) reads a world nothing writes while the
   main CPU runs the walk. Built with PSP_WSOLD=1; keep word wsold.

   Each field holds only the bits its readers test, so a write to a bit
   nobody here reads doesn't count as a change. Engine units and psp/src both
   include this header: it names no engine type. */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* object slots held (absolute datum index): the object array's own maximum
   (psp_capacity.h's HALO_PORT_MAXIMUM_OBJECTS_PER_MAP; objects.c checks it
   against the engine's at compile time), so no map can make an object the
   copy has no slot for (d20 reaches 1,070: docs/box_world_store.md §9) */
#define BOX_WS_OLD_SLOTS 2048
#define BOX_WS_OLD_BIPEDS 96

/* every object the sweep can meet: 32 B, two to a line */
struct box_ws_old_object {
	float center[3];       /* +80 bounding sphere */
	float radius;          /* +92 */
	uint32_t flags;        /* +4, only invisible, no collisions, movie star */
	int32_t next;          /* +196, the sibling chain the child walk follows */
	int32_t first_child;   /* +200 */
	int16_t identifier;    /* the datum index's high half; 0: no record */
	uint8_t type;          /* +100 */
	uint8_t dead;          /* +182's dead bit */
};
_Static_assert(sizeof(struct box_ws_old_object) == 32, "box_ws_old_object");

/* a biped's: the pill the sweep tests and what the bump reads, 64 B */
struct box_ws_old_biped {
	float pill_base[3];    /* biped_get_physics_pill's base, as the sweep raises it */
	float pill_height, pill_width;
	float position[3];     /* +12 */
	float velocity[3];     /* +24 */
	int32_t parent;        /* +204 */
	int32_t player;        /* unit +456 */
	int16_t seat;          /* unit +672 */
	uint8_t passes_through; /* biped +1060's passes-through-bipeds bit */
	uint8_t pill_held;     /* 0 for a child: its pill is its parent's node's, made on demand */
	uint8_t pad[8];
};
_Static_assert(sizeof(struct box_ws_old_biped) == 64, "box_ws_old_biped");

/* the clusters' collideable lists as the walk began (docs/box_world_store.md
   §12): each cluster's objects in the order the engine's list held them, a
   run of datum indices in the members array. Up to the structure's 512
   clusters; the collideable partition holds at most 2,048 references
   (HALO_PORT_MAXIMUM_CLUSTER_REFERENCES), so the runs always fit */
#define BOX_WS_OLD_CLUSTERS 512
#define BOX_WS_OLD_MEMBERS 2048
struct box_ws_old_cluster { uint16_t first, count; };

/* a model's live nodes as the walk began: each node the sweep walks under the
   object's permutations (collision_model_get_features_in_sphere's test), its
   collision BSP (the tag's, read where it lies) and its matrix (scale,
   forward, left, up, position). 768 nodes: sized from sweepme2's first runs'
   high water (the `wsold:` line), a model past them has no nodes held */
#define BOX_WS_OLD_NODES 768
struct box_ws_old_node { const void *bsp; float m[13]; };
_Static_assert(sizeof(void *) != 4 || sizeof(struct box_ws_old_node) == 56, "box_ws_old_node");
enum
{
	BOX_WS_OLD_MODEL_NONE,     /* not a model the sweep walks (or no record) */
	BOX_WS_OLD_MODEL_HELD,     /* its live nodes, first..first+count */
	BOX_WS_OLD_MODEL_NO_MODEL, /* a vehicle, scenery, machine or control without a collision model */
	BOX_WS_OLD_MODEL_NOT_HELD, /* owed (its matrices weren't made) or past the nodes: a reader can't answer */
};
struct box_ws_old_model { uint16_t first; uint8_t count; uint8_t state; };

extern struct box_ws_old_object box_ws_old_objects[BOX_WS_OLD_SLOTS];
extern struct box_ws_old_biped box_ws_old_bipeds[BOX_WS_OLD_BIPEDS];
extern int16_t box_ws_old_biped_of[BOX_WS_OLD_SLOTS]; /* -1: none */
extern struct box_ws_old_cluster box_ws_old_clusters[BOX_WS_OLD_CLUSTERS];
extern const int32_t *box_ws_old_members;
extern int box_ws_old_cluster_count; /* the level's clusters held; 0: none (a list couldn't be read) */
extern const void *box_ws_old_cluster_level; /* the structure BSP they're of */
extern struct box_ws_old_model box_ws_old_models[BOX_WS_OLD_SLOTS];
extern struct box_ws_old_node box_ws_old_nodes[BOX_WS_OLD_NODES];
extern int box_ws_old_on;
extern int box_ws_old_checking; /* wsoldcheck */
extern int box_ws_old_parts;    /* the clusters and the models' nodes held too (keep word wsoldparts, or wsoldcheck) */

/* the readers: the record of the object as the walk began, or NULL (made
   later, past the slots, or the copy is off) */
static inline const struct box_ws_old_object *box_ws_old_object_of(long datum_index)
{
	unsigned absolute = (unsigned)datum_index & 0xffff;
	const struct box_ws_old_object *record;

	if (absolute >= BOX_WS_OLD_SLOTS)
		return NULL;
	record = &box_ws_old_objects[absolute];
	return record->identifier && record->identifier == (int16_t)((unsigned long)datum_index >> 16) ? record : NULL;
}

static inline const struct box_ws_old_biped *box_ws_old_biped_of_index(long datum_index)
{
	unsigned absolute = (unsigned)datum_index & 0xffff;

	if (!box_ws_old_object_of(datum_index) || box_ws_old_biped_of[absolute] < 0)
		return NULL;
	return &box_ws_old_bipeds[box_ws_old_biped_of[absolute]];
}

/* the writers' mark: the object changed since the copy was taken */
void box_ws_old_mark(long datum_index);

/* the engine's side (psp_fixups.py FIXES_WSOLD, objects.c) makes a record
   from a datum; the store keeps or compares it */
void box_ws_old_put(unsigned absolute, struct box_ws_old_object const *object, struct box_ws_old_biped const *biped);
/* at the walk's start: 0 the copy is off, 1 refresh the marked, 2 fill
   every slot (the first time, or after box_ws_old_forget) */
int box_ws_old_begin(void);
/* the objects to refresh: the marked ones, every biped held (their
   records are 2 KB in all, cheaper than a mark at each of their writers),
   and with the parts every model the copy has as NOT_HELD (means B's fill
   of owed nodes, in store_pose.c, carries no mark) */
int box_ws_old_marked(uint16_t *absolute, unsigned capacity);
void box_ws_old_flipped(unsigned objects, unsigned bipeds, unsigned high_water);
/* wsoldcheck: a fresh record of a live object against the copy, after the
   refresh (a change no mark caught) */
void box_ws_old_check(unsigned absolute, struct box_ws_old_object const *object, struct box_ws_old_biped const *biped);
/* wsoldcheck: at each object the engine's sweep visits, the copy against
   what the engine reads now (the one-tick-old world's reach) */
void box_ws_old_visit(long datum_index, struct box_ws_old_object const *now, struct box_ws_old_biped const *biped_now);

/* a map started or a game state loaded: every record is of another world,
   so the next refresh fills every slot and every cluster again */
void box_ws_old_forget(void);

/* the models' nodes (with box_ws_old_parts): an object's run, made from its
   datum by the engine's side (count nodes, or state NO_MODEL / NOT_HELD with
   none). put keeps it, check compares it with what's held */
void box_ws_old_model_put(unsigned absolute, int state, const struct box_ws_old_node *nodes, unsigned count);
void box_ws_old_model_check(unsigned absolute, int state, const struct box_ws_old_node *nodes, unsigned count);
/* wsoldcheck at the sweep: a model's nodes held against what it reads now */
void box_ws_old_model_visit(unsigned absolute, int state, const struct box_ws_old_node *nodes, unsigned count);

/* the clusters (with box_ws_old_parts): a reconnect or disconnect's mark on
   each cluster its object leaves or joins; the refresh then copies the runs
   of the unmarked and takes the marked ones' lists again */
void box_ws_old_mark_cluster(int cluster);
/* the refresh: 1 every cluster to be taken again (the first time, another
   level, a revert), 0 only the marked */
int box_ws_old_clusters_begin(const void *level, int cluster_count);
int box_ws_old_cluster_marked(int cluster);
/* a cluster's list as the engine holds it now (taken), or its run as held */
void box_ws_old_cluster_put(int cluster, const int32_t *objects, unsigned count);
void box_ws_old_cluster_keep(int cluster);
void box_ws_old_clusters_end(unsigned taken);
/* wsoldcheck: a cluster's held run against its list now, after the refresh */
void box_ws_old_cluster_check(int cluster, const int32_t *objects, unsigned count);
