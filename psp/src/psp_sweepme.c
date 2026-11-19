/* The ME's half of the pill sweep, slice 1 (psp/include/psp_sweepme.h says
   what it is; docs/box_collision.md §11 is its design). Each staged biped's
   level half is a record of the tick's batch (psp_me.c's batches): added as
   its intent ends, handed to the ME two at a time while the main CPU runs
   the next intents, and needed by the gather. Whichever CPU makes a record
   runs the same code on the same inputs, so the answer doesn't depend on who
   made it or when. */
#include <stddef.h>
#include <string.h>
#define BOX_SWEEP_FEATURES 32 /* a record holds far fewer: a list that reaches it is cut short */
#include "box_sweep.h"
#include "psp_me.h"
#include "psp_sweepme.h"

void platform_psp_log(const char *format, ...);
int psp_keep(const char *name);
void psp_me_cache_refresh(void); /* psp_me.c: on the ME, its whole cache written back and dropped */

/* 48 records of 1.25 KB: c20's held bench stages ~43 a tick, b30 ~12, d20
   ~14 (the lab's sweepmeid1); the leaves and the features' room are sized
   from d20's, the map whose walks reach the most leaves */
#define SWEEPME_RECORDS 48
#define SWEEPME_RECORD_BYTES 1280
#define SWEEPME_ROOM 960 /* the features' bytes a record holds */

enum { SWEEPME_NOT_MADE, SWEEPME_MADE, SWEEPME_CUT, SWEEPME_LEAVES, SWEEPME_ROOMLESS, SWEEPME_STATES };

struct psp_sweepme_record
{
	/* in: written here before the hand-over */
	struct box_collbsp level;
	unsigned int breakable[PSP_SWEEPME_BREAKABLE_WORDS];
	float center[3], radius, height, width;
	int structure;
	long biped;
	/* out: the maker's */
	int status, leaf_count;
	short count[3];
	unsigned short leaves[PSP_SWEEPME_LEAVES]; /* a leaf index past 65,535 isn't held */
	unsigned char features[SWEEPME_ROOM] __attribute__((aligned(4)));
} __attribute__((aligned(64)));
_Static_assert(sizeof(struct psp_sweepme_record) <= SWEEPME_RECORD_BYTES, "psp_sweepme_record");

/* the box's layouts are the engine's (collision_features.h's asserts hold
   the engine's to these sizes; collisions.c's cut-in checks them again) */
_Static_assert(sizeof(struct box_sweep_sphere) == 0x1C && sizeof(struct box_sweep_cylinder) == 0x28 &&
	sizeof(struct box_sweep_prism) == 0x68, "the box's features in the engine's layouts");

/* each CPU's working memory: the ME's lines are only ever the ME's */
struct psp_sweepme_scratch
{
	struct box_collbsp_sphere_result walk;
	struct box_sweep_features features;
} __attribute__((aligned(64)));
static struct psp_sweepme_scratch sweepme_scratch[2];

static unsigned char sweepme_records[SWEEPME_RECORDS][SWEEPME_RECORD_BYTES] __attribute__((aligned(64)));
static unsigned char sweepme_state[SWEEPME_RECORDS];
static struct psp_me_batch sweepme_batch __attribute__((aligned(64)));
static struct box_collbsp sweepme_level;
static unsigned int sweepme_breakable[PSP_SWEEPME_BREAKABLE_WORDS];
static long sweepme_biped[SWEEPME_RECORDS];
static int sweepme_added, sweepme_open;
int psp_sweepme_check;
/* the ME's own: the level its cached tag lines belong to */
static const void *sweepme_me_planes;

int psp_sweepme_on = -1;
int psp_sweepme_pending = -1;

static unsigned long sweepme_ticks, sweepme_records_added, sweepme_full, sweepme_by_me, sweepme_taken,
	sweepme_status[SWEEPME_STATES], sweepme_not_found, sweepme_level_changed, sweepme_breakable_changed,
	sweepme_same, sweepme_differ;

/* the record made, on either CPU: the level's walk and its features, packed
   into the record in the engine's layouts */
