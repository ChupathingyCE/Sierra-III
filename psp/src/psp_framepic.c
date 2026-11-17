/* The frame picture (docs/frame_picture.md; keep word "framepic", build
switch PSP_FRAMEPIC=1): each box of the tick and the frame in counts of the
main CPU's profiler (cycles, memory stall, asleep, misses by kind), as
histograms per tick and per frame, so p50 and p99 come with the means.

The boxes are the engine's own perf phases (halo_perf_log.h), compiled in
under PSP_FRAMEPIC: the tick's top-level regions (units, AI, effects, the
game engine, objects), the netcode, the frame's render and its parts, sound,
the network, present and the waits. Each is entered once a tick or a frame.
The phases entered once a call or an object (collision, physics, node
matrices, an object type's update, damage, functions, lights, activation,
garbage) are left unmarked: v1's bound of ~60 marks a tick, and their cost
stays inside the box that calls them.

Each mark is one system call into halo_profile.prx (halo_profile_box),
which charges the counts since the last mark to the box that was open. The
block those land in is the game's (partition 2, outside the heap), taken
whenever this build runs the profiler, with framepic or not, so both sides
of the A/B have it. Without the module (the emulator) the marks are only
counted, which is the emulator's check of marks a tick */

#ifdef PSP_FRAMEPIC

#include <pspkernel.h>
#include <stdio.h>
#include <string.h>
#include "halo_perf_log.h"
#include "../profile/framepic.h"

int psp_keep(const char *name);
void platform_log(const char *format, ...);
int halo_profile_box(int box);
int halo_profile_totals(unsigned long long *out); /* halo_profile.prx: every counter's total at this instant */
#define PROFILE_COUNTERS 19

int halo_perf_log_on = 0;
unsigned long halo_perf_counters[NUMBER_OF_HALO_PERF_COUNTERS];

enum framepic_box
{
	BOX_OUTSIDE, /* no box open: the module's charge for 0 */
	BOX_TICK, /* the tick less the boxes inside it */
	BOX_UNITS,
	BOX_AI,
	BOX_EFFECTS,
	BOX_GAME_ENGINE,
	BOX_OBJECTS,
	BOX_NETCODE,
	BOX_RENDER, /* the render less its parts */
	BOX_RENDER_WORLD,
	BOX_RENDER_OBJECTS,
	BOX_RENDER_TRANSPARENT,
	BOX_RENDER_EFFECTS,
	BOX_RENDER_HUD,
	BOX_RENDER_OTHER, /* begin, setup, lens flares, the screen effect, end */
	BOX_SOUND,
	BOX_NETWORK,
	BOX_PRESENT,
	BOX_VSYNC_WAIT,
	BOX_RENDER_WAIT, /* the occlusion and texture waits, inside a render part */
	BOX_NULL, /* a mark pair with nothing between, once a tick: one mark's own cost */
	BOXES
};

static const char *const box_names[BOXES] =
{
	"outside any box", "tick (other)", "units", "ai", "effects", "game engine", "objects", "netcode",
	"render (other)", "render: world", "render: objects", "render: transparent", "render: effects",
	"render: hud", "render: other parts", "sound", "network", "present", "vsync wait", "render waits",
	"the marks (null pair)",
};

/* folded once a tick (a tick may run 0 to several times a frame); the rest
once a frame */
#define TICK_BOX(box) ((box) >= BOX_TICK && (box) <= BOX_OBJECTS)

static signed char phase_box[NUMBER_OF_HALO_PERF_PHASES];
static unsigned char phase_depth[NUMBER_OF_HALO_PERF_PHASES];
static int render_part = -1;

/* the histograms: 32 buckets from 256 cycles, two a doubling (to ~50 ms) */
#define BUCKETS 32
#define HISTOGRAM_MEASURES (FRAMEPIC_MEASURES + 1) /* + cycles in switch-free samples */
struct framepic_game
{
	struct framepic_block block;
	unsigned int histogram[BOXES][HISTOGRAM_MEASURES][BUCKETS];
	unsigned int samples[BOXES], clean_samples[BOXES];
	unsigned long long marks_by_box[BOXES];
};
static struct framepic_game *game = NULL;
static int module_on = 0, set_up = 0;
/* the emulator's check: marks a frame and a tick */
static unsigned int marks_frame = 0, marks_frame_most = 0, ticks_frame = 0;
static unsigned long long marks_total = 0, frames_total = 0, ticks_total = 0;
static unsigned int beats = 0;
/* the identity check (docs/frame_picture.md): the module's totals and the
boxes' sums when the marks turn on; the report compares both deltas */
static unsigned long long identity_module[PROFILE_COUNTERS];
static int identity_on = 0;
static const unsigned char identity_registers[FRAMEPIC_MEASURES] = FRAMEPIC_REGISTERS;
static const char *const identity_names[FRAMEPIC_MEASURES] = {
	"cycles", "memory stall", "asleep", "I-misses", "D-misses", "write-backs", "uncached loads", "uncached stores" };

