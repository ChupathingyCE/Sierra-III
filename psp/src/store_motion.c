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

64 entries, old-only's pool: clause 10's branch (i), ruled 00:2x (2) and picked by the pool count (the lab's row
t5pool_never_over: no tick over it on the six maps, both seeds). A 65th intent gets no entry and writes nothing
past the table: store_intent_open counts it (over the pool) and returns -1, and the proof reads the count at 0. If
one came, M would run that biped's whole update at its turn (store_motion_update, the dead biped's path), its move
gathering live: the on form has no margin gather (store_coll.c).

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
#include "ai/ai_debug.h" /* M's helpers: ai_debug_describe_actor (bipeds.c 281) */
#include "ai/ai.h" /* M's helpers: ai_handle_bump (bipeds.c 282) */
#include "ai/actors.h"
#include "cseries/errors.h" /* M's physics tail: error, _error_silent (bipeds.c 284) */
#include "cutscene/cinematics.h"
#include "cutscene/recorded_animations.h" /* M's helpers: recorded_animation_controlling_unit (bipeds.c 287) */
#include "devices/device_definitions.h" /* machine_definition_get (bipeds.c 288) */
#include "devices/devices.h" /* _machine_never_appears_locked_bit (bipeds.c 289) */
#include "effects/material_effects.h" /* M's helpers: material_effect_visible, material_effect_new_from_point (bipeds.c 290) */
#include "game/cheats.h" /* M's helpers: cheat (bipeds.c 291) */
#include "game/game_engine.h" /* M's helpers: game_engine_running (bipeds.c 293) */
#include "game/game_globals.h"
#include "game/player_rumble.h" /* M's helpers: rumble_player_impulse, struct rumble_definition (bipeds.c 295) */
#include "game/players.h"
#include "interface/first_person_weapons.h" /* M's tail: first_person_weapon_message_from_unit (bipeds.c 297) */
#include "items/weapons.h" /* M's tail: weapon_prevents_melee_attack, weapon_stop_reload, weapon_get_first_person_animation_time and the melee's enums (bipeds.c 298) */
#include "models/model_animation_definitions.h"
#include "models/model_definitions.h"
#include "objects/damage.h" /* M's helpers: damage_data_new, object_cause_damage, struct damage_data (bipeds.c 301) */
#include "physics/collision_bsp.h" /* M's physics tail: collision_surface_edge_ring_continues, collision_bsp_valid_surface_index (bipeds.c 308) */
#include "physics/collision_bsp_definitions.h" /* PSP_DESIGNATOR (bipeds.c 239) */
#include "physics/collision_debug.h" /* collision_debug_repeat and the rest (bipeds.c 310) */
#include "physics/collision_features.h" /* the limp body: struct collision_feature_list, collision_get_features_in_sphere, collision_test_sphere, collision_move_point (bipeds.c 311) */
#include "physics/collision_models.h" /* _moving's tail: collision_model_instance_new, collision_model_test_vector (bipeds.c 312) */
#include "physics/collision_usage.h" /* COLLISION_USER_PUSH and POP, COLLISION_USER_DEPTH, MAXIMUM_COLLISION_USER_STACK_DEPTH (bipeds.c 314) */
#include "scenario/scenario.h"
#include "network_coop.h" /* port: port/linux/game/network_coop.c */
#include "mcc_script_effects.h" /* M's helpers: mcc_script_effect_enabled, mcc_script_effects_bump, MCC_EFFECT_JETPACK (bipeds.c 321) */
#include "mcc_movement.h" /* port: MCC weapon-authored movement penalties */
#include "store_motion.h"
#include "store_coll.h" /* M's physics tail: store_coll_move, store_coll_stand (piece 3's, store_coll.c) */

#ifdef HALO_PSP
/* bipeds.c's verify gate (psp_fixups.py's FIXES_DIET1, VERIFYGATE: bipeds.c 2027-2035 in the ship form), so I's and
   M's calls ask the check only when bipeds.c's own calls would (psp_verify_call: psp_platform.c, from the keep words).
   Without it every call here ran the function, which then tests "verifyvectors" and returns (the lab's row
   t5s3_bipeds_private) */
extern int psp_verify_call;
#define biped_verify_object_vectors(biped_index, state) \
	((void)(psp_verify_call && (biped_verify_object_vectors((biped_index), (state)), 1)))
#endif
#ifdef PSP_S1COUNT
/* s1count's classes (a) and (b) (the lab's briefs/s1_count.md, task s1c-a; docs/ktstamp.md's s1count), log only:
   the marks only read the game's state and write their own table (end of file). The #line below keeps every later
   line's number, so the off form's lines and the profiles' line checksums are s1c-hdr's */
void psp_s1a_open(long biped_index, struct biped_datum *biped);
void psp_s1a_resume(long biped_index, struct biped_datum *biped);
void psp_s1a_close(int stage);
void psp_s1b_open(struct biped_datum *biped, real_point3d const *target);
void psp_s1b_close(struct biped_datum *biped);
#define PSP_S1A_OPEN(index, biped) (psp_kt_count && !psp_work_on_me() ? psp_s1a_open((index), (biped)) : (void)0)
#define PSP_S1A_RESUME(index, biped) (psp_s1_on && !psp_work_on_me() ? psp_s1a_resume((index), (biped)) : (void)0)
#define PSP_S1A_CLOSE(stage) (psp_s1_on && !psp_work_on_me() ? psp_s1a_close(stage) : (void)0)
#define PSP_S1B_OPEN(biped, target) (psp_s1_on && !psp_work_on_me() ? psp_s1b_open((biped), (target)) : (void)0)
#define PSP_S1B_CLOSE(biped) (psp_s1_on && !psp_work_on_me() ? psp_s1b_close(biped) : (void)0)
#else
#define PSP_S1A_OPEN(index, biped) ((void)0)
#define PSP_S1A_RESUME(index, biped) ((void)0)
#define PSP_S1A_CLOSE(stage) ((void)0)
#define PSP_S1B_OPEN(biped, target) ((void)0)
#define PSP_S1B_CLOSE(biped) ((void)0)
#endif
#line 89

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
extern boolean debug_biped_limp_body_disable;            /* bipeds.c 509: M's dead tests it, as biped_update_dead did */
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

/* I's physics to the move's call: biped_update_physics (bipeds.c 2896-3188 in the ship form at ff2bb9fb) from its
   locals to bipedstage3's stop just before the move, the same tests and the same float expressions in the same order,
   over M's entry and the call's three outputs. In place of the stop (3179-3188) it writes what bipedstage3's record
   took: M's entry's in-flags and facing, and the call's flags, position and velocity. Left out: the resume branch
   (2913-2922: the resume is M's), and the two flag blocks the PSP build never compiles (2988-2990 and 3007-3033:
   psp_build.py's CHUPA_FLAGS_DROPPED, and bipeds.c isn't in ONLINE_GAMES_UNITS). clipped_position, clipped_velocity,
   collisions and collision_count are dropped: they're the call's, past the stop. The engine's locals position and
   velocity are call_position and call_velocity here (the parameters have their names). store_intent_moving calls it;
   nothing calls it yet */
static void store_intent_physics(struct biped_physics *physics, struct store_motion *motion, unsigned long *flags,
	real_point3d *position, real_vector3d *velocity)
{
	real_vector2d facing = { 0.f, 0.f };
	boolean climbing = TEST_FLAG(physics->in_flags, _biped_physics_in_climb_anything_bit);
	word in_flags;
	real_point3d call_position;
	real_vector3d call_velocity;
	unsigned long collision_flags;

	match_assert_valid_real_point3d("c:\\halo\\SOURCE\\units\\bipeds.c", 0xBCC, &physics->position);
	match_assert_valid_real_vector3d("c:\\halo\\SOURCE\\units\\bipeds.c", 0xBCD, &physics->velocity);

	in_flags = physics->in_flags;
	physics->out_flags = 0;
	if (TEST_FLAG(in_flags, _biped_physics_in_flying_bit))
	{
		real_vector3d up;
		real_vector3d left;
		real_vector3d desired_velocity;
		real_vector3d acceleration;
		real_vector3d direction;
		real_vector3d velocity_change;

		biped_build_flying_axes(&physics->forward, &left, &up);
		desired_velocity.i = physics->forward.i * physics->movement_desired.i +
			left.i * physics->movement_desired.j +
			up.i * physics->movement_desired.k;
		desired_velocity.j = physics->forward.j * physics->movement_desired.i +
			left.j * physics->movement_desired.j +
			up.j * physics->movement_desired.k;
		desired_velocity.k = physics->forward.k * physics->movement_desired.i +
			left.k * physics->movement_desired.j +
			up.k * physics->movement_desired.k;
		scale_vector3d(&desired_velocity, 1.f - physics->movement_penalty, &desired_velocity);
		acceleration.i = desired_velocity.i - physics->velocity.i;
		acceleration.j = desired_velocity.j - physics->velocity.j;
		acceleration.k = desired_velocity.k - physics->velocity.k;
		direction = acceleration;
		if (normalize3d(&direction) > physics->acceleration_maximum)
			scale_vector3d(&direction, physics->acceleration_maximum, &velocity_change);
		else
			velocity_change = acceleration;
		add_vectors3d(&velocity_change, &physics->velocity, &physics->new_velocity);
		SET_FLAG(physics->out_flags, _biped_physics_out_airborne_bit, TRUE);
		SET_FLAG(physics->out_flags, _biped_physics_out_slipping_bit, FALSE);
	}
	else if (TEST_FLAG(in_flags, _biped_physics_in_absolute_movement_bit))
	{
		physics->new_velocity.k = physics->movement_desired.k;
		facing.i = physics->new_velocity.i =
			physics->movement_desired.i * physics->forward.i -
			physics->forward.j * physics->movement_desired.j;
		facing.j = physics->new_velocity.j =
			physics->movement_desired.j * physics->forward.i +
			physics->movement_desired.i * physics->forward.j;
	}
	else if (TEST_FLAG(in_flags, _biped_physics_in_airborne_bit))
	{
		real_vector2d desired_velocity;
		real_vector2d acceleration;
		real_vector2d direction;
		real_vector2d velocity_change;

		desired_velocity.i = physics->movement_desired.i * physics->forward.i -
			physics->forward.j * physics->movement_desired.j;
		desired_velocity.j = physics->movement_desired.j * physics->forward.i +
			physics->movement_desired.i * physics->forward.j;
		scale_vector2d(&desired_velocity, 1.f - physics->movement_penalty, &desired_velocity);
		acceleration.i = desired_velocity.i - physics->velocity.i;
		acceleration.j = desired_velocity.j - physics->velocity.j;
		direction = acceleration;
		if (normalize2d(&direction) > physics->airborne_acceleration_maximum)
			scale_vector2d(&direction, physics->airborne_acceleration_maximum, &velocity_change);
		else
			velocity_change = acceleration;
		physics->new_velocity.i = velocity_change.i + physics->velocity.i;
		physics->new_velocity.j = velocity_change.j + physics->velocity.j;
		/* the game browser's gravity variant isn't in the PSP build (psp_build.py drops the flag) */
		physics->new_velocity.k = physics->velocity.k - global_gravity;
		physics->out_flags = TEST_FLAG(in_flags, _biped_physics_in_slipping_bit) ?
			FLAG(_biped_physics_out_slipping_bit) : 0;
	}
	else
	{
		real speed = magnitude3d(&physics->movement_desired);
		real_vector3d const *ground_normal = &physics->ground_plane.n;
		real_vector3d move_direction;

		if (TEST_FLAG(in_flags, _biped_physics_in_climb_anything_bit))
		{
			real_vector3d forward = physics->aiming;
			real_vector3d left;

			/* Stubbs's hand-on-a-wall frame isn't in the PSP build (psp_build.py drops the flag) */
			cross_product3d(ground_normal, &forward, &left);
			if (normalize3d(&left) == 0.f)
			{
				cross_product3d(ground_normal, global_up3d, &left);
				if (normalize3d(&left) == 0.f)
				{
					cross_product3d(ground_normal, global_forward3d, &left);
					normalize3d(&left);
				}
			}
			cross_product3d(&left, ground_normal, &forward);
			normalize3d(&forward);

			move_direction.i = forward.i * physics->movement_desired.i +
				left.i * physics->movement_desired.j;
			move_direction.j = forward.j * physics->movement_desired.i +
				left.j * physics->movement_desired.j;
			move_direction.k = forward.k * physics->movement_desired.i +
				left.k * physics->movement_desired.j +
				physics->movement_desired.k;
			normalize3d(&move_direction);
		}
		else if (ground_normal->k > _real_epsilon)
		{
			move_direction.i = facing.i =
				physics->movement_desired.i * physics->forward.i -
				physics->forward.j * physics->movement_desired.j;
			move_direction.j = facing.j =
				physics->movement_desired.j * physics->forward.i +
				physics->movement_desired.i * physics->forward.j;
			move_direction.k = physics->movement_desired.k -
				(move_direction.j * ground_normal->j + move_direction.i * ground_normal->i) /
				ground_normal->k;
			normalize3d(&move_direction);
		}
		else
		{
			real_vector3d forward = physics->aiming;
			real_vector3d left;

			cross_product3d(global_up3d, &physics->aiming, &left);
			normalize3d(&left);
			point_from_line3d(
				(real_point3d *)&forward,
				ground_normal,
				-dot_product3d(&forward, ground_normal),
				(real_point3d *)&forward);
			point_from_line3d(
				(real_point3d *)&left,
				ground_normal,
				-dot_product3d(&left, ground_normal),
				(real_point3d *)&left);
			facing.i = physics->movement_desired.i * physics->forward.i -
				physics->forward.j * physics->movement_desired.j;
			facing.j = physics->movement_desired.j * physics->forward.i +
				physics->movement_desired.i * physics->forward.j;
			move_direction.i = forward.i * physics->movement_desired.i +
				left.i * physics->movement_desired.j;
			move_direction.j = forward.j * physics->movement_desired.i +
				left.j * physics->movement_desired.j;
			move_direction.k = forward.k * physics->movement_desired.i +
				left.k * physics->movement_desired.j +
				physics->movement_desired.k;
			if (!climbing)
				move_direction.k *= 5.f;
			normalize3d(&move_direction);
		}

		if (!TEST_FLAG(in_flags, _biped_physics_in_climb_anything_bit))
		{
			if (move_direction.k <= physics->downhill_k1)
			{
				speed *= physics->downhill_velocity_scale;
			}
			else if (move_direction.k < physics->downhill_k0)
			{
				speed *= (move_direction.k - physics->downhill_k0) *
					(physics->downhill_velocity_scale - 1.f) /
					(physics->downhill_k1 - physics->downhill_k0) + 1.f;
			}
			else if (move_direction.k >= physics->uphill_k1)
			{
				speed *= physics->uphill_velocity_scale;
			}
			else if (move_direction.k > physics->uphill_k0)
			{
				speed *= (move_direction.k - physics->uphill_k0) *
					(physics->uphill_velocity_scale - 1.f) /
					(physics->uphill_k1 - physics->uphill_k0) + 1.f;
			}
		}

		{
			real_vector3d desired_velocity;
			real_vector3d acceleration;
			real_vector3d direction;
			real_vector3d velocity_change;
			boolean slipping = FALSE;

			scale_vector3d(&move_direction, speed * (1.f - physics->movement_penalty), &desired_velocity);
			acceleration.i = desired_velocity.i - physics->velocity.i;
			acceleration.j = desired_velocity.j - physics->velocity.j;
			acceleration.k = desired_velocity.k - physics->velocity.k;
			direction = acceleration;
			if (normalize3d(&direction) > physics->acceleration_maximum)
			{
				if (!TEST_FLAG(in_flags, _biped_physics_in_climb_anything_bit))
					slipping = TEST_FLAG(in_flags, _biped_physics_in_slipping_bit);
				scale_vector3d(&direction, physics->acceleration_maximum, &velocity_change);
			}
			else
			{
				velocity_change = acceleration;
			}

			velocity_change.i -= ground_normal->i * (1.f / 128.f);
			velocity_change.j -= ground_normal->j * (1.f / 128.f);
			velocity_change.k -= ground_normal->k * (1.f / 128.f);
			physics->out_flags = slipping ? FLAG(_biped_physics_out_slipping_bit) : 0;
			add_vectors3d(&velocity_change, &physics->velocity, &physics->new_velocity);
			if (TEST_FLAG(physics->out_flags, _biped_physics_out_slipping_bit))
				physics->new_velocity.k -= global_gravity;
		}
	}

	if (TEST_FLAG(physics->in_flags, _biped_physics_in_no_collision_bit))
	{
		collision_flags = 0;
	}
	else if (TEST_FLAG(physics->in_flags, _biped_physics_in_dead_bit))
	{
		collision_flags = _collision_test_for_bipeds_dead_flags;
	}
	else
	{
		collision_flags = TEST_FLAG(physics->in_flags, _biped_physics_in_pass_through_bipeds_bit) ?
			_collision_test_for_bipeds_passthrough_living_flags :
			_collision_test_for_bipeds_living_flags;
		if (TEST_FLAG(physics->in_flags, _biped_physics_in_pass_through_players_bit))
			collision_flags |= FLAG(_collision_test_skip_player_bipeds_bit);
	}

	call_position = physics->position;
	call_velocity = physics->new_velocity;
	call_velocity.k += physics->crouch_velocity;
	motion->in_flags = in_flags;
	motion->facing = facing;
	*flags = collision_flags;
	*position = call_position;
	*velocity = call_velocity;
}

