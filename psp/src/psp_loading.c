/* psp_loading.c: the load between the menu and a level, and between
levels, as the loading screen sees it (docs/plan_loading_screen.md).

A load on the PSP is one blocking chain on the main thread: the tag data in
one read, then the BSP, the stencils and the texture pack's table. Nothing
is drawn meanwhile, so the screen holds the menu's faded last frame. This
unit keeps the load's progress by bytes (psp_fixups.py FIXES_LOADING splits
the tag read into pieces and reports each one), times the load's parts for
the log, and hands the GE layer a view to draw at most once a frame. The
drawing itself is the GE layer's (psp_ge_loading_*, linked weakly): without
it the load behaves as before, with its timings logged. */
#include <string.h>
#include "psp_loading.h"

extern unsigned long psp_microseconds(void);
extern void platform_log(const char *format, ...);
extern int psp_keep(const char *name);

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

static void draw(int phase)
{
	struct psp_loading_view view;
	unsigned long now = psp_microseconds();
	float target;

	if (!loading.screen || !psp_ge_loading_frame)
		return;
	if (loading.draws && now - loading.last_draw_us < 33000)
		return;
	target = loading.expected ? (float)loading.done / (float)loading.expected : 0.0f;
	if (target > 1.0f)
		target = 1.0f;
	loading.shown = ease(loading.shown, target, loading.draws ? (now - loading.last_draw_us) / 1e6f : 0.0f);
	loading.last_draw_us = now;
	loading.draws++;
	view.progress = loading.shown;
	view.brightness = brightness_of(loading.shown);
	view.phase = phase;
	view.captured = captured;
	psp_ge_loading_frame(&view);
}

void psp_loading_begin(const char *map_name)
{
	memset(&loading, 0, sizeof(loading));
	loading.active = 1;
	loading.screen = !psp_keep("loadingoff");
	loading.begin_us = psp_microseconds();
	strncpy(loading.map, map_name ? map_name : "?", sizeof(loading.map) - 1);
	captured = loading.screen && psp_ge_loading_capture ? psp_ge_loading_capture() : NULL;
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
	draw(0);
}

int psp_loading_active(void)
{
	return loading.active;
}

void psp_loading_end(void)
{
	if (!loading.active)
		return;
	platform_log("loading: %s took %lu ms (%lu of %lu bytes counted, %lu frames drawn)", loading.map,
		(psp_microseconds() - loading.begin_us) / 1000, loading.done, loading.expected, loading.draws);
	loading.active = 0;
	captured = NULL;
}