static unsigned long long boxes_sum(const struct framepic_block *block, int measure)
{
	unsigned long long total = block->self[measure];
	int box;

	for (box = 0; box < FRAMEPIC_BOXES; box++)
		total += block->sum[box][measure];
	return total;
}

static void identity_report(void)
{
	unsigned long long now[PROFILE_COUNTERS];
	int m;

	if (!identity_on || halo_profile_totals(now) < 0)
	{
		platform_log("framepic: identity: not read (%s)", identity_on ? "the module's totals refused" : "no start reading");
		return;
	}
	for (m = 0; m < FRAMEPIC_MEASURES; m++)
	{
		unsigned long long module = now[identity_registers[m]] - identity_module[identity_registers[m]];
		unsigned long long boxes = boxes_sum(&game->block, m);
		double diff = module ? 100.0 * ((double)boxes - (double)module) / (double)module : 0.0;

		platform_log("framepic: identity: %s module %llu boxes %llu (%+.3f%%)%s", identity_names[m], module, boxes, diff,
			diff > 0.05 || diff < -0.05 ? " OVER 0.05%" : "");
	}
}

static void map_phases(void)
{
	int k;

	for (k = 0; k < NUMBER_OF_HALO_PERF_PHASES; k++)
		phase_box[k] = -1;
	phase_box[_halo_perf_tick] = BOX_TICK;
	phase_box[_halo_perf_tick_units] = BOX_UNITS;
	phase_box[_halo_perf_tick_ai] = BOX_AI;
	phase_box[_halo_perf_tick_effects] = BOX_EFFECTS;
	phase_box[_halo_perf_tick_game_engine] = BOX_GAME_ENGINE;
	phase_box[_halo_perf_tick_objects] = BOX_OBJECTS;
	phase_box[_halo_perf_tick_netcode] = BOX_NETCODE;
	phase_box[_halo_perf_render] = BOX_RENDER;
	phase_box[_halo_perf_render_world] = BOX_RENDER_WORLD;
	phase_box[_halo_perf_render_objects] = BOX_RENDER_OBJECTS;
	phase_box[_halo_perf_render_transparent] = BOX_RENDER_TRANSPARENT;
	phase_box[_halo_perf_render_effects] = BOX_RENDER_EFFECTS;
	phase_box[_halo_perf_render_hud] = BOX_RENDER_HUD;
	/* the render's small parts (begin, setup, lens flares, the screen effect,
	end) map to no box, so their time is the render's other work: as boxes of
	their own they took ~10 of the ~70 marks a frame (framepic1, v1's bound
	of ~60). So does the network, which the campaign never runs. Both stay
	named, for a build that wants them back (framepicall) */
	if (psp_keep("framepicall"))
	{
		phase_box[_halo_perf_render_begin] = BOX_RENDER_OTHER;
		phase_box[_halo_perf_render_setup] = BOX_RENDER_OTHER;
		phase_box[_halo_perf_render_lens_flares] = BOX_RENDER_OTHER;
		phase_box[_halo_perf_render_screen_effect] = BOX_RENDER_OTHER;
		phase_box[_halo_perf_render_end] = BOX_RENDER_OTHER;
		phase_box[_halo_perf_network] = BOX_NETWORK;
	}
	/* sound is entered in the tick (game_sound_update) and the render
	(sound_render, inside its own sound phase): both are this box */
	phase_box[_halo_perf_sound] = BOX_SOUND;
	phase_box[_halo_perf_present] = BOX_PRESENT;
	phase_box[_halo_perf_vsync_wait] = BOX_VSYNC_WAIT;
	phase_box[_halo_perf_occlusion_wait] = BOX_RENDER_WAIT;
	phase_box[_halo_perf_texture_wait] = BOX_RENDER_WAIT;
}

static void mark(int box)
{
	marks_frame++;
	marks_total++;
	if (box >= 0)
		game->marks_by_box[box]++;
	if (module_on)
		halo_profile_box(box);
}