/* I's move build to the call: biped_update_moving (bipeds.c 4171-4664 in the ship form at ff2bb9fb) from its top to
   its stop just before the move, over the biped and definition the driver holds, then store_intent_physics above, which
   is the move's call: biped_update_physics up to bipedstage3's stop 1, the same tests and the same float expressions in
   the same order. It fills M's entry (the physics record, the in-flags, the facing) and the call's flags, position and
   velocity, and returns 1 at the call; 0 where the build stops short, which is _moving's dead biped at rest alone
   (4198-4203), never one of I's living bipeds. Left out: the held pair's lookups (4187, 4205) and PSP_RECON's check
   (4188-4190): the driver holds both, so there's no lookup to check against; the bipedstage3 resume branch (4191-4196,
   M's), and the hoist's locals (4177-4180): the records are looked up once each here, the record's address and no more,
   and nothing in the range moves one. The physics record the build fills is M's entry's (motion->physics), and
   store_intent_physics writes the in-flags and the facing beside it in M's table. store_intent_body calls it */
static int store_intent_moving(long biped_index, struct biped_datum *biped, struct biped_definition *definition,
	struct unit_animation_update_data *animation, struct store_motion *motion, unsigned long *flags,
	real_point3d *position, real_vector3d *velocity)
{
	real movement_scale;
	word in_flags;
	struct biped_physics *physics = &motion->physics;
	struct tick2_unit_ctl *control = tick2_unit_ctl_of(biped->object.root);
	struct tick2_unit_bip1 *line1 = tick2_unit_bip1_of(biped->object.root);
	struct tick2_unit_bip2 *line2 = tick2_unit_bip2_of(biped->object.root);
	struct unit_animation *anim = tick2_unit_anim_of(biped->object.root);
	struct object_animation_state *object_anim = tick2_anim_of(biped->object.root);

	if (TEST_FLAG(biped->object.flags, _object_at_rest_bit) &&
		TEST_FLAG(biped->object.damage_flags, _object_dead_bit) &&
		TEST_FLAG(anim->flags, _unit_animation_ignore_translation_bit))
	{
		return 0;
	}

	physics->biped_index = biped_index;
	physics->in_flags = 0;
	physics->forward = biped->object.root->forward;
	if (TEST_FLAG(definition->unit.flags, _unit_simple_creature_bit))
		physics->aiming = biped->object.root->forward;
	else
		unit_get_aiming_vector(biped_index, &physics->aiming);

	physics->velocity = biped->object.root->translational_velocity;
	physics->crouch_velocity = 0.f;
	physics->acceleration_maximum = 0.0053333333f;
	physics->airborne_acceleration_maximum = 0.f;
	biped_get_physics_pill_with(biped_index, biped, definition, &physics->position, &physics->height, &physics->width);
	physics->minimum_normal_k = definition->biped.runtime_minimum_normal_k;
	physics->downhill_k0 = definition->biped.runtime_downhill_k0;
	physics->downhill_k1 = definition->biped.runtime_downhill_k1;
	physics->downhill_velocity_scale = definition->biped.downhill_velocity_scale;
	physics->uphill_k0 = definition->biped.runtime_uphill_k0;
	physics->uphill_k1 = definition->biped.runtime_uphill_k1;
	physics->uphill_velocity_scale = definition->biped.uphill_velocity_scale;
	physics->ground_plane = line2->ground_plane;
	physics->ground_tangential_velocity_max = REAL_MAX;
	physics->ground_tangential_angle = 0.f;
	physics->existing_support_surface_index = line2->support_surface_index;

	if (line2->landing == 1)
	{
		physics->movement_desired.i = 0.f;
		physics->movement_desired.j = 0.f;
		physics->movement_desired.k = 0.f;
		physics->movement_penalty = 1.f;
	}
	else
	{
		movement_scale = 1.f;
		if (TEST_FLAG(definition->biped.flags, _biped_random_speed_increase_bit) &&
			!control->aiming_speed)
		{
			unsigned long stagger_ordinal = (unsigned long)biped_index % 137;
			real difficulty = game_difficulty_get_value(
				_game_difficulty_value_infection_forms);

			movement_scale =
				difficulty * ((real)stagger_ordinal * 0.00729927f) + 1.f;
		}

		if (!TEST_FLAG(definition->biped.flags, _biped_flying_bit) ||
			TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
		{
			real throttle_x = control->throttle.i;
			real throttle_y = control->throttle.j;

			if (throttle_x != 0.f || throttle_y != 0.f ||
				control->throttle.k != 0.f)
			{
				boolean stunned = biped->unit.body_stun > 0.2f;
				char movement_base;

				if (definition->unit.stunned_movement_threshold > 0.f &&
					biped->object.recent_body_damage >
						definition->unit.stunned_movement_threshold)
				{
					stunned = TRUE;
				}

				movement_base = stunned ? _unit_state_stunned_move_front :
					_unit_state_move_front;
				if (fabs(throttle_x) >= fabs(throttle_y))
				{
					animation->state_desired = throttle_x >= 0.f ?
						movement_base : movement_base + 1;
				}
				else
				{
					animation->state_desired = throttle_y >= 0.f ?
						movement_base + 2 : movement_base + 3;
				}
				/* port: Stubbs mode's sprint (port/linux/game/stubbs_player.c) */
				if (stubbs_mode_active())
					stubbs_player_sprint(biped_index, &animation->state_desired);
			}
		}
		else
		{
			animation->state_desired = _unit_state_idle;
		}

		physics->movement_desired = *global_zero_vector3d;
		if (object_anim->state.index != NONE &&
			(TEST_FLAG(biped->object.damage_flags, _object_dead_bit) ||
			!TEST_FLAG(definition->biped.flags, _biped_flying_bit)) &&
			!TEST_FLAG(
				anim->flags,
				_unit_animation_ignore_translation_bit))
		{
			struct animation_graph *animation_graph;
			struct animation *animation_definition;
			real dy = 0.f;
			real dyaw = 0.f;

			model_definition_get(definition->object.model.index);
			animation_graph = animation_graph_definition_get(
				object_anim->animation_graph_index);
			animation_definition = TAG_BLOCK_GET_ELEMENT(
				&animation_graph->animations,
				object_anim->state.index,
				struct animation);

			switch (animation_definition->frame_info_type)
			{
			case _animation_frame_info_xy_translation:
				{
					struct animation_frame_info_dx_dy const *frame_info =
						(struct animation_frame_info_dx_dy const *)animation_get_frame_info(
							animation_definition,
							object_anim->state.frame_index,
							sizeof(struct animation_frame_info_dx_dy));

					physics->movement_desired.i = frame_info->dx;
					dy = frame_info->dy;
				}
				break;

			case _animation_frame_info_xy_translation_yaw_rotation:
				{
					struct animation_frame_info_dx_dy_dyaw const *frame_info =
						(struct animation_frame_info_dx_dy_dyaw const *)animation_get_frame_info(
							animation_definition,
							object_anim->state.frame_index,
							sizeof(struct animation_frame_info_dx_dy_dyaw));

					physics->movement_desired.i = frame_info->dx;
					dy = frame_info->dy;
					physics->movement_desired.j = frame_info->dy;
					dyaw = frame_info->dyaw;
				}
				break;

			case _animation_frame_info_xyz_translation_yaw_rotation:
				{
					struct animation_frame_info_dx_dy_dz_dyaw const *frame_info =
						(struct animation_frame_info_dx_dy_dz_dyaw const *)animation_get_frame_info(
							animation_definition,
							object_anim->state.frame_index,
							sizeof(struct animation_frame_info_dx_dy_dz_dyaw));

					physics->movement_desired.i = frame_info->dx;
					dy = frame_info->dy;
					physics->movement_desired.j = frame_info->dy;
					physics->movement_desired.k = frame_info->dz;
					dyaw = frame_info->dyaw;
				}
				break;
			}

			physics->movement_desired.i *= movement_scale;
			physics->movement_desired.j = dy * movement_scale;
			physics->movement_desired.k *= movement_scale;

			if (!(fabs(dyaw) < _real_epsilon))
			{
				real_vector3d rotated_forward;
				real sine_value = (real)sin(dyaw);
				real cosine_value = (real)cos(dyaw);

				rotated_forward = biped->object.root->forward;
				rotate_vector_about_axis(
					&rotated_forward,
					&biped->object.root->up,
					sine_value,
					cosine_value);
				if (TEST_FLAG(
					control->control_flags,
					_unit_control_exact_facing_bit) &&
					(anim->state == _unit_state_turn_left ||
					anim->state == _unit_state_turn_right) &&
					dot_product3d(
						&biped->object.root->forward,
						&control->desired_facing_vector) > 0.5f)
				{
					real_vector3d old_cross;
					real_vector3d new_cross;

					cross_product3d(
						&control->desired_facing_vector,
						&biped->object.root->forward,
						&old_cross);
					cross_product3d(
						&control->desired_facing_vector,
						&rotated_forward,
						&new_cross);
					if (dot_product3d(&old_cross, &biped->object.root->up) *
						dot_product3d(&new_cross, &biped->object.root->up) <= 0.f)
					{
						rotated_forward = control->desired_facing_vector;
						unit_abort_animation(biped_index);
					}
				}

				biped->object.root->forward = rotated_forward;
				biped_snap_facing_with(biped_index, biped, definition);
			}
		}

		if (!TEST_FLAG(definition->biped.flags, _biped_flying_bit) ||
			TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
		{
			if (!TEST_FLAG(definition->biped.flags, _biped_uses_player_physics_bit) ||
				anim->state == _unit_state_user_animation)
			{
				if (!TEST_FLAG(biped->biped.flags, _biped_slipping_bit) &&
					!TEST_FLAG(biped->biped.flags, _biped_airborne_bit))
				{
					biped->object.root->translational_velocity.i =
						physics->movement_desired.i;
					biped->object.root->translational_velocity.j =
						physics->movement_desired.j;
					physics->acceleration_maximum = REAL_MAX;
				}
			}
			else
			{
				struct game_globals_player_information *player_information =
					TAG_BLOCK_GET_ELEMENT(
						&scenario_get_game_globals()->player_information,
						0,
						struct game_globals_player_information);
				struct game_globals_player_information player_information_block;
				real body_stun_scale = 1.f;
				real crouch;
				real uncrouch;
				real stun_scale;
				real forward_speed;
				real sideways_speed;
				real acceleration;

				if (cinematic_in_progress() ||
					TEST_FLAG(
						definition->biped.flags,
						_biped_uses_old_player_physics_bit))
				{
					player_information_block = *player_information;
					player_information = &player_information_block;
					player_information_block.walking_speed = 0.51200002f;
					player_information_block.run_forward_speed = 2.25f;
					player_information_block.run_backward_speed = 2.f;
					player_information_block.run_sideways_speed = 2.f;
					player_information_block.run_acceleration = 0.31999999f;
				}

				if (biped->unit.player_index != NONE)
				{
					body_stun_scale =
						player_get(biped->unit.player_index)->speed_multiplier;
				}

				crouch = line1->crouch;
				uncrouch = 1.f - crouch;
				stun_scale =
					-(player_information->stun_movement_penalty *
						biped->unit.body_stun - 1.f) *
					body_stun_scale * movement_scale;
				if (control->throttle.i <= 0.f)
				{
					forward_speed = player_information->sneak_backward_speed;
					sideways_speed = player_information->run_backward_speed;
				}
				else
				{
					forward_speed = player_information->sneak_forward_speed;
					sideways_speed = player_information->run_forward_speed;
				}

				forward_speed = sideways_speed * uncrouch + forward_speed * crouch;
				sideways_speed = player_information->run_sideways_speed * uncrouch +
					player_information->sneak_sideways_speed * crouch;
				acceleration = player_information->run_acceleration * uncrouch +
					player_information->sneak_acceleration * crouch;
				if (anim->base_seat_index == _unit_base_seat_alert)
				{
					forward_speed = control->throttle.i <= 0.f ?
						0.f : player_information->walking_speed;
					sideways_speed = 0.f;
				}

				physics->acceleration_maximum = acceleration / TICKS_PER_SECOND;
				physics->movement_desired.i =
					control->throttle.i * stun_scale * forward_speed /
					TICKS_PER_SECOND;
				physics->movement_desired.j =
					control->throttle.j * stun_scale * sideways_speed /
					TICKS_PER_SECOND;
				physics->movement_desired.k = 0.f;
				/* port: MCC-only input scaling; legacy player physics is unchanged. */
				mcc_movement_apply(biped_index, &physics->movement_desired);
				physics->airborne_acceleration_maximum =
					player_information->airborne_acceleration / TICKS_PER_SECOND;
				if (!TEST_FLAG(
					control->control_flags,
					_unit_control_look_dont_turn_bit))
				{
					physics->forward = control->desired_facing_vector;
				}

				if (game_players_are_double_speed())
				{
					scale_vector3d(
						&physics->movement_desired,
						player_information->double_speed_multiplier,
						&physics->movement_desired);
				}
			}
		}
		else
		{
			real deceleration_scale =
				1.f - MIN(magnitude3d(&control->throttle), 1.f);
			real crouch_modifier = 1.f;
			real acceleration;

			if (definition->biped.flying_crouch_velocity_modifier > 0.f)
			{
				if (line1->crouch == 1.f)
				{
					crouch_modifier =
						definition->biped.flying_crouch_velocity_modifier;
				}
				else if (line1->crouch > 0.f)
				{
					crouch_modifier =
						(definition->biped.flying_crouch_velocity_modifier - 1.f) *
						line1->crouch + 1.f;
				}
			}

			physics->movement_desired.i =
				definition->biped.flying_velocity * crouch_modifier * movement_scale;
			physics->movement_desired.j =
				definition->biped.flying_sidestep_velocity * crouch_modifier *
				movement_scale;
			physics->movement_desired.k =
				definition->biped.flying_sidestep_velocity * crouch_modifier *
				movement_scale;
			acceleration = (1.f - deceleration_scale) *
				definition->biped.flying_acceleration +
				definition->biped.flying_deceleration * deceleration_scale;
			physics->movement_desired.i *=
				control->throttle.i / TICKS_PER_SECOND;
			physics->movement_desired.j *=
				control->throttle.j / TICKS_PER_SECOND;
			physics->movement_desired.k *=
				control->throttle.k / TICKS_PER_SECOND;
			physics->acceleration_maximum =
				acceleration * crouch_modifier * movement_scale /
				TICKS_PER_SECOND;
			physics->airborne_acceleration_maximum =
				physics->acceleration_maximum;
		}

		if (TEST_FLAG(biped->biped.flags, _biped_airborne_bit) &&
			line2->airborne_ticks < 22 &&
			biped->unit.actor_index != NONE &&
			actor_is_leaping(biped->unit.actor_index))
		{
			physics->ground_tangential_velocity_max = 0.1f;
			physics->ground_tangential_angle = 0.5f;
		}

		physics->movement_penalty = 0.f;
	}

	{
		real crouch_delta;
		real crouch_transition_velocity =
			definition->biped.runtime_crouch_transition_velocity;

		if (anim->base_seat_index == _unit_base_seat_crouch)
		{
			crouch_delta = 1.f - line1->crouch;
			if (crouch_delta <= crouch_transition_velocity)
			{
				line1->crouch = 1.f;
			}
			else
			{
				crouch_delta = crouch_transition_velocity;
				line1->crouch += crouch_transition_velocity;
			}
		}
		else
		{
			crouch_delta = -line1->crouch;
			if (crouch_delta >= -crouch_transition_velocity)
			{
				line1->crouch = 0.f;
			}
			else
			{
				crouch_delta = -crouch_transition_velocity;
				line1->crouch -= crouch_transition_velocity;
			}
		}

		if (fabs(crouch_delta) > 0.01f &&
			TEST_FLAG(biped->biped.flags, _biped_airborne_bit))
		{
			physics->crouch_velocity =
				(definition->biped.collision_height_standing -
					definition->biped.collision_height_crouching) * crouch_delta;
		}
	}

	in_flags = physics->in_flags;
	if (line1->crouch != 0.f)
	{
		SET_FLAG(in_flags, _biped_physics_in_crouched_bit, TRUE);
		if (!animation->crouching)
			SET_FLAG(in_flags, _biped_physics_in_trying_to_stand_bit, TRUE);
	}
	if (TEST_FLAG(biped->biped.flags, _biped_airborne_bit))
		SET_FLAG(in_flags, _biped_physics_in_airborne_bit, TRUE);
	if (TEST_FLAG(biped->biped.flags, _biped_slipping_bit))
		SET_FLAG(in_flags, _biped_physics_in_slipping_bit, TRUE);
	if (TEST_FLAG(biped->biped.flags, _biped_absolute_movement_bit))
		SET_FLAG(in_flags, _biped_physics_in_absolute_movement_bit, TRUE);
	if (TEST_FLAG(biped->biped.flags, _biped_no_collision_bit))
		SET_FLAG(in_flags, _biped_physics_in_no_collision_bit, TRUE);
	if (TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
		SET_FLAG(in_flags, _biped_physics_in_dead_bit, TRUE);
	if (TEST_FLAG(definition->biped.flags, _biped_flying_bit) &&
		!TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
	{
		SET_FLAG(in_flags, _biped_physics_in_flying_bit, TRUE);
	}
	if (TEST_FLAG(definition->biped.flags, _biped_passes_through_bipeds_bit))
		SET_FLAG(in_flags, _biped_physics_in_pass_through_bipeds_bit, TRUE);
	if (biped->unit.player_index != NONE && !network_coop_player_collisions())
		SET_FLAG(in_flags, _biped_physics_in_pass_through_players_bit, TRUE);
	if (TEST_FLAG(definition->biped.flags, _biped_climbs_anything_bit) &&
		!TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
	{
		SET_FLAG(in_flags, _biped_physics_in_climb_anything_bit, TRUE);
	}
	physics->in_flags = in_flags;
	store_intent_physics(physics, motion, flags, position, velocity);
	return 1;
}

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
	outer = store_motion_owner_set(biped_index); PSP_S1A_OPEN(biped_index, biped);
	stop = store_intent_body(biped_index, biped, biped_definition_get(biped->definition_index), motion, &flags,
		&position, &velocity);
	if (stop)
		store_intent_keep(k, n, stop, flags, &position, &velocity, motion->physics.height, motion->physics.width);
	PSP_S1A_CLOSE(PSP_S1_STAGE_I); store_motion_owner_put(outer);
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

extern boolean debug_biped_physics, debug_biped_skip_collision; /* bipeds.c 506, 508 */
boolean network_objects_may_delete(long object_index);          /* psp_campaign.c 303 (as bipeds.c 374) */
void platform_psp_log(const char *format, ...);                 /* psp_platform.c 82 (as bipeds.c 264) */
unsigned long psp_microseconds(void);                           /* psp_platform.c 2800 (as bipeds.c 265) */
/* PSP_SWEEPREUSE (psp_fixups.py), as bipeds.c 329-331: collisions.c's */
boolean psp_sr_try(long biped_index, unsigned long flags, real_point3d const *position, real_vector3d const *velocity, real height, real width, short *count, real_point3d *clipped_position, real_vector3d *clipped_velocity, struct collision_plane *collisions);
void psp_sr_begin(void);
void psp_sr_store(long biped_index, unsigned long flags, real_point3d const *position, real_vector3d const *velocity, real height, real width, short *count, real_point3d *clipped_position, real_vector3d *clipped_velocity, struct collision_plane *collisions);

/* M's physics tail: biped_update_physics's from the move's call (the psp_bs3_resume_call: label) to its end, on M's
   physics record. The engine's lines 2896-2911 and 3189-3724 in the ship form at ff2bb9fb: the same tests and the same
   float expressions in the same order, so the game computes bit-for-bit the same values. Its inputs: facing M's entry's
   (I left it there), collision_flags the intent's or the whole update's own build's, and position and velocity made
   again from the record by store_motion_from, as 3176-3178 made them; climbing it works out from physics->in_flags at
   its head, as 2900 did. Position and velocity come by value (the tail only reads them: the engine's `position.` and
   `&velocity` stay as they are), and there's no in_flags: the tail never reads the local past the label, only
   physics->in_flags. It returns nothing: the engine's tail writes only physics->... Left out: the census, psp_pill_reach
   (3221-3223: the design's part 5 leaves it out of M); the Stubbs wall block (3490-3513) and its climber_wall (3459),
   which psp_build.py's CHUPA_FLAGS_DROPPED never compiles for the PSP (it drops -DHALO_MAP_LIST) and nothing else
   reads; and FIXES_STACKHOT2's features with psp_stackhot_list and its #undef (3697-3699, 3725), since collnew's stand
   test replaced the engine's two calls and nothing in the function reads them. PSP_BMS(biped, X) is biped->X (3688,
   3690). The pill-sweep timer's counters and their "bipeds: pill sweeps" line are this call's now (3210-3220 and
   3240-3257, kept whole: the move is priced by them). store_motion_from calls it */
static void store_motion_call(struct biped_physics *physics, real_vector2d facing, unsigned long collision_flags,
	real_point3d position, real_vector3d velocity)
{
	boolean climbing = TEST_FLAG(physics->in_flags, _biped_physics_in_climb_anything_bit);
	real_point3d clipped_position;
	real_vector3d clipped_velocity;
	struct collision_plane collisions[16];
	short collision_count;

	match_assert_valid_real_point3d("c:\\halo\\SOURCE\\units\\bipeds.c", 0xBCC, &physics->position);
	match_assert_valid_real_vector3d("c:\\halo\\SOURCE\\units\\bipeds.c", 0xBCD, &physics->velocity);

	if (debug_biped_skip_collision)
	{
		collisions[0].t = 0.f;
		collisions[0].point = physics->position;
		collisions[0].plane.n = *global_up3d;
		collisions[0].plane.d = -position.z;
		collisions[0].object_index = physics->biped_index;
		collisions[0].surface_index = NONE;
		collisions[0].flags = 0;
		collisions[0].breakable_surface_index = 0;
		collisions[0].material_index = NONE;
		clipped_velocity = velocity;
		clipped_position.x = position.x + clipped_velocity.i;
		clipped_position.y = position.y + clipped_velocity.j;
		clipped_position.z = position.z;
		clipped_velocity.k = 0.f;
		collision_count = 1;
	}
	else
	{
#ifdef HALO_PSP
		/* timed, by whether the biped is moving (psp_fixups.py) */
		static unsigned long psp_pill_us[3], psp_pill_count[3], psp_pill_hits[3];
		unsigned long psp_mark = psp_microseconds();
		/* "still": little horizontal movement (gravity pulls every standing
		biped into its ground plane each tick, so the vertical part is never
		zero): 2 under 0.001 wu a tick, 1 under 0.01, 0 moving */
		float psp_speed2 = velocity.i * velocity.i + velocity.j * velocity.j;
		int psp_still = TEST_FLAG(physics->in_flags, _biped_physics_in_airborne_bit) ? 0 :
			psp_speed2 < 1e-6f ? 2 : psp_speed2 < 1e-4f ? 1 : 0;
#endif
		if (!psp_sr_try(physics->biped_index, collision_flags, &position, &velocity, physics->height, physics->width, &collision_count, &clipped_position, &clipped_velocity, collisions)) /* PSP_SWEEPREUSE */
		{
		psp_sr_begin();
		collision_count = store_coll_move( /* TICK2 collnew: the store's move with its take, until task 5 */
			collision_flags,
			&position,
			&velocity,
			physics->height,
			physics->width,
			physics->biped_index,
			&clipped_position,
			&clipped_velocity,
			16,
			collisions);
		psp_sr_store(physics->biped_index, collision_flags, &position, &velocity, physics->height, physics->width, &collision_count, &clipped_position, &clipped_velocity, collisions);
		}
#ifdef HALO_PSP
		psp_pill_us[psp_still] += psp_microseconds() - psp_mark;
		psp_pill_count[psp_still]++;
		psp_pill_hits[psp_still] += collision_count;
		if (psp_pill_count[0] + psp_pill_count[1] + psp_pill_count[2] >= 3000)
		{
			platform_psp_log("bipeds: pill sweeps: moving %lu at %lu us (%.1f collisions each), slow %lu at %lu us (%.1f), still %lu at %lu us (%.1f)",
				psp_pill_count[0], psp_pill_count[0] ? psp_pill_us[0] / psp_pill_count[0] : 0,
				psp_pill_count[0] ? (double)psp_pill_hits[0] / psp_pill_count[0] : 0.0,
				psp_pill_count[1], psp_pill_count[1] ? psp_pill_us[1] / psp_pill_count[1] : 0,
				psp_pill_count[1] ? (double)psp_pill_hits[1] / psp_pill_count[1] : 0.0,
				psp_pill_count[2], psp_pill_count[2] ? psp_pill_us[2] / psp_pill_count[2] : 0,
				psp_pill_count[2] ? (double)psp_pill_hits[2] / psp_pill_count[2] : 0.0);
			memset(psp_pill_us, 0, sizeof(psp_pill_us));
			memset(psp_pill_count, 0, sizeof(psp_pill_count));
			memset(psp_pill_hits, 0, sizeof(psp_pill_hits));
		}
#endif
	}

	if (debug_biped_physics &&
		object_get(physics->biped_index)->object.owner_player_index != NONE)
	{
		collision_debug_repeat = TRUE;
		collision_debug_point = position;
		collision_debug_vector = velocity;
		collision_debug_length = 1.0f;
		collision_debug_width = physics->width;
		collision_debug_height = physics->height;
	}

	SET_FLAG(physics->out_flags, _biped_physics_out_splatter_bit, collision_count >= 16);
	physics->stick_surface_index = NONE;
	if (!collision_count && physics->existing_support_surface_index != NONE)
	{
		struct collision_bsp *bsp = global_collision_bsp_get();
		long best_surface_index = NONE;
		real best_distance_squared = REAL_MAX;

		if (VALID_INDEX(physics->existing_support_surface_index, bsp->surfaces.count))
		{
			struct collision_surface *surface = TAG_BLOCK_GET_ELEMENT(
				&bsp->surfaces,
				physics->existing_support_surface_index,
				struct collision_surface);
			long edge_index = surface->first_edge_index;
			short edge_count = 0;
			real_plane3d plane;
			real_plane3d best_plane;
			real best_velocity_dot;
			real_point3d point;
			real distance;

			bsp3d_get_plane_from_designator(&bsp->bsp3d, PSP_DESIGNATOR(surface->plane_designator), &plane);
			distance = -(plane.n.i * clipped_position.x +
				plane.n.j * clipped_position.y +
				plane.n.k * clipped_position.z -
				plane.d);
			point.x = plane.n.i * distance + clipped_position.x;
			point.y = plane.n.j * distance + clipped_position.y;
			point.z = plane.n.k * distance + clipped_position.z;

			do
			{
				struct collision_edge *edge;
				boolean reverse;
				long neighbor_surface_index;

				/* port: the surface's ring of edges ends as
				collision_surface_edge_ring_continues says, or at an edge
				whose vertices aren't the bsp's (a map's indices; the
				retail rings all close within 3 to 8 edges) */
				if (!collision_surface_edge_ring_continues(bsp, edge_index, edge_count++))
				{
					break;
				}
				edge = TAG_BLOCK_GET_ELEMENT(
					&bsp->edges,
					edge_index,
					struct collision_edge);
				if (!VALID_INDEX(edge->vertex_indices[0], bsp->vertices.count) ||
					!VALID_INDEX(edge->vertex_indices[1], bsp->vertices.count))
				{
					static boolean reported = FALSE;

					if (!reported)
					{
						reported = TRUE;
						error(_error_silent, "collision edge #%ld's vertices are not the bsp's", edge_index);
					}
					break;
				}
				reverse = physics->existing_support_surface_index == edge->surface_indices[1];
				neighbor_surface_index = edge->surface_indices[!reverse];

				/* port: (and a neighbor that is no surface of the bsp's is
				none) */
				if (neighbor_surface_index != NONE &&
					collision_bsp_valid_surface_index(bsp, neighbor_surface_index))
				{
					struct collision_surface *neighbor_surface = TAG_BLOCK_GET_ELEMENT(
						&bsp->surfaces,
						neighbor_surface_index,
						struct collision_surface);

					if (TEST_FLAG(physics->in_flags, _biped_physics_in_climb_anything_bit) ||
						TEST_FLAG(neighbor_surface->flags, _collision_surface_climbable_bit))
					{
						real velocity_dot;

						bsp3d_get_plane_from_designator(
							&bsp->bsp3d,
							PSP_DESIGNATOR(neighbor_surface->plane_designator),
							&plane);
						velocity_dot = dot_product3d(&plane.n, &clipped_velocity);
						if (velocity_dot > 0.f &&
							plane.n.i * clipped_position.x +
								plane.n.j * clipped_position.y +
								plane.n.k * clipped_position.z -
								plane.d > physics->width * -0.5f)
						{
							struct collision_vertex *vertex0 = TAG_BLOCK_GET_ELEMENT(
								&bsp->vertices,
								edge->vertex_indices[0],
								struct collision_vertex);
							struct collision_vertex *vertex1 = TAG_BLOCK_GET_ELEMENT(
								&bsp->vertices,
								edge->vertex_indices[1],
								struct collision_vertex);
							real_vector3d edge_vector;
							real_point3d closest_point;
							real t;
							real distance_squared;

							vector_from_points3d(&vertex0->point, &vertex1->point, &edge_vector);
							t = ((point.x - vertex0->point.x) * edge_vector.i +
								(point.y - vertex0->point.y) * edge_vector.j +
								(point.z - vertex0->point.z) * edge_vector.k) /
								magnitude_squared3d(&edge_vector);
							if (t < 0.f)
								closest_point = vertex0->point;
							else if (t > 1.f)
								closest_point = vertex1->point;
							else
								point_from_line3d(&vertex0->point, &edge_vector, t, &closest_point);

							distance_squared = distance_squared3d(&point, &closest_point);
							if (distance_squared < best_distance_squared)
							{
								best_distance_squared = distance_squared;
								best_surface_index = neighbor_surface_index;
								best_plane = plane;
								best_velocity_dot = velocity_dot;
							}
						}
					}
				}

				edge_index = edge->edge_indices[reverse];
			}
			while (edge_index != surface->first_edge_index);

			if (best_surface_index != NONE &&
				best_distance_squared <= (physics->width * 2.f) * (physics->width * 2.f) &&
				best_velocity_dot <= 1.6f / TICKS_PER_SECOND)
			{
				distance = best_plane.n.i * clipped_position.x +
					best_plane.n.j * clipped_position.y +
					best_plane.n.k * clipped_position.z -
					(best_plane.d + physics->width);
				if (!(fabs(distance) > physics->width * 0.5f))
				{
					real new_velocity_dot = dot_product3d(&best_plane.n, &clipped_velocity);

					clipped_position.x = best_plane.n.i * -distance + clipped_position.x;
					clipped_position.y = best_plane.n.j * -distance + clipped_position.y;
					clipped_position.z = best_plane.n.k * -distance + clipped_position.z;
					if (new_velocity_dot > -1.f / TICKS_PER_SECOND)
					{
						point_from_line3d(
							(real_point3d *)&clipped_velocity,
							&best_plane.n,
							-(new_velocity_dot + 1.f / TICKS_PER_SECOND),
							(real_point3d *)&clipped_velocity);
					}

					collisions[0].t = 0.f;
					collisions[0].point.x = best_plane.n.i * -physics->width + clipped_position.x;
					collisions[0].point.y = best_plane.n.j * -physics->width + clipped_position.y;
					collisions[0].point.z = best_plane.n.k * -physics->width + clipped_position.z;
					collisions[0].plane = best_plane;
					collisions[0].object_index = NONE;
					collisions[0].surface_index = best_surface_index;
					collisions[0].flags = 0;
					collisions[0].breakable_surface_index = 0;
					collisions[0].material_index = NONE;
					collision_count = 1;
					physics->stick_surface_index = best_surface_index;
				}
			}
		}
	}

	{
		real magnitude_squared = facing.j * facing.j + facing.i * facing.i;

		if (magnitude_squared > _real_epsilon * _real_epsilon)
		{
			real inverse_magnitude = 1.0 / square_root(magnitude_squared);

			facing.i *= inverse_magnitude;
			facing.j *= inverse_magnitude;
		}
	}

	{
		short best_collision_index = NONE;
		boolean best_walkable = FALSE;
		boolean best_stuck = FALSE;
		real best_normal_k = -REAL_MAX;
		real best_velocity_dot = -REAL_MAX;

		if (!TEST_FLAG(physics->in_flags, _biped_physics_in_flying_bit))
		{
			short collision_index;

			for (collision_index = 0; collision_index < collision_count; collision_index++)
			{
				struct collision_plane *collision = &collisions[collision_index];
				boolean walkable =
					!TEST_FLAG(physics->in_flags, _biped_physics_in_dead_bit) &&
					(climbing || TEST_FLAG(collision->flags, _collision_surface_climbable_bit));
				boolean stuck =
					physics->stick_surface_index != NONE &&
					best_collision_index != NONE &&
					collisions[best_collision_index].surface_index == physics->stick_surface_index;
				real velocity_dot = -dot_product3d(&collision->plane.n, &physics->new_velocity);
				boolean better;

				if (walkable)
				{
					if (!climbing && collision->plane.n.i * facing.i + collision->plane.n.j * facing.j > 0.5f)
						better = FALSE;
					else if (!best_walkable || stuck)
						better = TRUE;
					else if (best_stuck)
						better = FALSE;
					else
						better = velocity_dot > best_velocity_dot;
				}
				else
				{
					better = !best_walkable && collision->plane.n.k > best_normal_k;
				}

				if (better)
				{
					best_velocity_dot = velocity_dot;
					best_walkable = walkable;
					best_collision_index = collision_index;
					best_stuck = stuck;
					best_normal_k = collision->plane.n.k;
				}

				if (!TEST_FLAG(physics->out_flags, _biped_physics_out_volatile_collision_bit) &&
					(TEST_FLAG(collision->flags, _collision_surface_breakable_bit) ||
					(collision->object_index != NONE &&
					!TEST_FLAG(_object_mask_scenery, object_get_type(collision->object_index)))))
				{
					SET_FLAG(physics->out_flags, _biped_physics_out_volatile_collision_bit, TRUE);
				}
			}
		}

		if (best_collision_index != NONE)
		{
			struct collision_plane *collision = &collisions[best_collision_index];
			real_plane3d plane = collision->plane;
			boolean supported = TRUE;
			real velocity_dot = -dot_product3d(&plane.n, &velocity);

			if (!best_walkable && !best_stuck)
			{
				if (best_normal_k < physics->minimum_normal_k)
				{
					supported = FALSE;
				}
				else if (TEST_FLAG(physics->in_flags, _biped_physics_in_airborne_bit) &&
					physics->ground_tangential_velocity_max < REAL_MAX)
				{
					real_vector3d tangential_velocity;
					real maximum = physics->ground_tangential_velocity_max;

					point_from_line3d(
						(real_point3d *)&velocity,
						&plane.n,
						velocity_dot,
						(real_point3d *)&tangential_velocity);
					if (magnitude_squared3d(&tangential_velocity) > maximum * maximum &&
						velocity_dot / magnitude3d(&velocity) < physics->ground_tangential_angle)
					{
						supported = FALSE;
					}
				}
			}

			if (supported)
			{
				SET_FLAG(physics->out_flags, _biped_physics_out_airborne_bit, FALSE);
				physics->ground_plane = collision->plane;
				physics->support_surface_index = collision->surface_index;
				if (physics->support_surface_index == NONE ||
					physics->support_surface_index != physics->stick_surface_index)
				{
					physics->landing_velocity = -dot_product3d(&velocity, &physics->ground_plane.n);
				}
				else
				{
					physics->landing_velocity = 0.f;
				}
			}
			else
			{
				SET_FLAG(physics->out_flags, _biped_physics_out_airborne_bit, TRUE);
				physics->ground_plane = depths_of_hell;
				physics->support_surface_index = NONE;
				physics->landing_velocity = 0.f;
			}
		}
		else
		{
			SET_FLAG(physics->out_flags, _biped_physics_out_airborne_bit, TRUE);
			physics->ground_plane = depths_of_hell;
			physics->support_surface_index = NONE;
			physics->landing_velocity = 0.f;
		}
	}

	{
		long bumped_object_index = NONE;
		short bumped_object_type;
		real bumped_speed_squared;
		short collision_index;

		for (collision_index = 0; collision_index < collision_count; collision_index++)
		{
			long object_index = collisions[collision_index].object_index;

			if (object_index != NONE)
			{
				struct object_datum *object = object_get(object_index);
				real_vector3d relative_velocity;
				real speed_squared;
				boolean bump;

				relative_velocity.i = object->object.root->translational_velocity.i - clipped_velocity.i;
				relative_velocity.j = object->object.root->translational_velocity.j - clipped_velocity.j;
				relative_velocity.k = object->object.root->translational_velocity.k - clipped_velocity.k;
				speed_squared = magnitude_squared3d(&relative_velocity);
				if (bumped_object_index == NONE)
					bump = TRUE;
				else if (bumped_object_type == _object_type_vehicle)
					bump = object->object.type == _object_type_vehicle && speed_squared > bumped_speed_squared;
				else
					bump = object->object.type == _object_type_vehicle || speed_squared > bumped_speed_squared;

				if (bump)
				{
					bumped_object_type = object->object.type;
					bumped_speed_squared = speed_squared;
					bumped_object_index = object_index;
				}
			}
		}
		physics->bumped_object_index = bumped_object_index;
	}

	physics->elevator_object_index = NONE;
	{
		short collision_index;

		for (collision_index = 0; collision_index < collision_count; collision_index++)
		{
			long object_index = collisions[collision_index].object_index;

			if (object_index != NONE)
			{
				struct object_datum *machine = object_try_and_get_and_verify_type(
					object_index,
					_object_mask_machine);

				if (machine)
				{
					struct machine_definition *machine_definition =
						machine_definition_get(machine->definition_index);

					if (TEST_FLAG(machine_definition->machine.flags, _machine_never_appears_locked_bit) &&
						machine_definition->machine.elevator_node_index != NONE)
					{
						physics->elevator_object_index = object_index;
					}
				}
			}
		}
	}

	physics->new_position = clipped_position;
	physics->new_velocity = clipped_velocity;
	{
		real_vector3d velocity_delta;

		velocity_delta.i = clipped_velocity.i - velocity.i;
		velocity_delta.j = clipped_velocity.j - velocity.j;
		velocity_delta.k = clipped_velocity.k - velocity.k;
		physics->collision_velocity = magnitude3d(&velocity_delta);
	}
	physics->new_velocity.k -= physics->crouch_velocity;

	if (TEST_FLAG(physics->in_flags, _biped_physics_in_crouched_bit) &&
		TEST_FLAG(physics->in_flags, _biped_physics_in_trying_to_stand_bit))
	{
		struct biped_datum *biped = biped_get(physics->biped_index);

		if (biped->unit.player_index != NONE)
		{
			struct biped_definition *definition = biped_definition_get(biped->definition_index);

			if (!TEST_FLAG(definition->biped.flags, _biped_pill_centered_at_origin_bit) &&
				!TEST_FLAG(definition->biped.flags, _biped_spherical_bit))
			{
				real radius = definition->biped.collision_height_standing * 0.5f;
				real_point3d center = physics->new_position;

				center.z += radius;
				if (store_coll_stand( /* TICK2 collnew: the store's stand test, the engine's two calls, until task 5 */
					TEST_FLAG(physics->in_flags, _biped_physics_in_dead_bit) ?
						_collision_test_for_bipeds_dead_flags :
						_collision_test_for_bipeds_living_flags,
					&center,
					radius,
					definition->biped.collision_radius,
					physics->biped_index,
					&physics->new_position,
					definition->biped.collision_height_standing -
						2.f * definition->biped.collision_radius))
				{
					SET_FLAG(physics->out_flags, _biped_physics_out_cannot_stand_bit, TRUE);
				}
			}
		}
	}

	match_assert_valid_real_point3d("c:\\halo\\SOURCE\\units\\bipeds.c", 0xF10, &physics->new_position);
	match_assert_valid_real_vector3d("c:\\halo\\SOURCE\\units\\bipeds.c", 0xF11, &physics->new_velocity);

	return;
}

/* bipeds.c's material effect constants (its constants block at 382-390 in the ship form at ff2bb9fb), which
   store_motion.h's held text doesn't reach: the jump's two footsteps name _material_effect_biped_jump */
enum
{
	_material_effect_biped_walk = 0,
	_material_effect_biped_run,
	_material_effect_biped_sliding,
	_material_effect_biped_shuffle,
	_material_effect_biped_jump,
	_material_effect_biped_jump_land,
};

/* bipeds.c's melee action enum (its constants block at 392-395 in the ship form at ff2bb9fb), which store_motion.h's
   held text doesn't reach: biped_update's tail names _unit_animation_action_melee */
enum
{
	_unit_animation_action_melee = 7,
};

/* biped_make_footstep (bipeds.c 2276-2311 in the ship form at ff2bb9fb), not on tools/tick2_deleted.txt yet, but
   every caller is (biped_jump, biped_try_to_make_footsteps, biped_update_landing, biped_update_slipping). The same
   body; the calls between the six helpers are M's names */
static void store_motion_footstep(
	long biped_index,
	short event_index,
	short contact_point_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);

	match_assert("c:\\halo\\SOURCE\\units\\bipeds.c", 3965, COLLISION_USER_DEPTH < MAXIMUM_COLLISION_USER_STACK_DEPTH);
	COLLISION_USER_PUSH(_collision_user_bipeds);

	if (contact_point_index<definition->biped.contact_points.count &&
		definition->biped.material_effects.index!=NONE &&
		material_effect_visible(&biped->object.root->bounding_sphere_center))
	{
		struct biped_contact_point *contact_point = TAG_BLOCK_GET_ELEMENT(
			&definition->biped.contact_points,
			contact_point_index,
			struct biped_contact_point);
		struct object_marker marker;

		if (object_get_marker_by_name(biped_index, contact_point->marker_name, &marker, 1))
		{
			material_effect_new_from_point(
				definition->biped.material_effects.index,
				event_index,
				&marker.matrix.position,
				0.f);
		}
	}

	match_assert("c:\\halo\\SOURCE\\units\\bipeds.c", 3982, COLLISION_USER_DEPTH > 1);
	COLLISION_USER_POP();

	return;
}
/* biped_jump (bipeds.c 2144-2226 in the ship form at ff2bb9fb), on the delete list: only biped_update's chain called
   it (biped_update 5150, which store_motion_tail carries, and biped_update_jumping). The
   same body; the HALO_GAME_BROWSER block (2175-2179) goes, never defined on the PSP; PSP_BMS(W)(biped, X) is
   biped->X; PSP_BMS_SYNC (2206, 2211) goes, its call bare; and biped_make_footstep (2220, 2221) is
   store_motion_footstep */
static boolean store_motion_jump(
	long biped_index)
{
	{ void psp_ws_dirty(long object_index); psp_ws_dirty(biped_index); } /* (FIXES_BIPEDSTAGE #wsdirty) 1c: a write outside the staged passes */
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);
	boolean jumped = FALSE;

	if (!TEST_FLAG(biped->biped.flags, _biped_airborne_bit) &&
		(*tick2_unit_bip2_of(biped->object.root)).landing!=1)
	{
		long actor_index;
		real_vector3d jump_velocity;
		real jump_magnitude;

		jumped = TRUE;
		jump_magnitude = definition->biped.jump_velocity;
		if (biped->unit.player_index!=NONE)
		{
			struct game_globals_player_information *player_information =
				TAG_BLOCK_GET_ELEMENT(
					&scenario_get_game_globals()->player_information,
					0,
					struct game_globals_player_information);

			jump_magnitude *=
				1.f - biped->unit.body_stun*player_information->stun_turning_penalty;
		}
		/* port: MCC's host-approved jump also applies to client prediction. */
		if ((cheat.super_jump || mcc_script_effect_enabled(MCC_EFFECT_SUPER_JUMP)) && biped->unit.player_index!=NONE)
			jump_magnitude *= 4.f;

		jump_velocity = biped->object.root->translational_velocity;
		{
			real upward_velocity =
				jump_velocity.i*biped->object.root->up.i +
				(jump_velocity.k*biped->object.root->up.k +
				jump_velocity.j*biped->object.root->up.j);

			if (upward_velocity<jump_magnitude)
			{
				real velocity_delta = jump_magnitude-upward_velocity;

				jump_velocity.i += biped->object.root->up.i*velocity_delta;
				jump_velocity.j += biped->object.root->up.j*velocity_delta;
				jump_velocity.k += biped->object.root->up.k*velocity_delta;
			}
		}

		actor_index = biped->unit.swarm_actor_index;
		if (actor_index==NONE)
			actor_index = biped->unit.actor_index;
		if (actor_index!=NONE)
		{
			boolean leap = (*tick2_unit_anim_of(biped->object.root)).state==_unit_state_leap_start ||
				(*tick2_unit_anim_of(biped->object.root)).state==_unit_state_leap_airborne;

			jumped = actor_aim_jump(
				actor_index,
				biped_index,
				leap,
				jump_magnitude,
				&jump_velocity);
		}

		if (jumped)
		{
			biped->object.root->translational_velocity = jump_velocity;
			SET_FLAG(biped->biped.flags, _biped_airborne_bit, TRUE);
			(*tick2_unit_bip2_of(biped->object.root)).jump_recovery_timer = 0;
			(*tick2_unit_bip2_of(biped->object.root)).support_surface_index = NONE;
			store_motion_footstep(biped_index, _material_effect_biped_jump, 0);
			store_motion_footstep(biped_index, _material_effect_biped_jump, 1);
		}
	}

	return jumped;
}
/* biped_bumped_object (bipeds.c 766-820 in the ship form at ff2bb9fb), on the delete list: only biped_update_moving
   called it. The same body */
static void store_motion_bumped(
	long biped_index,
	long object_index,
	real_vector3d const *old_velocity)
{
	struct biped_datum *biped = biped_get(biped_index);
	char bump_ticks;

	biped_definition_get(biped->definition_index);
	bump_ticks = (*tick2_unit_bip2_of(biped->object.root)).bump_ticks;
	if (bump_ticks < 0)
	{
		if (object_index == NONE)
			(*tick2_unit_bip2_of(biped->object.root)).bump_ticks = bump_ticks + 1;
		else
			(*tick2_unit_bip2_of(biped->object.root)).bump_ticks = -15;
	}
	else if (object_index != NONE)
	{
		struct object_datum *object = object_get(object_index);

		ai_handle_bump(biped_index, object_index, old_velocity);
		if (biped->unit.player_index != NONE ||
			recorded_animation_controlling_unit(biped_index))
		{
			if ((*tick2_unit_bip2_of(biped->object.root)).bump_object_index != object_index)
			{
				(*tick2_unit_bip2_of(biped->object.root)).bump_object_index = object_index;
				(*tick2_unit_bip2_of(biped->object.root)).bump_ticks = 0;
			}
			else
			{
				(*tick2_unit_bip2_of(biped->object.root)).bump_ticks++;
				if ((*tick2_unit_bip2_of(biped->object.root)).bump_ticks > 3)
				{
					/* port: MCC handles local/remote ownership without the local-only helper. */
					if (!mcc_script_effects_bump(biped_index, object_index) &&
						object->object.type == _object_type_biped && cheat.bump_possession)
					{
						short local_player_index = unit_get_local_player_index(biped_index);

						if (local_player_index != NONE)
						{
							(*tick2_unit_bip2_of(((struct biped_datum *)object)->object.root)).bump_ticks = -15;
							players_set_local_player_unit(local_player_index, object_index);
						}
					}
					(*tick2_unit_bip2_of(biped->object.root)).bump_ticks = -15;
				}
			}
		}
	}

	return;
}
/* biped_falling_damage (bipeds.c 822-903 in the ship form at ff2bb9fb), on the delete list: only biped_update_moving
   called it. The same body; the HALO_GAME_BROWSER block (842-848) is 847's line alone, "if (!immune)", never defined
   on the PSP; PSP_BMS(biped, X) is biped->X; and the window wrappers (861's and 873's PSP_BMS_SYNCV, 897's
   PSP_BMS_DROPV) go, their calls bare */
static void store_motion_falling_damage(
	long biped_index,
	real collision_velocity)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);
	struct game_globals_falling_damage *falling_damage = TAG_BLOCK_GET_ELEMENT(
		&scenario_get_game_globals()->falling_damage,
		0,
		struct game_globals_falling_damage);
	boolean immune =
		TEST_FLAG(biped->unit.flags, _unit_no_falling_damage_bit) ||
		TEST_FLAG(definition->biped.flags, _biped_immune_to_falling_damage_bit);
	real falling_velocity = biped->object.root->translational_velocity.k;

	/* port: MCC clients predict only the effect received from the host. */
	if (!(cheat.jetpack || mcc_script_effect_enabled(MCC_EFFECT_JETPACK)) || biped->unit.player_index == NONE)
	{
		if (collision_velocity > falling_damage->runtime_minimum_damage_velocity)
		{
			if (!immune)
			{
				struct damage_data damage;

				damage_data_new(&damage, falling_damage->falling_damage.index);
				damage.scale =
					(collision_velocity - falling_damage->runtime_minimum_damage_velocity) /
					(falling_damage->runtime_maximum_damage_velocity -
						falling_damage->runtime_minimum_damage_velocity);
				damage.scale = PIN(
					damage.scale,
					0.f,
					1.f);
				object_cause_damage(&damage, biped_index, NONE, NONE, NONE, NULL);
			}
		}
		else if (!TEST_FLAG(definition->biped.flags, _biped_flying_bit) &&
			falling_velocity <
				-falling_damage->runtime_maximum_falling_velocity)
		{
			if (!immune && !TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
			{
				struct damage_data damage;

				damage_data_new(&damage, falling_damage->maximum_distance_damage.index);
				object_cause_damage(&damage, biped_index, NONE, NONE, NONE, NULL);
			}

			/* (port: not the host's biped on a client, which the host erases:
			the delete was refused, and logged as done) */
			if (!game_engine_running() &&
				TEST_FLAG(biped->object.flags, _object_outside_of_map_bit) &&
				player_index_from_unit_index(biped_index) == NONE &&
				network_objects_may_delete(biped_index))
			{
				long actor_index = biped->unit.swarm_actor_index;

				if (actor_index == NONE)
					actor_index = biped->unit.actor_index;
				error(
					_error_silent,
					"WARNING: biped %s (%s) fell outside world and was erased",
					tag_name_strip_path(tag_get_name(biped->definition_index)),
					ai_debug_describe_actor(
						actor_index,
						biped_index,
						TRUE,
						temporary,
						256));
				object_delete(biped_index);
			}
		}
	}

	return;
}
/* biped_start_landing (bipeds.c 919-968 in the ship form at ff2bb9fb), on the delete list: only biped_update_moving
   called it. The same body; PSP_BMS(biped, X) is biped->X (924) */
static void store_motion_landing(
	long biped_index,
	real landing_velocity)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);
	real minimum_soft_landing_velocity =
		definition->biped.minimum_soft_landing_velocity * (1.f / TICKS_PER_SECOND);
	real minimum_hard_landing_velocity =
		definition->biped.minimum_hard_landing_velocity * (1.f / TICKS_PER_SECOND);

	short landing_type;
	real velocity;
	real velocity_range;
	real recovery_time;

	if (landing_velocity < minimum_soft_landing_velocity)
		return;

	if (landing_velocity < minimum_hard_landing_velocity)
	{
		velocity = landing_velocity - minimum_soft_landing_velocity;
		velocity_range =
			minimum_hard_landing_velocity - minimum_soft_landing_velocity;
		recovery_time = definition->biped.maximum_soft_landing_time;
		landing_type = 0;
	}
	else
	{
		velocity = landing_velocity;
		velocity_range =
			definition->biped.maximum_hard_landing_velocity *
				(1.f / TICKS_PER_SECOND) - minimum_hard_landing_velocity;
		recovery_time = definition->biped.maximum_hard_landing_time;
		landing_type = 1;
	}

	recovery_time *= TICKS_PER_SECOND;
	if (velocity_range > 0.f)
	{
		velocity /= velocity_range;
		velocity = PIN(velocity, 0.f, 1.f);
		(*tick2_unit_bip2_of(biped->object.root)).landing = landing_type;
		(*tick2_unit_bip2_of(biped->object.root)).landing_recovery_counter = 0;
		(*tick2_unit_bip2_of(biped->object.root)).landing_recovery_time =
			(char)(long)(recovery_time * velocity);
	}

	return;
}
/* biped_update_jumping (bipeds.c 2539-2618 in the ship form at ff2bb9fb), on the delete list: only
   biped_update_moving called it. The same body; PSP_BMS(W)(biped, X) is biped->X; and biped_jump (2555) is
   store_motion_jump */
