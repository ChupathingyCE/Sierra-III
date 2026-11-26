/*
store_motion.c: the bipeds' motion as the store's passes (slice 1's task 5; docs/box_world_store.md section 19)

Step 3, first piece (the lead 2026-11-26 01:3x: the code in Part 3's order, I first): the INTENT table. I keeps one
entry per living biped's intent, dense in the intents' own order n, with a k-to-n map by staged position k beside it
(Part 7's correction to Part 3: staged positions run to 256 and take in the dead). It replaces bipedstage3's record
pool and psp_bs3_find's hash with its fallback scan (bipeds.c, FIXES_BIPEDSTAGE): G and M walk k in I's order and
read n from the map, one byte.

An entry holds what G reads: the move's call as I leaves it (the flags and the pill) and the gather's sphere, made
once here from the call's position and velocity by the four expressions store_coll_gather and
psp_sweepme_after_intent each make today, on the same floats, so the take's verdicts are old-only's (Part 7, clause
9; the lead 00:2x (1)). It also holds the ME's record for that sphere, so G's find is one read with today's scan as
its counted fallback (Part 9, taken 00:3x (5)). What M reads after the call (the physics' record, the animation's
update data, the facing) is M's own table by the same n, written with I's code; G's walk doesn't touch it.

64 entries, old-only's pool (clause 10, ruled 00:2x (2): branch (i) if no tick goes over it on the six maps, both
seeds). A 65th intent gets no entry and writes nothing past the table; it is counted, and the proof reads the count
at 0. What M does for a biped with no entry is M's piece's to name.

Censuses: bipedstage3's five counters (intents; stopped at the move; at the tail; over the pool; solves resumed) are
re-earned as this file's line, with the pool's three the ruling asks (ticks over it, intents past it, the peak a
tick) and the ME find's three. Its line prints every 1,000 staged ticks and whenever a tick over the pool or a new
peak comes, so a run of any length ends with its counts in the log (bipedstage3's every 3,000 missed both legs).

Built with PSP_TICK2_MOTIONNEW=1 (psp_build.py), as bipeds.c is.
*/

#include <string.h>
#include "cseries.h"
#include "math/real_math.h"
#include "physics/collisions.h"
#include "psp_sweepme.h"
#include "units/units.h" /* struct unit_animation_update_data, M's table's */
#include "ai/ai_communication.h"
#include "game/game.h"
#include "objects/objects.h"
#include "physics/physics.h"
#include "units/biped_definitions.h"
#include "units/bipeds.h"
#include "units/vehicle_definitions.h"
#include "units/vehicles.h"
#include "mcc_seats.h" /* port: port/linux/game/mcc_seats.c */
#include "stubbs_mode.h" /* port: port/linux/game/stubbs_mode.c */
#include "stubbs_player.h" /* port: port/linux/game/stubbs_player.c */
#include "store_motion.h"

static struct store_intent store_intents[STORE_INTENT_ENTRIES];
/* M's own table by the same n (Part 7): bipedstage3's record less what G reads. I's code writes it at the call, M
reads it; G's walk never touches these lines */
struct store_motion
{
	struct unit_animation_update_data animation;
	struct biped_physics physics;
	word in_flags;
	real_vector2d facing;
};
static struct store_motion store_motions[STORE_INTENT_ENTRIES];
static unsigned char store_intent_map[STORE_INTENT_STAGED];
static short store_intent_count;   /* entries kept this tick */
static char store_intent_over;     /* this tick went over the pool */
/* staged ticks; intents opened; kept at the move, at the tail; over the pool: intents, ticks; the peak a tick */
static unsigned long store_intent_ticks, store_intent_stat[5];
static short store_intent_peak;

void store_intent_begin(void)
{
	memset(store_intent_map, STORE_INTENT_NONE, sizeof(store_intent_map));
	store_intent_count = 0;
	store_intent_over = 0;
}

short store_intent_open(long biped_index)
{
	struct store_intent *intent;

	if (store_intent_count >= STORE_INTENT_ENTRIES)
	{
		store_intent_stat[3]++;
		store_intent_over = 1;
		return -1;
	}
	intent = &store_intents[store_intent_count];
	intent->biped_index = biped_index;
	intent->stop = 0;
	intent->me = -1;
	store_intent_stat[0]++;
	return store_intent_count;
}

