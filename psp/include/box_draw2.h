/* The far kind's sub-lists (FARCALL; docs/box_draw_main.md section 4.2a).
Built in by PSP_DRAW2=1 only (psp_build.py), off by default.

A far model's GE words are written by the main CPU while it records the
frame, into this frame's set of a small arena (main RAM, cached, written
back to RAM when a model's sub-list is closed), and the record the Media
Engine replays carries only the sub-list's address: the replay turns it into
BASE and CALL. A set is reused three frames later: under geinflight the main
CPU records frame N+1 while the ME replays N and the GE may still be drawing
N-1, so two sets (section 4.2a's ~22 KB) could be written under the GE.

The word writers below write what pspgu's own functions send (the command
in the top 8 bits, its 24-bit argument below; a float as its top 24 bits),
so a sub-list holds the words the old path sent to the display list. */
#ifndef BOX_DRAW2_H
#define BOX_DRAW2_H

/* a set's room (section 4.2a: ~11 KB at b30's worst window [D]); a model
whose words don't fit is drawn the old way, and counted (draw2_report) */
#define DRAW2_SET_BYTES (12 * 1024)
#define DRAW2_SETS 3

struct draw2_sub
{
	unsigned int *start, *at, *end;
};

/* the main CPU's frame turn: the next set, emptied (its frame's list long
drawn). Called once a frame, before its first far model */
void draw2_frame_begin(void);
/* room for a sub-list of at most max_words words (its RET included) in this
frame's set: 0 when the arena is missing or full */
int draw2_open(struct draw2_sub *sub, unsigned int max_words);
/* the RET, the words written back to RAM for the GE; the bytes written */
unsigned int draw2_close(struct draw2_sub *sub);
/* the arena's figures for the log: its bytes, this frame's words, the
frames' highest, the models drawn the old way for want of room */
void draw2_report(void);
/* the rso count (the lab's decisions 2026-11-27 19:2x (1)), log only: a far
model drawn the old way, by why (rso sent its kept record, rso is capturing
its walk, draw2_far gave 0), with the words its sub-list would hold (its
shown groups' words as draw2_far writes them, and the RET). Returns 1 */
enum { DRAW2_RSO_SENT, DRAW2_RSO_CAUGHT, DRAW2_FELL, DRAW2_WHYS };
int draw2_tally(int why, const void *block, unsigned int mask_low, unsigned int mask_high);

static inline unsigned int draw2_float(float f)
{
	union { float f; unsigned int u; } v;

	v.f = f;
	return v.u >> 8;
}

/* a 4x4 matrix's first three columns, as sceGuSetMatrix (GU_MODEL: 0x3A,
0x3B) and sceGuBoneMatrix (0x2A slot * 12, 0x2B) send them */
static inline unsigned int *draw2_matrix(unsigned int *at, unsigned int place_command, unsigned int place,
	unsigned int data_command, const float *m)
{
	int row, column;

	*at++ = (place_command << 24) | place;
	for (row = 0; row < 4; row++)
		for (column = 0; column < 3; column++)
			*at++ = (data_command << 24) | draw2_float(m[row * 4 + column]);
	return at;
}

/* sceGuColorMaterial (0x53) */
static inline unsigned int *draw2_color_material(unsigned int *at, unsigned int components)
{
	*at++ = (0x53u << 24) | (components & 0xFFFFFFu);
	return at;
}

/* sceGuMaterial(GU_AMBIENT | GU_DIFFUSE, colour): 0x55 and 0x58 the
ambient's colour and alpha, 0x56 the diffuse's */
static inline unsigned int *draw2_material(unsigned int *at, unsigned int colour)
{
	*at++ = (0x55u << 24) | (colour & 0xFFFFFFu);
	*at++ = (0x58u << 24) | (colour >> 24);
	*at++ = (0x56u << 24) | (colour & 0xFFFFFFu);
	return at;
}

/* a draw's words as gu_draw (psp_ge.c) and sceGuDrawArray write them: the
vertex type, the indices' and the vertices' BASE and address, the primitive */
static inline unsigned int *draw2_draw(unsigned int *at, int prim, unsigned int vtype, int count,
	const void *indices, const void *vertices)
{
	*at++ = (0x12u << 24) | (vtype & 0xFFFFFFu);
	*at++ = (0x10u << 24) | (((unsigned int)indices >> 8) & 0xF0000u);
	*at++ = (0x02u << 24) | ((unsigned int)indices & 0xFFFFFFu);
	*at++ = (0x10u << 24) | (((unsigned int)vertices >> 8) & 0xF0000u);
	*at++ = (0x01u << 24) | ((unsigned int)vertices & 0xFFFFFFu);
	*at++ = (0x04u << 24) | ((((unsigned int)prim << 16) | (unsigned int)count) & 0xFFFFFFu);
	return at;
}

#endif
