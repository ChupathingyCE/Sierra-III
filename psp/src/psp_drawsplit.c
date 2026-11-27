/* The drawing's split (docs/box_draw_main.md §1.3, §1.5; keep word
"drawsplit", build switch PSP_DRAWSPLIT=1): the main CPU's drawing a frame
(frametrace's d: main_game_render and the loading veil) split by its parts,
and two of the parts by their fields, every frame, in the build's own form.

Level 1. The engine's own perf phases (halo_perf_log.h) are compiled in for
the drawing's files only (psp_build.py: render.c, main.c, interface.c,
xbox_texture_cache.c, rasterizer_xbox_widgets.c), never the tick's, so the
tick is the build's as it is without the switch. Each hook that changes the
part charged reads the clock (sceKernelGetSystemTimeLow, ~0.5 us) and
charges the time since the last read to the part that was open: the render's
parts follow each other (halo_perf_render_part), and the waits and the
render's sound nest inside them (halo_perf_begin and _end, a stack). A hook
that leaves the same part charged reads nothing.

Level 2 (§1.5). What is charged is a (part, field) pair: the field NONE is
the part's own time outside every field, its residual. psp_fixups.py's hooks
(psp_drawsplit.h: PSP_DRAWSPLIT_IN(field), _OUT(), placed only when the
switch is on) open a field under the part open now and close it, on the same
stack as level 1's waits, so a field opened under world and one opened under
objects are told apart. An item field nests: the inner one is charged while
it runs, then the outer again. A whole field charges all inside it to
itself: the field hooks nested in it read nothing. PSP_DRAWSPLIT_COUNT adds
to the frame's items (instances, model parts drawn, textured level draws
under the part open, a whole field's apart; rendered clusters and objects)
and reads no clock. A part's level 1 figure is the sum of its fields.

The frame. The render phase's begin (main.c, before main_game_render) opens
it; psp_frame_account (psp_platform.c) closes it with d, so the parts cover
the same span d does, less the few instructions between the render's begin
and d's first clock read. Time from there to the render's end (the frame's
end, e) is charged to no part.

The lines, every 300 frames, the parts in ms a frame: the window's mean, and
p50 and p99 over the run so far from a histogram in 0.1 ms bins:
  drawsplit: N frames (run F): d M/P50/P99; parts M; unnamed M/P50/P99;
  world M/P50/P99, ...; stamps M a frame (most K)
and level 2's, the window's means: the part, its residual and its share,
the items a frame, then each field's ms a frame and us an item:
  drawsplit2 objects: N frames: part M ms; residual M ms (S%); inst M,
  part M a frame; walk M ms U/inst; ...; shadows M ms; fpw M ms;
  stamps2 M a frame
  drawsplit2 world: N frames: part M ms; residual M ms (S%); draw M,
  cluster M, object M, sky part M a frame; vis M ms U/cluster; ...;
  elsewhere M ms
"unnamed" is the render's own time outside every part (between them, and
before the first); "elsewhere" is fields' time under parts other than
objects and world (a model drawn in transparent, a hook inside a wait).
Measurement only: nothing the game reads is written. With the word off the
hooks are each a flag's test and nothing is allocated.

The sampled form (PSP_DRAWSPLIT_SAMPLED; the lab's
findings/drawsplit2_2026-11-27.md 'The thinner cut, design (a)'; decisions
2026-11-27 04:0x, 04:1x (3)). The hooks are stores (psp_drawsplit.h): a
(part, field) code on a 16-deep stack, level 1's through the three phase
tables here, so no hooked function gains a call or a branch. The clock is an
alarm's (sceKernelSetAlarm, every PERIOD us, from the first render on, the
whole frame long): each run charges the time since its last run to the code
on the stack's top, the same wall time the exact form's deltas charge,
sampled. The frame: main.c's render calls, uncounted (psp_fixups.py places
them), psp_drawsplit_render_begin before the render's begin and
psp_drawsplit_render_end after its end, so d is the render's span and
psp_frame_account calls nothing; the end folds the samples since the last
end (a buffer each, swapped there, so the alarm never writes the one being
read) and finds the stack at its bottom, else counts the frame unbalanced and
puts it back. A part's figure is quantised by the period: the first sample
after the render's begin charges the gap since the one before to the render,
and the gap from the last sample to the render's end goes to the next frame's
out, ~half a period each, roughly cancelling. Its lines are the exact form's
without the us an item (a share isn't a count: decisions 04:1x (3)(d)) and
with the samples' own:
  drawsplit2 samples: N frames, every P us: M a frame, M in the drawing;
  late mean M us, worst W us[; SHARES U: ...]
  drawsplit2 samples objects: residual n, walk n, ...; under 100: f, ...
(and world's, and world's window). A run's lateness is its gap past the
period, read on the runs that land in the drawing: it equals the fire's
lateness when the kernel counts the next delay from the handler's run [U:
not read; from the fire's due time, it is the change in lateness], and it is
the bias either way, since a late run charges its whole gap to what is open
when it lands. A mean over a tenth of the period marks the shares [U]
(04:1x (3)(b)); a field under 100 samples on the window is named, not read
as us (3)(c). */

