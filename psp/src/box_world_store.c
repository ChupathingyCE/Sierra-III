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
#include <string.h>
#include "box_world_store.h"
#include "box_world_store_biped.h"

void platform_psp_log(const char *format, ...);

#define BOX_WS_OWNERS 5
#define BOX_WS_GEN_RUNS (sizeof(box_ws_biped_runs) / sizeof(box_ws_biped_runs[0]))
#define BOX_WS_UNIT2_RUNS (sizeof(box_ws_biped_unit2_runs) / sizeof(box_ws_biped_unit2_runs[0]))
#define BOX_WS_READ2_RUNS (sizeof(box_ws_biped_read2_runs) / sizeof(box_ws_biped_read2_runs[0]))
/* the generated runs, then biped motion's additions (box_world_store_biped.h) */
#define BOX_WS_BIPED_RUNS (BOX_WS_GEN_RUNS + BOX_WS_UNIT2_RUNS + BOX_WS_READ2_RUNS)
#define BOX_WS_BIPED_SLOTS 64

static const char *const box_ws_owner_name[BOX_WS_OWNERS] = { "read", "unit", "posing", "walk", "control" };
static const unsigned short box_ws_owner_size[BOX_WS_OWNERS] = {
	sizeof(struct box_ws_biped_read), sizeof(struct box_ws_biped_unit), sizeof(struct box_ws_biped_posing),
	sizeof(struct box_ws_biped_walk), sizeof(struct box_ws_biped_control) };
#define BOX_WS_GEN_BYTES (sizeof(struct box_ws_biped_read) + sizeof(struct box_ws_biped_unit) + \
	sizeof(struct box_ws_biped_posing) + sizeof(struct box_ws_biped_walk) + sizeof(struct box_ws_biped_control))
#define BOX_WS_BIPED_BYTES (BOX_WS_GEN_BYTES + sizeof(struct box_ws_biped_unit2) + sizeof(struct box_ws_biped_read2))

static unsigned short box_ws_owner_base[BOX_WS_OWNERS];
static unsigned char box_ws_run_owner[BOX_WS_BIPED_RUNS];
/* every run, generated and added, with its offset in the record */
static struct box_ws_run box_ws_all_runs[BOX_WS_BIPED_RUNS];
static unsigned short box_ws_run_store[BOX_WS_BIPED_RUNS];
/* -1 not yet, 0 the runs don't match the sections (logged; the check stays off), 1 ready */
static int box_ws_ready = -1;
static unsigned char box_ws_biped[BOX_WS_BIPED_SLOTS][BOX_WS_BIPED_BYTES] __attribute__((aligned(64)));
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
	for (i = 0; i < BOX_WS_GEN_RUNS; i++)
	{
		box_ws_all_runs[i] = box_ws_biped_runs[i];
		box_ws_run_store[i] = box_ws_owner_base[box_ws_run_owner[i]] + box_ws_biped_runs[i].store;
	}
	for (i = 0; i < BOX_WS_UNIT2_RUNS; i++)
	{
		box_ws_all_runs[BOX_WS_GEN_RUNS + i] = box_ws_biped_unit2_runs[i];
		box_ws_run_owner[BOX_WS_GEN_RUNS + i] = 1;  /* unit */
		box_ws_run_store[BOX_WS_GEN_RUNS + i] = BOX_WS_GEN_BYTES + box_ws_biped_unit2_runs[i].store;
	}
	for (i = 0; i < BOX_WS_READ2_RUNS; i++)
	{
		box_ws_all_runs[BOX_WS_GEN_RUNS + BOX_WS_UNIT2_RUNS + i] = box_ws_biped_read2_runs[i];
		box_ws_run_owner[BOX_WS_GEN_RUNS + BOX_WS_UNIT2_RUNS + i] = 0;  /* read */
		box_ws_run_store[BOX_WS_GEN_RUNS + BOX_WS_UNIT2_RUNS + i] =
			BOX_WS_GEN_BYTES + sizeof(struct box_ws_biped_unit2) + box_ws_biped_read2_runs[i].store;
	}
	return 1;
}

int box_ws_biped_slots(void)
{
	if (box_ws_ready < 0)
		box_ws_ready = box_ws_init();
	return box_ws_ready ? BOX_WS_BIPED_SLOTS : 0;
}

void box_ws_biped_fill(int slot, void const *datum, long index)
{
	box_ws_slot_index[slot] = index;
	box_ws_slot_tick[slot] = box_ws_ticks + 1;
	unsigned char const *from = (unsigned char const *)datum;
	unsigned char *record = box_ws_biped[slot];
	unsigned i;

	for (i = 0; i < BOX_WS_BIPED_RUNS; i++)
	{
		struct box_ws_run const *run = &box_ws_all_runs[i];

		memcpy(record + box_ws_run_store[i], from + run->datum, run->bytes);
	}
}

/* before this tick's fills: last tick's record of this biped, if it was
   staged then, against its datum now */
void box_ws_biped_cross(long index, void const *datum)
{
	unsigned char const *from = (unsigned char const *)datum;
	int slot;
	unsigned i;

	for (slot = 0; slot < BOX_WS_BIPED_SLOTS; slot++)
	{
		if (box_ws_slot_index[slot] == index && box_ws_slot_tick[slot] == box_ws_ticks)
			break;
	}
	if (slot == BOX_WS_BIPED_SLOTS)
		return;
	box_ws_cross_records++;
	for (i = 0; i < BOX_WS_BIPED_RUNS; i++)
	{
		struct box_ws_run const *run = &box_ws_all_runs[i];

		box_ws_cross_compared[i]++;
		if (memcmp(box_ws_biped[slot] + box_ws_run_store[i], from + run->datum, run->bytes))
			box_ws_cross_changed[i]++;
	}
}

/* kind: 0 steady, 1 died this tick, 2 its parent changed this tick */
void box_ws_biped_compare(int slot, void const *datum, int kind)
{
	unsigned char const *from = (unsigned char const *)datum;
	unsigned char const *record = box_ws_biped[slot];
	unsigned i;

	if (kind < 0 || kind >= BOX_WS_CLASSES)
		kind = 0;
	box_ws_class_records[kind]++;
	for (i = 0; i < BOX_WS_BIPED_RUNS; i++)
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
	for (i = 0; i < BOX_WS_BIPED_RUNS; i++)
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
	for (i = 0; i < BOX_WS_BIPED_RUNS && n < (int)sizeof(line) - 40; i++)
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

		for (i = 0; i < BOX_WS_BIPED_RUNS; i++)
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
		for (i = 0; i < BOX_WS_BIPED_RUNS && n < (int)sizeof(line) - 40; i++)
		{
			if (box_ws_run_owner[i] == 1 && box_ws_cross_changed[i])
				n += snprintf(line + n, sizeof(line) - n, " unit+%u(%u B) %lu;", box_ws_all_runs[i].datum, box_ws_all_runs[i].bytes, box_ws_cross_changed[i]);
		}
		platform_psp_log("wsbipedcross: unit runs written outside the staged passes (datum offset, bytes, times):%s", n ? line : " none");
	}
}