static unsigned int bucket(unsigned int value)
{
	unsigned int bit, index;

	if (value < 256)
		return 0;
	bit = 31 - (unsigned int)__builtin_clz(value);
	index = 2 * (bit - 8) + ((value >> (bit - 1)) & 1) + 1;
	return index < BUCKETS ? index : BUCKETS - 1;
}

static void fold(int ticks)
{
	int box, m;

	if (!module_on)
		return;
	for (box = 0; box < BOXES; box++)
	{
		struct framepic_block *block = &game->block;

		if (TICK_BOX(box) != ticks || !block->marks[box])
			continue;
		if (!block->switched[box])
		{
			game->histogram[box][FRAMEPIC_MEASURES][bucket(block->acc[box][FRAMEPIC_CYCLES])]++;
			game->clean_samples[box]++;
		}
		for (m = 0; m < FRAMEPIC_MEASURES; m++)
		{
			game->histogram[box][m][bucket(block->acc[box][m])]++;
			block->acc[box][m] = 0;
		}
		game->samples[box]++;
		block->switched[box] = 0;
		block->marks[box] = 0;
	}
}

void halo_perf_begin(int phase)
{
	int box;

	if (phase < 0 || phase >= NUMBER_OF_HALO_PERF_PHASES || (box = phase_box[phase]) < 0)
		return;
	if (phase_depth[phase]++)
		return;
	mark(box);
}

void halo_perf_end(int phase)
{
	int box;

	if (phase < 0 || phase >= NUMBER_OF_HALO_PERF_PHASES || (box = phase_box[phase]) < 0)
		return;
	if (!phase_depth[phase] || --phase_depth[phase])
		return;
	/* a render part still open when the render ends */
	if (phase == _halo_perf_render && render_part >= 0)
	{
		mark(FRAMEPIC_POP);
		render_part = -1;
	}
	mark(FRAMEPIC_POP);
	if (phase == _halo_perf_tick)
	{
		/* one mark's own cost, then the tick's boxes into their histograms */
		mark(BOX_NULL);
		mark(FRAMEPIC_POP);
		fold(1);
		ticks_frame++;
		ticks_total++;
	}
}

/* the render's parts follow each other: the one running is closed as the
next opens */
void halo_perf_render_part(int phase)
{
	if (render_part >= 0)
	{
		mark(FRAMEPIC_POP);
		render_part = -1;
	}
	if (phase >= 0 && phase < NUMBER_OF_HALO_PERF_PHASES && phase_box[phase] >= 0)
	{
		render_part = phase;
		mark(phase_box[phase]);
	}
}

int halo_perf_render_part_now(void)
{
	return render_part;
}

void halo_perf_frame(void)
{
	fold(0);
	if (marks_frame > marks_frame_most)
		marks_frame_most = marks_frame;
	marks_frame = 0;
	ticks_frame = 0;
	frames_total++;
}

/* the block for halo_profile.prx: taken from partition 2 outside the heap
whenever the profiler runs in this build, so both sides of the A/B have it */
unsigned int psp_framepic_block(void)
{
	SceUID block;

	if (game)
		return (unsigned int)&game->block;
	block = sceKernelAllocPartitionMemory(2, "halo framepic", PSP_SMEM_High, sizeof(struct framepic_game) + 64, NULL);
	if (block < 0)
	{
		platform_log("framepic: no room for its block (%u KB): %08x", (unsigned int)(sizeof(struct framepic_game) / 1024),
			(unsigned int)block);
		return 0;
	}
	game = (struct framepic_game *)(((unsigned int)sceKernelGetBlockHeadAddr(block) + 63) & ~63u);
	memset(game, 0, sizeof(*game));
	game->block.magic = FRAMEPIC_MAGIC;
	platform_log("framepic: its block, %u KB from partition 2 at %08x", (unsigned int)(sizeof(struct framepic_game) / 1024),
		(unsigned int)game);
	return (unsigned int)&game->block;
}

/* the cycles, as microseconds at the clock's 333 MHz */
#define US(cycles) ((double)(cycles) / 333.0)

static unsigned int percentile(const unsigned int *histogram, unsigned int samples, unsigned int per_mille)
{
	unsigned int want = (unsigned int)(((unsigned long long)samples * per_mille + 999) / 1000), seen = 0, k;

	for (k = 0; k < BUCKETS; k++)
	{
		seen += histogram[k];
		if (seen >= want && want)
			/* the bucket's upper edge, in cycles (bucket k > 0 starts at
			2^b, or 1.5 x 2^b for its second half, b = 8 + (k - 1) / 2) */
			return k ? (256u << ((k - 1) / 2)) + (((k - 1) & 1) + 1) * (128u << ((k - 1) / 2)) : 256;
	}
	return 0;
}