static void store_motion_jumping(
	long biped_index,
	struct unit_animation_update_data *animation)
{
	struct biped_datum *biped = biped_get(biped_index);

	biped_definition_get(biped->definition_index);
	if (!TEST_FLAG(biped->biped.flags, _biped_airborne_bit) &&
		(*tick2_unit_bip2_of(biped->object.root)).landing != 1)
	{
		if ((*tick2_unit_bip2_of(biped->object.root)).jump_recovery_timer < SCHAR_MAX)
			++(*tick2_unit_bip2_of(biped->object.root)).jump_recovery_timer;

		if (TEST_FLAG((*tick2_unit_ctl_of(biped->object.root)).control_flags, _unit_control_jump_bit) &&
			(*tick2_unit_bip2_of(biped->object.root)).jump_recovery_timer > 5)
		{
			store_motion_jump(biped_index);
		}
	}

	/* port: MCC clients use replicated campaign state, not local cheat permissions. */
	if ((cheat.jetpack || mcc_script_effect_enabled(MCC_EFFECT_JETPACK)) && biped->unit.player_index != NONE)
	{
		struct player_datum *player = player_get(biped->unit.player_index);
		boolean impulse = FALSE;

		if (TEST_FLAG((*tick2_unit_ctl_of(biped->object.root)).control_flags, _unit_control_weapon_primary_trigger_bit) &&
			TEST_FLAG((*tick2_unit_ctl_of(biped->object.root)).control_flags, _unit_control_throw_grenade_bit))
		{
			real forward_velocity = MAX(
				0.f,
				dot_product3d(
					&biped->object.root->translational_velocity,
					&(*tick2_unit_bip1_of(biped->object.root)).aiming_vector));
			real acceleration_scale = PIN(forward_velocity * 0.71428573f, 0.f, 1.f);
			real_vector3d lateral_velocity;

			point_from_line3d(
				(real_point3d *)&biped->object.root->translational_velocity,
				&(*tick2_unit_bip1_of(biped->object.root)).aiming_vector,
				-forward_velocity,
				(real_point3d *)&lateral_velocity);
			point_from_line3d(
				(real_point3d *)&biped->object.root->translational_velocity,
				&lateral_velocity,
				0.8f - 1.f,
				(real_point3d *)&biped->object.root->translational_velocity);
			point_from_line3d(
				(real_point3d *)&biped->object.root->translational_velocity,
				&(*tick2_unit_bip1_of(biped->object.root)).aiming_vector,
				acceleration_scale * 0.04f -
					acceleration_scale * acceleration_scale * 0.05f + 0.01f,
				(real_point3d *)&biped->object.root->translational_velocity);
			SET_FLAG(biped->biped.flags, _biped_airborne_bit, TRUE);
			impulse = TRUE;
		}

		if (!impulse &&
			TEST_FLAG((*tick2_unit_ctl_of(biped->object.root)).control_flags, _unit_control_crouch_modifier_bit) &&
			TEST_FLAG(biped->biped.flags, _biped_airborne_bit))
		{
			real_vector3d *velocity = &biped->object.root->translational_velocity;

			scale_vector3d(velocity, 0.8f - 1.f, velocity);
		}

		if (player->local_player_index != NONE && impulse)
		{
			struct rumble_definition rumble;

			csmemset(&rumble, 0, sizeof(rumble));
			rumble.motors[0].scale = 1.f;
			rumble.motors[0].duration = 0.3f;
			rumble.motors[0].transition_function = 3;
			rumble_player_impulse(player->local_player_index, &rumble, 1.f, 1.f);
		}
	}