/* an intent that stops before either point (the debug skip, a dead biped at rest) never comes here, and its place
is the next intent's, as a record with no stop was bipedstage3's */
void store_intent_keep(short k, short n, int stop, unsigned long flags, real_point3d const *position,
	real_vector3d const *velocity, real height, real width)
{
	struct store_intent *intent;

	if (n < 0) /* over the pool: counted at the open, and nothing is written past the table */
		return;
	intent = &store_intents[n];
	intent->stop = (char)stop;
	intent->me = -1;
	intent->flags = 0;
	if (stop == 1)
	{
		intent->flags = flags;
		intent->height = height;
		intent->width = width;
		/* the gather's sphere, as store_coll_gather and psp_sweepme_after_intent make it (character for character:
		the ME's answer is matched on these floats) */
		intent->center.x = position->x + velocity->i * 0.5f;
		intent->center.y = position->y + velocity->j * 0.5f;
		intent->center.z = height * 0.5f + (position->z + velocity->k * 0.5f);
		intent->radius = magnitude3d(velocity) * 0.5f + height * 0.5f + width + 1e-4f;
		/* the ME's level half, psp_sweepme_after_intent's tests in its order */
		if (psp_sweepme_on > 0 && flags &&
			(flags & (FLAG(_collision_test_structure_bit) | FLAG(_collision_test_media_bit) | FLAG(_collision_test_objects_bit))))
			intent->me = (short)psp_sweepme_add(intent->biped_index, &intent->center.x, intent->radius, height, width,
				(flags & FLAG(_collision_test_structure_bit)) != 0);
	}
	store_intent_stat[stop == 1 ? 1 : 2]++;
	if ((unsigned short)k < STORE_INTENT_STAGED)
		store_intent_map[k] = (unsigned char)n;
	store_intent_count = n + 1;
}

struct store_intent const *store_intent_of(short k)
{
	unsigned char n = (unsigned short)k < STORE_INTENT_STAGED ? store_intent_map[k] : STORE_INTENT_NONE;

	return n == STORE_INTENT_NONE ? NULL : &store_intents[n];
}

struct store_motion *store_motion_at(short n)
{
	return (unsigned short)n < STORE_INTENT_ENTRIES ? &store_motions[n] : NULL;
}

void store_intent_end(void)
{
	void platform_psp_log(const char *format, ...);
	int new_line = 0;

	store_intent_ticks++;
	if (store_intent_over)
	{
		store_intent_stat[4]++;
		new_line = 1;
	}
	if (store_intent_count > store_intent_peak)
	{
		store_intent_peak = store_intent_count;
		new_line = 1;
	}
	if (new_line || store_intent_ticks % 1000 == 0)
		platform_psp_log("store_intent: %lu staged ticks: intents %lu, kept at the move %lu, at the tail %lu; over the pool: %lu intents in %lu ticks; peak %d of %d; ME find by place %lu, no place %lu, place not its own %lu",
			store_intent_ticks, store_intent_stat[0], store_intent_stat[1], store_intent_stat[2], store_intent_stat[3],
			store_intent_stat[4], store_intent_peak, STORE_INTENT_ENTRIES, psp_sweepme_at[0], psp_sweepme_at[1],
			psp_sweepme_at[2]);
	store_intent_count = 0;
}

/* ---- I, the intent (step 3, third piece on: a function a run, in biped_update's order) ----
Written from 2e5eac00's text as slice 1's fixups leave it (the ship form, /tmp/t5g/ship), over the store's records:
CONTROL (tick2_unit_ctl_of), BIPED's lines (tick2_unit_bip2_of, _bip3_of) and ANIM (tick2_unit_anim_of), one lookup
each where the hoist (PSP_TICK2_HOIST) worked each out at its first use. The datum's fields are read plain: under
PSP_TICK2_ROOT, PSP_BMS is the datum's own field (psp_bipedmove.h: the window never goes live) and PSP_BMS_SYNC the
call alone, so the plain text is the one that runs today. The biped and its definition are passed, never looked up
again: FIXES_UNITHOIST's held pair, without its keep word (unithoistold looked them up again, to the same answer).
Helpers that stay in bipeds.c (they have callers past biped_update) lose their static there, by
tick2_motionnew_helpers_cut, and store_motion.h declares them; helpers only biped_update reached are on the delete
list and come here, rewritten as I's own. */

