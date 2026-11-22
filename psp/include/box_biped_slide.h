/*
Biped motion's slide, stage 5a (docs/box_biped_motion.md §4b): every staged
biped's slide made as one pass before the solve, from its intent's call and the
list its gather packed, so the slide's few dozen code lines stay resident over
all of them. The solve then takes the result instead of sliding.

This file holds the results: each one is keyed by the call it was made from
(the flags, the position, the velocity, the pill and the plane limit, compared
bit for bit), so a call that differs in any input, whoever changed it, finds no
result and slides inline as before. The engine's side (psp_fixups.py,
FIXES_BMSLIDE in collisions.c) makes the slide and hands it here.
*/
#ifndef BOX_BIPED_SLIDE_H
#define BOX_BIPED_SLIDE_H

/* collision_move_pill's limit for a biped's move, and its plane record
(struct collision_plane, checked against this size where the engine's side
compiles) */
#define BOX_BMSLIDE_PLANES 16
#define BOX_BMSLIDE_PLANE_BYTES 44
/* one result a staged biped (the staged pass's own limit) */
#define BOX_BMSLIDE_RESULTS 256
/* a move hits one or two planes as a rule (~70-110 B a result, §4b); a tick
that fills this leaves its later bipeds to slide inline, counted */
#define BOX_BMSLIDE_ARENA 8192

struct box_bmslide_call
{
	long biped_index;
	unsigned long flags;
	float position[3], velocity[3], height, width;
	short maximum;
};

struct box_bmslide_result
{
	struct box_bmslide_call call;
	short slot; /* the gather's slot the slide read */
	short count; /* collision_move_pill's answer */
	unsigned short planes; /* where its planes start in the arena */
	short taken;
	float new_position[3], new_velocity[3];
};

void box_bmslide_begin(void);
/* the planes' buffer a slide writes into, BOX_BMSLIDE_PLANES long */
void *box_bmslide_scratch(void);
/* a result for this call, its planes copied out of the scratch buffer; 0 when
the tick's room is spent (the solve slides inline) */
int box_bmslide_store(const struct box_bmslide_call *call, short slot, short count,
	const float *new_position, const float *new_velocity);
/* the untaken result made from exactly this call, or 0 */
struct box_bmslide_result *box_bmslide_find(const struct box_bmslide_call *call);
const void *box_bmslide_planes(const struct box_bmslide_result *result);
/* bmslidecheck: the inline slide's answer against the result's */
void box_bmslide_compare(const struct box_bmslide_result *result, short count,
	const float *new_position, const float *new_velocity, const void *planes);
/* a staged biped whose slide was left inline: the move's take wouldn't take
its list, or it has no gathered move */
void box_bmslide_left(void);
void box_bmslide_end(void);

#endif