	return;
}

/* _moving's tail: biped_update_moving from the return of its biped_update_physics(&physics) call to its end
   (4660-4831 in the ship form at ff2bb9fb; the old comment's 4792 and 4956 were another form's), on M's physics record
   after store_motion_call: the elevator's ticks and its object, the record's outputs to the datum (the position, the
   velocity, the flags, the ground plane), landing, jumping, the melee impact, bumps and falling damage. The engine's
   tests, float expressions and order are kept as they are, so the game computes bit-for-bit the same values. biped and
   definition are the held pair (the engine looked neither up here: biped_update_moving's own were the driver's). Left
   out: the old pass's stop 1 (4660-4664, "psp_bipedstage_phase == 3 && psp_bs3_cur && psp_bs3_cur->stop == 1"), which
   M's driver is now (store_motion_from).
   Renamed: the hoist's lookups (4668-4676, 4699-4701, 4724, 4731, 4734, 4760, 4791), each looked up once into line2
   and line3 as store_intent_moving looks its line2 up: nothing in the range moves a record, and the hoist's cache
   held across the same calls there; the record's fields, physics->X for the engine's physics.X (so
   &physics.position is &physics->position); PSP_BMS(biped, X) and PSP_BMSW(biped, X), the datum's own field under
   PSP_TICK2_ROOT (psp_bipedmove.h 37-40, 62-65), biped->X; the two window wrappers, PSP_BMS_NARROWV (4697) and
   PSP_BMS_SYNCV (4789-4797), whose calls stay bare, as the window never goes live under ROOT (box_ws_move_slot
   stays < 0, as t5s3-moving2 did with PSP_BMS_SYNC); and the four helpers take M's names: biped_start_landing,
   biped_update_jumping, biped_bumped_object and biped_falling_damage are store_motion_landing, store_motion_jumping,
   store_motion_bumped and store_motion_falling_damage, with their arguments as they are. The build's movement_scale
   and in_flags locals are never read here (the tail reads physics->in_flags, 4817 and 4822: the engine's
   movement_scale and in_flags were the build's). The four helpers are the next piece's, declared above.
   store_motion_from calls it */
