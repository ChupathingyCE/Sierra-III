/* The world store's biped section, step 1a (docs/box_biped_motion.md §1c;
   docs/box_world_store.md §2, §7). Built with PSP_BIPEDSTAGE=1.

   A store record a biped: the five owner sections of box_world_store.h laid
   end to end (read, unit, posing, walk, control), filled from the datum by
   the header's copy runs. Nothing reads it yet. Under keep word wsbipedcheck
   the staged run fills each living biped's record after the unit pass and
   compares it with the datum after the solves: a field the biped passes
   changed is one they write, so the owner map is checked on the staged path
   before the intent reads the store (step 1b). The bipeds that died or
   entered or left a vehicle in the tick are counted apart. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "box_world_store.h"
#include "box_world_store_biped.h"
#include "psp_bipedmove_map.h"

/* biped motion's census (PSP_BMS_CENSUS, recon only): the window's copies
   between the record and the datum under a label of their own, so the
   emulator's per-step field counts keep them apart from the code's own
   accesses */
#if PSP_BMS_CENSUS
const char *psp_step_label(const char *name);
static void box_ws_copy_unlabel(const char **old) { psp_step_label(*old); }
#define BMS_COPY_LABEL const char *bms_copy_old __attribute__((cleanup(box_ws_copy_unlabel))) = psp_step_label("bms copy")
#else
#define BMS_COPY_LABEL do { } while (0)
#endif
void platform_psp_log(const char *format, ...);

#define BOX_WS_OWNERS 5
#define BOX_WS_GEN_RUNS (sizeof(box_ws_biped_runs) / sizeof(box_ws_biped_runs[0]))
#define BOX_WS_UNIT2_RUNS (sizeof(box_ws_biped_unit2_runs) / sizeof(box_ws_biped_unit2_runs[0]))
#define BOX_WS_READ2_RUNS (sizeof(box_ws_biped_read2_runs) / sizeof(box_ws_biped_read2_runs[0]))
#define BOX_WS_ANIM_RUNS (sizeof(box_ws_biped_anim_runs) / sizeof(box_ws_biped_anim_runs[0]))
/* the generated runs (less those inside a field held whole), then biped
   motion's additions (box_world_store_biped.h): at most this many */
#define BOX_WS_BIPED_RUNS (BOX_WS_GEN_RUNS + BOX_WS_UNIT2_RUNS + BOX_WS_READ2_RUNS + BOX_WS_ANIM_RUNS)
#define BOX_WS_BIPED_SLOTS 64
static int box_ws_move_map_init(void);

static const char *const box_ws_owner_name[BOX_WS_OWNERS] = { "read", "unit", "posing", "walk", "control" };
static const unsigned short box_ws_owner_size[BOX_WS_OWNERS] = {
	sizeof(struct box_ws_biped_read), sizeof(struct box_ws_biped_unit), sizeof(struct box_ws_biped_posing),
	sizeof(struct box_ws_biped_walk), sizeof(struct box_ws_biped_control) };
#define BOX_WS_GEN_BYTES (sizeof(struct box_ws_biped_read) + sizeof(struct box_ws_biped_unit) + \
	sizeof(struct box_ws_biped_posing) + sizeof(struct box_ws_biped_walk) + sizeof(struct box_ws_biped_control))
#define BOX_WS_BIPED_BYTES (BOX_WS_GEN_BYTES + sizeof(struct box_ws_biped_unit2) + sizeof(struct box_ws_biped_read2) + \
	sizeof(struct box_ws_biped_anim))

static unsigned short box_ws_owner_base[BOX_WS_OWNERS];
static unsigned char box_ws_run_owner[BOX_WS_BIPED_RUNS];
/* every run, generated and added, with its offset in the record */
static struct box_ws_run box_ws_all_runs[BOX_WS_BIPED_RUNS];
static unsigned short box_ws_run_store[BOX_WS_BIPED_RUNS];
/* the runs in use, and which are written back at the window's end */
static unsigned box_ws_nruns;
static unsigned char box_ws_run_back[BOX_WS_BIPED_RUNS];
/* bipedmovestore keeps each staged biped's record in these slots from its
   intent to its tail, so the store's own checks (wsbipedcheck, wsbipedcross)
   stand aside while it runs */
static int box_ws_move_claimed;
/* -1 not yet, 0 the runs don't match the sections (logged; the check stays off), 1 ready */
static int box_ws_ready = -1;
static unsigned char box_ws_biped[BOX_WS_BIPED_SLOTS][BOX_WS_BIPED_BYTES] __attribute__((aligned(64)));
/* The split of the window's cost (the lab's wssplit1; recon only): under
   keep word bmsghost (with bipedmovestore) every fill, keep, resume, sync
   and write-back runs as in the window, but the accessor is never pointed
   at the record, so the window's own reads and writes stay on the datum.
   Each copy of the record to the datum is refilled from the datum first, so
   it writes back what's there and play stays the same. bmsghost1: the same
   with every slot's record in slot 0's lines, so the kept set doesn't take
   cache room. ghost - off is the fills and write-backs with the kept set;
   ghost - ghost1 the kept set's residency; window - ghost the accessor
   live (the record's reads against the datum lines it saves). */
