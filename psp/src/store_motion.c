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
at 0. A biped with no entry gets M's whole update (store_motion_update), as old-only ran biped_update whole for it.

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
static struct store_motion store_motions[STORE_INTENT_ENTRIES + 1]; /* and the spare: M's whole update's (M below) */
static unsigned char store_intent_map[STORE_INTENT_STAGED];
static short store_intent_count;   /* entries kept this tick */
static char store_intent_over;     /* this tick went over the pool */
/* staged ticks; intents opened; kept at the move, at the tail; over the pool: intents, ticks; the peak a tick */
static unsigned long store_intent_ticks, store_intent_stat[5];
static short store_intent_peak;
/* M's (M's design part 6): resumed at the call, at the tail; whole with no intent: alive, dead; whole with an intent
   not its own; whole outside the pass; the whole update's build stopped short */
static unsigned long store_motion_stat[7];

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
	/* M's line beside I's, so the run's last lines hold both. The proof reads them against each other: resumed at
	   the call = kept at the move, at the tail = kept at the tail; whole and alive <= the intents over the pool (a
	   biped can die between); not its own and outside the pass 0. bipedstage3's "solves resumed" is the first two */
	if (new_line || store_intent_ticks % 1000 == 0)
		platform_psp_log("store_motion: %lu staged ticks: resumed at the call %lu, at the tail %lu; whole with no intent: alive %lu, dead %lu; whole with an intent not its own %lu; whole outside the pass %lu; whole stopped short of the call %lu",
			store_intent_ticks, store_motion_stat[0], store_motion_stat[1], store_motion_stat[2], store_motion_stat[3],
			store_motion_stat[4], store_motion_stat[5], store_motion_stat[6]);
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

/* biped_update from the pre-update verify to the move's call, I's and M's whole update's (defined after the driver,
   store_intent_run) */
static int store_intent_body(long biped_index, struct biped_datum *biped, struct biped_definition *definition,
	struct store_motion *motion, unsigned long *flags, real_point3d *position, real_vector3d *velocity);

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

/* I's turning, biped_update_turning (bipeds.c 4004-4295 in the ship form, on the delete list: only biped_update
   called it), the same tests and the same float expressions in the same order. CONTROL and line 2 are looked up once
   each where the hoist worked each out at its first use (tick2_unit_ctl_of and _bip2_of are the record's address
   and no more: the same pointer either way), ANIM once on the ground branch, which alone reads it. The snap is
   biped_snap_facing_with on the pair I holds: biped_snap_facing was it over biped_get and the definition's lookup,
   the same datum and definition (FIXES_UNITHOIST's unithoistold answer). PSP_RECON's psp_unithoist_check is left
   out: the pair here is always passed, never NULL, so there's no lookup to check against */
static void store_intent_turning(long biped_index, struct biped_datum *biped, struct biped_definition *definition,
	struct unit_animation_update_data *animation)
{
	unsigned long flags = definition->biped.flags;
	struct tick2_unit_ctl *control = tick2_unit_ctl_of(biped->object.root);
	struct tick2_unit_bip2 *line2 = tick2_unit_bip2_of(biped->object.root);

