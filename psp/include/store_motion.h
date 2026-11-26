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

/* The move's record, which I builds and M reads after the call: bipeds.c's own text, held here so store_motion.c can
   hold it in M's table. psp_fixups.py's tick2_motionnew_physics_cut checks bipeds.c's text is this, character for
   character, and puts this header's include in its place; edit it only by copying the tree's. */
/* ---- moved from bipeds.c (struct biped_physics, the move's record): begin ---- */
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
/* ---- moved from bipeds.c: struct biped_physics: finish ---- */

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
/* staged biped k's entry, or NULL (dead, no intent kept, or over the pool) */
struct store_intent const *store_intent_of(short k);
/* after M: the tick's counts, and the log line */
void store_intent_end(void);

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