#ifdef PSP_DRAWSPLIT

#include <pspkernel.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "halo_perf_log.h"
#include "psp_drawsplit.h"

int psp_keep(const char *name);
void platform_log(const char *format, ...);

int halo_perf_log_on = 0;
unsigned long halo_perf_counters[NUMBER_OF_HALO_PERF_COUNTERS];

/* the parts charged. OUT: no part (outside the drawing, or after d's end) */
enum drawsplit_part
{
	PART_OUT,
	PART_UNNAMED, /* inside the render, no part open */
	PART_BEGIN,
	PART_SETUP,
	PART_WORLD,
	PART_OBJECTS,
	PART_TRANSPARENT,
	PART_EFFECTS,
	PART_LENS_FLARES,
	PART_HUD,
	PART_SCREEN_EFFECT,
	PART_END,
	PART_SOUND,
	PART_OCCLUSION_WAIT,
	PART_TEXTURE_WAIT,
	PARTS
};

static const char *const part_names[PARTS] =
{
	"out", "unnamed", "begin", "setup", "world", "objects", "transparent", "effects", "flares", "hud", "screen", "end",
	"sound", "occlusion", "texture",
};

#define FIELDS PSP_DRAWSPLIT_FIELDS
#define ITEMS PSP_DRAWSPLIT_ITEMS
static const char *const field_names[FIELDS] =
{
	"residual", "walk", "lights", "matrices", "d3d", "inst", "state", "encode", "vis",
	"shadows", "fpw", "sound", "sky", "lightpass", "decals", "diffuse", "specular",
	"winbegin", "reflect", "fog", "detail", "aa", "winend",
};
static const char *const item_names[ITEMS] = { "inst", "part", "draw", "cluster", "object" };

/* what is charged: a (part, field) pair */
#define SLOT(part, field) ((part) * FIELDS + (field))
#define SLOTS (PARTS * FIELDS)
#define PART_OF(slot) ((slot) / FIELDS)
/* a stack entry pushed by a field's hook, not a level 1 phase's */
#define FIELD_MARK 0x8000

/* d and the parts' sum have histograms of their own */
#define HIST_D PARTS
#define HIST_SUM (PARTS + 1)
#define HISTS (PARTS + 2)
/* 0.1 ms bins to 60 ms; the last holds the rest */
#define BINS 600
#define WINDOW 300
#define STACK 16

struct drawsplit_run
{
	unsigned int histogram[HISTS][BINS];
	unsigned long long window_us[HISTS];
	unsigned long long window_slot_us[SLOTS];
	/* [part][item][inside a whole field] */
	unsigned long long window_items[PARTS][ITEMS][2];
	unsigned int frames, run_frames, unfolded, unbalanced;
	/* sampled: the samples in the drawing, and all of them */
	unsigned long long window_stamps, window_stamps2;
	unsigned int stamps_most;
#ifdef PSP_DRAWSPLIT_SAMPLED
	unsigned long long window_samples[SLOTS];
	/* late_us: the drawing's runs' gaps past the period; odd: runs finding
	a code no part or field has */
	unsigned long long window_late_us;
	unsigned int late_most, odd;
#endif
};

#ifdef PSP_DRAWSPLIT_SAMPLED
/* the alarm's period, us (5000: the lab's decisions 2026-11-27 06:3x (2),
drawsplit3-b30's alarm at 100 cost +12.3% drawing; PSP_DRAWSPLIT_PERIOD is
the host test's, never a played build's); a field under FEW samples on the
window is named */
#ifdef PSP_DRAWSPLIT_PERIOD
#define PERIOD PSP_DRAWSPLIT_PERIOD
#else
#define PERIOD 5000
#endif
#define FEW 100
#endif

static struct drawsplit_run *run = NULL;
static int on = -1;
static signed char phase_part[NUMBER_OF_HALO_PERF_PHASES];
#ifndef PSP_DRAWSPLIT_SAMPLED
static unsigned char phase_depth[NUMBER_OF_HALO_PERF_PHASES];
static unsigned short stack[STACK];
/* whole_open: 1 inside a whole field, more for the field hooks nested in it;
lost: field opens refused (past STACK deep, or no such field), whose closes
are skipped */
static int depth = 0, current = SLOT(PART_OUT, PSP_DRAWSPLIT_NONE), frame_open = 0, whole_open = 0, lost = 0;
static unsigned int last = 0, frame_stamps = 0, frame_stamps2 = 0;
static unsigned int frame_us[SLOTS];
static unsigned int frame_items[PARTS][ITEMS][2];
#endif

