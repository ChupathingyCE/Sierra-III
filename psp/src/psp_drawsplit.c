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
/* X1's split by kind (below, 'by kind'): render_object's depth, and whether
the outer one is split */
static unsigned int object_depth = 0, object_counted = 0;
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
	if (object_depth && run)
	{
		run->unbalanced++;
		object_depth = 0;
		object_counted = 0;
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

/* X1's split by kind (docs/box_draw_main.md §4.4 A2, §3.4.7; the lab's
decisions 2026-11-27 10:4x (5): "X1's near/far split first"). Objects' time
inside each object's render_object (its drawn branch: the lighting pick, the
object list, render_model for it and its children) is charged to the kind its
instances were drawn as: far when every instance it began (rasterizer_model_
begin) was drawn as its far level (psp_far_draw returned non-zero), near when
none was, mixed when some were, empty when it began none (widgets only). The
time outside every render_object (finding the objects, the list, the walk's
lighting pre-pass) is objects' part less the four: "outside". Each object
costs two clock reads more. An object opened inside a whole field (the
shadows') or outside objects' part isn't split. psp_far_draw also gives the
groups it shows, D1's shown groups a frame (§3.4.1) */
enum { KIND_NEAR, KIND_FAR, KIND_MIXED, KIND_EMPTY, KINDS };
static const char *const kind_names[KINDS] = { "near", "far", "mixed", "empty" };
/* [kind][field]; FIELDS: the time inside the object charged to other parts
(level 1's waits) */
static unsigned long long window_kind_us[KINDS][FIELDS + 1];
static unsigned long long window_kind_objects[KINDS], window_kind_instances[KINDS], window_kind_far[KINDS];
static unsigned long long window_far, window_far_groups;
static unsigned int far_instances = 0;
static unsigned int object_start, object_instances, object_far, object_field_us[FIELDS];

/* the pair open now charged up to now, its pair unchanged */
static unsigned int charge_now(void)
{
	unsigned int now = sceKernelGetSystemTimeLow();

	if (PART_OF(current) != PART_OUT)
		frame_us[current] += now - last;
	last = now;
	frame_stamps++;
	frame_stamps2++;
	return now;
}

void psp_drawsplit_object_in(void)
{
	int field;

	if (on <= 0 || object_depth++)
		return;
	object_counted = !whole_open && PART_OF(current) == PART_OBJECTS;
	if (!object_counted)
		return;
	object_start = charge_now();
	for (field = 0; field < FIELDS; field++)
		object_field_us[field] = frame_us[SLOT(PART_OBJECTS, field)];
	object_instances = frame_items[PART_OBJECTS][PSP_DRAWSPLIT_INSTANCES][0];
	object_far = far_instances;
}

void psp_drawsplit_object_out(void)
{
	unsigned int now, instances, far, fields_us = 0, us;
	int field, kind;

	if (on <= 0 || !object_depth || --object_depth || !object_counted)
		return;
	object_counted = 0;
	/* a part change inside the object (none is expected) leaves it unsplit */
	if (PART_OF(current) != PART_OBJECTS)
	{
		run->unbalanced++;
		return;
	}
	now = charge_now();
	instances = frame_items[PART_OBJECTS][PSP_DRAWSPLIT_INSTANCES][0] - object_instances;
	far = far_instances - object_far;
	kind = !instances ? KIND_EMPTY : !far ? KIND_NEAR : far >= instances ? KIND_FAR : KIND_MIXED;
	for (field = 0; field < FIELDS; field++)
	{
		us = frame_us[SLOT(PART_OBJECTS, field)] - object_field_us[field];
		window_kind_us[kind][field] += us;
		fields_us += us;
	}
	us = now - object_start;
	window_kind_us[kind][FIELDS] += us > fields_us ? us - fields_us : 0;
	window_kind_objects[kind]++;
	window_kind_instances[kind] += instances;
	window_kind_far[kind] += far;
}

/* D4's two (docs/box_draw_main.md §3.4.4 'Its items', §4.4 A2): (a)'s merges,
the near parts an instance draws in objects' own (outside a whole field) whose
shader and permutation an earlier part of the same instance drew (one baked
part could hold both), and those sharing the shader alone (another permutation);
an instance is the objects' instance count at the part, in its frame, and one
that drew its far level (psp_drawsplit_far since its begin) is left out, its
decal and transparent parts with it. An instance keys its first MERGE_KEYS
(shader, permutation) pairs; a part past them is counted apart and, unless it
matches one of them, as no merge, so the merges are a floor. (b)'s far models,
by what psp_far_draw shows: a skinned group (its pose moves, so folding freezes
it), several nodes and no skinned group (rigid if those nodes don't move, which
the block doesn't say), or one node (one matrix today: nothing to fold). It
reads no clock */
#define MERGE_KEYS 16
static unsigned int merge_frame = 0, merge_instance = 0, merge_keys = 0, merge_far_frame = 0, merge_far_instance = 0;
static int merge_sharing = 0;
static const void *merge_shader[MERGE_KEYS];
static int merge_permutation[MERGE_KEYS];
static unsigned long long window_merge_instances = 0, window_merge_parts = 0, window_merge_state = 0, window_merge_shader = 0,
	window_merge_sharing = 0, window_merge_past = 0, window_far_form[3];
/* D5's key (docs/box_draw_main.md §3.4.5 'Its first asks'): among the same
near parts, across their instances, the distinct (shader, permutation) keys a
frame, and the parts that would follow a part of their own key once the
frame's parts are sorted by it (a part whose key the frame drew before). The
table holds SORT_KEYS keys and is closed at psp_drawsplit_frame; a part past
them that matches none is counted apart, so the followers are a floor */
#define SORT_KEYS 64
static unsigned int sort_keys = 0;
static const void *sort_shader[SORT_KEYS];
static int sort_permutation[SORT_KEYS];
static unsigned long long window_sort_keys = 0, window_sort_follow = 0, window_sort_past = 0;

void psp_drawsplit_far(unsigned int groups, int form)
{
	if (on <= 0)
		return;
	far_instances++;
	window_far++;
	window_far_groups += groups;
	if (form >= 0 && form < 3)
		window_far_form[form]++;
	if (!whole_open && PART_OF(current) == PART_OBJECTS)
	{
		merge_far_frame = run->run_frames;
		merge_far_instance = frame_items[PART_OBJECTS][PSP_DRAWSPLIT_INSTANCES][0];
	}
}

static void merge_part(const void *shader, int permutation)
{
	unsigned int instance, k;
	int same = 0;

	if (whole_open || PART_OF(current) != PART_OBJECTS)
		return;
	instance = frame_items[PART_OBJECTS][PSP_DRAWSPLIT_INSTANCES][0];
	if (!instance || (merge_far_frame == run->run_frames && merge_far_instance == instance))
		return;
	if (merge_frame != run->run_frames || merge_instance != instance)
	{
		merge_frame = run->run_frames;
		merge_instance = instance;
		merge_keys = 0;
		merge_sharing = 0;
		window_merge_instances++;
	}
	window_merge_parts++;
	for (k = 0; k < sort_keys; k++)
		if (sort_shader[k] == shader && sort_permutation[k] == permutation)
			break;
	if (k < sort_keys)
		window_sort_follow++;
	else if (sort_keys < SORT_KEYS)
	{
		sort_shader[sort_keys] = shader;
		sort_permutation[sort_keys] = permutation;
		sort_keys++;
		window_sort_keys++;
	}
	else
		window_sort_past++;
	for (k = 0; k < merge_keys && same < 2; k++)
		if (merge_shader[k] == shader)
			same = merge_permutation[k] == permutation ? 2 : 1;
	if (same == 2)
		window_merge_state++;
	else
	{
		if (same)
			window_merge_shader++;
		if (merge_keys < MERGE_KEYS)
		{
			merge_shader[merge_keys] = shader;
			merge_permutation[merge_keys] = permutation;
			merge_keys++;
		}
		else
			window_merge_past++;
	}
	if (same && !merge_sharing)
	{
		merge_sharing = 1;
		window_merge_sharing++;
	}
}

static void report_merge(double n)
{
	platform_log("drawsplit2 merge: %.1f near instances a frame drew %.1f parts in objects' own; %.1f a frame share an earlier "
		"part's shader and permutation in their instance, %.1f its shader alone; %.1f instances a frame with one or more; "
		"%llu parts past an instance's %d keys; far models %.1f a frame: %.1f with a skinned group shown, %.1f on several "
		"nodes and none skinned, %.1f on one node",
		window_merge_instances / n, window_merge_parts / n, window_merge_state / n, window_merge_shader / n,
		window_merge_sharing / n, window_merge_past, MERGE_KEYS, (window_far_form[0] + window_far_form[1] + window_far_form[2]) / n,
		window_far_form[0] / n, window_far_form[1] / n, window_far_form[2] / n);
	window_merge_instances = window_merge_parts = window_merge_state = window_merge_shader = window_merge_sharing = 0;
	window_merge_past = window_far_form[0] = window_far_form[1] = window_far_form[2] = 0;
}

static void report_sort(double n)
{
	platform_log("drawsplit2 sort: the near parts' %.1f distinct (shader, permutation) keys a frame; %.1f parts a frame follow "
		"a part of their own key in sorted order; %llu parts past the frame's %d keys",
		window_sort_keys / n, window_sort_follow / n, window_sort_past, SORT_KEYS);
	window_sort_keys = window_sort_follow = window_sort_past = 0;
}

/* D3's store (docs/box_draw_main.md §3.4.2 'Memory', §4.4 A2): the distinct
model parts the GE drew (rasterizer_model_draw's psp_model_draw returning
TRUE), keyed as D3 would bake a list: the part's triangle and vertex buffers (a
model's part at an LOD has its own), its shader and the shader's permutation.
Counted from the run's start, so a map's count is what its run drew: a floor of
the map's store. A key's first draw is named objects' own (outside a whole
field) or elsewhere (the first person weapon's and the sky's whole fields,
another part). A list's ~1 KB (~250 words with 2-4 passes, ge_audit M6) is the
page's design size, printed beside the count as [D], never a measured byte.
The table holds STORE_FULL keys; a draw of a key past them is counted as
missed, and the count is then a floor again. It reads no clock */
#define STORE_KEYS 2048
#define STORE_FULL (STORE_KEYS * 3 / 4)
struct store_key
{
	const void *triangles, *vertices, *shader;
	int permutation;
	unsigned int window;
};
static struct store_key *store = NULL;
static int store_failed = 0;
static unsigned int store_keys = 0, store_objects = 0, store_missed = 0, store_window = 1;
static unsigned long long window_store_parts = 0, window_store_seen = 0, window_store_new = 0;

void psp_drawsplit_store(int drawn, const void *triangles, const void *vertices, const void *shader, int permutation)
{
	unsigned int h;
	struct store_key *key;

	if (on <= 0 || !drawn || !triangles)
		return;
	merge_part(shader, permutation);
	if (!store)
	{
		if (store_failed)
			return;
		store = calloc(STORE_KEYS, sizeof(*store));
		if (!store)
		{
			store_failed = 1;
			platform_log("drawsplit2 store: no room for its %u KB; not counted", (unsigned)(STORE_KEYS * sizeof(*store) / 1024));
			return;
		}
	}
	window_store_parts++;
	h = (unsigned int)((unsigned long)triangles >> 2) * 2654435761u ^ (unsigned int)((unsigned long)vertices >> 2) * 2246822519u
		^ (unsigned int)((unsigned long)shader >> 2) * 3266489917u ^ (unsigned int)permutation;
	for (h &= STORE_KEYS - 1; store[h].triangles; h = (h + 1) & (STORE_KEYS - 1))
	{
		key = &store[h];
		if (key->triangles == triangles && key->vertices == vertices && key->shader == shader && key->permutation == permutation)
		{
			if (key->window != store_window)
			{
				key->window = store_window;
				window_store_seen++;
			}
			return;
		}
	}
	if (store_keys >= STORE_FULL)
	{
		store_missed++;
		return;
	}
	key = &store[h];
	key->triangles = triangles;
	key->vertices = vertices;
	key->shader = shader;
	key->permutation = permutation;
	key->window = store_window;
	store_keys++;
	if (!whole_open && PART_OF(current) == PART_OBJECTS)
		store_objects++;
	window_store_seen++;
	window_store_new++;
}

static void report_store(double n)
{
	if (!store)
		return;
	platform_log("drawsplit2 store: %u distinct parts drawn since the run's start (%u first in objects' own, %u elsewhere), "
		"~%u KB at ~1 KB a list [D]; this window %.1f parts drawn a frame, %llu distinct, %llu new; %u draws past the table's %u keys",
		store_keys, store_objects, store_keys - store_objects, store_keys, window_store_parts / n, window_store_seen,
		window_store_new, store_missed, STORE_FULL);
	window_store_parts = window_store_seen = window_store_new = 0;
	store_window++;
}

/* D6's lists (docs/box_draw_main.md §3.4.6 'Its first asks': "the lists
CALLed a frame and their bytes", beside the store's count): psp_ge.c's
psp_ge_draw_model hands over each model part's bytes in the GE's list, read
across its body (sceGuCheckList before and after), with the kinds' line's
split of the models' share: bone matrices (13-word loads, sub-lists and their
calls) and data copied in (kept indices, cut corners, palettes) are a frame's,
and the rest, the commands and their inline data, is what a baked list would
hold, so it is the list's size. A list is keyed by the part's GE geometry
(vertices and indices as locked) and its base texture's key; its size is its
first draw's commands, and a later draw whose commands differ is counted. The
main CPU's draws only: the ME's replay counts none, so on a PSP build with the
replay on the ME the line reads nothing. The sky's parts are counted apart; a
draw that wrote nothing (left out for room) or whose list went backwards
(unmeasured) is counted and not split. A frame is the run's frame count when
the part is drawn. The table holds LIST_FULL keys; a draw of a key past them is
counted as missed, and the distinct counts are then floors. It reads no clock */
#define LIST_KEYS 2048
#define LIST_FULL (LIST_KEYS * 3 / 4)
struct list_key
{
	const void *vertices, *indices;
	unsigned int key, bytes, frame, window;
};
static struct list_key *lists = NULL;
static int lists_failed = 0;
static unsigned int list_keys = 0, list_largest = 0, list_missed = 0, list_window = 1;
static unsigned long long list_total = 0, window_list_parts = 0, window_list_bytes = 0, window_list_bone = 0,
	window_list_data = 0, window_list_frame = 0, window_list_frame_bytes = 0, window_list_seen = 0, window_list_seen_bytes = 0,
	window_list_varied = 0, window_list_sky = 0, window_list_empty = 0, window_list_lost = 0;

void psp_drawsplit_list(const void *vertices, const void *indices, unsigned int key, int sky, unsigned int before,
	unsigned int after, unsigned long bone, unsigned long data)
{
	unsigned int h, bytes, commands;
	struct list_key *slot;

	if (on <= 0)
		return;
	if (sky)
	{
		window_list_sky++;
		return;
	}
	if (after < before || bone + data > (unsigned long)(after - before))
	{
		window_list_lost++;
		return;
	}
	bytes = after - before;
	if (!bytes)
	{
		window_list_empty++;
		return;
	}
	commands = bytes - (unsigned int)(bone + data);
	window_list_parts++;
	window_list_bytes += bytes;
	window_list_bone += bone;
	window_list_data += data;
	if (!lists)
	{
		if (lists_failed)
			return;
		lists = calloc(LIST_KEYS, sizeof(*lists));
		if (!lists)
		{
			lists_failed = 1;
			platform_log("drawsplit2 lists: no room for its %u KB; not keyed", (unsigned)(LIST_KEYS * sizeof(*lists) / 1024));
			return;
		}
	}
	h = (unsigned int)((unsigned long)vertices >> 2) * 2654435761u ^ (unsigned int)((unsigned long)indices >> 2) * 2246822519u
		^ key * 3266489917u;
	for (h &= LIST_KEYS - 1; lists[h].vertices; h = (h + 1) & (LIST_KEYS - 1))
		if (lists[h].vertices == vertices && lists[h].indices == indices && lists[h].key == key)
			break;
	slot = &lists[h];
	if (!slot->vertices)
	{
		if (list_keys >= LIST_FULL)
		{
			list_missed++;
			return;
		}
		slot->vertices = vertices;
		slot->indices = indices;
		slot->key = key;
		slot->bytes = commands;
		list_keys++;
		list_total += commands;
		if (commands > list_largest)
			list_largest = commands;
	}
	else if (slot->bytes != commands)
		window_list_varied++;
	if (slot->frame != run->run_frames + 1)
	{
		slot->frame = run->run_frames + 1;
		window_list_frame++;
		window_list_frame_bytes += slot->bytes;
	}
	if (slot->window != list_window)
	{
		slot->window = list_window;
		window_list_seen++;
		window_list_seen_bytes += slot->bytes;
	}
}

static void report_lists(double n)
{
	platform_log("drawsplit2 lists: %.1f model parts a frame (main CPU) wrote %.1f KB a frame: commands %.1f KB (a baked list's), "
		"bones %.1f, data copied in %.1f; distinct lists %.1f a frame, %.1f KB; this window %llu, %.1f KB; since the run's start "
		"%u, %.1f KB, largest %u B; %llu draws unlike their list's first; sky %.1f a frame apart, %.1f wrote nothing, %llu "
		"unmeasured; %u past the table's %u keys",
		window_list_parts / n, window_list_bytes / 1024.0 / n,
		(window_list_bytes - window_list_bone - window_list_data) / 1024.0 / n, window_list_bone / 1024.0 / n,
		window_list_data / 1024.0 / n, window_list_frame / n, window_list_frame_bytes / 1024.0 / n, window_list_seen,
		window_list_seen_bytes / 1024.0, list_keys, list_total / 1024.0, list_largest, window_list_varied, window_list_sky / n,
		window_list_empty / n, window_list_lost, list_missed, LIST_FULL);
	window_list_parts = window_list_bytes = window_list_bone = window_list_data = window_list_frame = 0;
	window_list_frame_bytes = window_list_seen = window_list_seen_bytes = window_list_varied = 0;
	window_list_sky = window_list_empty = window_list_lost = 0;
	list_window++;
}

/* A2b (1)'s words (the lab's briefs/draw_main.md 'Added 13:4x', A2b (1);
box_draw_main.md §3.4.2): for a sample of D6's lists, which command words of a
later draw differ from the list's first, by class: D3's per-frame side
(matrices, bones, lights) or other (colours, texture, shader, state, geometry,
the rest). psp_ge.c hands over each measured part's words as written: the
list's pointer before and after its body. The walk follows the JUMPs over the
regions taken (copied data, bone sub-lists) and stops at the part's end. An
address (VADDR, IADDR, JUMP, CALL, with the BASE before it) inside the part's
own span is kept as its offset from the part's start, so the same list written
elsewhere in the next frame's list compares equal; one outside it (the model's
vertices, a bone sub-list, the texture) as a 23-bit hash of itself. BASE is
folded into the address after it and not compared. The sample is the first
lists drawn until WORDS_FULL lists or WORDS_POOL words are held, each its first
draw's words (at most WORDS_MAX); a later draw of a sampled list is compared
command by command, each command's values in order against the first's, a
value missing or extra counting as a word that differs. The sky's parts are
left out, as the lists' are. The counts are a window's (300 frames), not a
frame's. It reads no clock */
#define WORDS_KEYS 512
#define WORDS_FULL (WORDS_KEYS * 3 / 4)
#define WORDS_POOL 32768
#define WORDS_MAX 2048
enum { W_MATRICES, W_BONES, W_LIGHTS, W_COLOURS, W_TEXTURE, W_SHADER, W_STATE, W_GEOMETRY, W_REST, W_CLASSES };
#define W_FRAME ((1u << W_MATRICES) | (1u << W_BONES) | (1u << W_LIGHTS))
static const char *const word_class_names[W_CLASSES] = { "matrices", "bones", "lights", "colours", "texture", "shader",
	"state", "geometry", "rest" };
struct words_key
{
	const void *vertices, *indices;
	unsigned int key, at, count;
};
struct words_room
{
	struct words_key keys[WORDS_KEYS];
	unsigned int pool[WORDS_POOL], now[WORDS_MAX], sorted[2][WORDS_MAX];
	unsigned short counts[2][256], starts[2][256];
};
static struct words_room *words = NULL;
static int words_failed = 0;
static unsigned int words_keys = 0, words_used = 0, words_long = 0, words_past = 0;
static unsigned long long window_words_compared = 0, window_words_unlike = 0, window_words_count = 0,
	window_words_frame = 0, window_words_colours = 0, window_words_geometry = 0, window_words_lost = 0,
	window_word_draws[W_CLASSES], window_word_words[W_CLASSES], window_word_command[256];

/* a GE command's class (pspgu's command numbers) */
static int word_class(unsigned int command)
{
	if (command >= 0x3A && command <= 0x41)
		return W_MATRICES; /* world, view, projection, texture-generation matrices */
	if ((command >= 0x2A && command <= 0x33) || command == 0x0A)
		return W_BONES; /* bone matrices, morph weights, a bone sub-list's CALL */
	if ((command >= 0x17 && command <= 0x1B) || (command >= 0x5C && command <= 0x9A))
		return W_LIGHTS; /* lighting and light enables, ambient, the lights' types, positions, directions, colours */
	if (command >= 0x53 && command <= 0x5B)
		return W_COLOURS; /* the material's colours and alpha (sceGuColor: a part's tint or light level) */
	if (command == 0x50 || command == 0xC9 || command == 0xCA || (command >= 0xCD && command <= 0xCF)
		|| (command >= 0xD8 && command <= 0xDB) || (command >= 0xDF && command <= 0xE6))
		return W_SHADER; /* shading, texture function and colour, fog, colour and alpha tests, blend, dither, logic op */
	if ((command >= 0x48 && command <= 0x4B) || (command >= 0xA0 && command <= 0xD0))
		return W_TEXTURE; /* texture scale and offset, addresses, sizes, CLUT, mode, filter, wrap, flush */
	if ((command >= 0x1C && command <= 0x28) || (command >= 0x42 && command <= 0x47) || command == 0x4C || command == 0x4D
		|| command == 0x51 || command == 0x9B || (command >= 0xD2 && command <= 0xD7) || (command >= 0xDC && command <= 0xDE)
		|| (command >= 0xE7 && command <= 0xE9))
		return W_STATE; /* enables, viewport, offset, front face, scissor, depth range and test, stencil, masks */
	if (command <= 0x02 || command == 0x04 || command == 0x07 || command == 0x08 || command == 0x10 || command == 0x12
		|| command == 0x13 || command == 0x14)
		return W_GEOMETRY; /* vertex and index addresses, the draw, bounding box, jump, base, vertex type, offset, origin */
	return W_REST;
}

/* the part's words from begin to end, its addresses as above; WORDS_MAX + 1
when it can't be walked (longer than WORDS_MAX, a jump out of the part or back) */
static unsigned int words_walk(const unsigned int *begin, const unsigned int *end, unsigned int *out)
{
	unsigned int from = (unsigned int)(unsigned long)begin & 0x0FFFFFFFu, to = (unsigned int)(unsigned long)end & 0x0FFFFFFFu;
	unsigned int base = 0, n = 0, steps = 0;
	const unsigned int *word = begin;

	if (to < from)
		return WORDS_MAX + 1;
	while (word != end)
	{
		unsigned int w = *word, command = w >> 24, here = (unsigned int)(unsigned long)word & 0x0FFFFFFFu, at;

		if (++steps > 4 * WORDS_MAX || n >= WORDS_MAX)
			return WORDS_MAX + 1;
		if (command == 0x10)
		{
			base = w;
			word++;
			continue;
		}
		if (command == 0x01 || command == 0x02 || command == 0x08 || command == 0x0A)
		{
			at = ((base & 0xF0000u) << 8) | (w & 0xFFFFFFu);
			out[n++] = (command << 24) | (at >= from && at < to ? 0x800000u | (at - from) : (at * 2654435761u) >> 9);
			if (command == 0x08)
			{
				if (at <= here || at > to)
					return WORDS_MAX + 1;
				word = (const unsigned int *)((const char *)word + (at - here));
			}
			else
				word++;
			continue;
		}
		out[n++] = w;
		word++;
	}
	return n;
}

/* words[0..n) by command, each command's values in their order */
static void words_sort(const unsigned int *in, unsigned int n, int side)
{
	unsigned short *counts = words->counts[side], *starts = words->starts[side], next[256];
	unsigned int k, at = 0;

	memset(counts, 0, sizeof(words->counts[side]));
	for (k = 0; k < n; k++)
		counts[in[k] >> 24]++;
	for (k = 0; k < 256; k++)
	{
		starts[k] = next[k] = (unsigned short)at;
		at += counts[k];
	}
	for (k = 0; k < n; k++)
		words->sorted[side][next[in[k] >> 24]++] = in[k];
}

static void words_compare(const unsigned int *first, unsigned int first_count, unsigned int count)
{
	unsigned int c, k, mask = 0;

	words_sort(first, first_count, 0);
	words_sort(words->now, count, 1);
	for (c = 0; c < 256; c++)
	{
		unsigned int a = words->counts[0][c], b = words->counts[1][c], common = a < b ? a : b;
		unsigned int differ = a > b ? a - b : b - a;
		const unsigned int *x = &words->sorted[0][words->starts[0][c]], *y = &words->sorted[1][words->starts[1][c]];

		for (k = 0; k < common; k++)
			differ += x[k] != y[k];
		if (differ)
		{
			int class = word_class(c);

			window_word_words[class] += differ;
			window_word_command[c] += differ;
			mask |= 1u << class;
		}
	}
	window_words_compared++;
	if (!mask)
		return;
	window_words_unlike++;
	window_words_count += first_count != count;
	window_words_frame += !(mask & ~W_FRAME);
	window_words_colours += !(mask & ~(W_FRAME | (1u << W_COLOURS)));
	window_words_geometry += !(mask & ~(W_FRAME | (1u << W_COLOURS) | (1u << W_GEOMETRY)));
	for (c = 0; c < W_CLASSES; c++)
		window_word_draws[c] += (mask >> c) & 1;
}

void psp_drawsplit_words(const void *vertices, const void *indices, unsigned int key, int sky, const unsigned int *begin,
	const unsigned int *end)
{
	unsigned int h, count;
	struct words_key *slot;

	if (on <= 0 || sky || begin == end)
		return;
	if (!words)
	{
		if (words_failed)
			return;
		words = calloc(1, sizeof(*words));
		if (!words)
		{
			words_failed = 1;
			platform_log("drawsplit2 words: no room for its %u KB; not compared", (unsigned)(sizeof(*words) / 1024));
			return;
		}
	}
	h = (unsigned int)((unsigned long)vertices >> 2) * 2654435761u ^ (unsigned int)((unsigned long)indices >> 2) * 2246822519u
		^ key * 3266489917u;
	for (h &= WORDS_KEYS - 1; words->keys[h].vertices; h = (h + 1) & (WORDS_KEYS - 1))
		if (words->keys[h].vertices == vertices && words->keys[h].indices == indices && words->keys[h].key == key)
			break;
	slot = &words->keys[h];
	if (!slot->vertices && (words_keys >= WORDS_FULL || words_used >= WORDS_POOL))
		return; /* the sample is full: a list first drawn now isn't held */
	count = words_walk(begin, end, words->now);
	if (count > WORDS_MAX)
	{
		window_words_lost++;
		if (!slot->vertices)
			words_long++;
		return;
	}
	if (slot->vertices)
	{
		words_compare(&words->pool[slot->at], slot->count, count);
		return;
	}
	if (words_used + count > WORDS_POOL)
	{
		words_past++;
		return;
	}
	slot->vertices = vertices;
	slot->indices = indices;
	slot->key = key;
	slot->at = words_used;
	slot->count = count;
	memcpy(&words->pool[words_used], words->now, count * sizeof(unsigned int));
	words_used += count;
	words_keys++;
}

static int append(char *line, int size, int at, const char *format, ...);

static void report_words(void)
{
	char line[512];
	int at, k, top;
	unsigned int c;

	if (!words)
		return;
	platform_log("drawsplit2 words: %u lists sampled, %.1f KB of their first draws' words (%u too long, %u past the "
		"pool's %u KB); this window %llu later draws of them compared, %llu unlike their first (%llu a different count of "
		"words): %llu only in D3's per-frame words (matrices, bones, lights), %llu only in those and colours, %llu only in "
		"those, colours and geometry; %llu unmeasured", words_keys, words_used * 4 / 1024.0, words_long, words_past,
		(unsigned)(WORDS_POOL * 4 / 1024), window_words_compared, window_words_unlike, window_words_count,
		window_words_frame, window_words_colours, window_words_geometry, window_words_lost);
	at = snprintf(line, sizeof(line), "drawsplit2 words by class (unlike draws / words differing):");
	for (k = 0; k < W_CLASSES; k++)
		at = append(line, sizeof(line), at, "%s %s %llu/%llu", k ? ";" : "", word_class_names[k], window_word_draws[k],
			window_word_words[k]);
	at = append(line, sizeof(line), at, "; most differing commands:");
	for (top = 0; top < 6; top++)
	{
		unsigned int best = 256;

		for (c = 0; c < 256; c++)
			if (window_word_command[c] && (best == 256 || window_word_command[c] > window_word_command[best]))
				best = c;
		if (best == 256)
			break;
		at = append(line, sizeof(line), at, " 0x%02X %llu", best, window_word_command[best]);
		window_word_command[best] = 0;
	}
	platform_log("%s", line);
	window_words_compared = window_words_unlike = window_words_count = window_words_frame = 0;
	window_words_colours = window_words_geometry = window_words_lost = 0;
	memset(window_word_draws, 0, sizeof(window_word_draws));
	memset(window_word_words, 0, sizeof(window_word_words));
	memset(window_word_command, 0, sizeof(window_word_command));
}

/* D2's pieces (docs/box_draw_main.md §3.4.3 'Memory', §4.4 A2): the engine's
psp_drawsplit_pieces (structure_visibility.c, psp_fixups.py DRAWSPLIT2_PIECES_C)
splits a BSP's surfaces into (cluster, material) pieces, the lists D2 would
bake, and hands this file the map's counts once a BSP and each window's visible
pieces. The index bytes are the pieces' triangles at 6 B (three 16-bit
indices), measured from the BSP; the passes' words beside them are the page's
~1,300 a draw today (box_draw_list.md §12.1), printed as [D], never a measured
byte. It reads no clock */
static struct psp_drawsplit_pieces_map pieces_map;
static int pieces_maps = 0;
static unsigned long long window_pieces[10];
static unsigned int window_pieces_calls = 0;

int psp_drawsplit_pieces_on(void)
{
	return on > 0;
}

void *psp_drawsplit_pieces_alloc(unsigned long bytes)
{
	void *block = malloc(bytes ? bytes : 1);

	if (!block)
		platform_log("drawsplit2 pieces: no room for %lu KB; not counted", (bytes + 1023) / 1024);
	return block;
}

void psp_drawsplit_pieces_free(void *block)
{
	free(block);
}

void psp_drawsplit_pieces_map(const struct psp_drawsplit_pieces_map *map)
{
	pieces_map = *map;
	pieces_maps++;
	platform_log("drawsplit2 pieces map %d: %ld surfaces in %ld clusters and %ld materials -> %ld (cluster, material) pieces "
		"holding %ld triangles, the largest %ld; %ld KB of indices at 6 B a triangle [M]; passes' words ~%ld KB at a draw's "
		"~1,300 words a piece [D]; %ld surfaces in no cluster, %ld listings past a surface's first cluster, %ld vertex indices past their material's",
		pieces_maps, map->surfaces, map->clusters, map->materials, map->pieces, map->triangles, map->largest,
		(map->triangles * 6 + 1023) / 1024, (map->pieces * 1300 * 4 + 1023) / 1024, map->unclustered, map->shared,
		map->bad_vertices);
}

void psp_drawsplit_pieces_window(const struct psp_drawsplit_pieces_window *window)
{
	window_pieces[0] += (unsigned long)window->visible;
	window_pieces[1] += (unsigned long)window->triangles;
	window_pieces[2] += (unsigned long)window->unpieced;
	window_pieces[3] += (unsigned long)window->outside;
	window_pieces[4] += (unsigned long)window->inside;
	window_pieces[5] += (unsigned long)window->near_plane;
	window_pieces[6] += (unsigned long)window->far_plane;
	window_pieces[7] += (unsigned long)window->fog_on;
	window_pieces[8] += (unsigned long)window->fog_cross;
	window_pieces[9] += (unsigned long)window->fog_below;
	window_pieces_calls++;
}

static void report_pieces(double n)
{
	int k;

	if (!window_pieces_calls)
		return;
	platform_log("drawsplit2 pieces: %u windows; %.1f visible pieces a frame (%.1f wholly inside the view, %.1f crossing the near plane, "
		"%.1f the far, %.1f outside it), %.1f visible triangles a frame, %.1f in no piece; planar fog in %llu windows: "
		"%.1f pieces a frame crossing its plane, %.1f wholly below it, the eye above",
		window_pieces_calls, window_pieces[0] / n, window_pieces[4] / n, window_pieces[5] / n, window_pieces[6] / n,
		window_pieces[3] / n, window_pieces[1] / n, window_pieces[2] / n, window_pieces[7], window_pieces[8] / n,
		window_pieces[9] / n);
	for (k = 0; k < 10; k++)
		window_pieces[k] = 0;
	window_pieces_calls = 0;
}

static int append(char *line, int size, int at, const char *format, ...);

/* the kinds' lines: one of the far groups and what lies outside, then one a
kind with its fields */
static void report_kinds(double n)
{
	static const signed char fields[] = { PSP_DRAWSPLIT_WALK, PSP_DRAWSPLIT_LIGHTS, PSP_DRAWSPLIT_MATRICES,
		PSP_DRAWSPLIT_D3D, PSP_DRAWSPLIT_INST, PSP_DRAWSPLIT_STATE, PSP_DRAWSPLIT_ENCODE, PSP_DRAWSPLIT_NONE };
	char line[500];
	int at, kind, k;
	unsigned long long part_us = 0, inside_us = 0;

	for (k = 0; k < FIELDS; k++)
	{
		part_us += run->window_slot_us[SLOT(PART_OBJECTS, k)];
		for (kind = 0; kind < KINDS; kind++)
			inside_us += window_kind_us[kind][k];
	}
	platform_log("drawsplit2 objects by kind: %u frames: part %.2f ms; inside render_object %.2f ms, outside %.2f ms; "
		"far drawn %.1f a frame, their shown groups %.1f (%.2f a far draw)", run->frames, part_us / n / 1000.0,
		inside_us / n / 1000.0, (part_us > inside_us ? part_us - inside_us : 0) / n / 1000.0, window_far / n,
		window_far_groups / n, window_far ? (double)window_far_groups / window_far : 0.0);
	for (kind = 0; kind < KINDS; kind++)
	{
		unsigned long long us = 0;

		for (k = 0; k < FIELDS; k++)
			us += window_kind_us[kind][k];
		at = snprintf(line, sizeof(line), "drawsplit2 objects' %s: %.1f objects, %.1f inst, %.1f far a frame; %.2f ms (%.1f us an object)",
			kind_names[kind], window_kind_objects[kind] / n, window_kind_instances[kind] / n, window_kind_far[kind] / n,
			us / n / 1000.0, window_kind_objects[kind] ? (double)us / window_kind_objects[kind] : 0.0);
		for (k = 0; k < (int)sizeof(fields); k++)
			at = append(line, sizeof(line), at, "; %s %.2f", field_names[(int)fields[k]], window_kind_us[kind][(int)fields[k]] / n / 1000.0);
		append(line, sizeof(line), at, "; waits %.2f ms", window_kind_us[kind][FIELDS] / n / 1000.0);
		platform_log("%s", line);
	}
	for (kind = 0; kind < KINDS; kind++)
	{
		for (k = 0; k <= FIELDS; k++)
			window_kind_us[kind][k] = 0;
		window_kind_objects[kind] = window_kind_instances[kind] = window_kind_far[kind] = 0;
	}
	window_far = window_far_groups = 0;
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
#ifndef PSP_DRAWSPLIT_SAMPLED
	report_kinds(n);
	report_store(n);
	report_lists(n);
	report_words();
	report_merge(n);
	report_sort(n);
	report_pieces(n);
#endif
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
	sort_keys = 0;
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
