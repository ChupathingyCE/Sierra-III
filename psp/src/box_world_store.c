/* The world store's biped section, step 1a (docs/box_biped_motion.md §1c;
   docs/box_world_store.md §2, §7). Built with PSP_BIPEDSTAGE=1.

   A store record a biped: the five owner sections of box_world_store.h laid
   end to end (read, unit, posing, walk, control), filled from the datum by
   the header's copy runs. Nothing reads it yet. Under keep word wsbipedcheck
   the staged run fills each living biped's record after the unit pass and
   compares it with the datum after the solves: a field the biped passes
   changed is one they write, so the owner map is checked on the staged path
   before the intent reads the store (step 1b). */
#include <stdio.h>
#include <string.h>
#include "box_world_store.h"

void platform_psp_log(const char *format, ...);

#define BOX_WS_OWNERS 5
#define BOX_WS_BIPED_RUNS (sizeof(box_ws_biped_runs) / sizeof(box_ws_biped_runs[0]))
#define BOX_WS_BIPED_SLOTS 64

static const char *const box_ws_owner_name[BOX_WS_OWNERS] = { "read", "unit", "posing", "walk", "control" };
static const unsigned short box_ws_owner_size[BOX_WS_OWNERS] = {
	sizeof(struct box_ws_biped_read), sizeof(struct box_ws_biped_unit), sizeof(struct box_ws_biped_posing),
	sizeof(struct box_ws_biped_walk), sizeof(struct box_ws_biped_control) };
#define BOX_WS_BIPED_BYTES (sizeof(struct box_ws_biped_read) + sizeof(struct box_ws_biped_unit) + \
	sizeof(struct box_ws_biped_posing) + sizeof(struct box_ws_biped_walk) + sizeof(struct box_ws_biped_control))

static unsigned short box_ws_owner_base[BOX_WS_OWNERS];
static unsigned char box_ws_run_owner[BOX_WS_BIPED_RUNS];
/* -1 not yet, 0 the runs don't match the sections (logged; the check stays off), 1 ready */
static int box_ws_ready = -1;
static unsigned char box_ws_biped[BOX_WS_BIPED_SLOTS][BOX_WS_BIPED_BYTES] __attribute__((aligned(64)));
/* per run: the times it was compared and the times it differed */
static unsigned long box_ws_compared[BOX_WS_BIPED_RUNS], box_ws_changed[BOX_WS_BIPED_RUNS];
static unsigned long box_ws_ticks;

/* the header's runs are grouped by owner in the sections' order, each
   owner's store offsets starting again at 0 */
static int box_ws_init(void)
{
	unsigned i;
	int owner = 0;
	unsigned short base = 0;

	for (i = 0; i < BOX_WS_BIPED_RUNS; i++)
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
	if (i != BOX_WS_BIPED_RUNS || owner != BOX_WS_OWNERS - 1)
	{
		platform_psp_log("box_ws: the biped runs don't match the sections (run %u of %u, owner %d); the check is off",
			i, (unsigned)BOX_WS_BIPED_RUNS, owner);
		return 0;
	}
	return 1;
}

int box_ws_biped_slots(void)
{
	if (box_ws_ready < 0)
		box_ws_ready = box_ws_init();
	return box_ws_ready ? BOX_WS_BIPED_SLOTS : 0;
}

void box_ws_biped_fill(int slot, void const *datum)
{
	unsigned char const *from = (unsigned char const *)datum;
	unsigned char *record = box_ws_biped[slot];
	unsigned i;

	for (i = 0; i < BOX_WS_BIPED_RUNS; i++)
	{
		struct box_ws_run const *run = &box_ws_biped_runs[i];

		memcpy(record + box_ws_owner_base[box_ws_run_owner[i]] + run->store, from + run->datum, run->bytes);
	}
}

void box_ws_biped_compare(int slot, void const *datum)
{
	unsigned char const *from = (unsigned char const *)datum;
	unsigned char const *record = box_ws_biped[slot];
	unsigned i;

	for (i = 0; i < BOX_WS_BIPED_RUNS; i++)
	{
		struct box_ws_run const *run = &box_ws_biped_runs[i];

		box_ws_compared[i]++;
		if (memcmp(record + box_ws_owner_base[box_ws_run_owner[i]] + run->store, from + run->datum, run->bytes))
			box_ws_changed[i]++;
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
				box_ws_biped_runs[i].datum, box_ws_biped_runs[i].bytes, box_ws_changed[i]);
	}
	platform_psp_log("wsbipedcheck: changed outside unit (datum offset, bytes, times):%s", n ? line : " none");
}