static void map_phases(void)
{
	int k;

	for (k = 0; k < NUMBER_OF_HALO_PERF_PHASES; k++)
		phase_part[k] = -1;
	phase_part[_halo_perf_render] = PART_UNNAMED;
	phase_part[_halo_perf_render_begin] = PART_BEGIN;
	phase_part[_halo_perf_render_setup] = PART_SETUP;
	phase_part[_halo_perf_render_world] = PART_WORLD;
	phase_part[_halo_perf_render_objects] = PART_OBJECTS;
	phase_part[_halo_perf_render_transparent] = PART_TRANSPARENT;
	phase_part[_halo_perf_render_effects] = PART_EFFECTS;
	phase_part[_halo_perf_render_lens_flares] = PART_LENS_FLARES;
	phase_part[_halo_perf_render_hud] = PART_HUD;
	phase_part[_halo_perf_render_screen_effect] = PART_SCREEN_EFFECT;
	phase_part[_halo_perf_render_end] = PART_END;
	/* sound_render (main.c) is inside both sound and render sound: the
	inner one only, so it costs one pair of reads, not two */
	phase_part[_halo_perf_render_sound] = PART_SOUND;
	phase_part[_halo_perf_occlusion_wait] = PART_OCCLUSION_WAIT;
	phase_part[_halo_perf_texture_wait] = PART_TEXTURE_WAIT;
	/* sound, network, present and the vertical blank's wait are outside d:
	unmapped, they read nothing */
}

#ifndef PSP_DRAWSPLIT_SAMPLED
/* the pair charged from now on: the time since the last read goes to the one
that was open. 1 if it read the clock */
static int charge_to(int slot)
{
	unsigned int now;

	if (slot == current)
		return 0;
	now = sceKernelGetSystemTimeLow();
	if (PART_OF(current) != PART_OUT)
		frame_us[current] += now - last;
	last = now;
	current = slot;
	frame_stamps++;
	return 1;
}

/* a level 1 phase's close finding fields still open above it: they are
dropped, and said so. A whole field open from before the phase began is
under the phase's own entry, so it stays open: whole_open is cleared only
when a field is dropped (a wait inside the sky kept the sky whole) */
static void drop_fields(void)
{
	while (depth && (stack[depth - 1] & FIELD_MARK))
	{
		depth--;
		run->unbalanced++;
		whole_open = 0;
	}
}

void halo_perf_begin(int phase)
{
	int part;

	if (phase < 0 || phase >= NUMBER_OF_HALO_PERF_PHASES || (part = phase_part[phase]) < 0)
		return;
	if (phase_depth[phase])
	{
		phase_depth[phase]++;
		return;
	}
	if (depth >= STACK)
	{
		run->unbalanced++;
		return;
	}
	phase_depth[phase] = 1;
	stack[depth++] = (unsigned short)current;
	if (phase == _halo_perf_render)
		frame_open = 1;
	charge_to(SLOT(part, PSP_DRAWSPLIT_NONE));
}

void halo_perf_end(int phase)
{
	if (phase < 0 || phase >= NUMBER_OF_HALO_PERF_PHASES || phase_part[phase] < 0)
		return;
	if (!phase_depth[phase] || --phase_depth[phase])
		return;
	drop_fields();
	if (!depth)
	{
		run->unbalanced++;
		return;
	}
	charge_to(stack[--depth]);
}

/* the render's parts follow each other: the one running is closed as the
next opens; NONE (or a phase with no part) is the render's own time. A field
still open across the change is dropped */
void halo_perf_render_part(int phase)
{
	if (!depth)
		return;
	drop_fields();
	charge_to(SLOT(phase >= 0 && phase < NUMBER_OF_HALO_PERF_PHASES && phase_part[phase] > PART_UNNAMED ? phase_part[phase] : PART_UNNAMED,
		PSP_DRAWSPLIT_NONE));
}

/* (no unit of this build asks: NONE) */
int halo_perf_render_part_now(void)
{
	return -1;
}

/* the main loop's pass ends (main.c; called whether the word is on or not):
every phase and field should be closed */
void halo_perf_frame(void)
{
	if ((depth || lost) && run)
	{
		run->unbalanced++;
		depth = 0;
		whole_open = 0;
		lost = 0;
		current = SLOT(PART_OUT, PSP_DRAWSPLIT_NONE);
	}
}

/* level 2: a field opened under the part open now */
void psp_drawsplit_in(int field)
{
	if (on <= 0)
		return;
	if (whole_open)
	{
		whole_open++;
		return;
	}
	if (field <= PSP_DRAWSPLIT_NONE || field >= FIELDS || depth >= STACK)
	{
		lost++;
		run->unbalanced++;
		return;
	}
	stack[depth++] = (unsigned short)(current | FIELD_MARK);
	if (field >= PSP_DRAWSPLIT_FIRST_WHOLE)
		whole_open = 1;
	frame_stamps2 += charge_to(SLOT(PART_OF(current), field));
}

/* the field opened last is closed: the pair under it charged again */
void psp_drawsplit_out(void)
{
	if (on <= 0)
		return;
	if (whole_open > 1)
	{
		whole_open--;
		return;
	}
	if (lost)
	{
		lost--;
		return;
	}
	if (!depth || !(stack[depth - 1] & FIELD_MARK))
	{
		run->unbalanced++;
		return;
	}
	whole_open = 0;
	frame_stamps2 += charge_to(stack[--depth] & ~FIELD_MARK);
}

