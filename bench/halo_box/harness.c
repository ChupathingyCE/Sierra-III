/*
The box harness, its portable part (bench/halo_box/README.md).

For each tick of a capture, and for the box and the null box beside it:

  - the check: every entity run once, its output compared with the capture's;
  - warm: the tick's entities back to back three times; the first is
    discarded, the second timed whole (two clock reads a tick), the third
    with a clock read either side of each entity (the spread);
  - emptied: both caches emptied before each entity, a clock read either side;
  - crowded: both caches filled with other work's lines before each entity.

With the counters asked for, each mode has one more pass, alone: the CPU's
own counters read either side of each entity (cycles, cycles stalled, cache
misses). Its times aren't used: a read costs about 3 us.

The three timed modes run once alone and once for each GE duty asked for.
The null box's figure in the same mode is the harness's own cost there, and
is printed and subtracted.

A box whose design is one walk over a tick's entities (adapter.batch) is run
once a tick, and its figures are divided by the tick's entity count.

With the Media Engine asked for (options.on_me), each mode's per-entity pass
also runs on the ME, in each of its places (README, "On the Media Engine"): from main
RAM; from its eDRAM; and from its eDRAM with the main CPU's own loop timed
beside it. Every output of every such pass is checked as the main CPU's are.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "harness.h"
#include "../../psp/src/psp_cap1.h"

#define KINDS_MOST 16
#define SAMPLE_MOST 65535u

static const char *const mode_names[MODES] = { "warm", "emptied", "crowded" };

/* For the emulator's cache models, which split their exact counts by a word
the program names (the lab's PSP_DCACHE_PHASE): 0 the harness, then mode + 1
while the box runs an entity in its per-entity pass, alone. The null box
stores 0 the same way, so the two stores cancel in the subtraction; on a PSP
nothing reads it. */
volatile unsigned int box_phase;

struct series
{
	unsigned short *us; /* one a timed call: an entity, or a tick for a batching box */
	unsigned int count, room, clipped;
};

struct figures
{
	struct series calls[MODES][DUTIES_MOST];
	/* the warm mode's whole-tick pass: microseconds and entities, summed */
	double whole_us[DUTIES_MOST];
	unsigned int whole_entities[DUTIES_MOST];
	unsigned int short_spans[MODES][DUTIES_MOST]; /* passes the GE's list ended before */
	double ge_span[MODES][DUTIES_MOST], ge_other[MODES][DUTIES_MOST]; /* the GE's spans, and their part off main RAM, summed */
	double counters[MODES][COUNTERS]; /* summed over the counted pass's calls */
	unsigned int counted[MODES];      /* entities behind them */
	unsigned char *kind;       /* an entity's kind, in the order sampled */
	unsigned int *call_entities; /* entities behind each timed call (1, or a tick's count) */
	unsigned int calls_noted, calls_room;
	/* the Media Engine's passes: its cycles a call, by place and mode */
	struct cycle_series { unsigned int *cycles; unsigned int count, room; } me[PLACES_ME][MODES];
	struct me_busy busy; /* the main CPU's loop beside the ME's busy-place passes */
	/* the per-entity jobs (mejob), by place and mode: the main CPU's µs over
	the hand-over and the wait, the ME's own cycles for the call, and the
	main CPU's write-back before each */
	struct cycle_series job_us[PLACES_ME][MODES], job_cycles[PLACES_ME][MODES], job_writeback[PLACES_ME][MODES];
};

struct state
{
	const struct box_adapter *box;
	struct box_adapter null;
	const struct harness_options *options;
	struct figures of_box, of_null;
	unsigned int ticks, entities, kind_count[KINDS_MOST];
	/* the check */
	unsigned int checked, outside, first_tick, first_entity, first_element;
	float largest_absolute, largest_relative;
	int owned_bytes;
	unsigned int static_bytes, capture_bytes, setup_us;
	/* the Media Engine */
	unsigned int me_checked[PLACES_ME], me_outside[PLACES_ME], me_first_tick[PLACES_ME], me_first_entity[PLACES_ME];
	int me_stopped;        /* a pass on the ME failed: no more of them */
	unsigned int *me_cycles, me_cycles_room;
	/* the jobs' three arrays, each on lines of its own (plat_me_jobs) */
	unsigned int *job_arrays, job_room;
	unsigned int me_job_checked[PLACES_ME], me_job_outside[PLACES_ME], me_job_first_tick[PLACES_ME], me_job_first_entity[PLACES_ME];
	struct me_busy busy_alone; /* the main CPU's loop with the ME idle */
};

static void *grow(void *old, unsigned int *room, unsigned int need, unsigned int size)
{
	if (need <= *room)
		return old;
	*room = need + need / 2 + 1024;
	old = realloc(old, (size_t)*room * size);
	if (!old)
	{
		plat_out("STOPPED: out of memory for %u records of %u bytes", *room, size);
		exit(1);
	}
	return old;
}

static void sample(struct series *s, unsigned int us)
{
	s->us = (unsigned short *)grow(s->us, &s->room, s->count + 1, sizeof(*s->us));
	if (us > SAMPLE_MOST)
	{
		us = SAMPLE_MOST;
		s->clipped++;
	}
	s->us[s->count++] = (unsigned short)us;
}

/* ---- the comparison ---- */

