/* Step 1b of biped motion (docs/box_biped_motion.md section 1c): the
   intent run against one biped's record in the world store
   (psp/src/box_world_store.c). Here, not in psp/src, because bipeds.c
   includes it (psp_fixups.py BIPEDMOVE) and psp/include is the engine
   units' path to the port's headers. */
#pragma once
#include <stddef.h>
#include "psp_bipedmove_map.h"
#define BOX_WS_DATUM_BYTES 1152  /* the biped datum, datum_layouts/biped.txt */
extern void *box_ws_move_datum;
extern short box_ws_move_map[BOX_WS_DATUM_BYTES];
extern unsigned char box_ws_move_record[];
int box_ws_biped_slots(void);
int box_ws_move_begin(void *datum);
void box_ws_move_out(void);
void box_ws_move_in(void);
void box_ws_move_end(int check);

/* A field's place: in the record when this datum's record is live and the
   store holds every byte of the field in one run, else in the datum. The
   field's last byte is checked as well as its first, so a field the store
   holds only part of (or across two runs that aren't side by side in the
   record) stays on the datum whole. The map is a constant table and every
   PSP_BMS passes a constant offset and size, so the compiler settles the
   field's place at the call: a field the store doesn't hold is a plain datum
   access, and one it holds costs one compare of the datum with the live one.
   box_ws_move_map (built at launch from the same runs) checks the table. */
static inline __attribute__((always_inline)) void *box_ws_move_at(void *datum, unsigned offset, unsigned size)
{
	short first = box_ws_move_map_c[offset];

	if (first >= 0 && box_ws_move_map_c[offset + size - 1] == first + (short)(size - 1) &&
		datum == box_ws_move_datum && datum)
		return box_ws_move_record + first;
	return (unsigned char *)datum + offset;
}
