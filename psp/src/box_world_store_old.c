/* The world store's first slice (docs/box_world_store.md §9): the one-tick-
   old copy of the pill sweep's object inputs and of what biped motion reads
   of other bipeds. Built with PSP_WSOLD=1; keep word wsold takes the copy,
   wsoldcheck also proves it complete and counts how far it parts from the
   live world at the sweep's reads.

   The copy is refreshed once a tick, where the object walk begins, from the
   objects marked as written since (the walk's updates, the sleep's wake
   marks at the outside writers, creation, node matrices made on demand, the
   child lists) and from every biped held. Nothing writes it between two
   refreshes, so a reader on the other core needs no lock: it reads the world
   as the walk began. The engine's side (psp_fixups.py FIXES_WSOLD) makes each
   record from its datum; this file owns where it goes. */
#include <stdio.h>
#include <string.h>
#include "box_ws_old.h"

void platform_psp_log(const char *format, ...);
int psp_keep(const char *name);

struct box_ws_old_object box_ws_old_objects[BOX_WS_OLD_SLOTS] __attribute__((aligned(64)));
struct box_ws_old_biped box_ws_old_bipeds[BOX_WS_OLD_BIPEDS] __attribute__((aligned(64)));
int16_t box_ws_old_biped_of[BOX_WS_OLD_SLOTS];
int box_ws_old_on = -1;
int box_ws_old_checking;

static int16_t box_ws_old_biped_owner[BOX_WS_OLD_BIPEDS];
static uint32_t box_ws_old_bits[BOX_WS_OLD_SLOTS / 32];
static uint16_t box_ws_old_list[BOX_WS_OLD_SLOTS];
static unsigned box_ws_old_listed;

/* the counts behind the log line, since the first refresh */
static unsigned long box_ws_old_flips, box_ws_old_refreshed, box_ws_old_refreshed_bipeds, box_ws_old_cleared;
static unsigned long box_ws_old_past_slots, box_ws_old_bipeds_full;
static unsigned box_ws_old_high_water, box_ws_old_most_refreshed, box_ws_old_most_bipeds;

enum { WSO_SPHERE, WSO_FLAGS, WSO_LINKS, WSO_TYPE, WSO_DEAD, WSO_NONE, WSO_PILL, WSO_MOTION, WSO_UNIT, WSO_FIELDS };
static const char *const box_ws_old_field_name[WSO_FIELDS] = {
	"sphere", "flags", "links", "type", "dead", "no record", "pill", "position/velocity", "parent/player/seat/passes"
};
static unsigned long box_ws_old_check_objects, box_ws_old_check_missed[WSO_FIELDS];
static unsigned box_ws_old_check_told;
static unsigned long box_ws_old_visits, box_ws_old_visit_parted[WSO_FIELDS], box_ws_old_visits_parted;

int box_ws_old_begin(void)
{
	if (box_ws_old_on < 0)
	{
		box_ws_old_checking = psp_keep("wsoldcheck") != 0;
		box_ws_old_on = box_ws_old_checking || psp_keep("wsold");
		if (!box_ws_old_on)
			return 0;
		memset(box_ws_old_biped_of, 0xff, sizeof(box_ws_old_biped_of));
		memset(box_ws_old_biped_owner, 0xff, sizeof(box_ws_old_biped_owner));
		platform_psp_log("wsold: the one-tick-old copy on (%u object slots of %u B, %u bipeds of %u B, %u KB)%s",
			(unsigned)BOX_WS_OLD_SLOTS, (unsigned)sizeof(struct box_ws_old_object), (unsigned)BOX_WS_OLD_BIPEDS,
			(unsigned)sizeof(struct box_ws_old_biped),
			(unsigned)((sizeof(box_ws_old_objects) + sizeof(box_ws_old_bipeds) + sizeof(box_ws_old_biped_of)) / 1024),
			box_ws_old_checking ? ", checked" : "");
		return 2;
	}
	return box_ws_old_on;
}

void box_ws_old_mark(long datum_index)
{
	unsigned absolute = (unsigned)datum_index & 0xffff;

	if (box_ws_old_on <= 0)
		return;
	if (absolute >= BOX_WS_OLD_SLOTS)
	{
		box_ws_old_past_slots++;
		return;
	}
	if (box_ws_old_bits[absolute >> 5] & (1u << (absolute & 31)))
		return;
	box_ws_old_bits[absolute >> 5] |= 1u << (absolute & 31);
	box_ws_old_list[box_ws_old_listed++] = (uint16_t)absolute;
}

