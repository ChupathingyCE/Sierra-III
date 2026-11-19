/* Biped motion's means B (docs/box_biped_motion.md, "Means B"; the store's
   page, docs/box_world_store.md section 10). The fields only the window's
   own functions touch (tools/bmsb_map.py, from the lab's msbcensus1) live in
   a packed record per staged biped from the fill after the unit pass to the
   one write-back after the solves, and bipeds.c's window reads and writes
   them there by plain pointer (psp/include/psp_bmsb.h). No helper is synced:
   a field any of them touches isn't in the record.

   Keep word bmsb (PSP_BMSB=1 builds). Off, and for a biped with no staged
   record (dead at the fill, past the slots, or outside the staged passes),
   biped_update fills a scratch record at its entry and writes it back at its
   exit, so the window's code always has a record and plays the same.
   bmsbcheck: each live record's fields are also kept as the fill read them,
   and the write-back counts the fields something else wrote to the datum
   meanwhile (a write the record would overwrite). */
#include <stdlib.h>
#include <string.h>
#include "psp_bmsb.h"

void platform_psp_log(const char *format, ...);
int psp_keep(const char *name);

#define PSP_BMSB_SLOTS 64
#define PSP_BMSB_DEPTH 4

unsigned char *psp_bmsb_rec;
static unsigned char psp_bmsb_record[PSP_BMSB_SLOTS][PSP_BMSB_BYTES] __attribute__((aligned(64)));
static unsigned char psp_bmsb_scratch[PSP_BMSB_DEPTH][PSP_BMSB_BYTES] __attribute__((aligned(64)));
static void *psp_bmsb_datum[PSP_BMSB_SLOTS];
static long psp_bmsb_index[PSP_BMSB_SLOTS];
static unsigned char psp_bmsb_state[PSP_BMSB_SLOTS];  /* 0 none, 1 filled, 2 run (written back after the solves) */
static short psp_bmsb_slot_of[1024];
int psp_bmsb_current = -1;  /* the slot of the update running, -2 - depth for a scratch record, -1 none */
static int psp_bmsb_live, psp_bmsb_depth;
static void *psp_bmsb_scratch_datum[PSP_BMSB_DEPTH];
static unsigned char psp_bmsb_dropped[PSP_BMSB_DEPTH];
static int psp_bmsb_on = -1, psp_bmsb_check;
static unsigned char (*psp_bmsb_shadow)[PSP_BMSB_BYTES];
static unsigned long psp_bmsb_ticks, psp_bmsb_filled, psp_bmsb_entered, psp_bmsb_scratched, psp_bmsb_written,
	psp_bmsb_drops, psp_bmsb_gone, psp_bmsb_outside[PSP_BMSB_BACK_RUNS_COUNT + 1];

static void psp_bmsb_copy_in(unsigned char *record, unsigned char const *datum)
{
	int i;

	for (i = 0; i < PSP_BMSB_FILL_RUNS_COUNT; i++)
		memcpy(record + psp_bmsb_fill_runs[i][1], datum + psp_bmsb_fill_runs[i][0], psp_bmsb_fill_runs[i][2]);
}

static void psp_bmsb_copy_out(unsigned char const *record, unsigned char *datum)
{
	int i;

	for (i = 0; i < PSP_BMSB_BACK_RUNS_COUNT; i++)
		memcpy(datum + psp_bmsb_back_runs[i][0], record + psp_bmsb_back_runs[i][1], psp_bmsb_back_runs[i][2]);
}

/* objects.c, after the unit pass: is means B on this tick */
int psp_bmsb_begin(void)
{
	if (psp_bmsb_on < 0)
	{
		psp_bmsb_check = psp_keep("bmsbcheck");
		psp_bmsb_on = psp_bmsb_check || psp_keep("bmsb");
		if (psp_bmsb_check && !(psp_bmsb_shadow = malloc(sizeof(*psp_bmsb_shadow) * PSP_BMSB_SLOTS)))
			psp_bmsb_check = 0;
		if (psp_bmsb_on)
			platform_psp_log("bmsb: biped motion's means B on, a %d B record a biped%s", PSP_BMSB_BYTES,
				psp_bmsb_check ? ", outside writes counted (bmsbcheck)" : "");
	}
	psp_bmsb_live = psp_bmsb_on;
	return psp_bmsb_on;
}

/* objects.c, after the unit pass: one sequential pass over the living staged
   bipeds, each one's record filled in its slot */
void psp_bmsb_fill(int slot, void *datum, long index)
{
	if (!psp_bmsb_live || slot < 0 || slot >= PSP_BMSB_SLOTS)
		return;
	psp_bmsb_copy_in(psp_bmsb_record[slot], (unsigned char const *)datum);
	if (psp_bmsb_check)
		psp_bmsb_copy_in(psp_bmsb_shadow[slot], (unsigned char const *)datum);
	psp_bmsb_datum[slot] = datum;
	psp_bmsb_index[slot] = index;
	psp_bmsb_state[slot] = 1;
	psp_bmsb_slot_of[index & 1023] = (short)slot;
	psp_bmsb_filled++;
}

