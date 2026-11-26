/*
store_coll.h: the world store's collision gather table (slice 1's task 4)

docs/box_world_store.md section 18, 'The code plan', piece 1 (the lead 2026-11-25 13:3x): bipedstage2's gather table
and its code are the store's, in psp/src/store_coll.c: moved out of collisions.c (psp_fixups.py
tick2_collnew_move_cut, PSP_TICK2_COLLNEW=1), then written new over the store's own table (section 18, 'The gather
and the take rewritten'; the moved copy is psp/control/store_coll_moved.txt, the cut's control). collisions.c (where the sweep reuse's bounds were), store_coll.c and
(piece 3) collision_features.c include this. The includer has the engine's math and collision headers in hand first.

The table: one entry a gathered biped, dense in gather order (the run's order, the walk's), a biped-to-position map
beside it, and the sweep reuse's reach guard's fields on positions 0-31 (its slot mask stays collisions.c's). A slot
holds the world as the gather phase saw it until its move takes it; nothing refreshes it (section 18, 13:2x).
*/
#ifndef STORE_COLL_H
#define STORE_COLL_H

/* ---- moved from collisions.c (SWEEPREUSE's bounds and near entry): begin ---- */
#define PSP_SR_CLUSTERS 8 /* gathers walking more clusters aren't kept */
#define PSP_SR_NEAR 8 /* gathers meeting more near objects can't be kept */
/* an object's root position, forward and up as two hashes (12 B, not 40: tick2sr3's c20 ran out of heap with the poses kept whole) */
struct psp_sr_near { long index; unsigned long h1, h2; };
/* ---- moved from collisions.c: the sweep reuse's bounds: finish ---- */

/* the table's entries, psp/src/store_coll.c's */
int store_coll_begin(void);                       /* the tick's table emptied: nonzero when bipedstage2 is on */
#ifndef PSP_TICK2_MOTIONNEW
int store_coll_gather(long biped_index);          /* the exact gather, from the intent's kept call: 0, no record */
#endif
void store_coll_gather_margin(long biped_index);  /* the margin gather, where the intent left no record */
int store_coll_take(unsigned long flags, real_point3d const *center, real radius,
	real height, real width, long biped_index, struct collision_feature_list *list); /* 1 list, 2 empty, 0 gather live */
void store_coll_end(void);                        /* the tick's end, its log line each 1000 ticks */
/* the table itself (entries, count, arena, map) is store_coll.c's statics: nothing outside it names them */

/* piece 3: the biped physics' two collision entries (the engine's physics calls them until task 5, with its own
arguments; task 5's stage passes the slot). The move is collision_move_pill as the cuts before left it, with the
table's take; the stand test is the engine's gather on level 0's list and its vector, in the same order */
short store_coll_move(unsigned long flags, real_point3d const *position, real_vector3d const *velocity,
	real height, real radius, long ignore_object_index, real_point3d *clipped_position,
	real_vector3d *clipped_velocity, short maximum_collision_count, struct collision_plane *collisions);
boolean store_coll_stand(unsigned long flags, real_point3d const *center, real radius, real width,
	long biped_index, real_point3d const *position, real standing_height);

/* the view of a taken list: the move's list read where it lies in the arena (collision_features_test_vector reads
through it for that list only); the variable is store_coll.c's, its type the tree's (psp_fixups.py checks it) */
/* ---- moved from collisions.c (FIXES_BIPEDSTAGE's view of a taken list): begin ---- */
struct psp_bs2_view
{
	void const *list;
	struct collision_sphere const *spheres;
	struct collision_cylinder const *cylinders;
	struct collision_prism const *prisms;
};
extern struct psp_bs2_view psp_bs2_view;
/* ---- moved from collisions.c: the view: finish ---- */

/* the sweep reuse's names the gather and the take read and write, collisions.c's (the cut drops their static) */
extern int psp_sr_on_pill, psp_sr_met;
extern struct psp_sr_near psp_sr_near_list[PSP_SR_NEAR];
extern short psp_sr_near_count;
extern real_point3d psp_sr_center;
extern real psp_sr_radius;
extern short psp_sr_cluster_list[PSP_SR_CLUSTERS], psp_sr_cluster_count;
extern unsigned long psp_sr_gather_seq;
extern unsigned char psp_sr_slot_dead[32];
extern int psp_sr_reach_inherit;
void psp_sr_slot_watch_set(int slot, short const *clusters, short count, long biped_index, real_point3d const *center, real radius);
/* the stamps' order and settle, objects.c's (TICK2 stamps) */
extern unsigned long tick2_cs_seq;
void tick2_cs_settle(void);

#endif