static int box_ws_ghost, box_ws_ghost1;
static unsigned char box_ws_ghost_datum;  /* the accessor's datum while a ghost window is open: matches no datum */
#define BOX_WS_REC(slot) box_ws_biped[box_ws_ghost1 ? 0 : (slot)]
/* per run: the times it was compared and the times it differed, in the
   steady case; and per class of the biped's tick (steady, died, its parent
   changed: a vehicle entered or left), the record's run copies compared and
   changed in unit and outside it: the moves between states are where a slot
   goes stale (the lead, 2026-11-18 10:0x) */
static unsigned long box_ws_compared[BOX_WS_BIPED_RUNS], box_ws_changed[BOX_WS_BIPED_RUNS];
#define BOX_WS_CLASSES 3
static const char *const box_ws_class_name[BOX_WS_CLASSES] = { "steady", "died", "parent changed" };
static unsigned long box_ws_class_records[BOX_WS_CLASSES], box_ws_class_unit[BOX_WS_CLASSES], box_ws_class_other[BOX_WS_CLASSES];
static unsigned long box_ws_ticks;
/* the check across ticks (keep word wsbipedcross, 1c's design): each record
   keeps its biped's datum index and the staged tick it was filled in; before
   the next fill, last tick's record of the same biped is compared with its
   datum, so a field written to it between its staged passes (damage,
   scripts, the vehicle path) is counted, by owner and run */
static long box_ws_slot_index[BOX_WS_BIPED_SLOTS];
static unsigned long box_ws_slot_tick[BOX_WS_BIPED_SLOTS];
static unsigned long box_ws_cross_records, box_ws_cross_compared[BOX_WS_BIPED_RUNS], box_ws_cross_changed[BOX_WS_BIPED_RUNS];
/* 1c's slot-dirty marks (psp_ws_dirty, below) */
#define BOX_WS_DIRTY 1024
static unsigned char box_ws_dirty[BOX_WS_DIRTY];
static unsigned long box_ws_cross_marked, box_ws_cross_unmarked_unit, box_ws_cross_unmarked_every;
static unsigned long box_ws_cross_unmarked_run[BOX_WS_BIPED_RUNS];
/* the unit runs the lab's wswatch2 found written by many paths outside the
   staged passes (animation, weapons, AI control and speech, map links): 39
   bytes a biped, and the two timers wsdirty4 left (+172, +948: 16 more),
   cheaper to refill every tick than to mark at each writer */
static const unsigned short box_ws_every_runs[] = { 4, 76, 128, 172, 592, 604, 920, 940, 948, 1116 };

static int box_ws_run_every(unsigned datum)
{
	unsigned k;

	for (k = 0; k < sizeof(box_ws_every_runs) / sizeof(box_ws_every_runs[0]); k++)
		if (box_ws_every_runs[k] == datum)
			return 1;
	return 0;
}

/* Biped motion's window (docs/box_biped_motion.md section 1c, 1b and 1c's
   intent-to-tail window): from a biped's intent to the end of its tail, the
   engine's code reads and writes the biped's record in the store in place of
   its datum. The engine's code is kept as it is; a build-time pass
   (psp_fixups.py, BIPEDMOVE) points each of its datum accesses in the
   window's functions at PSP_BMS, which asks box_ws_move_at for the field's
   place: the live record, else the datum. The map gives every datum byte's
   place in the record (-1 where the store doesn't hold it), so any field the
   store holds is redirected with no table of names, and one it doesn't hold
   stays on the datum.

   A record lives in the slot of its intent's bipedstage3 record. It's filled
   at the intent's start; where the intent stops (at the move's call, or at
   the tail when no move is reached) only the velocity is written through
   (other bipeds' solves read it), and the record waits, kept, until the
   biped's own solve resumes it; the written-back runs go to the datum at
   psp_bs3_out, before posing and the children read the root. An outside
   writer's mark (psp_ws_dirty) on a kept biped writes its record back first,
   and the resume then refills it. */
void *box_ws_move_datum;  /* the datum whose record is live, else NULL */
unsigned char *box_ws_move_live;  /* that record */
short box_ws_move_map[BOX_WS_DATUM_BYTES];
static unsigned char box_ws_move_back[BOX_WS_DATUM_BYTES];
/* per slot: 0 free, 1 live, 2 kept (intent done, solve not yet), 3 kept and
   already written back by an outside writer's mark (refilled at the resume) */
