/* The ME's half of the pill sweep, slices 1 and 2 (psp/include/psp_sweepme.h
   says what it is; docs/box_collision.md §11 is its design). Each staged
   biped's gather is a record of the tick's batch (psp_me.c's batches): added
   as its intent ends, handed to the ME two at a time while the main CPU runs
   the next intents, and needed by the gather. Whichever CPU makes a record
   runs the same code on the same inputs, so the answer doesn't depend on who
   made it or when: slice 2's inputs are the world store's copy, which nothing
   writes between one object walk's start and the next. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define BOX_SWEEP_FEATURES 32 /* a record holds far fewer: a list that reaches it is cut short */
#include "box_sweep.h"
#include "box_ws_old.h"
#include "psp_me.h"
#include "psp_sweepme.h"

void platform_psp_log(const char *format, ...);
int psp_keep(const char *name);
void psp_me_cache_refresh(void); /* psp_me.c: on the ME, its whole cache written back and dropped */

/* 48 records of 1.75 KB: c20's held bench stages ~43 a tick, b30 ~12, d20
   ~14 (the lab's sweepmeid1); the leaves are sized from d20's, the map whose
   walks reach the most leaves; the features' room from slice 1's (960 B held
   all but 0.3-4% of the level's halves, sweepmeid2) and the objects' features
   on top, a list past it counted as too big */
#define SWEEPME_RECORDS 48
#define SWEEPME_RECORD_BYTES 1792
#define SWEEPME_ROOM 1472 /* the features' bytes a record holds */

enum { SWEEPME_NOT_MADE, SWEEPME_MADE, SWEEPME_CUT, SWEEPME_LEAVES, SWEEPME_ROOMLESS, SWEEPME_WHOLE, SWEEPME_STATES };
/* why a slice 2 record holds the level's half alone: the box's reasons
   (BOX_SWEEP_WHY_), then the record's own */
enum { SWEEPME_WHY_CUT = BOX_SWEEP_WHYS, SWEEPME_WHY_ROOM, SWEEPME_WHYS };

struct psp_sweepme_record
{
	/* in: written here before the hand-over */
	struct box_collbsp level;
	unsigned int breakable[PSP_SWEEPME_BREAKABLE_WORDS];
	float center[3], radius, height, width;
	unsigned int flags; /* the move's collision flags */
	int structure;
	long biped;
	int whole;          /* slice 2: the whole gather, from the copy */
	unsigned int tick;  /* the copy it reads: the ME drops its cached lines of the last one */
	const void *structure_leaves;
	int structure_leaf_count;
	/* out: the maker's */
	int status, why, leaf_count;
	short count[3];
	unsigned short leaves[PSP_SWEEPME_LEAVES]; /* a leaf index past 65,535 isn't held */
	unsigned char features[SWEEPME_ROOM] __attribute__((aligned(4)));
} __attribute__((aligned(64)));
_Static_assert(sizeof(struct psp_sweepme_record) <= SWEEPME_RECORD_BYTES, "psp_sweepme_record");

/* the box's layouts are the engine's (collision_features.h's asserts hold
   the engine's to these sizes; collisions.c's cut-in checks them again) */
_Static_assert(sizeof(struct box_sweep_sphere) == 0x1C && sizeof(struct box_sweep_cylinder) == 0x28 &&
	sizeof(struct box_sweep_prism) == 0x68, "the box's features in the engine's layouts");

/* each CPU's working memory: the ME's lines are only ever the ME's. The
   world's fields other than the level's are the store's (read through its
   arrays) or zero, as the engine's gather runs: no early-out, no kept walks */
struct psp_sweepme_scratch
{
	struct box_sweep_gather gather;
	struct box_sweep_world world;
} __attribute__((aligned(64)));
static struct psp_sweepme_scratch sweepme_scratch[2];

static unsigned char sweepme_records[SWEEPME_RECORDS][SWEEPME_RECORD_BYTES] __attribute__((aligned(64)));
static unsigned char sweepme_state[SWEEPME_RECORDS];
static struct psp_me_batch sweepme_batch __attribute__((aligned(64)));
static struct box_collbsp sweepme_level;
static unsigned int sweepme_breakable[PSP_SWEEPME_BREAKABLE_WORDS];
static const void *sweepme_structure_leaves;
static int sweepme_structure_leaf_count;
static long sweepme_biped[SWEEPME_RECORDS];
static int sweepme_added, sweepme_open;
int psp_sweepme_check, psp_sweepme_whole, psp_sweepme_whole_check;
/* the ME's own, on a line of its own: the level its cached tag lines belong
   to, and the copy its cached store lines are of */
