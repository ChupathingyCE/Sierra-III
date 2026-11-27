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
hooks are each a flag's test and nothing is allocated. */

#ifdef PSP_DRAWSPLIT

#include <pspkernel.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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
	unsigned long long window_stamps, window_stamps2;
	unsigned int stamps_most;
};

static struct drawsplit_run *run = NULL;
static int on = -1;
static signed char phase_part[NUMBER_OF_HALO_PERF_PHASES];
static unsigned char phase_depth[NUMBER_OF_HALO_PERF_PHASES];
static unsigned short stack[STACK];
/* whole_open: 1 inside a whole field, more for the field hooks nested in it;
lost: field opens refused (past STACK deep, or no such field), whose closes
are skipped */
static int depth = 0, current = SLOT(PART_OUT, PSP_DRAWSPLIT_NONE), frame_open = 0, whole_open = 0, lost = 0;
static unsigned int last = 0, frame_stamps = 0, frame_stamps2 = 0;
static unsigned int frame_us[SLOTS];
static unsigned int frame_items[PARTS][ITEMS][2];

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
		if (fields[k].item >= 0)
		{
			unsigned long long items_seen = run->window_items[part][(int)fields[k].item][0];

			if (items_seen)
				at = append(line, sizeof(line), at, " %.1f/%s", (double)us / items_seen, item_names[(int)fields[k].item]);
			else
				at = append(line, sizeof(line), at, " -/%s", item_names[(int)fields[k].item]);
		}
	}
	if (part == PART_OBJECTS)
		at = append(line, sizeof(line), at, "; stamps2 %.1f a frame", run->window_stamps2 / n);
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
	append(line, sizeof(line), at, "; stamps %.1f a frame (most %u)%s", run->window_stamps / n, run->stamps_most,
		run->unbalanced ? "; UNBALANCED" : "");
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
	if (run->unbalanced || run->unfolded)
		platform_log("drawsplit: %u hooks unbalanced (a close with nothing open, a field left open, or past %d deep); %u frames drawn outside a render phase, not split",
			run->unbalanced, STACK, run->unfolded);
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

#endif