static unsigned char box_ws_move_state[BOX_WS_BIPED_SLOTS];
static void *box_ws_move_slot_datum[BOX_WS_BIPED_SLOTS];
static long box_ws_move_slot_index[BOX_WS_BIPED_SLOTS];
static int box_ws_move_slot = -1, box_ws_move_kept;
static unsigned long box_ws_move_count, box_ws_move_ended, box_ws_move_flushed, box_ws_move_settled, box_ws_move_dropped;
/* bipedmovestorecheck: per slot, the datum as it stood when the record last
   agreed with it (the fill, each sync and write-back); against it, at the
   resume, the datum's runs written between the passes, and at the end, those
   written inside the window, by anything that went round the record; and
   the runs not written back whose record differs from the datum (a write
   through the record that would be lost) */
static unsigned char (*box_ws_move_shadow)[BOX_WS_BIPED_BYTES];
static int box_ws_move_check;
static unsigned long box_ws_move_check_records, box_ws_move_check_resumes, box_ws_move_check_early;
static unsigned long box_ws_move_check_between[BOX_WS_BIPED_RUNS], box_ws_move_check_inside[BOX_WS_BIPED_RUNS],
	box_ws_move_check_lost[BOX_WS_BIPED_RUNS], box_ws_move_check_marked[BOX_WS_BIPED_RUNS];

/* after the runs are known: each datum byte's place in the record; two runs
   holding one datum byte would leave two copies, so the move refuses them */
static int box_ws_move_map_init(void)
{
	unsigned i, j;

	for (i = 0; i < BOX_WS_DATUM_BYTES; i++)
		box_ws_move_map[i] = -1;
	for (i = 0; i < box_ws_nruns; i++)
	{
		struct box_ws_run const *run = &box_ws_all_runs[i];

		if (run->datum + run->bytes > BOX_WS_DATUM_BYTES)
		{
			platform_psp_log("box_ws: run at datum +%u (%u B) past the datum's %u bytes; the store is off",
				run->datum, run->bytes, (unsigned)BOX_WS_DATUM_BYTES);
			return 0;
		}
		for (j = 0; j < run->bytes; j++)
		{
			if (box_ws_move_map[run->datum + j] >= 0)
			{
				platform_psp_log("box_ws: datum +%u held by two runs; the store is off", run->datum + j);
				return 0;
			}
			box_ws_move_map[run->datum + j] = (short)(box_ws_run_store[i] + j);
			box_ws_move_back[run->datum + j] = box_ws_run_back[i];
		}
	}
	/* the accessor's constant tables (tools/bipedmove_map.py) must be these */
	for (i = j = 0; i < BOX_WS_DATUM_BYTES && box_ws_move_back_sum_c[i] == j; i++)
		j += box_ws_move_back[i];
	if (memcmp(box_ws_move_map, box_ws_move_map_c, sizeof(box_ws_move_map)) ||
		i < BOX_WS_DATUM_BYTES || box_ws_move_back_sum_c[i] != j)
	{
		platform_psp_log("box_ws: psp_bipedmove_map.h doesn't match the store's runs (rerun tools/bipedmove_map.py); the store is off");
		return 0;
	}
	return 1;
}

static void box_ws_slot_in(int slot);
static void box_ws_slot_out(int slot)
{
	unsigned char *to = (unsigned char *)box_ws_move_slot_datum[slot];
	unsigned i;

	if (box_ws_ghost)
		box_ws_slot_in(slot);

	for (i = 0; i < box_ws_nruns; i++)
	{
		if (box_ws_run_back[i])
		{
			memcpy(to + box_ws_all_runs[i].datum, BOX_WS_REC(slot) + box_ws_run_store[i], box_ws_all_runs[i].bytes);
			if (box_ws_move_check)
				memcpy(box_ws_move_shadow[slot] + box_ws_run_store[i], BOX_WS_REC(slot) + box_ws_run_store[i], box_ws_all_runs[i].bytes);
		}
	}
}

static void box_ws_slot_in(int slot)
{
	unsigned char const *from = (unsigned char const *)box_ws_move_slot_datum[slot];
	unsigned i;

	for (i = 0; i < box_ws_nruns; i++)
		memcpy(BOX_WS_REC(slot) + box_ws_run_store[i], from + box_ws_all_runs[i].datum, box_ws_all_runs[i].bytes);
	if (box_ws_move_check)
		memcpy(box_ws_move_shadow[slot], BOX_WS_REC(slot), BOX_WS_BIPED_BYTES);
}

static void box_ws_move_attach(int slot)
{
	box_ws_move_slot = slot;
	box_ws_move_state[slot] = 1;
	box_ws_move_live = BOX_WS_REC(slot);
	box_ws_move_datum = box_ws_ghost ? (void *)&box_ws_ghost_datum : box_ws_move_slot_datum[slot];
}