void psp_drawsplit_count(int item, unsigned int n)
{
	if (on <= 0 || item < 0 || item >= ITEMS)
		return;
	if (item >= PSP_DRAWSPLIT_CLUSTERS)
		frame_items[PART_WORLD][item][0] += n;
	else
		frame_items[PART_OF(current)][item][whole_open ? 1 : 0] += n;
}
#endif

static unsigned int bin(unsigned int us)
{
	return us / 100 < BINS ? us / 100 : BINS - 1;
}

/* the run's percentile from a histogram, as its bin's upper edge in ms */
static double percentile(const unsigned int *histogram, unsigned int samples, unsigned int per_mille)
{
	unsigned int want = (unsigned int)(((unsigned long long)samples * per_mille + 999) / 1000), seen = 0, k;

	for (k = 0; k < BINS && want; k++)
	{
		seen += histogram[k];
		if (seen >= want)
			return (k + 1) / 10.0;
	}
	return 0.0;
}

/* a field of a level 2 line: its ms a frame, and us an item when it has an
item (-1: none) */
struct line_field
{
	signed char field, item;
};

static const struct line_field objects_fields[] =
{
	{ PSP_DRAWSPLIT_WALK, PSP_DRAWSPLIT_INSTANCES }, { PSP_DRAWSPLIT_LIGHTS, PSP_DRAWSPLIT_INSTANCES },
	{ PSP_DRAWSPLIT_MATRICES, PSP_DRAWSPLIT_INSTANCES }, { PSP_DRAWSPLIT_D3D, PSP_DRAWSPLIT_INSTANCES },
	{ PSP_DRAWSPLIT_INST, PSP_DRAWSPLIT_INSTANCES }, { PSP_DRAWSPLIT_STATE, PSP_DRAWSPLIT_PARTS_DRAWN },
	{ PSP_DRAWSPLIT_ENCODE, PSP_DRAWSPLIT_PARTS_DRAWN }, { PSP_DRAWSPLIT_SHADOWS, -1 }, { PSP_DRAWSPLIT_FPW, -1 },
};

static const struct line_field world_fields[] =
{
	{ PSP_DRAWSPLIT_VIS, PSP_DRAWSPLIT_CLUSTERS }, { PSP_DRAWSPLIT_SOUND, -1 }, { PSP_DRAWSPLIT_LIGHTS, -1 },
	{ PSP_DRAWSPLIT_WALK, PSP_DRAWSPLIT_LEVEL_DRAWS }, { PSP_DRAWSPLIT_STATE, PSP_DRAWSPLIT_LEVEL_DRAWS },
	{ PSP_DRAWSPLIT_D3D, PSP_DRAWSPLIT_LEVEL_DRAWS }, { PSP_DRAWSPLIT_ENCODE, PSP_DRAWSPLIT_LEVEL_DRAWS },
	{ PSP_DRAWSPLIT_SKY, -1 }, { PSP_DRAWSPLIT_LIGHTPASS, -1 }, { PSP_DRAWSPLIT_DECALS, -1 },
	{ PSP_DRAWSPLIT_DIFFUSE, -1 }, { PSP_DRAWSPLIT_SPECULAR, -1 },
};

/* appends to a line held in its buffer (snprintf returns the length it
wanted: at is held inside the line) */
static int append(char *line, int size, int at, const char *format, ...)
{
	va_list ap;

	if (at >= size - 1)
		return size - 1;
	va_start(ap, format);
	at += vsnprintf(line + at, size - at, format, ap);
	va_end(ap);
	return at > size - 1 ? size - 1 : at;
}

