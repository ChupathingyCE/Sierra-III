/* psp_loading.c: the load between the menu and a level, and between
levels, as the loading screen sees it (docs/plan_loading_screen.md).

A load on the PSP is one blocking chain on the main thread: the tag data in
one read, then the BSP, the stencils and the texture pack's table. Nothing
is drawn meanwhile, so the screen holds the menu's faded last frame. This
unit keeps the load's progress by bytes (psp_fixups.py FIXES_LOADING splits
the tag read into pieces and reports each one), times the load's parts for
the log, plays the Xbox's chant over it, and hands the GE layer a view to
draw at most once a frame. After the load, the settle draws the level's
first view with no tick until its textures are in. The drawing itself is
the GE layer's (psp_ge_loading_*, linked weakly): without it the screen
holds as before, with the chant, the settle and the timings.
Keep words: "loadingoff" (none of it), "chantoff", "chantheap" (the chant's voices
from the heap: psp_dsound.c), "settleoff", "settleN". */
#include <math.h>
#include <string.h>
#include "psp_loading.h"

extern unsigned long psp_microseconds(void);
extern void platform_log(const char *format, ...);
extern int psp_keep(const char *name);
extern int psp_keep_number(const char *prefix, int fallback);
extern unsigned long psp_tex_load_count(void);

static struct
{
	int active;
	int screen;               /* drawing asked for (not "loadingoff") */
	char map[64];
	unsigned long begin_us;
	unsigned long last_draw_us;
	unsigned long expected;   /* bytes the load says it will read */
	unsigned long done;
	unsigned long draws;
	float shown;              /* the progress as drawn, eased toward the true one */
	unsigned long settle_us;  /* the settle's start */
	unsigned long settle_cap_us;
	unsigned long settle_loads; /* the texture cache's count at the last settle frame */
	unsigned long settle_loads_first; /* and at the settle's start */
	int settle_frames;
	int settle_quiet;         /* settle frames in a row that read no texture */
} loading;

/* the frame shown when the load began, kept where the GE layer chooses
(the shown buffer may simply stay in VRAM): no static copy here, since a
480 x 272 frame would cost the heap 255 KB in every level */
static const void *captured;

static float ease(float shown, float target, float seconds)
{
	/* toward the true progress at a rate set in time, not in frames: the
	Xbox moved a fifth of the way each frame at 30 fps */
	float step = seconds * 6.0f;

	if (step > 1.0f)
		step = 1.0f;
	return shown + (target - shown) * step;
}

static float brightness_of(float progress)
{
	/* the Xbox's sin(progress * pi)^0.9, without libm's pow on the
	hot side of a load: x^0.9 ~= x * (1.1 - 0.1x) on 0..1 is within
	0.012 of it, finer than the 16-bit frame can show */
	float x = progress;
	float s;

	if (x <= 0.0f || x >= 1.0f)
		return 0.0f;
	/* sin(pi x) by its 4th-order Bhaskara form: within 0.002 */
	s = 16.0f * x * (1.0f - x) / (5.0f - 4.0f * x * (1.0f - x));
	return s * (1.1f - 0.1f * s);
}

/* The chant: the Xbox's four looping copies of one tone at four pitches
(progress_bar.c's SetPitch -4681, -6386, -3999, -8773, in 1/4096 octave from
48 kHz), each swelling over its own part of the load. The bytes are the
engine's own (progress_bar.c compressed_monk_tone, Xbox ADPCM at 22,050 Hz). */
#define CHANT_VOICES 4
extern unsigned char compressed_monk_tone[18396];
extern void *psp_dsound_loop_start(const void *adpcm, unsigned long bytes, unsigned long sample_rate,
	unsigned long frequency, long millibels);
extern void psp_dsound_loop_volume(void *voice, long millibels);
extern void psp_dsound_loop_stop(void *voice);

static const unsigned long chant_frequency[CHANT_VOICES] = { 21738, 16290, 24397, 10876 };
static const float chant_range[CHANT_VOICES][2] = { { 0.0f, 1.0f }, { 0.4f, 1.0f }, { 0.5f, 1.0f }, { 0.55f, 1.0f } };
static const float chant_swell[CHANT_VOICES] = { 3500.0f, 4500.0f, 3500.0f, 4500.0f };
static void *chant[CHANT_VOICES];

static void chant_start(void)
{
	int k;

	if (psp_keep("chantoff"))
		return;
	for (k = 0; k < CHANT_VOICES; k++)
		chant[k] = psp_dsound_loop_start(compressed_monk_tone, sizeof(compressed_monk_tone), 22050,
			chant_frequency[k], -5000);
}

static void chant_gains(float progress)
{
	int k;

	for (k = 0; k < CHANT_VOICES; k++)
	{
		float t = 0.0f;

		if (!chant[k])
			continue;
		if (progress >= chant_range[k][0] && progress <= chant_range[k][1])
			t = (progress - chant_range[k][0]) / (chant_range[k][1] - chant_range[k][0]);
		/* the Xbox's sin(t pi)^0.3 of each voice's swell over -50 dB */
		psp_dsound_loop_volume(chant[k], (long)(powf(sinf(t * 3.1415f), 0.3f) * chant_swell[k] - 5000.0f));
	}
}