static int box_ws_move_detach(void)
{
	int slot = box_ws_move_slot;

	box_ws_move_slot = -1;
	box_ws_move_datum = NULL;
	return slot;
}

/* the runs of the slot's datum written since the shadow was taken: by a
   writer that went round the record */
static unsigned box_ws_move_check_datum(int slot, unsigned long *count)
{
	unsigned char const *datum = (unsigned char const *)box_ws_move_slot_datum[slot];
	unsigned i, differ = 0;

	for (i = 0; i < box_ws_nruns; i++)
	{
		if (memcmp(box_ws_move_shadow[slot] + box_ws_run_store[i], datum + box_ws_all_runs[i].datum, box_ws_all_runs[i].bytes))
		{
			count[i]++;
			differ++;
		}
	}
	return differ;
}

static void box_ws_move_check_log(unsigned long records, int force)
{
	static const char *const what[4] = { "between the passes", "inside the window", "lost (not written back)",
		"between the passes by a marked writer (written back first, refilled)" };
	unsigned long *counts[4] = { box_ws_move_check_between, box_ws_move_check_inside, box_ws_move_check_lost,
		box_ws_move_check_marked };
	int k;

	if (!force && records % 3000)
		return;
	for (k = 0; k < 4; k++)
	{
		char line[400];
		int n = 0;
		unsigned i;

		line[0] = 0;
		for (i = 0; i < box_ws_nruns && n < (int)sizeof(line) - 40; i++)
		{
			if (counts[k][i])
				n += snprintf(line + n, sizeof(line) - n, " %s+%u(%u B) %lu;", box_ws_owner_name[box_ws_run_owner[i]],
					box_ws_all_runs[i].datum, box_ws_all_runs[i].bytes, counts[k][i]);
		}
		platform_psp_log("bipedmovestorecheck: %lu windows (%lu resumed): datum runs written %s:%s", records,
			box_ws_move_check_resumes, what[k], n ? line : " none");
	}
}

/* the narrow syncs (box_world_store_biped.h): only the named fields out and in */
static void box_ws_move_spans(int slot, struct box_ws_span const *spans, unsigned count, int put)
{
	unsigned char *datum = (unsigned char *)box_ws_move_slot_datum[slot];
	unsigned i;

	for (i = 0; i < count; i++)
	{
		short at = box_ws_move_map_c[spans[i].datum];

		if (put && box_ws_ghost)
			memcpy(BOX_WS_REC(slot) + at, datum + spans[i].datum, spans[i].bytes);
		if (put)
			memcpy(datum + spans[i].datum, BOX_WS_REC(slot) + at, spans[i].bytes);
		else
			memcpy(BOX_WS_REC(slot) + at, datum + spans[i].datum, spans[i].bytes);
		if (box_ws_move_check)
			memcpy(box_ws_move_shadow[slot] + at, datum + spans[i].datum, spans[i].bytes);
	}
}

#define BOX_WS_SPANS(a) a, (unsigned)(sizeof(a) / sizeof(a[0]))

/* the intent's start: the record filled from the datum and made live in its
   slot; 0 when the store's runs didn't check out (the window then runs on
   the datum, as before) */
int box_ws_move_begin(void *datum, long index, int slot, int check)
{
	BMS_COPY_LABEL;
	if (box_ws_biped_slots() == 0 || slot < 0 || slot >= BOX_WS_BIPED_SLOTS)
		return 0;
	if (!box_ws_move_claimed)
	{
		box_ws_move_claimed = 1;
		box_ws_move_check = check;
		{
			int psp_keep(const char *name);

			box_ws_ghost1 = psp_keep("bmsghost1") != 0;
			box_ws_ghost = box_ws_ghost1 || psp_keep("bmsghost");
			if (box_ws_ghost)
				platform_psp_log("bmsghost: the window's copies without the record read (%s)",
					box_ws_ghost1 ? "every record in one slot's lines" : "a record a slot");
			if (box_ws_ghost)
				box_ws_move_check = check = 0;
		}
		if (check && !(box_ws_move_shadow = malloc(sizeof(*box_ws_move_shadow) * BOX_WS_BIPED_SLOTS)))
		{
			platform_psp_log("bipedmovestorecheck: no room for the shadow records; the check is off");
			box_ws_move_check = 0;
		}
	}
	if (box_ws_move_state[slot] >= 2)
		box_ws_move_kept--;
	box_ws_move_slot_datum[slot] = datum;
	box_ws_move_slot_index[slot] = index;
	box_ws_slot_in(slot);
	box_ws_move_attach(slot);
	box_ws_move_count++;
	return 1;
}