static int compare_floats(const struct box_adapter *box, const void *got, const void *want, unsigned int length,
	float *largest_absolute, float *largest_relative, unsigned int *element)
{
	unsigned int k, bad = 0;

	*largest_absolute = *largest_relative = 0;
	*element = 0;
	if (box->tolerance == BOX_EXACT)
	{
		const unsigned char *a = (const unsigned char *)got, *b = (const unsigned char *)want;

		for (k = 0; k < length; k++)
			if (a[k] != b[k])
			{
				*element = k / 4;
				return 1;
			}
		return 0;
	}
	for (k = 0; k + 4 <= length; k += 4)
	{
		float a, b, difference, scale, relative;

		memcpy(&a, (const char *)got + k, 4);
		memcpy(&b, (const char *)want + k, 4);
		if (a == b || !memcmp(&a, &b, 4))
			continue;
		difference = fabsf(a - b);
		scale = fabsf(a) > fabsf(b) ? fabsf(a) : fabsf(b);
		relative = scale > 0 ? difference / scale : 0;
		/* a NaN on one side only compares false with everything */
		if (!(difference <= box->eps_absolute) && !(box->tolerance == BOX_RELATIVE && relative <= box->eps_relative))
		{
			if (!bad)
				*element = k / 4;
			bad = 1;
		}
		if (difference > *largest_absolute || difference != difference)
			*largest_absolute = difference;
		if (relative > *largest_relative)
			*largest_relative = relative;
	}
	return (int)bad;
}

/* every output of the tick's entities against the capture's: into the
counters given, and the first offender noted when `first_tick` isn't null */
static void check_outputs(struct state *st, const struct box_tick *tick, unsigned int *checked, unsigned int *outside,
	unsigned int *first_tick, unsigned int *first_entity, unsigned int *first_element)
{
	const struct box_adapter *box = st->box;
	unsigned int i;

	for (i = 0; i < tick->entities; i++)
	{
		const void *got;
		unsigned int length, element = 0;
		float absolute = 0, relative = 0;
		int bad;

		box->output((int)i, &got, &length);
		if (length != tick->entity[i].output_length)
			bad = 1;
		else if (!length)
			bad = 0;
		else if (box->compare)
			bad = box->compare((int)i, got, tick->entity[i].output, length, &absolute, &relative, &element);
		else
			bad = compare_floats(box, got, tick->entity[i].output, length, &absolute, &relative, &element);
		(*checked)++;
		if (absolute > st->largest_absolute || absolute != absolute)
			st->largest_absolute = absolute;
		if (relative > st->largest_relative)
			st->largest_relative = relative;
		if (bad && !(*outside)++ && first_tick)
		{
			*first_tick = tick->tick;
			*first_entity = tick->entity[i].id;
			if (first_element)
				*first_element = length != tick->entity[i].output_length ? 0xffffffffu : element;
		}
	}
}

static void check_tick(struct state *st, const struct box_tick *tick)
{
	const struct box_adapter *box = st->box;
	unsigned int i;

	box->tick(tick);
	if (box->batch)
		box->run(BOX_ALL);
	else
		for (i = 0; i < tick->entities; i++)
			box->run((int)i);
	check_outputs(st, tick, &st->checked, &st->outside, &st->first_tick, &st->first_entity, &st->first_element);
}

/* ---- the timed passes ---- */

/* one pass over a tick with a clock read either side of each call; returns
the pass's whole span, clock reads and the work between entities included */
static unsigned int pass_each(const struct box_adapter *box, const struct box_tick *tick, int mode, struct series *into, unsigned int phase)
{
	unsigned int i, calls = box->batch ? 1 : tick->entities, start = plat_now();

	for (i = 0; i < calls; i++)
	{
		unsigned int before, after;

		if (mode == MODE_EMPTIED)
			plat_empty();
		else if (mode == MODE_CROWDED)
			plat_crowd();
		before = plat_now();
		box_phase = phase;
		box->run(box->batch ? BOX_ALL : (int)i);
		box_phase = 0;
		after = plat_now();
		if (into)
			sample(into, after - before);
	}
	return plat_now() - start;
}

static void pass_counted(const struct box_adapter *box, const struct box_tick *tick, int mode, struct figures *f)
{
	unsigned int i, k, calls = box->batch ? 1 : tick->entities, before[COUNTERS], after[COUNTERS];

	for (i = 0; i < calls; i++)
	{
		if (mode == MODE_EMPTIED)
			plat_empty();
		else if (mode == MODE_CROWDED)
			plat_crowd();
		plat_counters(before);
		box->run(box->batch ? BOX_ALL : (int)i);
		plat_counters(after);
		for (k = 0; k < COUNTERS; k++)
			f->counters[mode][k] += (double)(unsigned int)(after[k] - before[k]); /* 32 bits, wrapping */
	}
	f->counted[mode] += tick->entities;
}

static unsigned int pass_whole(const struct box_adapter *box, const struct box_tick *tick)
{
	unsigned int i, before = plat_now();

	if (box->batch)
		box->run(BOX_ALL);
	else
		for (i = 0; i < tick->entities; i++)
			box->run((int)i);
	return plat_now() - before;
}

/* ---- the Media Engine's passes ---- */

static const char *const place_names[PLACES_ME] = { "ram", "edram", "edram, CPU busy" };

static void *where_ram(void *function) { return function; }
static void *where_edram(void *function) { return plat_me_where(function, PLACE_ME_EDRAM); }

static void sample_cycles(struct cycle_series *s, const unsigned int *cycles, unsigned int count)
{
	s->cycles = (unsigned int *)grow(s->cycles, &s->room, s->count + count, sizeof(*s->cycles));
	memcpy(s->cycles + s->count, cycles, count * sizeof(*cycles));
	s->count += count;
}

