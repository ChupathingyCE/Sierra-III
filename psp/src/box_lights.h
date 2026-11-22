/* box_lights.h - the lighting box (docs/box_lights.md). Slice 1: the reach of
a light that has not changed, kept from one frame to the next.

A level light that is where it was reaches the same triangles of the same
level draw as strongly as it did the frame before. psp_ge.c's light pass
works that out again for every light draw of every frame: the walk over the
draw's triangles and each corner's reach. This keeps, for a light and a draw,
the triangles the light reaches and their corners' reach as the walk made
them, and hands them back while both are unchanged, so the pass goes on from
the lit triangles alone. What follows the eye (the corners from the camera,
the cuts, the bands) and the light's colour are worked out each frame as
before.

The store is the Media Engine's: the pass runs there, and the lists are its
working data, in its own eDRAM. The main CPU reads only the counts. */
#ifndef BOX_LIGHTS_H
#define BOX_LIGHTS_H

/* what a kept list was made from, compared word for word */
struct box_lights_key
{
	unsigned int light[9];  /* position (from the copy's origin), forward, radius, cosine falloff, cosine cutoff */
	unsigned int origin[3]; /* the level copy's origin */
	unsigned int stream;    /* the copy's slot */
	unsigned int serial;    /* the number the copy was made under */
	unsigned int indices;   /* the draw's index list: where it is, */
	unsigned int count;     /* its triangles, */
	unsigned int generation; /* and the geometry cache's generation there (0 for the map's own) */
};

/* a lit triangle as the walk left it */
struct box_lights_triangle
{
	float reach[3];
	unsigned short vertex[3], spare;
};

enum
{
	BOX_LIGHTS_WALK,    /* nothing kept and nothing to keep: the walk as before */
	BOX_LIGHTS_COLLECT, /* the walk, each lit triangle handed to box_lights_add, then box_lights_end */
	BOX_LIGHTS_KEPT     /* a kept list: no walk */
};

/* why a light draw was left to the walk without asking */
enum
{
	BOX_LIGHTS_SPOT,   /* a spotlight: its cone cull and split follow the cone */
	BOX_LIGHTS_GEL,    /* a gel: three reaches a corner */
	BOX_LIGHTS_BAKE,   /* the copy has stencil bake pieces: the index list is swapped a draw */
	BOX_LIGHTS_MODE,   /* another light mode, or lightmemocheck */
	BOX_LIGHTS_REASONS
};

int box_lights_begin(const struct box_lights_key *key, const struct box_lights_triangle **kept, long *kept_count);
void box_lights_add(const unsigned short *vertex, const float *reach);
void box_lights_end(int walked_whole);
void box_lights_refused(int reason);
void box_lights_checked(int differed);
/* "lightkinds", a count only: each light draw by its light's form (0 a point
light, 1 one with a gel, 2 a spotlight) and by whether the light, the draw
and the copy have been seen before as they are. world is the light's place
before the copy's origin is taken from it */
void box_lights_kind(int form, const float *world, const float *forward, float radius, float falloff, float cutoff,
	const void *stream, unsigned int serial, const float *origin, const void *indices, long count, int baked);
/* the main CPU's, every 300 frames: the counts as a log line */
void box_lights_report(void);

#endif