/* where the intent stops: the velocity written through, the record kept */
void box_ws_move_keep(void)
{
	BMS_COPY_LABEL;
	unsigned i;
	int slot = box_ws_move_slot;

	if (slot < 0)
		return;
	for (i = 0; i < sizeof(box_ws_biped_intent_put) / sizeof(box_ws_biped_intent_put[0]); i++)
	{
		struct box_ws_span const *span = &box_ws_biped_intent_put[i];
		short at = box_ws_move_map_c[span->datum];

		if (box_ws_ghost)
			memcpy(box_ws_move_live + at, (unsigned char *)box_ws_move_slot_datum[slot] + span->datum, span->bytes);
		memcpy((unsigned char *)box_ws_move_slot_datum[slot] + span->datum, box_ws_move_live + at, span->bytes);
		if (box_ws_move_check)
			memcpy(box_ws_move_shadow[slot] + at, box_ws_move_live + at, span->bytes);
	}
	box_ws_move_detach();
	box_ws_move_state[slot] = 2;
	box_ws_move_kept++;
}

/* the solve's start: the kept record made live again; 0 when there's none */
int box_ws_move_resume(void *datum, int slot)
{
	BMS_COPY_LABEL;
	if (slot < 0 || slot >= BOX_WS_BIPED_SLOTS || box_ws_move_state[slot] < 2 || box_ws_move_slot_datum[slot] != datum)
		return 0;
	box_ws_move_kept--;
	box_ws_move_spans(slot, BOX_WS_SPANS(box_ws_biped_resume_get), 0);
	if (box_ws_move_check)
	{
		box_ws_move_check_resumes++;
		if (box_ws_move_state[slot] == 3)
			box_ws_move_check_datum(slot, box_ws_move_check_marked);
		else if (box_ws_move_check_datum(slot, box_ws_move_check_between) && box_ws_move_check_early++ < 20)
			box_ws_move_check_log(box_ws_move_check_records, 1);
	}
	if (box_ws_move_state[slot] == 3)
		box_ws_slot_in(slot);
	box_ws_move_attach(slot);
	return 1;
}

/* psp_bs3_out: the written-back runs to the datum, and the window closed */
void box_ws_move_end(void)
{
	BMS_COPY_LABEL;
	int slot = box_ws_move_slot;

	if (slot < 0)
		return;
	if (box_ws_move_check)
	{
		unsigned char const *datum = (unsigned char const *)box_ws_move_datum;
		unsigned i, differ;

		box_ws_move_check_records++;
		differ = box_ws_move_check_datum(slot, box_ws_move_check_inside);
		for (i = 0; i < box_ws_nruns; i++)
		{
			if (!box_ws_run_back[i] &&
				memcmp(box_ws_move_live + box_ws_run_store[i], datum + box_ws_all_runs[i].datum, box_ws_all_runs[i].bytes))
			{
				box_ws_move_check_lost[i]++;
				differ++;
			}
		}
		box_ws_move_check_log(box_ws_move_check_records, differ && box_ws_move_check_early++ < 20);
	}
	box_ws_slot_out(slot);
	box_ws_move_detach();
	box_ws_move_state[slot] = 0;
	if (++box_ws_move_ended % 3000 == 0)
		platform_psp_log("bipedmovestore: %lu windows run from the store; kept records written back by a mark %lu, after the solves %lu; windows ended by a delete %lu",
			box_ws_move_count, box_ws_move_flushed, box_ws_move_settled, box_ws_move_dropped);
}

/* a call that reads and writes the biped's datum itself: the written-back
   runs out before (the record off while it runs, so the window's own
   functions reached from it use the datum), the whole record in after */
int box_ws_move_sync_out(void)
{
	BMS_COPY_LABEL;
	int slot = box_ws_move_slot;

	if (slot >= 0)
	{
		box_ws_slot_out(slot);
		box_ws_move_detach();
	}
	return slot;
}

void box_ws_move_sync_in(int slot)
{
	BMS_COPY_LABEL;
	if (slot < 0)
		return;
	box_ws_slot_in(slot);
	box_ws_move_attach(slot);
}

int box_ws_move_narrow_out(int which)
{
	BMS_COPY_LABEL;
	int slot = box_ws_move_slot;

	if (slot < 0)
		return slot;
	if (which == 0)
		box_ws_move_spans(slot, BOX_WS_SPANS(box_ws_biped_translate_put), 1);
	return box_ws_move_detach();
}

void box_ws_move_narrow_in(int slot, int which)
{
	BMS_COPY_LABEL;
	if (slot < 0)
		return;
	if (which == 0)
		box_ws_move_spans(slot, BOX_WS_SPANS(box_ws_biped_translate_get), 0);
	else
		box_ws_move_spans(slot, BOX_WS_SPANS(box_ws_biped_interp_get), 0);
	box_ws_move_attach(slot);
}

/* before the biped's datum is deleted: written back, and the window ends
   there (the rest of the update runs on the datum, as the engine's does) */