static struct { const void *planes; unsigned int tick; } sweepme_me __attribute__((aligned(64)));

int psp_sweepme_on = -1;
int psp_sweepme_pending = -1;

static unsigned long sweepme_ticks, sweepme_records_added, sweepme_full, sweepme_by_me, sweepme_taken,
	sweepme_status[SWEEPME_STATES], sweepme_not_found, sweepme_level_changed, sweepme_breakable_changed,
	sweepme_same, sweepme_differ;
static unsigned long sweepme_whole_taken, sweepme_whole_gone, sweepme_why[SWEEPME_WHYS];
/* sweepme2check: whole answers against the engine's gather */
static unsigned long sweepme_whole_same, sweepme_whole_differ, sweepme_whole_moved_only, sweepme_whole_unmatched,
	sweepme_whole_features_unmatched, sweepme_whole_features_moved, sweepme_whole_far[5];
static float sweepme_whole_farthest;

/* a record's answer packed in the engine's layouts: the features' first
   counts[k] of each kind, and the leaves when the gather needs them (the
   level's half alone) */
static int psp_sweepme_pack(struct psp_sweepme_record *r, const struct box_sweep_features *features, const int *counts,
	const int *leaves, int leaf_count)
{
	unsigned int bytes[3], k, offset;
	int i;

	if (counts[0] >= BOX_SWEEP_FEATURES || counts[1] >= BOX_SWEEP_FEATURES || counts[2] >= BOX_SWEEP_FEATURES)
		return SWEEPME_CUT;
	if (leaves)
	{
		for (i = 0; i < leaf_count && i <= PSP_SWEEPME_LEAVES; i++)
			if ((unsigned int)leaves[i] > 0xFFFFu)
				break;
		if (leaf_count > PSP_SWEEPME_LEAVES || i < leaf_count)
			return SWEEPME_LEAVES;
	}
	bytes[0] = (unsigned int)counts[0] * sizeof(struct box_sweep_sphere);
	bytes[1] = (unsigned int)counts[1] * sizeof(struct box_sweep_cylinder);
	bytes[2] = (unsigned int)counts[2] * sizeof(struct box_sweep_prism);
	if (bytes[0] + bytes[1] + bytes[2] > SWEEPME_ROOM)
		return SWEEPME_ROOMLESS;
	r->leaf_count = leaves ? leaf_count : 0;
	for (i = 0; leaves && i < leaf_count; i++)
		r->leaves[i] = (unsigned short)leaves[i];
	offset = 0;
	for (k = 0; k < 3; k++)
	{
		const unsigned int *from = k == 0 ? (const unsigned int *)features->spheres :
			k == 1 ? (const unsigned int *)features->cylinders : (const unsigned int *)features->prisms;
		unsigned int *to = (unsigned int *)(r->features + offset), w;

		/* word copies: a library call here would leave the ME's copy */
		for (w = 0; w < bytes[k] / 4; w++)
			to[w] = from[w];
		r->count[k] = (short)counts[k];
		offset += bytes[k];
	}
	return SWEEPME_MADE;
}

/* the record made, on either CPU */
static void psp_sweepme_make(void *at, const unsigned int *args)
{
	struct psp_sweepme_record *r = (struct psp_sweepme_record *)at;
	int on_me = psp_on_me();
	struct psp_sweepme_scratch *s = &sweepme_scratch[on_me ? 1 : 0];
	struct box_sweep_gather *g = &s->gather;
	int status;

	(void)args;
	/* the ME's cache may still hold the last level's tag lines where a BSP
	switch loaded the new one, and (slice 2) the last copy's store lines,
	which the refresh has since rewritten */
	if (on_me && (r->level.planes != sweepme_me.planes || (r->whole && r->tick != sweepme_me.tick)))
	{
		psp_me_cache_refresh();
		sweepme_me.planes = r->level.planes;
		sweepme_me.tick = r->tick;
	}
	r->why = 0;
	if (!r->whole)
	{
		box_sweep_level_features(&r->level, 256, r->breakable, r->center, r->radius, r->height, r->width, r->structure,
			&g->sphere, &g->features);
		r->status = psp_sweepme_pack(r, &g->features, g->features.count, g->sphere.leaves, g->sphere.leaf_count);
		return;
	}
	s->world.level = &r->level;
	s->world.breakable_count = 256;
	s->world.breakable = r->breakable;
	s->world.structure_leaves = r->structure_leaves;
	s->world.structure_leaf_count = r->structure_leaf_count;
	if (box_sweep_gather(&s->world, r->flags, r->center, r->radius, r->height, r->width, (int)r->biped, g) == 2)
	{
		status = psp_sweepme_pack(r, &g->features, g->features.count, 0, 0);
		if (status == SWEEPME_MADE)
		{
			r->status = SWEEPME_WHOLE;
			return;
		}
		/* the whole list is past what a record holds: its level's half may not be */
		r->why = status == SWEEPME_CUT ? SWEEPME_WHY_CUT : SWEEPME_WHY_ROOM;
	}
	else
		r->why = g->why;
	r->status = psp_sweepme_pack(r, &g->features, g->level_count, g->leaves, g->leaf_count);
}

