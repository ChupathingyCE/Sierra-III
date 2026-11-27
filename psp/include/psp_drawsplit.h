/* The drawing's split, level 2 (docs/box_draw_main.md §1.5; psp_drawsplit.c,
keep word "drawsplit", build switch PSP_DRAWSPLIT=1): the fields inside a
part, and the items a frame. psp_fixups.py includes this and places the
hooks only when PSP_DRAWSPLIT=1, so with the switch off no unit sees it.

PSP_DRAWSPLIT_IN(field) opens a field under the part open now (level 1's
part: objects, world, or any other); PSP_DRAWSPLIT_OUT() closes the field
opened last. An item field may hold item fields: the inner one is charged
while it runs, then the outer one again. A whole field charges everything
inside it to itself: the field hooks nested in it read nothing (level 1's
waits still charge their own parts). PSP_DRAWSPLIT_COUNT(item, n) adds n to
the frame's count of an item; it reads no clock. Each hook is a flag's test
when the word is off. Under PSP_DRAWSPLIT=sampled (PSP_DRAWSPLIT_SAMPLED) the
hooks are stores instead, and the clock is an alarm's: below. */

#ifndef PSP_DRAWSPLIT_H
#define PSP_DRAWSPLIT_H

/* the fields; NONE is a part's residual: its time outside every field */
enum psp_drawsplit_field
{
	PSP_DRAWSPLIT_NONE,
	/* item fields */
	PSP_DRAWSPLIT_WALK,
	PSP_DRAWSPLIT_LIGHTS,
	PSP_DRAWSPLIT_MATRICES,
	PSP_DRAWSPLIT_D3D,
	PSP_DRAWSPLIT_INST,
	PSP_DRAWSPLIT_STATE,
	PSP_DRAWSPLIT_ENCODE,
	PSP_DRAWSPLIT_VIS,
	/* whole fields, from here on */
	PSP_DRAWSPLIT_SHADOWS,
	PSP_DRAWSPLIT_FPW,
	PSP_DRAWSPLIT_SOUND,
	PSP_DRAWSPLIT_SKY,
	PSP_DRAWSPLIT_LIGHTPASS,
	PSP_DRAWSPLIT_DECALS,
	PSP_DRAWSPLIT_DIFFUSE,
	PSP_DRAWSPLIT_SPECULAR,
	/* render_window's own world calls, outside the fields above: the
	window's begin (the screen flash, rasterizer_window_begin), the three
	reflection passes, the fog and the screen's fog, the detail objects, the
	antialias (Stubbs' image effect with it), the window's end (bink through
	rasterizer_window_end) */
	PSP_DRAWSPLIT_WINBEGIN,
	PSP_DRAWSPLIT_REFLECT,
	PSP_DRAWSPLIT_FOG,
	PSP_DRAWSPLIT_DETAIL,
	PSP_DRAWSPLIT_AA,
	PSP_DRAWSPLIT_WINEND,
	PSP_DRAWSPLIT_FIELDS
};
#define PSP_DRAWSPLIT_FIRST_WHOLE PSP_DRAWSPLIT_SHADOWS

/* the items a frame: instances (rasterizer_model_begin), model parts drawn
(psp_model_draw returning TRUE), textured level draws (psp_environment_draw
reaching psp_ge_draw_level), counted under the part open (a whole field's
apart, so the sky's parts don't add to objects'); rendered clusters and
objects (render's and render_object_globals' counts, summed over the frame's
windows) whatever part is open */
enum psp_drawsplit_item
{
	PSP_DRAWSPLIT_INSTANCES,
	PSP_DRAWSPLIT_PARTS_DRAWN,
	PSP_DRAWSPLIT_LEVEL_DRAWS,
	PSP_DRAWSPLIT_CLUSTERS,
	PSP_DRAWSPLIT_OBJECTS,
	PSP_DRAWSPLIT_ITEMS
};