void box_ws_move_drop(void)
{
	BMS_COPY_LABEL;
	int slot = box_ws_move_slot;

	if (slot < 0)
		return;
	box_ws_slot_out(slot);
	box_ws_move_detach();
	box_ws_move_state[slot] = 0;
	box_ws_move_dropped++;
}

/* after the staged solves: a kept record whose solve didn't resume it (its
   update was skipped) written back, so no write of its intent is lost */
void box_ws_move_settle(void)
{
	BMS_COPY_LABEL;
	int slot;

	if (box_ws_move_slot >= 0)
		box_ws_move_end();
	for (slot = 0; box_ws_move_kept > 0 && slot < BOX_WS_BIPED_SLOTS; slot++)
	{
		if (box_ws_move_state[slot] >= 2)
		{
			if (box_ws_move_state[slot] == 2)
				box_ws_slot_out(slot);
			box_ws_move_state[slot] = 0;
			box_ws_move_kept--;
			box_ws_move_settled++;
		}
	}
	box_ws_move_kept = 0;
}

/* an outside writer about to write a biped whose record is kept: the
   record's written-back runs go to the datum first, and the resume refills */
static void box_ws_move_flush(long object_index)
{
	BMS_COPY_LABEL;
	int slot;

	for (slot = 0; slot < BOX_WS_BIPED_SLOTS; slot++)
	{
		if (box_ws_move_state[slot] == 2 && box_ws_move_slot_index[slot] == object_index)
		{
			box_ws_slot_out(slot);
			box_ws_move_state[slot] = 3;
			box_ws_move_flushed++;
			return;
		}
	}
}

/* the header's runs are grouped by owner in the sections' order, each
   owner's store offsets starting again at 0 */
static int box_ws_init(void)
{
	unsigned i;
	int owner = 0;
	unsigned short base = 0;

	for (i = 0; i < BOX_WS_GEN_RUNS; i++)
	{
		struct box_ws_run const *run = &box_ws_biped_runs[i];

		if (i && run->store == 0)
		{
			base += box_ws_owner_size[owner];
			if (++owner >= BOX_WS_OWNERS)
				break;
		}
		if (i == 0 || run->store == 0)
			box_ws_owner_base[owner] = base;
		if (run->store + run->bytes > box_ws_owner_size[owner])
			break;
		box_ws_run_owner[i] = (unsigned char)owner;
	}
	if (i != BOX_WS_GEN_RUNS || owner != BOX_WS_OWNERS - 1)
	{
		platform_psp_log("box_ws: the biped runs don't match the sections (run %u of %u, owner %d); the check is off",
			i, (unsigned)BOX_WS_GEN_RUNS, owner);
		return 0;
	}
	box_ws_nruns = 0;
	for (i = 0; i < BOX_WS_GEN_RUNS; i++)
	{
		struct box_ws_run const *run = &box_ws_biped_runs[i];
		unsigned w;

		for (w = 0; w < BOX_WS_ANIM_RUNS; w++)
		{
			struct box_ws_run const *whole = &box_ws_biped_anim_runs[w];

			if (run->datum >= whole->datum && run->datum + run->bytes <= whole->datum + whole->bytes)
				break;
		}
		if (w < BOX_WS_ANIM_RUNS)
			continue;  /* inside a field held whole */
		box_ws_run_owner[box_ws_nruns] = box_ws_run_owner[i];
		box_ws_all_runs[box_ws_nruns] = *run;
		box_ws_run_store[box_ws_nruns++] = box_ws_owner_base[box_ws_run_owner[i]] + run->store;
	}
	for (i = 0; i < BOX_WS_UNIT2_RUNS; i++)
	{
		box_ws_all_runs[box_ws_nruns] = box_ws_biped_unit2_runs[i];
		box_ws_run_owner[box_ws_nruns] = 1;  /* unit */
		box_ws_run_store[box_ws_nruns++] = BOX_WS_GEN_BYTES + box_ws_biped_unit2_runs[i].store;
	}
	for (i = 0; i < BOX_WS_READ2_RUNS; i++)
	{
		box_ws_all_runs[box_ws_nruns] = box_ws_biped_read2_runs[i];
		box_ws_run_owner[box_ws_nruns] = 0;  /* read */
		box_ws_run_store[box_ws_nruns++] = BOX_WS_GEN_BYTES + sizeof(struct box_ws_biped_unit2) + box_ws_biped_read2_runs[i].store;
	}
	for (i = 0; i < BOX_WS_ANIM_RUNS; i++)
	{
		box_ws_all_runs[box_ws_nruns] = box_ws_biped_anim_runs[i];
		box_ws_run_owner[box_ws_nruns] = 1;  /* unit */
		box_ws_run_store[box_ws_nruns++] = BOX_WS_GEN_BYTES + sizeof(struct box_ws_biped_unit2) +
			sizeof(struct box_ws_biped_read2) + box_ws_biped_anim_runs[i].store;
	}
	for (i = 0; i < box_ws_nruns; i++)
	{
		unsigned k;

		box_ws_run_back[i] = box_ws_run_owner[i] == 1 || box_ws_run_owner[i] == 4;
		for (k = 0; k < sizeof(box_ws_biped_move_back) / sizeof(box_ws_biped_move_back[0]); k++)
			box_ws_run_back[i] |= box_ws_all_runs[i].datum == box_ws_biped_move_back[k];
	}
	return box_ws_move_map_init();
}

