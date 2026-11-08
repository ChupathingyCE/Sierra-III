/* R slice 5, step 3 (docs/redesign_r_slice5.md, "Slice 5 as 2.1"): the
engine's per-part loop skipped, and a shader's tag-only keys found once.

"retainedloop" (on by default; off: "retainedloopold"): render_model_parts walks pass -> region -> permutation ->
geometry -> part and, for each part, finds its shader and calls the draw. All
of that comes from the model's tags and the call's inputs (the model, its
region permutations, its level of detail, the forced shader permutation and
the flags), so for a key it has seen the loop makes the same calls in the same
order. The first time a key is drawn the loop runs and its calls are noted;
after that, models.c (psp_fixups.py, FIXES_RLOOP) makes the noted calls
itself. The calls themselves are unchanged, so the GE's stream is the same.
A key whose loop does anything else (a see-through part: its sort records and
centroid are the frame's; a dynamic buffer; the debug views) isn't kept.

"retainedshader" (below; off: "retainedshaderold"): the bitmap tags' keys the draw's hook looks up,
found once a map.

Both are emptied at each map load. Their tables are fixed: a key or a shader
that doesn't fit runs the old way */
#include <stdlib.h>
#include <string.h>

int psp_keep(const char *name);
void platform_log(const char *format, ...);
extern int psp_map_generation;

#define RL_KEYS 1024        /* slots, a power of two */
#define RL_CALLS 6144       /* noted calls, all keys */
#define RL_REGIONS 16
#define RL_KEY_WORDS (4 + RL_REGIONS / 4)

struct psp_rl_call
{
	void const *shader;
	void const *triangles;
	void const *vertices;
	long triangle_count;
	short permutation;
	short kind; /* 0 the model draw, 1 the shadow draw */
};

struct rl_slot
{
	unsigned int key[RL_KEY_WORDS];
	unsigned short first, count; /* count 0xFFFF: the loop isn't kept */
	unsigned char used;
};

int psp_rl_on = -1;
int psp_rs_on = -1;
static int rl_generation = -1;
static struct rl_slot *rl_slots;
static struct psp_rl_call *rl_calls;
static unsigned int rl_calls_used;
/* the key being noted, its first call and whether it can still be kept */
static int rl_noting = 0, rl_spoiled;
static struct rl_slot *rl_noting_slot;
static unsigned long rl_hits, rl_notes, rl_spoils, rl_full;

static void rl_reset(void)
{
	if (rl_slots)
	{
		if (rl_hits || rl_notes)
			platform_log("rloop: map load %d: %lu loops skipped, %lu noted (%u calls), %lu not kept, %lu without room",
				rl_generation, rl_hits, rl_notes, rl_calls_used, rl_spoils, rl_full);
		memset(rl_slots, 0, RL_KEYS * sizeof(*rl_slots));
	}
	rl_calls_used = 0;
	rl_hits = rl_notes = rl_spoils = rl_full = 0;
	rl_generation = psp_map_generation;
}

static void rl_start(void)
{
	/* on by default: exact (play, seeds and pictures as without, rloop1-2)
	and on the PSP's b30 combat bench the frame 3-5% faster in every stretch
	(lab sierra-hw 42494203180); "retainedloopold" and "retainedshaderold"
	turn them off */
	psp_rl_on = !psp_keep("retainedloopold");
	psp_rs_on = !psp_keep("retainedshaderold");
	if (psp_rl_on)
	{
		rl_slots = calloc(RL_KEYS, sizeof(*rl_slots));
		rl_calls = malloc(RL_CALLS * sizeof(*rl_calls));
		if (!rl_slots || !rl_calls)
		{
			free(rl_slots);
			free(rl_calls);
			rl_slots = NULL;
			rl_calls = NULL;
			psp_rl_on = 0;
			platform_log("rloop: no room for its tables: off");
		}
	}
}

