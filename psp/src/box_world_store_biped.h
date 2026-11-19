/* The world store's biped section: what the generated v0 list (box_world_store.h)
   leaves out and biped motion's intent reads or writes (docs/box_biped_motion.md
   section 1c, 1b; the lab's findings intent_fields_vs_store, checked against the
   datum layout, psp/sierra/datum_layouts/biped.txt). Hand-written, beside the
   generated header, until the store's census rule is looked at again
   (docs/box_world_store.md section 2, Open 2026-11-18). Each field carries its
   datum offset; the copy runs are datum offset, offset in its struct, bytes. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "box_world_store.h"

/* written by biped motion: added to its unit section */
struct box_ws_biped_unit2 {  /* 48 B */
	float angular_velocity[3];  /* +60, 12 B: the flying turn */
	float crouch;  /* +1124, 4 B */
	float bank;  /* +1128, 4 B: the flying turn */
	float ground_plane[4];  /* +1132, 16 B */
	int32_t support_surface_index;  /* +1072, 4 B */
	int16_t looking_screen_index_unused;  /* +588 is held in unit.animation whole (box_ws_biped_anim) */
	char state;  /* +1066, 1 B */
	char airborne_ticks;  /* +1113, 1 B */
	char slipping_ticks;  /* +1114, 1 B */
	char pad[3];
};
_Static_assert(sizeof(struct box_ws_biped_unit2) == 48, "box_ws: box_ws_biped_unit2");
_Static_assert(offsetof(struct box_ws_biped_unit2, crouch) == 12, "box_ws: box_ws_biped_unit2.crouch");
_Static_assert(offsetof(struct box_ws_biped_unit2, support_surface_index) == 36, "box_ws: box_ws_biped_unit2.support_surface_index");
_Static_assert(offsetof(struct box_ws_biped_unit2, looking_screen_index_unused) == 40, "box_ws: box_ws_biped_unit2.looking_screen_index");
_Static_assert(offsetof(struct box_ws_biped_unit2, state) == 42, "box_ws: box_ws_biped_unit2.state");
_Static_assert(offsetof(struct box_ws_biped_unit2, airborne_ticks) == 43, "box_ws: box_ws_biped_unit2.airborne_ticks");

/* only read by biped motion (damage and the engine write them): added to read */
struct box_ws_biped_read2 {  /* 16 B */
	float recent_body_damage;  /* +168, 4 B: the stun test */
	float body_stun;  /* +980, 4 B: the stun scale */
	uint32_t biped_flags;  /* +1060, 4 B */
	int16_t landing;  /* +1120, 2 B */
	int16_t pad;
};
_Static_assert(sizeof(struct box_ws_biped_read2) == 16, "box_ws: box_ws_biped_read2");

static const struct box_ws_run box_ws_biped_unit2_runs[] = {
	{ 60, 0, 12 },
	{ 1124, 12, 24 },  /* crouch, bank, ground_plane */
	{ 1072, 36, 4 },
	/* looking_screen_index (+588) is held in unit.animation whole, below */
	{ 1066, 42, 1 },
	{ 1113, 43, 2 },  /* airborne_ticks, slipping_ticks */
};
static const struct box_ws_run box_ws_biped_read2_runs[] = {
	{ 168, 0, 4 },
	{ 980, 4, 4 },
	{ 1060, 8, 4 },
	{ 1120, 12, 2 },
};

/* unit.animation held whole, in biped motion's unit section: the tail hands
   &unit.animation to the animation's helpers (units.c), so the accessor
   redirects the struct only when every byte of it is in one run (the lab's
   census_phase4_unit, block 1). The generated runs inside it (read, unit,
   posing and control: 29 of its 72 bytes) are left out of the record, by
   box_ws_init and tools/bipedmove_map.py alike. */
struct box_ws_biped_anim {  /* 72 B */
	uint8_t animation[72];  /* +584, unit.animation */
};
_Static_assert(sizeof(struct box_ws_biped_anim) == 72, "box_ws: box_ws_biped_anim");
static const struct box_ws_run box_ws_biped_anim_runs[] = {
	{ 584, 0, 72 },
};

/* The runs outside unit and control that biped motion's window (intent to
   tail) writes, so they're written back with unit and control: the tail's
   biped flags (+1060) and landing (+1120) from read2, walk's idle ticks
   (+108, its 4 B run), and the animation's graph index (+124, posing's run;
   unit_set_animation). A PSP_BMS write to any other held run fails the
   build (PSP_BMSW, psp_bipedmove.h). Each entry is a run's datum offset. */
static const uint16_t box_ws_biped_move_back[] = { 1060, 1120, 108, 124 };

/* The window's narrow syncs (psp_fixups.py BIPEDMOVE): a call that reads or
   writes a few of the biped's held fields on the datum itself has those
   fields put to the datum before and got back after, not the whole record.
   Datum offset, bytes. object_translate reads the flags and writes the
   position, the flags, the cluster and the map link (the census's 28 B);
   object_start_interpolation writes the interpolation frames. */
struct box_ws_span { uint16_t datum, bytes; };
static const struct box_ws_span box_ws_biped_translate_put[] = { { 4, 4 } };
static const struct box_ws_span box_ws_biped_translate_get[] = { { 4, 4 }, { 12, 12 }, { 76, 4 }, { 196, 4 } };
static const struct box_ws_span box_ws_biped_interp_put[] = { { 132, 4 } };
static const struct box_ws_span box_ws_biped_interp_get[] = { { 132, 4 } };
/* written through to the datum where the intent ends: other bipeds' solves
   read it (the bump) before this biped's own solve */
static const struct box_ws_span box_ws_biped_intent_put[] = { { 24, 12 } };

#include "psp_bipedmove.h"  /* step 1b's accessor, beside the engine's headers (psp/include) */
