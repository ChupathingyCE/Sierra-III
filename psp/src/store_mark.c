/*
store_mark.c: the store's biped pass in four marked parts, for the cache model's split (PSP_SPLITMARK=1 only)

An instrument build, never a ship form (the lab's split6 ask; the lead 2026-11-26 15:0x: the marker build of
store_biped_pass's body). The emulator's cache split counts a listed entry with all it calls, so the pass's own inlined
code (its three loops, store_biped_at, store_intent_run's frame, store_coll_pack, store_biped_update's
psp_object_update) has no row: it lands in the main caches with the code outside the pass. Here the pass is the same
calls in the same order, cut into four noinline functions that a split lists by name, alike on both forms:

  store_mark_intent  the unit parts and I (new: store_biped_units, the intent loop and the ME's opening and close;
                     slice 1: store_biped_intent_until_task5, the same unit parts and bipedstage3's intent)
  store_mark_gather  G: the table's begin, the gather loop, and the ME's end (psp_sweepme_end), on both
  store_mark_solve   M: new's motion loop and store_biped_settle; slice 1's store_biped_solve_until_task5 (its settle inside)
  store_mark_ends    the tick's ends: store_coll_end (and new's store_intent_end)

store_coll.c's own pass is built as store_biped_pass_unmarked under the switch (psp_build.py's -D), so its text and its
line numbers, and every other store_coll.c function's profile, stay as they are; this unit has no profile. Built as
store_coll.c is (collisions.c's flags, LTO with it), only with PSP_TICK2_COLLNEW=1 and PSP_SPLITMARK=1. Each part is
noinline and noclone (a clone's dotted name can't be listed) and externally visible (its ELF name is its C name).
*/

#include <string.h>
#include "cseries.h"
#include "math/real_math.h"
#include "physics/collisions.h"
#include "physics/collision_features.h"
#include "objects/objects.h"
#include "units/bipeds.h"
#include "psp_sweepme.h"
#include "store_coll.h"

#ifndef PSP_SPLITMARK
#error "store_mark.c is built only with PSP_SPLITMARK=1 (psp_build.py)"
#endif

#define STORE_MARK __attribute__((noinline, noclone, used, externally_visible))

void store_biped_pass(void);

#ifdef PSP_TICK2_MOTIONNEW
#include "store_motion.h"
int store_biped_units(void); /* objects.c (TICK2 motionnew): the staged list's four */
long store_biped_at(short k, int *dead);
void store_biped_update(short k, int phase);
void store_biped_settle(void);
void psp_sweepme_tick_begin(void); /* collisions.c (FIXES_SWEEPME) */
int store_coll_gather_intent(short k); /* store_coll.c */

int store_mark_intent(void);
void store_mark_gather(int count);
void store_mark_solve(int count);
void store_mark_ends(void);

STORE_MARK int store_mark_intent(void)
{
	long biped_index;
	int count, dead;
	short k;

	count = store_biped_units();
	if (!count)
		return 0;
	store_intent_begin();
	psp_sweepme_tick_begin(); /* (FIXES_SWEEPME) */
	for (k = 0; k < count; k++)
	{
		biped_index = store_biped_at(k, &dead);
		if (biped_index != NONE && !dead)
			store_intent_run(k, biped_index);
	}
	psp_sweepme_close(); /* (FIXES_SWEEPME) */
	return count;
}

STORE_MARK void store_mark_gather(int count)
{
	long biped_index;
	int dead;
	short k;

	if (store_coll_begin())
	{
		for (k = 0; k < count; k++)
		{
			biped_index = store_biped_at(k, &dead);
			if (biped_index != NONE && !dead)
				store_coll_gather_intent(k);
		}
	}
	psp_sweepme_end(); /* (FIXES_SWEEPME) */
}

STORE_MARK void store_mark_solve(int count)
{
	long biped_index;
	int dead;
	short k;

	for (k = 0; k < count; k++)
	{
		biped_index = store_biped_at(k, &dead);
		if (biped_index == NONE)
			continue;
		store_motion_cur = k;
		store_biped_update(k, dead && !store_intent_of(k) ? 0 : 4);
		store_motion_cur = -1;
	}
	store_biped_settle();
}

STORE_MARK void store_mark_ends(void)
{
	store_coll_end();
	store_intent_end();
}

/* store_coll.c's pass (MOTIONNEW side), its calls in its order */
void store_biped_pass(void)
{
	int count = store_mark_intent();

	if (!count)
		return;
	store_mark_gather(count);
	store_mark_solve(count);
	store_mark_ends();
}
#else
int store_biped_intent_until_task5(int *intents); /* objects.c (TICK2 collnew), until task 5 */
long store_biped_alive_until_task5(short k);
void store_biped_solve_until_task5(void);

int store_mark_intent(int *intents);
void store_mark_gather(int count, int intents);
void store_mark_solve(void);
void store_mark_ends(void);

STORE_MARK int store_mark_intent(int *intents)
{
	return store_biped_intent_until_task5(intents);
}

STORE_MARK void store_mark_gather(int count, int intents)
{
	long biped_index;
	short k;

	if (store_coll_begin())
	{
		for (k = 0; k < count; k++)
		{
			biped_index = store_biped_alive_until_task5(k);
			if (biped_index != NONE && !(intents && store_coll_gather(biped_index)))
				store_coll_gather_margin(biped_index);
		}
	}
	psp_sweepme_end(); /* (FIXES_SWEEPME) */
}

STORE_MARK void store_mark_solve(void)
{
	store_biped_solve_until_task5();
}

STORE_MARK void store_mark_ends(void)
{
	store_coll_end();
}

/* store_coll.c's pass (the #else side), its calls in its order */
void store_biped_pass(void)
{
	int intents = 0, count;

	count = store_mark_intent(&intents);
	if (!count)
		return;
	store_mark_gather(count, intents);
	store_mark_solve();
	store_mark_ends();
}
#endif
