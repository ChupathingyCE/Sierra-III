/* The drawing's split (docs/box_draw_main.md §1.3; keep word "drawsplit",
build switch PSP_DRAWSPLIT=1): the main CPU's drawing a frame (frametrace's
d: main_game_render and the loading veil) split by its parts, every frame,
in the build's own form.

Level 1. The engine's own perf phases (halo_perf_log.h) are compiled in for
the drawing's files only (psp_build.py: render.c, main.c, interface.c,
xbox_texture_cache.c, rasterizer_xbox_widgets.c), never the tick's, so the
tick is the build's as it is without the switch. Each hook that changes the
part charged reads the clock (sceKernelGetSystemTimeLow, ~0.5 us) and
charges the time since the last read to the part that was open: the render's
parts follow each other (halo_perf_render_part), and the waits and the
render's sound nest inside them (halo_perf_begin and _end, a stack). A hook
that leaves the same part charged reads nothing.

The frame. The render phase's begin (main.c, before main_game_render) opens
it; psp_frame_account (psp_platform.c) closes it with d, so the parts cover
the same span d does, less the few instructions between the render's begin
and d's first clock read. Time from there to the render's end (the frame's
end, e) is charged to no part.

The line, every 300 frames, the parts in ms a frame: the window's mean, and
p50 and p99 over the run so far from a histogram in 0.1 ms bins:
  drawsplit: N frames (run F): d M/P50/P99; parts M; unnamed M/P50/P99;
  world M/P50/P99, ...; stamps M a frame (most K)
"unnamed" is the render's own time outside every part (between them, and
before the first). Measurement only: nothing the game reads is written. With
the word off the hooks are each a flag's test and nothing is allocated. */

#ifdef PSP_DRAWSPLIT

#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include "halo_perf_log.h"

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

/* d and the parts' sum have histograms of their own */
#define HIST_D PARTS
#define HIST_SUM (PARTS + 1)
#define HISTS (PARTS + 2)
/* 0.1 ms bins to 60 ms; the last holds the rest */
#define BINS 600
#define WINDOW 300
#define STACK 8

struct drawsplit_run
{
	unsigned int histogram[HISTS][BINS];
	unsigned long long window_us[HISTS];
	unsigned int frames, run_frames, unfolded, unbalanced;
	unsigned long long window_stamps;
	unsigned int stamps_most;
};

static struct drawsplit_run *run = NULL;
static int on = -1;
static signed char phase_part[NUMBER_OF_HALO_PERF_PHASES];
static unsigned char phase_depth[NUMBER_OF_HALO_PERF_PHASES];
static unsigned char stack[STACK];
static int depth = 0, current = PART_OUT, frame_open = 0;
static unsigned int last = 0, frame_stamps = 0;
static unsigned int frame_us[PARTS];

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

/* the part charged from now on: the time since the last read goes to the one
that was open */
static void charge_to(int part)
{
	unsigned int now;

	if (part == current)
		return;
	now = sceKernelGetSystemTimeLow();
	if (current != PART_OUT)
		frame_us[current] += now - last;
	last = now;
	current = part;
	frame_stamps++;
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
	stack[depth++] = (unsigned char)current;
	if (phase == _halo_perf_render)
		frame_open = 1;
	charge_to(part);
}

void halo_perf_end(int phase)
{
	if (phase < 0 || phase >= NUMBER_OF_HALO_PERF_PHASES || phase_part[phase] < 0)
		return;
	if (!phase_depth[phase] || --phase_depth[phase])
		return;
	if (!depth)
	{
		run->unbalanced++;
		return;
	}
	charge_to(stack[--depth]);
}

/* the render's parts follow each other: the one running is closed as the
next opens; NONE (or a phase with no part) is the render's own time */
void halo_perf_render_part(int phase)
{
	if (!depth)
		return;
	charge_to(phase >= 0 && phase < NUMBER_OF_HALO_PERF_PHASES && phase_part[phase] > PART_UNNAMED ? phase_part[phase] : PART_UNNAMED);
}

/* (no unit of this build asks: NONE) */
int halo_perf_render_part_now(void)
{
	return -1;
}

/* the main loop's pass ends (main.c; called whether the word is on or not):
every phase should be closed */
void halo_perf_frame(void)
{
	if (depth && run)
	{
		run->unbalanced++;
		depth = 0;
		current = PART_OUT;
	}
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

static void report(void)
{
	char line[500];
	int at, part;
	double n = run->frames ? (double)run->frames : 1.0;

	at = snprintf(line, sizeof(line), "drawsplit: %u frames (run %u): d %.1f/%.1f/%.1f; parts %.1f",
		run->frames, run->run_frames, run->window_us[HIST_D] / n / 1000.0,
		percentile(run->histogram[HIST_D], run->run_frames, 500), percentile(run->histogram[HIST_D], run->run_frames, 990),
		run->window_us[HIST_SUM] / n / 1000.0);
	/* (snprintf returns the length it wanted: at is held inside the line) */
	for (part = PART_UNNAMED; part < PARTS && at < (int)sizeof(line) - 1; part++)
	{
		at += snprintf(line + at, sizeof(line) - at, "; %s %.1f/%.1f/%.1f", part_names[part], run->window_us[part] / n / 1000.0,
			percentile(run->histogram[part], run->run_frames, 500), percentile(run->histogram[part], run->run_frames, 990));
		if (at > (int)sizeof(line) - 1)
			at = (int)sizeof(line) - 1;
	}
	if (at < (int)sizeof(line) - 1)
		snprintf(line + at, sizeof(line) - at, "; stamps %.1f a frame (most %u)%s", run->window_stamps / n, run->stamps_most,
			run->unbalanced ? "; UNBALANCED" : "");
	platform_log("%s", line);
	if (run->unbalanced || run->unfolded)
		platform_log("drawsplit: %u hooks unbalanced (a close with nothing open, or past %d deep); %u frames drawn outside a render phase, not split",
			run->unbalanced, STACK, run->unfolded);
	for (part = 0; part < HISTS; part++)
		run->window_us[part] = 0;
	run->window_stamps = 0;
	run->stamps_most = 0;
	run->frames = 0;
}

/* a frame has been drawn, from psp_frame_account (psp_platform.c), with
frametrace's d: the parts are closed, folded and cleared */
void psp_drawsplit_frame(unsigned long draw_us)
{
	unsigned int sum = 0;
	int part;

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
			platform_log("drawsplit: on (the drawing's %d parts, the engine's perf phases in its five files; a line each %d frames)",
				PARTS - 1, WINDOW);
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
	/* close the part open now; what follows until the render's end is no
	part's */
	{
		unsigned int now = sceKernelGetSystemTimeLow();

		if (current != PART_OUT)
			frame_us[current] += now - last;
		last = now;
		current = PART_OUT;
		frame_stamps++;
	}
	for (part = PART_UNNAMED; part < PARTS; part++)
	{
		run->histogram[part][bin(frame_us[part])]++;
		run->window_us[part] += frame_us[part];
		sum += frame_us[part];
		frame_us[part] = 0;
	}
	run->histogram[HIST_SUM][bin(sum)]++;
	run->window_us[HIST_SUM] += sum;
	run->histogram[HIST_D][bin((unsigned int)draw_us)]++;
	run->window_us[HIST_D] += draw_us;
	run->window_stamps += frame_stamps;
	if (frame_stamps > run->stamps_most)
		run->stamps_most = frame_stamps;
	frame_stamps = 0;
	frame_open = 0;
	run->run_frames++;
	if (++run->frames >= WINDOW)
		report();
}

#endif
