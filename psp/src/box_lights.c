/* box_lights.c - the lighting box, slice 1: the kept reach (box_lights.h,
docs/box_lights.md section 9).

Everything here but box_lights_report runs where the light pass runs: on the
Media Engine, or on the main CPU when the emulator plays the ME's part. So no
VFPU, no system call and no log line; the counts are left in main RAM, on
lines of their own that only that side writes, for the main CPU to print. */
#include <string.h>
#include <malloc.h>
#include "psp_me.h"
#include "box_lights.h"

extern void platform_log(const char *format, ...);

/* The store: 128 KB of the ME's eDRAM at 520 KB, above its code copy's room
and below the sound tier (psp_me.h; docs/memory_ledger.md). A table of keys,
then the lit triangles, filled from the start; when either is full the whole
store is cleared and fills again: a light that stays is kept again two frames
on. */
#define BOX_LIGHTS_AT 0x82000u
#define BOX_LIGHTS_BYTES 0x20000u
#define BOX_LIGHTS_SLOTS 256 /* a power of two */
#define BOX_LIGHTS_PROBES 4
#define BOX_LIGHTS_LONGEST 4095 /* triangles in one list */
#define BOX_LIGHTS_MAGIC 0x4C495431u

typedef char box_lights_below_the_sound_tier[(BOX_LIGHTS_AT + BOX_LIGHTS_BYTES <= ME_EDRAM_SNDTIER) ? 1 : -1];

struct box_lights_slot
{
	struct box_lights_key key;
	unsigned int first;   /* its triangles' place in the store */
	unsigned short count; /* how many */
	unsigned short state; /* 0 empty, 1 seen once, 2 kept */
};

struct box_lights_store
{
	unsigned int magic, used; /* used: triangles */
	struct box_lights_slot slots[BOX_LIGHTS_SLOTS];
	struct box_lights_triangle triangles[1]; /* to the store's end */
};

#define BOX_LIGHTS_ROOM ((BOX_LIGHTS_BYTES - sizeof(struct box_lights_store)) / sizeof(struct box_lights_triangle) + 1)

enum
{
	COUNT_ASKED,    /* light draws that asked */
	COUNT_SERVED,   /* answered from a kept list */
	COUNT_KEPT,     /* lists kept */
	COUNT_FIRST,    /* a key's first sighting */
	COUNT_REPLACED, /* a slot given to another key */
	COUNT_CLEARED,  /* the store cleared for room */
	COUNT_SHORT,    /* a walk cut short, or a list too long: not kept */
	COUNT_NO_STORE, /* asked where the store can't be reached */
	COUNT_CHECKED,  /* lightkeepcheck: kept lists compared with the walk's */
	COUNT_DIFFERED,
	COUNT_USED,     /* triangles in the store now */
	COUNT_REFUSED,  /* and BOX_LIGHTS_REASONS after it */
	COUNTS = COUNT_REFUSED + BOX_LIGHTS_REASONS
};

/* written where the pass runs, read by the main CPU's report */
static unsigned long counts[16] __attribute__((aligned(64)));
typedef char box_lights_counts_fit[(COUNTS <= 16) ? 1 : -1];

/* the pass's side only */
static struct
{
	struct box_lights_store *twin; /* the emulator's, when it maps no eDRAM there */
	struct box_lights_slot *collecting;
	unsigned int collected;
	int cleared, overflowed, twin_tried;
} state __attribute__((aligned(64)));

static struct box_lights_store *store_get(void)
{
	struct box_lights_store *store;

	if (psp_on_me() || psp_me_sim_edram_holds(BOX_LIGHTS_AT, BOX_LIGHTS_BYTES))
		store = (struct box_lights_store *)(ME_EDRAM + BOX_LIGHTS_AT);
	else if (psp_me_simulated())
	{
		/* the emulator, its eDRAM not mapped here: the same store from the heap */
		if (!state.twin && !state.twin_tried)
		{
			state.twin_tried = 1;
			state.twin = memalign(64, BOX_LIGHTS_BYTES);
		}
		store = state.twin;
	}
	else
		store = NULL; /* a PSP's main CPU can't reach the ME's eDRAM */
	if (!store)
		return NULL;
	/* what a start, a sleep or another program left there is not ours */
	if (!state.cleared || store->magic != BOX_LIGHTS_MAGIC || store->used > BOX_LIGHTS_ROOM)
	{
		memset(store->slots, 0, sizeof(store->slots));
		store->magic = BOX_LIGHTS_MAGIC;
		store->used = 0;
		state.cleared = 1;
	}
	return store;
}

static void store_clear(struct box_lights_store *store)
{
	memset(store->slots, 0, sizeof(store->slots));
	store->used = 0;
	counts[COUNT_CLEARED]++;
	counts[COUNT_USED] = 0;
}

static unsigned int key_hash(const struct box_lights_key *key)
{
	const unsigned int *word = (const unsigned int *)key;
	unsigned int hash = 2166136261u, k;

	for (k = 0; k < sizeof(*key) / sizeof(unsigned int); k++)
		hash = (hash ^ word[k]) * 16777619u;
	return hash ^ (hash >> 15);
}

