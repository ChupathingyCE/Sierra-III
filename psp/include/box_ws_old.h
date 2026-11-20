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

extern struct box_ws_old_object box_ws_old_objects[BOX_WS_OLD_SLOTS];
extern struct box_ws_old_biped box_ws_old_bipeds[BOX_WS_OLD_BIPEDS];
extern int16_t box_ws_old_biped_of[BOX_WS_OLD_SLOTS]; /* -1: none */
extern int box_ws_old_on;
extern int box_ws_old_checking; /* wsoldcheck */

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
   every slot (the first time) */
int box_ws_old_begin(void);
/* the objects to refresh: the marked ones and every biped held (their
   records are 2 KB in all, cheaper than a mark at each of their writers) */
int box_ws_old_marked(uint16_t *absolute, unsigned capacity);
void box_ws_old_flipped(unsigned objects, unsigned bipeds, unsigned high_water);
/* wsoldcheck: a fresh record of a live object against the copy, after the
   refresh (a change no mark caught) */
void box_ws_old_check(unsigned absolute, struct box_ws_old_object const *object, struct box_ws_old_biped const *biped);
/* wsoldcheck: at each object the engine's sweep visits, the copy against
   what the engine reads now (the one-tick-old world's reach) */
void box_ws_old_visit(long datum_index, struct box_ws_old_object const *now, struct box_ws_old_biped const *biped_now);