extern boolean debug_biped_skip_update, rider_ejection; /* bipeds.c 633, 636 */
real network_distributed_rider_ejection_up(void);        /* port: network_distributed.c (as bipeds.c 378) */
extern const long psp_datum_biped_bytes;                 /* units.c (FIXES_BSPR): the datum's size, its tail after */
extern void *const *psp_bspr_datum_at;                   /* collision_bsp.c (FIXES_BSPR): the turn's owner */
typedef char store_motion_bspr_datum_fits[sizeof(struct biped_datum) <= 0x480 ? 1 : -1]; /* as biped_update's */

/* FIXES_BSPR's owner: BSPR answers only while a biped's datum is set ("NULL outside biped_update", psp_fixups.py
   34163). I and M each set it around a biped's turn, as biped_update did around its whole body, and put back what
   was there */
static inline void *const *store_motion_owner_set(long biped_index)
{
	void *const *outer = psp_bspr_datum_at;
	struct object_header_datum *header = object_header_get(biped_index);

	psp_bspr_datum_at = header->data_size >= psp_datum_biped_bytes + 640 ? (void *const *)&header->datum : NULL;
	return outer;
}

static inline void store_motion_owner_put(void *const *outer)
{
	psp_bspr_datum_at = outer;
}

/* biped_vehicle_speech (bipeds.c 2176-2268, on the delete list: only biped_update's rider branch called it), the
   same tests in the same order on the biped I holds (it looked the biped up again by index: the same datum) */
static void store_intent_rider_speech(long biped_index, struct biped_datum *biped)
{
	struct vehicle_runtime_datum *vehicle = vehicle_runtime_get(biped->object.parent_object_index);
	struct unit_definition *vehicle_definition = vehicle_definition_get(vehicle->definition_index);

	if (TEST_FLAG(vehicle_definition->unit.flags, _unit_causes_passenger_dialogue_bit) &&
		biped->unit.actor_index != NONE &&
		tick2_unit_anim_of(biped->object.root)->state != _unit_state_ai_impulse &&
		biped->unit.gunner_inactive_ticks > 120)
	{
		long game_time = game_time_get();
		struct tick2_unit_bip3 *line3 = tick2_unit_bip3_of(biped->object.root);

		if (vehicle->vehicle.airborne_ticks > 30 &&
			(line3->last_falling_communication_time == NONE ||
			line3->last_falling_communication_time + 15 < game_time))
		{
			boolean falling = FALSE;

			line3->last_falling_communication_time = game_time;
			if (biped_find_ground_surface(biped_index, global_down3d, 8.f, NULL, NULL) == NONE)
			{
				real_vector3d predicted_direction;

				falling = TRUE;
				predicted_direction.i = vehicle->object.root->translational_velocity.i*60.f;
				predicted_direction.j = vehicle->object.root->translational_velocity.j*60.f;
				predicted_direction.k = vehicle->object.root->translational_velocity.k*60.f -
					global_gravity*1800.f;
				if (normalize3d(&predicted_direction) > 0.f)
				{
					real_vector3d landing_normal;

					if (biped_find_ground_surface(biped_index, &predicted_direction, 8.f, NULL, &landing_normal) != NONE)
					{
						if (landing_normal.k > 0.3f)
							falling = FALSE;
					}
				}
			}

			if (falling)
			{
				ai_communication_event(_ai_communication_vehicle_falling, biped_index, NONE, NONE, NONE, NONE, NULL);
			}
			else if (vehicle->object.root->up.k > 0.6f &&
				magnitude3d(&vehicle->object.root->angular_velocity) < 0.052359879f)
			{
				ai_communication_event(_ai_communication_vehicle_woohoo, biped_index, NONE, NONE, NONE, NONE, NULL);
			}
			else
			{
				ai_communication_event(_ai_communication_vehicle_scared, biped_index, NONE, NONE, NONE, NONE, NULL);
			}
		}
	}
}