static void psp_sweepme_report(void)
{
	static const char *const why_name[SWEEPME_WHYS] = { "none", "object", "biped", "model", "physics", "cluster", "leaf",
		"cut short", "too big" };
	unsigned long level_only = 0;
	int k;

	platform_psp_log("sweepme: %lu ticks, %lu records (%lu bipeds past the %d records); made by the ME %lu, here %lu; taken %lu (whole %lu); not taken: cut short %lu, too many leaves %lu, too big %lu, no record %lu, another level %lu, breakable changed %lu",
		sweepme_ticks, sweepme_records_added, sweepme_full, SWEEPME_RECORDS, sweepme_by_me,
		sweepme_records_added - sweepme_by_me, sweepme_taken, sweepme_whole_taken, sweepme_status[SWEEPME_CUT],
		sweepme_status[SWEEPME_LEAVES], sweepme_status[SWEEPME_ROOMLESS], sweepme_not_found, sweepme_level_changed,
		sweepme_breakable_changed);
	if (psp_sweepme_whole)
	{
		char line[256];
		int n = 0;

		for (k = 1; k < SWEEPME_WHYS; k++)
		{
			level_only += sweepme_why[k];
			n += snprintf(line + n, sizeof(line) - (unsigned)n, "%s%s %lu", k > 1 ? ", " : "", why_name[k], sweepme_why[k]);
		}
		platform_psp_log("sweepme2: whole gathers taken %lu, the level's half alone %lu (the copy couldn't answer: %s); not taken, naming an object deleted since %lu",
			sweepme_whole_taken - sweepme_whole_gone, level_only, line, sweepme_whole_gone);
	}
	if (psp_sweepme_check)
		platform_psp_log("sweepmecheck: %lu answers taken were compared with the engine's own level half: %lu the same, %lu differ",
			sweepme_same + sweepme_differ, sweepme_same, sweepme_differ);
	if (psp_sweepme_whole_check)
		platform_psp_log("sweepme2check: %lu whole answers compared with the engine's gather on the live world: %lu the same, %lu differ (%lu only moved or reordered, %lu with features one list lacks); %lu features unmatched, %lu moved; the farthest a feature moved per answer that differs: 0 %lu, <0.01 %lu, <0.1 %lu, <0.5 %lu, >=0.5 %lu; farthest %.4f",
			sweepme_whole_same + sweepme_whole_differ, sweepme_whole_same, sweepme_whole_differ, sweepme_whole_moved_only,
			sweepme_whole_unmatched, sweepme_whole_features_unmatched, sweepme_whole_features_moved,
			sweepme_whole_far[0], sweepme_whole_far[1], sweepme_whole_far[2], sweepme_whole_far[3], sweepme_whole_far[4],
			(double)sweepme_whole_farthest);
}