/* the per-entity jobs (mejob): every entity of the tick handed to the ME as
a job of its own, timed from the main CPU, in the place and mode given; the
outputs checked after, as the passes' are */
static void me_jobs(struct state *st, const struct box_adapter *box, struct figures *f, const struct box_tick *tick, int place,
	int mode)
{
	/* three arrays of a line-rounded stride, from a line boundary */
	unsigned int stride = (tick->entities * 4 + 63) / 64 * 16, *base, *us, *cycles, *writeback;
	int result;

	st->job_arrays = (unsigned int *)grow(st->job_arrays, &st->job_room, 3 * stride + 16, sizeof(unsigned int));
	base = (unsigned int *)(((unsigned long)st->job_arrays + 63) & ~(unsigned long)63);
	us = base;
	cycles = base + stride;
	writeback = base + 2 * stride;
	if (st->ticks == 1)
		plat_step("tick %u: %s box's jobs on the ME, %s, %s", tick->tick, f == &st->of_box ? box->name : "the null", place_names[place],
			mode_names[mode]);
	box->tick(tick);
	if (box->me_aim)
		box->me_aim(place == PLACE_ME_RAM ? where_ram : where_edram);
	result = plat_me_jobs(box->run, tick->entities, mode, place, us, cycles, writeback);
	if (box->me_aim)
		box->me_aim(where_ram);
	if (result < 0)
	{
		plat_out("ME: STOPPED at tick %u, %s box's jobs, %s, %s, entity %u: %s", tick->tick, f == &st->of_box ? box->name : "the null",
			place_names[place], mode_names[mode], plat_me_at(),
			result == -1 ? "the ME didn't finish (a system call, a VFPU instruction or a fault on its path?)" : "the place can't run");
		st->me_stopped = 1;
		return;
	}
	sample_cycles(&f->job_us[place][mode], us, tick->entities);
	sample_cycles(&f->job_cycles[place][mode], cycles, tick->entities);
	sample_cycles(&f->job_writeback[place][mode], writeback, tick->entities);
	if (f == &st->of_box)
		check_outputs(st, tick, &st->me_job_checked[place], &st->me_job_outside[place], &st->me_job_first_tick[place],
			&st->me_job_first_entity[place], 0);
}

/* each mode's per-entity pass on the ME, in each place it can run; the box's
outputs checked after each. A pass that fails names itself, and the ME's
passes stop for the rest of the run */
static void me_passes(struct state *st, const struct box_adapter *box, struct figures *f, const struct box_tick *tick)
{
	int place, mode;

	if (!st->options->on_me || st->me_stopped || box->batch || !tick->entities)
		return;
	st->me_cycles = (unsigned int *)grow(st->me_cycles, &st->me_cycles_room, tick->entities, sizeof(*st->me_cycles));
	for (place = 0; place < PLACES_ME && !st->me_stopped; place++)
	{
		if (!((st->options->on_me >> place) & 1) || (place != PLACE_ME_RAM && !plat_me_copied()))
			continue;
		for (mode = 0; mode < MODES && !st->me_stopped; mode++)
		{
			int result;

			if (st->ticks == 1)
				plat_step("tick %u: %s box on the ME, %s, %s", tick->tick, f == &st->of_box ? box->name : "the null", place_names[place],
					mode_names[mode]);
			box->tick(tick);
			if (box->me_aim)
				box->me_aim(place == PLACE_ME_RAM ? where_ram : where_edram);
			result = plat_me_pass(box->run, box->me_place, tick->entities, mode, place, st->me_cycles,
				place == PLACE_ME_BUSY && f == &st->of_box ? &f->busy : 0);
			if (box->me_aim)
				box->me_aim(where_ram);
			if (result < 0)
			{
				plat_out("ME: STOPPED at tick %u, %s box, %s, %s, call %u: %s", tick->tick, f == &st->of_box ? box->name : "the null",
					place_names[place], mode_names[mode], plat_me_at(),
					result == -1 ? "the ME didn't finish (a system call, a VFPU instruction or a fault on its path?)"
					: result == -3 ? "the box's data didn't fit its eDRAM room" : "the place can't run");
				if (result == -3)
					plat_out("ME: the box's me_place answered %d", plat_me_placed());
				st->me_stopped = 1;
				break;
			}
			sample_cycles(&f->me[place][mode], st->me_cycles, tick->entities);
			if (f == &st->of_box)
				check_outputs(st, tick, &st->me_checked[place], &st->me_outside[place], &st->me_first_tick[place],
					&st->me_first_entity[place], 0);
			if (st->options->me_jobs && place != PLACE_ME_BUSY)
				me_jobs(st, box, f, tick, place, mode);
		}
	}
}

