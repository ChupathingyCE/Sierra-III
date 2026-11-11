/*
The box harness, its portable part (bench/halo_box/README.md).

For each tick of a capture, and for the box and the null box beside it:

  - the check: every entity run once, its output compared with the capture's;
  - warm: the tick's entities back to back three times; the first is
    discarded, the second timed whole (two clock reads a tick), the third
    with a clock read either side of each entity (the spread);
  - emptied: both caches emptied before each entity, a clock read either side;
  - crowded: both caches filled with other work's lines before each entity.

The three timed modes run once alone and once for each GE duty asked for.
The null box's figure in the same mode is the harness's own cost there, and
is printed and subtracted.

A box whose design is one walk over a tick's entities (adapter.batch) is run
once a tick, and its figures are divided by the tick's entity count.
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
	unsigned char *kind;       /* an entity's kind, in the order sampled */
	unsigned int *call_entities; /* entities behind each timed call (1, or a tick's count) */
	unsigned int calls_noted, calls_room;
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

static void check_tick(struct state *st, const struct box_tick *tick)
{
	const struct box_adapter *box = st->box;
	unsigned int i;

	box->tick(tick);
	if (box->batch)
		box->run(BOX_ALL);
	for (i = 0; i < tick->entities; i++)
	{
		const void *got;
		unsigned int length, element = 0;
		float absolute = 0, relative = 0;
		int bad;

		if (!box->batch)
			box->run((int)i);
		box->output((int)i, &got, &length);
		if (length != tick->entity[i].output_length)
			bad = 1;
		else if (!length)
			bad = 0;
		else if (box->compare)
			bad = box->compare((int)i, got, tick->entity[i].output, length, &absolute, &relative, &element);
		else
			bad = compare_floats(box, got, tick->entity[i].output, length, &absolute, &relative, &element);
		st->checked++;
		if (absolute > st->largest_absolute || absolute != absolute)
			st->largest_absolute = absolute;
		if (relative > st->largest_relative)
			st->largest_relative = relative;
		if (bad && !st->outside++)
		{
			st->first_tick = tick->tick;
			st->first_entity = tick->entity[i].id;
			st->first_element = length != tick->entity[i].output_length ? 0xffffffffu : element;
		}
	}
}

/* ---- the timed passes ---- */

/* one pass over a tick with a clock read either side of each call; returns
the pass's whole span, clock reads and the work between entities included */
static unsigned int pass_each(const struct box_adapter *box, const struct box_tick *tick, int mode, struct series *into)
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
		box->run(box->batch ? BOX_ALL : (int)i);
		after = plat_now();
		if (into)
			sample(into, after - before);
	}
	return plat_now() - start;
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
				span = pass_each(box, tick, mode, into);
				if (duty)
					covered = plat_ge_stop();
				else
					alone[mode] = span + whole * 2;
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

	plat_out("mode     GE%%  us an entity: net mean  (raw mean, harness)  median   95th  worst | ms a tick net%s | calls  GE short",
		box->budget_ms_a_tick > 0 ? "  x budget" : "");
	for (d = 0; d < o->duty_count; d++)
		for (mode = 0; mode < MODES; mode++)
		{
			struct summary raw = summarise(&f->calls[mode][d]), harness = summarise(&n->calls[mode][d]);
			double net = (raw.mean - harness.mean) / share, tick_ms = net * per_tick / 1000.0;
			char budget[32] = "";

			if (box->budget_ms_a_tick > 0)
				snprintf(budget, sizeof(budget), "  x%.2f", tick_ms / box->budget_ms_a_tick);
			plat_out("%-8s %3d  %22.2f  (%8.2f, %7.2f)  %6.1f %6.1f %6.0f | %13.3f%s | %5u  %u%s", mode_names[mode], o->duties[d], net,
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
	plat_out("==== box %s: %s ====", box->name, st->outside ? "OUTPUTS OUTSIDE THE TOLERANCE: its times mean nothing until that is fixed" : "done");
}

static void one_tick(struct state *st, const struct box_tick *tick)
{
	st->ticks++;
	st->entities += tick->entities;
	check_tick(st, tick);
	time_tick(st, &st->null, &st->of_null, tick);
	time_tick(st, st->box, &st->of_box, tick);
}

int harness_run(const struct box_adapter *box, const char *const *files, int file_count, const struct harness_options *options)
{
	static struct state st; /* zeroed; one run a program */
	int f;

	st.box = box;
	st.options = options;
	st.null = box_null;
	st.null.batch = box->batch;
	if (!file_count)
	{
		/* no capture: entities that carry nothing, for a box that reads nothing */
		struct box_entity *blank = (struct box_entity *)calloc((size_t)options->synthetic_entities + 1, sizeof(*blank));
		struct box_tick tick = { 0, (unsigned int)options->synthetic_entities, blank };
		unsigned int t;

		if (!blank || box->setup(0, 0) < 0)
			return plat_out("STOPPED: box %s needs a capture", box->name), 1;
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
		if (!capture_open(&c, files[f], box->name))
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
			if (!(st.ticks & 31))
				plat_step("tick %u (the %uth of this run)", tick.tick, st.ticks + 1);
			one_tick(&st, &tick);
		}
		st.capture_bytes += (unsigned int)ftell(c.file);
		capture_close(&c);
	}
	if (!st.ticks)
		return plat_out("STOPPED: no tick of box %s was read", box->name), 1;
	plat_step("the figures");
	report(&st);
	return st.outside != 0;
}