/* I's opening, biped_update's head (bipeds.c 5105-5202 in the ship form) from the parent test to crouch: the rider
   branch, the facing's snap, CONTROL's settle (the facing flattened, a throttle under 0.1 zeroed: Part 3's first
   correction, I's and not U's), the state from ANIM, the airborne and slipping counters (the bits last tick's M set
   in the datum: Part 3's third correction), and crouch. Returns 1 when the biped goes on to turning and the
   physics' build; 0 for a rider (a parent, a seat or a biped), whose turn has no move and ends at the tail, as
   old-only's rider reaches biped_update's tail (bipedstage3 kept it at stop 2) */
static int store_intent_head(long biped_index, struct biped_datum *biped, struct biped_definition *definition,
	struct unit_animation_update_data *animation)
{
	struct tick2_unit_ctl *control;
	struct tick2_unit_bip2 *line2;

	if (biped->object.parent_object_index != NONE)
	{
		struct object_datum *parent = object_get(biped->object.parent_object_index);

		if (parent->object.type == _object_type_vehicle)
		{
			struct unit_datum *vehicle = vehicle_get(biped->object.parent_object_index);

			store_intent_rider_speech(biped_index, biped);
			if (TEST_FLAG(tick2_unit_ctl_of(biped->object.root)->control_flags, _unit_control_action_bit))
				unit_try_and_exit_seat(biped_index);

			/* (port: below 0 as the Xbox's, but in a network game whose version's layout says another height,
			network_distributed.c) */
			if (rider_ejection &&
				vehicle->object.root->up.k < network_distributed_rider_ejection_up() &&
				TEST_FLAG(vehicle->object.flags, _object_on_ground_bit))
			{
				unit_exit_seat_end(biped_index);
			}
		}
		else if (parent->object.type == _object_type_biped)
		{
			animation->state_desired = (char)((parent->object.damage_flags & FLAG(_object_dead_bit)) | 0x20);
			/* port: MCC seats use seat animations, not infection attachment. */
			if (mcc_seat_update(biped_index))
				animation->state_desired = _unit_state_idle;
		}
		return 0;
	}

	biped_snap_facing_with(biped_index, biped, definition);
	control = tick2_unit_ctl_of(biped->object.root);
	if (TEST_FLAG(biped->object.damage_flags, _object_dead_bit) ||
		!(definition->biped.flags & (FLAG(_biped_flying_bit) | FLAG(_biped_climbs_anything_bit))))
	{
		control->desired_facing_vector.k = 0.f;
		if (normalize3d(&control->desired_facing_vector) == 0.f)
			control->desired_facing_vector = *global_forward3d;
	}

	line2 = tick2_unit_bip2_of(biped->object.root);
	switch (tick2_unit_anim_of(biped->object.root)->state)
	{
	case _unit_state_move_front:
	case _unit_state_move_back:
	case _unit_state_move_left:
	case _unit_state_move_right:
		line2->state = biped_state_moving;
		break;

	case _unit_state_idle:
	case _unit_state_turn_left:
	case _unit_state_turn_right:
		line2->state = biped_state_idle;
		break;

	default:
		line2->state = biped_state_unknown;
		break;
	}

	if (magnitude_squared3d(&control->throttle) < 0.1f*0.1f)
		control->throttle = *global_zero_vector3d;

	if (TEST_FLAG(biped->biped.flags, _biped_airborne_bit))
	{
		if (line2->airborne_ticks < SCHAR_MAX)
			++line2->airborne_ticks;
	}
	else
	{
		line2->airborne_ticks = 0;
	}

	if (TEST_FLAG(biped->biped.flags, _biped_slipping_bit))
	{
		if (line2->slipping_ticks < SCHAR_MAX)
			++line2->slipping_ticks;
	}
	else
	{
		line2->slipping_ticks = 0;
	}

	animation->state_desired = _unit_state_idle;
	animation->crouching = TEST_FLAG(control->control_flags, _unit_control_crouch_modifier_bit);
	/* port: Stubbs mode's crouch button is his sprint (port/linux/game/stubbs_player.c) */
	if (animation->crouching && stubbs_mode_active() && stubbs_player_never_crouches(biped_index))
		animation->crouching = FALSE;

	biped_verify_object_vectors(biped_index, "pre-turning");
	return 1;
}