static void time_tick(struct state *st, const struct box_adapter *box, struct figures *f, const struct box_tick *tick)
{
	int mode, d, tries;
	unsigned int alone[MODES] = { 0, 0, 0 }, span, i;

	if (!tick->entities)
		return;
	if (f == &st->of_box)
	{
		/* which kind each sampled entity was, and how many entities each call stood for */
		unsigned int calls = box->batch ? 1 : tick->entities;

		f->kind = (unsigned char *)grow(f->kind, &f->calls_room, f->calls_noted + calls, 1);
		for (i = 0; i < calls; i++)
			f->kind[f->calls_noted + i] = box->batch ? 0 : (unsigned char)(tick->entity[i].kind < KINDS_MOST ? tick->entity[i].kind : KINDS_MOST - 1);
		f->calls_noted += calls;
	}
	for (d = 0; d < st->options->duty_count; d++)
		for (mode = 0; mode < MODES; mode++)
			for (tries = 0; tries < 3; tries++)
			{
				struct series *into = &f->calls[mode][d];
				unsigned int kept = into->count, whole = 0;
				int duty = st->options->duties[d], covered = 1;

				/* the first tick walks every path the run will take: if one hangs, the log names it */
				if (st->ticks == 1)
					plat_step("tick %u: %s box, %s, GE %d%%, try %d", tick->tick, f == &st->of_box ? box->name : "the null",
						mode_names[mode], duty, tries + 1);
				if (duty)
					/* a miss beside the GE takes up to three times as long */
					plat_ge_start(duty, (alone[mode] * 3 + 1000) << tries);
				if (mode == MODE_WARM)
				{
					box->tick(tick);
					pass_whole(box, tick);
					box->tick(tick);
					whole = pass_whole(box, tick);
				}
				box->tick(tick);
				span = pass_each(box, tick, mode, into, !duty && f == &st->of_box ? (unsigned int)mode + 1 : 0);
				if (duty)
				{
					covered = plat_ge_stop();
					{
						double span_us, other_us;

						if (covered && plat_ge_spent(&span_us, &other_us))
						{
							f->ge_span[mode][d] += span_us;
							f->ge_other[mode][d] += other_us;
						}
					}
				}
				else
				{
					alone[mode] = span + whole * 2;
					if (st->options->counted)
					{
						if (mode == MODE_WARM)
						{
							/* the pass before it had clock reads between entities: once more to settle */
							box->tick(tick);
							pass_whole(box, tick);
						}
						box->tick(tick);
						pass_counted(box, tick, mode, f);
					}
				}
				if (covered || tries == 2)
				{
					if (!covered)
						f->short_spans[mode][d]++;
					if (mode == MODE_WARM)
					{
						f->whole_us[d] += whole;
						f->whole_entities[d] += tick->entities;
					}
					break;
				}
				into->count = kept; /* the GE's list ended early: again, with a longer one */
			}
	me_passes(st, box, f, tick);
}

/* ---- the capture ---- */

struct capture
{
	FILE *file;
	struct cap1_header header;
	unsigned char *statics;
	struct box_static *static_index;
	unsigned int static_count;
	unsigned char *tick_bytes;
	unsigned int tick_room;
	struct box_entity *entities;
	unsigned int entity_room;
};

static int capture_open(struct capture *c, const char *path, const char *box_name)
{
	unsigned int at, count = 0;

	memset(c, 0, sizeof(*c));
	c->file = fopen(path, "rb");
	if (!c->file)
		return plat_out("capture: %s can't be opened", path), 0;
	if (fread(&c->header, 1, sizeof(c->header), c->file) != sizeof(c->header) || c->header.magic != CAP1_MAGIC ||
		c->header.version != CAP1_VERSION || (c->header.static_bytes & 3u))
		return plat_out("capture: %s isn't a CAP1 version %u file", path, CAP1_VERSION), 0;
	if (strncmp(c->header.box, box_name, sizeof(c->header.box)))
		return plat_out("capture: %s is box '%.8s', not '%s': skipped", path, c->header.box, box_name), 0;
	c->statics = (unsigned char *)malloc(c->header.static_bytes + 4);
	if (!c->statics || fread(c->statics, 1, c->header.static_bytes, c->file) != c->header.static_bytes)
		return plat_out("capture: %s: its static section (%u bytes) can't be read", path, c->header.static_bytes), 0;
	for (at = 0; at + sizeof(struct cap1_static) <= c->header.static_bytes; count++)
	{
		struct cap1_static record;

		memcpy(&record, c->statics + at, sizeof(record));
		if (record.length > c->header.static_bytes - at - sizeof(record))
			return plat_out("capture: %s: static record %u runs past its section", path, count), 0;
		at += sizeof(record) + ((record.length + 3u) & ~3u);
	}
	c->static_count = count;
	c->static_index = (struct box_static *)malloc((count + 1) * sizeof(*c->static_index));
	if (!c->static_index)
		return 0;
	for (at = 0, count = 0; count < c->static_count; count++)
	{
		struct cap1_static record;

		memcpy(&record, c->statics + at, sizeof(record));
		c->static_index[count].key = record.key;
		c->static_index[count].length = record.length;
		c->static_index[count].bytes = c->statics + at + sizeof(record);
		at += sizeof(record) + ((record.length + 3u) & ~3u);
	}
	return 1;
}

