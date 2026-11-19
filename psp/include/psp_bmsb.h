/* Biped motion's means B (docs/box_biped_motion.md, "Means B"; the store's
   page, docs/box_world_store.md section 10): the window's fields that only
   its own functions touch held in a packed record per staged biped
   (psp/src/box_bmsb.c), filled once after the unit pass and written back
   once after the solves. bipeds.c includes this in place of the 1c
   accessor (psp_fixups.py, BIPEDMOVE under PSP_BMSB). Each access is
   settled when it's compiled: the field's place is a constant, so a held
   field is a plain load from the function's record pointer and any other a
   plain load from the datum, with no compare at run time. */
#pragma once
#include <stddef.h>
#include "psp_bmsb_map.h"

/* the record of the biped whose update runs (box_bmsb.c): set at
   biped_update's entry, from its staged slot or a scratch record; each
   function of the window takes it once, as psp_bmsb_r */
extern unsigned char *psp_bmsb_rec;
extern int psp_bmsb_current;
int psp_bmsb_enter(long biped_index, void *datum);
void psp_bmsb_leave(int token, void *datum);
void psp_bmsb_drop(void);

/* the build fails on a field the record holds only part of (or in two runs
   not side by side), so no access reaches half a field in each place */
void psp_bmsb_partly_held(void)
	__attribute__((error("PSP_BMS: a field means B's record holds only part of (tools/bmsb_map.py)")));
/* and on a write to a held field the window doesn't write back */
void psp_bmsb_write_not_written_back(void)
	__attribute__((error("PSP_BMSW: a write to a field means B's record holds read-only (tools/bmsb_map.py)")));

static inline __attribute__((always_inline)) int psp_bmsb_held(unsigned offset, unsigned size)
{
	unsigned held = (unsigned)(psp_bmsb_held_sum_c[offset + size] - psp_bmsb_held_sum_c[offset]);
	short first = psp_bmsb_map_c[offset];

	if (held == 0)
		return 0;
	if (held != size || first < 0 || psp_bmsb_map_c[offset + size - 1] != first + (short)(size - 1))
		psp_bmsb_partly_held();
	return 1;
}

static inline __attribute__((always_inline)) void *psp_bmsb_at(unsigned char *record, void *datum, unsigned offset, unsigned size)
{
	if (psp_bmsb_held(offset, size))
		return record + psp_bmsb_map_c[offset];
	return (unsigned char *)datum + offset;
}

static inline __attribute__((always_inline)) void *psp_bmsb_at_w(unsigned char *record, void *datum, unsigned offset, unsigned size)
{
	if (psp_bmsb_held(offset, size) &&
		(unsigned)(psp_bmsb_back_sum_c[offset + size] - psp_bmsb_back_sum_c[offset]) != size)
		psp_bmsb_write_not_written_back();
	return psp_bmsb_at(record, datum, offset, size);
}

/* the accesses as psp_fixups.py's pass writes them: psp_bmsb_r is the
   function's own copy of the record pointer, taken at its entry */
#define PSP_BMS(p, field) (*(__typeof__(&(p)->field))psp_bmsb_at(psp_bmsb_r, (void *)(p), \
	offsetof(__typeof__(*(p)), field), sizeof((p)->field)))
#define PSP_BMSW(p, field) (*(__typeof__(&(p)->field))psp_bmsb_at_w(psp_bmsb_r, (void *)(p), \
	offsetof(__typeof__(*(p)), field), sizeof((p)->field)))
/* the biped's own delete inside the window: its record written back first */
#define PSP_BMS_DROPV(name, call) ({ psp_bmsb_drop(); call; })