static void chant_stop(void)
{
	int k;

	for (k = 0; k < CHANT_VOICES; k++)
	{
		psp_dsound_loop_stop(chant[k]);
		chant[k] = NULL;
	}
}

/* the progress as the screen and the chant show it: while loading at most
once a frame's time; while settling once each of the game's frames */
static void update(int phase)
{
	struct psp_loading_view view;
	unsigned long now = psp_microseconds();
	float target;

	if (!loading.screen)
		return;
	if (phase == 0 && loading.draws && now - loading.last_draw_us < 33000)
		return;
	if (phase)
		target = 1.0f;
	else
		target = loading.expected ? (float)loading.done / (float)loading.expected : 0.0f;
	if (target > 1.0f)
		target = 1.0f;
	loading.shown = ease(loading.shown, target, loading.draws ? (now - loading.last_draw_us) / 1e6f : 0.0f);
	loading.last_draw_us = now;
	loading.draws++;
	chant_gains(loading.shown);
	view.progress = loading.shown;
	view.brightness = brightness_of(loading.shown);
	view.phase = phase;
	view.captured = captured;
	if (phase == 0 && psp_ge_loading_frame)
		psp_ge_loading_frame(&view);
	if (phase == 1 && psp_ge_loading_veil)
		psp_ge_loading_veil(&view);
}

void psp_loading_begin(const char *map_name)
{
	memset(&loading, 0, sizeof(loading));
	loading.active = 1;
	loading.screen = !psp_keep("loadingoff");
	loading.begin_us = psp_microseconds();
	strncpy(loading.map, map_name ? map_name : "?", sizeof(loading.map) - 1);
	{
		extern void psp_ge_stencils_expect(const char *map_name);

		psp_ge_stencils_expect(map_name);
	}
	captured = loading.screen && psp_ge_loading_capture ? psp_ge_loading_capture() : NULL;
	if (loading.screen)
		chant_start();
	platform_log("loading: %s begins%s", loading.map,
		!loading.screen ? " (loadingoff: no screen)" : psp_ge_loading_frame ? "" : " (no screen in this build)");
}

void psp_loading_expect(unsigned long bytes)
{
	if (loading.active)
		loading.expected += bytes;
}

void psp_loading_progress(unsigned long bytes)
{
	if (!loading.active)
		return;
	loading.done += bytes;
	update(0);
}

int psp_loading_active(void)
{
	return loading.active;
}

/* The settle: once the level is loaded, its first view is drawn with no tick
until the texture cache stops filling (three frames in a row with nothing
read) or the cap passes, under the GE layer's veil. The textures the opening
needs are then read while the game stands still, rather than while it
plays with surfaces missing and frames slow. main.c's psp_main_settle
draws the frames (psp_fixups.py FIXES_LOADING); the engine's own frame-lock
reset after a load (main_start_time) drops the time they took.
Keep words: "settleoff" (none), "settleN" (cap N seconds, 3 by default). */
int psp_loading_settle_begin(void)
{
	int cap;

	if (!loading.active || psp_keep("settleoff"))
		return 0;
	cap = psp_keep_number("settle", 3);
	if (cap <= 0)
		return 0;
	loading.settle_cap_us = (unsigned long)cap * 1000000ul;
	loading.settle_us = psp_microseconds();
	loading.settle_loads = loading.settle_loads_first = psp_tex_load_count();
	loading.settle_frames = 0;
	loading.settle_quiet = 0;
	return 1;
}

int psp_loading_settle_frame(void)
{
	unsigned long count = psp_tex_load_count();

	loading.settle_frames++;
	loading.settle_quiet = count == loading.settle_loads ? loading.settle_quiet + 1 : 0;
	loading.settle_loads = count;
	/* warm, and the screen's progress (and the chant) played out to its end */
	if ((loading.settle_frames >= 4 && loading.settle_quiet >= 3 && (!loading.screen || loading.shown >= 0.99f)) ||
		psp_microseconds() - loading.settle_us >= loading.settle_cap_us)
	{
		platform_log("loading: %s settled in %lu ms, %d frames, %lu texture reads and waits%s", loading.map,
			(psp_microseconds() - loading.settle_us) / 1000, loading.settle_frames, count - loading.settle_loads_first,
			loading.settle_quiet >= 3 ? "" : " (at the cap, still reading)");
		return 0;
	}
	return 1;
}

void psp_loading_settle_veil(void)
{
	update(1);
}

void psp_loading_end(void)
{
	if (!loading.active)
		return;
	platform_log("loading: %s took %lu ms (%lu of %lu bytes counted, %lu frames drawn)", loading.map,
		(psp_microseconds() - loading.begin_us) / 1000, loading.done, loading.expected, loading.draws);
	loading.active = 0;
	chant_stop();
	captured = NULL;
	{
		extern void psp_ge_stencils_retry(void);

		psp_ge_stencils_retry();
	}
}