static void store_motion_moved(long biped_index, struct biped_datum *biped, struct biped_definition *definition,
	struct unit_animation_update_data *animation, struct biped_physics *physics)
{
	struct tick2_unit_bip2 *line2 = tick2_unit_bip2_of(biped->object.root);
	struct tick2_unit_bip3 *line3 = tick2_unit_bip3_of(biped->object.root);

	if (physics->elevator_object_index == NONE)
	{
		if (line2->elevator_ticks <= 0)
			line2->elevator_object_index = NONE;
		else
			--line2->elevator_ticks;
	}
	else
	{
		line2->elevator_object_index = physics->elevator_object_index;
		line2->elevator_ticks = 60;
	}

	if (TEST_FLAG(biped->unit.flags, _unit_suspended_bit))
	{
		physics->new_position = physics->position;
		SET_FLAG(physics->out_flags, _biped_physics_out_airborne_bit, FALSE);
		physics->new_velocity = *global_zero_vector3d;
		physics->velocity = *global_zero_vector3d;
	}

	{
		real_point3d new_biped_position = physics->new_position;
		word out_flags = physics->out_flags;

		if (!TEST_FLAG(
			definition->biped.flags,
			_biped_pill_centered_at_origin_bit))
		{
			new_biped_position.z -= physics->width;
		}
		PSP_S1B_OPEN(biped, &new_biped_position); object_translate(biped_index, &new_biped_position, NULL); PSP_S1B_CLOSE(biped);

		line2->support_surface_index = physics->support_surface_index;
		line3->pathfinding_point = new_biped_position;
		line3->pathfinding_surface_index = NONE;
		biped->object.root->translational_velocity = physics->new_velocity;
		if (!animation->crouching &&
			TEST_FLAG(out_flags, _biped_physics_out_cannot_stand_bit))
		{
			animation->crouching = TRUE;
		}

		SET_FLAG(
			biped->biped.flags,
			_biped_airborne_bit,
			TEST_FLAG(out_flags, _biped_physics_out_airborne_bit));
		SET_FLAG(
			biped->biped.flags,
			_biped_slipping_bit,
			TEST_FLAG(out_flags, _biped_physics_out_slipping_bit));
		SET_FLAG(
			biped->biped.flags,
			_biped_movement_passes_through_bipeds_bit,
			TEST_FLAG(
				definition->biped.flags,
				_biped_passes_through_bipeds_bit));

		line2->ground_plane = physics->ground_plane;
		if (physics->landing_velocity > 0.f)
			store_motion_landing(biped_index, physics->landing_velocity);
		if (!TEST_FLAG(out_flags, _biped_physics_out_cannot_stand_bit))
			store_motion_jumping(biped_index, animation);

		if (biped->unit.melee_attack_state == _unit_melee_attack_impact &&
			line3->impact_target_object_index != NONE)
		{
			struct object_datum *target = object_get(
				line3->impact_target_object_index);
			real_vector3d melee_vector;

			match_assert(
				"c:\\halo\\SOURCE\\units\\bipeds.c",
				0x87A,
				COLLISION_USER_DEPTH <
					MAXIMUM_COLLISION_USER_STACK_DEPTH);
			COLLISION_USER_PUSH(_collision_user_bipeds);

			vector_from_points3d(
				&physics->position,
				&physics->new_position,
				&melee_vector);
			if (fast_vector_intersects_sphere(
				&physics->position,
				&melee_vector,
				&target->object.root->bounding_sphere_center,
				target->object.root->bounding_sphere_radius))
			{
				struct collision_model_instance instance;
				struct collision_model_test_vector_result vector_result;
				struct collision_result collision;

				if (collision_model_instance_new(
					&instance,
					line3->impact_target_object_index) &&
					collision_model_test_vector(
						&instance,
						FLAG(_collision_test_front_facing_surfaces_bit) |
							FLAG(_collision_test_back_facing_surfaces_bit),
						&physics->position,
						&melee_vector,
						&vector_result) &&
					!collision_test_vector(
						_collision_test_for_bipeds_passthrough_living_flags,
						&physics->position,
						&melee_vector,
						biped_index,
						&collision))
				{
					real_point3d collision_point;
					real_plane3d collision_plane;

					point_from_line3d(
						&physics->position,
						&melee_vector,
						vector_result.bsp_result.t,
						&collision_point);
					matrix4x3_transform_plane(
						&instance.matrices[vector_result.node_index],
						vector_result.bsp_result.plane,
						&collision_plane);
					if (vector_result.bsp_result.plane_designator < 0)
						plane3d_negate(&collision_plane, &collision_plane);
					unit_impact_melee_damage(
						biped_index,
						line3->impact_target_object_index,
						vector_result.node_index,
						vector_result.region_index,
						vector_result.bsp_result.material_index,
						&collision_point,
						&collision_plane.n,
						&collision.location);
				}
			}

			match_assert(
				"c:\\halo\\SOURCE\\units\\bipeds.c",
				0x8A5,
				COLLISION_USER_DEPTH > 1);
			COLLISION_USER_POP();
		}

		store_motion_bumped(
			biped_index,
			physics->bumped_object_index,
			&physics->velocity);
		store_motion_falling_damage(biped_index, physics->landing_velocity);

		SET_FLAG(
			biped->object.flags,
			_object_on_ground_bit,
			!TEST_FLAG(physics->in_flags, _biped_physics_in_flying_bit) &&
				!TEST_FLAG(biped->biped.flags, _biped_airborne_bit));
		SET_FLAG(
			biped->object.flags,
			_object_at_rest_bit,
			!TEST_FLAG(physics->in_flags, _biped_physics_in_flying_bit) &&
				!TEST_FLAG(biped->biped.flags, _biped_airborne_bit) &&
				!TEST_FLAG(out_flags, _biped_physics_out_volatile_collision_bit) &&
				magnitude_squared3d(&biped->object.root->translational_velocity) < 0.0001f);
		if (TEST_FLAG(biped->object.flags, _object_on_ground_bit))
			biped->object.root->angular_velocity = *global_zero_vector3d;
	}

	return;
}

/* the limp body (bipeds.c biped_start_limp_body_physics and _stop_, and biped_limp_noodle.c's six), on the
   delete list: only biped_accelerate, units.c (4372, 10621) and network_objects.c (852) call the start and the stop,
   and only biped_update_dead called the relax (bipeds.c 2245). The same text with the store's names, because the
   delete cut poisons the engine's in objects.h, which this file includes, and the noodle's
   validate_real_vector3d_axes3 is only the noodle's (static here as store_limp_validate_axes3, so it never meets
   the engine's). The start and the stop stay public in store_motion.h for those four callers, which the delete cut
   points here; store_limp_relax is store_motion_dead's. point_from_line3d is real_math.h's inline here as in the
   noodle's own unit: neither defines real_math.h's external-point-from-line3d switch, so the file adds no define */
/* ---- the limp body, moved from biped_limp_noodle.c 87-855 and bipeds.c 724-764 (the ship form at ff2bb9fb): begin ---- */
/* ---------- constants */

enum
{
	_joint_type_ball_and_socket = 0,
	_joint_type_hinge,
	_joint_type_no_movement,
	NUMBER_OF_NODE_BLOCK_FLAGS,
};

/* ---------- macros */



/* ---------- structures */

struct animation_graph_node
{
	char name[TAG_STRING_LENGTH + 1];
	short next_sibling_node_index;
	short first_child_node_index;
	short parent_node_index;
	word pad;
	unsigned long flags;
	real_vector3d base_vector;
	real range;
	long pad1;
};

/* ---------- prototypes */

static boolean store_limp_valid_joint_rotation(
	long biped_index,
	long node_index,
	real_matrix4x3 *node_matrices,
	real_point3d *current_world_position,
	real_point3d const *new_world_position,
	unsigned long *moved_node_flags);
static void store_limp_move_relax_and_constrain_positions(
	long biped_index,
	real_matrix4x3 *node_matrices);
static void store_limp_adjust_orientations(
	long biped_index,
	real_matrix4x3 *node_matrices,
	long node_count,
	real_point3d const *last_positions);
static boolean store_limp_nodes_valid(
	long biped_index,
	struct animation_graph const *animation_graph);
/* ---------- globals */

static struct collision_feature_list features;
static real_point3d last_positions[MAXIMUM_NODES_PER_ANIMATION];

/* ---------- public code */

static long store_limp_max_iterations(
	long biped_index)
{
	return 20;
}

/* ---------- private code */

static boolean store_limp_valid_joint_rotation(
	long biped_index,
	long node_index,
	real_matrix4x3 *node_matrices,
	real_point3d *current_world_position,
	real_point3d const *new_world_position,
	unsigned long *moved_node_flags)
{
	boolean moved = FALSE;
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);
	struct animation_graph *animation_graph = animation_graph_definition_get(
		definition->object.animation_graph.index);
	struct animation_graph_node *node = TAG_BLOCK_GET_ELEMENT(
		&animation_graph->nodes,
		node_index,
		struct animation_graph_node);
	struct animation_graph_node *parent_node = TAG_BLOCK_GET_ELEMENT(
		&animation_graph->nodes,
		node->parent_node_index,
		struct animation_graph_node);
	real collision_radius = animation_graph->limp_body_node_collision_radius;

	if (realcmp(collision_radius, 0.f) ||
		collision_radius < 0.f ||
		collision_radius > 0.07f)
	{
		collision_radius = 0.03f;
	}

	if (node->parent_node_index &&
		!TEST_FLAG(parent_node->flags, _joint_type_no_movement))
	{
		real_vector3d current_direction;
		real_vector3d desired_direction;
		real_vector3d rotation_axis;
		real rotation_cosine;

		vector_from_points3d(
			&node_matrices[node->parent_node_index].position,
			&node_matrices[node_index].position,
			&current_direction);
		vector_from_points3d(
			&node_matrices[node->parent_node_index].position,
			new_world_position,
			&desired_direction);
		normalize3d(&current_direction);
		normalize3d(&desired_direction);
		cross_product3d(&current_direction, &desired_direction, &rotation_axis);
		normalize3d(&rotation_axis);
		rotation_cosine = dot_product3d(&current_direction, &desired_direction);

		if (!realcmp(rotation_cosine, 1.f))
		{
			real rotation_angle = arccosine(rotation_cosine);
			real_matrix4x3 parent_inverse;
			real_matrix4x3 grandparent_inverse;
			real_vector3d local_rotation_axis;
			real_vector3d parent_axis;

			matrix4x3_inverse(&node_matrices[node->parent_node_index], &parent_inverse);
			matrix4x3_inverse(
				&node_matrices[parent_node->parent_node_index],
				&grandparent_inverse);
			matrix4x3_transform_vector(
				&parent_inverse,
				&rotation_axis,
				&local_rotation_axis);
			parent_axis = node_matrices[node->parent_node_index].forward;
			matrix4x3_transform_vector(
				&grandparent_inverse,
				&parent_axis,
				&parent_axis);

			if (TEST_FLAG(parent_node->flags, _joint_type_hinge))
			{
				real_vector3d plane_normal = node_matrices[node->parent_node_index].up;
				real_plane3d plane;
				real projection_distance;
				real_point3d rotate_to_position;
				real_vector3d rotate_to_direction;
				real_vector3d local_rotate_to_direction;

				plane3d_from_point_and_normal(
					&plane,
					&node_matrices[node->parent_node_index].position,
					&plane_normal);
				projection_distance =
					plane3d_distance_to_point(&plane, new_world_position) * -1.f;
				point_from_line3d(
					new_world_position,
					&plane_normal,
					projection_distance,
					&rotate_to_position);

				vector_from_points3d(
					&node_matrices[node->parent_node_index].position,
					&rotate_to_position,
					&rotate_to_direction);
				normalize3d(&rotate_to_direction);
				matrix4x3_transform_vector(
					&parent_inverse,
					&rotate_to_direction,
					&local_rotate_to_direction);

				if (!realcmp(dot_product3d(&parent_node->base_vector, &local_rotate_to_direction), 1.f))
				{
					BIT_VECTOR_SET_FLAG(moved_node_flags, node_index, TRUE);
					if (current_world_position->z >= rotate_to_position.z &&
						!collision_test_sphere(
							&rotate_to_position,
							collision_radius,
							biped_index))
					{
						*current_world_position = rotate_to_position;
					}
					moved = TRUE;
				}
			}
			else
			{
				real constraint_cosine;

				rotate_vector_about_axis(
					&parent_axis,
					&local_rotation_axis,
					sine(rotation_angle),
					rotation_cosine);
				constraint_cosine = dot_product3d(&parent_node->base_vector, &parent_axis);
				if (!realcmp(constraint_cosine, 1.f) &&
					fabs(arccosine(constraint_cosine)) < parent_node->range &&
					current_world_position->z > new_world_position->z)
				{
					BIT_VECTOR_SET_FLAG(moved_node_flags, node_index, TRUE);
					moved = TRUE;
					*current_world_position = *new_world_position;
				}
			}
		}
	}

	if ((TEST_FLAG(parent_node->flags, _joint_type_no_movement) || !moved) &&
		BIT_VECTOR_TEST_FLAG(moved_node_flags, node->parent_node_index) &&
		current_world_position->z > new_world_position->z)
	{
		BIT_VECTOR_SET_FLAG(moved_node_flags, node_index, TRUE);
		*current_world_position = *new_world_position;
		moved = TRUE;
	}

	return moved;
}

