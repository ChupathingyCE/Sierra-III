/* The ME's half of the pill sweep, slice 1 (docs/box_collision.md §11): the
   level's half of each staged biped's gather (the BSP walk and the shapes
   made from it) handed to the Media Engine as one batch a tick, made by the
   box's code (box_collbsp.c, box_sweep.c) on whichever CPU gets to it, while
   the main CPU runs the bipeds' intents. The objects' half stays on the main
   CPU, in the engine's gather, fed the level's answer. Built with
   PSP_SWEEPME=1; keep word sweepme, sweepmecheck to compare every answer
   taken with the engine's own level half.

   The level is read only (a broken breakable surface is the one change, and
   each record carries the flags it was made with), so the answer is the
   engine's whatever the ME's timing: exact.

   Engine units and psp/src both include this header: it names no engine
   type. */
#pragma once
#include <stdint.h>

/* the level's arrays as the tags hold them, for the box's walk */
struct psp_sweepme_level
{
	const void *nodes, *planes, *leaves, *references, *nodes2d, *surfaces, *edges, *vertices;
	int node_count, plane_count, leaf_count, reference_count, node2d_count, surface_count, edge_count, vertex_count;
};

#define PSP_SWEEPME_BREAKABLE_WORDS 8 /* the engine's 256 breakable surfaces */
#define PSP_SWEEPME_LEAVES 64         /* the walk's leaves a record holds */

extern int psp_sweepme_on;     /* -1 unread, 0 off, 1 on */
extern int psp_sweepme_pending; /* the record the next gather takes, or -1 */
extern int psp_sweepme_check;   /* sweepmecheck */

/* the tick's batch: the level and its breakable flags as the intents begin */
void psp_sweepme_begin(const struct psp_sweepme_level *level, const void *breakable_flags);
/* one staged biped's move, as its gather will ask it: the record's index, or
   -1 (no room: its gather walks the level as before) */
int psp_sweepme_add(long biped_index, const float center[3], float radius, float height, float width, int structure);
/* every record handed over */
void psp_sweepme_close(void);
/* the record of this biped whose query is this one, bit for bit; -1 none */
int psp_sweepme_find(long biped_index, const float center[3], float radius, float height, float width, int structure);
/* PSP_TICK2_MOTIONNEW=1 (slice 1's task 5, Part 9's O(1) find): the same answer, the record looked up at the place
   psp_sweepme_add gave (or -1, none); a place not this biped's falls back to the scan. Counted: by place, no place,
   a place not its own (0 expected: a biped is added at most once a tick) */
int psp_sweepme_find_at(int place, long biped_index, const float center[3], float radius, float height, float width,
	int structure);
extern unsigned long psp_sweepme_at[3];
/* the record's answer, made and readable here (waited for, or made here);
   NULL when it can't stand for the engine's walk (cut short, too many
   leaves, another level, a breakable surface changed since) */
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
/* every record made and every job collected: the arena is free for the next tick */
void psp_sweepme_end(void);