/* the next tick section; 0 at the file's end or on a damaged one */
static int capture_tick(struct capture *c, struct box_tick *tick, const char *path)
{
	struct cap1_tick head;
	unsigned int at, i, room;
	long position = ftell(c->file);

	if (position & 63)
		fseek(c->file, (position + 63) & ~63l, SEEK_SET);
	if (fread(&head, 1, sizeof(head), c->file) != sizeof(head))
		return 0;
	if (head.magic != CAP1_TICK)
		return plat_out("capture: %s: no tick section at %ld", path, (position + 63) & ~63l), 0;
	room = c->tick_room;
	c->tick_bytes = (unsigned char *)grow(c->tick_bytes, &room, head.bytes + 4, 1);
	c->tick_room = room;
	room = c->entity_room;
	c->entities = (struct box_entity *)grow(c->entities, &room, head.entities + 1, sizeof(*c->entities));
	c->entity_room = room;
	if (fread(c->tick_bytes, 1, head.bytes, c->file) != head.bytes)
		return plat_out("capture: %s: tick %u is cut short", path, head.tick), 0;
	for (at = 0, i = 0; i < head.entities; i++)
	{
		struct cap1_entity record;
		struct box_entity *e = &c->entities[i];
		unsigned int in, out;

		if (at + sizeof(record) > head.bytes)
			return plat_out("capture: %s: tick %u: entity %u starts past the section", path, head.tick, i), 0;
		memcpy(&record, c->tick_bytes + at, sizeof(record));
		in = (record.input_length + 3u) & ~3u;
		out = (record.output_length + 3u) & ~3u;
		if (in > head.bytes || out > head.bytes || at + sizeof(record) + in + out > head.bytes)
			return plat_out("capture: %s: tick %u: entity %u runs past the section", path, head.tick, i), 0;
		e->id = record.id;
		e->kind = record.kind;
		e->flags = record.flags;
		e->input_length = record.input_length;
		e->output_length = record.output_length;
		e->input = c->tick_bytes + at + sizeof(record);
		e->output = e->input + in;
		at += sizeof(record) + in + out;
	}
	tick->tick = head.tick;
	tick->entities = head.entities;
	tick->entity = c->entities;
	return 1;
}

static void capture_close(struct capture *c)
{
	if (c->file)
		fclose(c->file);
	free(c->statics);
	free(c->static_index);
	free(c->tick_bytes);
	free(c->entities);
	memset(c, 0, sizeof(*c));
}

/* ---- the figures ---- */

static int by_size(const void *a, const void *b)
{
	return (int)*(const unsigned short *)a - (int)*(const unsigned short *)b;
}

struct summary { double mean, median, high, worst; unsigned int count; };

/* `us` is sorted in place: only call once everything that reads it by position has */
static struct summary summarise(struct series *s)
{
	struct summary out = { 0, 0, 0, 0, s->count };
	double sum = 0;
	unsigned int i;

	if (!s->count)
		return out;
	for (i = 0; i < s->count; i++)
		sum += s->us[i];
	qsort(s->us, s->count, sizeof(*s->us), by_size);
	out.mean = sum / s->count;
	out.median = s->us[s->count / 2];
	out.high = s->us[(unsigned int)((double)s->count * 0.95)  < s->count ? (unsigned int)((double)s->count * 0.95) : s->count - 1];
	out.worst = s->us[s->count - 1];
	return out;
}

static int by_cycles(const void *a, const void *b)
{
	unsigned int x = *(const unsigned int *)a, y = *(const unsigned int *)b;

	return x < y ? -1 : x > y;
}

/* as summarise, over a series of counts in µs at `per_us` counts a µs; sorts in place */
static struct summary summarise_counts(struct cycle_series *s, double per_us)
{
	struct summary out = { 0, 0, 0, 0, s->count };
	double sum = 0;
	unsigned int i, high;

	if (!s->count)
		return out;
	for (i = 0; i < s->count; i++)
		sum += s->cycles[i];
	qsort(s->cycles, s->count, sizeof(*s->cycles), by_cycles);
	high = (unsigned int)((double)s->count * 0.95);
	if (high >= s->count)
		high = s->count - 1;
	out.mean = sum / s->count / per_us;
	out.median = s->cycles[s->count / 2] / per_us;
	out.high = s->cycles[high] / per_us;
	out.worst = s->cycles[s->count - 1] / per_us;
	return out;
}

/* the ME's cycles, in µs at 333 MHz */
static struct summary summarise_cycles(struct cycle_series *s) { return summarise_counts(s, 333.0); }