static void store_limp_move_relax_and_constrain_positions(
	long biped_index,
	real_matrix4x3 *node_matrices)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);
	struct animation_graph *animation_graph = animation_graph_definition_get(
		definition->object.animation_graph.index);
	real collision_radius = animation_graph->limp_body_node_collision_radius;
	byte total_iterations;
	byte current_iteration;
	real relaxation_fraction;
	unsigned long moved_node_flags[BIT_VECTOR_SIZE_IN_LONGS(MAXIMUM_NODES_PER_ANIMATION)];
	short node_queue[MAXIMUM_NODES_PER_ANIMATION];
	real_vector3d velocity;
	struct collision_plane collision_planes[10];
	long iteration;

	match_assert(
		"c:\\halo\\SOURCE\\units\\biped_limp_noodle.c",
		319,
		COLLISION_USER_DEPTH < MAXIMUM_COLLISION_USER_STACK_DEPTH);
	COLLISION_USER_PUSH(_collision_user_limp_body_physics);

	if (realcmp(collision_radius, 0.f) ||
		collision_radius < 0.f ||
		collision_radius > 0.07f)
	{
		collision_radius = 0.03f;
	}

	total_iterations = (*tick2_unit_bip2_of(biped->object.root)).limp_body_max_relaxation_iterations;
	if (total_iterations <= 0 || total_iterations >= 30)
		return;

	current_iteration = (*tick2_unit_bip2_of(biped->object.root)).limp_body_current_relaxation_iterations;
	relaxation_fraction =
		(real)(current_iteration + 1) /
		(real)total_iterations;
	if (realcmp(relaxation_fraction, 0.f))
		return;
	if (current_iteration >= total_iterations)
		goto collision_user_end;

	collision_get_features_in_sphere(
		_collision_test_for_bipeds_dead_flags |
			FLAG(_collision_test_ignore_invisible_surfaces_bit),
		&biped->object.root->position,
		biped->object.root->bounding_sphere_radius + 0.0625f,
		0.f,
		collision_radius,
		biped_index,
		&features);
	csmemset(moved_node_flags, 0, sizeof(moved_node_flags));

	for (iteration = 0; iteration < 4; iteration++)
	{
		short queue_head = 0;
		short queue_tail = 1;

		node_queue[0] = 0;
		do
		{
			short node_index = node_queue[queue_head++];
			struct animation_graph_node *node = TAG_BLOCK_GET_ELEMENT(
				&animation_graph->nodes,
				node_index,
				struct animation_graph_node);

			if (node_index)
			{
				real_point3d *position;
				real_point3d *parent_position;
				real_vector3d segment;

				set_real_vector3d(
					&velocity,
					0.f,
					0.f,
					relaxation_fraction * -0.032086615f);
				position = &node_matrices[node_index].position;
				parent_position = &node_matrices[node->parent_node_index].position;
				vector_from_points3d(parent_position, position, &segment);

				if (!iteration &&
					!collision_test_sphere(
						position,
						collision_radius,
						biped_index))
				{
					real_point3d moved_position;
					real_vector3d moved_velocity;

					collision_move_point(
						position,
						&velocity,
						&features,
						&moved_position,
						&moved_velocity,
						3,
						collision_planes);
					if (realcmp(moved_velocity.i, 0.f) &&
						realcmp(moved_velocity.j, 0.f))
					{
						store_limp_valid_joint_rotation(
							biped_index,
							node_index,
							node_matrices,
							position,
							&moved_position,
							moved_node_flags);
					}
				}

				{
					real_point3d ray_origin;
					struct collision_result collision;

					vector_from_points3d(parent_position, position, &segment);
					point_from_line3d(parent_position, &segment, -0.015f, &ray_origin);
					scale_vector3d(&segment, 1.03f, &segment);

					if (collision_test_vector(
						_collision_test_for_bipeds_dead_flags |
							FLAG(_collision_test_ignore_invisible_surfaces_bit),
						&ray_origin,
						&segment,
						biped_index,
						&collision))
					{
						boolean embedded[2];
						real push_distance[2];
						long endpoint_index;
						long embedded_count;

						embedded[0] = collision_test_sphere(
							position,
							0.03f,
							biped_index);
						embedded[1] = collision_test_sphere(
							parent_position,
							0.03f,
							biped_index);
						embedded_count = embedded[0] + embedded[1];

						if (embedded_count)
						{
							if (embedded_count == 2)
							{
								for (endpoint_index = 0; endpoint_index < 2; endpoint_index++)
								{
									push_distance[endpoint_index] = vector_intersect_plane3d(
										endpoint_index == 0 ? position : parent_position,
										&collision.plane.n,
										&collision.plane);
									if (push_distance[endpoint_index] != 0.f)
									{
										push_distance[endpoint_index] +=
											collision_radius * 2.5f;
									}
								}
							}
							else
							{
								for (endpoint_index = 0; endpoint_index < 2; endpoint_index++)
								{
									if (embedded[endpoint_index])
									{
										push_distance[endpoint_index] = vector_intersect_plane3d(
											endpoint_index == 0 ? position : parent_position,
											&collision.plane.n,
											&collision.plane);
										if (push_distance[endpoint_index] != 0.f)
										{
											push_distance[endpoint_index] +=
												collision_radius * 2.5f;
										}
									}
									else
									{
										push_distance[endpoint_index] = 0.f;
									}
								}
							}

							for (endpoint_index = 0; endpoint_index < 2; endpoint_index++)
							{
								if (!realcmp(push_distance[endpoint_index], 0.f))
								{
									point_from_line3d(
										endpoint_index == 0 ? position : parent_position,
										&collision.plane.n,
										relaxation_fraction * push_distance[endpoint_index],
										endpoint_index == 0 ? position : parent_position);
								}
							}
						}
					}
				}

				vector_from_points3d(parent_position, position, &segment);
				{
					struct model *model = model_definition_get(
						definition->object.model.index);
					struct model_node *model_node = TAG_BLOCK_GET_ELEMENT(
						&model->nodes,
						node_index,
						struct model_node);
					real rest_distance = model_node->node_distance_from_parent;
					real distance = distance3d(position, parent_position);

					if (!(rest_distance <= 0.f || rest_distance > 10.f) &&
						distance >= 0.f &&
						distance < 20.f &&
						!realcmp(distance, 0.f))
					{
						if (!realcmp(rest_distance, 0.f) &&
							rest_distance != distance &&
							!realcmp(distance, 0.f))
						{
							real correction = (rest_distance - distance) / distance;

							if (node->parent_node_index)
							{
								real half_correction = correction * 0.5f;

								scale_vector3d(&segment, -half_correction, &velocity);
								collision_move_point(
									parent_position,
									&velocity,
									&features,
									parent_position,
									&velocity,
									3,
									collision_planes);
								scale_vector3d(&segment, half_correction, &velocity);
								collision_move_point(
									position,
									&velocity,
									&features,
									position,
									&velocity,
									3,
									collision_planes);
							}
							else
							{
								scale_vector3d(&segment, correction, &velocity);
								if (collision_test_sphere(
									position,
									collision_radius,
									biped_index))
								{
									goto next_node;
								}
								collision_move_point(
									position,
									&velocity,
									&features,
									position,
									&velocity,
									3,
									collision_planes);
							}
						}
					}
					else
					{
						/* port: no more than node_queue holds (a map's links,
						which could go in a loop) */
						if (node->next_sibling_node_index != NONE && queue_tail < (short)NUMBEROF(node_queue))
							node_queue[queue_tail++] = node->next_sibling_node_index;
						if (node->first_child_node_index != NONE && queue_tail < (short)NUMBEROF(node_queue))
							node_queue[queue_tail++] = node->first_child_node_index;
						continue;
					}
				}
			}

next_node:
			/* port: no more than node_queue holds (a map's links, which could
			go in a loop) */
			if (node->next_sibling_node_index != NONE && queue_tail < (short)NUMBEROF(node_queue))
				node_queue[queue_tail++] = node->next_sibling_node_index;
			if (node->first_child_node_index != NONE && queue_tail < (short)NUMBEROF(node_queue))
				node_queue[queue_tail++] = node->first_child_node_index;
		}
		while (queue_head != queue_tail);
	}

collision_user_end:
	match_assert(
		"c:\\halo\\SOURCE\\units\\biped_limp_noodle.c",
		530,
		COLLISION_USER_DEPTH > 1);
	COLLISION_USER_POP();

	return;
}

static void store_limp_validate_axes3(
	real_vector3d *forward,
	real_vector3d *left,
	real_vector3d *up)
{
	match_assert("c:\\halo\\SOURCE\\units\\biped_limp_noodle.c", 535, forward);
	match_assert("c:\\halo\\SOURCE\\units\\biped_limp_noodle.c", 535, left);
	match_assert("c:\\halo\\SOURCE\\units\\biped_limp_noodle.c", 535, up);

	if (normalize3d(forward) == 0.f)
		*forward = *global_forward3d;
	if (normalize3d(up) == 0.f)
		*up = *global_up3d;

	cross_product3d(up, forward, left);
	if (normalize3d(left) == 0.f)
		*left = *global_left3d;

	cross_product3d(forward, left, up);
	if (normalize3d(up) == 0.f)
		*up = *global_up3d;

	cross_product3d(up, forward, left);
	if (normalize3d(left) == 0.f)
		*left = *global_left3d;

	return;
}

static void store_limp_adjust_orientations(
	long biped_index,
	real_matrix4x3 *node_matrices,
	long node_count,
	real_point3d const *last_positions)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);
	struct animation_graph *animation_graph = animation_graph_definition_get(
		definition->object.animation_graph.index);
	long node_index;

	for (node_index = 0; node_index < animation_graph->nodes.count; node_index++)
	{
		struct animation_graph_node *node;
		struct animation_graph_node *parent_node;
		real_vector3d last_direction;
		real_vector3d current_direction;
		real_vector3d rotation_axis;
		real rotation_cosine;
		real rotation_angle;

		if (!node_index)
			continue;

		node = TAG_BLOCK_GET_ELEMENT(
			&animation_graph->nodes,
			node_index,
			struct animation_graph_node);
		parent_node = TAG_BLOCK_GET_ELEMENT(
			&animation_graph->nodes,
			node->parent_node_index,
			struct animation_graph_node);
		if (TEST_FLAG(parent_node->flags, _joint_type_no_movement))
			continue;

		vector_from_points3d(
			&last_positions[node->parent_node_index],
			&last_positions[node_index],
			&last_direction);
		vector_from_points3d(
			&node_matrices[node->parent_node_index].position,
			&node_matrices[node_index].position,
			&current_direction);
		normalize3d(&last_direction);
		normalize3d(&current_direction);
		cross_product3d(&last_direction, &current_direction, &rotation_axis);
		normalize3d(&rotation_axis);
		rotation_cosine = dot_product3d(&current_direction, &last_direction);
		if (fabs(rotation_cosine - 1.f) < _real_epsilon)
			continue;

		rotation_angle = arccosine(rotation_cosine);
		if (fabs(rotation_angle) < _real_epsilon ||
			!(fabs(rotation_angle) < 0.78539819f))
		{
			continue;
		}

		if (!valid_real_vector3d_axes3(
			&node_matrices[node->parent_node_index].forward,
			&node_matrices[node->parent_node_index].left,
			&node_matrices[node->parent_node_index].up))
		{
			store_limp_validate_axes3(
				&node_matrices[node->parent_node_index].forward,
				&node_matrices[node->parent_node_index].left,
				&node_matrices[node->parent_node_index].up);
		}

		rotate_vector_about_axis(
			&node_matrices[node->parent_node_index].forward,
			&rotation_axis,
			sine(rotation_angle),
			rotation_cosine);
		rotate_vector_about_axis(
			&node_matrices[node->parent_node_index].up,
			&rotation_axis,
			sine(rotation_angle),
			rotation_cosine);
		normalize3d(&node_matrices[node->parent_node_index].forward);
		normalize3d(&node_matrices[node->parent_node_index].up);
		cross_product3d(
			&node_matrices[node->parent_node_index].up,
			&node_matrices[node->parent_node_index].forward,
			&node_matrices[node->parent_node_index].left);
		normalize3d(&node_matrices[node->parent_node_index].left);

		if (!valid_real_vector3d_axes3(
			&node_matrices[node->parent_node_index].forward,
			&node_matrices[node->parent_node_index].left,
			&node_matrices[node->parent_node_index].up))
		{
			store_limp_validate_axes3(
				&node_matrices[node->parent_node_index].forward,
				&node_matrices[node->parent_node_index].left,
				&node_matrices[node->parent_node_index].up);
		}
	}

	return;
}

static boolean store_limp_relax(
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);
	struct animation_graph *animation_graph = animation_graph_definition_get(
		definition->object.animation_graph.index);
	struct object_datum *object = object_get(biped_index);
#ifdef PSP_BOX_POSING
	/* psp_fixups.py (FIXES_BOXPOSING, means B): the body reads and moves
	every node: an owed one is made first (a pose owes nothing once dead, but
	this tick's may be the last made alive) */
	void psp_boxb_whole(long object_index);

	psp_boxb_whole(biped_index);
#endif
	real_matrix4x3 *node_matrices = (real_matrix4x3 *)store_pose_limp_matrices(object->object.root);
	boolean relaxation_complete =
		(*tick2_unit_bip2_of(biped->object.root)).limp_body_current_relaxation_iterations >=
		(*tick2_unit_bip2_of(biped->object.root)).limp_body_max_relaxation_iterations;

	/* port: a graph whose nodes don't fit the biped is done relaxing (a
	map's counts and links) */
	if (!relaxation_complete &&
		!store_limp_nodes_valid(biped_index, animation_graph))
	{
		relaxation_complete = TRUE;
	}

	if (!relaxation_complete)
	{
		long node_index;
		/* port: the limp body poses the graph's nodes, in the biped's node
		matrices, which are its model's. A Halo PC map's graph may have
		more nodes than its model (beavercreek_rev_beta's cyborg's has 20,
		its model 19), which Halo PC's engine posed past the matrices: such
		a body is posed in a copy of them with the graph's every node, the
		extra ones where their parents are, and the model's taken back */
		long model_node_count = definition->object.model.index == NONE ? 1 :
			model_definition_get(definition->object.model.index)->nodes.count;
		real_matrix4x3 padded_matrices[MAXIMUM_NODES_PER_ANIMATION];
		real_matrix4x3 *limp_matrices = node_matrices;

		if (animation_graph->nodes.count > model_node_count &&
			animation_graph->nodes.count <= MAXIMUM_NODES_PER_ANIMATION)
		{
			csmemcpy(padded_matrices, node_matrices, model_node_count * sizeof(real_matrix4x3));
			for (node_index = model_node_count; node_index < animation_graph->nodes.count; node_index++)
			{
				short parent_index = TAG_BLOCK_GET_ELEMENT(&animation_graph->nodes, node_index,
					struct animation_graph_node)->parent_node_index;

				padded_matrices[node_index] = padded_matrices[parent_index >= 0 && parent_index < node_index ?
					parent_index : 0];
			}
			limp_matrices = padded_matrices;
		}

		for (node_index = 0; node_index < animation_graph->nodes.count; node_index++)
		{
			last_positions[node_index] =
				limp_matrices[node_index].position;
		}

		store_limp_move_relax_and_constrain_positions(biped_index, limp_matrices);
		store_limp_adjust_orientations(
			biped_index,
			limp_matrices,
			animation_graph->nodes.count,
			last_positions);
		if (limp_matrices != node_matrices)
			csmemcpy(node_matrices, padded_matrices, model_node_count * sizeof(real_matrix4x3));

		if ((*tick2_unit_bip2_of(biped->object.root)).limp_body_current_relaxation_iterations < CHAR_MAX)
			(*tick2_unit_bip2_of(biped->object.root)).limp_body_current_relaxation_iterations++;
	}

	return relaxation_complete;
}

