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
#include "store_motion.h"

static struct store_intent store_intents[STORE_INTENT_ENTRIES];
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