int box_ws_biped_slots(void)
{
	if (box_ws_ready < 0)
		box_ws_ready = box_ws_init();
	return box_ws_ready ? BOX_WS_BIPED_SLOTS : 0;
}

void box_ws_biped_fill(int slot, void const *datum, long index)
{
	if (box_ws_move_claimed)
		return;
	box_ws_dirty[index & (BOX_WS_DIRTY - 1)] = 0;
	box_ws_slot_index[slot] = index;
	box_ws_slot_tick[slot] = box_ws_ticks + 1;
	unsigned char const *from = (unsigned char const *)datum;
	unsigned char *record = box_ws_biped[slot];
	unsigned i;

	for (i = 0; i < box_ws_nruns; i++)
	{
		struct box_ws_run const *run = &box_ws_all_runs[i];

		memcpy(record + box_ws_run_store[i], from + run->datum, run->bytes);
	}
}

/* 1c's slot-dirty marks: the writers outside the staged passes that the
   lab's wswatch1 named (damage, speech, AI orders, placement) mark their
   object here before they write (FIXES_BIPEDSTAGE #wsdirty); a fill clears
   it. Keyed by the datum index's low bits, so two objects that share them
   both read as marked: a false mark only costs a refill. */

extern int psp_bipedstage_phase; /* objects.c (FIXES_BIPEDSTAGE): nonzero inside the staged passes */

void psp_ws_dirty(long object_index)
{
	if (box_ws_move_kept > 0)
		box_ws_move_flush(object_index);
	/* the staged passes' own writes are the record's, not an outside writer's */
	if (psp_bipedstage_phase)
		return;
	box_ws_dirty[object_index & (BOX_WS_DIRTY - 1)] = 1;
}

/* before this tick's fills: last tick's record of this biped, if it was
   staged then, against its datum now */
void box_ws_biped_cross(long index, void const *datum)
{
	unsigned char const *from = (unsigned char const *)datum;
	int slot;
	unsigned i;

	if (box_ws_move_claimed)
		return;
	for (slot = 0; slot < BOX_WS_BIPED_SLOTS; slot++)
	{
		if (box_ws_slot_index[slot] == index && box_ws_slot_tick[slot] == box_ws_ticks)
			break;
	}
	if (slot == BOX_WS_BIPED_SLOTS)
		return;
	/* 1c's outside-writer watch (the emulator's PSP_WATCH): one staged
	   biped's datum, named once, so a second run can watch its runs */
	{
		static int named;

		if (!named && box_ws_ticks >= 300)
		{
			named = 1;
			platform_psp_log("wsbipedcross: watch biped %08lx datum %08lx at tick %lu",
				(unsigned long)index, (unsigned long)(size_t)datum, box_ws_ticks);
		}
	}
	box_ws_cross_records++;
	{
		int marked = box_ws_dirty[index & (BOX_WS_DIRTY - 1)];

		box_ws_cross_marked += marked;
		for (i = 0; i < box_ws_nruns; i++)
		{
			struct box_ws_run const *run = &box_ws_all_runs[i];

			box_ws_cross_compared[i]++;
			if (memcmp(box_ws_biped[slot] + box_ws_run_store[i], from + run->datum, run->bytes))
			{
				box_ws_cross_changed[i]++;
				/* a unit write no mark explains: a writer the watch missed */
				if (!marked && box_ws_run_owner[i] == 1)
				{
					if (box_ws_run_every(run->datum))
						box_ws_cross_unmarked_every++;
					else
					{
						box_ws_cross_unmarked_unit++;
						box_ws_cross_unmarked_run[i]++;
					}
				}
			}
		}
	}
}

/* kind: 0 steady, 1 died this tick, 2 its parent changed this tick */
void box_ws_biped_compare(int slot, void const *datum, int kind)
{
	unsigned char const *from = (unsigned char const *)datum;
	unsigned char const *record = box_ws_biped[slot];
	unsigned i;

	if (box_ws_move_claimed)
		return;
	if (kind < 0 || kind >= BOX_WS_CLASSES)
		kind = 0;
	box_ws_class_records[kind]++;
	for (i = 0; i < box_ws_nruns; i++)
	{
		struct box_ws_run const *run = &box_ws_all_runs[i];
		int differs = memcmp(record + box_ws_run_store[i], from + run->datum, run->bytes) != 0;

		if (kind == 0)
		{
			box_ws_compared[i]++;
			box_ws_changed[i] += differs;
		}
		if (differs)
		{
			if (box_ws_run_owner[i] == 1)
				box_ws_class_unit[kind]++;
			else
				box_ws_class_other[kind]++;
		}
	}
}