/* port: TRUE when the graph's nodes fit last_positions, node_queue and the
biped's node matrices, and every link names one of them (a map's counts and
indices). The relaxation walks them without checking. Released bipeds have at
most 41 nodes, as many as their models, and no bad links. */
static boolean store_limp_nodes_valid(
	long biped_index,
	struct animation_graph const *animation_graph)
{
	static boolean reported = FALSE;
	struct object_datum *object = object_get(biped_index);
	long node_count = animation_graph->nodes.count;
	boolean valid = node_count > 0 &&
		node_count <= MAXIMUM_NODES_PER_ANIMATION &&
		node_count <= store_node_count(object->object.root);
	long node_index;

	for (node_index = 0; valid && node_index < node_count; node_index++)
	{
		struct animation_graph_node *node = TAG_BLOCK_GET_ELEMENT(
			&animation_graph->nodes,
			node_index,
			struct animation_graph_node);

		if ((node->next_sibling_node_index != NONE && !VALID_INDEX(node->next_sibling_node_index, node_count)) ||
			(node->first_child_node_index != NONE && !VALID_INDEX(node->first_child_node_index, node_count)) ||
			(node_index && !VALID_INDEX(node->parent_node_index, node_count)))
		{
			valid = FALSE;
		}
	}

	if (!valid && !reported)
	{
		reported = TRUE;
		error(_error_silent, "### ERROR %s has %ld animation nodes or a bad node link; its body doesn't relax",
			tag_get_name(object->definition_index), node_count);
	}

	return valid;
}

void store_biped_limp_start(
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	boolean uses_limp_body_physics = TEST_FLAG(
		biped_definition_get(biped->definition_index)->biped.flags,
		_biped_uses_limp_body_physics_bit);

	if (!debug_biped_limp_body_disable &&
		uses_limp_body_physics &&
		TEST_FLAG(biped->object.flags, _object_at_rest_bit) &&
		!TEST_FLAG(biped->biped.flags, _biped_airborne_bit) &&
		!TEST_FLAG(biped->biped.flags, _biped_limp_body_physics_active_bit))
	{
		(*tick2_unit_bip2_of(biped->object.root)).limp_body_current_relaxation_iterations = 0;
		(*tick2_unit_bip2_of(biped->object.root)).limp_body_max_relaxation_iterations =
			(byte)store_limp_max_iterations(biped_index);
		SET_FLAG(biped->object.flags, _object_do_not_recompute_node_matrices_bit, TRUE);
		SET_FLAG(biped->biped.flags, _biped_limp_body_physics_active_bit, TRUE);
	}

	return;
}

void store_biped_limp_stop(
	long biped_index)
{
	struct biped_datum *biped;
	struct biped_definition *definition;

	biped = biped_get(biped_index);
	definition = biped_definition_get(biped->definition_index);
	if (TEST_FLAG(definition->biped.flags, _biped_uses_limp_body_physics_bit) &&
		TEST_FLAG(biped->biped.flags, _biped_limp_body_physics_active_bit))
	{
		SET_FLAG(biped->object.flags, _object_do_not_recompute_node_matrices_bit, FALSE);
		SET_FLAG(biped->biped.flags, _biped_limp_body_physics_active_bit, FALSE);
	}

	return;
}
/* ---- the limp body: finish ---- */

/* biped_apply_rotation (bipeds.c 1399-1425 in the ship form at ff2bb9fb), on the delete list: only biped_update_dead
   and _airborne called it. The same body */
static void store_motion_rotation(
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	real_vector3d axis = biped->object.root->angular_velocity;
	real angle = normalize3d(&axis);
	real cosine = (real)cos(angle);
	real sine = (real)sin(angle);

	rotate_vector_about_axis(&biped->object.root->forward, &axis, sine, cosine);
	normalize3d(&biped->object.root->forward);
	{
		real_vector3d rotated_up = biped->object.root->up;
		real_vector3d left;

		rotate_vector_about_axis(&rotated_up, &axis, sine, cosine);
		cross_product3d(&biped->object.root->forward, &rotated_up, &left);
		cross_product3d(&left, &biped->object.root->forward, &biped->object.root->up);
		if (normalize3d(&biped->object.root->up)==0.f)
		{
			biped->object.root->forward = *global_forward3d;
			biped->object.root->up = *global_up3d;
		}
	}

	return;
}

/* biped_falling_danger (bipeds.c 1989-2048 in the ship form at ff2bb9fb), on the delete list: only biped_update's
   tail called it. The same body; the held pair's lookups and FIXES_UNITHOIST's check go (the pair is always passed,
   never NULL, so it needs no lookup); PSP_BMS(biped, X) is biped->X; and the window wrapper (2043) goes, its call
   bare */
static void store_motion_falling_danger(
	long biped_index,
	struct biped_datum *biped,
	struct biped_definition *definition)
{
	if (!TEST_FLAG(biped->object.damage_flags, _object_dead_bit) &&
		!(definition->biped.flags &
			(FLAG(_biped_flying_bit) | FLAG(_biped_immune_to_falling_damage_bit))) &&
		!TEST_FLAG(biped->unit.flags, _unit_no_falling_damage_bit) &&
		biped->unit.actor_index!=NONE &&
		(*tick2_unit_anim_of(biped->object.root)).state!=_unit_state_ai_impulse)
	{
		long game_time = game_time_get();

		if ((*tick2_unit_bip2_of(biped->object.root)).airborne_ticks>30 &&
			((*tick2_unit_bip3_of(biped->object.root)).last_falling_communication_time==NONE ||
			(*tick2_unit_bip3_of(biped->object.root)).last_falling_communication_time+15<game_time))
		{
			struct game_globals_falling_damage *falling_damage = TAG_BLOCK_GET_ELEMENT(
				&scenario_get_game_globals()->falling_damage,
				0,
				struct game_globals_falling_damage);
			boolean dangerous = TRUE;
			real_point3d ground_point;

			(*tick2_unit_bip3_of(biped->object.root)).last_falling_communication_time = game_time;
			if (biped_find_ground_surface(
				biped_index,
				global_down3d,
				6.f,
				&ground_point,
				NULL)!=NONE)
			{
				real_point3d origin;

				object_get_origin(biped_index, &origin);
				if (biped->object.root->translational_velocity.k>0.f ||
					(origin.z-ground_point.z)*global_gravity*2.f +
					biped->object.root->translational_velocity.k*
					biped->object.root->translational_velocity.k <
					falling_damage->runtime_maximum_damage_velocity*
					falling_damage->runtime_maximum_damage_velocity)
				{
					dangerous = FALSE;
				}
			}

			if (dangerous)
				unit_scream(biped_index, _unit_scream_falling);
		}
	}

	return;
}

/* biped_update_dead (bipeds.c 2228-2274 in the ship form at ff2bb9fb), on the delete list: only biped_update's tail
   called it. The same body; the held pair's lookups and FIXES_UNITHOIST's check go (the pair is always passed, never
   NULL); PSP_BMS(biped, X) is biped->X; the window wrapper (2245) goes, its call bare, and that call is the store's
   own copy now, store_limp_relax; biped_apply_rotation (2256) is store_motion_rotation; and biped_snap_facing (2265)
   is biped_snap_facing_with on the pair */
static void store_motion_dead(
	long biped_index,
	struct biped_datum *biped,
	struct biped_definition *definition,
	struct unit_animation_update_data *animation)
{
	if (TEST_FLAG(biped->biped.flags, _biped_limp_body_physics_active_bit) &&
		(*tick2_unit_bip2_of(biped->object.root)).limp_body_current_relaxation_iterations <
			(*tick2_unit_bip2_of(biped->object.root)).limp_body_max_relaxation_iterations)
	{
		if (!debug_biped_limp_body_disable)
			store_limp_relax(biped_index);
		biped_verify_object_vectors(biped_index, "post-limp-noodle");
		animation->crouching = FALSE;

		return;
	}

	if ((*tick2_unit_bip2_of(biped->object.root)).airborne_ticks>=3 &&
		!TEST_FLAG(definition->biped.flags, _biped_has_no_dying_airborne_bit))
	{
		if ((*tick2_unit_anim_of(biped->object.root)).state==_unit_state_dying_airborne)
			store_motion_rotation(biped_index);
		animation->state_desired = _unit_state_dying_airborne;
		biped_verify_object_vectors(biped_index, "post-dying-airborne");
	}
	else
	{
		if ((*tick2_unit_anim_of(biped->object.root)).state==_unit_state_dying_airborne)
		{
			(*tick2_unit_bip2_of(biped->object.root)).bank = 0.f;
			biped_snap_facing_with(biped_index, biped, definition);
		}
		animation->state_desired = _unit_state_dying;
		biped_verify_object_vectors(biped_index, "post-dying");
	}

	animation->crouching = FALSE;

	return;
}

/* biped_try_to_make_footsteps (bipeds.c 2313-2408 in the ship form at ff2bb9fb), on the delete list: only
   biped_update's tail called it. The same body; the held pair's lookup, FIXES_UNITHOIST's check and the definition's
   lookup go (the pair is always passed, never NULL); biped_make_footstep (2358, 2359, 2376, 2391, 2392) is
   store_motion_footstep. Its jump to no_footstep (2374, the label 2383) is the engine's as it is */
static void store_motion_footsteps(
	long biped_index,
	struct biped_datum *biped)
{
	boolean air_contact;
	boolean moving;

	moving = FALSE;
	air_contact = FALSE;

	switch ((*tick2_unit_anim_of(biped->object.root)).state)
	{
	case _unit_state_turn_left:
	case _unit_state_turn_right:
		air_contact = TRUE;
		break;

	case _unit_state_move_front:
	case _unit_state_move_back:
	case _unit_state_move_left:
	case _unit_state_move_right:
		if (magnitude_squared3d(&(*tick2_unit_ctl_of(biped->object.root)).throttle)>0.25f)
			moving = TRUE;
		break;
	}

	if ((*tick2_anim_of(biped->object.root)).state.index!=NONE)
	{
		struct animation_graph *animation_graph = animation_graph_definition_get(
			(*tick2_anim_of(biped->object.root)).animation_graph_index);
		struct animation *animation = TAG_BLOCK_GET_ELEMENT(
			&animation_graph->animations,
			(*tick2_anim_of(biped->object.root)).state.index,
			struct animation);

		if (air_contact)
		{
			if ((*tick2_anim_of(biped->object.root)).state.frame_index==0)
			{
				store_motion_footstep(biped_index, _material_effect_biped_shuffle, 0);
				store_motion_footstep(biped_index, _material_effect_biped_shuffle, 1);
			}
		}
		else if (moving &&
			(animation->private_left_foot_frame_index ||
			animation->private_right_foot_frame_index))
		{
			short frame_index = (*tick2_anim_of(biped->object.root)).state.frame_index;
			boolean right_foot;

			if (frame_index==animation->private_left_foot_frame_index)
				right_foot = FALSE;
			else if (frame_index==animation->private_right_foot_frame_index)
				right_foot = TRUE;
			else
				goto no_footstep;

			store_motion_footstep(
				biped_index,
				(*tick2_unit_anim_of(biped->object.root)).base_seat_index==_unit_base_seat_stand,
				right_foot!=FALSE);
		}
	}

no_footstep:
	switch ((*tick2_unit_bip2_of(biped->object.root)).state)
	{
	case biped_state_idle:
		if ((*tick2_unit_bip2_of(biped->object.root)).stop_ticks>0)
		{
			if (++(*tick2_unit_bip2_of(biped->object.root)).stop_ticks>3)
			{
				store_motion_footstep(biped_index, _material_effect_biped_shuffle, 0);
				store_motion_footstep(biped_index, _material_effect_biped_shuffle, 1);
				(*tick2_unit_bip2_of(biped->object.root)).stop_ticks = 0;
			}
		}
		break;

	case biped_state_moving:
		(*tick2_unit_bip2_of(biped->object.root)).stop_ticks = 1;
		break;

	default:
		(*tick2_unit_bip2_of(biped->object.root)).stop_ticks = 0;
		break;
	}

	return;
}

/* biped_update_airborne (bipeds.c 2410-2478 in the ship form at ff2bb9fb), on the delete list: only biped_update's
   tail called it. The same body; the held pair's lookups and FIXES_UNITHOIST's check go (the pair is always passed,
   never NULL); PSP_BMS(biped, X) is biped->X; biped_apply_rotation (2456) is store_motion_rotation; its jump to
   axis_ready (2441, the label 2448) is the engine's as it is */
static void store_motion_airborne(
	long biped_index,
	struct biped_datum *biped,
	struct biped_definition *definition,
	struct unit_animation_update_data *animation)
{
	boolean flying = FALSE;

	if (biped_flying_through_air(biped_index))
	{
		flying = TRUE;
		if (TEST_FLAG(definition->biped.flags, _biped_rotate_while_airborne_bit))
		{
			if ((*tick2_unit_anim_of(biped->object.root)).state!=_unit_state_melee_airborne &&
				(*tick2_unit_anim_of(biped->object.root)).state!=_unit_state_leap_melee)
			{
				real impulse = real_seed_random_range(
					get_global_random_seed_address(),
					0.052359879f,
					0.08726646f);
				real_vector3d axis;

				if (biped->object.root->up.k<0.8f)
				{
					cross_product3d(&biped->object.root->up, global_up3d, &axis);
					if (normalize3d(&axis)>0.f)
						goto axis_ready;
				}

				{
					vector3d_from_angle(&axis, real_random_range(0.f, 2.f*_pi));
				}

			axis_ready:
				point_from_line3d(
					(real_point3d *)&biped->object.root->angular_velocity,
					&axis,
					impulse,
					(real_point3d *)&biped->object.root->angular_velocity);
			}

			store_motion_rotation(biped_index);
		}
	}

	match_assert(
		"c:\\halo\\SOURCE\\units\\bipeds.c",
		2687,
		TEST_FLAG(biped->biped.flags, _biped_airborne_bit));

	if ((*tick2_unit_anim_of(biped->object.root)).state==_unit_state_leap_start ||
		(*tick2_unit_anim_of(biped->object.root)).state==_unit_state_leap_airborne)
	{
		animation->state_desired = _unit_state_leap_airborne;
	}
	else if ((*tick2_unit_anim_of(biped->object.root)).state==_unit_state_airborne || flying)
	{
		animation->state_desired = _unit_state_airborne;
	}

	biped_verify_object_vectors(biped_index, "post-airborne");

	return;
}

/* biped_update_landing (bipeds.c 2480-2512 in the ship form at ff2bb9fb), on the delete list: only biped_update's
   tail called it. The same body; the held pair's lookup, FIXES_UNITHOIST's check and the definition's lookup go (the
   pair is always passed, never NULL); and biped_make_footstep (2501, 2502) is store_motion_footstep */
static void store_motion_landed(
	long biped_index,
	struct biped_datum *biped,
	struct unit_animation_update_data *animation)
{
	if (++(*tick2_unit_bip2_of(biped->object.root)).landing_recovery_counter>=(*tick2_unit_bip2_of(biped->object.root)).landing_recovery_time)
		(*tick2_unit_bip2_of(biped->object.root)).landing = NONE;

	if (!cinematic_in_progress() &&
		((*tick2_unit_bip2_of(biped->object.root)).landing_recovery_counter==2 ||
		((*tick2_unit_bip2_of(biped->object.root)).landing==NONE &&
		(*tick2_unit_bip2_of(biped->object.root)).landing_recovery_time<2)))
	{
		store_motion_footstep(biped_index, _material_effect_biped_jump_land, 0);
		store_motion_footstep(biped_index, _material_effect_biped_jump_land, 1);
	}

	animation->state_desired = (*tick2_unit_bip2_of(biped->object.root)).landing==1
		? _unit_state_land_hard
		: _unit_state_land_soft;

	biped_verify_object_vectors(biped_index, "post-landing");

	return;
}

/* biped_update_slipping (bipeds.c 2514-2537 in the ship form at ff2bb9fb), on the delete list: only biped_update's
   tail called it. The same body; the held pair's lookup, FIXES_UNITHOIST's check and the definition's lookup go (the
   pair is always passed, never NULL); and biped_make_footstep (2530, 2531) is store_motion_footstep */
static void store_motion_slipping(
	long biped_index,
	struct biped_datum *biped,
	struct unit_animation_update_data *animation)
{
	if ((*tick2_unit_bip2_of(biped->object.root)).slipping_ticks>3 &&
		magnitude_squared3d(&biped->object.root->translational_velocity)>MINIMUM_SLIPPING_FOOTSTEP_VELOCITY_SQUARED)
	{
		store_motion_footstep(biped_index, _material_effect_biped_sliding, 0);
		store_motion_footstep(biped_index, _material_effect_biped_sliding, 1);
	}

	biped_verify_object_vectors(biped_index, "post-slipping");

	return;
}

/* biped_check_discard (bipeds.c 4856-4896 in the ship form at ff2bb9fb), on the delete list: only biped_update's
   tail called it. The same body; the held pair's lookup and FIXES_UNITHOIST's check go (the pair is always passed,
   never NULL); PSP_BMS(biped, X) is biped->X; and the window wrapper (4892) goes, its call bare */
