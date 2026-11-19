/* Biped motion's window (docs/box_biped_motion.md section 1c, steps 1b and
   1c): the intent, the solve and the tail run against one biped's record in
   the world store
   (psp/src/box_world_store.c). Here, not in psp/src, because bipeds.c
   includes it (psp_fixups.py BIPEDMOVE) and psp/include is the engine
   units' path to the port's headers. */
#pragma once
#include <stddef.h>
#include "psp_bipedmove_map.h"
#define BOX_WS_DATUM_BYTES 1152  /* the biped datum, datum_layouts/biped.txt */
extern void *box_ws_move_datum;
extern unsigned char *box_ws_move_live;
extern short box_ws_move_map[BOX_WS_DATUM_BYTES];
int box_ws_biped_slots(void);
int box_ws_move_begin(void *datum, long index, int slot, int check);
void box_ws_move_keep(void);
int box_ws_move_resume(void *datum, int slot);
void box_ws_move_end(void);
int box_ws_move_sync_out(void);
void box_ws_move_sync_in(int slot);
int box_ws_move_narrow_out(int which);
void box_ws_move_narrow_in(int slot, int which);
void box_ws_move_drop(void);
void box_ws_move_settle(void);

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
		return box_ws_move_live + first;
	return (unsigned char *)datum + offset;
}

/* A write's place: as box_ws_move_at, and the build fails where the field is
   one the store holds (so the write lands in the record) but its run isn't
   written back at the window's end, where the write would be lost (the lab's
   census_phase4_unit: the write-back set follows the accessor's writes).
   psp_fixups.py (BIPEDMOVE) uses it at every assignment, increment and
   address taken for writing. The test folds away at a constant offset; a
   call that survives it is the error. */
void box_ws_move_write_not_written_back(void)
	__attribute__((error("PSP_BMSW: a write to a field the store holds but doesn't write back (box_ws_biped_move_back)")));
static inline __attribute__((always_inline)) void *box_ws_move_at_w(void *datum, unsigned offset, unsigned size)
{
	short first = box_ws_move_map_c[offset];

	if (first >= 0 && box_ws_move_map_c[offset + size - 1] == first + (short)(size - 1) &&
		(unsigned)(box_ws_move_back_sum_c[offset + size] - box_ws_move_back_sum_c[offset]) != size)
		box_ws_move_write_not_written_back();
	return box_ws_move_at(datum, offset, size);
}

/* PSP_BMS_PLAIN (the lab's wssplit1; recon only): every access a plain
   field access, as before the pass, the split's baseline. Only with the
   window's keep words off: a record written back would undo the datum's
   writes */
#if PSP_BMS_PLAIN
#define PSP_BMS(p, field) ((p)->field)
#define PSP_BMSW(p, field) ((p)->field)
#else
/* the accessor (read, write), as psp_fixups.py's pass writes them */
#define PSP_BMS(p, field) (*(__typeof__(&(p)->field))box_ws_move_at((void *)(p), \
	offsetof(__typeof__(*(p)), field), sizeof((p)->field)))
#define PSP_BMSW(p, field) (*(__typeof__(&(p)->field))box_ws_move_at_w((void *)(p), \
	offsetof(__typeof__(*(p)), field), sizeof((p)->field)))
#endif
/* a call that touches the biped's datum itself, inside the window: the whole
   record (written-back runs out, all in after), the narrow ones (0
   object_translate, 1 object_start_interpolation), or the biped's delete */
#define PSP_BMS_SYNCV(call) ({ int psp_bms_l = box_ws_move_sync_out(); call; box_ws_move_sync_in(psp_bms_l); })
#define PSP_BMS_SYNC(call) ({ int psp_bms_l = box_ws_move_sync_out(); __typeof__(call) psp_bms_r = (call); \
	box_ws_move_sync_in(psp_bms_l); psp_bms_r; })
#define PSP_BMS_NARROWV(which, call) ({ int psp_bms_l = box_ws_move_narrow_out(which); call; box_ws_move_narrow_in(psp_bms_l, which); })
#define PSP_BMS_DROPV(call) ({ box_ws_move_drop(); call; })