static void report_me(struct state *st)
{
	const struct harness_options *o = st->options;
	struct figures *f = &st->of_box, *n = &st->of_null;
	double per_tick = st->ticks ? (double)st->entities / st->ticks : 0, cpu_net[MODES];
	int place, mode;

	if (!o->on_me)
		return;
	/* the main CPU's own figures beside them, alone: summarised before the
	series are sorted by the main table (summarise sorts in place, which
	leaves the mean as it was) */
	for (mode = 0; mode < MODES; mode++)
	{
		struct summary raw = summarise(&f->calls[mode][0]), harness = summarise(&n->calls[mode][0]);

		cpu_net[mode] = raw.mean - harness.mean;
	}
	plat_out("on the Media Engine: us an entity at 333 MHz (its cycle counter), net of the null box's same pass; x CPU against the main CPU's own net mean in the mode, alone");
	plat_out("place            mode      net mean  (raw, harness)  median   95th  worst | ms a tick net | x CPU | calls  outside");
	for (place = 0; place < PLACES_ME; place++)
		for (mode = 0; mode < MODES; mode++)
		{
			struct summary raw = summarise_cycles(&f->me[place][mode]), harness = summarise_cycles(&n->me[place][mode]);
			double net = raw.mean - harness.mean;

			if (!raw.count)
				continue;
			plat_out("%-16s %-8s %9.2f  (%6.2f, %6.2f)  %6.2f %6.2f %6.1f | %13.3f | %5.2f | %5u  %u", place_names[place], mode_names[mode], net,
				raw.mean, harness.mean, raw.median - harness.median, raw.high - harness.median, raw.worst, net * per_tick / 1000.0,
				cpu_net[mode] > 0 ? net / cpu_net[mode] : 0, raw.count, st->me_outside[place]);
		}
	if (st->busy_alone.lines && f->busy.lines)
	{
		double alone = st->busy_alone.us * 1000.0 / st->busy_alone.lines, beside = f->busy.us * 1000.0 / f->busy.lines;

		static const char *const phase_names[BUSY_PHASES] = { "the copy in", "the box's calls", "the copy back", "DMACplus copy in", "DMACplus copy back" };
		int m, p;

		plat_out("the main CPU's loop (one main-RAM line a read): %.2f us per 1,000 lines with the ME idle, %.2f beside the box's busy-place passes (%+.1f%%; %u lines)",
			alone, beside, alone > 0 ? 100.0 * (beside / alone - 1.0) : 0, f->busy.lines);
		for (m = 0; m < MODES; m++)
			for (p = 0; p < BUSY_PHASES; p++)
			{
				unsigned int n = f->busy.phase_lines[m * BUSY_PHASES + p];
				double each = n ? f->busy.phase_us[m * BUSY_PHASES + p] * 1000.0 / n : 0;

				if (n)
					plat_out("the main CPU's loop beside the ME's %s, %s: %.2f us per 1,000 lines (%+.1f%%; %u lines)", phase_names[p],
						mode_names[m], each, alone > 0 ? 100.0 * (each / alone - 1.0) : 0, n);
			}
		for (m = 0; m < MODES; m++)
			if (f->busy.dma_passes[m])
			{
				double in_us = f->busy.dma_cycles[m * 2] / 333.0, back_us = f->busy.dma_cycles[m * 2 + 1] / 333.0;

				plat_out("the ME's DMACplus copies, %s: %.0f bytes a way a pass over %u passes, in %.1f us (%.0f MB/s), back %.1f us (%.0f MB/s), timed by the ME's counter",
					mode_names[m], f->busy.dma_bytes[m] / f->busy.dma_passes[m], f->busy.dma_passes[m],
					in_us / f->busy.dma_passes[m], in_us > 0 ? f->busy.dma_bytes[m] / in_us : 0,
					back_us / f->busy.dma_passes[m], back_us > 0 ? f->busy.dma_bytes[m] / back_us : 0);
			}
		if (f->busy.dma_failed)
			plat_out("the ME's DMACplus copies: %u failed, the last with %d (-1 the channel busy, -2 the controller off, -3 timed out)",
				f->busy.dma_failed, f->busy.dma_error);
	}
	if (o->me_jobs)
	{
		plat_out("the ME's per-entity jobs (mejob): each entity handed to the ME alone and waited for, timed from the main CPU (sceKernelGetSystemTimeLow), net of the null box's same job; the ME's own µs for the call (its counter) and the main CPU's D-cache write-back before each beside");
		for (place = 0; place < PLACES_ME; place++)
			for (mode = 0; mode < MODES; mode++)
			{
				struct summary raw = summarise_counts(&f->job_us[place][mode], 1.0), harness = summarise_counts(&n->job_us[place][mode], 1.0);
				struct summary own = summarise_cycles(&f->job_cycles[place][mode]), own_null = summarise_cycles(&n->job_cycles[place][mode]);
				struct summary writeback = summarise_counts(&f->job_writeback[place][mode], 1.0);
				double net = raw.mean - harness.mean;

				if (!raw.count)
					continue;
				plat_out("ME job, %s, %s: %u entities, net %.2f us (raw %.2f, null %.2f), median %.2f, 95th %.2f, worst %.1f; ms a tick net %.3f; x CPU %.2f; the ME's own %.2f net; write-back %.2f; outside %u",
					place_names[place], mode_names[mode], raw.count, net, raw.mean, harness.mean, raw.median - harness.median,
					raw.high - harness.median, raw.worst, net * per_tick / 1000.0, cpu_net[mode] > 0 ? net / cpu_net[mode] : 0,
					own.mean - own_null.mean, writeback.mean, st->me_job_outside[place]);
			}
		for (place = 0; place < PLACES_ME; place++)
			if (st->me_job_checked[place])
			{
				plat_out("check of the ME's jobs, %s: %u outputs compared, %u outside the tolerance", place_names[place],
					st->me_job_checked[place], st->me_job_outside[place]);
				if (st->me_job_outside[place])
					plat_out("check of the ME's jobs, %s: the first is tick %u, entity %u", place_names[place], st->me_job_first_tick[place],
						st->me_job_first_entity[place]);
			}
	}
	for (place = 0; place < PLACES_ME; place++)
		if (st->me_checked[place])
		{
			plat_out("check on the ME, %s: %u outputs compared, %u outside the tolerance", place_names[place], st->me_checked[place],
				st->me_outside[place]);
			if (st->me_outside[place])
				plat_out("check on the ME, %s: the first is tick %u, entity %u", place_names[place], st->me_first_tick[place],
					st->me_first_entity[place]);
		}
	if (st->me_stopped)
		plat_out("on the Media Engine: the passes STOPPED (above): its figures cover the ticks before");
}