static boolean store_motion_discard(
	long biped_index,
	struct biped_datum *biped)
{
	/* port: not the host's biped on a client, which the host erases: the
	delete was refused, and logged as done every tick as it fell */
	if (!game_engine_running() &&
		(TEST_FLAG(biped->object.flags, _object_outside_of_map_bit) ||
		biped->object.root->location.cluster_index==NONE) &&
		biped->object.root->position.z<-2000.f &&
		network_objects_may_delete(biped_index))
	{
		long actor_index = biped->unit.swarm_actor_index;

		if (actor_index==NONE)
			actor_index = biped->unit.actor_index;

		error(
			_error_silent,
			"WARNING: biped %s (%s) is in a bad place (%.1f %.1f %.1f), erasing",
			tag_name_strip_path(tag_get_name(biped->definition_index)),
			ai_debug_describe_actor(
				actor_index,
				biped_index,
				TRUE,
				temporary,
				256),
			biped->object.root->position.x,
			biped->object.root->position.y,
			biped->object.root->position.z);
		object_delete(biped_index);
	}

	return FALSE;
}

/* biped_update's tail: biped_update from the post-moving verify to the post-update verify (5052-5138 and 5149-5162 in
   the ship form at ff2bb9fb; the old comment's 5178-5288 were another form's), on M's animation record: when moved
   (stop 1, or the whole update's build reached the move), dead/air/land/slip, the player's melee, the footsteps, the
   falling danger and the discard; then, always, the animation, the jump on its 1, the idle ticks and the verify. The
   biped and definition are the held pair. moved is 0 for a rider (stop 2: the engine's vehicle branch never reaches
   5052). The engine's tests, float expressions and order are kept as they are, so the game computes bit-for-bit the
   same values. Left out: the engine's two labels and the pass's stops (5139-5148: the else's brace, bipedstage3's stop
   at the tail, psp_bs3_resume_tail and psp_bs3_resume = 0) and 5163-5175 (psp_bs3_out, psp_bms_stop, FIXES_BSPR's
   put-back, which store_motion_owner_put is, profile_exit and return TRUE): M's driver holds them.
   Renamed: the hoist's lookups (5062, 5073, 5076, 5085, 5100, 5105, 5106, 5112, 5113, 5114, 5123, 5124, 5125, 5131,
   5133), each looked up once into control and line2 as store_intent_moving looks its line2 up: nothing in the range
   moves a record; PSP_BMS(biped, X) and PSP_BMSW(biped, X), the datum's own field under PSP_TICK2_ROOT
   (psp_bipedmove.h 37-40, 62-65), biped->X; the window wrappers (5121, 5132), whose calls stay bare, as the window
   never goes live under ROOT; the HALO_GAME_BROWSER block (5091-5096), never defined on the PSP; &animation, M's
   record's, passed as a pointer (5149); and the eight calls take M's names and parameters: biped_update_dead,
   biped_update_airborne, biped_update_landing, biped_update_slipping, biped_try_to_make_footsteps,
   biped_falling_danger, biped_check_discard and biped_jump are store_motion_dead, _airborne, _landed, _slipping,
   _footsteps, _falling_danger, _discard and _jump. store_motion_from calls it */
static void store_motion_tail(long biped_index, struct biped_datum *biped, struct biped_definition *definition,
	struct unit_animation_update_data *animation, int moved)
{
	struct tick2_unit_ctl *control = tick2_unit_ctl_of(biped->object.root);
	struct tick2_unit_bip2 *line2 = tick2_unit_bip2_of(biped->object.root);

	if (moved)
	{
		biped_verify_object_vectors(biped_index, "post-moving");

		if (TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
		{
			store_motion_dead(biped_index, biped, definition, animation);
		}
		else if (TEST_FLAG(biped->biped.flags, _biped_airborne_bit))
		{
			store_motion_airborne(biped_index, biped, definition, animation);
		}
		else if (line2->landing!=NONE)
		{
			store_motion_landed(biped_index, biped, animation);
		}
		else if (TEST_FLAG(biped->biped.flags, _biped_slipping_bit))
		{
			store_motion_slipping(biped_index, biped, animation);
		}

		biped_verify_object_vectors(biped_index, "post-dead/air/land/slip");

		if (!line2->player_melee_ticks)
		{
			if (biped->unit.player_index!=NONE &&
				TEST_FLAG(control->control_flags, _unit_control_use_equipment_bit))
			{
				long weapon_index = unit_inventory_get_weapon(
					biped_index,
					unit_get(biped_index)->unit.current_weapon_index);

				/* with no weapon, weapon_prevents_melee_attack(NONE) is TRUE:
				an unarmed player cannot melee, as in the retail game */
				if (!weapon_prevents_melee_attack(weapon_index) &&
					control->current_zoom_level==NONE)
				{
					short melee_speedup_ticks;

					unit_animation_start_action(biped_index, _unit_animation_action_melee);
					weapon_stop_reload(weapon_index);
					first_person_weapon_message_from_unit(
						biped_index,
						_first_person_weapon_message_melee);
					line2->player_melee_ticks = weapon_get_first_person_animation_time(
						weapon_index,
						_weapon_first_person_animation_time_frame_count,
						_first_person_weapon_animation_melee,
						NONE);
					line2->player_melee_attack_tick = line2->player_melee_ticks -
						weapon_get_first_person_animation_time(
							weapon_index,
							_weapon_first_person_animation_time_private_key_frame,
							_first_person_weapon_animation_melee,
							NONE);

					melee_speedup_ticks = line2->player_melee_ticks >> 2;
					line2->player_melee_ticks -= melee_speedup_ticks;
					line2->player_melee_attack_tick -= melee_speedup_ticks;
				}
				/* port: Stubbs mode's melee, his hands (port/linux/game/stubbs_player.c) */
				else if (stubbs_mode_active())
				{
					short ticks, attack_tick;

					if (stubbs_player_melee_begin(biped_index, &ticks, &attack_tick))
					{
						line2->player_melee_ticks = (char)ticks;
						line2->player_melee_attack_tick = (char)attack_tick;
					}
				}
			}
		}
		else
		{
			if (line2->player_melee_ticks==line2->player_melee_attack_tick)
				unit_cause_player_melee_damage(biped_index);
			--line2->player_melee_ticks;
		}

		store_motion_footsteps(biped_index, biped);
		store_motion_falling_danger(biped_index, biped, definition);
		store_motion_discard(biped_index, biped);
	}

	if (unit_update_animation(biped_index, animation)==1)
		store_motion_jump(biped_index);

	if (TEST_FLAG(biped->object.damage_flags, _object_dead_bit) &&
		TEST_FLAG(biped->object.flags, _object_at_rest_bit))
	{
		++biped->object.idle_ticks;
	}
	else
	{
		biped->object.idle_ticks = 0;
	}

	biped_verify_object_vectors(biped_index, "post-update");

	return;
}

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
		store_motion_call(&motion->physics, motion->facing, collision_flags, position, velocity);
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
		store_motion_stat[intent->stop == 1 ? 0 : 1]++; PSP_S1A_RESUME(biped_index, biped);
		store_motion_from(biped_index, biped, definition, &store_motions[intent - store_intents], intent->stop,
			intent->flags);
	PSP_S1A_CLOSE(PSP_S1_STAGE_M); }
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
		PSP_S1A_OPEN(biped_index, biped); stop = store_intent_body(biped_index, biped, definition, motion, &flags, &position, &velocity);
		if (stop == 0)
			store_motion_stat[6]++;
		store_motion_from(biped_index, biped, definition, motion, stop, flags);
	PSP_S1A_CLOSE(PSP_S1_STAGE_M); }
	store_motion_owner_put(outer);
	return TRUE;
}

#ifdef PSP_S1COUNT
/* s1count's (a) and (b) (the lab's briefs/s1_count.md, task s1c-a). Read only: every function here reads the game's
   state and writes only its own statics and s1count's sums (psp_s1_tag, psp_s1_add).

   (a) An intent tick whose inputs are bit-equal to last tick's. The inputs are what I's body (store_intent_body and
   what it calls, the head, turning, the physics' build) reads that can change from tick to tick, as a copy taken at
   the body's start, before it writes: the root's position, velocity, forward, up and angular velocity; CONTROL and
   line 1 whole; line 2's ground plane, bank, support surface, landing and the airborne and slipping counters; ANIM's
   state, base seat and flags; the object animation's graph and index, and its frame when its frame info is one of the
   three translations _moving reads (else the frame isn't read); the datum's damage flags, flags, parent, recent body
   damage, biped flags, actor, body stun, unit flags and player, the player's speed multiplier; for a rider, the
   parent's type, flags, damage flags and root motion (the vehicle's speech and ejection tests read them). Left out as
   constant in a map: the definitions and the game globals; as keep-exact (the brief's list): game time and the speech
   counters (gunner_inactive_ticks, last_falling_communication_time). The copy is taken every tick with ktstamp's keep
   word (psp_kt_count; the first copy is equal to nothing), off the ME, one a biped by its datum index, and compared
   with the one taken at the previous game time. On ktstamp's timed ticks, an equal intent's cycles are added: I's
   body and keep in store_intent_run (stage I); M's resume of it in store_motion_update, and the whole update of a
   biped with no intent there (stage M; M's cells hold the move and the tail, whose keep-exact parts the read can't
   take out: a ceiling). A biped whose I ran on the ME has no copy that tick, so its next tick is unequal.

   (b) A translate whose target is bit-equal to the position the object has, with its cluster set unchanged: the
   root's location and bounding sphere (centre, radius), which the reconnect's cluster walk is a function of, are
   bit-equal after the call to before it. Reached only at stop 1 (store_motion_moved), a suspended biped too; its
   cycles are object_translate's, stage M, on timed ticks */
struct psp_s1a_inputs
{
	real_point3d position;
	real_vector3d translational_velocity, forward, up, angular_velocity;
	struct tick2_unit_ctl control;
	struct tick2_unit_bip1 line1;
	real_plane3d ground_plane;
	real bank;
	long support_surface_index;
	long landing, airborne_ticks, slipping_ticks;
	long anim_state, anim_base_seat, anim_flags;
	long graph, animation, frame;
	long damage_flags, object_flags, parent, biped_flags, actor, unit_flags, player;
	real recent_body_damage, body_stun, speed_multiplier;
	long parent_type, parent_flags, parent_damage_flags;
	real_point3d parent_position;
	real_vector3d parent_velocity, parent_up, parent_angular_velocity;
};

#define PSP_S1A_SLOTS 256
static struct psp_s1a_slot
{
	long biped_index, time;
	int equal;
	struct psp_s1a_inputs inputs;
} psp_s1a_slots[PSP_S1A_SLOTS];
static struct psp_s1a_slot *psp_s1a_cur;
static int psp_s1a_tag; /* (a)'s biped, kept: psp_s1_tag is one global the other classes' adds set too */
static unsigned long psp_s1a_mark, psp_s1b_mark, psp_s1a_full;
static int psp_s1b_same;
static struct location psp_s1b_location;
static real_point3d psp_s1b_center;
static real psp_s1b_radius;

static void psp_s1a_take(struct biped_datum *biped, struct psp_s1a_inputs *in)
{
	struct tick2_root *root = biped->object.root;
	struct unit_animation *anim = tick2_unit_anim_of(root);
	struct tick2_unit_bip2 *line2 = tick2_unit_bip2_of(root);
	struct object_animation_state *object_anim = tick2_anim_of(root);

	memset(in, 0, sizeof(*in));
	in->position = root->position;
	in->translational_velocity = root->translational_velocity;
	in->forward = root->forward;
	in->up = root->up;
	in->angular_velocity = root->angular_velocity;
	in->control = *tick2_unit_ctl_of(root);
	in->line1 = *tick2_unit_bip1_of(root);
	in->ground_plane = line2->ground_plane;
	in->bank = line2->bank;
	in->support_surface_index = line2->support_surface_index;
	in->landing = line2->landing;
	in->airborne_ticks = line2->airborne_ticks;
	in->slipping_ticks = line2->slipping_ticks;
	in->anim_state = anim->state;
	in->anim_base_seat = anim->base_seat_index;
	in->anim_flags = anim->flags;
	in->graph = object_anim->animation_graph_index;
	in->animation = object_anim->state.index;
	in->frame = NONE;
	if (object_anim->state.index != NONE)
	{
		struct animation_graph *graph = animation_graph_definition_get(object_anim->animation_graph_index);
		struct animation *animation = TAG_BLOCK_GET_ELEMENT(&graph->animations, object_anim->state.index,
			struct animation);

		switch (animation->frame_info_type)
		{
		case _animation_frame_info_xy_translation:
		case _animation_frame_info_xy_translation_yaw_rotation:
		case _animation_frame_info_xyz_translation_yaw_rotation:
			in->frame = object_anim->state.frame_index;
			break;
		}
	}
	in->damage_flags = biped->object.damage_flags;
	in->object_flags = biped->object.flags;
	in->parent = biped->object.parent_object_index;
	in->recent_body_damage = biped->object.recent_body_damage;
	in->biped_flags = biped->biped.flags;
	in->actor = biped->unit.actor_index;
	in->unit_flags = biped->unit.flags;
	in->body_stun = biped->unit.body_stun;
	in->player = biped->unit.player_index;
	if (biped->unit.player_index != NONE)
		in->speed_multiplier = player_get(biped->unit.player_index)->speed_multiplier;
	if (biped->object.parent_object_index != NONE)
	{
		struct object_datum *parent = object_get(biped->object.parent_object_index);

		in->parent_type = parent->object.type;
		in->parent_flags = parent->object.flags;
		in->parent_damage_flags = parent->object.damage_flags;
		in->parent_position = parent->object.root->position;
		in->parent_velocity = parent->object.root->translational_velocity;
		in->parent_up = parent->object.root->up;
		in->parent_angular_velocity = parent->object.root->angular_velocity;
	}
}

/* the biped's slot: its own, else a free one or one not written at the last game time (linear probe of 8); none
   left, the biped goes uncompared (counted, logged at the first) */
static struct psp_s1a_slot *psp_s1a_slot(long biped_index, long time)
{
	unsigned long at = (unsigned long)biped_index * 2654435761ul;
	struct psp_s1a_slot *spare = NULL;
	int k;

	for (k = 0; k < 8; k++)
	{
		struct psp_s1a_slot *slot = &psp_s1a_slots[(at + k) % PSP_S1A_SLOTS];

		if (slot->biped_index == biped_index && slot->time)
			return slot;
		if (!spare && (!slot->time || slot->time < time - 1))
			spare = slot;
	}
	if (spare)
	{
		spare->biped_index = biped_index;
		spare->time = 0;
		return spare;
	}
	if (!psp_s1a_full++)
		platform_psp_log("s1count: (a)'s table full at gt %ld: a biped goes uncompared", time);
	return NULL;
}

/* I's body's start (store_intent_run), or the whole update's (store_motion_update): every tick, the copy and the
   compare; a second open in the same game time keeps the first's answer */
void psp_s1a_open(long biped_index, struct biped_datum *biped)
{
	long time = game_time_get() + 1; /* 0 marks a free slot */
	struct psp_s1a_slot *slot = psp_s1a_slot(biped_index, time);
	struct psp_s1a_inputs in;

	psp_s1a_cur = NULL;
	if (!slot)
		return;
	if (slot->time != time)
	{
		psp_s1a_take(biped, &in);
		slot->equal = slot->time == time - 1 && !memcmp(&in, &slot->inputs, sizeof(in));
		slot->inputs = in;
		slot->time = time;
	}
	if (psp_s1_on)
	{
		psp_s1a_tag = PSP_S1_WHO(biped);
		psp_s1_tag(psp_s1a_tag);
		psp_s1a_cur = slot;
		psp_s1a_mark = psp_kt_clock();
	}
}

/* M's resume of a kept intent (timed ticks): the answer I's open gave this game time */
void psp_s1a_resume(long biped_index, struct biped_datum *biped)
{
	long time = game_time_get() + 1;
	unsigned long at = (unsigned long)biped_index * 2654435761ul;
	int k;

	psp_s1a_cur = NULL;
	for (k = 0; k < 8; k++)
	{
		struct psp_s1a_slot *slot = &psp_s1a_slots[(at + k) % PSP_S1A_SLOTS];

		if (slot->biped_index == biped_index && slot->time == time)
		{
			psp_s1a_tag = PSP_S1_WHO(biped);
			psp_s1_tag(psp_s1a_tag);
			psp_s1a_cur = slot;
			psp_s1a_mark = psp_kt_clock();
			return;
		}
	}
}

void psp_s1a_close(int stage)
{
	if (psp_s1a_cur && psp_s1a_cur->equal)
	{
		unsigned long cycles = psp_kt_clock() - psp_s1a_mark;

		psp_s1_tag(psp_s1a_tag);
		psp_s1_add(PSP_S1_A, stage, cycles);
	}
	psp_s1a_cur = NULL;
}

void psp_s1b_open(struct biped_datum *biped, real_point3d const *target)
{
	struct tick2_root *root = biped->object.root;

	psp_s1b_same = !memcmp(target, &root->position, sizeof(*target));
	psp_s1b_location = root->location;
	psp_s1b_center = root->bounding_sphere_center;
	psp_s1b_radius = root->bounding_sphere_radius;
	psp_s1b_mark = psp_kt_clock();
}

void psp_s1b_close(struct biped_datum *biped)
{
	unsigned long cycles = psp_kt_clock() - psp_s1b_mark;
	struct tick2_root *root = biped->object.root;

	if (psp_s1b_same &&
		!memcmp(&psp_s1b_location, &root->location, sizeof(psp_s1b_location)) &&
		!memcmp(&psp_s1b_center, &root->bounding_sphere_center, sizeof(psp_s1b_center)) &&
		!memcmp(&psp_s1b_radius, &root->bounding_sphere_radius, sizeof(psp_s1b_radius)))
	{
		psp_s1_tag(PSP_S1_WHO(biped));
		psp_s1_add(PSP_S1_B, PSP_S1_STAGE_M, cycles);
	}
}
#endif