void psp_sweepme_begin(const struct psp_sweepme_level *level, const void *breakable_flags)
{
	if (psp_sweepme_on < 0)
	{
		psp_sweepme_whole_check = psp_keep("sweepme2check") != 0;
		psp_sweepme_whole = psp_sweepme_whole_check || psp_keep("sweepme2");
		psp_sweepme_check = psp_keep("sweepmecheck") != 0;
		psp_sweepme_on = psp_sweepme_check || psp_sweepme_whole || psp_keep("sweepme");
		/* slice 2 reads the copy's clusters and nodes, which only a copy
		taken with them holds (box_world_store_old.c takes them for sweepme2) */
		if (psp_sweepme_whole && !(box_ws_old_on > 0 && box_ws_old_parts))
		{
			platform_psp_log("sweepme2: the world store's copy isn't taken with its clusters and nodes (a PSP_WSOLD=1 build?); slice 1 only");
			psp_sweepme_whole = psp_sweepme_whole_check = 0;
		}
		if (psp_sweepme_on && !psp_me_batch_init(&sweepme_batch, psp_sweepme_make, sweepme_records, sweepme_state,
			SWEEPME_RECORD_BYTES, SWEEPME_RECORDS, 2, NULL))
		{
			platform_psp_log("sweepme: the batch refused its shapes; off");
			psp_sweepme_on = 0;
		}
		if (psp_sweepme_on)
			platform_psp_log("sweepme: the %s on the ME (%d records of %d B, %u KB with both CPUs' scratch)%s%s",
				psp_sweepme_whole ? "bipeds' whole gathers, from the one-tick-old copy," : "level's half of the bipeds' gathers",
				SWEEPME_RECORDS, SWEEPME_RECORD_BYTES,
				(unsigned)((sizeof(sweepme_records) + sizeof(sweepme_scratch)) / 1024), psp_sweepme_check ? ", checked" : "",
				psp_sweepme_whole_check ? ", whole answers checked" : "");
	}
	sweepme_open = 0;
	sweepme_added = 0;
	psp_sweepme_pending = -1;
	if (!psp_sweepme_on)
		return;
	memset(&sweepme_level, 0, sizeof(sweepme_level));
	sweepme_level.tag_nodes = level->nodes;
	sweepme_level.planes = (const float *)level->planes;
	sweepme_level.leaves = (const struct box_collbsp_leaf *)level->leaves;
	sweepme_level.references = (const struct box_collbsp_reference *)level->references;
	sweepme_level.nodes2d = (const struct box_collbsp_node2d *)level->nodes2d;
	sweepme_level.surfaces = (const struct box_collbsp_surface *)level->surfaces;
	sweepme_level.edges = (const struct box_collbsp_edge *)level->edges;
	sweepme_level.vertices = (const struct box_collbsp_vertex *)level->vertices;
	sweepme_level.node_count = level->node_count;
	sweepme_level.plane_count = level->plane_count;
	sweepme_level.leaf_count = level->leaf_count;
	sweepme_level.reference_count = level->reference_count;
	sweepme_level.node2d_count = level->node2d_count;
	sweepme_level.surface_count = level->surface_count;
	sweepme_level.edge_count = level->edge_count;
	sweepme_level.vertex_count = level->vertex_count;
	sweepme_structure_leaves = level->structure_leaves;
	sweepme_structure_leaf_count = level->structure_leaf_count;
	memcpy(sweepme_breakable, breakable_flags, sizeof(sweepme_breakable));
	/* the copy, refreshed as the object walk began, out to main RAM for the
	ME (its records go out with each hand-over) */
	if (psp_sweepme_whole)
		psp_me_writeback(NULL, ME_RING_ALL_OF_RAM);
	psp_me_batch_begin(&sweepme_batch);
	sweepme_open = 1;
}

int psp_sweepme_add(long biped_index, const float center[3], float radius, float height, float width, unsigned long flags)
{
	struct psp_sweepme_record *r;
	unsigned int index;

	if (!sweepme_open)
		return -1;
	/* two a hand-over while the ME has the most to gain from starting
	early, then four: a batch has 16 jobs, and c20 stages 40-odd */
	sweepme_batch.chunk = sweepme_added < 16 ? 2 : 4;
	r = (struct psp_sweepme_record *)psp_me_batch_add(&sweepme_batch, &index);
	if (!r)
	{
		sweepme_full++;
		return -1;
	}
	r->level = sweepme_level;
	memcpy(r->breakable, sweepme_breakable, sizeof(r->breakable));
	r->center[0] = center[0];
	r->center[1] = center[1];
	r->center[2] = center[2];
	r->radius = radius;
	r->height = height;
	r->width = width;
	r->flags = (unsigned int)flags;
	r->structure = (flags & (1u << BOX_SWEEP_TEST_STRUCTURE)) != 0;
	r->biped = biped_index;
	r->whole = psp_sweepme_whole;
	r->tick = (unsigned int)sweepme_ticks;
	r->structure_leaves = sweepme_structure_leaves;
	r->structure_leaf_count = sweepme_structure_leaf_count;
	r->status = SWEEPME_NOT_MADE;
	sweepme_biped[index] = biped_index;
	sweepme_added = (int)index + 1;
	sweepme_records_added++;
	return (int)index;
}

