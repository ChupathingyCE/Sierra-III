/* box_lights.c - the lighting box, slice 1: the kept reach (box_lights.h,
docs/box_lights.md section 9).

Everything here but box_lights_report runs where the light pass runs: on the
Media Engine, or on the main CPU when the emulator plays the ME's part. So no
VFPU, no system call and no log line; the counts are left in main RAM, on
lines of their own that only that side writes, for the main CPU to print. */
#include <string.h>
#include "psp_me.h"
#include "box_lights.h"

extern void platform_log(const char *format, ...);

/* The store: 192 KB of the ME's eDRAM at 608 KB, above its code copy's room
and up to the sound tier (psp_me.h's ME_EDRAM_LIGHTS; docs/memory_ledger.md).
A table of keys, then the lit triangles, filled from the start. The table is
sized for a frame's light draws at a third full (d20's swamp asks 159 a
frame): at more than half, keys that find their probes taken turn each other
out every frame and are never seen twice. A key that finds only kept lists in
its probes is turned away, and when that or the triangles' room says the
store is full of lists nobody asks for, the whole store is cleared and fills
again: a light that stays is kept again two frames on. */
#define BOX_LIGHTS_AT ME_EDRAM_LIGHTS
#define BOX_LIGHTS_BYTES (ME_EDRAM_LIGHTS_END - ME_EDRAM_LIGHTS)
#define BOX_LIGHTS_SLOTS 512 /* a power of two */
#define BOX_LIGHTS_PROBES 8
#define BOX_LIGHTS_TURNED 64 /* keys turned away before the store is cleared */
#define BOX_LIGHTS_LONGEST 4095 /* triangles in one list */
#define BOX_LIGHTS_MAGIC 0x4C495431u

typedef char box_lights_below_the_sound_tier[(BOX_LIGHTS_AT + BOX_LIGHTS_BYTES <= ME_EDRAM_SNDTIER) ? 1 : -1];

struct box_lights_slot
{
	struct box_lights_key key;
	unsigned int hash;    /* the key's, compared first */
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
	COUNT_TURNED,   /* no slot: every probe held a kept list */
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
	unsigned int collected, turned;
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
			state.twin = __builtin_malloc(BOX_LIGHTS_BYTES);
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
	state.turned = 0;
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
	struct box_lights_slot *slot, *empty = NULL, *once = NULL;
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
		if (slot->hash != at || memcmp(&slot->key, key, sizeof(*key)))
		{
			if (slot->state == 1 && !once)
				once = slot;
			continue;
		}
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
		/* a key seen once and not again gives its slot up; a kept list does not */
		if (!once)
		{
			counts[COUNT_TURNED]++;
			if (++state.turned >= BOX_LIGHTS_TURNED)
				store_clear(store);
			return BOX_LIGHTS_WALK;
		}
		empty = once;
		counts[COUNT_REPLACED]++;
	}
	empty->key = *key;
	empty->hash = at;
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

/* ---------- "lightkinds": what a bake could take, counted

A light is still when its nine words (place in the world, direction, radius,
cone) have been seen before; a copy is the same when that light has lit it
before with its place in memory, its serial and its origin as they are; an
index list is the same when that light has lit these indices before (a list
of the triangles in view is new whenever the camera moves, though the light
and the copy's vertices are as they were). A light that moves or whose radius is animated shows new words
at every draw. The tables forget: a light seen once gives its entry up first,
so moving lights don't push the still ones out. */
#define KIND_LIGHTS 128
#define KIND_PAIRS 2048 /* a power of two */
#define KIND_PROBES 8
enum { KIND_NEW, KIND_SAME, KIND_LIST, KIND_COPY, KIND_DRAW, KIND_OUTCOMES };
static struct
{
	unsigned int words[9], hash, seen;
} kind_light[KIND_LIGHTS];
static struct
{
	unsigned int light, indices, count, copy; /* indices 0: empty */
} kind_pair[KIND_PAIRS];
static struct
{
	unsigned int light, stream, copy; /* stream 0: empty */
} kind_copy[KIND_PAIRS];
static unsigned long kinds[24] __attribute__((aligned(64))); /* [3 forms][5 outcomes], then baked copies, lights forgotten, pairs replaced */
static unsigned int kind_next;