/* one level 2 line: the part, its residual, the items a frame, the fields */
static void report_part(int part, const struct line_field *fields, int count, const int *items, int item_count, double n)
{
	char line[500];
	int at, k;
	unsigned long long part_us = 0;

	for (k = 0; k < FIELDS; k++)
		part_us += run->window_slot_us[SLOT(part, k)];
	at = snprintf(line, sizeof(line), "drawsplit2 %s: %u frames: part %.2f ms; residual %.2f ms (%u%%); ", part_names[part], run->frames,
		part_us / n / 1000.0, run->window_slot_us[SLOT(part, PSP_DRAWSPLIT_NONE)] / n / 1000.0,
		part_us ? (unsigned)(run->window_slot_us[SLOT(part, PSP_DRAWSPLIT_NONE)] * 100 / part_us) : 0);
	for (k = 0; k < item_count; k++)
		at = append(line, sizeof(line), at, "%s%s%s %.1f", k ? ", " : "", items[k] < 0 ? "sky " : "",
			item_names[items[k] < 0 ? PSP_DRAWSPLIT_PARTS_DRAWN : items[k]],
			(items[k] < 0 ? run->window_items[part][PSP_DRAWSPLIT_PARTS_DRAWN][1] : run->window_items[part][items[k]][0]) / n);
	at = append(line, sizeof(line), at, " a frame");
	for (k = 0; k < count; k++)
	{
		unsigned long long us = run->window_slot_us[SLOT(part, fields[k].field)];

		at = append(line, sizeof(line), at, "; %s %.2f ms", field_names[(int)fields[k].field], us / n / 1000.0);
#ifndef PSP_DRAWSPLIT_SAMPLED
		if (fields[k].item >= 0)
		{
			unsigned long long items_seen = run->window_items[part][(int)fields[k].item][0];

			if (items_seen)
				at = append(line, sizeof(line), at, " %.1f/%s", (double)us / items_seen, item_names[(int)fields[k].item]);
			else
				at = append(line, sizeof(line), at, " -/%s", item_names[(int)fields[k].item]);
		}
#endif
	}
#ifdef PSP_DRAWSPLIT_SAMPLED
	if (part == PART_OBJECTS)
		at = append(line, sizeof(line), at, "; samples %.1f a frame", run->window_stamps / n);
#else
	if (part == PART_OBJECTS)
		at = append(line, sizeof(line), at, "; stamps2 %.1f a frame", run->window_stamps2 / n);
#endif
	else
	{
		unsigned long long elsewhere = 0;
		int other;

		for (other = PART_UNNAMED; other < PARTS; other++)
			if (other != PART_OBJECTS && other != PART_WORLD)
				for (k = PSP_DRAWSPLIT_NONE + 1; k < FIELDS; k++)
					elsewhere += run->window_slot_us[SLOT(other, k)];
		at = append(line, sizeof(line), at, "; elsewhere %.2f ms", elsewhere / n / 1000.0);
	}
	platform_log("%s", line);
}

#ifdef PSP_DRAWSPLIT_SAMPLED
/* one samples line: each field's samples on the window, then the fields
under FEW named (04:1x (3)(c): not read as us) */
static void report_samples(const char *title, int part, const signed char *fields, int count)
{
	char line[500];
	int at, k, few = 0;

	at = snprintf(line, sizeof(line), "drawsplit2 samples %s:", title);
	for (k = 0; k < count; k++)
		at = append(line, sizeof(line), at, "%s %s %u", k ? "," : "", field_names[(int)fields[k]],
			(unsigned)run->window_samples[SLOT(part, fields[k])]);
	at = append(line, sizeof(line), at, "; under %d:", FEW);
	for (k = 0; k < count; k++)
		if (run->window_samples[SLOT(part, fields[k])] < FEW)
			at = append(line, sizeof(line), at, "%s %s", few++ ? "," : "", field_names[(int)fields[k]]);
	if (!few)
		at = append(line, sizeof(line), at, " none");
	platform_log("%s", line);
}
#endif

