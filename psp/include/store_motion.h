/* store_motion.h: the bipeds' motion as the store's passes (slice 1's task 5; docs/box_world_store.md section 19,
   step 3). The INTENT table the stages hand on within a tick: I writes it, G and M read it, and nothing reads it
   across ticks. Built with PSP_TICK2_MOTIONNEW=1; the engine's real math types (cseries.h and math/real_math.h
   first). */
#pragma once

#define STORE_INTENT_ENTRIES 64  /* old-only's intent pool (bipeds.c PSP_BS3_RECORDS): clause 10's branch (i) */
#define STORE_INTENT_STAGED 256  /* the staged list's cap (objects.c PSP_BIPEDSTAGE_MAXIMUM) */
#define STORE_INTENT_NONE 0xff   /* the k-to-n map: no entry for this staged position */

/* one living biped's intent, as G reads it: the move's call as I leaves it, with the gather's sphere made from it */
struct store_intent
{
	long biped_index;
	char stop;               /* 1, at the move's call; 2, the update stopped short of a move */
	char pad;
	short me;                /* its ME record (psp_sweepme_add's index), or -1 */
	unsigned long flags;     /* the move's collision test flags; 0 on stop 2 */
	real height, width;      /* the pill */
	real_point3d center;     /* the gather's sphere */
	real radius;
};

/* bipeds.c's own enums, macros and structures, from the biped states to the vehicle's datum, with struct biped_physics
   (the move's record, which I builds and M reads after the call) among them: held here so store_motion.c can hold the
   record in M's table and its code, carried from bipeds.c, has the names that code uses (the states, the move's in and
   out bits, the frame infos, the vehicle's datum: no header declares them, each engine unit has its own). Only bipeds.c
   and store_motion.c include this header. psp_fixups.py's tick2_motionnew_physics_cut checks bipeds.c's text is this,
   character for character, and puts this header's include in its place; edit it only by copying the tree's. */
/* ---- moved from bipeds.c (its enums, macros and structures, struct biped_physics among them): begin ---- */
enum
{
	biped_state_idle = 0,
	biped_state_moving,
	biped_state_unknown,
	NUMBER_OF_BIPED_STATES,
};

enum
{
	_biped_physics_in_airborne_bit = 0,
	_biped_physics_in_slipping_bit,
	_biped_physics_in_crouched_bit,
	_biped_physics_in_trying_to_stand_bit,
	_biped_physics_in_flying_bit,
	_biped_physics_in_absolute_movement_bit,
	_biped_physics_in_no_collision_bit,
	_biped_physics_in_dead_bit,
	_biped_physics_in_pass_through_bipeds_bit,
	_biped_physics_in_climb_anything_bit,
	/* port: a player's, in a co-op game without player collisions: the other
	players' bipeds are not in its way (network_coop_player_collisions) */
	_biped_physics_in_pass_through_players_bit,
};

enum
{
	_biped_physics_out_airborne_bit = 0,
	_biped_physics_out_slipping_bit,
	_biped_physics_out_cannot_stand_bit,
	_biped_physics_out_splatter_bit,
	_biped_physics_out_volatile_collision_bit,
};

enum
{
	_unit_melee_attack_none = 0,
	_unit_melee_attack_starting,
	_unit_melee_attack_dangerous,
	_unit_melee_attack_impact,
	_unit_melee_attack_continuous,
};

enum
{
	_animation_frame_info_none = 0,
	_animation_frame_info_xy_translation,
	_animation_frame_info_xy_translation_yaw_rotation,
	_animation_frame_info_xyz_translation_yaw_rotation,
	NUMBER_OF_ANIMATION_FRAME_INFO_TYPES,
};

enum
{
	_collision_surface_two_sided_bit = 0,
	_collision_surface_invisible_bit,
	_collision_surface_climbable_bit,
	_collision_surface_breakable_bit,
};

/* ---------- macros */

#define BIPED_CLIMBING_SNAP_ANGLE ((real)(10.0*M_PI/180.0))
#define MINIMUM_SLIPPING_FOOTSTEP_VELOCITY_SQUARED (1.f/900.f)

/* ---------- structures */

struct biped_contact_point
{
	byte unused[32];
	char marker_name[32];
};