static unsigned int words_hash(const unsigned int *word, int count)
{
	unsigned int hash = 2166136261u;
	int k;

	for (k = 0; k < count; k++)
		hash = (hash ^ word[k]) * 16777619u;
	return hash ^ (hash >> 15);
}

void box_lights_kind(int form, const float *world, const float *forward, float radius, float falloff, float cutoff,
	const void *stream, unsigned int serial, const float *origin, const void *indices, long count, int baked)
{
	unsigned int words[9], copy_words[5], hash, copy, at, probe, k;
	int outcome, found = -1;

	if (!pass_side())
		return;
	memcpy(&words[0], world, 3 * sizeof(float));
	memcpy(&words[3], forward, 3 * sizeof(float));
	memcpy(&words[6], &radius, sizeof(float));
	memcpy(&words[7], &falloff, sizeof(float));
	memcpy(&words[8], &cutoff, sizeof(float));
	hash = words_hash(words, 9);
	copy_words[0] = (unsigned int)stream;
	copy_words[1] = serial;
	memcpy(&copy_words[2], origin, 3 * sizeof(float));
	copy = words_hash(copy_words, 5);
	kinds[15] += baked != 0;
	for (k = 0; k < KIND_LIGHTS && found < 0; k++)
		if (kind_light[k].seen && kind_light[k].hash == hash && !memcmp(kind_light[k].words, words, sizeof(words)))
			found = (int)k;
	if (found < 0)
	{
		/* a new light takes an empty entry, else one seen once, else the next in turn */
		for (k = 0; k < KIND_LIGHTS && found < 0; k++)
			if (!kind_light[k].seen)
				found = (int)k;
		for (k = 0; k < KIND_LIGHTS && found < 0; k++)
			if (kind_light[(kind_next + k) % KIND_LIGHTS].seen == 1)
				found = (int)((kind_next + k) % KIND_LIGHTS);
		if (found < 0)
			found = (int)(kind_next % KIND_LIGHTS);
		if (kind_light[found].seen)
			kinds[16]++;
		kind_next = (unsigned int)found + 1;
		memcpy(kind_light[found].words, words, sizeof(words));
		kind_light[found].hash = hash;
		kind_light[found].seen = 1;
		outcome = KIND_NEW;
	}
	else
	{
		if (kind_light[found].seen < 0x7FFFFFFF)
			kind_light[found].seen++;
		outcome = KIND_DRAW;
	}
	/* the light on this copy, then on this index list; both are kept for a new light too */
	{
		int copy_seen = 0, copy_same = 0, list_seen = 0;

		at = hash ^ ((unsigned int)stream >> 2) * 2654435761u;
		for (probe = 0; probe < KIND_PROBES; probe++)
		{
			unsigned int slot = (at + probe) & (KIND_PAIRS - 1);

			if (kind_copy[slot].stream == (unsigned int)stream && kind_copy[slot].light == hash)
			{
				copy_seen = 1;
				copy_same = kind_copy[slot].copy == copy;
				kind_copy[slot].copy = copy;
				break;
			}
			if (!kind_copy[slot].stream)
			{
				kind_copy[slot].light = hash;
				kind_copy[slot].stream = (unsigned int)stream;
				kind_copy[slot].copy = copy;
				break;
			}
		}
		if (probe == KIND_PROBES)
		{
			unsigned int slot = at & (KIND_PAIRS - 1);

			kind_copy[slot].light = hash;
			kind_copy[slot].stream = (unsigned int)stream;
			kind_copy[slot].copy = copy;
			kinds[17]++;
		}
		at = (hash ^ ((unsigned int)indices >> 2) * 2654435761u ^ (unsigned int)count * 40503u);
		for (probe = 0; probe < KIND_PROBES; probe++)
		{
			unsigned int slot = (at + probe) & (KIND_PAIRS - 1);

			if (kind_pair[slot].indices == (unsigned int)indices && kind_pair[slot].light == hash && kind_pair[slot].count == (unsigned int)count)
			{
				list_seen = kind_pair[slot].copy == copy;
				kind_pair[slot].copy = copy;
				break;
			}
			if (!kind_pair[slot].indices)
				break;
		}
		if (probe == KIND_PROBES)
		{
			probe = 0;
			kinds[18]++;
		}
		if (!list_seen)
		{
			unsigned int slot = (at + probe) & (KIND_PAIRS - 1);

			kind_pair[slot].light = hash;
			kind_pair[slot].indices = (unsigned int)indices;
			kind_pair[slot].count = (unsigned int)count;
			kind_pair[slot].copy = copy;
		}
		if (outcome != KIND_NEW)
			outcome = !copy_seen ? KIND_DRAW : !copy_same ? KIND_COPY : list_seen ? KIND_SAME : KIND_LIST;
	}
	kinds[(form < 0 || form > 2 ? 0 : form) * KIND_OUTCOMES + outcome]++;
}