static void report(struct state *st)
{
	const struct box_adapter *box = st->box;
	const struct harness_options *o = st->options;
	struct figures *f = &st->of_box, *n = &st->of_null;
	double per_tick = st->ticks ? (double)st->entities / st->ticks : 0;
	/* a batching box's call is a whole tick: its figures an entity are divided by the tick's mean count */
	double share = box->batch && per_tick > 0 ? per_tick : 1;
	int mode, d, k;

	plat_out("==== box %s: %u ticks, %u entities (%.1f a tick)%s ====", box->name, st->ticks, st->entities, per_tick,
		box->batch ? "; one walk a tick" : "");
	plat_out("capture: %u bytes read, %u of them static; set-up %u us; the box owns %d bytes afterwards", st->capture_bytes,
		st->static_bytes, st->setup_us, st->owned_bytes);
	if (box->budget_ms_a_tick > 0 || box->budget_us_an_entity > 0)
		plat_out("budget (the box's page): %.3f ms a tick, %.2f us an entity", box->budget_ms_a_tick, box->budget_us_an_entity);

	/* by kind, from the warm mode alone, before the series are sorted */
	if (!box->batch && f->calls[MODE_WARM][0].count == f->calls_noted)
	{
		double sums[KINDS_MOST] = { 0 }, null_mean = 0;
		unsigned int counts[KINDS_MOST] = { 0 }, i;
		struct series *s = &f->calls[MODE_WARM][0], *ns = &n->calls[MODE_WARM][0];

		for (i = 0; i < ns->count; i++)
			null_mean += ns->us[i];
		null_mean = ns->count ? null_mean / ns->count : 0;
		for (i = 0; i < s->count; i++)
		{
			sums[f->kind[i]] += s->us[i];
			counts[f->kind[i]]++;
		}
		for (k = 0; k < KINDS_MOST; k++)
			if (counts[k])
			{
				const char *name = "";
				int j;

				for (j = 0; box->kinds && box->kinds[j]; j++)
					if (j == k)
						name = box->kinds[j];
				plat_out("kind %d %s: %u entities, %.1f a tick, warm %.2f us an entity net", k, name, counts[k],
					st->ticks ? (double)counts[k] / st->ticks : 0, sums[k] / counts[k] - null_mean);
			}
	}

	/* one program holds every box, so adding one moves the others: where this run's code sat */
	plat_out("code: the box's run() at %p (instruction-cache set %u of 128), the null box's at %p (set %u), the harness's loop at %p (set %u)",
		(void *)box->run, ((unsigned int)(size_t)box->run >> 6) & 127u, (void *)st->null.run, ((unsigned int)(size_t)st->null.run >> 6) & 127u,
		(void *)pass_each, ((unsigned int)(size_t)pass_each >> 6) & 127u);
	plat_out("mode     GE%% (met)  us an entity: net mean  (raw mean, harness)  median   95th  worst | ms a tick net%s | calls  GE short",
		box->budget_ms_a_tick > 0 ? "  x budget" : "");
	for (d = 0; d < o->duty_count; d++)
		for (mode = 0; mode < MODES; mode++)
		{
			struct summary raw = summarise(&f->calls[mode][d]), harness = summarise(&n->calls[mode][d]);
			double net = (raw.mean - harness.mean) / share, tick_ms = net * per_tick / 1000.0;
			char budget[32] = "", met[16] = "     ";

			if (box->budget_ms_a_tick > 0)
				snprintf(budget, sizeof(budget), "  x%.2f", tick_ms / box->budget_ms_a_tick);
			if (f->ge_span[mode][d] > 0)
				snprintf(met, sizeof(met), "(%3.0f)", 100.0 * (1.0 - f->ge_other[mode][d] / f->ge_span[mode][d]));
			plat_out("%-8s %3d %s  %22.2f  (%8.2f, %7.2f)  %6.1f %6.1f %6.0f | %13.3f%s | %5u  %u%s", mode_names[mode], o->duties[d], met, net,
				raw.mean / share, harness.mean / share, (raw.median - harness.median) / share, (raw.high - harness.median) / share,
				raw.worst / share, tick_ms, budget, raw.count, f->short_spans[mode][d],
				f->calls[mode][d].clipped ? "  (some calls past 65 ms, counted as 65)" : "");
		}
	for (d = 0; d < o->duty_count; d++)
		if (f->whole_entities[d])
		{
			double whole = f->whole_us[d] - n->whole_us[d];

			plat_out("warm, whole ticks (two clock reads a tick), GE %d%%: %.3f ms a tick net, %.2f us an entity (the harness %.2f)",
				o->duties[d], whole / st->ticks / 1000.0, whole / f->whole_entities[d], n->whole_us[d] / f->whole_entities[d]);
		}
	if (o->counted)
	{
		plat_out("the CPU's counters an entity, alone, net of the harness's own (its figure in brackets):");
		plat_out("mode       cycles (us at 333 MHz)  stalled: memory  pipeline  | I misses  D misses  write-backs | cached loads  stores | FPU  VFPU  (harness: cycles, I, D)");
		for (mode = 0; mode < MODES; mode++)
			if (f->counted[mode] && n->counted[mode])
			{
				double c[COUNTERS], h[COUNTERS];

				for (k = 0; k < COUNTERS; k++)
				{
					h[k] = n->counters[mode][k] / n->counted[mode];
					c[k] = f->counters[mode][k] / f->counted[mode] - h[k];
				}
				plat_out("%-8s %9.0f (%7.2f) %15.0f %9.0f  | %8.2f %9.2f %12.2f | %12.1f %7.1f | %4.0f %5.0f  (%.0f, %.2f, %.2f)", mode_names[mode],
					c[C_CYCLES], c[C_CYCLES] / 333.0, c[C_STALL_MEMORY], c[C_STALL_INTERNAL], c[C_I_MISS], c[C_D_MISS], c[C_D_WRITEBACK],
					c[C_CACHED_LOAD], c[C_CACHED_STORE], c[C_FPU], c[C_VFPU], h[C_CYCLES], h[C_I_MISS], h[C_D_MISS]);
				if (mode == MODE_EMPTIED)
					plat_out("footprint: an entity touches %.1f lines of code and %.1f of data (each missed once from empty caches; a cache holds 256)",
						c[C_I_MISS], c[C_D_MISS]);
				if (c[C_I_MISS] + c[C_D_MISS] >= 0.05)
					plat_out("%-8s %.0f%% of an entity's cycles are waits on memory: %.0f cycles a miss", mode_names[mode],
						c[C_CYCLES] > 0 ? 100.0 * c[C_STALL_MEMORY] / c[C_CYCLES] : 0, c[C_STALL_MEMORY] / (c[C_I_MISS] + c[C_D_MISS]));
			}
	}
	report_me(st);
	plat_out("check: %u outputs compared, %u outside the tolerance (%s%s%g absolute, %g relative); largest difference %g absolute, %g relative",
		st->checked, st->outside, box->compare ? "the box's own comparison; " : "",
		box->tolerance == BOX_EXACT ? "exact; " : box->tolerance == BOX_ABSOLUTE ? "absolute; " : "relative; ",
		(double)box->eps_absolute, (double)box->eps_relative, (double)st->largest_absolute, (double)st->largest_relative);
	if (st->outside)
	{
		if (st->first_element == 0xffffffffu)
			plat_out("check: the first is tick %u, entity %u: its output's length differs from the capture's", st->first_tick, st->first_entity);
		else
			plat_out("check: the first is tick %u, entity %u, element %u", st->first_tick, st->first_entity, st->first_element);
	}
	plat_out("twin: phases 1 warm, 2 emptied, 3 crowded: each covers %u entity runs of box %s (its per-entity pass, alone)", st->entities, box->name);
	{
		unsigned int me_outside = st->me_outside[0] + st->me_outside[1] + st->me_outside[2] + st->me_job_outside[0] +
			st->me_job_outside[1] + st->me_job_outside[2];

		plat_out("==== box %s: %s ====", box->name, st->outside || me_outside ? "OUTPUTS OUTSIDE THE TOLERANCE: its times mean nothing until that is fixed"
			: st->me_stopped ? "done on the main CPU; the ME's passes stopped" : "done");
	}
}