struct game_globals_falling_damage
{
	long falling_unused[2];
	real falling_distance_lower_bound;
	real falling_distance_upper_bound;
	struct tag_reference falling_damage;
	long terminal_velocity_unused[2];
	real maximum_distance;
	struct tag_reference maximum_distance_damage;
	struct tag_reference vehicle_hit_environment_damage_effect;
	struct tag_reference vehicle_killed_unit_damage_effect;
	struct tag_reference vehicle_collision_damage;
	struct tag_reference flaming_death_damage;
	long unused7c[4];
	real runtime_maximum_falling_velocity;
	real runtime_minimum_damage_velocity;
	real runtime_maximum_damage_velocity;
};

struct biped_physics
{
	long biped_index;
	word in_flags;
	word pad6;
	real_point3d position;
	real_vector3d forward;
	real_vector3d aiming;
	real_vector3d velocity;
	real crouch_velocity;
	real_vector3d movement_desired;
	real movement_penalty;
	real acceleration_maximum;
	real airborne_acceleration_maximum;
	real height;
	real width;
	real ground_tangential_velocity_max;
	real ground_tangential_angle;
	real minimum_normal_k;
	real downhill_k0;
	real downhill_k1;
	real downhill_velocity_scale;
	real uphill_k0;
	real uphill_k1;
	real uphill_velocity_scale;
	real_plane3d ground_plane;
	long existing_support_surface_index;
	real gravity;
	long bumped_object_index;
	long elevator_object_index;
	word out_flags;
	word padA2;
	long support_surface_index;
	long stick_surface_index;
	real_point3d new_position;
	real_vector3d new_velocity;
	real landing_velocity;
	real collision_velocity;
};

struct animation_frame_info_dx_dy
{
	real dx;
	real dy;
};

struct animation_frame_info_dx_dy_dyaw
{
	real dx;
	real dy;
	real dyaw;
};

struct animation_frame_info_dx_dy_dz_dyaw
{
	real dx;
	real dy;
	real dz;
	real dyaw;
};

struct vehicle_runtime_datum
{
	long definition_index;
	struct _object_datum object;
	struct _unit_datum unit;
	struct
	{
		word flags;
		short reserved;
		byte airborne_ticks;
	} vehicle;
};
/* ---- moved from bipeds.c: its enums, macros and structures: finish ---- */

/* M's own entry for intent n (store_motion.c): what M reads after the call. NULL past the pool */
struct store_motion;
struct store_motion *store_motion_at(short n);

/* I's head: the table emptied, every staged position mapped to no entry */
void store_intent_begin(void);
/* the next entry's place n for this biped's intent, or -1 over the pool (counted; nothing written) */
short store_intent_open(long biped_index);
/* I's last act for staged biped k: its entry n kept, the sphere made, the ME given its half, k mapped to n */
void store_intent_keep(short k, short n, int stop, unsigned long flags, real_point3d const *position,
	real_vector3d const *velocity, real height, real width);
/* I for staged biped k: biped_update to its move's call (stop 1) or, for a rider, to its tail (stop 2), kept as entry n
   with the animation and the physics record in M's */
void store_intent_run(short k, long biped_index);
/* staged biped k's entry, or NULL (dead, no intent kept, or over the pool) */
struct store_intent const *store_intent_of(short k);
/* after M: the tick's counts, and the log lines (I's and M's) */
void store_intent_end(void);

/* M: the biped's datum_update (object_types.c 310's, with the delete cut), biped_update's signature, TRUE always. It
   resumes the intent at staged position store_motion_cur when that intent is this biped's, and runs the whole update
   otherwise (counted by kind) */
boolean store_motion_update(long biped_index);
/* the pass's staged position for the biped it is updating: set before each staged biped's update, -1 after and
   outside the pass */
extern short store_motion_cur;

/* bipeds.c's helpers that I and M call and that stay (each has callers past biped_update): their static goes by
   psp_fixups.py's tick2_motionnew_helpers_cut, so store_motion.c reaches them; LTO (LTO_LIKE_UNITS) still inlines
   and clones them as it did */
struct biped_datum;
struct biped_definition;
void biped_snap_facing_with(long biped_index, struct biped_datum *biped, struct biped_definition *definition);
void biped_verify_object_vectors(long biped_index, char const *state);
long biped_find_ground_surface(long biped_index, real_vector3d const *direction, real distance, real_point3d *point,
	real_vector3d *normal);

/* G (store_coll.c): staged biped k's gather from its entry. TRUE handled (gathered, or no gather wanted), FALSE no
   entry */
int store_coll_gather_intent(short k);