static void report(void)
{
	static const int objects_items[] = { PSP_DRAWSPLIT_INSTANCES, PSP_DRAWSPLIT_PARTS_DRAWN };
	/* -1: the model parts drawn inside a whole field (the sky's) */
	static const int world_items[] = { PSP_DRAWSPLIT_LEVEL_DRAWS, PSP_DRAWSPLIT_CLUSTERS, PSP_DRAWSPLIT_OBJECTS, -1 };
	char line[500];
	int at, part, k, item;
	double n = run->frames ? (double)run->frames : 1.0;

	at = snprintf(line, sizeof(line), "drawsplit: %u frames (run %u): d %.1f/%.1f/%.1f; parts %.1f",
		run->frames, run->run_frames, run->window_us[HIST_D] / n / 1000.0,
		percentile(run->histogram[HIST_D], run->run_frames, 500), percentile(run->histogram[HIST_D], run->run_frames, 990),
		run->window_us[HIST_SUM] / n / 1000.0);
	for (part = PART_UNNAMED; part < PARTS; part++)
		at = append(line, sizeof(line), at, "; %s %.1f/%.1f/%.1f", part_names[part], run->window_us[part] / n / 1000.0,
			percentile(run->histogram[part], run->run_frames, 500), percentile(run->histogram[part], run->run_frames, 990));
#ifdef PSP_DRAWSPLIT_SAMPLED
	append(line, sizeof(line), at, "; samples %.1f a frame (most %u)%s", run->window_stamps / n, run->stamps_most,
		run->unbalanced ? "; UNBALANCED" : "");
#else
	append(line, sizeof(line), at, "; stamps %.1f a frame (most %u)%s", run->window_stamps / n, run->stamps_most,
		run->unbalanced ? "; UNBALANCED" : "");
#endif
	platform_log("%s", line);
	report_part(PART_OBJECTS, objects_fields, (int)(sizeof(objects_fields) / sizeof(objects_fields[0])), objects_items,
		(int)(sizeof(objects_items) / sizeof(objects_items[0])), n);
	report_part(PART_WORLD, world_fields, (int)(sizeof(world_fields) / sizeof(world_fields[0])), world_items,
		(int)(sizeof(world_items) / sizeof(world_items[0])), n);
	/* world's render_window fields on a line of their own: on world's line
	they would pass the log's 511 */
	at = snprintf(line, sizeof(line), "drawsplit2 world's window: %u frames", run->frames);
	for (k = PSP_DRAWSPLIT_WINBEGIN; k <= PSP_DRAWSPLIT_WINEND; k++)
		at = append(line, sizeof(line), at, "; %s %.2f ms", field_names[k], run->window_slot_us[SLOT(PART_WORLD, k)] / n / 1000.0);
	platform_log("%s", line);
#ifdef PSP_DRAWSPLIT_SAMPLED
	{
		static const signed char objects_samples[] = { PSP_DRAWSPLIT_NONE, PSP_DRAWSPLIT_WALK, PSP_DRAWSPLIT_LIGHTS,
			PSP_DRAWSPLIT_MATRICES, PSP_DRAWSPLIT_D3D, PSP_DRAWSPLIT_INST, PSP_DRAWSPLIT_STATE, PSP_DRAWSPLIT_ENCODE,
			PSP_DRAWSPLIT_SHADOWS, PSP_DRAWSPLIT_FPW };
		static const signed char world_samples[] = { PSP_DRAWSPLIT_NONE, PSP_DRAWSPLIT_VIS, PSP_DRAWSPLIT_SOUND,
			PSP_DRAWSPLIT_LIGHTS, PSP_DRAWSPLIT_WALK, PSP_DRAWSPLIT_STATE, PSP_DRAWSPLIT_D3D, PSP_DRAWSPLIT_ENCODE,
			PSP_DRAWSPLIT_SKY, PSP_DRAWSPLIT_LIGHTPASS, PSP_DRAWSPLIT_DECALS, PSP_DRAWSPLIT_DIFFUSE, PSP_DRAWSPLIT_SPECULAR };
		static const signed char window_samples[] = { PSP_DRAWSPLIT_WINBEGIN, PSP_DRAWSPLIT_REFLECT, PSP_DRAWSPLIT_FOG,
			PSP_DRAWSPLIT_DETAIL, PSP_DRAWSPLIT_AA, PSP_DRAWSPLIT_WINEND };
		double late = run->window_stamps ? (double)run->window_late_us / run->window_stamps : 0.0;

		at = snprintf(line, sizeof(line), "drawsplit2 samples: %u frames, every %d us: %.1f a frame, %.1f in the drawing; late mean %.1f us, worst %u us",
			run->frames, PERIOD, run->window_stamps2 / n, run->window_stamps / n, late, run->late_most);
		/* 04:1x (3)(b): a mean over a tenth of the period */
		if (late * 10 > PERIOD)
			at = append(line, sizeof(line), at, "; SHARES U (late mean over %d us)", PERIOD / 10);
		if (run->odd)
			at = append(line, sizeof(line), at, "; %u odd", run->odd);
		platform_log("%s", line);
		report_samples("objects", PART_OBJECTS, objects_samples, (int)sizeof(objects_samples));
		report_samples("world", PART_WORLD, world_samples, (int)sizeof(world_samples));
		report_samples("world's window", PART_WORLD, window_samples, (int)sizeof(window_samples));
		if (run->unbalanced || run->unfolded)
			platform_log("drawsplit: %u frames unbalanced (the stack not at its bottom at the render's end, put back); %u render ends with no begin, not split",
				run->unbalanced, run->unfolded);
		for (k = 0; k < SLOTS; k++)
			run->window_samples[k] = 0;
		run->window_late_us = 0;
		run->late_most = 0;
		run->odd = 0;
	}
#else
	if (run->unbalanced || run->unfolded)
		platform_log("drawsplit: %u hooks unbalanced (a close with nothing open, a field left open, or past %d deep); %u frames drawn outside a render phase, not split",
			run->unbalanced, STACK, run->unfolded);
#endif
	for (part = 0; part < HISTS; part++)
		run->window_us[part] = 0;
	for (k = 0; k < SLOTS; k++)
		run->window_slot_us[k] = 0;
	for (part = 0; part < PARTS; part++)
		for (item = 0; item < ITEMS; item++)
			run->window_items[part][item][0] = run->window_items[part][item][1] = 0;
	run->window_stamps = 0;
	run->window_stamps2 = 0;
	run->stamps_most = 0;
	run->frames = 0;
}