/* objects.c, after the solves: one pass writing back each record its update
   ran on (the datum given only while the biped is still there) */
void psp_bmsb_write_back(int slot, void *datum)
{
	if (slot < 0 || slot >= PSP_BMSB_SLOTS || !psp_bmsb_state[slot])
		return;
	if (psp_bmsb_state[slot] == 2 && datum && datum == psp_bmsb_datum[slot])
	{
		if (psp_bmsb_check)
		{
			int i;

			for (i = 0; i < PSP_BMSB_FILL_RUNS_COUNT; i++)
			{
				unsigned short const *run = psp_bmsb_fill_runs[i];

				if (memcmp(psp_bmsb_shadow[slot] + run[1], (unsigned char const *)datum + run[0], run[2]))
					psp_bmsb_outside[i < PSP_BMSB_BACK_RUNS_COUNT ? i : PSP_BMSB_BACK_RUNS_COUNT]++;
			}
		}
		psp_bmsb_copy_out(psp_bmsb_record[slot], (unsigned char *)datum);
		psp_bmsb_written++;
	}
	else if (psp_bmsb_state[slot] == 2)
		psp_bmsb_gone++;
	psp_bmsb_state[slot] = 0;
}

void psp_bmsb_end(void)
{
	psp_bmsb_live = 0;
	if (psp_bmsb_on > 0 && ++psp_bmsb_ticks % 3000 == 0)
	{
		platform_psp_log("bmsb: %lu ticks: records filled %lu, updates on a record %lu, on a scratch record %lu, written back %lu, gone before the write-back %lu, dropped by a delete %lu",
			psp_bmsb_ticks, psp_bmsb_filled, psp_bmsb_entered, psp_bmsb_scratched, psp_bmsb_written, psp_bmsb_gone, psp_bmsb_drops);
		if (psp_bmsb_check)
		{
			int i;

			for (i = 0; i <= PSP_BMSB_BACK_RUNS_COUNT && i < PSP_BMSB_FILL_RUNS_COUNT; i++)
				platform_psp_log("bmsbcheck: fill run %d (+%u, %u B): written outside the window %lu times",
					i, psp_bmsb_fill_runs[i][0], psp_bmsb_fill_runs[i][2], psp_bmsb_outside[i]);
		}
	}
}

/* biped_update's entry: the record of this biped's staged slot while the
   staged passes run, else a scratch record filled from the datum. The token
   says which, for psp_bmsb_leave */
int psp_bmsb_enter(long biped_index, void *datum)
{
	int slot = psp_bmsb_live ? psp_bmsb_slot_of[biped_index & 1023] : -1;

	if (slot >= 0 && psp_bmsb_state[slot] && psp_bmsb_index[slot] == biped_index && psp_bmsb_datum[slot] == datum)
	{
		psp_bmsb_rec = psp_bmsb_record[slot];
		psp_bmsb_state[slot] = 2;
		psp_bmsb_current = slot;
		psp_bmsb_entered++;
		return slot;
	}
	if (psp_bmsb_depth >= PSP_BMSB_DEPTH)
	{
		platform_psp_log("bmsb: biped_update nested past %d scratch records", PSP_BMSB_DEPTH);
		psp_bmsb_depth = PSP_BMSB_DEPTH - 1;
	}
	psp_bmsb_copy_in(psp_bmsb_scratch[psp_bmsb_depth], (unsigned char const *)datum);
	psp_bmsb_dropped[psp_bmsb_depth] = 0;
	psp_bmsb_scratch_datum[psp_bmsb_depth] = datum;
	psp_bmsb_rec = psp_bmsb_scratch[psp_bmsb_depth];
	psp_bmsb_current = -2 - psp_bmsb_depth;
	psp_bmsb_scratched++;
	return -2 - psp_bmsb_depth++;
}

/* biped_update's exit: a scratch record written back (the datum given only
   while the biped is still there); the caller puts back psp_bmsb_rec and
   psp_bmsb_current as they were at its entry */
void psp_bmsb_leave(int token, void *datum)
{
	if (token <= -2)
	{
		int depth = -2 - token;

		if (datum && !psp_bmsb_dropped[depth])
			psp_bmsb_copy_out(psp_bmsb_scratch[depth], (unsigned char *)datum);
		psp_bmsb_depth = depth;
	}
}

/* the running biped's delete: its record written back now, and nothing of
   it after (the delete ends the window, as the engine's update returns) */
void psp_bmsb_drop(void)
{
	int k = psp_bmsb_current;

	psp_bmsb_drops++;
	if (k >= 0)
	{
		psp_bmsb_copy_out(psp_bmsb_record[k], (unsigned char *)psp_bmsb_datum[k]);
		psp_bmsb_state[k] = 0;
	}
	else if (k <= -2 && !psp_bmsb_dropped[-2 - k])
	{
		psp_bmsb_copy_out(psp_bmsb_scratch[-2 - k], (unsigned char *)psp_bmsb_scratch_datum[-2 - k]);
		psp_bmsb_dropped[-2 - k] = 1;
	}
}