	if (TEST_FLAG(flags, _biped_flying_bit) &&
		!TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
	{
		boolean nearly_stationary = FALSE;
		real_vector3d target_facing;
		real_vector3d turn_axis;
		real turn_rate;
		real target_bank;
		real bank_blend;
		real bank_time;
		real_rectangle2d facing_bounds;
		real angular_velocity_limit;
		real angular_acceleration_limit;

		if (magnitude_squared3d(&biped->object.root->translational_velocity)<0.00027777778f &&
			magnitude_squared3d(&biped->object.root->angular_velocity)<0.0000013538552f &&
			magnitude_squared3d(&control->throttle)<0.010000001f)
		{
			real stationary_turning_threshold =
				TEST_FLAG(control->control_flags, _unit_control_exact_facing_bit) ?
				0.99000001f : definition->biped.runtime_cosine_stationary_turning_threshold;

			if (dot_product3d(&control->desired_facing_vector, &biped->object.root->forward)>stationary_turning_threshold)
			{
				nearly_stationary = TRUE;
			}
		}

		if (nearly_stationary)
		{
			target_facing = biped->object.root->forward;
		}
		else
		{
			real pitch = definition->biped.flying_pitch_amount*control->throttle.k;

			target_facing = control->desired_facing_vector;
			if (pitch!=0.f)
			{
				target_facing.k += pitch;
				if (normalize3d(&target_facing)==0.f)
					target_facing = control->desired_facing_vector;
			}
		}

		cross_product3d(&biped->object.root->forward, &biped->object.root->up, &turn_axis);
		turn_rate = dot_product3d(&turn_axis, &control->desired_facing_vector)*
			(10.f/3.f)*control->throttle.i - control->throttle.j;
		if (turn_rate>1.5f)
			turn_rate = 1.5f;

		target_bank = definition->biped.flying_bank_amount*turn_rate;
		if (line2->bank*target_bank>0.f)
		{
			real bank_ratio = line2->bank/target_bank;

			if (bank_ratio>1.f)
				bank_ratio = 1.f;
			bank_blend = 1.f-bank_ratio;
		}
		else
		{
			bank_blend = 1.f;
		}

		bank_time = definition->biped.flying_bank_apply_time*bank_blend +
			definition->biped.flying_bank_decay_time*(1.f-bank_blend);
		if (bank_time>0.f)
		{
			line2->bank += (target_bank-line2->bank)/(bank_time*TICKS_PER_SECOND);
		}
		else
		{
			line2->bank = target_bank;
		}

		facing_bounds.x0 = -_pi;
		facing_bounds.x1 = _pi;
		facing_bounds.y0 = -_pi/2.f;
		facing_bounds.y1 = _pi/2.f;
		angular_velocity_limit =
			definition->biped.flying_angular_velocity/TICKS_PER_SECOND;
		angular_acceleration_limit =
			definition->biped.flying_angular_acceleration/
			(TICKS_PER_SECOND*TICKS_PER_SECOND);
		if (angular_acceleration_limit==0.f)
		{
			biped->object.root->forward = target_facing;
		}
		else
		{
			unit_euler_aiming_update(
				NULL,
				&biped->object.root->forward,
				&target_facing,
				&biped->object.root->angular_velocity,
				&facing_bounds,
				angular_velocity_limit,
				angular_acceleration_limit);
		}

		biped_snap_facing_with(biped_index, biped, definition);
		biped_verify_object_vectors(biped_index, "post-fly-turn");
	}
	else
	{
		struct unit_animation *anim = tick2_unit_anim_of(biped->object.root);

		if (anim->base_seat_index!=_unit_base_seat_asleep)
		{
			boolean flaming = anim->base_seat_index==_unit_base_seat_flaming;
			real_vector3d turn_axis;
			real turn_error;
			real facing_alignment;
			boolean turn_right;

			if (TEST_FLAG(flags, _biped_climbs_anything_bit))
			{
				real_vector3d cross;
				real_vector3d error;

				cross_product3d(&biped->object.root->up, &control->desired_facing_vector, &cross);
				cross_product3d(&cross, &biped->object.root->up, &turn_axis);
				if (normalize3d(&turn_axis)==0.f)
					turn_axis = biped->object.root->forward;

				cross_product3d(&turn_axis, &biped->object.root->forward, &error);
				turn_error = dot_product3d(&error, &biped->object.root->up);
				facing_alignment = dot_product3d(&turn_axis, &biped->object.root->forward);
			}
			else
			{
				real_vector2d forward_2d;
				real_vector2d turn_axis_2d;

				turn_axis = control->desired_facing_vector;
				turn_axis.k = 0.f;
				turn_axis_2d.i = turn_axis.i;
				turn_axis_2d.j = turn_axis.j;
				if (normalize2d(&turn_axis_2d)==0.f)
				{
					turn_axis = biped->object.root->forward;
				}
				else
				{
					turn_axis.i = turn_axis_2d.i;
					turn_axis.j = turn_axis_2d.j;
				}
				turn_axis_2d.i = turn_axis.i;
				turn_axis_2d.j = turn_axis.j;

				forward_2d.i = biped->object.root->forward.i;
				forward_2d.j = biped->object.root->forward.j;
				turn_error = cross_product2d(&turn_axis_2d, &forward_2d);
				facing_alignment = dot_product2d(&turn_axis_2d, &forward_2d);
			}

			turn_right = turn_error>0.f;
			if (facing_alignment<-0.89999998f)
			{
				if (anim->state==_unit_state_turn_right)
					turn_right = TRUE;
				else if (anim->state==_unit_state_turn_left)
					turn_right = FALSE;
			}

			if (line2->state==biped_state_moving ||
				TEST_FLAG(flags, _biped_turns_without_animating_bit))
			{
				if (!TEST_FLAG(control->control_flags, _unit_control_look_dont_turn_bit))
				{
					real turn_step =
						definition->biped.moving_turning_speed/TICKS_PER_SECOND;
					real cosine = (real)cos(turn_step);
					real sine = (real)sin(turn_step);
					real progress;

					if (turn_right)
						sine = -sine;

					if (TEST_FLAG(flags, _biped_climbs_anything_bit))
					{
						real_vector3d cross;

						rotate_vector_about_axis(&biped->object.root->forward, &biped->object.root->up, sine, cosine);
						cross_product3d(&turn_axis, &biped->object.root->forward, &cross);
						progress = dot_product3d(&cross, &biped->object.root->up);
					}
					else
					{
						real_vector2d forward_2d;
						real_vector2d turn_axis_2d;

						forward_2d.i = biped->object.root->forward.i;
						forward_2d.j = biped->object.root->forward.j;
						turn_axis_2d.i = turn_axis.i;
						turn_axis_2d.j = turn_axis.j;
						rotate_vector2d(&forward_2d, sine, cosine, &forward_2d);
						biped->object.root->forward.i = forward_2d.i;
						biped->object.root->forward.j = forward_2d.j;
						progress = cross_product2d(&turn_axis_2d, &forward_2d);
					}

					if ((!turn_right || progress<0.f) &&
						(turn_right || progress>0.f))
					{
						if (TEST_FLAG(flags, _biped_climbs_anything_bit))
						{
							real_vector3d rotation_axis;

							cross_product3d(&biped->object.root->up, &turn_axis, &rotation_axis);
							if (normalize3d(&rotation_axis)>0.f)
							{
								cross_product3d(&rotation_axis, &biped->object.root->up, &biped->object.root->forward);
							}
						}
						else
						{
							biped->object.root->forward = turn_axis;
							biped->object.root->forward.k = 0.f;
							biped->object.root->up = *global_up3d;
						}

						normalize3d(&biped->object.root->forward);
					}
				}

				biped_verify_object_vectors(biped_index, "post-moving-turn");
			}
			else if (line2->state==biped_state_idle &&
				!flaming &&
				!TEST_FLAG(biped->unit.flags, _unit_aim_without_turning_bit) &&
				!TEST_FLAG(control->control_flags, _unit_control_look_dont_turn_bit))
			{
				real stationary_turning_threshold =
					TEST_FLAG(control->control_flags, _unit_control_exact_facing_bit) ?
					0.99000001f : definition->biped.runtime_cosine_stationary_turning_threshold;

				if (facing_alignment<stationary_turning_threshold &&
					!TEST_FLAG(definition->unit.flags, _unit_is_special_bit))
				{
					animation->state_desired = turn_right ?
						_unit_state_turn_right : _unit_state_turn_left;
				}

				biped_verify_object_vectors(biped_index, "post-standing-turn");
			}
		}
	}
}

/* I's move build to the call: biped_update_moving (bipeds.c 4297 on in the ship form) and biped_update_physics up to
   the move's call (bipedstage3's stop 1, 3302-3304), the next piece's (the coder's brief). It fills M's entry (the
   physics record, the in-flags, the facing) and the call's flags, position and velocity, and returns 1 at the call;
   0 where the build stops short, which is _moving's dead biped at rest alone (4324-4329), never one of I's living
   bipeds. Declared here so the driver names its contract; defined with the build */
static int store_intent_moving(long biped_index, struct biped_datum *biped, struct biped_definition *definition,
	struct unit_animation_update_data *animation, struct store_motion *motion, unsigned long *flags,
	real_point3d *position, real_vector3d *velocity);

/* I's driver for staged biped k: biped_update's opening and bipedstage3's intent (psp_bipedstage3_intent; both on
   the delete list) over I's functions, in their order. The open comes first, as bipedstage3 counted an intent before
   its update ran: over the pool nothing runs (bipedstage3 ran no update for it either: M runs its whole update). The
   debug skip returns before the owner is set, as biped_update's did, and keeps nothing. Then the owner around
   store_intent_body (below: biped_update from the pre-update verify to the move's call, the part M's whole update
   shares), and the keep at the stop the body gives: at the call (stop 1) or, for a rider, at the tail (stop 2), with
   the animation in M's entry either way (bipedstage3's record took it at both stops); a build stopped short (0)
   keeps nothing, as a record with no stop was bipedstage3's. Left out: profile_enter(biped_update_section), which
   goes with biped_update, and BIPEDMOVE's psp_bms_start/_keep, which do nothing under PSP_TICK2_ROOT (psp_bms_on is
   0 there, so the window never opens) */
void store_intent_run(short k, long biped_index)
{
	struct biped_datum *biped;
	struct store_motion *motion;
	void *const *outer;
	unsigned long flags = 0;
	real_point3d position;
	real_vector3d velocity;
	int stop;
	short n = store_intent_open(biped_index);

	if (n < 0)
		return;
	if (debug_biped_skip_update)
		return;
	biped = biped_get(biped_index);
	motion = &store_motions[n];
	outer = store_motion_owner_set(biped_index);
	stop = store_intent_body(biped_index, biped, biped_definition_get(biped->definition_index), motion, &flags,
		&position, &velocity);
	if (stop)
		store_intent_keep(k, n, stop, flags, &position, &velocity, motion->physics.height, motion->physics.width);
	store_motion_owner_put(outer);
}

/* biped_update from the pre-update verify (5052) to the move's call: the animation's opening values (idle, not
   crouching), the head, turning with its post-turning verify for a biped not dead, and the move's build, into motion.
   Returns the stop: 1 at the call (flags, position and velocity are the call's, M's entry holds the physics record,
   the in-flags and the facing); 2 for a rider (no move: the turn goes on at the tail); 0 where the build stops short
   (_moving's dead biped at rest, 4324-4329: the turn goes on after the move, at 5178). I keeps 1 and 2 as an intent;
   M's whole update (store_motion_update) goes on from the same stop over the spare entry. The owner is the caller's */
static int store_intent_body(long biped_index, struct biped_datum *biped, struct biped_definition *definition,
	struct store_motion *motion, unsigned long *flags, real_point3d *position, real_vector3d *velocity)
{
	biped_verify_object_vectors(biped_index, "pre-update");
	motion->animation.state_desired = _unit_state_idle;
	motion->animation.crouching = FALSE;
	if (!store_intent_head(biped_index, biped, definition, &motion->animation))
		return 2;
	if (!TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
	{
		store_intent_turning(biped_index, biped, definition, &motion->animation);
		biped_verify_object_vectors(biped_index, "post-turning");
	}
	return store_intent_moving(biped_index, biped, definition, &motion->animation, motion, flags, position, velocity);
}

/* ---- M, the motion (step 3, M's design: box_world_store.md 19 'M's design', parts 1, 2, 4 and 6) ----
M is bipedstage3's solve as the store's own code, over I's tables: biped_update's resume (5056-5064) and its three
labels, as three entry points: (a) at the call (stop 1: the physics tail, _moving's tail, then biped_update's tail
from the post-moving verify), (b) after the move (5178, only the whole update's build stopped short reaches it), and
(c) the tail (5273, stop 2, the rider). Only the record's fields cross from I to M: every goto of old-only's resume
skipped the build's assignments and its tails read none of them (Part 1), so M's entry and the intent's flags are the
whole crossing. The tails are the next pieces' (the coder's), declared here with their contracts. */

/* the physics tail, biped_update_physics from the move's call (3315) to its end (3849), on M's physics record: the
   debug skip's plane or the reuse's try, store_coll_move with G's take and the reuse's store, and the clip and the
   record's outputs. Its five locals come in made as old-only's resume made them (3039-3047): in_flags and facing
   M's entry's, collision_flags the intent's (or the whole update's own build's), position and velocity made again
   from the record (3302-3304, in the caller); climbing it works out from physics->in_flags at its head, as 3026 did.
   It writes the record and the caller's position and velocity as the tail wrote its locals. The next piece's */
static void store_motion_call(struct biped_physics *physics, word in_flags, real_vector2d facing,
	unsigned long collision_flags, real_point3d *position, real_vector3d *velocity);

/* _moving's tail, biped_update_moving from the call's return (4792) to its end (4956), on M's physics record after
   store_motion_call: the elevator, the record's outputs to the datum, landing, jumping, bumps and falling damage
   (biped_start_landing, biped_update_jumping, biped_jump, biped_bumped_object, biped_falling_damage come with it).
   The biped and definition are the held pair (4319 looked the definition up again: the same answer); the build's
   movement_scale and in_flags locals are never read (the tail reads physics.in_flags, 4943, 4948). The next piece's */
static void store_motion_moved(long biped_index, struct biped_datum *biped, struct biped_definition *definition,
	struct unit_animation_update_data *animation, struct biped_physics *physics);

/* biped_update's tail: with moved, from the post-moving verify (5178) through dead/air/land/slip, the melee,
   footsteps, falling danger and the discard (5264) and on; without (a rider, stop 2), from 5273 alone: then
   unit_update_animation with the animation, biped_jump on 1, the idle ticks and the post-update verify (5288). The
   biped and definition are the held pair. The next pieces' (the limp body first: biped_update_dead calls it) */
static void store_motion_tail(long biped_index, struct biped_datum *biped, struct biped_definition *definition,
	struct unit_animation_update_data *animation, int moved);

short store_motion_cur = -1; /* the pass's staged position for the biped it updates, -1 outside the pass */
/* the spare entry (the table's 65th) is the whole update's: never mapped, never read by G, written and read in one
   biped's turn. A nested whole update would share it; it would come in as an intent not its own (cur is the outer
   biped's), counted, and the proof reads that count at 0 */
#define STORE_MOTION_SPARE STORE_INTENT_ENTRIES

/* M from a stop: (a) for 1, (b) for 0, (c) for 2. position and velocity are made again from the record by 3302-3304's
   three expressions: nothing writes the record between 3304 and bipedstage3's copy of it (3313, 4786-4788), so the
   same operands give the same floats, and one add can't contract */
static void store_motion_from(long biped_index, struct biped_datum *biped, struct biped_definition *definition,
	struct store_motion *motion, int stop, unsigned long collision_flags)
{
	if (stop == 1)
	{
		real_point3d position = motion->physics.position;
		real_vector3d velocity = motion->physics.new_velocity;

		velocity.k += motion->physics.crouch_velocity;
		store_motion_call(&motion->physics, motion->in_flags, motion->facing, collision_flags, &position, &velocity);
		store_motion_moved(biped_index, biped, definition, &motion->animation, &motion->physics);
	}
	store_motion_tail(biped_index, biped, definition, &motion->animation, stop != 2);
}

/* the biped's datum_update (object_types.c 310 retargets here with the delete cut): biped_update's signature, TRUE
   always. The debug skip first, as biped_update's (no owner, nothing counted). Then the owner. A biped the pass is
   updating whose staged position holds its own intent resumes it: the pre-update verify again (a kept biped gets it
   twice a tick, once in I and once here, as on old-only), the animation the entry's, and M from the intent's stop.
   Any other case is the whole update, counted by kind: no intent (alive: over the pool; dead: phase 0, no record),
   an intent not its own (0 expected), or outside the pass (0 expected). It runs store_intent_body into the spare
   entry, so the pre-update verify runs once, and M from the body's stop, with the build's own flags at the call; its
   move gathers live (G has no entry for it: the take finds no slot, and store_coll_move queries the world). The
   opening values I's body writes are written over by the entry's animation, so a resume doesn't write them.
   psp_bms_resume/_stop and profile_enter/_exit go, as in I */
boolean store_motion_update(long biped_index)
{
	struct biped_datum *biped;
	struct biped_definition *definition;
	struct store_intent const *intent;
	void *const *outer;

	if (debug_biped_skip_update)
		return TRUE;
	biped = biped_get(biped_index);
	definition = biped_definition_get(biped->definition_index);
	intent = store_motion_cur >= 0 ? store_intent_of(store_motion_cur) : NULL;
	outer = store_motion_owner_set(biped_index);
	if (intent && intent->biped_index == biped_index)
	{
		biped_verify_object_vectors(biped_index, "pre-update");
		store_motion_stat[intent->stop == 1 ? 0 : 1]++;
		store_motion_from(biped_index, biped, definition, &store_motions[intent - store_intents], intent->stop,
			intent->flags);
	}
	else
	{
		struct store_motion *motion = &store_motions[STORE_MOTION_SPARE];
		unsigned long flags = 0;
		real_point3d position;
		real_vector3d velocity;
		int stop;

		if (store_motion_cur < 0)
			store_motion_stat[5]++;
		else if (intent)
			store_motion_stat[4]++;
		else
			store_motion_stat[TEST_FLAG(biped->object.damage_flags, _object_dead_bit) ? 3 : 2]++;
		stop = store_intent_body(biped_index, biped, definition, motion, &flags, &position, &velocity);
		if (stop == 0)
			store_motion_stat[6]++;
		store_motion_from(biped_index, biped, definition, motion, stop, flags);
	}
	store_motion_owner_put(outer);
	return TRUE;
}