int box_ws_old_marked(uint16_t *absolute, unsigned capacity)
{
	unsigned n = 0, i;

	for (i = 0; i < box_ws_old_listed && n < capacity; i++)
		absolute[n++] = box_ws_old_list[i];
	for (i = 0; i < BOX_WS_OLD_BIPEDS && n < capacity; i++)
	{
		int16_t owner = box_ws_old_biped_owner[i];

		if (owner >= 0 && !(box_ws_old_bits[owner >> 5] & (1u << (owner & 31))))
			absolute[n++] = (uint16_t)owner;
	}
	box_ws_old_listed = 0;
	memset(box_ws_old_bits, 0, sizeof(box_ws_old_bits));
	return (int)n;
}

static void box_ws_old_free_biped(unsigned absolute)
{
	int16_t slot = box_ws_old_biped_of[absolute];

	if (slot >= 0)
	{
		box_ws_old_biped_owner[slot] = -1;
		box_ws_old_biped_of[absolute] = -1;
	}
}

void box_ws_old_put(unsigned absolute, struct box_ws_old_object const *object, struct box_ws_old_biped const *biped)
{
	if (absolute >= BOX_WS_OLD_SLOTS)
		return;
	if (!object)
	{
		if (box_ws_old_objects[absolute].identifier)
			box_ws_old_cleared++;
		box_ws_old_objects[absolute].identifier = 0;
		box_ws_old_free_biped(absolute);
		return;
	}
	box_ws_old_objects[absolute] = *object;
	box_ws_old_refreshed++;
	if (!biped)
	{
		box_ws_old_free_biped(absolute);
		return;
	}
	if (box_ws_old_biped_of[absolute] < 0)
	{
		int slot;

		for (slot = 0; slot < BOX_WS_OLD_BIPEDS && box_ws_old_biped_owner[slot] >= 0; slot++)
			;
		if (slot == BOX_WS_OLD_BIPEDS)
		{
			box_ws_old_bipeds_full++;
			return;
		}
		box_ws_old_biped_owner[slot] = (int16_t)absolute;
		box_ws_old_biped_of[absolute] = (int16_t)slot;
	}
	box_ws_old_bipeds[box_ws_old_biped_of[absolute]] = *biped;
	box_ws_old_refreshed_bipeds++;
}

static void box_ws_old_log(void)
{
	char line[300];
	int n = 0, k;
	double flips = box_ws_old_flips ? (double)box_ws_old_flips : 1.0;

	platform_psp_log("wsold: %lu refreshes: %.1f objects and %.1f bipeds a refresh (%.0f B; most %u and %u), %lu records cleared; slots used to %u of %u, %lu marks past them, %lu bipeds without a record",
		box_ws_old_flips, box_ws_old_refreshed / flips, box_ws_old_refreshed_bipeds / flips,
		(box_ws_old_refreshed * sizeof(struct box_ws_old_object) + box_ws_old_refreshed_bipeds * sizeof(struct box_ws_old_biped)) / flips,
		box_ws_old_most_refreshed, box_ws_old_most_bipeds, box_ws_old_cleared, box_ws_old_high_water,
		(unsigned)BOX_WS_OLD_SLOTS, box_ws_old_past_slots, box_ws_old_bipeds_full);
	if (!box_ws_old_checking)
		return;
	for (k = 0; k < WSO_FIELDS; k++)
		n += snprintf(line + n, sizeof(line) - n, "%s%s %lu", k ? ", " : "", box_ws_old_field_name[k], box_ws_old_check_missed[k]);
	platform_psp_log("wsoldcheck: %lu live records checked after the refresh; changes no mark caught: %s", box_ws_old_check_objects, line);
	n = 0;
	for (k = 0; k < WSO_FIELDS; k++)
		n += snprintf(line + n, sizeof(line) - n, "%s%s %lu", k ? ", " : "", box_ws_old_field_name[k], box_ws_old_visit_parted[k]);
	platform_psp_log("wsoldcheck: the engine's sweep visited %lu objects; the copy parted from the live world at %lu (%.2f%%): %s",
		box_ws_old_visits, box_ws_old_visits_parted,
		box_ws_old_visits ? 100.0 * box_ws_old_visits_parted / box_ws_old_visits : 0.0, line);
}