/* render_model, before render_model_parts: the calls to make for this key
(1, with them in *calls and their count), or 0, when the loop runs (and is
noted, when it can be kept) */
int psp_rl_find(void const *model, char const *permutations, int permutation_count, int level, int forced,
	unsigned long flags, struct psp_rl_call const **calls, int *count)
{
	unsigned int key[RL_KEY_WORDS];
	unsigned int hash, probe;
	struct rl_slot *slot;

	if (psp_rl_on < 0)
		rl_start();
	if (!psp_rl_on || permutation_count < 0 || permutation_count > RL_REGIONS)
		return 0;
	if (rl_generation != psp_map_generation)
		rl_reset();
	memset(key, 0, sizeof(key));
	key[0] = (unsigned int)model;
	key[1] = (unsigned int)permutation_count | ((unsigned int)(level & 0xFF) << 8) | ((unsigned int)(forced & 0xFFFF) << 16);
	key[2] = (unsigned int)flags;
	key[3] = 0x524C4F4F; /* a used key's word, so an empty slot never matches */
	memcpy(&key[4], permutations, (size_t)permutation_count);
	hash = key[0] * 2654435761u;
	for (probe = 1; probe < RL_KEY_WORDS; probe++)
		hash = (hash ^ key[probe]) * 2654435761u;
	for (probe = 0; probe < 8; probe++)
	{
		/* the hash's top bits: its low ones follow the pointers' alignment */
		slot = &rl_slots[((hash >> 22) + probe) & (RL_KEYS - 1)];
		if (!slot->used)
			break;
		if (!memcmp(slot->key, key, sizeof(key)))
		{
			if (slot->count == 0xFFFF)
				return 0;
			*calls = &rl_calls[slot->first];
			*count = slot->count;
			/* a running count, as a map's own total comes only at the next load */
			if ((++rl_hits & 8191) == 0)
				platform_log("rloop: %lu loops skipped, %lu noted (%u calls), %lu not kept, %lu without room", rl_hits,
					rl_notes, rl_calls_used, rl_spoils, rl_full);
			return 1;
		}
	}
	if (probe == 8 || rl_calls_used >= RL_CALLS)
	{
		rl_full++;
		return 0;
	}
	memcpy(slot->key, key, sizeof(key));
	slot->used = 1;
	slot->first = (unsigned short)rl_calls_used;
	slot->count = 0;
	rl_noting_slot = slot;
	rl_noting = 1;
	rl_spoiled = 0;
	return 0;
}

/* after render_model_parts: the noted calls kept, or the key marked as one
whose loop always runs */
void psp_rl_noted(void)
{
	if (!rl_noting)
		return;
	rl_noting = 0;
	if (rl_spoiled)
	{
		rl_noting_slot->count = 0xFFFF;
		rl_calls_used = rl_noting_slot->first;
		rl_spoils++;
	}
	else
	{
		rl_noting_slot->count = (unsigned short)(rl_calls_used - rl_noting_slot->first);
		rl_notes++;
	}
}

/* rasterizer.c's draws, while a loop is noted */
void psp_rl_note(int kind, void const *shader, int permutation, void const *triangles, long dynamic_triangles,
	long triangle_count, void const *vertices, long dynamic_vertices)
{
	struct psp_rl_call *call;

	if (!rl_noting || rl_spoiled)
		return;
	if (kind < 0 || dynamic_triangles != -1 || dynamic_vertices != -1 || rl_calls_used >= RL_CALLS)
	{
		rl_spoiled = 1;
		return;
	}
	call = &rl_calls[rl_calls_used++];
	call->shader = shader;
	call->triangles = triangles;
	call->vertices = vertices;
	call->triangle_count = triangle_count;
	call->permutation = (short)permutation;
	call->kind = (short)kind;
}

/* ---------- "retainedshader": a bitmap tag's key, once a map

The draw's hook (psp_fixups.py, rasterizer_model_draw) looks up to three
bitmap tags a part (the base map, the multipurpose map, the reflection's
cube) through tag_get; the answer is the tag's data address, the same all
map, so psp_tex_bitmap_key keeps it. A direct-mapped table: a tag whose slot
holds another is looked up again */

#define RS_SLOTS 1024

static long rs_index[RS_SLOTS];
static unsigned int rs_key[RS_SLOTS];
static int rs_generation = -1;
unsigned long psp_rs_hits, psp_rs_misses;

/* 1 with the key, or 0: look it up (and psp_rs_store it) */
int psp_rs_lookup(long bitmap_tag_index, unsigned int *key)
{
	unsigned int slot;

	if (psp_rs_on < 0)
		rl_start();
	if (!psp_rs_on)
		return 0;
	if (rs_generation != psp_map_generation)
	{
		if (psp_rs_hits || psp_rs_misses)
			platform_log("rshader: map load %d: %lu bitmap keys kept, %lu looked up", rs_generation, psp_rs_hits,
				psp_rs_misses);
		memset(rs_index, 0xFF, sizeof(rs_index));
		rs_generation = psp_map_generation;
		psp_rs_hits = psp_rs_misses = 0;
	}
	slot = ((unsigned int)bitmap_tag_index * 2654435761u) >> 22;
	if (rs_index[slot] == bitmap_tag_index)
	{
		*key = rs_key[slot];
		if ((++psp_rs_hits & 65535) == 0)
			platform_log("rshader: %lu bitmap keys kept, %lu looked up", psp_rs_hits, psp_rs_misses);
		return 1;
	}
	psp_rs_misses++;
	return 0;
}

void psp_rs_store(long bitmap_tag_index, unsigned int key)
{
	unsigned int slot = ((unsigned int)bitmap_tag_index * 2654435761u) >> 22;

	if (psp_rs_on > 0 && rs_generation == psp_map_generation)
	{
		rs_index[slot] = bitmap_tag_index;
		rs_key[slot] = key;
	}
}