int box_lights_begin(const struct box_lights_key *key, const struct box_lights_triangle **kept, long *kept_count)
{
	struct box_lights_store *store = store_get();
	struct box_lights_slot *slot, *empty = NULL;
	unsigned int at, probe;

	if (!store)
	{
		/* a PSP's main CPU running the pass itself (the fallback) leaves no
		mark here: these lines are the ME's */
		if (psp_me_simulated())
			counts[COUNT_NO_STORE]++;
		return BOX_LIGHTS_WALK;
	}
	state.collecting = NULL; /* a walk that left without its end kept nothing */
	counts[COUNT_ASKED]++;
	at = key_hash(key);
	for (probe = 0; probe < BOX_LIGHTS_PROBES; probe++)
	{
		slot = &store->slots[(at + probe) & (BOX_LIGHTS_SLOTS - 1)];
		if (!slot->state)
		{
			if (!empty)
				empty = slot;
			continue;
		}
		if (memcmp(&slot->key, key, sizeof(*key)))
			continue;
		if (slot->state == 2 && slot->first <= store->used && slot->count <= store->used - slot->first)
		{
			counts[COUNT_SERVED]++;
			*kept = &store->triangles[slot->first];
			*kept_count = slot->count;
			return BOX_LIGHTS_KEPT;
		}
		/* seen once before, as it is now: this walk's lit triangles are kept */
		slot->state = 1;
		state.collecting = slot;
		state.collected = 0;
		state.overflowed = 0;
		return BOX_LIGHTS_COLLECT;
	}
	if (!empty)
	{
		empty = &store->slots[at & (BOX_LIGHTS_SLOTS - 1)];
		counts[COUNT_REPLACED]++;
	}
	empty->key = *key;
	empty->first = 0;
	empty->count = 0;
	empty->state = 1;
	counts[COUNT_FIRST]++;
	return BOX_LIGHTS_WALK;
}

void box_lights_add(const unsigned short *vertex, const float *reach)
{
	struct box_lights_store *store = store_get();
	struct box_lights_triangle *triangle;

	if (!store || !state.collecting || state.overflowed)
		return;
	if (state.collected >= BOX_LIGHTS_LONGEST || store->used + state.collected >= BOX_LIGHTS_ROOM)
	{
		state.overflowed = 1;
		return;
	}
	triangle = &store->triangles[store->used + state.collected++];
	triangle->reach[0] = reach[0];
	triangle->reach[1] = reach[1];
	triangle->reach[2] = reach[2];
	triangle->vertex[0] = vertex[0];
	triangle->vertex[1] = vertex[1];
	triangle->vertex[2] = vertex[2];
	triangle->spare = 0;
}

void box_lights_end(int walked_whole)
{
	struct box_lights_store *store = store_get();
	struct box_lights_slot *slot;

	if (!store)
		return;
	slot = state.collecting;
	state.collecting = NULL;
	if (!slot)
		return;
	if (state.overflowed && state.collected < BOX_LIGHTS_LONGEST)
	{
		/* no room left: start again; this light is kept two frames on */
		store_clear(store);
		return;
	}
	if (!walked_whole || state.overflowed)
	{
		counts[COUNT_SHORT]++;
		return;
	}
	slot->first = store->used;
	slot->count = (unsigned short)state.collected;
	slot->state = 2;
	store->used += state.collected;
	counts[COUNT_KEPT]++;
	counts[COUNT_USED] = store->used;
}

/* 1 where the pass runs with the store in reach: the ME, or the emulator playing it */
static int pass_side(void)
{
	return psp_on_me() || psp_me_simulated();
}

void box_lights_refused(int reason)
{
	if (pass_side())
		counts[COUNT_REFUSED + reason]++;
}

void box_lights_checked(int differed)
{
	if (!pass_side())
		return;
	counts[COUNT_CHECKED]++;
	counts[COUNT_DIFFERED] += differed != 0;
}

void box_lights_report(void)
{
	if (!counts[COUNT_ASKED] && !counts[COUNT_NO_STORE] && !counts[COUNT_REFUSED + BOX_LIGHTS_SPOT] &&
		!counts[COUNT_REFUSED + BOX_LIGHTS_GEL] && !counts[COUNT_REFUSED + BOX_LIGHTS_BAKE] && !counts[COUNT_REFUSED + BOX_LIGHTS_MODE])
		return;
	platform_log("lights box: light draws asked %lu, served from a kept list %lu, lists kept %lu, first sightings %lu, "
		"slots replaced %lu, the store cleared %lu times, walks cut short or lists too long %lu, %lu of %lu triangles in the store; "
		"not asked: spotlights %lu, gels %lu, a baked copy %lu, another mode %lu, out of the store's reach %lu; "
		"lightkeepcheck: %lu kept lists compared with the walk, %lu differed",
		counts[COUNT_ASKED], counts[COUNT_SERVED], counts[COUNT_KEPT], counts[COUNT_FIRST], counts[COUNT_REPLACED],
		counts[COUNT_CLEARED], counts[COUNT_SHORT], counts[COUNT_USED], (unsigned long)BOX_LIGHTS_ROOM,
		counts[COUNT_REFUSED + BOX_LIGHTS_SPOT], counts[COUNT_REFUSED + BOX_LIGHTS_GEL], counts[COUNT_REFUSED + BOX_LIGHTS_BAKE],
		counts[COUNT_REFUSED + BOX_LIGHTS_MODE], counts[COUNT_NO_STORE], counts[COUNT_CHECKED], counts[COUNT_DIFFERED]);
}