void box_lights_report(void)
{
	{
		unsigned long all = 0;
		int k;

		for (k = 0; k < 15; k++)
			all += kinds[k];
		if (all)
			platform_log("lights box: lightkinds: light draws by the light's form, each as (the light's words new | still, on the same copy and "
				"index list | still, the same copy, another index list | still, the copy remade | still, a copy new to it): "
				"point lights %lu | %lu | %lu | %lu | %lu; with a gel %lu | %lu | %lu | %lu | %lu; spotlights %lu | %lu | %lu | %lu | %lu; "
				"of all, on a baked copy %lu; lights forgotten %lu, copy pairs replaced %lu, list pairs replaced %lu",
				kinds[0], kinds[1], kinds[2], kinds[3], kinds[4], kinds[5], kinds[6], kinds[7], kinds[8], kinds[9], kinds[10], kinds[11],
				kinds[12], kinds[13], kinds[14], kinds[15], kinds[16], kinds[17], kinds[18]);
	}
	if (!counts[COUNT_ASKED] && !counts[COUNT_NO_STORE] && !counts[COUNT_REFUSED + BOX_LIGHTS_SPOT] &&
		!counts[COUNT_REFUSED + BOX_LIGHTS_GEL] && !counts[COUNT_REFUSED + BOX_LIGHTS_BAKE] && !counts[COUNT_REFUSED + BOX_LIGHTS_MODE])
		return;
	platform_log("lights box: light draws asked %lu, served from a kept list %lu, lists kept %lu, first sightings %lu, "
		"slots replaced %lu, turned away with their probes full of kept lists %lu, the store cleared %lu times, "
		"walks cut short or lists too long %lu, %lu of %lu triangles in the store; "
		"not asked: spotlights %lu, gels %lu, a baked copy %lu, another mode %lu, out of the store's reach %lu; "
		"lightkeepcheck: %lu kept lists compared with the walk, %lu differed",
		counts[COUNT_ASKED], counts[COUNT_SERVED], counts[COUNT_KEPT], counts[COUNT_FIRST], counts[COUNT_REPLACED],
		counts[COUNT_TURNED], counts[COUNT_CLEARED], counts[COUNT_SHORT], counts[COUNT_USED], (unsigned long)BOX_LIGHTS_ROOM,
		counts[COUNT_REFUSED + BOX_LIGHTS_SPOT], counts[COUNT_REFUSED + BOX_LIGHTS_GEL], counts[COUNT_REFUSED + BOX_LIGHTS_BAKE],
		counts[COUNT_REFUSED + BOX_LIGHTS_MODE], counts[COUNT_NO_STORE], counts[COUNT_CHECKED], counts[COUNT_DIFFERED]);
}