#if defined(PSP_DRAWSPLIT) && defined(PSP_DRAWSPLIT_SAMPLED)
/* The sampled form (PSP_DRAWSPLIT=sampled; the lab's
findings/drawsplit2_2026-11-27.md 'The thinner cut, design (a)'; decisions
2026-11-27 04:0x): the exact form's hooks are calls, and any call added to a
function the profile counted changes its cfg checksum, so -fprofile-use drops
its counts (15 draw functions in drawsplit2's B). Here every hook, level 1's
and level 2's, is loads and stores only: no test, no call, no ?:, so a hooked
function keeps the branches A's profile was counted on. The clock is read by
psp_drawsplit.c's alarm, which charges the time since its last run to the code
on the stack's top.

A code is a (part, field) pair with the whole bit: the field in bits 0-4, the
part (psp_drawsplit.c's enum drawsplit_part) in 5-8, bit 9 set inside a whole
field. A push copies the part from the code under it; inside a whole field it
copies the code itself, so what nests in a whole field is the whole field's
(level 1's waits still push their own part, keeping the bit). The index is
masked, so a run past 16 deep wraps instead of testing: the fold counts a
frame that doesn't end at the bottom as unbalanced. The entry is stored
before the index, so the alarm never reads an entry not yet written. */
#define PSP_DRAWSPLIT_STACK 16
#define PSP_DRAWSPLIT_PART_SHIFT 5
#define PSP_DRAWSPLIT_WHOLE_SHIFT 9
#define PSP_DRAWSPLIT_FIELD_MASK 0x1fu
#define PSP_DRAWSPLIT_PART_MASK 0x1e0u
#define PSP_DRAWSPLIT_WHOLE_BIT 0x200u
/* items by the code's part and whole bit: code >> PART_SHIFT */
#define PSP_DRAWSPLIT_ITEM_ROWS 32

struct psp_drawsplit_state
{
	unsigned int sp;
	unsigned short stack[PSP_DRAWSPLIT_STACK];
	unsigned int items[PSP_DRAWSPLIT_ITEM_ROWS][PSP_DRAWSPLIT_ITEMS];
};
extern volatile struct psp_drawsplit_state psp_drawsplit_state;
/* level 1's codes by phase (psp_drawsplit.c fills them): a begin's new code
is (top & keep[phase]) | code[phase], so an unmapped phase (keep all, code 0)
copies the top and a mapped one pushes its part, keeping the whole bit; a
render part's code by phase + 1 (NONE is -1: the render's own time) */
extern unsigned short psp_drawsplit_phase_keep[], psp_drawsplit_phase_code[], psp_drawsplit_render_code[];

/* the field's whole bit: a constant, folded where the field is named */
#define PSP_DRAWSPLIT_WHOLE_OF(f) ((unsigned int)((f) >= PSP_DRAWSPLIT_FIRST_WHOLE) << PSP_DRAWSPLIT_WHOLE_SHIFT)
#define PSP_DRAWSPLIT_PUSH_(code_of_top) ({ \
	unsigned int ds_sp_ = psp_drawsplit_state.sp & (PSP_DRAWSPLIT_STACK - 1); \
	unsigned int ds_top_ = psp_drawsplit_state.stack[ds_sp_]; \
	unsigned int ds_new_ = (code_of_top); \
	ds_sp_ = (ds_sp_ + 1) & (PSP_DRAWSPLIT_STACK - 1); \
	psp_drawsplit_state.stack[ds_sp_] = (unsigned short)ds_new_; \
	psp_drawsplit_state.sp = ds_sp_; \
	(void)0; })