void psp_sweepme_close(void)
{
	if (sweepme_open)
		psp_me_batch_close(&sweepme_batch);
}

/* the query compared word for word: the record stands only for the very
   gather the engine asks */
int psp_sweepme_find(long biped_index, const float center[3], float radius, float height, float width, unsigned long flags)
{
	const struct psp_sweepme_record *r;
	int k;

	if (!sweepme_open)
		return -1;
	for (k = 0; k < sweepme_added; k++)
		if (sweepme_biped[k] == biped_index)
			break;
	if (k == sweepme_added)
	{
		sweepme_not_found++;
		return -1;
	}
	/* the inputs are the main CPU's own lines, written before the hand-over
	and not written since: readable whoever makes the record */
	r = (const struct psp_sweepme_record *)sweepme_records[k];
	if (memcmp(r->center, center, 12) || memcmp(&r->radius, &radius, 4) || memcmp(&r->height, &height, 4) ||
		memcmp(&r->width, &width, 4) || r->flags != (unsigned int)flags)
	{
		sweepme_not_found++;
		return -1;
	}
	return k;
}

int psp_sweepme_answer(int index, const void *level_planes, const void *breakable_flags, struct psp_sweepme_answer *answer)
{
	const struct psp_sweepme_record *r;
	const unsigned char *p;

	if (!sweepme_open || index < 0 || index >= sweepme_added)
		return 0;
	sweepme_by_me += psp_me_batch_need(&sweepme_batch, (unsigned int)index) ? 1 : 0;
	r = (const struct psp_sweepme_record *)sweepme_records[index];
	if (r->whole && r->status != SWEEPME_WHOLE)
		sweepme_why[r->why > 0 && r->why < SWEEPME_WHYS ? r->why : 0]++;
	if (r->status != SWEEPME_MADE && r->status != SWEEPME_WHOLE)
	{
		sweepme_status[r->status < SWEEPME_STATES ? r->status : 0]++;
		return 0;
	}
	if (r->level.planes != level_planes)
	{
		sweepme_level_changed++;
		return 0;
	}
	if (memcmp(r->breakable, breakable_flags, sizeof(r->breakable)))
	{
		sweepme_breakable_changed++;
		return 0;
	}
	answer->leaf_count = r->leaf_count;
	answer->leaves = r->leaves;
	p = r->features;
	answer->count[0] = r->count[0];
	answer->count[1] = r->count[1];
	answer->count[2] = r->count[2];
	answer->spheres = p;
	p += r->count[0] * sizeof(struct box_sweep_sphere);
	answer->cylinders = p;
	p += r->count[1] * sizeof(struct box_sweep_cylinder);
	answer->prisms = p;
	sweepme_taken++;
	if (r->status == SWEEPME_WHOLE)
	{
		sweepme_whole_taken++;
		return 2;
	}
	return 1;
}

void psp_sweepme_checked(int same)
{
	if (same)
		sweepme_same++;
	else
		sweepme_differ++;
}

void psp_sweepme_whole_gone(void)
{
	sweepme_whole_gone++;
}

void psp_sweepme_whole_checked(int same, int unmatched, int moved, float farthest)
{
	if (same)
	{
		sweepme_whole_same++;
		return;
	}
	sweepme_whole_differ++;
	if (unmatched)
		sweepme_whole_unmatched++;
	else
		sweepme_whole_moved_only++;
	sweepme_whole_features_unmatched += (unsigned long)unmatched;
	sweepme_whole_features_moved += (unsigned long)moved;
	sweepme_whole_far[farthest <= 0.f ? 0 : farthest < 0.01f ? 1 : farthest < 0.1f ? 2 : farthest < 0.5f ? 3 : 4]++;
	if (farthest > sweepme_whole_farthest)
		sweepme_whole_farthest = farthest;
}

void psp_sweepme_end(void)
{
	psp_sweepme_pending = -1;
	if (!sweepme_open)
		return;
	psp_me_batch_finish(&sweepme_batch);
	sweepme_open = 0;
	if (++sweepme_ticks % 300 == 0)
		psp_sweepme_report();
}
