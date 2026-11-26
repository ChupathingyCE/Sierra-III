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

/* G (store_coll.c): staged biped k's gather from its entry. TRUE handled (gathered, or no gather wanted), FALSE no
   entry */
int store_coll_gather_intent(short k);