static void psp_sweepme_make(void *at, const unsigned int *args)
{
	struct psp_sweepme_record *r = (struct psp_sweepme_record *)at;
	int on_me = psp_on_me();
	struct psp_sweepme_scratch *s = &sweepme_scratch[on_me ? 1 : 0];
	unsigned int bytes[3], k, offset;
	int i;

	(void)args;
	/* the ME's cache may still hold the last level's tag lines where a BSP
	   switch loaded the new one */
	if (on_me && r->level.planes != sweepme_me_planes)
	{
		psp_me_cache_refresh();
		sweepme_me_planes = r->level.planes;
	}
	box_sweep_level_features(&r->level, 256, r->breakable, r->center, r->radius, r->height, r->width, r->structure,
		&s->walk, &s->features);
	if (s->features.count[0] >= BOX_SWEEP_FEATURES || s->features.count[1] >= BOX_SWEEP_FEATURES ||
		s->features.count[2] >= BOX_SWEEP_FEATURES)
	{
		r->status = SWEEPME_CUT;
		return;
	}
	for (i = 0; i < s->walk.leaf_count && i <= PSP_SWEEPME_LEAVES; i++)
		if ((unsigned int)s->walk.leaves[i] > 0xFFFFu)
			break;
	if (s->walk.leaf_count > PSP_SWEEPME_LEAVES || i < s->walk.leaf_count)
	{
		r->status = SWEEPME_LEAVES;
		return;
	}
	bytes[0] = (unsigned int)s->features.count[0] * sizeof(struct box_sweep_sphere);
	bytes[1] = (unsigned int)s->features.count[1] * sizeof(struct box_sweep_cylinder);
	bytes[2] = (unsigned int)s->features.count[2] * sizeof(struct box_sweep_prism);
	if (bytes[0] + bytes[1] + bytes[2] > SWEEPME_ROOM)
	{
		r->status = SWEEPME_ROOMLESS;
		return;
	}
	r->leaf_count = s->walk.leaf_count;
	for (i = 0; i < s->walk.leaf_count; i++)
		r->leaves[i] = (unsigned short)s->walk.leaves[i];
	offset = 0;
	for (k = 0; k < 3; k++)
	{
		const unsigned int *from = k == 0 ? (const unsigned int *)s->features.spheres :
			k == 1 ? (const unsigned int *)s->features.cylinders : (const unsigned int *)s->features.prisms;
		unsigned int *to = (unsigned int *)(r->features + offset), w;

		/* word copies: a library call here would leave the ME's copy */
		for (w = 0; w < bytes[k] / 4; w++)
			to[w] = from[w];
		r->count[k] = s->features.count[k];
		offset += bytes[k];
	}
	r->status = SWEEPME_MADE;
}

static void psp_sweepme_report(void)
{
	platform_psp_log("sweepme: %lu ticks, %lu records (%lu bipeds past the %d records); made by the ME %lu, here %lu; taken %lu; not taken: cut short %lu, too many leaves %lu, too big %lu, no record %lu, another level %lu, breakable changed %lu",
		sweepme_ticks, sweepme_records_added, sweepme_full, SWEEPME_RECORDS, sweepme_by_me,
		sweepme_records_added - sweepme_by_me, sweepme_taken, sweepme_status[SWEEPME_CUT], sweepme_status[SWEEPME_LEAVES],
		sweepme_status[SWEEPME_ROOMLESS], sweepme_not_found, sweepme_level_changed, sweepme_breakable_changed);
	if (psp_sweepme_check)
		platform_psp_log("sweepmecheck: %lu answers taken were compared with the engine's own level half: %lu the same, %lu differ",
			sweepme_same + sweepme_differ, sweepme_same, sweepme_differ);
}

void psp_sweepme_begin(const struct psp_sweepme_level *level, const void *breakable_flags)
{
	if (psp_sweepme_on < 0)
	{
		psp_sweepme_check = psp_keep("sweepmecheck") != 0;
		psp_sweepme_on = psp_sweepme_check || psp_keep("sweepme");
		if (psp_sweepme_on && !psp_me_batch_init(&sweepme_batch, psp_sweepme_make, sweepme_records, sweepme_state,
			SWEEPME_RECORD_BYTES, SWEEPME_RECORDS, 2, NULL))
		{
			platform_psp_log("sweepme: the batch refused its shapes; off");
			psp_sweepme_on = 0;
		}
		if (psp_sweepme_on)
			platform_psp_log("sweepme: the level's half of the bipeds' gathers on the ME (%d records of %d B, %u KB with both CPUs' scratch)%s",
				SWEEPME_RECORDS, SWEEPME_RECORD_BYTES,
				(unsigned)((sizeof(sweepme_records) + sizeof(sweepme_scratch)) / 1024), psp_sweepme_check ? ", checked" : "");
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
	memcpy(sweepme_breakable, breakable_flags, sizeof(sweepme_breakable));
	psp_me_batch_begin(&sweepme_batch);
	sweepme_open = 1;
}

int psp_sweepme_add(long biped_index, const float center[3], float radius, float height, float width, int structure)
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
	r->structure = structure != 0;
	r->biped = biped_index;
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
   walk the gather asks */
int psp_sweepme_find(long biped_index, const float center[3], float radius, float height, float width, int structure)
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
		memcmp(&r->width, &width, 4) || r->structure != (structure != 0))
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
	if (r->status != SWEEPME_MADE)
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
	return 1;
}

void psp_sweepme_checked(int same)
{
	if (same)
		sweepme_same++;
	else
		sweepme_differ++;
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