void box_ws_old_flipped(unsigned objects, unsigned bipeds, unsigned high_water)
{
	box_ws_old_flips++;
	if (objects > box_ws_old_most_refreshed)
		box_ws_old_most_refreshed = objects;
	if (bipeds > box_ws_old_most_bipeds)
		box_ws_old_most_bipeds = bipeds;
	if (high_water > box_ws_old_high_water)
		box_ws_old_high_water = high_water;
	if (box_ws_old_flips % 300 == 0)
		box_ws_old_log();
}

/* the fields a record holds that differ, as WSO_ bits */
static unsigned box_ws_old_differ(struct box_ws_old_object const *a, struct box_ws_old_object const *b)
{
	unsigned bits = 0;

	if (memcmp(a->center, b->center, sizeof(a->center)) || memcmp(&a->radius, &b->radius, sizeof(a->radius)))
		bits |= 1u << WSO_SPHERE;
	if (a->flags != b->flags)
		bits |= 1u << WSO_FLAGS;
	if (a->next != b->next || a->first_child != b->first_child)
		bits |= 1u << WSO_LINKS;
	if (a->type != b->type)
		bits |= 1u << WSO_TYPE;
	if (a->dead != b->dead)
		bits |= 1u << WSO_DEAD;
	return bits;
}

static unsigned box_ws_old_biped_differ(struct box_ws_old_biped const *a, struct box_ws_old_biped const *b, int motion)
{
	unsigned bits = 0;

	if (a->pill_held != b->pill_held || (a->pill_held &&
		(memcmp(a->pill_base, b->pill_base, sizeof(a->pill_base)) || memcmp(&a->pill_height, &b->pill_height, 8))))
		bits |= 1u << WSO_PILL;
	if (motion && (memcmp(a->position, b->position, sizeof(a->position)) || memcmp(a->velocity, b->velocity, sizeof(a->velocity))))
		bits |= 1u << WSO_MOTION;
	if (a->parent != b->parent || a->player != b->player || a->seat != b->seat || a->passes_through != b->passes_through)
		bits |= 1u << WSO_UNIT;
	return bits;
}

void box_ws_old_check(unsigned absolute, struct box_ws_old_object const *object, struct box_ws_old_biped const *biped)
{
	struct box_ws_old_object const *held;
	unsigned bits = 0, k;

	if (absolute >= BOX_WS_OLD_SLOTS)
		return;
	box_ws_old_check_objects++;
	held = &box_ws_old_objects[absolute];
	if (held->identifier != object->identifier)
		bits = 1u << WSO_NONE;
	else
	{
		bits = box_ws_old_differ(held, object);
		if (biped)
		{
			if (box_ws_old_biped_of[absolute] < 0)
				bits |= 1u << WSO_NONE;
			else
				bits |= box_ws_old_biped_differ(&box_ws_old_bipeds[box_ws_old_biped_of[absolute]], biped, 1);
		}
	}
	if (!bits)
		return;
	for (k = 0; k < WSO_FIELDS; k++)
		if (bits & (1u << k))
			box_ws_old_check_missed[k]++;
	if (box_ws_old_check_told < 24)
	{
		box_ws_old_check_told++;
		platform_psp_log("wsoldcheck: refresh %lu: slot %u (type %u) changed with no mark (fields %x)",
			box_ws_old_flips, absolute, (unsigned)object->type, bits);
	}
	/* kept true from here, so one missed writer counts once a tick, not on */
	box_ws_old_put(absolute, object, biped);
}

void box_ws_old_visit(long datum_index, struct box_ws_old_object const *now, struct box_ws_old_biped const *biped_now)
{
	struct box_ws_old_object const *held = box_ws_old_object_of(datum_index);
	unsigned bits, k;

	box_ws_old_visits++;
	if (!held)
		bits = 1u << WSO_NONE;
	else
	{
		bits = box_ws_old_differ(held, now);
		if (biped_now)
		{
			struct box_ws_old_biped const *was = box_ws_old_biped_of_index(datum_index);

			bits |= was ? box_ws_old_biped_differ(was, biped_now, 0) : 1u << WSO_NONE;
		}
	}
	if (!bits)
		return;
	box_ws_old_visits_parted++;
	for (k = 0; k < WSO_FIELDS; k++)
		if (bits & (1u << k))
			box_ws_old_visit_parted[k]++;
}
