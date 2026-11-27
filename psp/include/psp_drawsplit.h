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
when the word is off. */

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

#ifdef PSP_DRAWSPLIT
void psp_drawsplit_in(int field);
void psp_drawsplit_out(void);
void psp_drawsplit_count(int item, unsigned int n);
#define PSP_DRAWSPLIT_IN(field) psp_drawsplit_in(PSP_DRAWSPLIT_##field)
#define PSP_DRAWSPLIT_OUT() psp_drawsplit_out()
#define PSP_DRAWSPLIT_COUNT(item, n) psp_drawsplit_count(PSP_DRAWSPLIT_##item, (unsigned int)(n))
#else
#define PSP_DRAWSPLIT_IN(field) ((void)0)
#define PSP_DRAWSPLIT_OUT() ((void)0)
#define PSP_DRAWSPLIT_COUNT(item, n) ((void)0)
#endif

#endif