/* ds_m_: all ones inside a whole field (the top copied), else none */
#define PSP_DRAWSPLIT_IN(field) PSP_DRAWSPLIT_PUSH_(({ \
	unsigned int ds_m_ = 0u - ((ds_top_ >> PSP_DRAWSPLIT_WHOLE_SHIFT) & 1u); \
	(ds_top_ & ds_m_) | (((ds_top_ & PSP_DRAWSPLIT_PART_MASK) | PSP_DRAWSPLIT_##field \
		| PSP_DRAWSPLIT_WHOLE_OF(PSP_DRAWSPLIT_##field)) & ~ds_m_); }))
#define PSP_DRAWSPLIT_OUT() ((void)(psp_drawsplit_state.sp = (psp_drawsplit_state.sp - 1) & (PSP_DRAWSPLIT_STACK - 1)))
/* A close after a call that ends its block (a one-statement if's call, an
arm's last call, a function's last call) is a statement after it: GCC's
-fprofile-arcs splits a block after every call that isn't its last statement
(the call's fake edge to the exit), so the block count and the cfg checksum
move with no branch added. There the close goes past the join: the index is
saved before the statement and put back after it, in a block of their own
(host check, lab toolbox/cfgsum.py on /tmp/builder2/dscfg/probe3.c: kept in
all five shapes). The few instructions between the call's return and the join
are charged to the field */
#define PSP_DRAWSPLIT_SAVE() unsigned int psp_ds_saved_ = psp_drawsplit_state.sp
#define PSP_DRAWSPLIT_RESTORE() ((void)(psp_drawsplit_state.sp = psp_ds_saved_))
#define PSP_DRAWSPLIT_COUNT(item, n) ((void)(psp_drawsplit_state.items[psp_drawsplit_state.stack[psp_drawsplit_state.sp \
	& (PSP_DRAWSPLIT_STACK - 1)] >> PSP_DRAWSPLIT_PART_SHIFT][PSP_DRAWSPLIT_##item] += (unsigned int)(n)))
/* level 1's (psp_fixups.py maps halo_perf_log.h's HALO_PERF_* to these under
the sampled form): a begin pushes, an end pops, a render part replaces the
top (the render's parts follow each other at its own depth), a count adds */
#define PSP_DRAWSPLIT_L1_BEGIN(phase) PSP_DRAWSPLIT_PUSH_((ds_top_ & psp_drawsplit_phase_keep[(phase)]) | psp_drawsplit_phase_code[(phase)])
#define PSP_DRAWSPLIT_L1_END(phase) PSP_DRAWSPLIT_OUT()
#define PSP_DRAWSPLIT_L1_RENDER_PART(phase) ((void)(psp_drawsplit_state.stack[psp_drawsplit_state.sp & (PSP_DRAWSPLIT_STACK - 1)] \
	= psp_drawsplit_render_code[(phase) + 1]))
/* main.c's calls around each render phase (psp_fixups.py places them; main.c
isn't counted), so d is the render's span */
void psp_drawsplit_render_begin(void);
void psp_drawsplit_render_end(void);
#define PSP_DRAWSPLIT_OBJECT_IN() ((void)0)
#define PSP_DRAWSPLIT_OBJECT_OUT() ((void)0)
#elif defined(PSP_DRAWSPLIT)
void psp_drawsplit_in(int field);
void psp_drawsplit_out(void);
void psp_drawsplit_count(int item, unsigned int n);
#define PSP_DRAWSPLIT_IN(field) psp_drawsplit_in(PSP_DRAWSPLIT_##field)
#define PSP_DRAWSPLIT_OUT() psp_drawsplit_out()
#define PSP_DRAWSPLIT_COUNT(item, n) psp_drawsplit_count(PSP_DRAWSPLIT_##item, (unsigned int)(n))
#define PSP_DRAWSPLIT_SAVE() ((void)0)
#define PSP_DRAWSPLIT_RESTORE() ((void)0)
/* X1's split (docs/box_draw_main.md §4.4 A2; exact form only): around
render_object's drawn branch, so objects' time inside each object is charged
to the kind its instances were drawn as (psp_drawsplit.c, 'by kind');
psp_ge.c's psp_far_draw calls psp_drawsplit_far with the groups it shows */
void psp_drawsplit_object_in(void);
void psp_drawsplit_object_out(void);
void psp_drawsplit_far(unsigned int groups);
#define PSP_DRAWSPLIT_OBJECT_IN() psp_drawsplit_object_in()
#define PSP_DRAWSPLIT_OBJECT_OUT() psp_drawsplit_object_out()
#else
#define PSP_DRAWSPLIT_OBJECT_IN() ((void)0)
#define PSP_DRAWSPLIT_OBJECT_OUT() ((void)0)
#define PSP_DRAWSPLIT_SAVE() ((void)0)
#define PSP_DRAWSPLIT_RESTORE() ((void)0)
#define PSP_DRAWSPLIT_IN(field) ((void)0)
#define PSP_DRAWSPLIT_OUT() ((void)0)
#define PSP_DRAWSPLIT_COUNT(item, n) ((void)0)
#endif

#endif