static void one_tick(struct state *st, const struct box_tick *tick)
{
	st->ticks++;
	st->entities += tick->entities;
	if (st->ticks == 1)
		plat_step("tick %u: the check (%u entities)", tick->tick, tick->entities);
	check_tick(st, tick);
	time_tick(st, &st->null, &st->of_null, tick);
	time_tick(st, st->box, &st->of_box, tick);
}

int harness_run(const struct box_adapter *box, const char *const *files, int file_count, const struct harness_options *options)
{
	static struct state st; /* zeroed; one run a program */
	unsigned int run_start = plat_now();
	int f;

	st.box = box;
	st.options = options;
	st.null = box_null;
	st.null.batch = box->batch;
	if (options->on_me)
		plat_me_busy_alone(&st.busy_alone, 64000);
	if (!file_count && !box->no_capture)
		return plat_out("STOPPED: box %s needs a capture, and there is none", box->name), 1;
	if (!file_count)
	{
		/* no capture: entities that carry nothing, for a box that reads nothing */
		struct box_entity *blank = (struct box_entity *)calloc((size_t)options->synthetic_entities + 1, sizeof(*blank));
		struct box_tick tick = { 0, (unsigned int)options->synthetic_entities, blank };
		unsigned int t;

		if (!blank || box->setup(0, 0) < 0)
			return plat_out("STOPPED: box %s wouldn't set up", box->name), 1;
		plat_step("box %s without a capture: %d ticks of %d entities that carry nothing", box->name, options->synthetic_ticks,
			options->synthetic_entities);
		for (t = 0; t < (unsigned int)options->synthetic_ticks; t++)
		{
			tick.tick = t;
			one_tick(&st, &tick);
		}
		free(blank);
	}
	for (f = 0; f < file_count; f++)
	{
		struct capture c;
		struct box_tick tick;
		unsigned int before;

		plat_step("capture %s", files[f]);
		if (!capture_open(&c, files[f], box->capture ? box->capture : box->name))
		{
			capture_close(&c);
			continue;
		}
		plat_out("capture: %s: box %.8s, map %.8s, bench %.20s, build %08x, from tick %u, %u ticks, %u entities, statics %u bytes in %u records",
			files[f], c.header.box, c.header.map, c.header.bench, c.header.commit, c.header.tick_first, c.header.ticks, c.header.entities,
			c.header.static_bytes, c.static_count);
		before = plat_now();
		st.owned_bytes = box->setup(c.static_index, c.static_count);
		st.setup_us += plat_now() - before;
		if (st.owned_bytes < 0)
		{
			plat_out("capture: %s: the box refused its static section (%d)", files[f], st.owned_bytes);
			capture_close(&c);
			continue;
		}
		st.static_bytes += c.header.static_bytes;
		while ((!options->ticks_most || st.ticks < (unsigned int)options->ticks_most) && capture_tick(&c, &tick, files[f]))
		{
			/* every tick: a run that outlives its wall says how far it got, and at what rate */
			plat_step("tick %u (the %uth of this run), %u ms in", tick.tick, st.ticks + 1, (plat_now() - run_start) / 1000u);
			one_tick(&st, &tick);
		}
		st.capture_bytes += (unsigned int)ftell(c.file);
		capture_close(&c);
	}
	if (!st.ticks)
		return plat_out("STOPPED: no tick of box %s was read", box->name), 1;
	plat_step("the figures");
	report(&st);
	return st.outside != 0 || st.me_outside[0] + st.me_outside[1] + st.me_outside[2] != 0 ||
		st.me_job_outside[0] + st.me_job_outside[1] + st.me_job_outside[2] != 0 || st.me_stopped;
}