/* every 3,000 staged ticks: per owner, the share of its runs the biped
   passes changed; then the runs outside unit that changed, by datum offset */
void box_ws_biped_tick(void)
{
	unsigned long compared[BOX_WS_OWNERS] = { 0 }, changed[BOX_WS_OWNERS] = { 0 };
	char line[400];
	int n = 0, owner;
	unsigned i;

	if (++box_ws_ticks % 3000)
		return;
	for (i = 0; i < box_ws_nruns; i++)
	{
		compared[box_ws_run_owner[i]] += box_ws_compared[i];
		changed[box_ws_run_owner[i]] += box_ws_changed[i];
	}
	for (owner = 0; owner < BOX_WS_OWNERS; owner++)
		n += snprintf(line + n, sizeof(line) - n, "%s%s %lu of %lu", owner ? ", " : "", box_ws_owner_name[owner],
			changed[owner], compared[owner]);
	platform_psp_log("wsbipedcheck: %lu staged ticks: run copies the biped passes changed, by owner: %s", box_ws_ticks, line);
	n = 0;
	line[0] = 0;
	for (i = 0; i < box_ws_nruns && n < (int)sizeof(line) - 40; i++)
	{
		if (box_ws_run_owner[i] != 1 && box_ws_changed[i])
			n += snprintf(line + n, sizeof(line) - n, " %s+%u(%u B) %lu;", box_ws_owner_name[box_ws_run_owner[i]],
				box_ws_all_runs[i].datum, box_ws_all_runs[i].bytes, box_ws_changed[i]);
	}
	platform_psp_log("wsbipedcheck: steady, changed outside unit (datum offset, bytes, times):%s", n ? line : " none");
	n = 0;
	for (owner = 0; owner < BOX_WS_CLASSES; owner++)
		n += snprintf(line + n, sizeof(line) - n, "%s%s %lu records, run copies changed in unit %lu, outside %lu",
			owner ? "; " : "", box_ws_class_name[owner], box_ws_class_records[owner], box_ws_class_unit[owner],
			box_ws_class_other[owner]);
	platform_psp_log("wsbipedcheck: by the biped's tick: %s", line);
	if (!box_ws_cross_records)
		return;
	{
		unsigned long cc[BOX_WS_OWNERS] = { 0 }, cx[BOX_WS_OWNERS] = { 0 };

		for (i = 0; i < box_ws_nruns; i++)
		{
			cc[box_ws_run_owner[i]] += box_ws_cross_compared[i];
			cx[box_ws_run_owner[i]] += box_ws_cross_changed[i];
		}
		n = 0;
		for (owner = 0; owner < BOX_WS_OWNERS; owner++)
			n += snprintf(line + n, sizeof(line) - n, "%s%s %lu of %lu", owner ? ", " : "", box_ws_owner_name[owner], cx[owner], cc[owner]);
		platform_psp_log("wsbipedcross: %lu records across a tick: run copies changed between staged passes, by owner: %s", box_ws_cross_records, line);
		n = 0;
		line[0] = 0;
		for (i = 0; i < box_ws_nruns && n < (int)sizeof(line) - 40; i++)
		{
			if (box_ws_run_owner[i] == 1 && box_ws_cross_changed[i])
				n += snprintf(line + n, sizeof(line) - n, " unit+%u(%u B) %lu;", box_ws_all_runs[i].datum, box_ws_all_runs[i].bytes, box_ws_cross_changed[i]);
		}
		platform_psp_log("wsbipedcross: unit runs written outside the staged passes (datum offset, bytes, times):%s", n ? line : " none");
		platform_psp_log("wsbipedcross: %lu of %lu records marked by an outside writer; unit run copies changed in unmarked records: %lu (and %lu in the runs refilled every tick)",
			box_ws_cross_marked, box_ws_cross_records, box_ws_cross_unmarked_unit, box_ws_cross_unmarked_every);
		n = 0;
		line[0] = 0;
		for (i = 0; i < box_ws_nruns && n < (int)sizeof(line) - 40; i++)
		{
			if (box_ws_cross_unmarked_run[i])
				n += snprintf(line + n, sizeof(line) - n, " unit+%u(%u B) %lu;", box_ws_all_runs[i].datum, box_ws_all_runs[i].bytes, box_ws_cross_unmarked_run[i]);
		}
		platform_psp_log("wsbipedcross: unmarked unit changes outside the every-tick runs, by run:%s", n ? line : " none");
	}
}