#ifndef PSP_DRAWSPLIT_SAMPLED
/* a frame has been drawn, from psp_frame_account (psp_platform.c), with
frametrace's d: the parts are closed, folded and cleared */
void psp_drawsplit_frame(unsigned long draw_us)
{
	unsigned int sum = 0;
	int part, field, item;

	if (on < 0)
	{
		on = psp_keep("drawsplit");
		if (on)
		{
			run = calloc(1, sizeof(*run));
			if (!run)
			{
				platform_log("drawsplit: no room for its %u KB; off", (unsigned)(sizeof(*run) / 1024));
				on = 0;
				return;
			}
			map_phases();
			halo_perf_log_on = 1;
			platform_log("drawsplit: on (the drawing's %d parts, the engine's perf phases in its five files; objects' and world's %d fields; a line each %d frames)",
				PARTS - 1, FIELDS - 1, WINDOW);
		}
		return;
	}
	if (!on)
		return;
	if (!frame_open)
	{
		run->unfolded++;
		return;
	}
	/* close the pair open now; what follows until the render's end is no
	part's */
	{
		unsigned int now = sceKernelGetSystemTimeLow();

		if (PART_OF(current) != PART_OUT)
			frame_us[current] += now - last;
		last = now;
		current = SLOT(PART_OUT, PSP_DRAWSPLIT_NONE);
		frame_stamps++;
	}
	for (part = PART_UNNAMED; part < PARTS; part++)
	{
		unsigned int part_us = 0;

		for (field = 0; field < FIELDS; field++)
		{
			part_us += frame_us[SLOT(part, field)];
			run->window_slot_us[SLOT(part, field)] += frame_us[SLOT(part, field)];
			frame_us[SLOT(part, field)] = 0;
		}
		run->histogram[part][bin(part_us)]++;
		run->window_us[part] += part_us;
		sum += part_us;
	}
	for (part = 0; part < PARTS; part++)
		for (item = 0; item < ITEMS; item++)
		{
			run->window_items[part][item][0] += frame_items[part][item][0];
			run->window_items[part][item][1] += frame_items[part][item][1];
			frame_items[part][item][0] = frame_items[part][item][1] = 0;
		}
	run->histogram[HIST_SUM][bin(sum)]++;
	run->window_us[HIST_SUM] += sum;
	run->histogram[HIST_D][bin((unsigned int)draw_us)]++;
	run->window_us[HIST_D] += draw_us;
	run->window_stamps += frame_stamps;
	run->window_stamps2 += frame_stamps2;
	if (frame_stamps > run->stamps_most)
		run->stamps_most = frame_stamps;
	frame_stamps = 0;
	frame_stamps2 = 0;
	frame_open = 0;
	run->run_frames++;
	if (++run->frames >= WINDOW)
		report();
}

#else
/* the sampled form: the hooks' state and level 1's tables (psp_drawsplit.h) */
volatile struct psp_drawsplit_state psp_drawsplit_state;
unsigned short psp_drawsplit_phase_keep[NUMBER_OF_HALO_PERF_PHASES], psp_drawsplit_phase_code[NUMBER_OF_HALO_PERF_PHASES],
	psp_drawsplit_render_code[NUMBER_OF_HALO_PERF_PHASES + 1];

/* what the alarm adds between two render ends: us and samples by (part,
field); all its runs, those in the drawing, their gaps past the period */
struct sampled
{
	unsigned int us[SLOTS], samples[SLOTS];
	unsigned int all, drawing, late_us, late_most, odd;
};

static struct sampled buffers[2];
/* the buffer the alarm fills; swapped by the render's end */
static volatile unsigned int filling = 0;
static unsigned int sample_last = 0, render_start = 0;
static int frame_open = 0;

#define BARRIER() __asm__ __volatile__("" ::: "memory")

/* the alarm (interrupt context): the gap since its last run to the code on
the stack's top. The hooks store the entry before the index, so the top is
always written */
static SceUInt sample(void *common)
{
	unsigned int now = sceKernelGetSystemTimeLow(), gap = now - sample_last;
	unsigned int code = psp_drawsplit_state.stack[psp_drawsplit_state.sp & (PSP_DRAWSPLIT_STACK - 1)];
	unsigned int part = (code & PSP_DRAWSPLIT_PART_MASK) >> PSP_DRAWSPLIT_PART_SHIFT, field = code & PSP_DRAWSPLIT_FIELD_MASK;
	struct sampled *b = &buffers[filling];

	(void)common;
	sample_last = now;
	b->all++;
	if (part != PART_OUT)
	{
		unsigned int late = gap > PERIOD ? gap - PERIOD : 0;

		if (part < PARTS && field < FIELDS)
		{
			b->us[SLOT(part, field)] += gap;
			b->samples[SLOT(part, field)]++;
		}
		else
			b->odd++;
		b->drawing++;
		b->late_us += late;
		if (late > b->late_most)
			b->late_most = late;
	}
	return PERIOD;
}

/* level 1's tables from map_phases: a mapped phase's begin pushes its part
(keeping the whole bit), an unmapped one copies the top; a render part is
its phase's part, or the render's own time (NONE, or a phase no part has) */
static void fill_tables(void)
{
	int k;

	for (k = 0; k < NUMBER_OF_HALO_PERF_PHASES; k++)
	{
		psp_drawsplit_phase_keep[k] = phase_part[k] < 0 ? 0xffffu : PSP_DRAWSPLIT_WHOLE_BIT;
		psp_drawsplit_phase_code[k] = phase_part[k] < 0 ? 0 : (unsigned short)(phase_part[k] << PSP_DRAWSPLIT_PART_SHIFT);
		psp_drawsplit_render_code[k + 1] = (unsigned short)((phase_part[k] > PART_UNNAMED ? phase_part[k] : PART_UNNAMED)
			<< PSP_DRAWSPLIT_PART_SHIFT);
	}
	psp_drawsplit_render_code[0] = PART_UNNAMED << PSP_DRAWSPLIT_PART_SHIFT;
}