static void report(const char *why)
{
	int box;

	platform_log("framepic: %s: %llu marks over %llu frames and %llu ticks (%.1f a frame, %.1f a tick; at most %u in a frame); %s",
		why, marks_total, frames_total, ticks_total, frames_total ? (double)marks_total / frames_total : 0.0,
		ticks_total ? (double)marks_total / ticks_total : 0.0, marks_frame_most,
		module_on ? "the counts below" : "counted only (no profiler module)");
	if (!module_on)
	{
		/* which boxes the marks come from (pops count to the box they close
		as marks_by_box only counts pushes: a push and its pop make 2) */
		char line[256];
		int at = 0;

		for (box = 0; box < BOXES && at < (int)sizeof(line) - 40; box++)
			if (game->marks_by_box[box])
				at += snprintf(line + at, sizeof(line) - at, "%s%s %.1f", at ? ", " : "", box_names[box],
					2.0 * (double)game->marks_by_box[box] / (frames_total ? (double)frames_total : 1.0));
		platform_log("framepic: marks a frame by box (push and pop): %s", line);
		return;
	}
	if (game->block.unbalanced)
		platform_log("framepic: %u marks unbalanced (a pop with nothing open, or past %d deep)", game->block.unbalanced, FRAMEPIC_STACK);
	for (box = 0; box < BOXES; box++)
	{
		const struct framepic_block *block = &game->block;
		unsigned int samples = game->samples[box];
		double n = samples ? (double)samples : 1.0;

		if (!samples && !game->marks_by_box[box])
			continue;
		platform_log("framepic: %s: %u samples (%s), %llu marks; cycles p50 %.0f p99 %.0f us (switch-free %u: p50 %.0f p99 %.0f), mean %.1f us; "
			"memory stall %.1f, asleep %.1f us a sample; I-misses %.0f, D-misses %.0f, write-backs %.0f, uncached loads %.0f, stores %.0f a sample",
			box_names[box], samples, TICK_BOX(box) ? "ticks" : "frames", game->marks_by_box[box],
			US(percentile(game->histogram[box][FRAMEPIC_CYCLES], samples, 500)),
			US(percentile(game->histogram[box][FRAMEPIC_CYCLES], samples, 990)),
			game->clean_samples[box],
			US(percentile(game->histogram[box][FRAMEPIC_MEASURES], game->clean_samples[box], 500)),
			US(percentile(game->histogram[box][FRAMEPIC_MEASURES], game->clean_samples[box], 990)),
			US(block->sum[box][FRAMEPIC_CYCLES]) / n, US(block->sum[box][FRAMEPIC_MEMORY]) / n,
			US(block->sum[box][FRAMEPIC_ASLEEP]) / n, block->sum[box][FRAMEPIC_IMISS] / n,
			block->sum[box][FRAMEPIC_DMISS] / n, block->sum[box][FRAMEPIC_WRITEBACK] / n,
			block->sum[box][FRAMEPIC_UNCACHED_LOAD] / n, block->sum[box][FRAMEPIC_UNCACHED_STORE] / n);
	}
	identity_report();
}

/* each heartbeat (psp_platform.c): the first one turns the marks on (the
keep words are read once, here), the table every sixth (~30 s) */
void psp_framepic_beat(int profiler_running)
{
	if (!set_up)
	{
		set_up = 1;
		if (!psp_keep("framepic"))
			return;
		if (!psp_framepic_block())
			return;
		map_phases();
		module_on = profiler_running;
		game->block.want_switch = !psp_keep("framepicnosw");
		/* before the first mark, so every box's sum starts from 0 */
		identity_on = module_on && halo_profile_totals(identity_module) == 0;
		halo_perf_log_on = 1;
		platform_log("framepic: on (%d boxes, the engine's perf phases; %s; switch count at each mark %s)", BOXES,
			module_on ? "halo_profile.prx charges each" : "no profiler module: marks counted only", game->block.want_switch ? "on" : "off (framepicnosw)");
		return;
	}
	if (halo_perf_log_on && ++beats % 6 == 0)
		report("so far");
}

/* the stretch's end (exitat and the other exits from the game's own thread) */
void psp_framepic_exit(void)
{
	if (halo_perf_log_on)
		report("at the exit");
}

#endif
