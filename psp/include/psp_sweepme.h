/* The ME's half of the pill sweep (docs/box_collision.md §11): each staged
   biped's gather handed to the Media Engine as one batch a tick, made by the
   box's code (box_collbsp.c, box_sweep.c) on whichever CPU gets to it, while
   the main CPU runs the bipeds' intents. Built with PSP_SWEEPME=1.

   Slice 1 (keep word sweepme): the level's half (the BSP walk and the shapes
   made from it); the objects' half stays on the main CPU, in the engine's
   gather, fed the level's answer. The level is read only (a broken breakable
   surface is the one change, and each record carries the flags it was made
   with), so the answer is the engine's whatever the ME's timing: exact.
   sweepmecheck compares every answer taken with the engine's own level half.

   Slice 2 (keep word sweepme2, which implies sweepme): the whole gather,
   the objects' half read from the world store's one-tick-old copy
   (box_ws_old.h: the objects, the clusters' lists and the models' nodes as
   the object walk began), so the gather pass only takes lists. Not exact:
   the objects are as the walk began, not as the gather finds them ("plays
   the same", the outcome census). A record whose objects the copy can't
   answer (an object made this tick, a model whose matrices are owed) carries
   the level's half alone, as slice 1's. sweepme2check makes every record
   and plays the engine's own gather, comparing each whole answer with it.

   Engine units and psp/src both include this header: it names no engine
   type. */
#pragma once
#include <stdint.h>

/* the level's arrays as the tags hold them, for the box's walk */
struct psp_sweepme_level
{
	const void *nodes, *planes, *leaves, *references, *nodes2d, *surfaces, *edges, *vertices;
	int node_count, plane_count, leaf_count, reference_count, node2d_count, surface_count, edge_count, vertex_count;
	/* the structure's leaf records (16 B, the cluster at +8): slice 2's cluster walk */
	const void *structure_leaves;
	int structure_leaf_count;
};

#define PSP_SWEEPME_BREAKABLE_WORDS 8 /* the engine's 256 breakable surfaces */
#define PSP_SWEEPME_LEAVES 64         /* the walk's leaves a record holds */

extern int psp_sweepme_on;     /* -1 unread, 0 off, 1 on */
extern int psp_sweepme_pending; /* the record the next gather takes, or -1 */
extern int psp_sweepme_check;   /* sweepmecheck */
extern int psp_sweepme_whole;   /* slice 2 (sweepme2): the whole gather from the copy */
extern int psp_sweepme_whole_check; /* sweepme2check */

/* the tick's batch: the level and its breakable flags as the intents begin */
void psp_sweepme_begin(const struct psp_sweepme_level *level, const void *breakable_flags);
/* one staged biped's move, as its gather will ask it (its collision flags,
   the biped the gather ignores): the record's index, or -1 (no room: its
   gather walks the level as before) */
int psp_sweepme_add(long biped_index, const float center[3], float radius, float height, float width, unsigned long flags);
/* every record handed over */
void psp_sweepme_close(void);
/* the record of this biped whose query is this one, bit for bit; -1 none */
int psp_sweepme_find(long biped_index, const float center[3], float radius, float height, float width, unsigned long flags);
/* the record's answer, made and readable here (waited for, or made here):
   2 the whole gather (slice 2: the gather walks nothing), 1 the level's half
   (the gather makes the objects' half from its leaves), 0 none that can
   stand for the engine's walk (cut short, too many leaves, another level, a
   breakable surface changed since) */
struct psp_sweepme_answer
{
	int leaf_count;
	const unsigned short *leaves;
	short count[3]; /* spheres, cylinders, prisms, in the engine's layouts */
	const void *spheres, *cylinders, *prisms;
};
int psp_sweepme_answer(int index, const void *level_planes, const void *breakable_flags, struct psp_sweepme_answer *answer);
/* sweepmecheck: the engine's own answer against the record's */
void psp_sweepme_checked(int same);
/* sweepme2check: a whole answer against the engine's own gather: the same
   to the bit, or how far apart (features neither list matched, features
   matched by object and surface that moved, the farthest move in world
   units) */
void psp_sweepme_whole_checked(int same, int unmatched, int moved, float farthest);
/* every record made and every job collected: the arena is free for the next tick */
void psp_sweepme_end(void);