/* the word read, the tables filled, the alarm started: from the first
render's begin */
static void start(void)
{
	SceUID alarm;

	on = psp_keep("drawsplit");
	if (!on)
		return;
	run = calloc(1, sizeof(*run));
	if (!run)
	{
		platform_log("drawsplit: no room for its %u KB; off", (unsigned)(sizeof(*run) / 1024));
		on = 0;
		return;
	}
	map_phases();
	fill_tables();
	sample_last = sceKernelGetSystemTimeLow();
	alarm = sceKernelSetAlarm(PERIOD, sample, NULL);
	if (alarm < 0)
	{
		platform_log("drawsplit: sampled: no alarm (0x%08x); off", (unsigned)alarm);
		on = 0;
		return;
	}
	platform_log("drawsplit: on, sampled every %d us (the drawing's %d parts, the engine's perf phases in its five files; objects' and world's %d fields; a line each %d frames)",
		PERIOD, PARTS - 1, FIELDS - 1, WINDOW);
}

/* main.c, before each HALO_PERF_BEGIN(_halo_perf_render) */
void psp_drawsplit_render_begin(void)
{
	if (on < 0)
		start();
	if (on <= 0)
		return;
	render_start = sceKernelGetSystemTimeLow();
	frame_open = 1;
}

/* main.c, after each HALO_PERF_END(_halo_perf_render): d is the render's
span; the samples since the last end are folded and cleared */
void psp_drawsplit_render_end(void)
{
	unsigned int draw_us, sum = 0, row;
	int part, field, item;
	struct sampled *b;

	if (on <= 0)
		return;
	if (!frame_open)
	{
		run->unfolded++;
		return;
	}
	draw_us = sceKernelGetSystemTimeLow() - render_start;
	frame_open = 0;
	/* the alarm fills the other buffer from here on; it runs to its end
	before this thread runs again, so b is whole */
	b = &buffers[filling];
	filling ^= 1;
	BARRIER();
	if (psp_drawsplit_state.sp & (PSP_DRAWSPLIT_STACK - 1))
	{
		run->unbalanced++;
		psp_drawsplit_state.stack[0] = 0;
		psp_drawsplit_state.sp = 0;
	}
	for (part = PART_UNNAMED; part < PARTS; part++)
	{
		unsigned int part_us = 0;

		for (field = 0; field < FIELDS; field++)
		{
			part_us += b->us[SLOT(part, field)];
			run->window_slot_us[SLOT(part, field)] += b->us[SLOT(part, field)];
			run->window_samples[SLOT(part, field)] += b->samples[SLOT(part, field)];
		}
		run->histogram[part][bin(part_us)]++;
		run->window_us[part] += part_us;
		sum += part_us;
	}
	/* the items by the code's part and whole bit; clusters and objects are
	world's whatever is open, as the exact form counts them */
	for (row = 0; row < PSP_DRAWSPLIT_ITEM_ROWS; row++)
	{
		unsigned int row_part = row & (PSP_DRAWSPLIT_PART_MASK >> PSP_DRAWSPLIT_PART_SHIFT), whole = row >> 4;

		for (item = 0; item < ITEMS; item++)
		{
			unsigned int n = psp_drawsplit_state.items[row][item];

			psp_drawsplit_state.items[row][item] = 0;
			if (item >= PSP_DRAWSPLIT_CLUSTERS)
				run->window_items[PART_WORLD][item][0] += n;
			else if (row_part < PARTS)
				run->window_items[row_part][item][whole] += n;
		}
	}
	run->histogram[HIST_SUM][bin(sum)]++;
	run->window_us[HIST_SUM] += sum;
	run->histogram[HIST_D][bin(draw_us)]++;
	run->window_us[HIST_D] += draw_us;
	run->window_stamps += b->drawing;
	run->window_stamps2 += b->all;
	if (b->drawing > run->stamps_most)
		run->stamps_most = b->drawing;
	run->window_late_us += b->late_us;
	if (b->late_most > run->late_most)
		run->late_most = b->late_most;
	run->odd += b->odd;
	memset(b, 0, sizeof(*b));
	run->run_frames++;
	if (++run->frames >= WINDOW)
		report();
}

/* main.c's pass end (HALO_PERF_FRAME, whether the word is on or not): the
sampled form's fold is the render's end, so nothing here */
void halo_perf_frame(void)
{
}

/* (no unit of this build asks: NONE) */
int halo_perf_render_part_now(void)
{
	return -1;
}
#endif

#endif
