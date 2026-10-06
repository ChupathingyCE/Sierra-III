/*
PSP_GE.C

The first slice of the PSP's own renderer: the level (the BSP) drawn by the
GE, the PSP's graphics chip, from the player's camera. Flat colors, one per
material, faded by distance fog, so the shapes read. No textures, objects,
sky or HUD yet; the game's own (null) Direct3D path still runs beside it.

How the geometry reaches the GE without copying:
- The game keeps the level's vertices in the map, 32 bytes each: position
  (3 floats), normal, binormal and tangent (packed, 4 bytes each), texture
  coordinate (2 floats). The GE reads vertices in a fixed component order,
  ending with the position, so it can't start a vertex at a position.
- Asked for a texture coordinate (8 bytes), a normal (12) and a position (12),
  the GE reads 32-byte vertices with the position at the end. Started 20
  bytes before the game's buffer, each GE vertex ends on the game vertex's
  position; the texture coordinate and normal it reads are the previous
  vertex's packed vectors, and with texturing and lighting off the GE never
  uses them.
- The triangles are the game's own list of the visible surfaces, built each
  frame (16-bit indices into the material's vertices).

Near the camera the GE needs help: it drops a whole triangle when a corner
falls behind the camera or lands too far off the screen (its coordinates
only reach 2048 pixels either side), and the ground under the player is
large triangles that do both. So every triangle is first checked on the CPU
against the camera: wholly outside the view, it is skipped; safely in front,
it goes to the GE as it is; otherwise the CPU cuts it to the part inside a
widened view (the near plane and four "guard band" planes) and the pieces
are drawn at the end of the frame, already in camera space.

Memory: the frame buffers and the depth buffer take the first 816 KB of
VRAM (16-bit color, 512 pixels a row); the on-screen log lives above them
(psp_platform.c) and the null renderer's targets never draw.
*/

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <pspge.h>

void platform_log(const char *format, ...);
#define PSP_CLAMP_255(x) ((x) < 0 ? 0 : (x) > 255 ? 255 : (x))

#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272
#define BUFFER_WIDTH 512
#define FRAME_BYTES (BUFFER_WIDTH * SCREEN_HEIGHT * 2)
/* 256 KB (build 33): on the PSP, firing at walls filled the 128 KB list
(decals pile up, shadows copy their receivers in), and the HUD, drawn last,
was what got cut */
/* 384 KB (build 40): sustained fire at a wall still filled 256 KB (decals
copy the wall's triangles under them, every spark is a sprite) */
#define DISPLAY_LIST_BYTES 0x60000
/* draws stop this close to the display list's end (each draw adds its
commands, and its copied indices and cut-up corners, to the list) */
#define DISPLAY_LIST_MARGIN 0x3000
/* and the 3D world stops this much earlier, so the HUD always has room */
#define HUD_RESERVE 0xC000
/* build 82: and the world's solid draws stop this much earlier still, so
the see-through draws (wall symbols, decals, the beam) always have room:
they are flushed last, and when the world had filled its share they were
the ones skipped (b79 run 3 at red base in the tank: 188 draws skipped,
the reticle's bracket missing a piece, the 2D items flickering as the
list's size crossed the limit from frame to frame) */
#define TRANSPARENT_RESERVE 0x8000
static unsigned long list_full_skips = 0, hud_reserve_low = 0;
static int list_limit = DISPLAY_LIST_BYTES - HUD_RESERVE - TRANSPARENT_RESERVE;
static void transparent_flush(void);
static void taa_resolve(void);
static int hud_phase = 0;

/* the HUD's turn: the reserve is its. The world's see-through draws are
flushed first, inside the world's share (build 34: in build 33 the flush
came after the switch and the effects of a firefight ate the HUD's room) */
static void list_hud_phase(void)
{
	if (hud_phase)
		return;
	transparent_flush();
	taa_resolve();
	hud_phase = 1;
	list_limit = DISPLAY_LIST_BYTES;
}

static int list_full(void)
{
	if (sceGuCheckList() < list_limit - DISPLAY_LIST_MARGIN)
		return 0;
	if (!list_full_skips++)
		platform_log("GE: the display list is full; draws skipped until the frame ends");
	return 1;
}

/* build 40: the first-person weapon's see-through parts (the assault
rifle's ammo display) are the nearest, so sorted far to near they came last,
and a full list skipped exactly them (Sheepdog01: the display went blank in
sustained fire). They may use a quarter of the HUD's reserve */
static int list_full_first_person(void)
{
	int saved = list_limit, full;

	if (list_limit < DISPLAY_LIST_BYTES - HUD_RESERVE * 3 / 4)
		list_limit = DISPLAY_LIST_BYTES - HUD_RESERVE * 3 / 4;
	full = list_full();
	list_limit = saved;
	return full;
}

/* room in the display list for a draw's own data, or NULL: a large draw
(a light over many triangles) can need more than the margin above, and
writing past the list's end corrupts memory (build 29) */
int psp_keep(const char *name);

/* build 48: the display list is written through the uncached view (pspgu's
sceGuStart sets bit 30 of its address), so every word the CPU puts there
is an uncached store over the bus, and sceGuGetMemory's space too: the
vertices and indices a draw copies there (cut triangles, kept indices,
decals, lights, shadows). A region of LIST_CACHED_MINIMUM bytes or more now
comes back as the cached view instead, on whole cache lines of its own (64
bytes of slack each side, so no line is shared with a command word written
uncached), each line made ours with "cache 0x18" (create dirty exclusive:
no read from memory; halo_bench v9 measured half the cost for whole lines).
The lines are written back and dropped from the cache ("cache 0x1B") before
the GE may read them: at the next stall address update, which every draw
makes (see __wrap_sceGeListUpdateStallAddr). Keep word "listuncached": as
before */
#define LIST_CACHED_MINIMUM 256
static unsigned int pending_start = 0, pending_end = 0;
static int list_cached = -1;
static unsigned long cached_takes = 0, cached_bytes = 0, cached_lines_flushed = 0;

static void list_flush_pending(void)
{
	unsigned int line;

	if (pending_end <= pending_start)
		return;
	for (line = pending_start; line < pending_end; line += 64)
		__builtin_allegrex_cache(0x1B, line);
	__asm__ volatile("sync");
	cached_lines_flushed += (pending_end - pending_start) / 64;
	pending_start = pending_end = 0;
}

static void *list_take(int bytes)
{
	void *taken;

	if (sceGuCheckList() + bytes + 2048 + 128 > list_limit)
	{
		if (!list_full_skips++)
			platform_log("GE: the display list is full (%d more bytes wanted); draws skipped until the frame ends", bytes);
		return NULL;
	}
	if (list_cached < 0)
		list_cached = !psp_keep("listuncached");
	if (!list_cached || bytes < LIST_CACHED_MINIMUM)
		return sceGuGetMemory(bytes);
	taken = sceGuGetMemory(bytes + 128);
	if (!taken)
		return NULL;
	{
		/* the cached view of the whole lines inside the region */
		unsigned int start = (((unsigned int)taken & ~0x40000000u) + 63) & ~63u, end = (start + bytes + 63) & ~63u, line;

		for (line = start; line < end; line += 64)
			__builtin_allegrex_cache(0x18, line);
		if (pending_end <= pending_start)
			pending_start = start;
		pending_end = end;
		cached_takes++;
		cached_bytes += bytes;
		return (void *)start;
	}
}

/* every pspgu draw (and sceGuGetMemory, sceGuFinish) moves the GE's stall
address with this system call, which stores the GE's register and waits
("sync") for the CPU's write buffer to drain (uofw kd/ge/stall.S). Build
48: the cached list regions are written back first, and only every
STALL_BATCH-th update is passed on (the GE then runs up to 16 draws behind
the CPU instead of one; it idles most of the frame anyway). sceGuFinish's
always goes through (stall_force, psp_ge_frame_end). Keep word "stalleach":
every update passed on, as before */
#define STALL_BATCH 16
int __real_sceGeListUpdateStallAddr(int qid, void *stall);
static int stall_batch = -1, stall_force = 0, stall_pending = 0;
static unsigned long stall_calls = 0, stall_forwarded = 0, stall_timed = 0, stall_timed_us = 0;

/* build 79: "geserial" (and the debug menu): no stall update reaches the GE
until the frame's list is finished, so the GE starts when the CPU is done
and the frame end's wait for it is the GE's whole busy time. A measurement,
not a way to play: the two no longer overlap */
static int ge_serial = -1;
int __wrap_sceGeListUpdateStallAddr(int qid, void *stall)
{
	stall_calls++;
	if (stall_batch < 0)
		stall_batch = !psp_keep("stalleach");
	if (ge_serial < 0)
		ge_serial = psp_keep("geserial");
	if (ge_serial && !stall_force)
		return 0;
	if (stall_batch && !stall_force && ++stall_pending < STALL_BATCH)
		return 0;
	stall_pending = 0;
	list_flush_pending();
	stall_forwarded++;
	if ((stall_forwarded & 63) == 0)
	{
		/* now and then, what the call itself costs */
		unsigned int t0 = sceKernelGetSystemTimeLow();
		int result = __real_sceGeListUpdateStallAddr(qid, stall);

		stall_timed_us += sceKernelGetSystemTimeLow() - t0;
		stall_timed++;
		return result;
	}
	return __real_sceGeListUpdateStallAddr(qid, stall);
}
#define SKY_COLOR 0xFFD8B080 /* ABGR: a pale blue */
#define FOG_NEAR 8.0f
#define FOG_FAR 160.0f
/* models are drawn unlit for now: their texture times this grey */
#define MODEL_LIGHT 0xFFB4B4B4

static unsigned int __attribute__((aligned(64))) display_list[DISPLAY_LIST_BYTES / 4];
static int ge_ready = 0, frame_open = 0;
/* the buffer being drawn (relative to VRAM), swapped each frame */
static void *draw_buffer = (void *)0;
static unsigned long frames = 0, draws_this_frame = 0, triangles_this_frame = 0;
static unsigned long clipped_this_frame = 0, culled_this_frame = 0, model_draws_this_frame = 0;

/* the camera, for the CPU's checks: position, and its right, up and forward
directions in the world */
static float camera_right[3], camera_up[3], camera_forward[3];
static float guard_x, guard_y;
/* build 36: the game's own near plane (0.0625 world units on Blood Gulch),
set each frame; it was a fixed 0.25, and the player stands about 0.2 from a
wall he walks into, so the wall was cut away and the view went through it */
static float near_clip = 0.25f;
/* build 38: the first-person weapon has its own near plane (the game's
0.0117 world units against the world's 0.0625, as the Xbox's
rasterizer_set_frustum_z) and the nearest slice of the depth buffer, so it
is never cut off by the near plane nor sinks into a wall in front of it */
#define WORLD_DEPTH_NEAR 61439
#define FIRST_PERSON_DEPTH_FAR 61440
static int model_first_person = 0, model_near = 0;
static unsigned long first_person_flushes = 0;
static float projection_fov = 1.2f, projection_near = 0.0625f, projection_far = 1000.0f;
static float first_person_near = 0.0117f, first_person_far = 1024.0f;
static unsigned long near_models = 0, near_triangles_dropped = 0;
/* the GE's front face for the Xbox's D3DCULL_CCW (build 35) */
#define LEVEL_FRONT GU_CW
#define NEAR_CLIP near_clip
/* how far off the screen a corner may land, in screen widths and heights:
6 keeps it within the GE's 2048 pixels */
#define GUARD 6.0f

/* build 37: CPU time in the renderer's entry points, by kind, for the log */
#define GE_ENTRIES 9
static unsigned long ge_entry_us[GE_ENTRIES];
/* build 47: inside the two biggest entries (level draws and model parts,
5 ms a frame each in b46), and decals per call. Each mark is a
sceKernelGetSystemTimeLow call; their own cost is measured once at the
first report (one timer reading) so the parts can be read net of it */
#define GE_PARTS 11
/* build 53: 0 all passes; 1 base only; 2 base + lightmap; 3 lightmap on white;
4 base + detail maps (psp_sdl.c cycles it with select + left under "passes") */
volatile int psp_pass_mode = 0;
int psp_keep(const char *name);
/* build 47, diagnostics for the dark lines on surfaces seen edge-on (Sheepdog01,
b46): "clearmagenta" clears to magenta and leaves out the sky, so a crack
between triangles shows magenta; "nomip" draws every texture at its full
size, so a line that is the smallest mip level's colour shows the texture */
static int magenta = -1;
static int tex_level_mode = GU_TEXTURE_AUTO;
static float tex_bias = 0.0f;
static int in_sky;
static unsigned long ge_part_us[GE_PARTS], ge_part_calls[GE_PARTS];
static const char *const ge_part_names[GE_PARTS] = {
	"level: stream + index range", "level: classify corners", "level: cut up", "level: passes (binds, lists)",
	"model: convert", "model: node matrix", "model: near-plane filter", "model: bind + state + lighting",
	"model: geometry", "model: two-sided + change/glow passes", "decal calls" };
static unsigned int part_mark;
static inline void part_start(void)
{
	part_mark = sceKernelGetSystemTimeLow();
}
static inline void part_add(int part)
{
	unsigned int now = sceKernelGetSystemTimeLow();

	ge_part_us[part] += now - part_mark;
	ge_part_calls[part]++;
	part_mark = now;
}
static const char *const ge_entry_names[GE_ENTRIES] = { "level draws", "model parts", "see-through models", "see-through level", "sprites", "decals", "lights", "shadows", "see-through drawing" };
static int list_peak = 0, clip_peak = 0;
static float view_far = 1000.0f;

/* the game's atmospheric fog (build 23): Halo fades linearly from its
minimum to its maximum distance, up to its density; the GE fades linearly
to all fog at its far distance, so the far distance is put where a full
fade would end and the two agree up to Halo's maximum (beyond it the GE
keeps thickening where Halo holds). Until the game says, no fog. */
static float fog_near = 1e6f, fog_far = 2e6f;
static unsigned int fog_color = SKY_COLOR;
static unsigned long fog_changes = 0;

void psp_ge_fog(const float *color, float minimum_distance, float maximum_distance, float density)
{
	float near = 1e6f, far = 2e6f;
	unsigned int c;
	int r, g, b;

	if (density > 0.001f && maximum_distance > minimum_distance)
	{
		if (density > 1.0f)
			density = 1.0f;
		near = minimum_distance;
		far = minimum_distance + (maximum_distance - minimum_distance) / density;
	}
	r = (int)(color[0] * 255.0f); g = (int)(color[1] * 255.0f); b = (int)(color[2] * 255.0f);
	r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
	c = 0xFF000000 | (b << 16) | (g << 8) | r;
	if (near != fog_near || far != fog_far || c != fog_color)
	{
		if (fog_changes++ < 8)
			platform_log("GE: fog %.1f to %.1f (density %.2f), colour %06x", minimum_distance, maximum_distance,
				density, c & 0xFFFFFF);
		fog_near = near;
		fog_far = far;
		fog_color = c;
	}
	if (frame_open)
		sceGuFog(fog_near, fog_far, fog_color);
}

/* the GE's own state, once: buffers, viewport, depth, fog */
static void ge_initialize(void)
{
	sceGuInit();
	sceGuStart(GU_DIRECT, display_list);
	sceGuDrawBuffer(GU_PSM_5650, (void *)0, BUFFER_WIDTH);
	sceGuDispBuffer(SCREEN_WIDTH, SCREEN_HEIGHT, (void *)FRAME_BYTES, BUFFER_WIDTH);
	sceGuDepthBuffer((void *)(2 * FRAME_BYTES), BUFFER_WIDTH);
	sceGuOffset(2048 - SCREEN_WIDTH / 2, 2048 - SCREEN_HEIGHT / 2);
	sceGuViewport(2048, 2048, SCREEN_WIDTH, SCREEN_HEIGHT);
	sceGuDepthRange(WORLD_DEPTH_NEAR, 0);
	sceGuScissor(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuDepthFunc(GU_GEQUAL);
	sceGuEnable(GU_DEPTH_TEST);
	/* the winding isn't settled yet: both sides */
	sceGuDisable(GU_CULL_FACE);
	sceGuDisable(GU_TEXTURE_2D);
	sceGuDisable(GU_LIGHTING);
	/* build 62: Gouraud, as the Xbox shades. Left flat from the first
	untextured builds (build 14), every lit model took one colour a
	triangle once the GE lit models (build 22 on): the facets on the
	weapons' slides and barrels Sheepdog01 kept seeing. "flatshade": as before */
	sceGuShadeModel(psp_keep("flatshade") ? GU_FLAT : GU_SMOOTH);
	sceGuEnable(GU_CLIP_PLANES);
	sceGuFog(fog_near, fog_far, fog_color);
	sceGuTexEnvColor(0xFFFFFFFF);
	/* build 50: "mipbias1" / "mipbias2" pick each texture's next smaller
	level half a level / a whole level sooner (blurrier far off, less
	shimmer). The GE's automatic choice, as PPSSPP models it
	(GPU/Software/Rasterizer.cpp CalculateSamplingParams), looks only at how
	s changes across the screen and how t changes down it, so a surface
	seen at a grazing angle gets a level too sharp for it: grass and dirt
	alternate from row to row at their border (Sheepdog01, b49) */
	tex_level_mode = psp_keep("nomip") ? GU_TEXTURE_CONST : GU_TEXTURE_AUTO;
	tex_bias = psp_keep("mipbias2") ? 1.0f : psp_keep("mipbias1") ? 0.5f : 0.0f;
	sceGuTexLevelMode(tex_level_mode, tex_bias);
	sceGuEnable(GU_FOG);
	stall_force = 1;
	sceGuFinish();
	sceGuSync(0, 0);
	stall_force = 0;
	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);
	ge_ready = 1;
	{
		/* from here the screen shows the GE's frames: the on-screen log's VRAM
		goes to textures */
		extern void psp_screen_log_stop(void);
		extern void psp_tex_screen_free(void);

		psp_screen_log_stop();
		psp_tex_screen_free();
	}
	platform_log("GE: drawing the level (480x272, 16-bit; flat colors and fog)");
}

/* a frame from the game's camera: Halo's world is z-up, and its camera gives
its position, forward and up directions and vertical field of view */
static float camera_eye[3];

/* a model matrix made relative to the camera */
static void relative(ScePspFMatrix4 *m)
{
	m->w.x -= camera_eye[0];
	m->w.y -= camera_eye[1];
	m->w.z -= camera_eye[2];
}

/* the model matrix for anything given in world coordinates (the level,
decals, shadows, lights): the world moved so the camera is at its origin */
static void world_model(void)
{
	ScePspFMatrix4 m;

	gumLoadIdentity(&m);
	relative(&m);
	sceGumLoadMatrix(&m);
}

/* build 64: "nearauto", the near plane moved out as far as what is near the
eye allows. near4 (four times finer depth everywhere; the rock bases stop
shimmering, Sheepdog01 b59) cut away walls within 0.25 of the eye (the bunker
wall in b63 run B, shots 33 and 35). Each frame:
- the level: the game's collision asks whether any structure surface lies
  within 1.4 x 4 x near of the eye, then within 1.4 x 2 x near (1.4: the
  near plane's corners lie farther from the eye than its middle);
- objects: the nearest bounding sphere of the models drawn last frame (the
  sky and the first-person weapon aside), one frame late.
The near plane is then 4, 2 or 1 times the game's. The cost is measured
(the log's "near plane" line) */
extern unsigned char collision_test_sphere(const float *center, float radius, long ignore_object_index);
static int near_auto = 0;
static int near_scale = -1; /* build 77: file scope, for the debug menu */
/* build 79's GE antialias flag ("geaa", GU_LINE_SMOOTH) is gone in build 80:
on the hardware it is for lines (PPSSPP's reading: the texture is looked up
by screen position and alpha halved), and with triangles it painted every
texture as blocks and checkerboards (Sheepdog01's b79 shots 000/002/014/015) */
/* build 79: texture coordinates re-centred per level draw (the ground
wobble). Blood Gulch's ground is one base texture across the whole terrain
with the detail maps tiled x100 and x60 over it, so the detail pass's
coordinates reach hundreds of repeats, and the cliffs' tens; if the GE
carries them at reduced precision, fractions of a texel are lost and the
pattern swims as the view moves (Sheepdog01, b64-b78: only the ground, worst on the
detail maps up close). Each draw's coordinates are shifted by whole repeats
to put the vertex nearest the eye near zero (sceGuTexOffset, applied after
the pass's scale, so the shift is an integer in that texture's own repeats:
exact for a repeating texture). "uvwide": off */
static int uv_centre = -1;
static float uv_centre_u = 0.0f, uv_centre_v = 0.0f, uv_shift_largest = 0.0f;
static unsigned long uv_centred_draws = 0;
/* the whole-repeat shift that puts (u, v) x scale near zero */
static void uv_shift(float scale_u, float scale_v)
{
	float su = -floorf(uv_centre_u * scale_u + 0.5f), sv = -floorf(uv_centre_v * scale_v + 0.5f);

	if (fabsf(su) > uv_shift_largest)
		uv_shift_largest = fabsf(su);
	if (fabsf(sv) > uv_shift_largest)
		uv_shift_largest = fabsf(sv);
	sceGuTexOffset(su, sv);
}
static float near_object_distance = 1e30f, near_object_distance_last = 1e30f;
static unsigned long near_auto_us = 0, near_auto_frames = 0, near_auto_chosen[3], near_auto_by_objects = 0;
static unsigned long near_auto_chosen_high[2]; /* build 84: x8, x16 */

/* build 84: candidates x16, x8, x4, x2, x1 (b64: x4, x2, x1). Each
doubling halves a depth step at every distance, so the stencils' nudges can
be half the lift for the same number of steps (Sheepdog01's b82-b83 runs: the
sweet spot between a stencil sinking, which is counted in steps, and
bleeding through what stands in front, which is counted in units, moved
with the distance; a finer step moves the two apart). The price is the
same as before: anything nearer the eye than the plane is cut open, so the
level is tested with a sphere at 1.4 x each candidate and the first that is
clear wins. The nearest model's sphere from the previous frame counts too,
but a sphere that CONTAINS the eye no longer does (the vehicle being driven:
b79's tank runs sat at x1 on most frames, "held down by an object", four
times coarser depth than on foot; a model that close is cut triangle by
triangle anyway, psp_ge_draw_model) */
static float near_plane_auto(const float *eye, float game_near)
{
	unsigned int start = sceKernelGetSystemTimeLow();
	int scale, level_scale = 1;

	near_object_distance_last = near_object_distance;
	near_object_distance = 1e30f;
	for (scale = 16; scale > 1; scale /= 2)
		if (!collision_test_sphere(eye, game_near * scale * 1.4f, -1))
		{
			level_scale = scale;
			break;
		}
	scale = level_scale;
	while (scale > 1 && near_object_distance_last < game_near * scale * 1.4f)
		scale /= 2;
	if (scale < level_scale)
		near_auto_by_objects++;
	near_auto_chosen[scale >= 4 ? 2 : scale == 2 ? 1 : 0]++;
	if (scale >= 8)
		near_auto_chosen_high[scale == 16 ? 1 : 0]++;
	near_auto_frames++;
	near_auto_us += sceKernelGetSystemTimeLow() - start;
	return game_near * scale;
}

/* ---------- build 77: temporal blend (cheap anti-aliasing)

Each frame's world (before the HUD) is blended with the last frame's world,
kept in a history buffer in RAM, and the projection is shifted by a quarter
pixel one way on even frames and the other way on odd ones: an edge's two
samples, half a pixel apart, average into a softer step. The GE does it (one
full-screen sprite pass and one copy, well under a millisecond of its time;
the CPU only queues them), and the GE had time to spare (b75: it waited for
the CPU 0.4 ms a frame). The price is ghosting when the view moves; the
adaptive setting lowers the blend while the view turns. The history holds
the world only, so the HUD and the debug menu never feed back into it.
Settings live in the debug menu (psp_debug.c): strength off/25/37/50%,
jitter, adaptive */
int psp_taa_strength = 0, psp_taa_jitter = 0, psp_taa_adaptive = 1;
/* build 78: jitter off by default (Sheepdog01's b77 run: everything vibrated). The
blend feeds its own output back (out = a x last out + (1 - a) x new), so the
two jittered positions never get equal weight: at 50% the newer one weighs
2/3 and the older 1/3, alternating each frame, and the image rocks by about
a sixth of a pixel at 15 Hz; at 25%, more. An exact average of the last two
frames would be still, but needs a second 255 KB buffer we don't have.

The spatial soften instead: the frame copied, then drawn back over itself
twice, half a pixel down-right and half a pixel up-left, with bilinear
filtering (each lands on a texel corner, the average of four), at fixed
weights: light 2/3 the pixel and 1/6 each average, medium 1/2 and 1/4,
strong 1/3 and 1/3. No history, so no ghosting and no vibration; the price
is a slight blur of textures as well as edges. It uses the same buffer, so
the temporal blend is off while it is on */
int psp_soften = 0;
float psp_taa_alpha_used = 0.0f;
static unsigned short *taa_history = NULL;
static int taa_valid = 0, taa_done = 0, taa_failed = 0;
static float taa_last_forward[3] = { 0.0f, 0.0f, 0.0f };
static float taa_jitter_x = 0.0f, taa_jitter_y = 0.0f;
static unsigned long taa_frame = 0, taa_frames_blended = 0;
#define TAA_BYTES (SCREEN_WIDTH * SCREEN_HEIGHT * 2)

/* the perspective, shifted by this frame's jitter: a translation in
normalised device units applied after the projection moves every point the
same fraction of a pixel on the screen */
static void perspective(float fov_degrees, float n, float f)
{
	sceGumLoadIdentity();
	if (taa_jitter_x != 0.0f || taa_jitter_y != 0.0f)
	{
		ScePspFVector3 shift = { taa_jitter_x, taa_jitter_y, 0.0f };

		sceGumTranslate(&shift);
	}
	sceGumPerspective(fov_degrees, (float)SCREEN_WIDTH / SCREEN_HEIGHT, n, f);
}

static int taa_history_ready(void)
{
	if (taa_history)
		return 1;
	if (taa_failed)
		return 0;
	taa_history = memalign(64, TAA_BYTES);
	if (!taa_history)
	{
		platform_log("GE: temporal blend / soften: no room for the %d KB buffer; off", TAA_BYTES / 1024);
		taa_failed = 1;
		psp_taa_strength = psp_soften = 0;
		return 0;
	}
	sceKernelDcacheWritebackInvalidateRange(taa_history, TAA_BYTES);
	platform_log("GE: temporal blend / soften: %d KB buffer at %p", TAA_BYTES / 1024, taa_history);
	return 1;
}

/* one textured full-screen pass of the buffer over the frame, shifted by
(shift, shift) texels, blended as src x keep_src + dst x keep_dst */
static void buffer_pass(float shift, int linear, unsigned int a, unsigned int b)
{
	struct pass_vertex { float u, v, x, y, z; } *v;
	int strip, strips = SCREEN_WIDTH / 32;

	v = list_take(strips * 2 * sizeof(*v));
	if (!v)
		return;
	for (strip = 0; strip < strips; strip++)
	{
		v[2 * strip].u = strip * 32 + shift;
		v[2 * strip].v = shift;
		v[2 * strip].x = (float)(strip * 32);
		v[2 * strip].y = 0.0f;
		v[2 * strip].z = 0.0f;
		v[2 * strip + 1].u = strip * 32 + 32 + shift;
		v[2 * strip + 1].v = SCREEN_HEIGHT + shift;
		v[2 * strip + 1].x = (float)(strip * 32 + 32);
		v[2 * strip + 1].y = (float)SCREEN_HEIGHT;
		v[2 * strip + 1].z = 0.0f;
	}
	sceGuTexFilter(linear ? GU_LINEAR : GU_NEAREST, linear ? GU_LINEAR : GU_NEAREST);
	sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, a | (a << 8) | (a << 16), b | (b << 8) | (b << 16));
	sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D, strips * 2, NULL, v);
}

static void buffer_state(int on)
{
	if (on)
	{
		sceGuTexSync();
		sceGuDisable(GU_DEPTH_TEST);
		sceGuDepthMask(GU_TRUE);
		sceGuDisable(GU_FOG);
		sceGuDisable(GU_CULL_FACE);
		sceGuDisable(GU_ALPHA_TEST);
		sceGuEnable(GU_TEXTURE_2D);
		sceGuTexMode(GU_PSM_5650, 0, 0, 0);
		sceGuTexImage(0, 512, 512, SCREEN_WIDTH, taa_history);
		sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
		sceGuTexWrap(GU_CLAMP, GU_CLAMP);
		sceGuTexFlush();
		sceGuEnable(GU_BLEND);
	}
	else
	{
		sceGuDisable(GU_BLEND);
		sceGuEnable(GU_DEPTH_TEST);
		sceGuDepthMask(GU_FALSE);
		sceGuEnable(GU_FOG);
	}
}

static void copy_frame_to_buffer(void)
{
	sceGuCopyImage(GU_PSM_5650, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, BUFFER_WIDTH,
		(void *)((unsigned int)sceGeEdramGetAddr() + (unsigned int)draw_buffer), 0, 0, SCREEN_WIDTH, taa_history);
}

static unsigned long soften_frames = 0;
static void soften_pass(void)
{
	/* (first pass weight, second pass weight) out of 255: light, medium,
	strong; the second is chosen so both shifted averages end equal */
	static const unsigned int first[4] = { 0, 51, 85, 128 }, second[4] = { 0, 43, 64, 85 };
	int level = psp_soften > 3 ? 3 : psp_soften;

	if (!taa_history_ready())
		return;
	copy_frame_to_buffer();
	buffer_state(1);
	buffer_pass(0.5f, 1, first[level], 255 - first[level]);
	buffer_pass(-0.5f, 1, second[level], 255 - second[level]);
	buffer_state(0);
	soften_frames++;
}

static void taa_resolve(void)
{
	static const float strengths[4] = { 0.0f, 0.25f, 0.375f, 0.5f };
	float alpha;

	if (taa_done || !frame_open)
		return;
	taa_done = 1;
	if (psp_soften > 0 && !taa_failed)
	{
		taa_valid = 0;
		psp_taa_alpha_used = 0.0f;
		soften_pass();
		return;
	}
	if (psp_taa_strength <= 0 || psp_taa_strength > 3 || taa_failed)
	{
		taa_valid = 0;
		psp_taa_alpha_used = 0.0f;
		return;
	}
	if (!taa_history_ready())
		return;
	alpha = strengths[psp_taa_strength];
	if (psp_taa_adaptive)
	{
		/* the turn since the last frame: none up to half a degree, the
		full blend; four degrees and more, none */
		float dot = camera_forward[0] * taa_last_forward[0] + camera_forward[1] * taa_last_forward[1] +
			camera_forward[2] * taa_last_forward[2];
		float degrees = dot >= 1.0f ? 0.0f : dot <= -1.0f ? 180.0f : acosf(dot) * (180.0f / 3.14159265f);
		float keep = 1.0f - (degrees - 0.5f) / 3.5f;

		alpha *= keep < 0.0f ? 0.0f : keep > 1.0f ? 1.0f : keep;
	}
	taa_last_forward[0] = camera_forward[0];
	taa_last_forward[1] = camera_forward[1];
	taa_last_forward[2] = camera_forward[2];
	psp_taa_alpha_used = alpha;
	if (taa_valid && alpha > 0.01f)
	{
		unsigned int a = (unsigned int)(alpha * 255.0f + 0.5f);

		buffer_state(1);
		buffer_pass(0.0f, 0, a, 255 - a);
		buffer_state(0);
		taa_frames_blended++;
	}
	/* this frame's world, blended, is the next frame's history */
	copy_frame_to_buffer();
	taa_valid = 1;
}

/* build 77: the debug menu's live settings (psp_debug.c). which: 0 detail
maps on, 1 nudge ceiling on, 2 level pass mode (0-4), 3 near plane (0 auto,
1 x4, 2 x2, 3 x1), 4 blend strength (0-3), 5 jitter, 6 adaptive. Returns the
value, after setting it when set is non-zero */
static int nudge_wide;
static int shadow_mode = 0;
/* build 86: the dynamic light pass, a test like b85's shadow one (Sheepdog01's b85
run: the shadow modes changed nothing, so the bright stripes beside the tank
and on the bunker are this pass). 0 as before: the lit triangles copied into
their own vertex format, depth EQUAL; 1 the level's own vertex array and
format with the kept triangles' indices, one flat colour a draw; 2 depth
GEQUAL with a nudge toward the eye; 3 the pass not drawn at all */
/* build 87: the nudge by default (Sheepdog01's b86 run 1: it removed the stripes
and looked right; "level array", the level's own vertices, still striped) */
static int light_mode = 2;
static unsigned long light_index_triangles = 0, light_nudged_draws = 0, lights_skipped = 0;
static int nudge_percent = 0; /* build 84: the lift limit off by default (Sheepdog01's b83 run) */
/* build 82: a test instrument. A fixed number of depth steps added to every
nudge toward the eye (wall symbols and decals), from the debug menu: 0, 16,
64, 256. With the slope nudges positive and sizeable (b81's tdump at red
base: +3 to +269 steps) the pillars' emblems still sank into their walls,
so the question is how large the hardware's own depth error is across a
large wall triangle; the floor that makes them whole measures it */
static int nudge_floor = 0;
/* build 83: the distance part of the nudge is a LIFT off the surface that
grows with the distance, a share of it with a ceiling, replacing b54's
fixed half centimetre. Sheepdog01's b82 run: the base emblems needed a floor of
16 steps from the tank at 20-30 units (0.4 units of lift there), none at
4 units, where 256 steps (0.27 units) saw through the pillar; a fixed step
count lifted far stencils through hills (16 steps at 100 units is 10
units). So: lift = min(share x distance, ceiling); 2% and 0.4 units give
0.08 units at 4 units (77 steps, under the 256 that bled), 0.4 at 20 (15
steps, the 16 that worked), 0.6 steps at 100 (the 3-step minimum). Both
from the debug menu */
static float nudge_lift_share = 0.01f, nudge_lift_ceiling = 0.2f; /* build 84: Sheepdog01's close-range best (b83) */
float psp_debug_seconds(void);
static int detail_off;
int psp_ge_setting(int which, int set, int value)
{
	if (detail_off < 0)
		detail_off = psp_keep("nodetail");
	if (nudge_wide < 0)
		nudge_wide = psp_keep("nudgewide");
	switch (which)
	{
	case 0:
		if (set) detail_off = !value;
		return !detail_off;
	case 1:
	{
		static const int percents[5] = { 0, 1, 2, 4, 8 };
		int k;

		if (nudge_wide < 0) nudge_wide = psp_keep("nudgewide");
		if (set)
		{
			nudge_percent = percents[value < 0 ? 0 : value > 4 ? 4 : value];
			nudge_wide = 0;
		}
		if (nudge_wide)
			return 0;
		for (k = 0; k < 5; k++)
			if (percents[k] == nudge_percent)
				return k;
		return 2;
	}
	case 2:
		if (set) psp_pass_mode = value;
		return psp_pass_mode;
	case 3:
		if (set)
		{
			near_auto = value == 0;
			near_scale = value == 1 ? 4 : value == 2 ? 2 : value == 3 ? 1 : (near_scale < 0 ? 1 : near_scale);
		}
		return near_auto ? 0 : near_scale == 4 ? 1 : near_scale == 2 ? 2 : 3;
	case 4:
		if (set) psp_taa_strength = value;
		return psp_taa_strength;
	case 5:
		if (set) psp_taa_jitter = value;
		return psp_taa_jitter;
	case 6:
		if (set) psp_taa_adaptive = value;
		return psp_taa_adaptive;
	case 7:
		if (set) psp_soften = value;
		return psp_soften;
	case 9:
		if (ge_serial < 0) ge_serial = psp_keep("geserial");
		if (set) ge_serial = value;
		return ge_serial;
	case 10:
		if (uv_centre < 0) uv_centre = !psp_keep("uvwide");
		if (set) uv_centre = value;
		return uv_centre;
	case 12:
	{
		static const float shares[4] = { 0.005f, 0.01f, 0.02f, 0.04f };
		int k;

		if (set)
			nudge_lift_share = shares[value < 0 ? 0 : value > 3 ? 3 : value];
		for (k = 0; k < 4; k++)
			if (shares[k] == nudge_lift_share)
				return k;
		return 2;
	}
	case 13:
	{
		static const float ceilings[4] = { 0.1f, 0.2f, 0.4f, 0.8f };
		int k;

		if (set)
			nudge_lift_ceiling = ceilings[value < 0 ? 0 : value > 3 ? 3 : value];
		for (k = 0; k < 4; k++)
			if (ceilings[k] == nudge_lift_ceiling)
				return k;
		return 2;
	}
	case 14:
		if (set) shadow_mode = value;
		return shadow_mode;
	case 15:
		if (set) light_mode = value < 0 ? 0 : value > 3 ? 3 : value;
		return light_mode;
	case 11:
	{
		static const int floors[4] = { 0, 16, 64, 256 };
		int k;

		if (set)
			nudge_floor = floors[value < 0 ? 0 : value > 3 ? 3 : value];
		for (k = 0; k < 4; k++)
			if (floors[k] == nudge_floor)
				return k;
		return 0;
	}
	}
	return 0;
}

void psp_ge_window_begin(const float *position, const float *forward, const float *up,
	float vertical_field_of_view, float z_near, float z_far)
{
	ScePspFVector3 eye, center, upward;

	if (frame_open)
		return;
	if (!ge_ready)
		ge_initialize();
	sceGuStart(GU_DIRECT, display_list);
	sceGuFog(fog_near, fog_far, fog_color);
	list_limit = DISPLAY_LIST_BYTES - HUD_RESERVE - TRANSPARENT_RESERVE;
	hud_phase = 0;
	taa_done = 0;
	taa_frame++;
	if (psp_taa_strength > 0 && psp_taa_jitter && !psp_soften && !taa_failed)
	{
		/* a quarter pixel each way on alternate frames */
		float sign = (taa_frame & 1) ? 0.25f : -0.25f;

		taa_jitter_x = sign * 2.0f / SCREEN_WIDTH;
		taa_jitter_y = sign * 2.0f / SCREEN_HEIGHT;
	}
	else
		taa_jitter_x = taa_jitter_y = 0.0f;
	if (magenta < 0)
		magenta = psp_keep("clearmagenta");
	sceGuClearColor(magenta ? 0xFFFF00FF : SKY_COLOR);
	sceGuClearDepth(0);
	sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
	/* build 63: the world's depth state from a known start each frame, so a
	slip the frame before (a test left off, a nudge left on) can't carry */
	sceGuEnable(GU_DEPTH_TEST);
	sceGuDepthFunc(GU_GEQUAL);
	sceGuDepthOffset(0);

	if (!(z_near > 0.01f))
		z_near = 0.05f;
	if (!(z_far > z_near))
		z_far = 1000.0f;
	{
		/* build 49: the depth buffer is 16 bits (the Xbox's 24), and a
		perspective depth spends most of its steps near the eye: the step at
		distance z is about z * z / (near * 61439). At the game's near plane
		a surface 30 units off resolves only to a few tenths of a unit, so
		overlapping surfaces far off flicker and a rock's buried base pokes
		through the ground (Sheepdog01, b48). "near2" / "near4" move the world's near
		plane out 2 or 4 times (2 or 4 times finer everywhere; the price:
		walls closer than the new plane are cut open). The first-person
		weapon has its own projection and depth range */
		if (near_scale < 0)
		{
			near_scale = psp_keep("near4") ? 4 : psp_keep("near2") ? 2 : 1;
			near_auto = psp_keep("nearauto");
			platform_log("GE: the game's near plane %.4f, far %.1f; world near plane x%d%s", z_near, z_far, near_scale,
				near_auto ? "; nearauto: x4, x2 or x1 by what is near the eye" : "");
		}
		if (near_auto)
			z_near = near_plane_auto(position, z_near);
		else
			z_near *= near_scale;
	}
	near_clip = z_near;
	sceGumMatrixMode(GU_PROJECTION);
	perspective(vertical_field_of_view * (180.0f / 3.14159265f), z_near, z_far);
	projection_fov = vertical_field_of_view;
	projection_near = z_near;
	projection_far = z_far;
	model_first_person = 0;
	model_near = 0;
	/* the clipping against the near plane, every frame (the GE's state may
	not survive the PSP sleeping) */
	sceGuEnable(GU_CLIP_PLANES);
	view_far = z_far;
	eye.x = position[0]; eye.y = position[1]; eye.z = position[2];
	center.x = eye.x + forward[0]; center.y = eye.y + forward[1]; center.z = eye.z + forward[2];
	upward.x = up[0]; upward.y = up[1]; upward.z = up[2];
	/* camera-relative (build 35): the GE keeps its matrices as 24-bit
	floats, so a view matrix carrying the camera's world position (a hundred
	units out on Blood Gulch) and a model matrix carrying the model's lost
	about a millimetre each; next to the camera that is several pixels, and
	the first-person weapon crumpled as its bones moved. The view is now the
	camera's turn alone, and every model matrix is made relative to the
	camera on the CPU, in full floats (relative(), world_model()) */
	{
		ScePspFVector3 origin = { 0.0f, 0.0f, 0.0f }, ahead;

		ahead.x = forward[0]; ahead.y = forward[1]; ahead.z = forward[2];
		sceGumMatrixMode(GU_VIEW);
		sceGumLoadIdentity();
		sceGumLookAt(&origin, &ahead, &upward);
	}
	(void)center;
	frame_open = 1;
	draws_this_frame = triangles_this_frame = clipped_this_frame = culled_this_frame = 0;
	model_draws_this_frame = 0;

	/* the camera's directions, for classifying triangles */
	{
		float length;

		camera_eye[0] = eye.x; camera_eye[1] = eye.y; camera_eye[2] = eye.z;
		camera_forward[0] = forward[0]; camera_forward[1] = forward[1]; camera_forward[2] = forward[2];
		/* right = forward x up */
		camera_right[0] = forward[1] * up[2] - forward[2] * up[1];
		camera_right[1] = forward[2] * up[0] - forward[0] * up[2];
		camera_right[2] = forward[0] * up[1] - forward[1] * up[0];
		length = sqrtf(camera_right[0] * camera_right[0] + camera_right[1] * camera_right[1] + camera_right[2] * camera_right[2]);
		if (length > 0.0f)
		{
			camera_right[0] /= length; camera_right[1] /= length; camera_right[2] /= length;
		}
		/* up = right x forward */
		camera_up[0] = camera_right[1] * forward[2] - camera_right[2] * forward[1];
		camera_up[1] = camera_right[2] * forward[0] - camera_right[0] * forward[2];
		camera_up[2] = camera_right[0] * forward[1] - camera_right[1] * forward[0];
		guard_y = GUARD * tanf(vertical_field_of_view * 0.5f);
		guard_x = guard_y * (float)SCREEN_WIDTH / SCREEN_HEIGHT;
	}
	sceGumMatrixMode(GU_MODEL);
	world_model();
}

/* a point in camera space: x right, y up, z forward (distance ahead) */
static inline void to_camera(const float *p, float *out)
{
	float d0 = p[0] - camera_eye[0], d1 = p[1] - camera_eye[1], d2 = p[2] - camera_eye[2];

	out[0] = d0 * camera_right[0] + d1 * camera_right[1] + d2 * camera_right[2];
	out[1] = d0 * camera_up[0] + d1 * camera_up[1] + d2 * camera_up[2];
	out[2] = d0 * camera_forward[0] + d1 * camera_forward[1] + d2 * camera_forward[2];
}

/* a corner to be cut: camera space for the clipping, the world position
carried along for drawing (build 34) */
static inline void to_camera_corner(const float *p, float *corner)
{
	to_camera(p, corner);
	corner[7] = p[0];
	corner[8] = p[1];
	corner[9] = p[2];
}

/* which of the five planes a camera-space point is outside of, as bits:
near, right, left, top, bottom */
static inline int outside(const float *v)
{
	int bits = 0;

	if (v[2] < NEAR_CLIP) bits |= 1;
	if (v[0] > guard_x * v[2]) bits |= 2;
	if (v[0] < -guard_x * v[2]) bits |= 4;
	if (v[1] > guard_y * v[2]) bits |= 8;
	if (v[1] < -guard_y * v[2]) bits |= 16;
	return bits;
}

/* build 42: a model corner the GE can't take: behind the near plane (with
a margin), or so far to a side that its screen position overflows the GE's
2048 pixels, where it wraps round and the triangle becomes a long spike
across the screen (Sheepdog01's warthog roll bars and axles passing the camera) */
static inline int camera_corner_out(float x, float y, float z, float limit)
{
	return z < limit || x > guard_x * z || x < -guard_x * z || y > guard_y * z || y < -guard_y * z;
}

/* each vertex's verdict for the draw at hand: corner_stamp in, +1 out,
anything else not yet worked out (a stamp, so nothing is cleared per draw) */
/* build 44: 16K entries (32 KB, was 128 KB): a model buffer's vertices
number a few thousand at most; a corner past the table is worked out
each time */
#define CORNER_VERDICTS 8192 /* build 64: 8K (16 KB; was 16K), room for the angle buckets */
static unsigned short corner_verdict[CORNER_VERDICTS];
static unsigned short corner_stamp = 0;

/* build 60: a stamp a draw, 32 apart, so a verdict can be the stamp plus a
corner's five plane bits (rigid parts) or plus 0 / 1 (skinned ones) */
#define CORNER_STAMP_STEP 32
static void corner_verdict_reset(void)
{
	corner_stamp += CORNER_STAMP_STEP;
	if (corner_stamp < CORNER_STAMP_STEP || corner_stamp > 0xFFFF - 2 * CORNER_STAMP_STEP)
	{
		memset(corner_verdict, 0xFF, sizeof(corner_verdict));
		corner_stamp = CORNER_STAMP_STEP;
	}
}

/* the same for a camera-relative world position */
static inline int model_corner_out(float x, float y, float z, float limit)
{
	return camera_corner_out(x * camera_right[0] + y * camera_right[1] + z * camera_right[2],
		x * camera_up[0] + y * camera_up[1] + z * camera_up[2],
		x * camera_forward[0] + y * camera_forward[1] + z * camera_forward[2], limit);
}

/* a model-space point through a part's camera-relative matrix into camera
space (build 60) */
static inline void model_to_camera(const ScePspFMatrix4 *m, const float *p, float *out)
{
	float x = m->x.x * p[0] + m->y.x * p[1] + m->z.x * p[2] + m->w.x;
	float y = m->x.y * p[0] + m->y.y * p[1] + m->z.y * p[2] + m->w.y;
	float z = m->x.z * p[0] + m->y.z * p[1] + m->z.z * p[2] + m->w.z;

	out[0] = x * camera_right[0] + y * camera_right[1] + z * camera_right[2];
	out[1] = x * camera_up[0] + y * camera_up[1] + z * camera_up[2];
	out[2] = x * camera_forward[0] + y * camera_forward[1] + z * camera_forward[2];
}

/* the signed distance of a point inside one plane (positive inside) */
static inline float plane_distance(const float *v, int plane)
{
	switch (plane)
	{
	case 0: return v[2] - NEAR_CLIP;
	case 1: return guard_x * v[2] - v[0];
	case 2: return guard_x * v[2] + v[0];
	case 3: return guard_y * v[2] - v[1];
	default: return guard_y * v[2] + v[1];
	}
}

/* cuts a camera-space triangle to the inside of the planes it crosses; each
corner carries its camera-space position and both texture coordinates
(x, y, z, base u, v, lightmap u, v), all cut alike. The pieces (a fan, as a
triangle list) go into out; returns how many corners it added */
/* a cut corner: camera-space position (0-2), two pairs of texture
coordinates or other values (3-6), and its world position (7-9); build 60:
one more value (10), a model corner's normal's third part */
#define CORNER_FLOATS 11
static int clip_triangle(float polygon[3][CORNER_FLOATS], int bits, float (*out)[CORNER_FLOATS], int room)
{
	float buffer[2][12][CORNER_FLOATS];
	int count = 3, plane, index, source = 0, added = 0, k;

	memcpy(buffer[0], polygon, 3 * sizeof(polygon[0]));
	for (plane = 0; plane < 5 && count >= 3; plane++)
	{
		int kept = 0;

		if (!(bits & (1 << plane)))
			continue;
		for (index = 0; index < count; index++)
		{
			float *a = buffer[source][index], *b = buffer[source][(index + 1) % count];
			float da = plane_distance(a, plane), db = plane_distance(b, plane);

			if (da >= 0.0f && kept < 12)
				memcpy(buffer[!source][kept++], a, sizeof(buffer[0][0]));
			if ((da >= 0.0f) != (db >= 0.0f) && kept < 12)
			{
				float t = da / (da - db);

				for (k = 0; k < CORNER_FLOATS; k++)
					buffer[!source][kept][k] = a[k] + (b[k] - a[k]) * t;
				kept++;
			}
		}
		count = kept;
		source = !source;
	}
	for (index = 1; index + 1 < count && added + 3 <= room; index++)
	{
		memcpy(out[added++], buffer[source][0], sizeof(buffer[0][0]));
		memcpy(out[added++], buffer[source][index], sizeof(buffer[0][0]));
		memcpy(out[added++], buffer[source][index + 1], sizeof(buffer[0][0]));
	}
	clipped_this_frame++;
	return added;
}

/* ---------- the level, textured (build 20)

The level's own vertices can't carry texture coordinates to the GE (in the
game's layout they come after the position, and the GE wants them before),
and the CPU reads them (visibility, object lighting), so they stay as they
are. For the GE, each material's vertices are copied once, the first time
it is drawn, into two streams: base texture coordinates with the position,
and lightmap coordinates with the position (20 bytes each).

A lit surface is two passes: the base texture, then the lightmap multiplied
over it (where the depth is exactly the first pass's, so alpha-tested holes
stay holes). Fog: the first pass fades toward the fog colour and the second
toward white, so far away the result is the fog colour and near it is
base x lightmap. */

struct ge_vertex
{
	float u, v, x, y, z;
};

struct level_stream
{
	const void *source;
	struct ge_vertex *base, *lightmap;
	/* build 68: the packed normals (the map's 32-bit form), for the dynamic
	lights' reach; the copies are all the renderer has of a material once
	its bytes have left the cache */
	unsigned int *normals;
	int count;
	/* build 68: the frame it was last drawn in, for letting the least used
	go when the copies are over their budget (a map built with its geometry
	outside the tags, psp_geo.c) */
	unsigned int last_frame;
	/* build 58: texture coordinate units per world unit (0: not yet worked out) */
	float uv_per_unit;
	/* build 66: the vertices' bounding box, for skipping the classification
	of a draw wholly inside the view */
	float low[3], high[3];
};
#define LEVEL_STREAMS 512
static struct level_stream level_streams[LEVEL_STREAMS];
static unsigned long stream_bytes = 0;
static unsigned int stream_frame = 1;
static unsigned long streams_let_go = 0;
/* build 68: the copies' budget while a geometry pack is open (its blocks come
and go; the copies are what stays) */
#define LEVEL_STREAM_BUDGET (1024 * 1024)
unsigned int psp_geo_address_of(const void *ptr);
unsigned int psp_geo_generation(const void *ptr);
int psp_geo_open_count(void);

/* build 68: what a material's vertices are known by. In the tags, their
address; in the geometry cache, their address in the pack, since the block
they sit in can be reused for other vertices once they are evicted */
int psp_geo_external(unsigned int address);
void *psp_geo_lock(unsigned int address, unsigned int size);

static const void *stream_key(const void *vertices)
{
	unsigned int external;

	/* the level's hooks pass the pack address itself for vertices kept
	outside (psp_fixups.py): the copies are what's drawn, and the pack's
	bytes are only asked for to build them */
	if (psp_geo_external((unsigned int)vertices))
		return vertices;
	external = psp_geo_address_of(vertices);
	return external ? (const void *)external : vertices;
}

static struct level_stream *level_stream_get(const void *vertices, const void *lightmap_vertices, int count)
{
	const void *key = stream_key(vertices);
	unsigned int slot = ((unsigned int)key >> 5) % LEVEL_STREAMS, tries = 0;
	struct level_stream *stream;
	int index;

	while (level_streams[slot].source && level_streams[slot].source != key)
	{
		slot = (slot + 1) % LEVEL_STREAMS;
		if (++tries >= LEVEL_STREAMS)
			return NULL;
	}
	stream = &level_streams[slot];
	if (stream->source && stream->base)
	{
		stream->last_frame = stream_frame;
		return stream;
	}
	/* new, or let go when over budget (level_streams_trim): built here */
	if (psp_geo_external((unsigned int)vertices))
	{
		/* the pack's bytes: the material's own vertices (32 each) and, when it
		has them, the lightmap's (8 each) right after; one block */
		unsigned int address = (unsigned int)vertices;
		const char *raw = psp_geo_lock(address, (unsigned int)count * (lightmap_vertices ? 40 : 32));

		if (!raw)
			return NULL;
		vertices = raw;
		if (lightmap_vertices)
			lightmap_vertices = raw + 32 * count;
	}
	stream->source = key;
	stream->count = count;
	stream->last_frame = stream_frame;
	stream->uv_per_unit = 0.0f;
	stream->base = malloc(count * sizeof(struct ge_vertex));
	stream->lightmap = lightmap_vertices ? malloc(count * sizeof(struct ge_vertex)) : NULL;
	stream->normals = malloc(count * sizeof(unsigned int));
	if (!stream->base || (lightmap_vertices && !stream->lightmap) || !stream->normals)
	{
		platform_log("GE: no memory for a material's vertices (%d)", count);
		free(stream->base);
		free(stream->lightmap);
		free(stream->normals);
		stream->base = stream->lightmap = NULL;
		stream->normals = NULL;
		return NULL;
	}
	stream->low[0] = stream->low[1] = stream->low[2] = 1e30f;
	stream->high[0] = stream->high[1] = stream->high[2] = -1e30f;
	for (index = 0; index < count; index++)
	{
		const float *vertex = (const float *)((const char *)vertices + 32 * index);
		struct ge_vertex *b = &stream->base[index];
		int axis;

		b->x = vertex[0]; b->y = vertex[1]; b->z = vertex[2];
		b->u = vertex[6]; b->v = vertex[7];
		stream->normals[index] = ((const unsigned int *)vertex)[3];
		for (axis = 0; axis < 3; axis++)
		{
			if (vertex[axis] < stream->low[axis]) stream->low[axis] = vertex[axis];
			if (vertex[axis] > stream->high[axis]) stream->high[axis] = vertex[axis];
		}
		if (stream->lightmap)
		{
			const short *texcoord = (const short *)((const char *)lightmap_vertices + 8 * index + 4);
			struct ge_vertex *l = &stream->lightmap[index];

			l->x = b->x; l->y = b->y; l->z = b->z;
			l->u = (texcoord[0] * 2.0f + 1.0f) * (1.0f / 65535.0f);
			l->v = (texcoord[1] * 2.0f + 1.0f) * (1.0f / 65535.0f);
		}
	}
	sceKernelDcacheWritebackRange(stream->base, count * sizeof(struct ge_vertex));
	if (stream->lightmap)
		sceKernelDcacheWritebackRange(stream->lightmap, count * sizeof(struct ge_vertex));
	stream_bytes += count * sizeof(struct ge_vertex) * (stream->lightmap ? 2 : 1) + count * sizeof(unsigned int);
	return stream;
}

int psp_tex_bind(unsigned int key, int *has_alpha);
int psp_tex_size(unsigned int key, int *width, int *height);
static unsigned long textured_draws = 0, flat_draws = 0;

/* build 58: the texture level chosen by distance ("slope" mode) instead of
the GE's automatic choice. The automatic choice (as PPSSPP models it,
GPU/Software/Rasterizer.cpp) looks only at how s changes across the screen
and t down it, so on ground seen at a slant it depends on how the texture
happens to lie: turn, and the level changes, and the ground and the bullet
holes on it shimmer and change shape (Sheepdog01, b57; holes on the walls, which
face the eye, stayed still). In slope mode the GE takes the level from each
pixel's depth: log2(2 x w x slope), w its distance. With a surface's texels
per world unit known, slope = texels per unit / (2 x pixels per unit at
distance 1) puts one texel on one pixel of a surface facing the eye, the
Xbox's choice there; at a slant it doesn't blur more as the Xbox's did, but
it no longer swims as the view turns. "mipauto": the automatic choice */
static int mip_slope = -1;

static float pixels_per_unit(void)
{
	return (SCREEN_HEIGHT * 0.5f) / tanf(projection_fov * 0.5f);
}

/* texture coordinate units per world unit over a draw's triangles */
static float uv_density(const struct ge_vertex *stream, const unsigned short *indices, long triangle_count)
{
	double uv_area = 0.0, world_area = 0.0;
	long t;

	for (t = 0; t < triangle_count && t < 512; t++)
	{
		const struct ge_vertex *a = &stream[indices[3 * t]], *b = &stream[indices[3 * t + 1]], *c = &stream[indices[3 * t + 2]];
		float e1[3] = { b->x - a->x, b->y - a->y, b->z - a->z }, e2[3] = { c->x - a->x, c->y - a->y, c->z - a->z };
		float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };

		world_area += sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		uv_area += fabsf((b->u - a->u) * (c->v - a->v) - (c->u - a->u) * (b->v - a->v));
	}
	return world_area > 0.0 ? (float)sqrt(uv_area / world_area) : 0.0f;
}

/* the GE's level mode for a texture of size width x height drawn at
uv_per_unit (times scale) */
static float texture_levels_by_distance(float uv_per_unit, int width, int height, float scale, float bias)
{
	float slope;

	if (mip_slope < 0)
		mip_slope = !psp_keep("mipauto");
	if (!mip_slope || uv_per_unit <= 0.0f || width <= 0 || height <= 0)
	{
		sceGuTexLevelMode(tex_level_mode, bias);
		return 0.0f;
	}
	slope = uv_per_unit * scale * sqrtf((float)width * (float)height) / (2.0f * pixels_per_unit());
	sceGuTexSlope(slope);
	sceGuTexLevelMode(tex_level_mode == GU_TEXTURE_CONST ? GU_TEXTURE_CONST : GU_TEXTURE_SLOPE, bias);
	return tex_level_mode == GU_TEXTURE_CONST ? 0.0f : slope;
}

/* build 64: the slant put back into the level choice. In slope mode the GE
takes the level from each pixel's depth alone, as for a surface facing the
eye; one seen at a slant is squeezed on the screen by 1 / cos(angle) across
the slant, and the Xbox's choice (by how fast the texture changes across
the screen, in either direction) is that much blurrier there. Sheepdog01's b63 run
B: by distance alone, crisp floors but the ground swam outside and the
cliffs' feet looked aliased. The GE's slope is one value a draw, so each
level draw's triangles are sorted into buckets by their slant (half a level
apart, up to three levels: 1 / cos up to 8), and each bucket is drawn with
its own slope. "noangle": by distance alone (b63 run B) */
#define ANGLE_BUCKETS 7
/* build 66: each slant bucket twice, near and far: a triangle whose nearest
corner is past DETAIL_FAR is drawn in the base and lightmap passes only. The
detail passes fade to their neutral grey by DETAIL_FAR (the fog), so they
change nothing there; skipping them saves the GE the fill and the CPU the
draws. "detailallfar": the detail passes everywhere, as before */
#define SORT_BUCKETS (2 * ANGLE_BUCKETS)
#define ANGLE_TRIANGLES 4096
static int angle_buckets = -1, detail_all_far = -1;
static int angle_bucketed = 0, angle_near_any = 0;
static long angle_first[SORT_BUCKETS], angle_count[SORT_BUCKETS];
static unsigned char angle_bucket_of[ANGLE_TRIANGLES];
static const float angle_factor[ANGLE_BUCKETS] = { 1.0f, 1.41421356f, 2.0f, 2.82842712f, 4.0f, 5.65685425f, 8.0f };
static unsigned long angle_draws = 0, angle_triangles[ANGLE_BUCKETS], angle_us = 0, angle_far_triangles = 0,
	detail_passes_skipped_far = 0, level_draws_inside = 0, level_draws_classified = 0;
static float detail_far_squared = 0.0f;

/* sorts a draw's triangles by slant; returns the list to draw (the given
one, or a copy in the display list sorted by bucket) */
static const unsigned short *angle_sort(const struct ge_vertex *stream, const unsigned short *indices, long count,
	int far_possible)
{
	long t, total[SORT_BUCKETS];
	int b, used = 0;
	unsigned short *sorted;
	float ex = camera_eye[0], ey = camera_eye[1], ez = camera_eye[2];

	angle_bucketed = 0;
	angle_near_any = 1;
	if (angle_buckets < 0)
	{
		angle_buckets = !psp_keep("noangle");
		detail_all_far = psp_keep("detailallfar");
	}
	if (mip_slope < 0)
		mip_slope = !psp_keep("mipauto");
	if (!angle_buckets || !mip_slope || tex_level_mode == GU_TEXTURE_CONST || count <= 0 || count > ANGLE_TRIANGLES)
		return indices;
	if (detail_all_far)
		far_possible = 0;
	memset(total, 0, sizeof(total));
	for (t = 0; t < count; t++)
	{
		const struct ge_vertex *a = &stream[indices[3 * t]], *p = &stream[indices[3 * t + 1]], *q = &stream[indices[3 * t + 2]];
		float e1[3] = { p->x - a->x, p->y - a->y, p->z - a->z }, e2[3] = { q->x - a->x, q->y - a->y, q->z - a->z };
		float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
		float da[3] = { a->x - ex, a->y - ey, a->z - ez };
		float d[3] = { da[0] + e1[0] * (1.0f / 3.0f) + e2[0] * (1.0f / 3.0f), da[1] + e1[1] * (1.0f / 3.0f) + e2[1] * (1.0f / 3.0f),
			da[2] + e1[2] * (1.0f / 3.0f) + e2[2] * (1.0f / 3.0f) };
		float dot = n[0] * d[0] + n[1] * d[1] + n[2] * d[2];
		float lengths = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) * (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		int bucket = 0;

		/* 1 / cos^2 against the bucket edges, 2^(b - 1/2) */
		if (lengths > 0.0f)
		{
			float ratio_limit = dot * dot;

			while (bucket < ANGLE_BUCKETS - 1 && lengths > ratio_limit * angle_factor[bucket] * angle_factor[bucket] * 1.41421356f)
				bucket++;
		}
		if (far_possible)
		{
			/* far: every corner past DETAIL_FAR (the nearest corner decides) */
			float sa = da[0] * da[0] + da[1] * da[1] + da[2] * da[2];

			if (sa > detail_far_squared)
			{
				float dp[3] = { p->x - ex, p->y - ey, p->z - ez };
				float sp = dp[0] * dp[0] + dp[1] * dp[1] + dp[2] * dp[2];

				if (sp > detail_far_squared)
				{
					float dq[3] = { q->x - ex, q->y - ey, q->z - ez };

					if (dq[0] * dq[0] + dq[1] * dq[1] + dq[2] * dq[2] > detail_far_squared)
						bucket += ANGLE_BUCKETS;
				}
			}
		}
		angle_bucket_of[t] = (unsigned char)bucket;
		total[bucket]++;
	}
	for (b = 0; b < SORT_BUCKETS; b++)
		if (total[b])
			used++;
	sorted = used > 1 ? list_take((int)(count * 3 * sizeof(unsigned short) + 3) & ~3) : NULL;
	if (used > 1 && !sorted)
		return indices;
	{
		long first = 0;

		angle_near_any = 0;
		for (b = 0; b < SORT_BUCKETS; b++)
		{
			angle_first[b] = first;
			angle_count[b] = total[b];
			angle_triangles[b % ANGLE_BUCKETS] += total[b];
			if (b >= ANGLE_BUCKETS)
				angle_far_triangles += total[b];
			else if (total[b])
				angle_near_any = 1;
			first += total[b];
		}
	}
	angle_bucketed = 1;
	angle_draws++;
	if (!sorted)
	{
		/* one bucket: the list as it is, its first triangle at 0 */
		for (b = 0; b < SORT_BUCKETS; b++)
			angle_first[b] = 0;
		return indices;
	}
	{
		long place[SORT_BUCKETS];

		memcpy(place, angle_first, sizeof(place));
		for (t = 0; t < count; t++)
		{
			long at = place[angle_bucket_of[t]]++;

			sorted[3 * at] = indices[3 * t];
			sorted[3 * at + 1] = indices[3 * t + 1];
			sorted[3 * at + 2] = indices[3 * t + 2];
		}
	}
	return sorted;
}

/* one pass over the kept triangles (a list of indices into the stream) and
the cut-up ones (camera-space corners, drawn with no view matrix) */
/* build 66: a draw's cut corners as GE vertices, made once a draw (they were
made again in each of the four passes) */
static struct ge_vertex *cut_vertices_prepared[2];
static int cut_vertices_count = 0;

static void draw_pass(const struct ge_vertex *stream, const unsigned short *indices, int index_count,
	float (*cut)[CORNER_FLOATS], int cut_count, int lightmap)
{
	if (index_count > 0)
		sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
			index_count, indices, stream);
	if (cut_count > 0 && cut_count == cut_vertices_count && cut_vertices_prepared[lightmap ? 1 : 0])
	{
		sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D, cut_count, NULL,
			cut_vertices_prepared[lightmap ? 1 : 0]);
		return;
	}
	if (cut_count > 0)
	{
		struct ge_vertex *corners = list_take(cut_count * sizeof(struct ge_vertex));
		int index;

		if (!corners)
			return;

		/* in the world, through the same matrices as the uncut triangles
		(build 34): drawn from camera space, a cut triangle's corners came out
		a hair off its uncut neighbours' on the PSP, and their shared edges
		opened into see-through seams near the camera. Corners the cut kept
		are the very same floats as the neighbours'; the new ones lie on the
		near or guard planes, well inside what the GE can draw */
		for (index = 0; index < cut_count; index++)
		{
			corners[index].u = cut[index][lightmap ? 5 : 3];
			corners[index].v = cut[index][lightmap ? 6 : 4];
			corners[index].x = cut[index][7];
			corners[index].y = cut[index][8];
			corners[index].z = cut[index][9];
		}
		sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D, cut_count, NULL, corners);
	}
}

/* a pass of a level draw sorted by angle_sort: each bucket with the slope
times its factor, the cut-up corners (at the eye, level 0 regardless) with
the slope as it is */
static void draw_pass_angled(const struct ge_vertex *stream, const unsigned short *indices, int index_count,
	float (*cut)[CORNER_FLOATS], int cut_count, int lightmap, float slope, int near_only)
{
	int b, buckets = near_only ? ANGLE_BUCKETS : SORT_BUCKETS;

	if (!angle_bucketed || slope <= 0.0f || index_count <= 0)
	{
		draw_pass(stream, indices, index_count, cut, cut_count, lightmap);
		return;
	}
	for (b = 0; b < buckets; b++)
	{
		if (!angle_count[b])
			continue;
		sceGuTexSlope(slope * angle_factor[b % ANGLE_BUCKETS]);
		sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
			(int)angle_count[b] * 3, indices + 3 * angle_first[b], stream);
	}
	sceGuTexSlope(slope);
	if (cut_count > 0)
		draw_pass(stream, NULL, 0, cut, cut_count, lightmap);
}


/* one material's visible triangles: vertices are the game's 32-byte level
vertices, lightmap_vertices its 8-byte lightmap ones (or NULL), indices its
16-bit triangle list. base_key and lightmap_key name the textures (0: none).

Each vertex is classified once (build 19): its outside-the-planes bits go
into vertex_codes the first time a triangle uses it, so a vertex shared by
six triangles is fetched and transformed once instead of six times. Only
the corners of triangles that need cutting up are transformed again */
/* the shader's detail maps (build 31): the Xbox combines the base map with
a tiled detail map and a tiled micro detail map, each doubled and multiplied
in. The GE has one texture at a time, so each is a pass of its own, like the
lightmap: the same triangles, the base map's coordinates scaled by the
shader's tiling, blended as 2 x src x dst where pass 1 drew. texpack.py bakes
the maps with an average of 0.5 and folds the real averages into the base
map, so a pass that is skipped, mipped down or faded out changes nothing; it
fades to that neutral grey from DETAIL_NEAR to DETAIL_FAR (or the fog's
far end), where the maps would only shimmer at 272 lines */
/* build 57: 8 to 24 units (was 20 to 60): far off, the detail maps' pattern
only beat against the pixels (the canyon bands; Sheepdog01 preferred the walls
without them, b54 shots 24-25), and at 272 lines it can't be seen there
anyway. "detailfar": the old range */
/* build 64: 20 to 60 by default (Sheepdog01's b63 runs A and B: no bands with the
Xbox's faded levels, pack 59). "detailnear": 8 to 24 (b57-b63) */
#define DETAIL_NEAR (detail_far ? 20.0f : 8.0f)
#define DETAIL_FAR (detail_far ? 60.0f : 24.0f)
static int detail_far = -1;
static unsigned int detail_key[2];
static float detail_u[2], detail_v[2];
static unsigned long detail_passes = 0;
static int detail_off = -1;
static float detail_bias = -2.0f;
static int detail_slope = -1;
int psp_keep(const char *name);

void psp_ge_level_detail(unsigned int primary_key, float primary_u, float primary_v,
	unsigned int micro_key, float micro_u, float micro_v)
{
	detail_key[0] = primary_key; detail_u[0] = primary_u; detail_v[0] = primary_v;
	detail_key[1] = micro_key; detail_u[1] = micro_u; detail_v[1] = micro_v;
}

#define CODE_UNKNOWN 0x80
static unsigned char vertex_codes[65536];
static int box_test = -1;
static unsigned long budget_renderer_us = 0, budget_wait_us = 0;

/* build 64: the renderer's CPU time and the wait for the GE, us a frame, as
last logged (the heartbeat's budget line) */
void psp_ge_budget(unsigned long *renderer_us, unsigned long *wait_us)
{
	*renderer_us = budget_renderer_us;
	*wait_us = budget_wait_us;
}
#define MAXIMUM_CUT_CORNERS 384 /* the peak seen on the PSP is 249 (build 32); build 60: 768 to 512, room for model_cut */
static float cut_corners[MAXIMUM_CUT_CORNERS][CORNER_FLOATS];

static int level_cull = -1;

static void psp_ge_draw_level_body(const void *vertices, const void *lightmap_vertices, int vertex_count,
	const unsigned short *indices, long triangle_count, unsigned int color,
	unsigned int base_key, unsigned int lightmap_key, int alpha_tested)
{
	const unsigned short *draw = indices;
	unsigned short *kept = NULL;
	long index, kept_count = 0;
	unsigned int lowest = 0xFFFF, highest = 0;
	struct level_stream *stream;
	int cut_count = 0, base_alpha = 0, textured, all_inside, far_possible = 0;
	unsigned int cut_us = 0;
	float base_slope = 0.0f, detail_pass_slope = 0.0f;

	if (!frame_open || triangle_count <= 0 || !vertices || !indices || vertex_count <= 0 || list_full())
		return;
	part_start();
	cut_vertices_count = 0;
	cut_vertices_prepared[0] = cut_vertices_prepared[1] = NULL;
	stream = level_stream_get(vertices, lightmap_vertices, vertex_count);
	if (!stream)
		return;
	for (index = 0; index < triangle_count * 3; index++)
	{
		unsigned int vertex = indices[index];

		if (vertex < lowest)
			lowest = vertex;
		if (vertex > highest)
			highest = vertex;
	}
	if (highest >= (unsigned int)vertex_count)
		return;
	part_add(0);
	/* build 66: a draw whose whole vertex box lies inside the view (in front
	of the near plane, within the guard band) needs no classifying: every
	triangle is kept as it is. Outdoors most draws are (the far walls); the
	ground underfoot still gets the loop. The box's corners are tested the
	way the vertices would be. "noboxtest": the loop for every draw */
	if (box_test < 0)
		box_test = !psp_keep("noboxtest");
	all_inside = 0;
	if (box_test)
	{
		int corner;

		all_inside = 1;
		for (corner = 0; corner < 8 && all_inside; corner++)
		{
			float p[3] = { corner & 1 ? stream->high[0] : stream->low[0], corner & 2 ? stream->high[1] : stream->low[1],
				corner & 4 ? stream->high[2] : stream->low[2] }, c[3];

			to_camera(p, c);
			if (outside(c))
				all_inside = 0;
		}
	}
	if (all_inside)
	{
		kept_count = triangle_count;
		level_draws_inside++;
	}
	else
	{
		memset(vertex_codes + lowest, CODE_UNKNOWN, highest - lowest + 1);
		level_draws_classified++;
	}
	/* each triangle against the camera: skipped, kept, or cut up */
	for (index = 0; index < triangle_count && !all_inside; index++)
	{
		const unsigned short *triangle = indices + 3 * index;
		int bits[3], corner;

		for (corner = 0; corner < 3; corner++)
		{
			int code = vertex_codes[triangle[corner]];

			if (code & CODE_UNKNOWN)
			{
				float point[3];

				to_camera(&stream->base[triangle[corner]].x, point);
				code = outside(point);
				vertex_codes[triangle[corner]] = (unsigned char)code;
			}
			bits[corner] = code;
		}
		if (!(bits[0] | bits[1] | bits[2]))
		{
			/* safe: kept as it is (copied only once something was dropped) */
			if (kept)
			{
				kept[3 * kept_count] = triangle[0];
				kept[3 * kept_count + 1] = triangle[1];
				kept[3 * kept_count + 2] = triangle[2];
			}
			kept_count++;
			continue;
		}
		if (!kept)
		{
			/* the first triangle left out: from here the kept ones are copied
			into the display list's memory */
			kept = (unsigned short *)list_take((int)(triangle_count * 3 * sizeof(unsigned short) + 3) & ~3);
			if (!kept)
				return;
			memcpy(kept, indices, kept_count * 3 * sizeof(unsigned short));
			draw = kept;
		}
		if (bits[0] & bits[1] & bits[2])
			culled_this_frame++;
		else
		{
			float corners[3][CORNER_FLOATS] = { { 0.0f } };

			for (corner = 0; corner < 3; corner++)
			{
				const struct ge_vertex *b = &stream->base[triangle[corner]];

				to_camera_corner(&b->x, corners[corner]);
				corners[corner][3] = b->u;
				corners[corner][4] = b->v;
				corners[corner][5] = stream->lightmap ? stream->lightmap[triangle[corner]].u : 0.0f;
				corners[corner][6] = stream->lightmap ? stream->lightmap[triangle[corner]].v : 0.0f;
			}
			{
				/* build 47: the cutting timed on its own (taken out of the
				classification's time below) */
				unsigned int cut_start = sceKernelGetSystemTimeLow();

				cut_count += clip_triangle(corners, bits[0] | bits[1] | bits[2], cut_corners + cut_count,
					MAXIMUM_CUT_CORNERS - cut_count);
				cut_us += sceKernelGetSystemTimeLow() - cut_start;
			}
		}
	}
	ge_part_us[2] += cut_us;
	ge_part_calls[2]++;
	part_add(1);
	ge_part_us[1] -= cut_us;
	if (cut_count > clip_peak)
		clip_peak = cut_count;
	if (kept_count + cut_count == 0)
		return;
	/* build 40: nothing left out, so the GE reads the game's own triangle
	list, which the game wrote this frame into memory the CPU caches. The GE
	draws as soon as a draw is listed, before the frame's end writes the cache
	back, so it read whatever an earlier frame left there: the level's
	triangles came and went with the view (Sheepdog01's canyon walls, the same way
	each time he turned). Out of the cache first, as the meters (build 39) */
	if (!kept)
		sceKernelDcacheWritebackRange(indices, triangle_count * 3 * sizeof(unsigned short));
	/* build 64: sorted by slant (angle_sort; the copy, if one is made, is the
	display list's own). Build 66: with the far triangles (past DETAIL_FAR)
	set apart, when the draw has detail maps and reaches that far */
	if (detail_off < 0)
		detail_off = psp_keep("nodetail");
	if (detail_far < 0)
		detail_far = !psp_keep("detailnear");
	if (!detail_off && (detail_key[0] || detail_key[1]))
	{
		float far = fog_far < DETAIL_FAR ? fog_far : DETAIL_FAR, reach = 0.0f;
		int axis;

		/* the box's farthest corner from the eye: nearer than DETAIL_FAR,
		and no triangle can be far */
		for (axis = 0; axis < 3; axis++)
		{
			float a = fabsf(stream->low[axis] - camera_eye[axis]), b = fabsf(stream->high[axis] - camera_eye[axis]);
			float m = a > b ? a : b;

			reach += m * m;
		}
		detail_far_squared = far * far;
		far_possible = reach > detail_far_squared;
	}
	{
		unsigned int angle_start = sceKernelGetSystemTimeLow();

		draw = angle_sort(stream->base, draw, kept_count, far_possible);
		angle_us += sceKernelGetSystemTimeLow() - angle_start;
	}
	/* build 66: the cut corners as GE vertices, once for the base passes and
	once for the lightmap's */
	if (cut_count > 0)
	{
		int which;

		for (which = 0; which < (stream->lightmap ? 2 : 1); which++)
		{
			struct ge_vertex *corners = list_take(cut_count * sizeof(struct ge_vertex));

			if (!corners)
				break;
			for (index = 0; index < cut_count; index++)
			{
				corners[index].u = cut_corners[index][which ? 5 : 3];
				corners[index].v = cut_corners[index][which ? 6 : 4];
				corners[index].x = cut_corners[index][7];
				corners[index].y = cut_corners[index][8];
				corners[index].z = cut_corners[index][9];
			}
			cut_vertices_prepared[which] = corners;
		}
		cut_vertices_count = cut_count;
	}

	/* build 35: the back faces culled, as the Xbox does (D3DCULL_CCW for
	every environment pass). Drawn, a hill's far side shares the 16-bit depth
	of its near side at the crest far off, so the multiplying passes (depth
	EQUAL) landed there twice: the bright dots along distant ridges. Fewer
	pixels for the GE too. Keep words: "nocull", "cullflip" */
	if (level_cull < 0)
		level_cull = psp_keep("nocull") ? 0 : psp_keep("cullflip") ? 2 : 1;
	if (level_cull)
	{
		sceGuFrontFace(level_cull == 1 ? LEVEL_FRONT : (LEVEL_FRONT == GU_CW ? GU_CCW : GU_CW));
		sceGuEnable(GU_CULL_FACE);
	}

	textured = psp_tex_bind(base_key, &base_alpha);
	/* build 79: the coordinates of the vertex nearest the eye (of every
	eighth or so of the draw's; a near one is all that's needed, the shift
	only has to be small where the surface is large on the screen) */
	uv_centre_u = uv_centre_v = 0.0f;
	if (uv_centre < 0)
		uv_centre = !psp_keep("uvwide");
	if (textured && uv_centre)
	{
		unsigned int step = (highest - lowest) / 96 + 1, i;
		float best = 1e30f;

		for (i = lowest; i <= highest; i += step)
		{
			const struct ge_vertex *v = &stream->base[i];
			float dx = v->x - camera_eye[0], dy = v->y - camera_eye[1], dz = v->z - camera_eye[2];
			float d = dx * dx + dy * dy + dz * dz;

			if (d < best)
			{
				best = d;
				uv_centre_u = v->u;
				uv_centre_v = v->v;
			}
		}
		uv_centred_draws++;
	}
	if (textured)
	{
		int base_width = 0, base_height = 0;

		if (stream->uv_per_unit <= 0.0f)
			stream->uv_per_unit = uv_density(stream->base, indices, triangle_count);
		psp_tex_size(base_key, &base_width, &base_height);
		base_slope = texture_levels_by_distance(stream->uv_per_unit, base_width, base_height, 1.0f, tex_bias);
	}
	if (textured)
	{
		/* build 53: the passes on their own, for finding which one draws
		something (keep word "passes", then select + left cycles; psp_sdl.c) */
		int white_base = psp_pass_mode == 3, skip_lightmap = psp_pass_mode == 1 || psp_pass_mode == 4,
			skip_detail = psp_pass_mode == 1 || psp_pass_mode == 2 || psp_pass_mode == 3;

		/* pass 1: the base texture, fading toward the fog */
		sceGuEnable(GU_TEXTURE_2D);
		sceGuTexFunc(GU_TFX_REPLACE, base_alpha && alpha_tested ? GU_TCC_RGBA : GU_TCC_RGB);
		sceGuTexWrap(GU_REPEAT, GU_REPEAT);
		if (uv_centre)
			uv_shift(1.0f, 1.0f);
		if (base_alpha && alpha_tested)
		{
			sceGuAlphaFunc(GU_GREATER, 0x7F, 0xFF);
			sceGuEnable(GU_ALPHA_TEST);
		}
		if (white_base)
		{
			sceGuDisable(GU_TEXTURE_2D);
			sceGuColor(0xFFFFFFFF);
		}
		draw_pass_angled(stream->base, draw, (int)kept_count * 3, cut_corners, cut_count, 0, base_slope, 0);
		if (white_base)
			sceGuEnable(GU_TEXTURE_2D);
		sceGuDisable(GU_ALPHA_TEST);
		if (uv_centre)
			sceGuTexOffset(0.0f, 0.0f);
		textured_draws++;
		/* pass 2: the lightmap multiplied in, where pass 1 drew */
		if (!skip_lightmap && stream->lightmap && psp_tex_bind(lightmap_key, NULL))
		{
			sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
			sceGuTexWrap(GU_CLAMP, GU_CLAMP);
			sceGuDepthFunc(GU_EQUAL);
			sceGuDepthMask(GU_TRUE);
			/* far off the lightmap fades to white, leaving the fog colour
			from pass 1 */
			sceGuFog(fog_near, fog_far, 0xFFFFFFFF);
			/* src x dst, once: the Xbox lightmap pass writes lightmap x
			material colour (final combiner T2 x EF, no shift) and the
			texture pass multiplies the frame by the base (DESTCOLOR, ZERO);
			only the detail maps are doubled (build 35: was 2 x, overbright) */
			sceGuBlendFunc(GU_ADD, GU_DST_COLOR, GU_FIX, 0, 0);
			sceGuEnable(GU_BLEND);
			draw_pass(stream->lightmap, draw, (int)kept_count * 3, cut_corners, cut_count, 1);
			sceGuDisable(GU_BLEND);
			sceGuFog(fog_near, fog_far, fog_color);
			sceGuDepthMask(GU_FALSE);
			sceGuDepthFunc(GU_GEQUAL);
		}
		/* passes 3 and 4: the detail maps, the same way */
		if (detail_off < 0)
			detail_off = psp_keep("nodetail");
		if (!detail_off && !skip_detail && (detail_key[0] || detail_key[1]) && !angle_near_any)
			detail_passes_skipped_far++;
		if (!detail_off && !skip_detail && (detail_key[0] || detail_key[1]) && angle_near_any)
		{
			int pass;
			float far = fog_far < DETAIL_FAR ? fog_far : DETAIL_FAR;
			float near = fog_near < DETAIL_NEAR ? fog_near : DETAIL_NEAR;

			/* build 56: the detail maps a level and a half blurrier. They
			tile tens of times across a surface, and the GE's choice of level
			(too sharp at a slant, see ge_initialize) left their fine pattern
			beating against the pixels: the canyon walls' bands (Sheepdog01's b54
			pass screenshots: present with the detail maps, gone without).
			A detail map's smaller levels fade to its neutral average, so
			blurrier only means less of a pattern that couldn't be seen.
			"detailsharp": as before; "detailbias2": two and a half */
			/* build 64: no extra blur by default (the faded levels do the
			fading); "detailbias15": the b56-b63 1.5, "detailbias2": 2.5 */
			if (detail_bias < -1.0f)
				detail_bias = psp_keep("detailbias15") ? 1.5f : psp_keep("detailbias2") ? 2.5f : 0.0f;
			if (detail_far < 0)
				detail_far = !psp_keep("detailnear");
			sceGuTexLevelMode(tex_level_mode, tex_bias + detail_bias);
			for (pass = 0; pass < 2; pass++)
			{
				int detail_width = 0, detail_height = 0;
				float detail_scale;

				if (!detail_key[pass] || !psp_tex_bind(detail_key[pass], NULL))
					continue;
				psp_tex_size(detail_key[pass], &detail_width, &detail_height);
				detail_scale = sqrtf(fabsf(detail_u[pass] * detail_v[pass]));
				/* build 64: by distance again (b59-b63: the GE's own choice,
				which on the PSP went too blurry at a slant: the bunker floor
				and the grass lost their detail, Sheepdog01's b63 run A against B),
				with the slant added back per triangle (the angle buckets
				below). "detailauto": the GE's own choice */
				if (detail_slope < 0)
					detail_slope = !psp_keep("detailauto");
				detail_pass_slope = 0.0f;
				if (detail_slope)
					detail_pass_slope = texture_levels_by_distance(stream->uv_per_unit, detail_width, detail_height,
						detail_scale, tex_bias + detail_bias);
				sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
				sceGuTexWrap(GU_REPEAT, GU_REPEAT);
				sceGuTexScale(detail_u[pass], detail_v[pass]);
				if (uv_centre)
					uv_shift(detail_u[pass], detail_v[pass]);
				sceGuDepthFunc(GU_EQUAL);
				sceGuDepthMask(GU_TRUE);
				sceGuFog(near, far > near ? far : near + 1.0f, 0xFF808080);
				sceGuBlendFunc(GU_ADD, GU_DST_COLOR, GU_SRC_COLOR, 0, 0);
				sceGuEnable(GU_BLEND);
				draw_pass_angled(stream->base, draw, (int)kept_count * 3, cut_corners, cut_count, 0, detail_pass_slope, 1);
				detail_passes++;
			}
			sceGuDisable(GU_BLEND);
			sceGuTexLevelMode(tex_level_mode, tex_bias);
			sceGuTexScale(1.0f, 1.0f);
			sceGuTexOffset(0.0f, 0.0f);
			sceGuFog(fog_near, fog_far, fog_color);
			sceGuDepthMask(GU_FALSE);
			sceGuDepthFunc(GU_GEQUAL);
		}
		sceGuDisable(GU_TEXTURE_2D);
		sceGuTexLevelMode(tex_level_mode, tex_bias);
	}
	else
	{
		/* not baked, or still on its way from the Memory Stick: flat */
		sceGuColor(color);
		draw_pass(stream->base, draw, (int)kept_count * 3, cut_corners, cut_count, 0);
		flat_draws++;
	}
	sceGuDisable(GU_CULL_FACE);
	draws_this_frame++;
	triangles_this_frame += kept_count;
	part_add(3);
}

void psp_ge_draw_level(const void *vertices, const void *lightmap_vertices, int vertex_count,
	const unsigned short *indices, long triangle_count, unsigned int color,
	unsigned int base_key, unsigned int lightmap_key, int alpha_tested)
{
	unsigned int start = sceKernelGetSystemTimeLow();

	psp_ge_draw_level_body(vertices, lightmap_vertices, vertex_count, indices, triangle_count, color, base_key, lightmap_key, alpha_tested);
	ge_entry_us[0] += sceKernelGetSystemTimeLow() - start;
}

/* ---------- objects: a model's parts, each drawn with one node's matrix

A model's vertices (model_vertex_compressed: position, three packed vectors,
16-bit texture coordinates, two node indices and a weight) are only read by
the renderer, so the first time a part is drawn its vertices are rewritten
in place for the GE (build 20): texture coordinate (2 floats), the nodes and
weight where the GE expects a normal (unused, lighting is off), then the
position. Every part is still drawn rigidly with the node of its first
vertex: right for vehicles, scenery and weapons, approximate for anything
that bends */

static const float *model_nodes = NULL;
static int model_node_count = 0;
static float model_u_scale = 1.0f, model_v_scale = 1.0f;
#define CONVERTED_BUFFERS 2048
static const void *converted[CONVERTED_BUFFERS];
/* build 68: the geometry cache's generation of the block when it was
converted; the block can hold other vertices after an eviction */
static unsigned int converted_generation[CONVERTED_BUFFERS];
static unsigned long converted_count = 0, reconverted_count = 0;
static unsigned long multi_node_rigid = 0, transparent_placed = 0;

/* the model's lighting, from the game (build 22): an ambient colour and up
to two distant lights, each a colour and the direction its light travels */
static unsigned int model_ambient = MODEL_LIGHT;
static int model_light_count = 0;
static unsigned int model_light_color[2];
static ScePspFVector3 model_light_toward[2];

static unsigned int color_from_rgb(const float *rgb)
{
	int r = (int)(rgb[0] * 255.0f), g = (int)(rgb[1] * 255.0f), b = (int)(rgb[2] * 255.0f);

	r = r < 0 ? 0 : r > 255 ? 255 : r;
	g = g < 0 ? 0 : g > 255 ? 255 : g;
	b = b < 0 ? 0 : b > 255 ? 255 : b;
	return 0xFF000000 | (b << 16) | (g << 8) | r;
}

void psp_ge_model_begin(const float *node_matrices, int node_matrix_count, float u_scale, float v_scale,
	const float *ambient, int light_count, const float *light0, const float *light1)
{
	int index;

	model_nodes = node_matrices;
	model_node_count = node_matrix_count;
	model_u_scale = u_scale != 0.0f ? u_scale : 1.0f;
	model_v_scale = v_scale != 0.0f ? v_scale : 1.0f;
	model_ambient = ambient ? color_from_rgb(ambient) : MODEL_LIGHT;
	model_light_count = light_count < 0 ? 0 : light_count > 2 ? 2 : light_count;
	for (index = 0; index < model_light_count; index++)
	{
		const float *light = index ? light1 : light0;

		model_light_color[index] = color_from_rgb(light);
		/* the GE wants the direction toward the light */
		model_light_toward[index].x = -light[3];
		model_light_toward[index].y = -light[4];
		model_light_toward[index].z = -light[5];
	}
}

/* build 38: whether the next model is the first-person weapon (its own
projection and depth slice), and whether its bounding sphere comes within
reach of the near plane: the GE draws a triangle that reaches behind the eye
as a wedge across the screen (Sheepdog01's b37 shot of a rock in the cave), so
those models' triangles are checked one by one (psp_ge_draw_model) */
static void use_projection(int first_person)
{
	sceGumMatrixMode(GU_PROJECTION);
	perspective(projection_fov * (180.0f / 3.14159265f),
		first_person ? first_person_near : projection_near, first_person ? first_person_far : projection_far);
	if (first_person)
		sceGuDepthRange(65535, FIRST_PERSON_DEPTH_FAR);
	else
		sceGuDepthRange(WORLD_DEPTH_NEAR, 0);
	sceGumMatrixMode(GU_MODEL);
}

void psp_ge_model_shape(int first_person, const float *centroid, float radius, float near_fp, float far_fp)
{
	if (!frame_open)
		return;
	if (near_fp > 0.001f)
		first_person_near = near_fp;
	if (far_fp > first_person_near)
		first_person_far = far_fp;
	first_person = first_person != 0;
	if (first_person != model_first_person)
	{
		use_projection(first_person);
		model_first_person = first_person;
	}
	model_near = 0;
	if (!first_person && centroid && radius > 0.0f)
	{
		float c[3];

		to_camera(centroid, c);
		/* build 42: also near enough that a corner could land past the GE's
		reach to the side (a point at depth d inside the model's sphere is at
		most its radius out; past about 7 d it is off the GE's 2048 pixels) */
		/* build 56: the exact bound instead of build 42's rule of thumb. The
		sphere's nearest depth is c[2] - radius and its widest reach to a
		side |c| + radius; if that reach can pass the guard band at that
		depth, some corner may land past the GE's 2048 pixels and wrap into
		a spike (Sheepdog01, b54: a warthog beside him, its wheel arm's triangle
		swept across the screen). Then each triangle is checked, as for
		models at the eye */
		/* build 64: the nearest model to the eye, for nearauto's next frame */
		if (!in_sky)
		{
			float reach = sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) - radius;

			/* build 84: a sphere the eye is inside doesn't count (see
			near_plane_auto) */
			if (reach >= 0.0f && reach < near_object_distance)
				near_object_distance = reach;
		}
		{
			float nearest = c[2] - radius;

			model_near = c[2] + radius > 0.0f &&
				(nearest < projection_near * 2.0f + 0.02f ||
				fabsf(c[0]) + radius > guard_x * nearest || fabsf(c[1]) + radius > guard_y * nearest);
		}
		if (model_near)
			near_models++;
	}
}

/* active camouflage (build 30): the Xbox draws a camouflaged model as a
shimmer that bends what is behind it; the GE draws it faint, see-through by
the camouflage's amount, without writing depth */
static float model_camouflage = 0.0f;
static unsigned long two_sided_draws = 0;
static int model_cull = -1;
int psp_keep(const char *name);

void psp_ge_model_effect(int type, float intensity)
{
	model_camouflage = type == 1 && intensity > 0.0f ? (intensity > 1.0f ? 1.0f : intensity) : 0.0f;
}

static void model_lighting(int on)
{
	int index;

	if (!on || (!model_light_count && model_ambient == 0xFF000000))
	{
		sceGuDisable(GU_LIGHTING);
		return;
	}
	sceGuEnable(GU_LIGHTING);
	sceGuAmbient(model_ambient);
	for (index = 0; index < 2; index++)
	{
		if (index < model_light_count)
		{
			sceGuLight(index, GU_DIRECTIONAL, GU_DIFFUSE, &model_light_toward[index]);
			sceGuLightColor(index, GU_DIFFUSE, model_light_color[index]);
			sceGuEnable(GU_LIGHT0 + index);
		}
		else
			sceGuDisable(GU_LIGHT0 + index);
	}
}

/* Halo's packed normal: 11, 11 and 10 bits, signed (rasterizer_geometry.c's
uncompress_int32_to_real_vector3d) */
static void unpack_normal(unsigned int packed, float *normal)
{
	float value = (float)(int)(packed << 21);

	normal[0] = (value * (1.0f / 1048576.0f) + 1.0f) * (1.0f / 2047.0f);
	packed >>= 11;
	value = (float)(int)(packed << 21);
	normal[1] = (value * (1.0f / 1048576.0f) + 1.0f) * (1.0f / 2047.0f);
	packed >>= 11;
	value = (float)(int)(packed << 22);
	normal[2] = (value * (1.0f / 2097152.0f) + 1.0f) * (1.0f / 1023.0f);
}

/* the reverse, for uncompressed vertices, so both kinds share one path */
static unsigned int pack_normal(const float *n)
{
	int i = (int)(n[0] * 1023.0f) & 0x7FF, j = (int)(n[1] * 1023.0f) & 0x7FF, k = (int)(n[2] * 511.0f) & 0x3FF;

	return (unsigned int)i | ((unsigned int)j << 11) | ((unsigned int)k << 22);
}

/* ---------- skinning (build 23)

A Halo model part may hang its vertices on several nodes (a cyborg's body
on 16, the first-person arms on 36), each vertex on one node or blended
between two. The GE skins with up to 8 bone matrices a draw, each vertex
weighting them, so a part is split into batches of triangles that touch at
most 8 nodes. The vertices aren't copied per batch: every node of the part
gets one of 8 slots, with any two nodes that share a triangle on different
slots (a colouring of the skeleton's neighbours, which needs few), and each
vertex's weights are written once, by slot, in place of its Xbox data. A
batch then is any set of triangles whose nodes sit on distinct slots, with
those nodes' matrices loaded into the slots.

Record: the weights (one byte each, 128 = all), the texture coordinate, an
8-bit normal, the position; 28 or 32 bytes, within the Xbox's 32. Parts on
one node keep the 24-byte rigid record. */

#define SKIN_SLOTS 8
#define SKIN_NODES 128

static void halo_matrix(const float *node, ScePspFMatrix4 *model);
static void node_matrix(const void *vertices, const unsigned short *indices, ScePspFMatrix4 *model);

struct skin_batch
{
	unsigned char node[SKIN_SLOTS];
	unsigned short *indices;
	int index_count;
};

struct skin_part
{
	int weights, stride, batch_count;
	struct skin_batch *batches;
};

static struct skin_part *skinned[CONVERTED_BUFFERS];
/* each node's slot in the part being planned */
static signed char skin_slot[SKIN_NODES];
static unsigned long skinned_parts = 0, skin_batches = 0, skin_failures = 0, skin_bytes = 0;

/* a vertex's nodes and the first's weight, from either kind of vertex */
static void vertex_nodes(const void *vertices, int index, int uncompressed, int *n0, int *n1, float *w0)
{
	if (uncompressed)
	{
		const unsigned char *source = (const unsigned char *)vertices + 68 * index;
		short node_indices[2];
		float weights[2];

		memcpy(node_indices, source + 56, 4);
		memcpy(weights, source + 60, 8);
		*n0 = node_indices[0];
		*n1 = node_indices[1];
		*w0 = weights[0];
	}
	else
	{
		const unsigned char *source = (const unsigned char *)vertices + 32 * index;
		short weight;

		memcpy(&weight, source + 30, 2);
		*n0 = source[28] / 3;
		*n1 = source[29] / 3;
		*w0 = weight * (1.0f / 32767.0f);
	}
	if (*n0 < 0 || *n0 >= SKIN_NODES)
		*n0 = 0;
	if (*n1 < 0 || *n1 >= SKIN_NODES || *n1 == *n0 || !(*w0 < 0.995f))
	{
		*n1 = -1;
		*w0 = 1.0f;
	}
}

/* the part's triangles as a list (strips unrolled, keeping each triangle's
winding; strips' joining triangles dropped) into a new array */
static unsigned short *triangle_list(const unsigned short *indices, int index_count, int strip, int *triangle_count)
{
	unsigned short *list = malloc((strip ? (index_count - 2) : index_count / 3) * 3 * sizeof(unsigned short) + 4);
	int count = 0, i;

	if (!list)
		return NULL;
	if (strip)
	{
		for (i = 0; i + 2 < index_count; i++)
		{
			unsigned short a = indices[i], b = indices[i + 1], c = indices[i + 2];

			if (a == b || b == c || a == c)
				continue;
			if (i & 1) { unsigned short t = a; a = b; b = t; }
			list[3 * count] = a; list[3 * count + 1] = b; list[3 * count + 2] = c;
			count++;
		}
	}
	else
	{
		memcpy(list, indices, (index_count / 3) * 3 * sizeof(unsigned short));
		count = index_count / 3;
	}
	*triangle_count = count;
	return list;
}

/* works out the slots and batches; NULL leaves the part rigid */
static struct skin_part *skin_plan(const void *vertices, int count, int uncompressed, const unsigned short *indices,
	int index_count, int strip, signed char *node0, signed char *node1, unsigned char *weight0)
{
	static unsigned char conflict[SKIN_NODES][SKIN_NODES / 8];
	signed char *slot_of = skin_slot;
	static int degree[SKIN_NODES], slot_use[SKIN_SLOTS];
	unsigned short *list;
	int triangle_count, used[SKIN_NODES] = { 0 }, node_count = 0, i, t, slots = 0;
	struct skin_part *part;
	struct skin_batch batches[64];
	int batch_count = 0, batch_triangles[64];
	unsigned char *batch_of;

	for (i = 0; i < count; i++)
	{
		int n0, n1;
		float w0;

		vertex_nodes(vertices, i, uncompressed, &n0, &n1, &w0);
		node0[i] = (signed char)n0;
		node1[i] = (signed char)n1;
		weight0[i] = (unsigned char)(w0 * 128.0f + 0.5f);
		if (!used[n0]++) node_count++;
		if (n1 >= 0 && !used[n1]++) node_count++;
	}
	if (node_count <= 1 || !indices || index_count < 3)
		return NULL;
	list = triangle_list(indices, index_count, strip, &triangle_count);
	if (!list || !triangle_count)
	{
		free(list);
		return NULL;
	}
	/* the nodes that share a triangle */
	memset(conflict, 0, sizeof(conflict));
	memset(degree, 0, sizeof(degree));
	for (t = 0; t < triangle_count; t++)
	{
		int nodes[6], n = 0, a, b;

		for (a = 0; a < 3; a++)
		{
			int v = list[3 * t + a];

			if (v >= count)
			{
				free(list);
				return NULL;
			}
			nodes[n++] = node0[v];
			if (node1[v] >= 0)
				nodes[n++] = node1[v];
		}
		for (a = 0; a < n; a++)
			for (b = 0; b < n; b++)
				if (nodes[a] != nodes[b] && !(conflict[nodes[a]][nodes[b] >> 3] & (1 << (nodes[b] & 7))))
				{
					conflict[nodes[a]][nodes[b] >> 3] |= 1 << (nodes[b] & 7);
					degree[nodes[a]]++;
				}
	}
	/* slots: the most connected nodes first, each on the least used slot its
	neighbours leave free */
	memset(slot_of, -1, SKIN_NODES);
	memset(slot_use, 0, sizeof(slot_use));
	for (i = 0; i < node_count; i++)
	{
		int best = -1, node, other, slot;

		for (node = 0; node < SKIN_NODES; node++)
			if (used[node] && slot_of[node] < 0 && (best < 0 || degree[node] > degree[best]))
				best = node;
		{
			int taken = 0, choice = -1;

			for (other = 0; other < SKIN_NODES; other++)
				if (slot_of[other] >= 0 && (conflict[best][other >> 3] & (1 << (other & 7))))
					taken |= 1 << slot_of[other];
			for (slot = 0; slot < SKIN_SLOTS; slot++)
				if (!(taken & (1 << slot)) && (choice < 0 || slot_use[slot] < slot_use[choice]))
					choice = slot;
			if (choice < 0)
			{
				/* more than 8 nodes share triangles with one another */
				free(list);
				skin_failures++;
				return NULL;
			}
			slot_of[best] = (signed char)choice;
			slot_use[choice]++;
			if (choice + 1 > slots)
				slots = choice + 1;
		}
	}
	/* batches: each triangle into the first whose slots agree with its nodes */
	batch_of = malloc(triangle_count);
	if (!batch_of)
	{
		free(list);
		return NULL;
	}
	for (t = 0; t < triangle_count; t++)
	{
		int nodes[6], n = 0, a, b;

		for (a = 0; a < 3; a++)
		{
			int v = list[3 * t + a];

			nodes[n++] = node0[v];
			if (node1[v] >= 0)
				nodes[n++] = node1[v];
		}
		for (b = 0; b < batch_count; b++)
		{
			for (a = 0; a < n; a++)
			{
				unsigned char held = batches[b].node[(int)slot_of[nodes[a]]];

				if (held != 0xFF && held != nodes[a])
					break;
			}
			if (a == n)
				break;
		}
		if (b == batch_count)
		{
			if (batch_count == 64)
			{
				free(batch_of);
				free(list);
				skin_failures++;
				return NULL;
			}
			memset(batches[b].node, 0xFF, SKIN_SLOTS);
			batch_triangles[b] = 0;
			batch_count++;
		}
		for (a = 0; a < n; a++)
			batches[b].node[(int)slot_of[nodes[a]]] = (unsigned char)nodes[a];
		batch_of[t] = (unsigned char)b;
		batch_triangles[b]++;
	}
	/* one allocation: the part, its batches, their indices */
	{
		int bytes = sizeof(struct skin_part) + batch_count * sizeof(struct skin_batch) + triangle_count * 3 * 2;
		unsigned short *out;
		int b;

		part = malloc(bytes);
		if (!part)
		{
			free(batch_of);
			free(list);
			return NULL;
		}
		skin_bytes += bytes;
		part->weights = slots;
		part->stride = (slots <= 4 ? 4 : 8) + 8 + 4 + 12;
		part->batch_count = batch_count;
		part->batches = (struct skin_batch *)(part + 1);
		out = (unsigned short *)(part->batches + batch_count);
		for (b = 0; b < batch_count; b++)
		{
			part->batches[b] = batches[b];
			part->batches[b].indices = out;
			part->batches[b].index_count = 0;
			for (t = 0; t < triangle_count; t++)
				if (batch_of[t] == b)
				{
					out[0] = list[3 * t]; out[1] = list[3 * t + 1]; out[2] = list[3 * t + 2];
					out += 3;
					part->batches[b].index_count += 3;
				}
			sceKernelDcacheWritebackRange(part->batches[b].indices, part->batches[b].index_count * 2);
		}
	}
	free(batch_of);
	free(list);
	skinned_parts++;
	skin_batches += batch_count;
	return part;
}

/* rewrites a model vertex buffer for the GE, once; 0 if there's no room to
remember it. Parts on several nodes are planned for skinning when their
triangles are given (see-through parts aren't: they are queued with one
matrix) */
static int model_convert(void *vertices, int count, int uncompressed, const unsigned short *indices, int index_count,
	int strip, struct skin_part **skin)
{
	unsigned int slot = ((unsigned int)vertices >> 4) % CONVERTED_BUFFERS, tries = 0;
	int index;
	struct skin_part *part = NULL;
	signed char *node0 = NULL, *node1 = NULL;
	unsigned char *weight0 = NULL;

	if (skin)
		*skin = NULL;
	while (converted[slot] && converted[slot] != vertices)
	{
		slot = (slot + 1) % CONVERTED_BUFFERS;
		if (++tries >= CONVERTED_BUFFERS)
			return 0;
	}
	if (converted[slot])
	{
		unsigned int generation = psp_geo_generation(vertices);

		if (generation == converted_generation[slot])
		{
			if (skin)
				*skin = skinned[slot];
			return 1;
		}
		/* the cache has read other vertices (or these again) into the block
		since: converted afresh, the old plan let go */
		free(skinned[slot]);
		skinned[slot] = NULL;
		converted_generation[slot] = generation;
		reconverted_count++;
	}
	else
		converted_generation[slot] = psp_geo_generation(vertices);
	if (skin && indices)
	{
		node0 = malloc(count * 3);
		if (node0)
		{
			node1 = node0 + count;
			weight0 = (unsigned char *)(node1 + count);
			part = skin_plan(vertices, count, uncompressed, indices, index_count, strip, node0, node1, weight0);
		}
	}
	converted[slot] = vertices;
	skinned[slot] = part;
	if (!part)
	{
		/* build 42: a buffer whose vertices hang from several nodes but is
		drawn on one (as see-through parts are, or when its plan failed):
		its far vertices are drawn where the first node puts them */
		int first = -1, nodes = 0, why = !skin ? 1 : !indices ? 2 : 3;

		for (index = 0; index < count; index++)
		{
			int n0, n1;
			float w0;

			vertex_nodes(vertices, index, uncompressed, &n0, &n1, &w0);
			if (first < 0)
				first = n0;
			if (n0 != first || n1 >= 0)
			{
				nodes = 1;
				break;
			}
		}
		if (nodes)
		{
			multi_node_rigid++;
			if (multi_node_rigid <= 12)
				platform_log("GE: a buffer of %d vertices on several nodes converted rigid (%s)", count,
					why == 1 ? "see-through" : why == 2 ? "no triangles given" : "its skinning plan failed");
		}
	}
	if (skin)
		*skin = part;
	converted_count++;
	for (index = 0; index < count; index++)
	{
		unsigned char *vertex = (unsigned char *)vertices + 32 * index;
		float position[3], uv[2];
		unsigned int nodes, packed_normal = 0;

		if (uncompressed)
		{
			/* 68-byte vertices (the sky, and models too big for 16 bits): packed
			down into the same records, in place; record i ends before vertex i
			starts, and vertices before it are already read */
			const unsigned char *source = (const unsigned char *)vertices + 68 * index;
			short node_indices[2];

			memcpy(position, source, 12);
			memcpy(uv, source + 48, 8);
			memcpy(node_indices, source + 56, 4);
			{
				float n[3];

				memcpy(n, source + 12, 12);
				packed_normal = pack_normal(n);
			}
			nodes = (unsigned int)(node_indices[0] >= 0 && node_indices[0] < 85 ? node_indices[0] * 3 : 0);
		}
		else
		{
			short texcoord[2];

			memcpy(position, vertex, 12);
			memcpy(&packed_normal, vertex + 12, 4);
			memcpy(texcoord, vertex + 24, 4);
			memcpy(&nodes, vertex + 28, 4);
			uv[0] = texcoord[0] * (1.0f / 32767.0f);
			uv[1] = texcoord[1] * (1.0f / 32767.0f);
		}
		{
			float normal[3];
			int k;

			unpack_normal(packed_normal, normal);
			if (part)
			{
				/* skinned: weights by slot, then as the rigid record */
				unsigned char *record = (unsigned char *)vertices + part->stride * index;
				int uv_at = part->stride - 24;

				memset(record, 0, uv_at);
				if (node1[index] >= 0)
				{
					record[(int)skin_slot[(int)node0[index]]] = weight0[index];
					record[(int)skin_slot[(int)node1[index]]] = (unsigned char)(128 - weight0[index]);
				}
				else
					record[(int)skin_slot[(int)node0[index]]] = 128;
				memcpy(record + uv_at, uv, 8);
				for (k = 0; k < 3; k++)
					record[uv_at + 8 + k] = (unsigned char)(signed char)(normal[k] * 127.0f);
				record[uv_at + 11] = 0;
				memcpy(record + uv_at + 12, position, 12);
			}
			else
			{
				/* 24 bytes: texture coordinate, an 8-bit normal and the first
				node (in the padding byte the GE skips), position (build 22) */
				unsigned char *record = (unsigned char *)vertices + 24 * index;

				memcpy(record, uv, 8);
				for (k = 0; k < 3; k++)
					record[8 + k] = (unsigned char)(signed char)(normal[k] * 127.0f);
				record[11] = (unsigned char)(nodes & 0xFF);
				memcpy(record + 12, position, 12);
			}
		}
	}
	free(node0);
	sceKernelDcacheWritebackRange(vertices, count * (part ? part->stride : 24));
	return 1;
}

/* the change colour (build 31): on the Xbox the lit base map is multiplied
by white blended toward the object's change colour by the multipurpose
map's alpha. Here that is a second pass over the same triangles: the
multipurpose map with a palette worked out per entry (white toward the
colour by the entry's alpha), multiplied into what the first pass drew */
int psp_tex_palette(unsigned int key, const unsigned int **palette, int *count);
int psp_tex_palette16(unsigned int key, const unsigned short **palette, int *count);
static unsigned int change_key = 0, change_color = 0xFFFFFFFF, glow_color = 0;
static unsigned long change_passes = 0, glow_passes = 0;
static int in_sky;

struct skin_part;
static void model_geometry(void *vertices, const unsigned short *indices, long index_count, int strip,
	struct skin_part *skin, ScePspFMatrix4 *model);

/* build 60: a rigid part's triangles cut at the near plane or the guard
band, as GE vertices in the part's space (drawn by model_geometry after the
part's own list, in every pass) */
#define MAXIMUM_MODEL_CUT_CORNERS 192
static float model_cut[MAXIMUM_MODEL_CUT_CORNERS][CORNER_FLOATS];
static int model_cut_count = 0;
static void *model_cut_vertices = NULL;
static int model_drop = -1;

static unsigned int abgr_from_rgb(float const *rgb)
{
	int r = (int)(rgb[0] * 255.0f + 0.5f), g = (int)(rgb[1] * 255.0f + 0.5f), b = (int)(rgb[2] * 255.0f + 0.5f);

	return 0xFF000000 | (PSP_CLAMP_255(b) << 16) | (PSP_CLAMP_255(g) << 8) | PSP_CLAMP_255(r);
}

/* the model part's multipurpose map and the two colours it masks: the change
colour (by its blue) and the self-illumination (by its green; build 31) */
void psp_ge_model_multipurpose(unsigned int multipurpose_key, float const *change, float const *glow)
{
	change_key = multipurpose_key;
	change_color = abgr_from_rgb(change);
	glow_color = abgr_from_rgb(glow) & 0xFFFFFF;
}

/* a pass over the part's triangles with the multipurpose map, its palette
worked out per entry from one channel: 0 the change colour (white toward the
colour by the blue, multiplied in), 1 the self-illumination (the colour by
the green, added) */
/* build 66: the worked-out palettes kept, by map, colour and pass. A part's
palette was worked out again every frame (256 entries, three divisions
each): the cyborg's and the vehicles', two passes each. A slot is reused
only when nothing this frame still needs it (the GE reads a palette when the
list runs), as the meters' (METER_CACHE) */
#define MULTIPURPOSE_CACHE 6
static unsigned int multipurpose_cache_palette[MULTIPURPOSE_CACHE][256] __attribute__((aligned(64)));
static struct
{
	unsigned int key, color;
	unsigned long frame;
	int which, count;
} multipurpose_cache[MULTIPURPOSE_CACHE];
static unsigned long multipurpose_worked = 0, multipurpose_reused = 0;

static int multipurpose_pass(int which, void *vertices, const unsigned short *indices, long index_count, int strip,
	struct skin_part *skin, ScePspFMatrix4 *model)
{
	const unsigned int *palette = NULL;
	const unsigned short *palette16 = NULL;
	unsigned int *computed = NULL, color = which ? glow_color : change_color;
	int count = 0, entry, slot, free_slot = -1, hit = 0;

	/* the Xbox's channel order, read off the Xbox disc's own Blood Gulch
	(the cyborg's map: the armour plates solid in blue, alpha all white): red
	the reflection mask, green self-illumination, blue the change colour,
	alpha the detail mask. Gearbox's PC release reordered them */
	if (!psp_tex_bind(change_key, NULL) ||
		!(psp_tex_palette(change_key, &palette, &count) || psp_tex_palette16(change_key, &palette16, &count)))
		return 0;
	for (slot = 0; slot < MULTIPURPOSE_CACHE; slot++)
	{
		if (multipurpose_cache[slot].key == change_key && multipurpose_cache[slot].color == color &&
			multipurpose_cache[slot].which == which && multipurpose_cache[slot].count == count)
		{
			computed = multipurpose_cache_palette[slot];
			multipurpose_cache[slot].frame = frames + 1;
			multipurpose_reused++;
			hit = 1;
			break;
		}
		if (multipurpose_cache[slot].frame != frames + 1 &&
			(free_slot < 0 || multipurpose_cache[slot].frame < multipurpose_cache[free_slot].frame))
			free_slot = slot;
	}
	if (!computed)
	{
		if (free_slot >= 0)
		{
			computed = multipurpose_cache_palette[free_slot];
			multipurpose_cache[free_slot].key = change_key;
			multipurpose_cache[free_slot].color = color;
			multipurpose_cache[free_slot].which = which;
			multipurpose_cache[free_slot].count = count;
			multipurpose_cache[free_slot].frame = frames + 1;
		}
		else
		{
			computed = list_take(count * 4 + 16);
			if (!computed)
				return 0;
			computed = (unsigned int *)(((unsigned int)computed + 15) & ~15u);
		}
		multipurpose_worked++;
	}
	for (entry = hit ? count : 0; entry < count; entry++)
	{
		unsigned int mask, out = 0xFF000000;
		int channel;

		if (which)
			mask = palette ? (palette[entry] >> 8) & 0xFF : ((palette16[entry] >> 5) & 0x3F) * 255 / 63;
		else
			mask = palette ? (palette[entry] >> 16) & 0xFF : (palette16[entry] >> 11) * 255 / 31;
		for (channel = 0; channel < 3; channel++)
		{
			unsigned int c = (color >> (8 * channel)) & 0xFF;

			out |= (which ? mask * c / 255 : 255 - mask * (255 - c) / 255) << (8 * channel);
		}
		computed[entry] = out;
	}
	if (!hit)
		sceKernelDcacheWritebackRange(computed, count * 4);
	sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
	sceGuClutLoad(count / 8 > 0 ? count / 8 : 1, computed);
	sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
	sceGuDisable(GU_ALPHA_TEST);
	sceGuDepthFunc(GU_EQUAL);
	sceGuDepthMask(GU_TRUE);
	if (which)
	{
		/* added light fades to nothing in the fog */
		sceGuFog(fog_near, fog_far, 0xFF000000);
		sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF);
	}
	else
	{
		/* fogged toward white: far off, nothing changes */
		sceGuFog(fog_near, fog_far, 0xFFFFFFFF);
		sceGuBlendFunc(GU_ADD, GU_DST_COLOR, GU_FIX, 0, 0);
	}
	sceGuEnable(GU_BLEND);
	model_geometry(vertices, indices, index_count, strip, skin, model);
	sceGuDisable(GU_BLEND);
	sceGuFog(fog_near, fog_far, fog_color);
	sceGuDepthMask(GU_FALSE);
	sceGuDepthFunc(GU_GEQUAL);
	return 1;
}

static void model_geometry(void *vertices, const unsigned short *indices, long index_count, int strip,
	struct skin_part *skin, ScePspFMatrix4 *model)
{
	if (skin)
	{
		int batch, slot;

		for (batch = 0; batch < skin->batch_count && !list_full(); batch++)
		{
			struct skin_batch const *b = &skin->batches[batch];
			const unsigned short *draw = b->indices;
			int draw_count = b->index_count;
			/* each slot's matrix as the depth it gives a point: its rows
			dotted with the camera's forward direction (build 41) */
			float depth_row[SKIN_SLOTS][3][4];

			for (slot = 0; slot < skin->weights; slot++)
			{
				int node = b->node[slot];

				memset(depth_row[slot], 0, sizeof(depth_row[slot]));
				if (node != 0xFF)
				{
					int axis;

					halo_matrix(model_nodes + 13 * (node < model_node_count ? node : 0), model);
					sceGuBoneMatrix(slot, model);
					/* the matrix's rows along the camera's right, up and forward */
					for (axis = 0; axis < 3; axis++)
					{
						const float *d = axis == 0 ? camera_right : axis == 1 ? camera_up : camera_forward;

						depth_row[slot][axis][0] = model->x.x * d[0] + model->x.y * d[1] + model->x.z * d[2];
						depth_row[slot][axis][1] = model->y.x * d[0] + model->y.y * d[1] + model->y.z * d[2];
						depth_row[slot][axis][2] = model->z.x * d[0] + model->z.y * d[1] + model->z.z * d[2];
						depth_row[slot][axis][3] = model->w.x * d[0] + model->w.y * d[1] + model->w.z * d[2];
					}
				}
			}
			if (model_near && !model_first_person)
			{
				/* build 41: a bending model at the eye (the warthog round the
				camera behind it: its roll bars, turret, axles) drops the
				triangles not wholly in front of the near plane, as rigid ones
				have since build 38. A corner behind the eye, given to the GE,
				lands on the far side of the screen: the long spikes Sheepdog01 saw.
				The CPU works out each corner's depth from its weights */
				unsigned short *kept = list_take((draw_count * 2 + 3) & ~3);
				float limit = projection_near * 1.25f;
				int t, k, out = 0, uv_at = skin->stride - 24;

				if (!kept)
					continue;
				/* each vertex's verdict once a batch (build 43: worked out per
				corner, it cost up to 6 ms a frame on the PSP in build 42) */
				corner_verdict_reset();
				for (t = 0; t + 2 < draw_count; t += 3)
				{
					int behind = 0;

					for (k = 0; k < 3 && !behind; k++)
					{
						const unsigned char *record = (const unsigned char *)vertices + skin->stride * draw[t + k];
						int known = draw[t + k] < CORNER_VERDICTS ? corner_verdict[draw[t + k]] : -1;

						if (known == corner_stamp)
							continue;
						if (known == corner_stamp + 1)
						{
							behind = 1;
							break;
						}
						const float *p = (const float *)(record + uv_at + 12);
						float at[3] = { 0.0f, 0.0f, 0.0f };
						int axis;

						for (slot = 0; slot < skin->weights; slot++)
							if (record[slot])
								for (axis = 0; axis < 3; axis++)
									at[axis] += record[slot] * (1.0f / 128.0f) * (depth_row[slot][axis][0] * p[0] +
										depth_row[slot][axis][1] * p[1] + depth_row[slot][axis][2] * p[2] +
										depth_row[slot][axis][3]);
						behind = camera_corner_out(at[0], at[1], at[2], limit);
						if (draw[t + k] < CORNER_VERDICTS)
							corner_verdict[draw[t + k]] = (unsigned short)(corner_stamp + behind);
					}
					if (behind)
					{
						near_triangles_dropped++;
						continue;
					}
					kept[out++] = draw[t];
					kept[out++] = draw[t + 1];
					kept[out++] = draw[t + 2];
				}
				draw = kept;
				draw_count = out;
				if (!draw_count)
					continue;
			}
			sceGumDrawArray(GU_TRIANGLES, GU_WEIGHTS(skin->weights) | GU_WEIGHT_8BIT | GU_TEXTURE_32BITF |
				GU_NORMAL_8BIT | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D, draw_count, draw,
				vertices);
		}
	}
	else
	{
		if (index_count > 0)
			sceGumDrawArray(strip ? GU_TRIANGLE_STRIP : GU_TRIANGLES,
				GU_TEXTURE_32BITF | GU_NORMAL_8BIT | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
				(int)index_count, indices, vertices);
		if (model_cut_count > 0 && model_cut_vertices)
			sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_NORMAL_8BIT | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
				model_cut_count, NULL, model_cut_vertices);
	}
}

static void psp_ge_draw_model_body(void *vertices, int vertex_count, int uncompressed, const unsigned short *indices,
	long index_count, int strip, unsigned int color, unsigned int base_key, float u_scale, float v_scale,
	int alpha_tested)
{
	ScePspFMatrix4 model;
	int base_alpha = 0, textured, two_sided = (alpha_tested & 2) != 0;
	struct skin_part *skin = NULL;

	alpha_tested &= 1;
	model_cut_count = 0;
	model_cut_vertices = NULL;
	if (!frame_open || index_count < 3 || !vertices || !indices || !model_nodes || model_node_count <= 0 ||
		vertex_count <= 0 || list_full() || (in_sky && magenta == 1))
		return;
	part_start();
	if (!model_convert(vertices, vertex_count, uncompressed, indices, (int)index_count, strip, &skin))
		return;
	part_add(4);
	sceGumMatrixMode(GU_MODEL);
	if (skin)
		sceGumLoadIdentity();
	else
	{
		/* one node: the first vertex's (stored times three, for the Xbox's
		vertex shader constants) */
		node_matrix(vertices, indices, &model);
		sceGumLoadMatrix(&model);
		part_add(5);
		if (model_near && !model_first_person)
		{
			/* build 38: near the eye, only the triangles wholly in front of
			the near plane (with a margin), as a list */
			unsigned short *kept = list_take((int)((strip ? index_count * 3 : index_count) * 2 + 3) & ~3);
			long triangle, triangles = strip ? index_count - 2 : index_count / 3, count = 0;
			float limit = projection_near * 1.25f;

			/* build 59: back to the world's matrix on the way out, as the end
			of this function does. Returning with this part's matrix still
			loaded moved every level draw after it by the part's placement:
			the whole level vanished while a model near the eye had all its
			triangles left out (Sheepdog01's b58 spot at 33.2 -90.2, and the flicker
			getting into the warthog) */
			if (!kept)
			{
				world_model();
				return;
			}
			/* build 60: each corner's planes as bits (corner_verdict holds
			stamp + bits); a triangle wholly in front is kept as it is, one
			wholly outside a plane is dropped, and one across the near plane or
			the guard band is cut to its inside on the CPU, as the level's are,
			and drawn from model_cut (model_geometry). Dropped whole as before
			(build 38 to 59), the triangle of a rock or wall nearest the eye
			went missing whenever a corner reached past the screen's edge
			(Sheepdog01's b59 rock shots), and a part with every triangle so placed
			disappeared. "modeldrop": as before */
			if (model_drop < 0)
				model_drop = psp_keep("modeldrop");
			corner_verdict_reset();
			for (triangle = 0; triangle < triangles; triangle++)
			{
				unsigned short corner[3];
				int k, bits[3];

				if (strip)
				{
					corner[0] = indices[triangle];
					corner[1] = indices[triangle + ((triangle & 1) ? 2 : 1)];
					corner[2] = indices[triangle + ((triangle & 1) ? 1 : 2)];
					if (corner[0] == corner[1] || corner[1] == corner[2] || corner[0] == corner[2])
						continue;
				}
				else
				{
					corner[0] = indices[3 * triangle];
					corner[1] = indices[3 * triangle + 1];
					corner[2] = indices[3 * triangle + 2];
				}
				for (k = 0; k < 3; k++)
				{
					int known = corner[k] < CORNER_VERDICTS ? corner_verdict[corner[k]] : -1;

					if (known >= corner_stamp && known < corner_stamp + CORNER_STAMP_STEP)
						bits[k] = known - corner_stamp;
					else
					{
						const float *p = (const float *)((const char *)vertices + 24 * corner[k] + 12);
						float camera_point[3];

						model_to_camera(&model, p, camera_point);
						bits[k] = model_drop ? (camera_corner_out(camera_point[0], camera_point[1], camera_point[2],
							limit) ? 1 : 0) : outside(camera_point);
						if (corner[k] < CORNER_VERDICTS)
							corner_verdict[corner[k]] = (unsigned short)(corner_stamp + bits[k]);
					}
				}
				if (!(bits[0] | bits[1] | bits[2]))
				{
					kept[3 * count] = corner[0];
					kept[3 * count + 1] = corner[1];
					kept[3 * count + 2] = corner[2];
					count++;
					continue;
				}
				near_triangles_dropped++;
				if (model_drop || (bits[0] & bits[1] & bits[2]))
					continue;
				{
					float polygon[3][CORNER_FLOATS];

					for (k = 0; k < 3; k++)
					{
						const unsigned char *record = (const unsigned char *)vertices + 24 * corner[k];
						const float *uv = (const float *)record, *p = (const float *)(record + 12);

						model_to_camera(&model, p, polygon[k]);
						polygon[k][3] = uv[0];
						polygon[k][4] = uv[1];
						polygon[k][5] = (signed char)record[8];
						polygon[k][6] = (signed char)record[9];
						polygon[k][10] = (signed char)record[10];
						polygon[k][7] = p[0];
						polygon[k][8] = p[1];
						polygon[k][9] = p[2];
					}
					model_cut_count += clip_triangle(polygon, bits[0] | bits[1] | bits[2], model_cut + model_cut_count,
						MAXIMUM_MODEL_CUT_CORNERS - model_cut_count);
				}
			}
			if (model_cut_count)
			{
				/* the cut corners as GE vertices, in the part's own space */
				unsigned char *out = list_take(model_cut_count * 24);
				int c;

				if (out)
				{
					for (c = 0; c < model_cut_count; c++)
					{
						unsigned char *record = out + 24 * c;
						float uv[2] = { model_cut[c][3], model_cut[c][4] };

						memcpy(record, uv, 8);
						record[8] = (unsigned char)(signed char)model_cut[c][5];
						record[9] = (unsigned char)(signed char)model_cut[c][6];
						record[10] = (unsigned char)(signed char)model_cut[c][10];
						record[11] = 0;
						memcpy(record + 12, &model_cut[c][7], 12);
					}
					model_cut_vertices = out;
				}
				else
					model_cut_count = 0;
			}
			if (!count && !model_cut_count)
			{
				world_model();
				return;
			}
			indices = kept;
			index_count = count * 3;
			strip = 0;
			part_add(6);
		}
	}
	textured = psp_tex_bind(base_key, &base_alpha);
	if (textured)
	{
		/* unlit for now: the texture dimmed toward the level's lighting */
		sceGuEnable(GU_TEXTURE_2D);
		sceGuTexFunc(GU_TFX_MODULATE, base_alpha && alpha_tested ? GU_TCC_RGBA : GU_TCC_RGB);
		sceGuTexWrap(GU_REPEAT, GU_REPEAT);
		sceGuTexScale(model_u_scale * (u_scale != 0.0f ? u_scale : 1.0f), model_v_scale * (v_scale != 0.0f ? v_scale : 1.0f));
		/* lit by the GE from the game's lighting; this is the material colour */
		sceGuColor(0xFFFFFFFF);
		if (base_alpha && alpha_tested)
		{
			sceGuAlphaFunc(GU_GREATER, 0x7F, 0xFF);
			sceGuEnable(GU_ALPHA_TEST);
		}
	}
	else
		sceGuColor(color);
	model_lighting(1);
	if (model_camouflage > 0.0f)
	{
		/* the model's own colour faded to a trace (15% at full camouflage) */
		int alpha = (int)((1.0f - model_camouflage * 0.85f) * 255.0f);

		sceGuColor(((unsigned int)PSP_CLAMP_255(alpha) << 24) | 0xFFFFFF);
		sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
		sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
		sceGuEnable(GU_BLEND);
		sceGuDepthMask(GU_TRUE);
	}
	/* build 36: back faces culled, as the Xbox culls them (D3DCULL_CCW). Drawn,
	a two-sided leaf's two triangles share their 16-bit depth and the lit one
	and the dark one took turns as the plant swayed (Sheepdog01, b35). A two-sided
	shader's back faces are then drawn with the light from the other side,
	as the Xbox's second pass flips the normals */
	if (model_cull < 0)
		model_cull = psp_keep("nocull") ? 0 : psp_keep("modelflip") ? 2 : 1;
	sceGuFrontFace(model_cull == 2 ? (LEVEL_FRONT == GU_CW ? GU_CCW : GU_CW) : LEVEL_FRONT);
	if (model_cull)
		sceGuEnable(GU_CULL_FACE);
	part_add(7);
	model_geometry(vertices, indices, index_count, strip, skin, &model);
	part_add(8);
	if (two_sided && model_cull && model_camouflage <= 0.0f)
	{
		int index;

		sceGuFrontFace(model_cull == 2 ? LEVEL_FRONT : (LEVEL_FRONT == GU_CW ? GU_CCW : GU_CW));
		for (index = 0; index < model_light_count; index++)
		{
			ScePspFVector3 flipped = { -model_light_toward[index].x, -model_light_toward[index].y,
				-model_light_toward[index].z };

			sceGuLight(index, GU_DIRECTIONAL, GU_DIFFUSE, &flipped);
		}
		model_geometry(vertices, indices, index_count, strip, skin, &model);
		sceGuFrontFace(LEVEL_FRONT);
		two_sided_draws++;
	}
	sceGuDisable(GU_CULL_FACE);
	model_lighting(0);
	/* passes 2 and 3: the change colour multiplied in and the
	self-illumination added, where pass 1 drew */
	if (textured && change_key && model_camouflage <= 0.0f && !in_sky)
	{
		if (change_color != 0xFFFFFFFF && multipurpose_pass(0, vertices, indices, index_count, strip, skin, &model))
			change_passes++;
		if (glow_color && multipurpose_pass(1, vertices, indices, index_count, strip, skin, &model))
			glow_passes++;
	}
	if (model_camouflage > 0.0f)
	{
		sceGuDisable(GU_BLEND);
		sceGuDepthMask(GU_FALSE);
	}
	if (textured)
	{
		sceGuDisable(GU_ALPHA_TEST);
		sceGuTexScale(1.0f, 1.0f);
		sceGuDisable(GU_TEXTURE_2D);
	}
	world_model();
	model_cut_count = 0;
	model_cut_vertices = NULL;
	model_draws_this_frame++;
	part_add(9);
}

void psp_ge_draw_model(void *vertices, int vertex_count, int uncompressed, const unsigned short *indices,
	long index_count, int strip, unsigned int color, unsigned int base_key, float u_scale, float v_scale,
	int alpha_tested)
{
	unsigned int start = sceKernelGetSystemTimeLow();

	psp_ge_draw_model_body(vertices, vertex_count, uncompressed, indices, index_count, strip, color, base_key, u_scale, v_scale, alpha_tested);
	ge_entry_us[1] += sceKernelGetSystemTimeLow() - start;
}

/* the frame to the screen: the CPU's writes (the triangle lists, the
display list) out of its data cache first, since the GE reads memory */
/* the sky (build 21): drawn at the far plane (the bottom of the reversed depth
range), without writing depth or fog, so the level covers it wherever the
level is */
static unsigned long sky_draws = 0, sky_parts_start = 0, sky_parts = 0;
static int in_sky = 0;

void psp_ge_sky(int on)
{
	if (!frame_open)
		return;
	in_sky = on;
	if (on)
		sky_parts_start = model_draws_this_frame;
	else
		sky_parts += model_draws_this_frame - sky_parts_start;
	if (on)
	{
		sky_draws++;
		sceGuDepthRange(1, 0);
		sceGuDepthMask(GU_TRUE);
		sceGuDisable(GU_FOG);
	}
	else
	{
		sceGuDepthRange(WORLD_DEPTH_NEAR, 0);
		sceGuDepthMask(GU_FALSE);
		sceGuEnable(GU_FOG);
	}
}

/* ---------- a new map or BSP (build 27)

The level streams are copies of a BSP's vertices, and the model buffers
were rewritten where the map's tags were loaded; the next BSP or map loads
at the same addresses, so what was made from the last one is let go. */

static void overlay_backing_forget(void);

void psp_ge_forget_level(void)
{
	int index;

	for (index = 0; index < LEVEL_STREAMS; index++)
	{
		free(level_streams[index].base);
		free(level_streams[index].lightmap);
		free(level_streams[index].normals);
	}
	memset(level_streams, 0, sizeof(level_streams));
	/* build 64: the overlays' on-a-wall answers belong to this BSP */
	overlay_backing_forget();
	stream_bytes = 0;
	platform_log("GE: the level's vertex copies let go (a new BSP or map)");
}

void psp_ge_forget_map(void)
{
	int index;

	for (index = 0; index < CONVERTED_BUFFERS; index++)
		free(skinned[index]);
	memset(skinned, 0, sizeof(skinned));
	memset(converted, 0, sizeof(converted));
	memset(converted_generation, 0, sizeof(converted_generation));
	converted_count = reconverted_count = skinned_parts = skin_batches = skin_bytes = skin_failures = 0;
	psp_ge_forget_level();
}

/* ---------- see-through model parts (build 21)

The game sorts its see-through geometry and draws it after the solid world;
the GE path keeps its own list: each part's matrix and texture are fixed
when it is queued (the node matrices it came from may not outlive the
call), and the list is drawn far to near at the end of the frame, without
writing depth. The sky's parts go first, at the far plane. One texture each,
the shader's first map, with the shader's blend function. */

struct transparent_draw
{
	ScePspFMatrix4 matrix;
	void *vertices;
	const unsigned short *indices;
	int index_count, strip;
	unsigned int key;
	float u_scale, v_scale, u_offset, v_offset, distance;
	short blend;
	unsigned char sky, sprites;
	/* a widget sprite's (sky 2): whether it is hidden behind the level */
	unsigned char depth_test;
	/* the level's (build 23): 1 indexed into a level stream, 2 cut-up corners */
	unsigned char level;
	/* build 38: a first-person part (its own projection and depth slice) */
	unsigned char first_person;
	/* build 60: a see-through model part's triangles cut at the near plane
	or the guard band: GE vertices in the part's space, drawn after its list */
	void *cut_vertices;
	int cut_count;
	/* build 60: a level overlay's nudge toward the eye, in depth steps
	(0: worked out from distance when drawn, as builds 54-59) */
	short depth_units;
};

/* a sprite batch's GE vertices: texture coordinate, colour, position */
struct sprite_vertex
{
	float u, v;
	unsigned int color;
	float x, y, z;
};

/* the game's build_sprite_vertex: position (view space), texture coordinate, colour (ARGB) */
struct game_sprite_vertex
{
	float x, y, z, u, v;
	unsigned int argb;
};
#define MAXIMUM_TRANSPARENT_DRAWS 160
static struct transparent_draw transparent[MAXIMUM_TRANSPARENT_DRAWS];
static int transparent_count = 0, transparent_order[MAXIMUM_TRANSPARENT_DRAWS];
static unsigned long transparent_drawn = 0, transparent_dropped = 0;

static void hud_blend(short blend);

/* build 36: a node matrix far off or not a number (Sheepdog01 saw something stretch
across the screen as the warthog rolled over a ghost): counted, and the
first few logged with the frame */
static unsigned long wild_nodes = 0;

static void halo_matrix(const float *node, ScePspFMatrix4 *model)
{
	float dx = node[10] - camera_eye[0], dy = node[11] - camera_eye[1], dz = node[12] - camera_eye[2];

	if (!(node[0] > 0.0f && node[0] < 100.0f) || !(dx * dx + dy * dy + dz * dz < 4.0e6f))
	{
		if (++wild_nodes <= 8)
			platform_log("GE: frame %lu, a wild node matrix: scale %f at %f %f %f", frames, node[0], node[10], node[11],
				node[12]);
	}
	model->x.x = node[0] * node[1]; model->x.y = node[0] * node[2]; model->x.z = node[0] * node[3]; model->x.w = 0.0f;
	model->y.x = node[0] * node[4]; model->y.y = node[0] * node[5]; model->y.z = node[0] * node[6]; model->y.w = 0.0f;
	model->z.x = node[0] * node[7]; model->z.y = node[0] * node[8]; model->z.z = node[0] * node[9]; model->z.w = 0.0f;
	model->w.x = node[10]; model->w.y = node[11]; model->w.z = node[12]; model->w.w = 1.0f;
	relative(model);
}

static void node_matrix(const void *vertices, const unsigned short *indices, ScePspFMatrix4 *model)
{
	int node_index = ((const unsigned char *)vertices)[24 * indices[0] + 11] / 3;

	if (node_index >= model_node_count)
		node_index = 0;
	halo_matrix(model_nodes + 13 * node_index, model);
}

/* a batch of sprites (build 22): quads in view space, drawn with the view's
own matrix back to the world; queued with the see-through parts */
static unsigned long sprites_queued = 0;

static int psp_ge_sprites_body(void const *vertices, int sprite_count, unsigned int bitmap_key, short blend, float const *view_to_world)
{
	struct game_sprite_vertex const *in = vertices;
	struct sprite_vertex *out;
	struct transparent_draw *draw;
	int sprite, corner;
	static const int fan[6] = { 0, 1, 2, 0, 2, 3 };
	float cx = 0.0f, cy = 0.0f, cz = 0.0f;

	if (!frame_open || !vertices || sprite_count <= 0 || !bitmap_key || list_full())
		return 0;
	if (transparent_count >= MAXIMUM_TRANSPARENT_DRAWS)
	{
		transparent_dropped++;
		return 0;
	}
	out = list_take(sprite_count * 6 * sizeof(struct sprite_vertex));
	if (!out)
		return 0;
	for (sprite = 0; sprite < sprite_count; sprite++)
	{
		for (corner = 0; corner < 6; corner++)
		{
			struct game_sprite_vertex const *v = &in[4 * sprite + fan[corner]];
			struct sprite_vertex *o = &out[6 * sprite + corner];
			unsigned int argb = v->argb;

			o->u = v->u;
			o->v = v->v;
			o->color = (argb & 0xFF00FF00) | ((argb >> 16) & 0xFF) | ((argb & 0xFF) << 16);
			o->x = v->x; o->y = v->y; o->z = v->z;
		}
		cx += in[4 * sprite].x; cy += in[4 * sprite].y; cz += in[4 * sprite].z;
	}
	draw = &transparent[transparent_count++];
	halo_matrix(view_to_world, &draw->matrix);
	draw->vertices = out;
	draw->indices = NULL;
	draw->index_count = sprite_count * 6;
	draw->strip = 0;
	draw->key = bitmap_key;
	draw->u_scale = draw->v_scale = 1.0f;
	draw->u_offset = draw->v_offset = 0.0f;
	draw->blend = blend;
	draw->sky = 0;
	draw->sprites = 1;
	draw->level = 0;
	draw->first_person = 0;
	/* view space: distance from the eye */
	cx /= sprite_count; cy /= sprite_count; cz /= sprite_count;
	draw->distance = cx * cx + cy * cy + cz * cz;
	sprites_queued += sprite_count;
	return 1;
}

int psp_ge_sprites(void const *vertices, int sprite_count, unsigned int bitmap_key, short blend, float const *view_to_world)
{
	unsigned int start = sceKernelGetSystemTimeLow();
	int result = psp_ge_sprites_body(vertices, sprite_count, bitmap_key, blend, view_to_world);

	ge_entry_us[4] += sceKernelGetSystemTimeLow() - start;
	return result;
}

/* ---------- the widget layer's sprites (build 31): lens flares and light
volumes. The game hands over a world point, a radius, a scale, a rotation
and a colour (ARGB, alpha the brightness); the Xbox draws a square facing the
screen, the texture times the colour, added with the alpha as its weight. Here
the colour is premultiplied by its alpha and the square added, built in the
world on the camera's right and up so the GE's depth test applies (for those
that ask for it). A far one (the sun's, 1,024 units out) is pulled in along
its line of sight to within the far plane, shrinking with it, so it looks the
same. The Xbox also adds a white-hot core (the texture to the sixth power,
by a tint factor); that is left out */
static unsigned int widget_key = 0;
static int widget_depth = 1;
static unsigned long widget_sprites = 0, widget_begins = 0, widget_untextured = 0, occlusion_tests = 0,
	occlusion_visible = 0;
extern unsigned long psp_occlusion_rays_cast, psp_occlusion_rays_kept; /* rasterizer_lights.c (psp_fixups.py) */

void psp_ge_widget_begin(int depth_test)
{
	widget_depth = depth_test;
	widget_begins++;
}

void psp_ge_widget_texture(unsigned int key)
{
	widget_key = key;
	if (!key)
		widget_untextured++;
}

/* the occlusion rays' tally, for the log */
void psp_ge_widget_occlusion(int visible)
{
	occlusion_tests++;
	if (visible)
		occlusion_visible++;
}

void psp_ge_widget_sprite(float const *point, float radius, float scale_x, float scale_y, float rotation,
	unsigned long color)
{
	static const float corner_uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	static const int fan[6] = { 0, 1, 2, 0, 2, 3 };
	struct sprite_vertex *out;
	struct transparent_draw *draw;
	float centre[3], offset[4][2], camera[3], axis_x, axis_y, alpha, limit, pull = 1.0f;
	unsigned int r, g, b;
	int corner;

	if (!frame_open || !widget_key || radius <= 0.0f || list_full())
		return;
	if (transparent_count >= MAXIMUM_TRANSPARENT_DRAWS)
	{
		transparent_dropped++;
		return;
	}
	to_camera(point, camera);
	if (camera[2] <= NEAR_CLIP)
		return;
	limit = view_far * 0.8f;
	if (camera[2] > limit)
		pull = limit / camera[2];
	centre[0] = camera_eye[0] + (point[0] - camera_eye[0]) * pull;
	centre[1] = camera_eye[1] + (point[1] - camera_eye[1]) * pull;
	centre[2] = camera_eye[2] + (point[2] - camera_eye[2]) * pull;
	radius *= pull;
	/* rasterizer_widget_draw_sprite3d's corners, in radii: its axes are the
	radius turned by the rotation (in degrees there), x right, y down */
	{
		float angle = rotation * (3.14159265f / 180.0f), c = cosf(angle), s = sinf(angle);

		axis_x = radius * (c - s);
		axis_y = radius * (s + c);
	}
	offset[0][0] = -scale_x * axis_x; offset[0][1] = -scale_y * axis_y;
	offset[1][0] = scale_x * axis_y; offset[1][1] = -scale_y * axis_x;
	offset[2][0] = scale_x * axis_x; offset[2][1] = scale_y * axis_y;
	offset[3][0] = -scale_x * axis_y; offset[3][1] = scale_y * axis_x;
	alpha = (float)((color >> 24) & 0xFF) * (1.0f / 255.0f);
	r = (unsigned int)(((color >> 16) & 0xFF) * alpha);
	g = (unsigned int)(((color >> 8) & 0xFF) * alpha);
	b = (unsigned int)((color & 0xFF) * alpha);
	if (!(r | g | b))
		return;
	out = list_take(6 * sizeof(struct sprite_vertex));
	if (!out)
		return;
	for (corner = 0; corner < 6; corner++)
	{
		int k = fan[corner];
		struct sprite_vertex *o = &out[corner];

		o->u = corner_uv[k][0];
		o->v = corner_uv[k][1];
		o->color = 0xFF000000 | (b << 16) | (g << 8) | r;
		/* screen y runs down, the camera's up runs up */
		o->x = centre[0] + camera_right[0] * offset[k][0] - camera_up[0] * offset[k][1];
		o->y = centre[1] + camera_right[1] * offset[k][0] - camera_up[1] * offset[k][1];
		o->z = centre[2] + camera_right[2] * offset[k][0] - camera_up[2] * offset[k][1];
	}
	draw = &transparent[transparent_count++];
	gumLoadIdentity(&draw->matrix);
	relative(&draw->matrix);
	draw->vertices = out;
	draw->indices = NULL;
	draw->index_count = 6;
	draw->strip = 0;
	draw->key = widget_key;
	draw->u_scale = draw->v_scale = 1.0f;
	draw->u_offset = draw->v_offset = 0.0f;
	draw->blend = 3;
	draw->sky = 2;
	draw->depth_test = (unsigned char)widget_depth;
	draw->sprites = 1;
	draw->level = 0;
	draw->first_person = 0;
	draw->distance = camera[2] * camera[2] * pull * pull;
	widget_sprites++;
}

/* build 60: a see-through model part checked against the camera as the
level is: triangles wholly inside kept, wholly outside a plane skipped, the
rest cut on the CPU. These parts were given to the GE whole. The sky is
one: a dome some 40 units round the eye, its planet a single quad that at
some headings has a corner behind the eye, and the GE then drew nothing of
it (Sheepdog01's b59 shots 67-70, the planet gone while in view; reproduced in the
emulator at his spot, 29.9 -114.4, "skydump") */
static void transparent_cut(struct transparent_draw *draw, int vertex_count)
{
	const unsigned short *indices = draw->indices;
	long triangles = draw->strip ? draw->index_count - 2 : draw->index_count / 3, t, count = 0;
	unsigned short *kept = NULL;
	int k, any = 0, cut = 0;

	corner_verdict_reset();
	for (t = 0; t < triangles; t++)
	{
		unsigned short corner[3];
		int bits[3];

		if (draw->strip)
		{
			corner[0] = indices[t];
			corner[1] = indices[t + ((t & 1) ? 2 : 1)];
			corner[2] = indices[t + ((t & 1) ? 1 : 2)];
			if (corner[0] == corner[1] || corner[1] == corner[2] || corner[0] == corner[2])
				continue;
		}
		else
		{
			corner[0] = indices[3 * t];
			corner[1] = indices[3 * t + 1];
			corner[2] = indices[3 * t + 2];
		}
		if (corner[0] >= vertex_count || corner[1] >= vertex_count || corner[2] >= vertex_count)
			continue;
		for (k = 0; k < 3; k++)
		{
			int known = corner[k] < CORNER_VERDICTS ? corner_verdict[corner[k]] : -1;

			if (known >= corner_stamp && known < corner_stamp + CORNER_STAMP_STEP)
				bits[k] = known - corner_stamp;
			else
			{
				float c[3];

				model_to_camera(&draw->matrix, (const float *)((const unsigned char *)draw->vertices + 24 * corner[k] + 12), c);
				bits[k] = outside(c);
				if (corner[k] < CORNER_VERDICTS)
					corner_verdict[corner[k]] = (unsigned short)(corner_stamp + bits[k]);
			}
		}
		if (!any && (bits[0] | bits[1] | bits[2]))
		{
			/* the first triangle not wholly inside: from here a list of our own */
			long u;

			any = 1;
			kept = list_take((int)((triangles * 3) * sizeof(unsigned short) + 3) & ~3);
			if (!kept)
				return;
			/* the triangles before this one were all inside: copied as a list */
			count = 0;
			for (u = 0; u < t; u++)
			{
				unsigned short a, b, c;

				if (draw->strip)
				{
					a = indices[u];
					b = indices[u + ((u & 1) ? 2 : 1)];
					c = indices[u + ((u & 1) ? 1 : 2)];
					if (a == b || b == c || a == c)
						continue;
				}
				else
				{
					a = indices[3 * u]; b = indices[3 * u + 1]; c = indices[3 * u + 2];
				}
				if (a >= vertex_count || b >= vertex_count || c >= vertex_count)
					continue;
				kept[3 * count] = a; kept[3 * count + 1] = b; kept[3 * count + 2] = c;
				count++;
			}
		}
		if (!(bits[0] | bits[1] | bits[2]))
		{
			if (any)
			{
				kept[3 * count] = corner[0]; kept[3 * count + 1] = corner[1]; kept[3 * count + 2] = corner[2];
			}
			count++;
			continue;
		}
		if (bits[0] & bits[1] & bits[2])
			continue;
		{
			float polygon[3][CORNER_FLOATS];

			for (k = 0; k < 3; k++)
			{
				const unsigned char *record = (const unsigned char *)draw->vertices + 24 * corner[k];
				const float *uv = (const float *)record, *p = (const float *)(record + 12);

				model_to_camera(&draw->matrix, p, polygon[k]);
				polygon[k][3] = uv[0];
				polygon[k][4] = uv[1];
				polygon[k][5] = (signed char)record[8];
				polygon[k][6] = (signed char)record[9];
				polygon[k][10] = (signed char)record[10];
				polygon[k][7] = p[0];
				polygon[k][8] = p[1];
				polygon[k][9] = p[2];
			}
			cut += clip_triangle(polygon, bits[0] | bits[1] | bits[2], model_cut + cut, MAXIMUM_MODEL_CUT_CORNERS - cut);
		}
	}
	if (!any)
		return;
	draw->indices = kept;
	draw->index_count = (int)count * 3;
	draw->strip = 0;
	if (cut)
	{
		unsigned char *out = list_take(cut * 24);
		int c;

		if (!out)
			return;
		for (c = 0; c < cut; c++)
		{
			unsigned char *record = out + 24 * c;
			float uv[2] = { model_cut[c][3], model_cut[c][4] };

			memcpy(record, uv, 8);
			record[8] = (unsigned char)(signed char)model_cut[c][5];
			record[9] = (unsigned char)(signed char)model_cut[c][6];
			record[10] = (unsigned char)(signed char)model_cut[c][10];
			record[11] = 0;
			memcpy(record + 12, &model_cut[c][7], 12);
		}
		draw->cut_vertices = out;
		draw->cut_count = cut;
	}
}

static void psp_ge_queue_transparent_body(void *vertices, int vertex_count, int uncompressed, const unsigned short *indices,
	long index_count, int strip, unsigned int key, float u_scale, float v_scale, float u_offset, float v_offset,
	short blend, const float *centroid)
{
	struct transparent_draw *draw;

	if (!frame_open || index_count < 3 || !vertices || !indices || !model_nodes || model_node_count <= 0 ||
		vertex_count <= 0 || !model_convert(vertices, vertex_count, uncompressed, NULL, 0, 0, NULL))
		return;
	if (transparent_count >= MAXIMUM_TRANSPARENT_DRAWS)
	{
		transparent_dropped++;
		return;
	}
	draw = &transparent[transparent_count++];
	draw->cut_vertices = NULL;
	draw->cut_count = 0;
	node_matrix(vertices, indices, &draw->matrix);
	draw->first_person = (unsigned char)model_first_person;
	draw->vertices = vertices;
	{
		/* build 42: a see-through part hanging from several nodes (the
		warthog's lights on its hull and gun mount, the chain gun's belt on
		the mount, chain and gun) was drawn on its first vertex's node, so
		the rest stretched toward it as the turret turned. Its vertices are
		placed on the CPU each frame instead, each by its own node (blended
		ones by the first: the rigid record keeps only that), and drawn with
		no further matrix */
		const unsigned char *base = vertices;
		long i;
		int first = indices[0] < vertex_count ? base[24 * indices[0] + 11] : 0, multi = 0;

		for (i = 0; i < index_count && !multi; i++)
			if (indices[i] < vertex_count && base[24 * indices[i] + 11] != first)
				multi = 1;
		if (multi && vertex_count <= 4096)
		{
			unsigned char *out = list_take(vertex_count * 24);

			if (out)
			{
				static ScePspFMatrix4 node_at[SKIN_NODES];
				static unsigned char node_done[SKIN_NODES];
				int v;

				memset(node_done, 0, sizeof(node_done));
				for (v = 0; v < vertex_count; v++)
				{
					const unsigned char *in = base + 24 * v;
					const float *p = (const float *)(in + 12);
					float *q = (float *)(out + 24 * v + 12);
					int node = in[11] / 3;
					ScePspFMatrix4 *m;

					if (node >= SKIN_NODES || node >= model_node_count)
						node = 0;
					m = &node_at[node];
					if (!node_done[node])
					{
						halo_matrix(model_nodes + 13 * node, m);
						node_done[node] = 1;
					}
					memcpy(out + 24 * v, in, 12);
					q[0] = m->x.x * p[0] + m->y.x * p[1] + m->z.x * p[2] + m->w.x;
					q[1] = m->x.y * p[0] + m->y.y * p[1] + m->z.y * p[2] + m->w.y;
					q[2] = m->x.z * p[0] + m->y.z * p[1] + m->z.z * p[2] + m->w.z;
				}
				draw->vertices = out;
				memset(&draw->matrix, 0, sizeof(draw->matrix));
				draw->matrix.x.x = draw->matrix.y.y = draw->matrix.z.z = draw->matrix.w.w = 1.0f;
				transparent_placed++;
			}
		}
	}
	draw->indices = indices;
	draw->index_count = (int)index_count;
	draw->strip = strip;
	draw->key = key;
	draw->u_scale = model_u_scale * (u_scale != 0.0f ? u_scale : 1.0f);
	draw->v_scale = model_v_scale * (v_scale != 0.0f ? v_scale : 1.0f);
	draw->u_offset = u_offset;
	draw->v_offset = v_offset;
	draw->blend = blend;
	draw->sky = (unsigned char)in_sky;
	draw->sprites = 0;
	draw->level = 0;
	if (centroid)
	{
		float dx = centroid[0] - camera_eye[0], dy = centroid[1] - camera_eye[1], dz = centroid[2] - camera_eye[2];

		draw->distance = dx * dx + dy * dy + dz * dz;
	}
	else
		draw->distance = 0.0f;
	if (draw->sky)
		draw->distance = 1e30f;
	if (model_drop < 0)
		model_drop = psp_keep("modeldrop");
	if (!draw->first_person && !model_drop)
		transparent_cut(draw, vertex_count);
	/* build 60: "skydump": where a sky part's corners lie from the eye */
	if (draw->sky)
	{
		static int dump = -1;
		static unsigned long parts = 0;

		if (dump < 0)
			dump = psp_keep("skydump");
		if (dump && (parts++ % 400) < 8)
		{
			float zmin = 1e30f, zmax = -1e30f, side = 0.0f;
			int behind = 0, beyond = 0, wide = 0, n = 0;
			long i;

			for (i = 0; i < index_count; i++)
			{
				const float *q = (const float *)((const unsigned char *)draw->vertices + 24 * indices[i] + 12);
				float c[3];

				if (indices[i] >= vertex_count)
					continue;
				model_to_camera(&draw->matrix, q, c);
				n++;
				if (c[2] < zmin) zmin = c[2];
				if (c[2] > zmax) zmax = c[2];
				if (c[2] < projection_near) behind++;
				if (c[2] > projection_far) beyond++;
				if (c[2] > 0.0f && (fabsf(c[0]) > guard_x * c[2] || fabsf(c[1]) > guard_y * c[2])) wide++;
				if (c[2] > 0.0f && fabsf(c[0]) / c[2] > side) side = fabsf(c[0]) / c[2];
			}
			platform_log("skydump: part key %08x, %ld indices%s, corners %d: z %.1f to %.1f (far %.1f); behind the near plane %d, past the far plane %d, past the guard band %d",
				key, index_count, strip ? " (strip)" : "", n, zmin, zmax, projection_far, behind, beyond, wide);
		}
	}
}

void psp_ge_queue_transparent(void *vertices, int vertex_count, int uncompressed, const unsigned short *indices,
	long index_count, int strip, unsigned int key, float u_scale, float v_scale, float u_offset, float v_offset,
	short blend, const float *centroid)
{
	unsigned int start = sceKernelGetSystemTimeLow();

	psp_ge_queue_transparent_body(vertices, vertex_count, uncompressed, indices, index_count, strip, key, u_scale, v_scale, u_offset, v_offset, blend, centroid);
	ge_entry_us[2] += sceKernelGetSystemTimeLow() - start;
}

/* the level's see-through surfaces (build 23): as the solid level, each
triangle checked against the camera (kept, skipped, or cut up, the pieces
turned back into the world's coordinates), then queued with an identity
matrix: the kept ones indexed into the material's stream, the pieces as
corners */
static unsigned long level_transparent_draws = 0;
static unsigned long ge_wait_us = 0, ge_wait_worst = 0;
static unsigned long frame_end_us[3];

static struct transparent_draw *transparent_slot(unsigned int key, float u_scale, float v_scale, float u_offset,
	float v_offset, short blend, const float *centroid)
{
	struct transparent_draw *draw;

	if (transparent_count >= MAXIMUM_TRANSPARENT_DRAWS)
	{
		transparent_dropped++;
		return NULL;
	}
	draw = &transparent[transparent_count++];
	memset(&draw->matrix, 0, sizeof(draw->matrix));
	draw->matrix.x.x = draw->matrix.y.y = draw->matrix.z.z = draw->matrix.w.w = 1.0f;
	relative(&draw->matrix);
	draw->key = key;
	draw->u_scale = u_scale != 0.0f ? u_scale : 1.0f;
	draw->v_scale = v_scale != 0.0f ? v_scale : 1.0f;
	draw->u_offset = u_offset;
	draw->v_offset = v_offset;
	draw->blend = blend;
	draw->sky = 0;
	draw->sprites = 0;
	draw->strip = 0;
	draw->depth_units = 0;
	draw->cut_vertices = NULL;
	draw->cut_count = 0;
	/* build 63: never set here before, so a level slot kept whatever the
	slot's last user left: after a frame with more first-person see-through
	parts (the pistol's), some of the level's painted symbols, the cave
	fungus and the teleporter's beam went out in the weapon's depth slice,
	in front of everything (Sheepdog01's b62 shots 6, 18-21; "tdump" showed
	level draws marked first-person). Turning or zooming changed the counts
	and the slots, so it came and went */
	draw->first_person = 0;
	draw->depth_test = 1;
	draw->distance = 0.0f;
	if (centroid)
	{
		float dx = centroid[0] - camera_eye[0], dy = centroid[1] - camera_eye[1], dz = centroid[2] - camera_eye[2];

		draw->distance = dx * dx + dy * dy + dz * dz;
	}
	return draw;
}

/* build 60: how far to nudge a level overlay triangle toward the eye, in
depth steps. Two parts:
- the distance part (build 54): about half a centimetre at the triangle's
  nearest corner;
- the slope part: one pixel's worth of the triangle's depth change across the
  screen. The GE snaps each corner to a sixteenth of a pixel and to a whole
  depth step and interpolates from there, so the wall and the symbol painted
  on it, made of different triangles, disagree by up to some fraction of
  a pixel's depth change, more where their corners lie far off. Seen
  face-on that is a step or two; seen at a slant it is tens of steps a pixel,
  and a fixed nudge lost: the symbols went missing on one side of a line
  across them (Sheepdog01's b59 shots 78-80). 1/z is linear across the screen for a
  plane n.P = d in camera space: per pixel it changes by |n.x| or |n.y| /
  (pixels per unit x d), and the depth by that times near x 61439.
"overlaynoslope": the distance part only (b54-b59) */
/* build 62: two pixels' worth (b60-61: one; Sheepdog01's b61 run: the symbols still
cut off). "overlayslope1" / "overlayslope4": one or four */
static float overlay_slope_pixels = 2.0f;
static int overlay_slope = -1;

/* the slope part alone: overlay_slope_pixels pixels of a camera-space
triangle's depth change across the screen, in depth steps (build 65: shared
with the decals) */
static float slope_units(const float *a, const float *b, const float *c)
{
	float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
	float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
	float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

	if (overlay_slope < 0)
	{
		overlay_slope = !psp_keep("overlaynoslope");
		overlay_slope_pixels = psp_keep("overlayslope1") ? 1.0f : psp_keep("overlayslope4") ? 4.0f : 2.0f;
	}
	if (overlay_slope && length > 0.0f)
	{
		float plane = fabsf(n[0] * a[0] + n[1] * a[1] + n[2] * a[2]) / length;
		float across = fabsf(n[0]) > fabsf(n[1]) ? fabsf(n[0]) : fabsf(n[1]);

		if (plane < 0.001f)
			plane = 0.001f;
		return overlay_slope_pixels * 61439.0f * projection_near * (across / length) / (pixels_per_unit() * plane);
	}
	return 0.0f;
}

/* build 76: the slope part's ceiling. slope_units() grows without bound as a
surface turns edge-on (its plane passes near the eye), up to the 4096-step
cap; at the 16-bit depth's far end a step is metres (z^2 / (near x 61439):
0.33 units at 50 units, near x2), so a symbol, a wall stencil or a decal
batch could beat whole walls in front of it for the frame or two it was
edge-on (Sheepdog01's b75 run: "anything 2d ... visible through walls for a frame
or two"). Graphics APIs have the same knob: polygon offset clamp.
build 80: measured as the LIFT OFF THE SURFACE, not along the line of
sight. b76's 0.03 units along the view was barely a depth step at 25-30
units, and held back 80-98% of the slope nudges (b79 run 3): the symbols
at red base tore across and one sank into its wall, b60's bug again. A
nudge of d along the view lifts a point off its plane by d x cos (the
angle between the view ray and the plane's normal), so a wall seen at a
slant may move a long way toward the eye while staying glued to it in 3D,
which is exactly the case that needs the big nudge; a decal far off still
can't jump tens of units through a cliff. The lift allowed is a share of
the distance (the debug menu: off, 1, 2, 4, 8%; default 2), never under
0.01 units (3 cm). "nudgewide": no ceiling (b60-b75) */
static int nudge_wide = -1;
static unsigned long nudges_clamped = 0, nudges_asked = 0;
static float slope_units_capped(float units, const float *a, const float *b, const float *c)
{
	float ceiling, nearest = a[2], lift, cos_angle, length;
	float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
	float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
	float m[3] = { (a[0] + b[0] + c[0]) / 3.0f, (a[1] + b[1] + c[1]) / 3.0f, (a[2] + b[2] + c[2]) / 3.0f };

	if (nudge_wide < 0)
		nudge_wide = psp_keep("nudgewide");
	nudges_asked++;
	if (nudge_wide || nudge_percent <= 0)
		return units;
	if (b[2] < nearest) nearest = b[2];
	if (c[2] < nearest) nearest = c[2];
	if (nearest < projection_near)
		nearest = projection_near;
	/* the lift allowed, then the distance along the view that gives it */
	lift = nudge_percent * 0.01f * nearest;
	if (lift < 0.01f)
		lift = 0.01f;
	length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) * sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
	cos_angle = length > 0.0f ? fabsf(n[0] * m[0] + n[1] * m[1] + n[2] * m[2]) / length : 1.0f;
	if (cos_angle < 0.02f)
		cos_angle = 0.02f;
	ceiling = (lift / cos_angle) * projection_near * 61439.0f / (nearest * nearest);
	if (units > ceiling)
	{
		nudges_clamped++;
		return ceiling;
	}
	return units;
}

/* build 65: a triangle standing free (not on a wall) is nudged away from the
eye instead, by the distance part: where its edge meets something solid (the
teleporter's shield inside its frame, Sheepdog01's b64 shot 37: a green line along
the frame's edge) the solid side now wins the tie. Returned negative */
static int overlay_units(const float *a, const float *b, const float *c, int on_a_wall)
{
	float nearest = a[2], units;

	if (b[2] < nearest) nearest = b[2];
	if (c[2] < nearest) nearest = c[2];
	if (nearest < projection_near)
		nearest = projection_near;
	{
		float lift = nudge_lift_share * nearest;

		if (lift > nudge_lift_ceiling)
			lift = nudge_lift_ceiling;
		if (lift < 0.005f)
			lift = 0.005f;
		units = lift * 61439.0f * projection_near / (nearest * nearest);
	}
	if (!on_a_wall)
		return units < 3.0f ? -3 : units > 4096.0f ? -4096 : -(int)units;
	units += slope_units_capped(slope_units(a, b, c), a, b, c) + nudge_floor;
	return units < 3.0f ? 3 : units > 4096.0f ? 4096 : (int)units;
}

/* the depth-step buckets level overlay triangles are drawn in: a quarter of a
doubling apart, so a draw's nudge is at most 19% over a triangle's own */
#define OVERLAY_BUCKETS 52
static unsigned char overlay_bucket_of(int units)
{
	int bucket;

	/* bucket 0: pushed away (free-standing); a nudge toward the eye is at
	least 3 steps, bucket 6 and up */
	if (units < 0)
		return 0;
	bucket = (int)(log2f((float)units) * 4.0f);
	if (bucket < 1)
		bucket = 1;

	return (unsigned char)(bucket < 0 ? 0 : bucket >= OVERLAY_BUCKETS ? OVERLAY_BUCKETS - 1 : bucket);
}
#define OVERLAY_TRIANGLES 2048
static unsigned char overlay_triangle_bucket[OVERLAY_TRIANGLES];

/* build 64: whether a see-through level triangle lies on a wall. The slope
part of the nudge is for symbols painted on a wall; a surface standing free
(the teleporter's shield: a thin upright panel) seen nearly edge-on got the
largest nudge of all and drew in front of the frame round it (Sheepdog01, b62-b63:
the beam through the frame, near its axis). Asked once per triangle of the
game's collision: is there a surface within 4 cm in front of it or behind
it (a ray through its middle along its normal; the see-through surfaces
themselves are render-only, not in the collision BSP)? "overlaynowall":
every triangle as on a wall (b60-b63) */
struct collision_result_space { unsigned char bytes[0x50]; } __attribute__((aligned(4)));
extern unsigned char collision_test_vector(unsigned long flags, const float *point, const float *vector,
	long ignore_object_index, struct collision_result_space *collision);
/* build 84: 2048, replaced round-robin when full (b81's 512 filled within a
minute at one base, Sheepdog01's b83 run: 31,250 lookups overflowed and were treated
as on a wall, the beam among them) */
#define OVERLAY_BACKINGS 2048
/* build 81: keyed by the indices' LASTING identity. Since build 68 the
level's indices live in the geometry cache's blocks, and a block is reused
for other data once evicted: keyed by the pointer (b64-b80), red base's
symbols found blue base's answers at the same address (the two bases are
mirror copies, the same counts), so some triangles were "free" and nudged
AWAY from the eye into their wall. The symbols tore along triangle edges and
one sank entirely (Sheepdog01's b79-b80 runs at red base); in the whole of those
runs only the first 226 triangles were ever asked. The key is the pack
address when the indices are the cache's, the pointer otherwise */
static struct
{
	unsigned int key;
	long count;
	unsigned char *flags; /* per triangle: 0 not asked, 1 on a wall, 2 free */
} overlay_backing[OVERLAY_BACKINGS];
static int overlay_backing_count = 0, overlay_all_walls = -1;

static void overlay_backing_forget(void)
{
	int index;

	for (index = 0; index < overlay_backing_count; index++)
		free(overlay_backing[index].flags);
	overlay_backing_count = 0;
}
static unsigned long overlay_wall_asks = 0, overlay_on_walls = 0, overlay_free = 0, overlay_ask_us = 0;

static unsigned long overlay_backing_full = 0;
static int overlay_on_a_wall(const struct ge_vertex *stream, const unsigned short *indices, long triangle_count,
	long triangle)
{
	int slot;
	unsigned char *flags = NULL;
	unsigned int key;

	if (overlay_all_walls < 0)
		overlay_all_walls = psp_keep("overlaynowall");
	if (overlay_all_walls)
		return 1;
	key = psp_geo_address_of(indices);
	if (!key)
		key = (unsigned int)indices;
	for (slot = 0; slot < overlay_backing_count; slot++)
		if (overlay_backing[slot].key == key && overlay_backing[slot].count == triangle_count)
		{
			flags = overlay_backing[slot].flags;
			break;
		}
	if (!flags)
	{
		if (!(flags = calloc(triangle_count, 1)))
			return 1;
		if (overlay_backing_count >= OVERLAY_BACKINGS)
		{
			static int victim = 0;

			slot = victim;
			victim = (victim + 1) % OVERLAY_BACKINGS;
			free(overlay_backing[slot].flags);
			overlay_backing_full++;
		}
		else
			slot = overlay_backing_count++;
		overlay_backing[slot].key = key;
		overlay_backing[slot].count = triangle_count;
		overlay_backing[slot].flags = flags;
	}
	if (!flags[triangle])
	{
		const struct ge_vertex *a = &stream[indices[3 * triangle]], *b = &stream[indices[3 * triangle + 1]],
			*c = &stream[indices[3 * triangle + 2]];
		float e1[3] = { b->x - a->x, b->y - a->y, b->z - a->z }, e2[3] = { c->x - a->x, c->y - a->y, c->z - a->z };
		float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
		float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		unsigned int start = sceKernelGetSystemTimeLow();

		flags[triangle] = 1;
		if (length > 0.0f)
		{
			struct collision_result_space result;
			float point[3], vector[3];
			int k;

			for (k = 0; k < 3; k++)
			{
				float middle = ((&a->x)[k] + (&b->x)[k] + (&c->x)[k]) * (1.0f / 3.0f);

				point[k] = middle + n[k] / length * 0.04f;
				vector[k] = -n[k] / length * 0.08f;
			}
			/* front- and back-facing surfaces of the structure */
			/* build 65: objects too (scenery, machines: a symbol may be painted
			on a scenery piece) */
			flags[triangle] = collision_test_vector(0xC0A3, point, vector, -1, &result) ? 1 : 2;
			{
				static int walldump = -1;

				if (walldump < 0)
					walldump = psp_keep("walldump");
				if (walldump)
				{
					/* the nearest structure surface on each side, within 25 cm */
					float side_point[3], side_vector[3], gap[2] = { -1.0f, -1.0f };
					int side;

					for (side = 0; side < 2; side++)
					{
						float sign = side ? -1.0f : 1.0f;

						for (k = 0; k < 3; k++)
						{
							side_point[k] = point[k] - n[k] / length * 0.04f;
							side_vector[k] = sign * n[k] / length * 0.25f;
						}
						if (collision_test_vector(0x23, side_point, side_vector, -1, &result))
							gap[side] = *(const float *)(result.bytes + 0x14) * 0.25f;
					}
					platform_log("walldump: key-less triangle %ld of %p: %s; nearest surface in front %.3f, behind %.3f (-1: none within 25 cm)",
						triangle, (const void *)indices, flags[triangle] == 1 ? "on a wall" : "free", gap[0], gap[1]);
				}
			}
		}
		overlay_wall_asks++;
		if (flags[triangle] == 1)
			overlay_on_walls++;
		else
			overlay_free++;
		overlay_ask_us += sceKernelGetSystemTimeLow() - start;
	}
	return flags[triangle] == 1;
}

static void psp_ge_queue_level_transparent_body(const void *vertices, int vertex_count, const unsigned short *indices,
	long triangle_count, unsigned int key, float u_scale, float v_scale, float u_offset, float v_offset, short blend,
	const float *centroid)
{
	struct level_stream *stream;
	unsigned short *kept;
	long index, kept_count = 0;
	unsigned int lowest = 0xFFFF, highest = 0;
	int cut_count = 0, units, cut_units = 0, cut_units_free = 0, bucket;
	unsigned short bucket_units[OVERLAY_BUCKETS];
	struct transparent_draw *draw;

	if (!frame_open || triangle_count <= 0 || !vertices || !indices || vertex_count <= 0 || list_full())
		return;
	memset(bucket_units, 0, sizeof(bucket_units));
	stream = level_stream_get(vertices, NULL, vertex_count);
	if (!stream)
		return;
	for (index = 0; index < triangle_count * 3; index++)
	{
		unsigned int vertex = indices[index];

		if (vertex < lowest) lowest = vertex;
		if (vertex > highest) highest = vertex;
	}
	if (highest >= (unsigned int)vertex_count)
		return;
	memset(vertex_codes + lowest, CODE_UNKNOWN, highest - lowest + 1);
	kept = (unsigned short *)list_take((int)(triangle_count * 3 * sizeof(unsigned short) + 3) & ~3);
	if (!kept)
		return;
	for (index = 0; index < triangle_count; index++)
	{
		const unsigned short *triangle = indices + 3 * index;
		int bits[3], corner;

		for (corner = 0; corner < 3; corner++)
		{
			int code = vertex_codes[triangle[corner]];

			if (code & CODE_UNKNOWN)
			{
				float point[3];

				to_camera(&stream->base[triangle[corner]].x, point);
				code = outside(point);
				vertex_codes[triangle[corner]] = (unsigned char)code;
			}
			bits[corner] = code;
		}
		if (!(bits[0] | bits[1] | bits[2]))
		{
			memcpy(kept + 3 * kept_count, triangle, 3 * sizeof(unsigned short));
			if (kept_count < OVERLAY_TRIANGLES)
			{
				float p[3][3];

				for (corner = 0; corner < 3; corner++)
					to_camera(&stream->base[triangle[corner]].x, p[corner]);
				units = overlay_units(p[0], p[1], p[2], overlay_on_a_wall(stream->base, indices, triangle_count, index));
				overlay_triangle_bucket[kept_count] = overlay_bucket_of(units);
				if (abs(units) > bucket_units[overlay_triangle_bucket[kept_count]])
					bucket_units[overlay_triangle_bucket[kept_count]] = (unsigned short)abs(units);
			}
			kept_count++;
		}
		else if (!(bits[0] & bits[1] & bits[2]))
		{
			float corners[3][CORNER_FLOATS] = { { 0.0f } };

			for (corner = 0; corner < 3; corner++)
			{
				const struct ge_vertex *b = &stream->base[triangle[corner]];

				to_camera_corner(&b->x, corners[corner]);
				corners[corner][3] = b->u;
				corners[corner][4] = b->v;
				corners[corner][5] = corners[corner][6] = 0.0f;
			}
			units = overlay_units(corners[0], corners[1], corners[2],
				overlay_on_a_wall(stream->base, indices, triangle_count, index));
			if (units > cut_units)
				cut_units = units;
			if (units < 0 && -units > cut_units_free)
				cut_units_free = -units;
			cut_count += clip_triangle(corners, bits[0] | bits[1] | bits[2], cut_corners + cut_count,
				MAXIMUM_CUT_CORNERS - cut_count);
		}
	}
	if (kept_count > OVERLAY_TRIANGLES)
	{
		/* more than the table holds: one draw at the largest nudge seen */
		int most = 0;

		for (bucket = 0; bucket < OVERLAY_BUCKETS; bucket++)
			if (bucket_units[bucket] > most)
				most = bucket_units[bucket];
		if ((draw = transparent_slot(key, u_scale, v_scale, u_offset, v_offset, blend, centroid)) != NULL)
		{
			draw->vertices = stream->base;
			draw->indices = kept;
			draw->index_count = (int)kept_count * 3;
			draw->level = 1;
			draw->depth_units = (short)most;
			level_transparent_draws++;
		}
	}
	else if (kept_count)
	{
		/* a draw per bucket of nudges, its triangles gathered together */
		for (bucket = 0; bucket < OVERLAY_BUCKETS; bucket++)
		{
			unsigned short *gathered;
			long t, count = 0;

			if (!bucket_units[bucket])
				continue;
			for (t = 0; t < kept_count; t++)
				count += overlay_triangle_bucket[t] == bucket;
			gathered = (unsigned short *)list_take((int)(count * 3 * sizeof(unsigned short) + 3) & ~3);
			if (!gathered || (draw = transparent_slot(key, u_scale, v_scale, u_offset, v_offset, blend, centroid)) == NULL)
				break;
			count = 0;
			for (t = 0; t < kept_count; t++)
				if (overlay_triangle_bucket[t] == bucket)
				{
					memcpy(gathered + 3 * count, kept + 3 * t, 3 * sizeof(unsigned short));
					count++;
				}
			draw->vertices = stream->base;
			draw->indices = gathered;
			draw->index_count = (int)count * 3;
			draw->level = 1;
			draw->depth_units = bucket ? (short)bucket_units[bucket] : (short)-bucket_units[bucket];
			level_transparent_draws++;
		}
	}
	if (cut_count && (draw = transparent_slot(key, u_scale, v_scale, u_offset, v_offset, blend, centroid)) != NULL)
	{
		struct ge_vertex *out = list_take(cut_count * sizeof(struct ge_vertex));

		if (!out)
			return;
		for (index = 0; index < cut_count; index++)
		{
			const float *c = cut_corners[index];
			int k;

			/* the world position carried through the cut (build 34) */
			for (k = 0; k < 3; k++)
				(&out[index].x)[k] = c[7 + k];
			out[index].u = c[3];
			out[index].v = c[4];
		}
		draw->vertices = out;
		draw->indices = NULL;
		draw->index_count = cut_count;
		draw->level = 2;
		/* on-a-wall triangles' largest nudge, or if all were free, pushed away */
		draw->depth_units = (short)(cut_units > 0 ? cut_units : -cut_units_free);
		level_transparent_draws++;
	}
}

void psp_ge_queue_level_transparent(const void *vertices, int vertex_count, const unsigned short *indices,
	long triangle_count, unsigned int key, float u_scale, float v_scale, float u_offset, float v_offset, short blend,
	const float *centroid)
{
	unsigned int start = sceKernelGetSystemTimeLow();

	psp_ge_queue_level_transparent_body(vertices, vertex_count, indices, triangle_count, key, u_scale, v_scale, u_offset, v_offset, blend, centroid);
	ge_entry_us[3] += sceKernelGetSystemTimeLow() - start;
}

#define LEVEL_OVERLAY_OFFSET 24
static int overlay_scaled = -1;
static int overlays_off = -1;
extern volatile int psp_screenshot_requested;
static int tdump = -1;

static void transparent_flush_body(void)
{
	int saved_limit = list_limit;

	int index, other;

	if (!transparent_count)
		return;
	if (list_limit < DISPLAY_LIST_BYTES - HUD_RESERVE)
		list_limit = DISPLAY_LIST_BYTES - HUD_RESERVE;
	if (overlays_off < 0)
		overlays_off = psp_keep("nooverlays");
	/* build 62: the world's projection and depth slice for the world's
	see-through parts, whatever is current. A flush can come while the
	first-person projection is still set (no model end after the weapon's
	last part); the level's painted symbols and the cave's fungus were then
	drawn in the weapon's depth slice, in front of everything (Sheepdog01, b61:
	only with the pistol out, gone when zoomed, when no weapon is drawn) */
	if (model_first_person)
	{
		use_projection(0);
		first_person_flushes++;
	}
	/* far to near (the sky's parts count as farthest) */
	for (index = 0; index < transparent_count; index++)
	{
		for (other = index; other > 0 && transparent[transparent_order[other - 1]].distance < transparent[index].distance; other--)
			transparent_order[other] = transparent_order[other - 1];
		transparent_order[other] = index;
	}
	sceGuDepthMask(GU_TRUE);
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
	sceGuTexWrap(GU_REPEAT, GU_REPEAT);
	sceGuColor(0xFFFFFFFF);
	sceGumMatrixMode(GU_MODEL);
	for (index = 0; index < transparent_count; index++)
	{
		struct transparent_draw const *draw = &transparent[transparent_order[index]];

		/* build 63: "tdump": each see-through draw of the frame a screenshot
		is asked for, with the state it goes out under */
		if (tdump < 0)
			tdump = psp_keep("tdump");
		if (tdump && psp_screenshot_requested)
		{
			unsigned int state_depth = sceGuGetStatus(GU_DEPTH_TEST);

			platform_log("tdump: %d/%d key %08x sky %d level %d fp %d sprites %d blend %d dist %.1f count %d cut %d units %d at %.2f %.2f %.2f depth test %u mfp %d",
				index, transparent_count, draw->key, draw->sky, draw->level, draw->first_person, draw->sprites, draw->blend,
				sqrtf(draw->distance), draw->index_count, draw->cut_count, draw->depth_units,
				draw->matrix.w.x, draw->matrix.w.y, draw->matrix.w.z, state_depth, model_first_person);
		}
		if ((draw->first_person ? list_full_first_person() : list_full()) || (draw->level && overlays_off) ||
			!psp_tex_bind(draw->key, NULL))
			continue;
		if (draw->sky == 2)
		{
			if (!draw->depth_test)
				sceGuDisable(GU_DEPTH_TEST);
			sceGuDisable(GU_FOG);
		}
		else if (draw->sky)
		{
			sceGuDepthRange(1, 0);
			sceGuDisable(GU_FOG);
		}
		else if (draw->blend == 0)
			sceGuEnable(GU_FOG);
		else
			/* added or multiplied light fades by its own: no fog colour on top */
			sceGuDisable(GU_FOG);
		hud_blend(draw->blend);
		if (draw->first_person)
			use_projection(1);
		/* build 35: the level's see-through surfaces (the base pillars'
		painted symbols) lie on solid ones; the Xbox's 24-bit depth sorts
		that out, the PSP's 16 bits flickered between them by the view.
		Nudged nearer, as decals are */
		if (draw->level)
		{
			/* build 54: the nudge sized by distance, as build 52 did for
			decals: a fixed 24 depth steps is metres far off (the base's
			painted symbols showed through whatever stood in front of them,
			Sheepdog01 b50-b52) and almost nothing near. Now about half a centimetre
			at the overlay's distance (draw->distance is its square), never
			under 3 steps; "overlayoffset": the fixed 24 */
			int units = LEVEL_OVERLAY_OFFSET;

			if (overlay_scaled < 0)
				overlay_scaled = !psp_keep("overlayoffset");
			if (overlay_scaled && draw->depth_units)
				units = draw->depth_units;
			else if (overlay_scaled && draw->distance > 0.0f)
			{
				float scaled = 0.005f * 61439.0f * projection_near / draw->distance;

				units = scaled < 3.0f ? 3 : scaled > 4096.0f ? 4096 : (int)scaled;
			}
			sceGuDepthOffset(units);
		}
		sceGuTexScale(draw->u_scale, draw->v_scale);
		sceGuTexOffset(draw->u_offset, draw->v_offset);
		sceGumLoadMatrix(&draw->matrix);
		if (draw->sprites)
			sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
				draw->index_count, NULL, draw->vertices);
		else if (draw->level == 1)
			sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
				draw->index_count, draw->indices, draw->vertices);
		else if (draw->level == 2)
			sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
				draw->index_count, NULL, draw->vertices);
		else
		{
			if (draw->index_count > 0)
				sceGumDrawArray(draw->strip ? GU_TRIANGLE_STRIP : GU_TRIANGLES,
					GU_TEXTURE_32BITF | GU_NORMAL_8BIT | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
					draw->index_count, draw->indices, draw->vertices);
			if (draw->cut_count > 0 && draw->cut_vertices)
				sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_NORMAL_8BIT | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
					draw->cut_count, NULL, draw->cut_vertices);
		}
		if (draw->level)
			sceGuDepthOffset(0);
		if (draw->first_person)
			use_projection(0);
		if (draw->sky == 2)
			sceGuEnable(GU_DEPTH_TEST);
		else if (draw->sky)
			sceGuDepthRange(WORLD_DEPTH_NEAR, 0);
		transparent_drawn++;
	}
	world_model();
	if (model_first_person)
		use_projection(1);
	sceGuTexScale(1.0f, 1.0f);
	sceGuTexOffset(0.0f, 0.0f);
	sceGuDisable(GU_BLEND);
	sceGuDisable(GU_TEXTURE_2D);
	sceGuEnable(GU_FOG);
	sceGuDepthMask(GU_FALSE);
	transparent_count = 0;
	list_limit = saved_limit;
}

static void transparent_flush(void)
{
	unsigned int start = sceKernelGetSystemTimeLow();

	transparent_flush_body();
	ge_entry_us[8] += sceKernelGetSystemTimeLow() - start;
}

/* ---------- the HUD (build 20)

The game draws its HUD as screen quads: four corners in its own screen's
pixels (848 x 480 here), texture coordinates from 0 to 1, a colour each,
and one bitmap with a tint, a fade and a way to blend with the frame. They
are drawn in the GE's 2D mode, scaled to 480 x 272, after the 3D world.

A meter (the shield bar, ammo counts) is a bitmap whose alpha orders its
pixels from empty to full: the game passes the fill as an alpha threshold,
and pixels below it take the fill colour, the rest the empty colour; the GE
does this with its alpha test, in two draws. */

struct hud_vertex
{
	float u, v;
	unsigned int color;
	float x, y, z;
};

/* the game's dynamic_screen_vertex: position, texture coordinates, colour (ARGB) */
struct game_screen_vertex
{
	float x, y, u, v;
	unsigned int argb;
};

/* the game's rasterizer_meter_parameters (colours ARGB) */
struct game_meter
{
	unsigned int gradient_min_color, gradient_max_color, background_color, flash_color;
	unsigned char flash_color_is_negative, tint_mode_2, pad[2];
	unsigned int tint_color;
	float gradient;
};

#define GAME_SCREEN_WIDTH 848.0f
#define GAME_SCREEN_HEIGHT 480.0f

/* where the game's screen coordinates land (build 26): its 848 x 480 in play
fills the PSP's screen; the menus' 640 x 480 (4:3) is pillarboxed, as the
3D view behind them isn't */
static float ui_scale_x = SCREEN_WIDTH / GAME_SCREEN_WIDTH, ui_scale_y = SCREEN_HEIGHT / GAME_SCREEN_HEIGHT;
static float ui_offset_x = 0.0f;
/* build 36: the HUD laid out in a smaller window (hud.c, psp_fixups.py) is
drawn this much larger */
static float hud_zoom = 1.0f;

void psp_ge_hud_zoom(float zoom)
{
	hud_zoom = zoom > 0.0f ? zoom : 1.0f;
}

static int ui_centered = 0;

void psp_ge_ui_centered(int centered)
{
	ui_centered = centered;
}

void psp_ge_ui_space(int width, int height)
{
	if (width <= 0 || height <= 0)
		return;
	if (ui_centered)
		width = 640;
	ui_scale_y = (float)SCREEN_HEIGHT / height;
	if (width * 3 <= height * 4 + 4)
	{
		/* 4:3 or narrower: square pixels, centred */
		ui_scale_x = ui_scale_y;
		ui_offset_x = (SCREEN_WIDTH - width * ui_scale_x) * 0.5f;
	}
	else
	{
		ui_scale_x = (float)SCREEN_WIDTH / width;
		ui_offset_x = 0.0f;
	}
}
int psp_tex_size(unsigned int key, int *width, int *height);
int psp_tex_palette(unsigned int key, const unsigned int **palette, int *count);
static unsigned long hud_quads = 0;

static unsigned int argb_to_abgr(unsigned int argb, float red, float green, float blue, float alpha)
{
	unsigned int a = (unsigned int)(((argb >> 24) & 0xFF) * alpha), r = (unsigned int)(((argb >> 16) & 0xFF) * red);
	unsigned int g = (unsigned int)(((argb >> 8) & 0xFF) * green), b = (unsigned int)((argb & 0xFF) * blue);

	if (a > 255) a = 255;
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	return (a << 24) | (b << 16) | (g << 8) | r;
}

static void hud_blend(short blend)
{
	sceGuEnable(GU_BLEND);
	switch (blend)
	{
	case 1: sceGuBlendFunc(GU_ADD, GU_DST_COLOR, GU_FIX, 0, 0); break;
	case 2: sceGuBlendFunc(GU_ADD, GU_DST_COLOR, GU_SRC_COLOR, 0, 0); break;
	case 3: sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF); break;
	case 4: sceGuBlendFunc(GU_REVERSE_SUBTRACT, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF); break;
	case 5: sceGuBlendFunc(GU_MIN, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF); break;
	case 6: sceGuBlendFunc(GU_MAX, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF); break;
	/* alpha-multiply-add: the colour is added and the alpha only darkens
	what is behind (the crosshairs and most of the HUD have no alpha, so
	they are purely additive) */
	case 7: sceGuBlendFunc(GU_ADD, GU_FIX, GU_ONE_MINUS_SRC_ALPHA, 0xFFFFFF, 0); break;
	default: sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0); break;
	}
}

/* how the quad's texture coordinates become the pack texture's texels: the
game's coordinates times its texture scale are 0 to 1 across the bitmap, or,
for its linear (interface) bitmaps, in the bitmap's own texels (build 26) */
static float hud_u_scale = 1.0f, hud_v_scale = 1.0f;

static void hud_draw(struct game_screen_vertex const *in, int width, int height, float offset_x, float offset_y,
	int use_vertex_color, unsigned int color, float red, float green, float blue, float fade)
{
	struct hud_vertex *out = list_take(4 * sizeof(struct hud_vertex));
	int index;

	if (!out)
		return;
	for (index = 0; index < 4; index++)
	{
		out[index].u = in[index].u * hud_u_scale * width;
		out[index].v = in[index].v * hud_v_scale * height;
		out[index].color = argb_to_abgr(use_vertex_color ? in[index].argb : color, red, green, blue, fade);
		out[index].x = (in[index].x + offset_x) * ui_scale_x * hud_zoom + ui_offset_x;
		out[index].y = (in[index].y + offset_y) * ui_scale_y * hud_zoom;
		out[index].z = 0.0f;
	}
	sceGuDrawArray(GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 4, NULL, out);
}

/* worked-out meter palettes, kept while their meter doesn't change (build 31);
a slot's frame is the frame (counted from 1) that last used it */
#define METER_CACHE 6
static unsigned int meter_cache_palette[METER_CACHE][256] __attribute__((aligned(64)));
static struct
{
	unsigned int key;
	struct game_meter meter;
	unsigned long frame;
	int count;
} meter_cache[METER_CACHE];
static unsigned long meter_reused = 0, meter_worked = 0;

static unsigned long hud_quad_us = 0, text_us = 0;
static void screen_quad(void const *vertices, unsigned int key, short blend, float red, float green, float blue,
	float fade, float offset_x, float offset_y, void const *meter_parameters, int wrapped, float u_scale, float v_scale);

/* the HUD's quads, timed for the log (build 31) */
void psp_ge_screen_quad(void const *vertices, unsigned int key, short blend, float red, float green, float blue,
	float fade, float offset_x, float offset_y, void const *meter_parameters, int wrapped, float u_scale, float v_scale)
{
	unsigned int start = sceKernelGetSystemTimeLow();

	screen_quad(vertices, key, blend, red, green, blue, fade, offset_x, offset_y, meter_parameters, wrapped, u_scale,
		v_scale);
	hud_quad_us += sceKernelGetSystemTimeLow() - start;
}

static unsigned long screen_quad_bailed = 0;

static void screen_quad(void const *vertices, unsigned int key, short blend, float red, float green, float blue,
	float fade, float offset_x, float offset_y, void const *meter_parameters, int wrapped, float u_scale, float v_scale)
{
	list_hud_phase();
	struct game_screen_vertex const *in = vertices;
	struct game_meter const *meter = meter_parameters;
	int width, height, alpha = 0;

	if (!frame_open || !vertices)
		return;
	/* the 3D world's see-through parts go before the HUD */
	transparent_flush();
	if (list_full())
		return;
	if (!psp_tex_size(key, &width, &height))
	{
		/* a HUD picture the texture pack doesn't have: named once each */
		static unsigned int unknown[16];
		static int unknown_count = 0;
		int index;

		for (index = 0; index < unknown_count && unknown[index] != key; index++);
		if (index == unknown_count && unknown_count < 16)
		{
			unknown[unknown_count++] = key;
			platform_log("HUD: picture %08x isn't in the texture pack", key);
		}
		return;
	}
	if (!psp_tex_bind(key, &alpha))
		return;
	{
		/* the game's bitmap_data: width +4, height +6, flags +14 (bit 4: linear) */
		const unsigned char *bitmap = (const unsigned char *)key;
		int linear = (*(const unsigned short *)(bitmap + 14) >> 4) & 1;
		int bitmap_width = *(const short *)(bitmap + 4), bitmap_height = *(const short *)(bitmap + 6);

		hud_u_scale = u_scale != 0.0f ? u_scale : 1.0f;
		hud_v_scale = v_scale != 0.0f ? v_scale : 1.0f;
		if (linear && bitmap_width > 0 && bitmap_height > 0)
		{
			hud_u_scale /= bitmap_width;
			hud_v_scale /= bitmap_height;
		}
	}
	sceGuDisable(GU_DEPTH_TEST);
	sceGuDisable(GU_FOG);
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexWrap(wrapped ? GU_REPEAT : GU_CLAMP, wrapped ? GU_REPEAT : GU_CLAMP);
	sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
	if (meter)
	{
		/* the Xbox works a meter out per pixel in four register-combiner
		stages; with a palette texture the same sum can be done once per
		palette entry instead (build 23). The texture's grey (its blue) is
		the position along the meter and its alpha the shape:
		  shape 0                    nothing drawn (the Xbox's alpha kill)
		  position past the flash    the empty colour
		  otherwise                  min to max colour along the meter,
		                             plus the flash colour past the fill
		each multiplied by the shape; the frame then gets
		colour x tint + frame x alpha (alpha: the fade, or the empty
		colour's). */
		const unsigned int *palette;
		unsigned int *computed = NULL, fill = meter->gradient_min_color >> 24, flash_end = meter->flash_color >> 24;
		int count, entry, slot, free_slot = -1, hit = 0;

		/* build 63: the depth test and fog back on the way out, as at the
		end; returned with them off, everything after in the frame drew
		through walls until the next HUD quad switched them back */
		if (!psp_tex_palette(key, &palette, &count))
		{
			screen_quad_bailed++;
			sceGuEnable(GU_FOG);
			sceGuEnable(GU_DEPTH_TEST);
			return;
		}
		/* a meter that hasn't changed since it was last worked out (most of
		them, most frames) reuses that palette (build 31): the sum is a
		dozen divisions an entry, about a third of a millisecond a meter */
		for (slot = 0; slot < METER_CACHE; slot++)
		{
			if (meter_cache[slot].key == key && meter_cache[slot].count == count &&
				!memcmp(&meter_cache[slot].meter, meter, sizeof(*meter)))
			{
				computed = meter_cache_palette[slot];
				meter_cache[slot].frame = frames + 1;
				meter_reused++;
				hit = 1;
				break;
			}
			if (meter_cache[slot].frame != frames + 1 &&
				(free_slot < 0 || meter_cache[slot].frame < meter_cache[free_slot].frame))
				free_slot = slot;
		}
		if (!computed)
		{
			/* into a slot not used this frame (the GE reads the palettes when
			the frame's list runs), or else the display list's memory */
			if (free_slot >= 0)
			{
				computed = meter_cache_palette[free_slot];
				meter_cache[free_slot].key = key;
				meter_cache[free_slot].count = count;
				meter_cache[free_slot].meter = *meter;
				meter_cache[free_slot].frame = frames + 1;
			}
			else
			{
				computed = list_take(count * 4 + 16);
				if (!computed)
				{
					screen_quad_bailed++;
					sceGuEnable(GU_FOG);
					sceGuEnable(GU_DEPTH_TEST);
					return;
				}
				computed = (unsigned int *)(((unsigned int)computed + 15) & ~15u);
			}
			meter_worked++;
		}
		for (entry = hit ? count : 0; entry < count; entry++)
		{
			unsigned int texel = palette[entry], shape = texel >> 24, position = (texel >> 16) & 0xFF;
			int channel, out[3], alpha_out;

			if (!shape)
			{
				computed[entry] = 0xFF000000;
				continue;
			}
			if (position >= flash_end)
			{
				for (channel = 0; channel < 3; channel++)
					out[channel] = (meter->background_color >> (16 - 8 * channel)) & 0xFF;
				alpha_out = meter->background_color >> 24;
			}
			else
			{
				/* past the fill by more than a quarter: the flash at full */
				int past = 255 - 2 * PSP_CLAMP_255(4 * ((int)fill - (int)position));

				if (past < 0)
					past = 0;
				for (channel = 0; channel < 3; channel++)
				{
					int low = (meter->gradient_min_color >> (16 - 8 * channel)) & 0xFF;
					int high = (meter->gradient_max_color >> (16 - 8 * channel)) & 0xFF;
					int flash = (meter->flash_color >> (16 - 8 * channel)) & 0xFF;

					out[channel] = low + (high - low) * (int)position / 255 +
						(meter->flash_color_is_negative ? -1 : 1) * flash * past / 255;
					out[channel] = PSP_CLAMP_255(out[channel]);
				}
				alpha_out = meter->tint_color >> 24;
			}
			/* ABGR, each colour times the shape */
			computed[entry] = ((unsigned int)alpha_out << 24) | ((out[2] * shape / 255) << 16) |
				((out[1] * shape / 255) << 8) | (out[0] * shape / 255);
		}
		/* build 39: out of the CPU's cache before the GE reads it. The GE
		draws each piece as soon as it is listed (GU_DIRECT moves its stall
		address on at every draw), long before the frame's end writes the
		cache back: a meter worked out afresh was drawn with whatever that slot
		held in memory before, a meter's previous value, for one frame each time
		it changed (Sheepdog01's "rubber banding" ammo and flashlight meters). The
		display list's own memory is uncached, so only the kept slots need it */
		if (!hit && computed >= &meter_cache_palette[0][0] && computed < &meter_cache_palette[METER_CACHE][0])
			sceKernelDcacheWritebackRange(computed, count * 4);
		sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
		sceGuClutLoad(count / 8, computed);
		sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
		sceGuEnable(GU_BLEND);
		{
			unsigned int tint = meter->tint_color;

			sceGuBlendFunc(GU_ADD, GU_FIX, GU_SRC_ALPHA, ((tint & 0xFF) << 16) | (tint & 0xFF00) | ((tint >> 16) & 0xFF), 0);
		}
		hud_draw(in, width, height, offset_x, offset_y, 0, 0xFFFFFFFF, 1.0f, 1.0f, 1.0f, 1.0f);
	}
	else
	{
		hud_blend(blend);
		hud_draw(in, width, height, offset_x, offset_y, 1, 0, red, green, blue, fade);
	}
	sceGuDisable(GU_BLEND);
	sceGuDisable(GU_TEXTURE_2D);
	sceGuEnable(GU_FOG);
	sceGuEnable(GU_DEPTH_TEST);
	hud_quads++;
}

/* ---------- object shadows (build 25)

The Xbox draws each object's silhouette from the light into a texture and
projects it onto the level around the object. The GE draws a soft round
blob instead, through the same projection: the game still picks the
receiving level triangles (structure_render_shadow) and the shadow's frame
(its centre, the light's direction and two axes across it); each receiving
vertex gets texture coordinates from its position in that frame, and the
blob darkens the frame buffer by the game's shadow colour, fading with
height above or below the object. The receivers lie exactly on the level,
so they are drawn where the depth is equal, cut up near the camera the
same way the level itself is. */

static struct
{
	float position[3], forward[3], left[3], up[3];
	float inverse_radius, radius;
	unsigned int darkness;
	int open;
} shadow;
static unsigned long shadows_drawn = 0, shadow_begins = 0, shadow_requests = 0;
/* build 85: a test of equal-depth drawing on the hardware. The shadow pass
re-draws the level's own triangles with the depth test EQUAL and the blob
on top, and on the PSP they fight in a dotted pattern (Sheepdog01, b79-b84: the
stripes beside the tank, the shadow on the bunker wall), while the
lightmap pass, which does the same with the wall's own vertex array and
index list, never does. Three ways, from the debug menu:
  0  as before: a copy of the triangles in a vertex format of its own (blob
     coordinates and a colour per corner), drawn without indices;
  1  the wall's draw repeated: its vertex array and an index list, the
     blob's coordinates made from the positions by the GE's texture matrix;
  2  GEQUAL with a nudge toward the eye, as the decals (not exact).
If 1 is clean and 0 isn't, the depth depends on the draw's form, and any
equal-depth pass must repeat the wall's own draw; if 1 fights too, equal
depth can't be trusted on this hardware */
static int shadow_mode;
static unsigned int blob_texture[32 * 32] __attribute__((aligned(16)));
static int blob_ready = 0;

struct shadow_vertex
{
	float u, v;
	unsigned int color;
	float x, y, z;
};

/* receivers are gathered here, then copied into the display list at their
exact size (shadows and lights; build 29) */
#define SCRATCH_TRIANGLES 480 /* build 36: 600, less 100 for the cone textures' room */
static struct shadow_vertex scratch_vertices[3 * SCRATCH_TRIANGLES];

/* the game's real_matrix4x3: scale, forward, left, up, position */
void psp_ge_shadow_begin(const float *matrix, const float *color, float radius)
{
	int k;

	shadow.open = 0;
	shadow_begins++;
	if (!frame_open || radius <= 0.0f)
		return;
	for (k = 0; k < 3; k++)
	{
		shadow.forward[k] = matrix[1 + k];
		shadow.left[k] = matrix[4 + k];
		shadow.up[k] = matrix[7 + k];
		shadow.position[k] = matrix[10 + k];
	}
	shadow.radius = radius;
	shadow.inverse_radius = 1.0f / radius;
	{
		/* the colour the frame is multiplied by under the shadow: its distance
		from white is how much the blob takes away */
		int r = PSP_CLAMP_255((int)((1.0f - color[0]) * 255.0f)), g = PSP_CLAMP_255((int)((1.0f - color[1]) * 255.0f)),
			b = PSP_CLAMP_255((int)((1.0f - color[2]) * 255.0f));

		shadow.darkness = (b << 16) | (g << 8) | r;
	}
	shadow.open = shadow.darkness != 0;
	if (!blob_ready)
	{
		/* the blob: full in the middle, nothing at the rim */
		int x, y;

		for (y = 0; y < 32; y++)
			for (x = 0; x < 32; x++)
			{
				float dx = (x + 0.5f) / 16.0f - 1.0f, dy = (y + 0.5f) / 16.0f - 1.0f;
				float d = sqrtf(dx * dx + dy * dy), t = d >= 1.0f ? 0.0f : 1.0f - d;
				int value = (int)(t * t * (3.0f - 2.0f * t) * 255.0f);

				blob_texture[32 * y + x] = 0xFF000000 | ((unsigned int)value * 0x010101);
			}
		sceKernelDcacheWritebackRange(blob_texture, sizeof(blob_texture));
		blob_ready = 1;
	}
}

static void shadow_corner(const float *p, float *u, float *v, unsigned int *color)
{
	float d[3], h, fade;
	int k;
	unsigned int r, g, b;

	for (k = 0; k < 3; k++)
		d[k] = p[k] - shadow.position[k];
	*u = 0.5f + 0.5f * shadow.inverse_radius * (d[0] * shadow.forward[0] + d[1] * shadow.forward[1] + d[2] * shadow.forward[2]);
	*v = 0.5f + 0.5f * shadow.inverse_radius * (d[0] * shadow.left[0] + d[1] * shadow.left[1] + d[2] * shadow.left[2]);
	h = d[0] * shadow.up[0] + d[1] * shadow.up[1] + d[2] * shadow.up[2];
	if (h < 0.0f)
		h = -h;
	fade = 1.0f - h * shadow.inverse_radius * 0.25f;
	if (fade < 0.0f)
		fade = 0.0f;
	r = (unsigned int)((shadow.darkness & 0xFF) * fade);
	g = (unsigned int)(((shadow.darkness >> 8) & 0xFF) * fade);
	b = (unsigned int)(((shadow.darkness >> 16) & 0xFF) * fade);
	*color = 0xFF000000 | (b << 16) | (g << 8) | r;
}

static struct level_stream *level_stream_find(const void *vertices)
{
	const void *key = stream_key(vertices);
	unsigned int slot = ((unsigned int)key >> 5) % LEVEL_STREAMS, tries = 0;

	while (level_streams[slot].source && level_streams[slot].source != key && tries++ < LEVEL_STREAMS)
		slot = (slot + 1) % LEVEL_STREAMS;
	return level_streams[slot].source == key && level_streams[slot].base ? &level_streams[slot] : NULL;
}

/* build 68: a material's copies by their key (object lighting: a material's
compressed vertex data address, which is the key when the data is in the
pack). The copies hold what it asks: base and lightmap texture coordinates */
int psp_ge_level_vertex_texcoords(unsigned int key, int vertex_index, float *uv, float *lightmap_uv)
{
	unsigned int slot = (key >> 5) % LEVEL_STREAMS, tries = 0;
	const struct level_stream *stream;

	while (level_streams[slot].source && (unsigned int)level_streams[slot].source != key && tries++ < LEVEL_STREAMS)
		slot = (slot + 1) % LEVEL_STREAMS;
	stream = &level_streams[slot];
	if ((unsigned int)stream->source != key || !stream->base || vertex_index < 0 || vertex_index >= stream->count)
		return 0;
	uv[0] = stream->base[vertex_index].u;
	uv[1] = stream->base[vertex_index].v;
	if (stream->lightmap)
	{
		lightmap_uv[0] = stream->lightmap[vertex_index].u;
		lightmap_uv[1] = stream->lightmap[vertex_index].v;
	}
	else
		lightmap_uv[0] = lightmap_uv[1] = 0.0f;
	return 1;
}

/* build 68: with a geometry pack open, the copies over their budget: the
least recently drawn go, never one drawn this frame. Open addressing can't
drop a slot without breaking the chains through it, so a dropped slot keeps
its key with no copies, and the next draw of it rebuilds in place */
static void level_streams_trim(void)
{
	while (stream_bytes > LEVEL_STREAM_BUDGET)
	{
		int index, oldest = -1;

		for (index = 0; index < LEVEL_STREAMS; index++)
		{
			struct level_stream *stream = &level_streams[index];

			if (!stream->source || !stream->base || stream->last_frame == stream_frame)
				continue;
			if (oldest < 0 || stream->last_frame < level_streams[oldest].last_frame)
				oldest = index;
		}
		if (oldest < 0)
			return;
		{
			struct level_stream *stream = &level_streams[oldest];

			stream_bytes -= stream->count * sizeof(struct ge_vertex) * (stream->lightmap ? 2 : 1) + stream->count * sizeof(unsigned int);
			free(stream->base);
			free(stream->lightmap);
			free(stream->normals);
			stream->base = stream->lightmap = NULL;
			stream->normals = NULL;
			streams_let_go++;
		}
	}
}

static void psp_ge_shadow_draw_body(const void *vertices, int vertex_count, const unsigned short *indices, long triangle_count)
{
	struct level_stream *stream;
	struct shadow_vertex *out;
	long index, kept_count = 0;
	int cut_count = 0;

	shadow_requests++;
	if (!shadow.open || !frame_open || triangle_count <= 0 || !vertices || !indices || list_full())
		return;
	/* the level's own copy, made when the level was drawn this frame */
	stream = level_stream_find(vertices);
	if (!stream)
		return;
	out = scratch_vertices;
	for (index = 0; index < triangle_count && kept_count < SCRATCH_TRIANGLES; index++)
	{
		const unsigned short *triangle = indices + 3 * index;
		float corners[3][CORNER_FLOATS] = { { 0.0f } };
		int bits[3], corner;

		if (triangle[0] >= vertex_count || triangle[1] >= vertex_count || triangle[2] >= vertex_count)
			return;
		for (corner = 0; corner < 3; corner++)
		{
			to_camera_corner(&stream->base[triangle[corner]].x, corners[corner]);
			bits[corner] = outside(corners[corner]);
		}
		if (bits[0] & bits[1] & bits[2])
			continue;
		if (!(bits[0] | bits[1] | bits[2]))
		{
			for (corner = 0; corner < 3; corner++)
			{
				const struct ge_vertex *b = &stream->base[triangle[corner]];
				struct shadow_vertex *o = &out[3 * kept_count + corner];

				shadow_corner(&b->x, &o->u, &o->v, &o->color);
				o->x = b->x; o->y = b->y; o->z = b->z;
			}
			kept_count++;
			continue;
		}
		/* cut up as the level was: the same corners in camera space, with the
		blob's coordinates riding along (as "base" texture coordinates) */
		for (corner = 0; corner < 3; corner++)
		{
			unsigned int color;

			shadow_corner(&stream->base[triangle[corner]].x, &corners[corner][3], &corners[corner][4], &color);
			corners[corner][5] = (float)(color & 0xFF);
			corners[corner][6] = (float)((color >> 8) & 0xFF);
		}
		cut_count += clip_triangle(corners, bits[0] | bits[1] | bits[2], cut_corners + cut_count,
			MAXIMUM_CUT_CORNERS - cut_count);
	}
	if (!kept_count && !cut_count)
		return;
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(GU_PSM_8888, 0, 0, 0);
	sceGuTexImage(0, 32, 32, 32, blob_texture);
	sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);
	sceGuTexWrap(GU_CLAMP, GU_CLAMP);
	if (shadow_mode == 2)
	{
		float d[3] = { shadow.position[0] - camera_eye[0], shadow.position[1] - camera_eye[1], shadow.position[2] - camera_eye[2] };
		float distance = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), lift, units;

		if (distance < projection_near)
			distance = projection_near;
		lift = 0.01f * distance;
		if (lift > 0.2f)
			lift = 0.2f;
		units = lift * 61439.0f * projection_near / (distance * distance);
		sceGuDepthFunc(GU_GEQUAL);
		sceGuDepthOffset(units < 3.0f ? 3 : units > 4096.0f ? 4096 : (int)units);
	}
	else
		sceGuDepthFunc(GU_EQUAL);
	sceGuDepthMask(GU_TRUE);
	sceGuEnable(GU_BLEND);
	/* the frame times (1 - blob x darkness) */
	sceGuBlendFunc(GU_ADD, GU_FIX, GU_ONE_MINUS_SRC_COLOR, 0, 0);
	/* far off, the fog leaves the frame alone */
	sceGuFog(fog_near, fog_far, 0xFF000000);
	sceGumMatrixMode(GU_MODEL);
	world_model();
	if (kept_count && shadow_mode == 1)
	{
		/* the wall's draw repeated: its vertex array, the kept triangles'
		indices, the blob's coordinates from the positions by the texture
		matrix (u = 0.5 + 0.5/r forward.(p - position), v with left) */
		unsigned short *copy = list_take((int)kept_count * 3 * sizeof(unsigned short));

		if (copy)
		{
			ScePspFMatrix4 m;
			float k = 0.5f * shadow.inverse_radius;
			long t, n = 0;

			for (t = 0; t < triangle_count && n < kept_count; t++)
			{
				const unsigned short *triangle = indices + 3 * t;
				float c[3];
				int corner, bits = 0;

				for (corner = 0; corner < 3; corner++)
				{
					to_camera(&stream->base[triangle[corner]].x, c);
					bits |= outside(c);
				}
				if (bits)
					continue;
				copy[3 * n] = triangle[0]; copy[3 * n + 1] = triangle[1]; copy[3 * n + 2] = triangle[2];
				n++;
			}
			memset(&m, 0, sizeof(m));
			m.x.x = k * shadow.forward[0]; m.y.x = k * shadow.forward[1]; m.z.x = k * shadow.forward[2];
			m.w.x = 0.5f - k * (shadow.forward[0] * shadow.position[0] + shadow.forward[1] * shadow.position[1] + shadow.forward[2] * shadow.position[2]);
			m.x.y = k * shadow.left[0]; m.y.y = k * shadow.left[1]; m.z.y = k * shadow.left[2];
			m.w.y = 0.5f - k * (shadow.left[0] * shadow.position[0] + shadow.left[1] * shadow.position[1] + shadow.left[2] * shadow.position[2]);
			m.w.z = 1.0f;
			m.w.w = 1.0f;
			sceGumMatrixMode(GU_TEXTURE);
			sceGumLoadMatrix(&m);
			sceGumMatrixMode(GU_MODEL);
			sceGuTexMapMode(GU_TEXTURE_MATRIX, GU_POSITION, 0);
			sceGuColor(0xFF000000 | shadow.darkness);
			sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
				(int)n * 3, copy, stream->base);
			sceGuTexMapMode(GU_TEXTURE_COORDS, 0, 0);
			sceGuColor(0xFFFFFFFF);
		}
	}
	else if (kept_count)
	{
		struct shadow_vertex *copy = list_take((int)kept_count * 3 * sizeof(struct shadow_vertex));

		if (copy)
		{
			memcpy(copy, out, kept_count * 3 * sizeof(struct shadow_vertex));
			sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
				(int)kept_count * 3, NULL, copy);
		}
	}
	if (cut_count)
	{
		struct shadow_vertex *pieces = list_take(cut_count * sizeof(struct shadow_vertex));

		for (index = 0; pieces && index < cut_count; index++)
		{
			const float *c = cut_corners[index];
			unsigned int r = (unsigned int)c[5], g = (unsigned int)c[6];

			pieces[index].u = c[3];
			pieces[index].v = c[4];
			/* the darkness is grey or near it: blue follows green */
			pieces[index].color = 0xFF000000 | (g << 16) | (g << 8) | r;
			pieces[index].x = c[7];
			pieces[index].y = c[8];
			pieces[index].z = c[9];
		}
		if (pieces)
			sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
				cut_count, NULL, pieces);
	}
	sceGuFog(fog_near, fog_far, fog_color);
	sceGuDisable(GU_BLEND);
	sceGuDepthMask(GU_FALSE);
	sceGuDepthFunc(GU_GEQUAL);
	if (shadow_mode == 2)
		sceGuDepthOffset(0);
	sceGuDisable(GU_TEXTURE_2D);
	shadows_drawn++;
}

void psp_ge_shadow_draw(const void *vertices, int vertex_count, const unsigned short *indices, long triangle_count)
{
	unsigned int start = sceKernelGetSystemTimeLow();

	psp_ge_shadow_draw_body(vertices, vertex_count, indices, triangle_count);
	ge_entry_us[7] += sceKernelGetSystemTimeLow() - start;
}

void psp_ge_shadow_end(void)
{
	shadow.open = 0;
}

/* ---------- dynamic lights (build 29)

The flashlight, muzzle flashes, plasma bolts and other moving lights. The
Xbox shapes each with a cube map (the "gel") and a falloff texture; the GE
lights each receiving level vertex on the CPU instead: a falloff with
distance, the light's cone (from its definition's cosines), and the angle to
the surface, then adds the level's base texture times that over the frame,
where the depth is equal. The game still picks the lights and the level
triangles each one reaches. */

static struct
{
	float position[3], forward[3], color[3];
	float radius, inverse_radius_squared, cosine_falloff, cosine_cutoff;
	int open;
} light;
static unsigned long lights_drawn = 0;

void psp_ge_light_begin(const float *position, const float *forward, const float *color, float radius,
	float cosine_falloff, float cosine_cutoff)
{
	int k;

	light.open = frame_open && radius > 0.0f && color[0] + color[1] + color[2] > 0.01f;
	for (k = 0; k < 3; k++)
	{
		light.position[k] = position[k];
		light.forward[k] = forward[k];
		light.color[k] = color[k];
	}
	light.radius = radius;
	light.inverse_radius_squared = radius > 0.0f ? 1.0f / (radius * radius) : 0.0f;
	light.cosine_cutoff = cosine_cutoff;
	light.cosine_falloff = cosine_falloff > cosine_cutoff ? cosine_falloff : cosine_cutoff + 0.001f;
}

/* how much of the light reaches a vertex (0 to 1); cone: whether the
spotlight's cone is in it (build 36: a spotlight's cone comes from a
projected texture instead, and only the side behind the light is cut here) */
static float light_reach(const float *p, unsigned int packed_normal, int cone_in_vertex)
{
	float d[3], distance_squared, fade, inverse, cone, facing, normal[3];

	d[0] = light.position[0] - p[0];
	d[1] = light.position[1] - p[1];
	d[2] = light.position[2] - p[2];
	distance_squared = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
	fade = 1.0f - distance_squared * light.inverse_radius_squared;
	if (fade <= 0.0f)
		return 0.0f;
	fade *= fade;
	inverse = distance_squared > 1e-8f ? 1.0f / sqrtf(distance_squared) : 0.0f;
	if (light.cosine_cutoff > -0.99f)
	{
		cone = -(d[0] * light.forward[0] + d[1] * light.forward[1] + d[2] * light.forward[2]) * inverse;
		if (cone_in_vertex)
		{
			/* the cone: full inside the falloff angle, none outside the cutoff */
			if (cone <= light.cosine_cutoff)
				return 0.0f;
			if (cone < light.cosine_falloff)
				fade *= (cone - light.cosine_cutoff) / (light.cosine_falloff - light.cosine_cutoff);
		}
		else if (cone <= 0.0f)
			return 0.0f;
		else if (cone < 0.25f)
			fade *= cone * 4.0f;
	}
	unpack_normal(packed_normal, normal);
	facing = (normal[0] * d[0] + normal[1] * d[1] + normal[2] * d[2]) * inverse;
	if (facing <= 0.0f)
		return 0.0f;
	return fade * facing;
}

static unsigned int light_color(float reach)
{
	int r = (int)(light.color[0] * reach * 255.0f), g = (int)(light.color[1] * reach * 255.0f),
		b = (int)(light.color[2] * reach * 255.0f);

	return 0xFF000000 | (PSP_CLAMP_255(b) << 16) | (PSP_CLAMP_255(g) << 8) | PSP_CLAMP_255(r);
}

/* build 36: a spotlight's cone as a texture projected from the light, so its
round edge is drawn per pixel. Lit per vertex, a terrain triangle several
metres across took the cone's value at its corners only: the flashlight
showed as lit triangles (Sheepdog01, b35). The GE has one texture a pass, so the
surface's own texture can't also be multiplied in; the light carries the
texture's average colour instead (psp_tex_average), over the frame that
already shows the texture */
#define CONE_SIZE 32
#define CONE_SLOTS 4
static struct
{
	float falloff, cutoff;
	unsigned char texels[CONE_SIZE * CONE_SIZE];
} cones[CONE_SLOTS] __attribute__((aligned(16)));
/* the cone textures are 8-bit indices into a grey ramp */
static unsigned int cone_ramp[256] __attribute__((aligned(16)));
static int cones_used = 0, cone_next = 0;
static unsigned long light_cone_draws = 0, light_cone_skipped = 0;
/* the light's right and up directions (x.x/y.x/z.x and x.y/y.y/z.y) */
static ScePspFMatrix4 light_basis;
int psp_tex_average(unsigned int key, unsigned int *abgr);

static unsigned char *cone_texture(float falloff, float cutoff)
{
	int slot, x, y;
	float tangent;

	for (slot = 0; slot < cones_used; slot++)
		if (cones[slot].falloff == falloff && cones[slot].cutoff == cutoff)
			return cones[slot].texels;
	/* a new one; the oldest goes (four kinds of spotlight is plenty) */
	slot = cone_next;
	cone_next = (cone_next + 1) % CONE_SLOTS;
	if (cones_used < CONE_SLOTS)
		cones_used++;
	cones[slot].falloff = falloff;
	cones[slot].cutoff = cutoff;
	tangent = sqrtf(1.0f - cutoff * cutoff) / cutoff;
	for (y = 0; y < CONE_SIZE; y++)
		for (x = 0; x < CONE_SIZE; x++)
		{
			float sx = (x + 0.5f) / (CONE_SIZE / 2) - 1.0f, sy = (y + 0.5f) / (CONE_SIZE / 2) - 1.0f;
			float r = sqrtf(sx * sx + sy * sy) * tangent, cosine = 1.0f / sqrtf(1.0f + r * r), value;
			int v;

			value = (cosine - cutoff) / (falloff - cutoff);
			if (value < 0.0f || x == 0 || y == 0 || x == CONE_SIZE - 1 || y == CONE_SIZE - 1)
				value = 0.0f;
			if (value > 1.0f)
				value = 1.0f;
			v = (int)(value * 255.0f + 0.5f);
			cones[slot].texels[y * CONE_SIZE + x] = (unsigned char)v;
		}
	if (!cone_ramp[255])
	{
		for (x = 0; x < 256; x++)
			cone_ramp[x] = 0xFF000000 | (x << 16) | (x << 8) | x;
		sceKernelDcacheWritebackRange(cone_ramp, sizeof(cone_ramp));
	}
	sceKernelDcacheWritebackRange(cones[slot].texels, sizeof(cones[slot].texels));
	return cones[slot].texels;
}

/* the texture matrix taking a world position to the cone texture: s and t
across the cone, q the distance along the light (the GE divides by it) */
static void cone_matrix(ScePspFMatrix4 *m)
{
	float f[3] = { light.forward[0], light.forward[1], light.forward[2] }, r[3], u[3], a_s[3], a_t[3], length;
	float tangent = sqrtf(1.0f - light.cosine_cutoff * light.cosine_cutoff) / light.cosine_cutoff;
	const float *L = light.position;
	int k;

	/* any vector across the light */
	if (fabsf(f[2]) < 0.9f)
	{
		r[0] = f[1]; r[1] = -f[0]; r[2] = 0.0f;
	}
	else
	{
		r[0] = 0.0f; r[1] = f[2]; r[2] = -f[1];
	}
	length = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
	for (k = 0; k < 3; k++)
		r[k] /= length;
	u[0] = f[1] * r[2] - f[2] * r[1];
	u[1] = f[2] * r[0] - f[0] * r[2];
	u[2] = f[0] * r[1] - f[1] * r[0];
	light_basis.x.x = r[0]; light_basis.y.x = r[1]; light_basis.z.x = r[2];
	light_basis.x.y = u[0]; light_basis.y.y = u[1]; light_basis.z.y = u[2];
	for (k = 0; k < 3; k++)
	{
		a_s[k] = 0.5f * f[k] + 0.5f * r[k] / tangent;
		a_t[k] = 0.5f * f[k] + 0.5f * u[k] / tangent;
	}
	m->x.x = a_s[0]; m->x.y = a_t[0]; m->x.z = f[0]; m->x.w = 0.0f;
	m->y.x = a_s[1]; m->y.y = a_t[1]; m->y.z = f[1]; m->y.w = 0.0f;
	m->z.x = a_s[2]; m->z.y = a_t[2]; m->z.z = f[2]; m->z.w = 0.0f;
	m->w.x = -(a_s[0] * L[0] + a_s[1] * L[1] + a_s[2] * L[2]);
	m->w.y = -(a_t[0] * L[0] + a_t[1] * L[1] + a_t[2] * L[2]);
	m->w.z = -(f[0] * L[0] + f[1] * L[1] + f[2] * L[2]);
	m->w.w = 1.0f;
}

/* whether a triangle is wholly outside the pyramid round the spotlight's
cone: all its corners beyond one of the four sides, or behind the light */
static int outside_cone(const float *p0, const float *p1, const float *p2, float tangent)
{
	const float *p[3] = { p0, p1, p2 };
	ScePspFMatrix4 const *m = &light_basis;
	int k, bits = 0x1F;

	for (k = 0; k < 3 && bits; k++)
	{
		float d[3] = { p[k][0] - light.position[0], p[k][1] - light.position[1], p[k][2] - light.position[2] };
		float x = d[0] * m->x.x + d[1] * m->y.x + d[2] * m->z.x;
		float y = d[0] * m->x.y + d[1] * m->y.y + d[2] * m->z.y;
		float z = (d[0] * light.forward[0] + d[1] * light.forward[1] + d[2] * light.forward[2]) * tangent;
		int b = 0;

		if (x > z) b |= 1;
		if (x < -z) b |= 2;
		if (y > z) b |= 4;
		if (y < -z) b |= 8;
		if (z <= 0.0f) b |= 16;
		bits &= b;
	}
	return bits != 0;
}

/* one lit triangle (corners: camera space, texture coordinates, the light's
reach at 5, the world position at 7): kept, cut up, or skipped by the view */
static void light_emit(float corners[3][CORNER_FLOATS], struct shadow_vertex *out, long *kept_count, int *cut_count)
{
	int bits[3], corner;

	if (*kept_count >= SCRATCH_TRIANGLES)
		return;
	for (corner = 0; corner < 3; corner++)
		bits[corner] = outside(corners[corner]);
	if (bits[0] & bits[1] & bits[2])
		return;
	if (!(bits[0] | bits[1] | bits[2]))
	{
		for (corner = 0; corner < 3; corner++)
		{
			struct shadow_vertex *o = &out[3 * *kept_count + corner];

			o->u = corners[corner][3]; o->v = corners[corner][4];
			*(float *)&o->color = corners[corner][5]; /* coloured once the tint is known */
			o->x = corners[corner][7]; o->y = corners[corner][8]; o->z = corners[corner][9];
		}
		(*kept_count)++;
		return;
	}
	*cut_count += clip_triangle(corners, bits[0] | bits[1] | bits[2], cut_corners + *cut_count,
		MAXIMUM_CUT_CORNERS - *cut_count);
}

static void psp_ge_light_draw_body(const void *vertices, int vertex_count, const unsigned short *indices, long triangle_count,
	unsigned int base_key)
{
	struct level_stream *stream;
	struct shadow_vertex *out;
	long index, kept_count = 0, whole_count = 0;
	int cut_count = 0, alpha, spot = light.cosine_cutoff > 0.05f && light.cosine_cutoff < 0.999f;
	/* build 86, modes 1 and 2: the whole triangles' indices (mode 1), their
	reach summed for the flat colour; the nearest lit corner (mode 2) */
	unsigned short *whole = NULL;
	float reach_sum = 0.0f, nearest_squared = 1e30f;
	unsigned int average = 0xFFFFFFFF;
	unsigned char *cone = NULL;
	ScePspFMatrix4 cone_projection;
	float tangent = 0.0f, tint[3] = { 1.0f, 1.0f, 1.0f }, saved[3];

	if (!light.open || !frame_open || triangle_count <= 0 || !vertices || !indices || list_full())
		return;
	if (light_mode == 3)
	{
		lights_skipped++;
		return;
	}
	stream = level_stream_find(vertices);
	if (!stream)
		return;
	if (spot)
	{
		/* the base texture's average colour (it must be resident) */
		if (!psp_tex_bind(base_key, &alpha) || !psp_tex_average(base_key, &average))
			return;
		tint[0] = (average & 0xFF) / 255.0f;
		tint[1] = ((average >> 8) & 0xFF) / 255.0f;
		tint[2] = ((average >> 16) & 0xFF) / 255.0f;
		tangent = sqrtf(1.0f - light.cosine_cutoff * light.cosine_cutoff) / light.cosine_cutoff;
		cone_matrix(&cone_projection);
	}
	else if (!psp_tex_bind(base_key, &alpha))
		return;
	out = scratch_vertices;
	if (light_mode == 1)
		whole = list_take((int)triangle_count * 3 * sizeof(unsigned short));
	for (index = 0; index < triangle_count && kept_count < SCRATCH_TRIANGLES; index++)
	{
		const unsigned short *triangle = indices + 3 * index;
		float corners[3][CORNER_FLOATS] = { { 0.0f } }, reach[3];
		int corner;
		const float *source[3];

		if (triangle[0] >= vertex_count || triangle[1] >= vertex_count || triangle[2] >= vertex_count)
			return;
		/* build 68: from the copies (position, and the packed normal kept
		beside them), not the map's vertices, which may be in the pack */
		for (corner = 0; corner < 3; corner++)
			source[corner] = &stream->base[triangle[corner]].x;
		if (spot && outside_cone(source[0], source[1], source[2], tangent))
		{
			light_cone_skipped++;
			continue;
		}
		for (corner = 0; corner < 3; corner++)
			reach[corner] = light_reach(source[corner], stream->normals[triangle[corner]], !spot);
		if (reach[0] <= 0.0f && reach[1] <= 0.0f && reach[2] <= 0.0f)
			continue;
		for (corner = 0; corner < 3; corner++)
		{
			to_camera_corner(&stream->base[triangle[corner]].x, corners[corner]);
			corners[corner][3] = stream->base[triangle[corner]].u;
			corners[corner][4] = stream->base[triangle[corner]].v;
			corners[corner][5] = reach[corner];
			corners[corner][6] = 0.0f;
			if (light_mode == 2)
			{
				float dx = corners[corner][7] - camera_eye[0], dy = corners[corner][8] - camera_eye[1],
					dz = corners[corner][9] - camera_eye[2], d2 = dx * dx + dy * dy + dz * dz;

				if (d2 < nearest_squared)
					nearest_squared = d2;
			}
		}
		if (spot)
		{
			/* the projected cone mirrors behind the light, so the part of
			the triangle there is cut off (build 36) */
			float polygon[4][CORNER_FLOATS] = { { 0.0f } }, q[3];
			int count = 0, k;

			for (corner = 0; corner < 3; corner++)
				q[corner] = (corners[corner][7] - light.position[0]) * light.forward[0] +
					(corners[corner][8] - light.position[1]) * light.forward[1] +
					(corners[corner][9] - light.position[2]) * light.forward[2] - 0.05f;
			if (q[0] <= 0.0f && q[1] <= 0.0f && q[2] <= 0.0f)
				continue;
			if (q[0] < 0.0f || q[1] < 0.0f || q[2] < 0.0f)
			{
				for (corner = 0; corner < 3; corner++)
				{
					int next = (corner + 1) % 3;

					if (q[corner] >= 0.0f)
						memcpy(polygon[count++], corners[corner], sizeof(polygon[0]));
					if ((q[corner] >= 0.0f) != (q[next] >= 0.0f))
					{
						float t = q[corner] / (q[corner] - q[next]);

						for (k = 0; k < CORNER_FLOATS; k++)
							polygon[count][k] = corners[corner][k] + (corners[next][k] - corners[corner][k]) * t;
						count++;
					}
				}
				for (k = 1; k + 1 < count; k++)
				{
					memcpy(corners[0], polygon[0], sizeof(polygon[0]));
					memcpy(corners[1], polygon[k], sizeof(polygon[0]));
					memcpy(corners[2], polygon[k + 1], sizeof(polygon[0]));
					light_emit(corners, out, &kept_count, &cut_count);
				}
				continue;
			}
		}
		if (whole)
		{
			/* mode 1: a triangle wholly in view is drawn as the level drew it */
			if (!(outside(corners[0]) | outside(corners[1]) | outside(corners[2])))
			{
				whole[3 * whole_count] = triangle[0];
				whole[3 * whole_count + 1] = triangle[1];
				whole[3 * whole_count + 2] = triangle[2];
				whole_count++;
				reach_sum += reach[0] + reach[1] + reach[2];
				continue;
			}
		}
		light_emit(corners, out, &kept_count, &cut_count);
	}
	if (!kept_count && !cut_count && !whole_count)
		return;
	/* the light's colour, times the surface's average colour for a spotlight */
	for (index = 0; index < 3; index++)
	{
		saved[index] = light.color[index];
		light.color[index] *= tint[index];
	}
	for (index = 0; index < kept_count * 3; index++)
		out[index].color = light_color(*(float *)&out[index].color);
	sceGuEnable(GU_TEXTURE_2D);
	if (spot)
	{
		cone = cone_texture(light.cosine_falloff, light.cosine_cutoff);
		sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
		sceGuClutLoad(256 / 8, cone_ramp);
		sceGuTexMode(GU_PSM_T8, 0, 0, 0);
		sceGuTexImage(0, CONE_SIZE, CONE_SIZE, CONE_SIZE, cone);
		sceGuTexFilter(GU_LINEAR, GU_LINEAR);
		sceGuTexWrap(GU_CLAMP, GU_CLAMP);
		sceGuTexMapMode(GU_TEXTURE_MATRIX, 0, 0);
		sceGuTexProjMapMode(GU_POSITION);
		/* straight to the GE: pspgum's own texture matrix slot isn't sent */
		sceGuSetMatrix(GU_TEXTURE, &cone_projection);
		light_cone_draws++;
	}
	else
		sceGuTexWrap(GU_REPEAT, GU_REPEAT);
	sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
	if (light_mode == 2)
	{
		/* as b85's shadow test: a lift of 1% of the distance to the nearest
		lit corner, at most 0.2 units, in depth steps there */
		float distance = sqrtf(nearest_squared), lift, units;

		if (distance < projection_near)
			distance = projection_near;
		lift = 0.01f * distance;
		if (lift > 0.2f)
			lift = 0.2f;
		units = lift * 61439.0f * projection_near / (distance * distance);
		sceGuDepthFunc(GU_GEQUAL);
		sceGuDepthOffset(units < 3.0f ? 3 : units > 4096.0f ? 4096 : (int)units);
		light_nudged_draws++;
	}
	else
		sceGuDepthFunc(GU_EQUAL);
	sceGuDepthMask(GU_TRUE);
	sceGuEnable(GU_BLEND);
	sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF);
	sceGuFog(fog_near, fog_far, 0xFF000000);
	sceGumMatrixMode(GU_MODEL);
	world_model();
	if (whole_count)
	{
		/* mode 1: the level's vertex array (positions and base texture
		coordinates; a spotlight's cone comes from the texture matrix as
		before), so the GE gets the very inputs the level's pass had. No
		colour per corner in that format: the average reach, flat */
		sceGuColor(light_color(reach_sum / (float)(whole_count * 3)));
		sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
			(int)whole_count * 3, whole, stream->base);
		sceGuColor(0xFFFFFFFF);
		light_index_triangles += whole_count;
	}
	if (kept_count)
	{
		struct shadow_vertex *copy = list_take((int)kept_count * 3 * sizeof(struct shadow_vertex));

		if (copy)
		{
			memcpy(copy, out, kept_count * 3 * sizeof(struct shadow_vertex));
			sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
				(int)kept_count * 3, NULL, copy);
		}
	}
	if (cut_count)
	{
		struct shadow_vertex *pieces = list_take(cut_count * sizeof(struct shadow_vertex));

		for (index = 0; pieces && index < cut_count; index++)
		{
			const float *c = cut_corners[index];

			pieces[index].u = c[3];
			pieces[index].v = c[4];
			pieces[index].color = light_color(c[5]);
			pieces[index].x = c[7];
			pieces[index].y = c[8];
			pieces[index].z = c[9];
		}
		if (pieces)
			sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
				cut_count, NULL, pieces);
	}
	for (index = 0; index < 3; index++)
		light.color[index] = saved[index];
	if (spot)
		sceGuTexMapMode(GU_TEXTURE_COORDS, 0, 0);
	sceGuFog(fog_near, fog_far, fog_color);
	sceGuDisable(GU_BLEND);
	sceGuDepthMask(GU_FALSE);
	sceGuDepthFunc(GU_GEQUAL);
	if (light_mode == 2)
		sceGuDepthOffset(0);
	sceGuDisable(GU_TEXTURE_2D);
	lights_drawn++;
}

void psp_ge_light_draw(const void *vertices, int vertex_count, const unsigned short *indices, long triangle_count,
	unsigned int base_key)
{
	unsigned int start = sceKernelGetSystemTimeLow();

	psp_ge_light_draw_body(vertices, vertex_count, indices, triangle_count, base_key);
	ge_entry_us[6] += sceKernelGetSystemTimeLow() - start;
}

/* ---------- the screen flash (build 23): a colour over the whole picture,
lightening (taking damage, pickups), darkening or tinting it, at the
game's intensity (rasterizer_xbox_screen_effect.c's sums; max, min and
invert are drawn as the nearest of those) */

static unsigned long flashes_drawn = 0;

void psp_ge_screen_flash(short type, float intensity, float const *argb)
{
	struct hud_vertex *out;
	float a, r, g, b;
	unsigned int color, fix;

	if (!frame_open || type <= 0 || intensity <= 0.0f || list_full())
		return;
	transparent_flush();
	a = argb[0] * intensity; r = argb[1] * intensity; g = argb[2] * intensity; b = argb[3] * intensity;
	color = ((unsigned int)PSP_CLAMP_255((int)(a * 255.0f)) << 24) | (PSP_CLAMP_255((int)(b * 255.0f)) << 16) |
		(PSP_CLAMP_255((int)(g * 255.0f)) << 8) | PSP_CLAMP_255((int)(r * 255.0f));
	sceGuDisable(GU_DEPTH_TEST);
	sceGuDisable(GU_FOG);
	sceGuDisable(GU_TEXTURE_2D);
	sceGuEnable(GU_BLEND);
	switch (type)
	{
	case 2: /* darken: the colour taken away */
	case 4: /* min */
		sceGuBlendFunc(GU_REVERSE_SUBTRACT, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF);
		break;
	case 6: /* tint: the picture times (1 - (1 - colour) x intensity), plus the alpha */
		fix = (PSP_CLAMP_255((int)((1.0f - (1.0f - argb[3]) * intensity) * 255.0f)) << 16) |
			(PSP_CLAMP_255((int)((1.0f - (1.0f - argb[2]) * intensity) * 255.0f)) << 8) |
			PSP_CLAMP_255((int)((1.0f - (1.0f - argb[1]) * intensity) * 255.0f));
		color = (color & 0xFF000000) | ((color >> 24) * 0x010101);
		sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xFFFFFF, fix);
		break;
	default: /* lighten: the colour added, the picture kept by one minus its alpha */
		sceGuBlendFunc(GU_ADD, GU_FIX, GU_ONE_MINUS_SRC_ALPHA, 0xFFFFFF, 0);
		break;
	}
	out = list_take(2 * sizeof(struct hud_vertex));
	if (!out)
		return;
	memset(out, 0, 2 * sizeof(struct hud_vertex));
	out[1].x = SCREEN_WIDTH;
	out[1].y = SCREEN_HEIGHT;
	out[0].color = out[1].color = color;
	sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, out);
	sceGuDisable(GU_BLEND);
	sceGuEnable(GU_FOG);
	sceGuEnable(GU_DEPTH_TEST);
	flashes_drawn++;
}

/* ---------- decals (build 23)

Bullet holes, blood, scorch marks and the level's painted markings: the
game's decal vertices (position, then 16-bit texture coordinates), four to a
quad, one colour and an intensity a decal. The Xbox's combiners fade a decal
toward "no change" as its intensity drops; the GE does the same sums with
its blend texture function (fragment x (1 - texel) + constant x texel) and
the frame buffer's blending:
  multiply          frame x (1 - i c (1 - t))
  double multiply   frame x 2 (0.5 - i c (0.5 - t))
  alpha blend, add  t c, by alpha t.a i / added times i
Decals lie on the surfaces they mark, so they are drawn a little nearer
(the GE's depth offset) and without writing depth. */

static unsigned long decals_drawn = 0;

struct decal_vertex_ge
{
	short u, v;
	float x, y, z;
};

static int decal_pull = -1, decal_big = 0, decal_all = -1, decal_slope = -1, decal_batch = 0;
static unsigned long decals_small = 0;
#define DECAL_MINIMUM_PIXELS 2.0f /* build 58: 2 (was 3: a little too far out, Sheepdog01 b57) */


static void psp_ge_decal_body(void const *vertices, int quad_count, unsigned int key, short blend, unsigned long color,
	int intensity, int alpha_tested)
{
	const unsigned char *in = vertices;
	struct decal_vertex_ge *out;
	static const int fan[6] = { 0, 1, 2, 0, 2, 3 };
	float i = intensity * (1.0f / 255.0f), c[3];
	int quad, corner, alpha = 0, k;
	unsigned int fragment = 0xFFFFFFFF, constant = 0xFFFFFFFF, fog = fog_color;
	float decal_units = 0.0f;
	/* build 76: each quad's own slope part, applied as its own pull (one
	depth offset for the draw took its largest quad's: a stencil seen
	edge-on beside the eye gave every decal of its batch, far ones too,
	hundreds of steps; far off that is tens of units) */
	static float quad_units[256];

	if (!frame_open || !vertices || quad_count <= 0 || quad_count > 256 || intensity <= 0 || list_full() ||
		!psp_tex_bind(key, &alpha))
		return;
	c[0] = ((color >> 16) & 0xFF) * (1.0f / 255.0f);
	c[1] = ((color >> 8) & 0xFF) * (1.0f / 255.0f);
	c[2] = (color & 0xFF) * (1.0f / 255.0f);
	out = list_take(quad_count * 6 * sizeof(struct decal_vertex_ge));
	if (!out)
		return;
	{
		/* build 57: decals smaller than DECAL_MINIMUM_PIXELS across on the
		screen are left out. A distant bullet hole covers a pixel or two, and
		at 272 lines it lit a pixel one frame and none the next as the view
		moved: the blinking Sheepdog01 saw at a distance (b50-b56; a larger depth
		nudge changed nothing, so it wasn't the depth test). "decalall": all */
		int kept = 0;
		float pixels_per_unit = (SCREEN_HEIGHT * 0.5f) / tanf(projection_fov * 0.5f);

		if (decal_all < 0)
			decal_all = psp_keep("decalall");
		if (decal_slope < 0)
			decal_slope = !psp_keep("decalnoslope");
		for (quad = 0; quad < quad_count; quad++)
		{
			const float *p0 = (const float *)(in + 16 * (4 * quad)), *p2 = (const float *)(in + 16 * (4 * quad + 2));

			if (!decal_all)
			{
				float centre[3] = { (p0[0] + p2[0]) * 0.5f, (p0[1] + p2[1]) * 0.5f, (p0[2] + p2[2]) * 0.5f }, camera[3];
				float dx = p2[0] - p0[0], dy = p2[1] - p0[1], dz = p2[2] - p0[2];

				to_camera(centre, camera);
				if (camera[2] > 0.0f &&
					sqrtf(dx * dx + dy * dy + dz * dz) * pixels_per_unit < DECAL_MINIMUM_PIXELS * camera[2])
				{
					decals_small++;
					continue;
				}
			}
			for (corner = 0; corner < 6; corner++)
			{
				const unsigned char *v = in + 16 * (4 * quad + fan[corner]);
				struct decal_vertex_ge *o = &out[6 * kept + corner];

				memcpy(&o->x, v, 12);
				memcpy(&o->u, v + 12, 4);
			}
			/* build 65: the quad's own depth slope (as the level's painted
			symbols since b60), the largest of the decal's quads */
			if (decal_slope)
			{
				float q[3][3];
				float units;

				for (corner = 0; corner < 3; corner++)
					to_camera((const float *)(in + 16 * (4 * quad + corner)), q[corner]);
				units = slope_units_capped(slope_units(q[0], q[1], q[2]), q[0], q[1], q[2]) + nudge_floor;
				if (units > decal_units)
					decal_units = units;
				quad_units[kept] = units;
			}
			else
				quad_units[kept] = 0.0f;
			kept++;
		}
		if (!kept)
			return;
		quad_count = kept;
	}
	{
		/* build 58: the texture level by distance, as for the level (a hole
		on the ground swam with the ground's own texture) */
		const struct decal_vertex_ge *a = &out[0], *b = &out[1], *c = &out[2];
		float e1[3] = { b->x - a->x, b->y - a->y, b->z - a->z }, e2[3] = { c->x - a->x, c->y - a->y, c->z - a->z };
		float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
		float world_area = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		float uv_area = fabsf((float)(b->u - a->u) * (float)(c->v - a->v) - (float)(c->u - a->u) * (float)(b->v - a->v)) *
			(1.0f / 32768.0f) * (1.0f / 32768.0f);
		int width = 0, height = 0;

		psp_tex_size(key, &width, &height);
		texture_levels_by_distance(world_area > 0.0f ? sqrtf(uv_area / world_area) : 0.0f, width, height, 1.0f, tex_bias);
	}
	/* build 52: a decal lies on its surface and must win the depth test
	against it. A fixed depth offset (48 steps, build 36) is the wrong size
	at both ends of the 16-bit depth range: near the eye 48 steps are under
	a centimetre (the decal and the ground took turns as Sheepdog01 walked up to
	them), far off they are metres (wall stencils showed through objects in
	front). Each corner now moves toward the eye along its own line of sight
	by about two depth steps at its distance (a step is z^2 / (near x
	61439)): the decal stays exactly where it was on the screen, and is
	always just in front of its surface. "decaloffset": the old way */
	if (decal_pull < 0)
	{
		decal_pull = !psp_keep("decaloffset");
		/* build 55, a test: a nudge four times larger plus 1% of the
		distance. If bullet holes still flicker with it, the cause isn't
		the depth test */
		decal_big = psp_keep("decalbig");
		decal_batch = psp_keep("decalbatch");
	}
	if (decal_pull)
	{
		int index;
		float per_step = 1.0f / (projection_near * 61439.0f);

		for (index = 0; index < quad_count * 6; index++)
		{
			struct decal_vertex_ge *o = &out[index];
			float d[3] = { o->x - camera_eye[0], o->y - camera_eye[1], o->z - camera_eye[2] };
			float length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
			float steps = 2.0f + (decal_batch ? 0.0f : quad_units[index / 6]);
			float shift = decal_big ? 8.0f * length * length * per_step + 0.01f * length + 0.01f :
				steps * length * length * per_step + 0.003f;

			if (length > 2.0f * shift)
			{
				float scale = 1.0f - shift / length;

				o->x = camera_eye[0] + d[0] * scale;
				o->y = camera_eye[1] + d[1] * scale;
				o->z = camera_eye[2] + d[2] * scale;
			}
		}
	}
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexWrap(GU_CLAMP, GU_CLAMP);
	sceGuDepthMask(GU_TRUE);
	/* build 36: 48 (was 32): bullet holes still sank into walls now and then (Sheepdog01, b35).
	build 65: with the pull, the slope part on top: a scorch or bullet hole on
	ground seen at a slant lost the depth test across whole triangles (Sheepdog01's
	b64 shot 40, a scorch cut off along a ground seam; the floor's bullet
	holes flickering while the walls' didn't, b56). "decalnoslope": the pull
	alone */
	/* build 76: the slope part is in each quad's pull now; "decalbatch": the
	b65-b75 single offset for the draw */
	sceGuDepthOffset(decal_pull ? (decal_batch ? (decal_units > 4096.0f ? 4096 : (int)decal_units) : 0) : 48);
	sceGuEnable(GU_BLEND);
	switch (blend)
	{
	case 1: /* multiply */
	case 2: /* double multiply */
	{
		int r[3], e[3], half = blend == 2;

		for (k = 0; k < 3; k++)
		{
			float f = half ? 0.5f - 0.5f * i * c[k] : 1.0f - i * c[k];
			float n = half ? 0.5f + 0.5f * i * c[k] : 1.0f;

			r[k] = PSP_CLAMP_255((int)(f * 255.0f));
			e[k] = PSP_CLAMP_255((int)(n * 255.0f));
		}
		fragment = 0xFF000000 | (r[2] << 16) | (r[1] << 8) | r[0];
		constant = 0xFF000000 | (e[2] << 16) | (e[1] << 8) | e[0];
		sceGuTexFunc(GU_TFX_BLEND, GU_TCC_RGB);
		sceGuTexEnvColor(constant);
		/* the frame times twice this, or this */
		if (half)
			sceGuBlendFunc(GU_ADD, GU_DST_COLOR, GU_SRC_COLOR, 0, 0);
		else
			sceGuBlendFunc(GU_ADD, GU_DST_COLOR, GU_FIX, 0, 0);
		/* far off, fog leaves the frame as it is */
		fog = half ? 0xFF808080 : 0xFFFFFFFF;
		break;
	}
	default:
	{
		int r = PSP_CLAMP_255((int)(c[0] * 255.0f)), g = PSP_CLAMP_255((int)(c[1] * 255.0f)),
			b = PSP_CLAMP_255((int)(c[2] * 255.0f)), a = PSP_CLAMP_255(intensity);

		if (blend == 3 || blend == 7)
		{
			/* added light: scaled by the intensity, and no fog colour on top */
			r = r * intensity / 255; g = g * intensity / 255; b = b * intensity / 255;
			fog = 0xFF000000;
		}
		fragment = ((unsigned int)a << 24) | (b << 16) | (g << 8) | r;
		sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
		if (blend == 3)
			sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF);
		else
			hud_blend(blend);
		break;
	}
	}
	if (alpha_tested && alpha)
	{
		sceGuAlphaFunc(GU_GREATER, 0x7F, 0xFF);
		sceGuEnable(GU_ALPHA_TEST);
	}
	sceGuFog(fog_near, fog_far, fog);
	sceGuColor(fragment);
	sceGumMatrixMode(GU_MODEL);
	world_model();
	sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_16BIT | GU_VERTEX_32BITF | GU_TRANSFORM_3D, quad_count * 6, NULL, out);
	sceGuTexLevelMode(tex_level_mode, tex_bias);
	sceGuFog(fog_near, fog_far, fog_color);
	sceGuDisable(GU_ALPHA_TEST);
	sceGuTexEnvColor(0xFFFFFFFF);
	sceGuDisable(GU_BLEND);
	sceGuDepthOffset(0);
	sceGuDepthMask(GU_FALSE);
	sceGuDisable(GU_TEXTURE_2D);
	decals_drawn++;
}

void psp_ge_decal(void const *vertices, int quad_count, unsigned int key, short blend, unsigned long color,
	int intensity, int alpha_tested)
{
	unsigned int start = sceKernelGetSystemTimeLow();

	psp_ge_decal_body(vertices, quad_count, key, blend, color, intensity, alpha_tested);
	ge_entry_us[5] += sceKernelGetSystemTimeLow() - start;
	ge_part_us[10] += sceKernelGetSystemTimeLow() - start;
	ge_part_calls[10]++;
}

/* ---------- the motion sensor (build 23)

The Xbox draws the sensor into a small target of its own: the blips added
on black, the sweep on top (its colour added, what was there kept by its
alpha), everything outside the round mask cut away, and the target then
added to the HUD over the sensor's background. The GE does the same with a
64 x 64 target at the start of VRAM's texture space (16 KB, kept out of the
texture cache; psp_tex.c). The sensor's square runs from -1 to 1. */

#define BLIP_TARGET 0x000CC000
#define BLIP_SIZE 64
static int blips_open = 0;
static unsigned long blips_drawn = 0;

struct blip_vertex
{
	float u, v;
	unsigned int color;
	float x, y, z;
};

/* a quad on the target: the corners of the sensor's square, x right and y up */
static void blip_quad(const float *uv, const float *xy, unsigned int color)
{
	struct blip_vertex *out = list_take(4 * sizeof(struct blip_vertex));
	int corner;

	if (!out)
		return;

	for (corner = 0; corner < 4; corner++)
	{
		out[corner].u = uv[2 * corner];
		out[corner].v = uv[2 * corner + 1];
		out[corner].color = color;
		out[corner].x = (xy[2 * corner] + 1.0f) * (BLIP_SIZE / 2);
		out[corner].y = (1.0f - xy[2 * corner + 1]) * (BLIP_SIZE / 2);
		out[corner].z = 0.0f;
	}
	sceGuDrawArray(GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 4, NULL, out);
}

/* texture coordinates in the 2D mode are in texels */
static int blip_texture(unsigned int key, float *uv)
{
	int width, height, alpha, corner;

	if (!key || !psp_tex_size(key, &width, &height) || !psp_tex_bind(key, &alpha))
		return 0;
	for (corner = 0; corner < 4; corner++)
	{
		uv[2 * corner] *= width;
		uv[2 * corner + 1] *= height;
	}
	return 1;
}

static unsigned int blip_color(float red, float green, float blue)
{
	int r = (int)(red * 255.0f), g = (int)(green * 255.0f), b = (int)(blue * 255.0f);

	return 0xFF000000 | (PSP_CLAMP_255(b) << 16) | (PSP_CLAMP_255(g) << 8) | PSP_CLAMP_255(r);
}

void psp_ge_blips_begin(void)
{
	list_hud_phase();
	static const float everywhere[8] = { -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f };
	static const float nothing[8] = { 0 };

	if (!frame_open || list_full())
		return;
	/* the world's see-through parts belong to the frame, not the target */
	transparent_flush();
	sceGuDrawBufferList(GU_PSM_8888, (void *)BLIP_TARGET, BLIP_SIZE);
	sceGuOffset(2048 - BLIP_SIZE / 2, 2048 - BLIP_SIZE / 2);
	sceGuViewport(2048, 2048, BLIP_SIZE, BLIP_SIZE);
	sceGuScissor(0, 0, BLIP_SIZE, BLIP_SIZE);
	sceGuDisable(GU_DEPTH_TEST);
	sceGuDisable(GU_FOG);
	sceGuDisable(GU_BLEND);
	sceGuDisable(GU_TEXTURE_2D);
	/* black, drawn rather than cleared (a clear covers the whole screen's
	size, past the target's end) */
	blip_quad(nothing, everywhere, 0xFF000000);
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexWrap(GU_CLAMP, GU_CLAMP);
	sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
	sceGuEnable(GU_BLEND);
	blips_open = 1;
}

void psp_ge_blip(float x, float y, float radius, float red, float green, float blue, unsigned int key)
{
	float uv[8] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
	float xy[8] = { x - radius, y + radius, x + radius, y + radius, x + radius, y - radius, x - radius, y - radius };

	if (!blips_open || list_full() || !blip_texture(key, uv))
		return;
	/* added light */
	sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF);
	blip_quad(uv, xy, blip_color(red, green, blue));
	blips_drawn++;
}

void psp_ge_blips_end(float center_x, float center_y, float scale, unsigned int sweep_key, unsigned int mask_key)
{
	static const float square[8] = { -1.015625f, 1.046875f, 1.046875f, 1.046875f, 1.046875f, -1.015625f, -1.015625f, -1.015625f };

	if (!blips_open)
		return;
	blips_open = 0;
	{
		/* the sweep: its colour added, the blips under it kept by its alpha */
		float low = 0.5f - scale * 0.5f, high = 0.5f + scale * 0.5f;
		float uv[8] = { high, low, low, low, low, high, high, high };

		if (!list_full() && blip_texture(sweep_key, uv))
		{
			sceGuBlendFunc(GU_ADD, GU_FIX, GU_SRC_ALPHA, 0xFFFFFF, 0);
			blip_quad(uv, square, blip_color(0.4588f, 0.7294f, 1.0f));
		}
	}
	{
		/* the mask: what is outside the round sensor goes */
		float uv[8] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };

		if (!list_full() && blip_texture(mask_key, uv))
		{
			sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
			sceGuBlendFunc(GU_ADD, GU_FIX, GU_SRC_ALPHA, 0, 0);
			blip_quad(uv, square, 0xFFFFFFFF);
		}
	}
	/* back to the frame */
	sceGuDrawBufferList(GU_PSM_5650, draw_buffer, BUFFER_WIDTH);
	sceGuOffset(2048 - SCREEN_WIDTH / 2, 2048 - SCREEN_HEIGHT / 2);
	sceGuViewport(2048, 2048, SCREEN_WIDTH, SCREEN_HEIGHT);
	sceGuScissor(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
	if (!list_full())
	{
		/* the target, added over the sensor (42 of the game's pixels either
		side of its centre) */
		struct hud_vertex *out = list_take(2 * sizeof(struct hud_vertex));
		const float radius = 42.0f;

		if (out)
		{

		sceGuTexFlush();
		sceGuTexMode(GU_PSM_8888, 0, 0, 0);
		sceGuTexImage(0, BLIP_SIZE, BLIP_SIZE, BLIP_SIZE, (void *)(0x04000000 + BLIP_TARGET));
		sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
		sceGuTexFilter(GU_LINEAR, GU_LINEAR);
		sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF);
		out[0].u = 0.0f; out[0].v = 0.0f;
		out[1].u = BLIP_SIZE; out[1].v = BLIP_SIZE;
		out[0].color = out[1].color = 0xFFFFFFFF;
		out[0].x = (center_x - radius) * (SCREEN_WIDTH / GAME_SCREEN_WIDTH) * hud_zoom;
		out[0].y = (center_y - radius) * (SCREEN_HEIGHT / GAME_SCREEN_HEIGHT) * hud_zoom;
		out[1].x = (center_x + radius) * (SCREEN_WIDTH / GAME_SCREEN_WIDTH) * hud_zoom;
		out[1].y = (center_y + radius) * (SCREEN_HEIGHT / GAME_SCREEN_HEIGHT) * hud_zoom;
		out[0].z = out[1].z = 0.0f;
		sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, out);
		}
	}
	sceGuDisable(GU_BLEND);
	sceGuDisable(GU_TEXTURE_2D);
	sceGuEnable(GU_FOG);
	sceGuEnable(GU_DEPTH_TEST);
}

/* ---------- text (build 23)

The game draws text a character at a time from a 128 x 128 picture of
recently used characters it keeps itself (rasterizer_text.c): 16-bit,
4 bits each of alpha and colour, the way the GE reads GU_PSM_4444 too, in
plain rows in main memory. A character's pixels are written just before
it is drawn, so each draw first writes those rows out of the CPU's cache
and has the GE forget what it read of the picture before. */

static int text_open = 0;
static unsigned short *text_pixels;
static int text_width, text_height;
static float text_offset_x, text_offset_y;
static unsigned long text_characters = 0;
/* build 44: the picture changes only when the game adds a character to it
(rasterizer_text.c's cache_hardware_format_character, hooked by
psp_fixups.py), a few times a minute; until build 43 every drawn character
wrote its rows back and had the GE forget the picture: ~750 cache calls a
frame for a 60-character HUD */
static int text_changed = 1;
static unsigned long text_changes = 0;

void psp_ge_text_changed(void const *bitmap, int x, int y, int width, int height)
{
	const unsigned char *data = bitmap;
	int bitmap_width = *(const short *)(data + 4), bitmap_height = *(const short *)(data + 6), row;
	unsigned short *pixels = *(unsigned short *const *)(data + 44);

	if (!pixels || bitmap_width <= 0 || x < 0 || y < 0 || width <= 0)
		return;
	if (x + width > bitmap_width)
		width = bitmap_width - x;
	if (y + height > bitmap_height)
		height = bitmap_height - y;
	/* whole cache lines: the row's first and last lines are shared with
	the characters either side, which the game wrote earlier */
	for (row = 0; row < height; row++)
	{
		unsigned int start = (unsigned int)(pixels + (y + row) * bitmap_width + x) & ~63u;
		unsigned int end = ((unsigned int)(pixels + (y + row) * bitmap_width + x + width) + 63) & ~63u;

		sceKernelDcacheWritebackRange((void *)start, end - start);
	}
	text_changed = 1;
	text_changes++;
}

int psp_ge_text_begin(void const *bitmap, short blend, float offset_x, float offset_y)
{
	list_hud_phase();
	/* the game's bitmap_data: width +4, height +6, format +12, pixels +44 */
	const unsigned char *data = bitmap;
	int width = *(const short *)(data + 4), height = *(const short *)(data + 6), format = *(const short *)(data + 12);
	void *pixels = *(void *const *)(data + 44);

	if (!frame_open || !pixels || format != 9 /* A4R4G4B4 */ || width <= 0 || height <= 0 || width > 512 ||
		height > 512 || list_full())
		return 0;
	transparent_flush();
	if (text_pixels != pixels)
		text_changed = 1;
	text_width = width;
	text_height = height;
	text_offset_x = offset_x;
	text_offset_y = offset_y;
	sceGuDisable(GU_DEPTH_TEST);
	sceGuDisable(GU_FOG);
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(GU_PSM_4444, 0, 0, 0);
	sceGuTexImage(0, width, height, width, pixels);
	sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);
	sceGuTexWrap(GU_CLAMP, GU_CLAMP);
	hud_blend(blend);
	text_pixels = pixels;
	text_open = 1;
	return 1;
}

/* four corners: position, texture coordinates in texels, colour (ARGB) */
static void text_character(void const *vertices);

void psp_ge_text_character(void const *vertices)
{
	unsigned int start = sceKernelGetSystemTimeLow();

	text_character(vertices);
	text_us += sceKernelGetSystemTimeLow() - start;
}

static void text_character(void const *vertices)
{
	struct game_screen_vertex const *in = vertices;
	struct hud_vertex *out;
	int corner;

	if (!text_open || list_full())
		return;
	if (text_changed)
	{
		/* the GE's own 8 KB texture cache may hold the old picture */
		sceGuTexFlush();
		text_changed = 0;
	}
	out = list_take(4 * sizeof(struct hud_vertex));
	if (!out)
		return;
	for (corner = 0; corner < 4; corner++)
	{
		out[corner].u = in[corner].u;
		out[corner].v = in[corner].v;
		out[corner].color = argb_to_abgr(in[corner].argb, 1.0f, 1.0f, 1.0f, 1.0f);
		out[corner].x = (in[corner].x + text_offset_x) * ui_scale_x * hud_zoom + ui_offset_x;
		out[corner].y = (in[corner].y + text_offset_y) * ui_scale_y * hud_zoom;
		out[corner].z = 0.0f;
	}
	sceGuDrawArray(GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 4, NULL, out);
	text_characters++;
}

void psp_ge_text_end(void)
{
	if (!text_open)
		return;
	text_open = 0;
	sceGuDisable(GU_BLEND);
	sceGuDisable(GU_TEXTURE_2D);
	sceGuEnable(GU_FOG);
	sceGuEnable(GU_DEPTH_TEST);
}

void psp_tex_frame_end(void);
void psp_geo_frame_end(void);

/* the screenshot button (build 34; select + right in play): the frame just
drawn, read from VRAM before it is shown, as a 480 x 272 BMP in
ms0:/halo/shots. The PSP's own pixels, with no camera in the way */
volatile int psp_screenshot_requested = 0;

static void screenshot(void)
{
	static unsigned char rows[4 * SCREEN_WIDTH * 3];
	const unsigned short *frame = (const unsigned short *)(0x44000000 + (unsigned int)draw_buffer);
	unsigned char header[54];
	unsigned int image = SCREEN_WIDTH * 3 * SCREEN_HEIGHT, size = 54 + image;
	char path[64];
	SceIoStat stat;
	SceUID file;
	int number, y, x, row = 0;

	sceIoMkdir("ms0:/halo/shots", 0777);
	for (number = 0; number < 1000; number++)
	{
		snprintf(path, sizeof(path), "ms0:/halo/shots/shot%03d.bmp", number);
		if (sceIoGetstat(path, &stat) < 0)
			break;
	}
	file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
	if (file < 0)
	{
		platform_log("screenshot: can't write %s", path);
		return;
	}
	memset(header, 0, sizeof(header));
	header[0] = 'B'; header[1] = 'M';
	memcpy(header + 2, &size, 4);
	header[10] = 54; header[14] = 40;
	x = SCREEN_WIDTH; memcpy(header + 18, &x, 4);
	y = SCREEN_HEIGHT; memcpy(header + 22, &y, 4);
	header[26] = 1; header[28] = 24;
	memcpy(header + 34, &image, 4);
	sceIoWrite(file, header, sizeof(header));
	/* BMP rows run bottom up, blue green red */
	for (y = SCREEN_HEIGHT - 1; y >= 0; y--)
	{
		unsigned char *out = rows + row * SCREEN_WIDTH * 3;

		for (x = 0; x < SCREEN_WIDTH; x++)
		{
			unsigned int v = frame[y * BUFFER_WIDTH + x];

			out[3 * x] = (unsigned char)(((v >> 11) & 31) * 255 / 31);
			out[3 * x + 1] = (unsigned char)(((v >> 5) & 63) * 255 / 63);
			out[3 * x + 2] = (unsigned char)((v & 31) * 255 / 31);
		}
		if (++row == 4 || y == 0)
		{
			sceIoWrite(file, rows, row * SCREEN_WIDTH * 3);
			row = 0;
		}
	}
	sceIoClose(file);
	/* where from (build 35): the debug camera can then go back there
	(camera.txt: position, forward, up, field of view) */
	platform_log("[t %.1f] screenshot: %s (frame %lu) from %.2f %.2f %.2f looking %.2f %.2f %.2f", psp_debug_seconds(), path, frames,
		camera_eye[0], camera_eye[1], camera_eye[2], camera_forward[0], camera_forward[1], camera_forward[2]);
}

/* build 74: the frame end's parts over each heartbeat window, for the log's
frameend: line (psp_platform.c). Sheepdog01's b72/b73 runs: the step holding the
frame end took 0.8 s a frame, growing by ~15 ms each frame, with no Memory
Stick reads; the per-300-frame report never came at 1 fps */
static unsigned long win_frames = 0, win_flush_us = 0, win_writeback_us = 0, win_ge_us = 0, win_ge_worst = 0,
	win_swap_us = 0, win_after_us = 0, win_list_bytes = 0, win_triangles = 0, win_draws = 0, win_transparent = 0;
void psp_ge_frame_window(char *text, int size)
{
	unsigned long n = win_frames ? win_frames : 1;

	snprintf(text, size, "frameend: %lu frames; a frame: see-through flush %.1f ms, cache write-back %.1f, waiting for the GE %.1f (worst %.1f), swap %.1f, caches %.1f; display list %lu KB, %lu level draws, %lu triangles, %lu see-through draws",
		win_frames, win_flush_us / 1000.0 / n, win_writeback_us / 1000.0 / n, win_ge_us / 1000.0 / n, win_ge_worst / 1000.0,
		win_swap_us / 1000.0 / n, win_after_us / 1000.0 / n, win_list_bytes / 1024 / n, win_draws / n, win_triangles / n,
		win_transparent / n);
	win_frames = win_flush_us = win_writeback_us = win_ge_us = win_ge_worst = win_swap_us = win_after_us = 0;
	win_list_bytes = win_triangles = win_draws = win_transparent = 0;
}

void psp_ge_frame_end(void)
{
	unsigned int flush_mark;

	if (!frame_open)
		return;
	flush_mark = sceKernelGetSystemTimeLow();
	win_transparent += transparent_count;
	transparent_flush();
	taa_resolve();
	win_flush_us += sceKernelGetSystemTimeLow() - flush_mark;
	win_frames++;
	win_draws += draws_this_frame;
	win_triangles += triangles_this_frame;
	/* the display list's high-water mark: it has no overflow check of its own
	(build 19) */
	{
		int used = sceGuCheckList();

		win_list_bytes += used;

		if (used > list_peak)
		{
			list_peak = used;
			if (used > (int)sizeof(display_list) * 7 / 8)
				platform_log("GE: display list %d of %d bytes used", used, (int)sizeof(display_list));
		}
	}
	{
		/* build 44: the frame end's own parts, timed (the profile's
		psp_frame_account step cost 1.3 ms a frame in build 42) */
		unsigned int mark = sceKernelGetSystemTimeLow(), waited;

		sceKernelDcacheWritebackAll();
		frame_end_us[0] += sceKernelGetSystemTimeLow() - mark;
		win_writeback_us += sceKernelGetSystemTimeLow() - mark;
		mark = sceKernelGetSystemTimeLow();
		/* build 48: the finish's stall update always reaches the GE */
		stall_force = 1;
		sceGuFinish();
		/* build 35: how long the CPU waits here for the GE to finish the
		frame's list. Long waits: the GE is the limit (fill, passes); short:
		the CPU is, and drawing overlapped with the next frame's work would
		gain little */
		sceGuSync(0, 0);
		stall_force = 0;
		waited = sceKernelGetSystemTimeLow() - mark;
		ge_wait_us += waited;
		win_ge_us += waited;
		if (waited > win_ge_worst)
			win_ge_worst = waited;
		if (waited > ge_wait_worst)
			ge_wait_worst = waited;
		{
			/* build 77: the debug menu and overlay, by the CPU into the
			finished frame (psp_debug.c), after the screenshot so shots never
			show it ("menushot": before, for testing the menu itself) */
			extern void psp_debug_draw(void *vram);
			static int menu_shot = -1;

			if (menu_shot < 0)
				menu_shot = psp_keep("menushot");
			if (menu_shot)
				psp_debug_draw((void *)(0x44000000 + (unsigned int)draw_buffer));
			if (psp_screenshot_requested)
			{
				psp_screenshot_requested = 0;
				screenshot();
			}
			if (!menu_shot)
				psp_debug_draw((void *)(0x44000000 + (unsigned int)draw_buffer));
		}
		mark = sceKernelGetSystemTimeLow();
		draw_buffer = sceGuSwapBuffers();
		frame_end_us[1] += sceKernelGetSystemTimeLow() - mark;
		win_swap_us += sceKernelGetSystemTimeLow() - mark;
		frame_open = 0;
		mark = sceKernelGetSystemTimeLow();
		psp_tex_frame_end();
		/* build 68: the geometry cache's reads, and the level copies' budget */
		psp_geo_frame_end();
		if (psp_geo_open_count())
			level_streams_trim();
		stream_frame++;
		frame_end_us[2] += sceKernelGetSystemTimeLow() - mark;
		win_after_us += sceKernelGetSystemTimeLow() - mark;
	}
	if (++frames == 1 || frames % 300 == 0)
	{
		platform_log("GE: sky drawn %lu times, %lu solid parts; %lu see-through draws, %lu dropped; %lu sprites; %lu HUD quads, %lu characters, %lu blips; skinned parts %lu (%lu batches, %lu KB, %lu left rigid); level see-through %lu; decals %lu; flashes %lu; shadows %lu begun, %lu requests, %lu draws; light draws %lu (%lu with a projected cone, %lu triangles outside cones skipped; light pass test mode %d: %lu triangles as the level drew them, %lu nudged draws, %lu skipped); two-sided parts %lu; models at the near plane %lu (%lu triangles left out)",
			sky_draws, sky_parts, transparent_drawn, transparent_dropped, sprites_queued, hud_quads, text_characters,
			blips_drawn, skinned_parts, skin_batches, skin_bytes / 1024, skin_failures, level_transparent_draws, decals_drawn, flashes_drawn, shadow_begins, shadow_requests, shadows_drawn, lights_drawn, light_cone_draws, light_cone_skipped, light_mode, light_index_triangles, light_nudged_draws, lights_skipped, two_sided_draws, near_models, near_triangles_dropped);
		platform_log("GE: frame %lu, %lu level draws (%lu textured, %lu flat so far), %lu triangles; %lu cut up near the camera, %lu skipped; %lu model parts (%lu buffers converted, %lu again after the cache moved them); level streams %lu KB (%lu let go); detail passes %lu; widget sprites %lu (%lu begun, %lu untextured; %lu of %lu flare answers clear, %lu rays cast, %lu answers kept); change colour passes %lu, self-illumination %lu; meter palettes %lu worked out, %lu reused; model palettes %lu worked out, %lu reused; HUD quads %lu us a frame, characters %lu (picture changed %lu times); peaks: list %d of %d bytes, cut corners %d of %d; looking (%.2f %.2f %.2f)",
			frames, draws_this_frame, textured_draws, flat_draws, triangles_this_frame, clipped_this_frame, culled_this_frame,
			model_draws_this_frame, converted_count, reconverted_count, stream_bytes / 1024, streams_let_go, detail_passes, widget_sprites, widget_begins, widget_untextured, occlusion_visible, occlusion_tests, psp_occlusion_rays_cast, psp_occlusion_rays_kept, change_passes, glow_passes, meter_worked, meter_reused, multipurpose_worked, multipurpose_reused, hud_quad_us / (frames ? frames : 1), text_us / (frames ? frames : 1), text_changes, list_peak, (int)sizeof(display_list),
			clip_peak, MAXIMUM_CUT_CORNERS, camera_forward[0], camera_forward[1], camera_forward[2]);
		if (frames > 1)
		{
			platform_log("GE: waited for the GE %lu us a frame (worst %lu) over the last 300 frames; frame end: cache write-back %lu us, buffer swap %lu, textures %lu; draws skipped with the display list full %lu in all; see-through parts placed by the CPU %lu; buffers on several nodes drawn rigid %lu; see-through flushes under the first-person projection %lu; HUD quads that left early with the depth test off %lu",
				ge_wait_us / 300, ge_wait_worst, frame_end_us[0] / 300, frame_end_us[1] / 300, frame_end_us[2] / 300,
				list_full_skips, transparent_placed, multi_node_rigid, first_person_flushes, screen_quad_bailed);
			budget_wait_us = ge_wait_us / 300;
			ge_wait_us = ge_wait_worst = 0;
			frame_end_us[0] = frame_end_us[1] = frame_end_us[2] = 0;
			{
				/* build 37: the renderer's own CPU time a frame, by kind (the
				rest of each profile step is the game's) */
				extern unsigned long psp_vfpu_multiplies;
				char text[512];
				int length = 0, k;
				unsigned long total = 0;

				for (k = 0; k < GE_ENTRIES; k++)
				{
					length += snprintf(text + length, sizeof(text) - length, "%s%s %lu", k ? ", " : "",
						ge_entry_names[k], ge_entry_us[k] / 300);
					total += ge_entry_us[k];
					ge_entry_us[k] = 0;
				}
				platform_log("GE: renderer CPU us a frame: %s (all %lu); VFPU matrix products %lu a frame", text,
					total / 300, psp_vfpu_multiplies / 300);
				budget_renderer_us = total / 300;
				/* build 64: the angle buckets, the overlays' walls, nearauto
				and the trial splits, over the last 300 frames */
				{
					unsigned long all = 0;
					int b;

					for (b = 0; b < ANGLE_BUCKETS; b++)
						all += angle_triangles[b];
					platform_log("GE: angle buckets: %lu draws, %lu us a frame sorting; triangles by 1/cos (1, 1.4, 2, 2.8, 4, 5.7, 8): %lu%% %lu%% %lu%% %lu%% %lu%% %lu%% %lu%%; overlays: %lu asked of the collision (%lu on a wall, %lu free, %lu us in all; %d draws remembered, %lu replaced); slope nudges %lu, held to the lift limit (%d%% of the distance) %lu%s",
						angle_draws, angle_us / 300, all ? angle_triangles[0] * 100 / all : 0, all ? angle_triangles[1] * 100 / all : 0,
						all ? angle_triangles[2] * 100 / all : 0, all ? angle_triangles[3] * 100 / all : 0,
						all ? angle_triangles[4] * 100 / all : 0, all ? angle_triangles[5] * 100 / all : 0,
						all ? angle_triangles[6] * 100 / all : 0, overlay_wall_asks, overlay_on_walls, overlay_free, overlay_ask_us, overlay_backing_count, overlay_backing_full, nudges_asked, nudge_percent, nudges_clamped,
						nudge_wide > 0 ? " (nudgewide: not held)" : "");
					nudges_asked = nudges_clamped = 0;
					platform_log("GE: near plane%s: %lu frames at x4 or more (x8 %lu, x16 %lu), %lu at x2, %lu at x1 (%lu held down by an object), %lu us a frame deciding; level draws a frame: %lu wholly inside the view (no classifying), %lu classified; triangles past the detail fade %lu a frame, detail passes skipped for whole draws %lu a frame",
						near_auto ? " (nearauto)" : " (fixed)", near_auto_chosen[2], near_auto_chosen_high[0], near_auto_chosen_high[1], near_auto_chosen[1], near_auto_chosen[0],
						near_auto_by_objects, near_auto_frames ? near_auto_us / near_auto_frames : 0, level_draws_inside / 300,
						level_draws_classified / 300, angle_far_triangles / 300, detail_passes_skipped_far / 300);
					platform_log("GE: uv centring %s: %lu draws, largest shift %.0f repeats; GE serial %s",
						uv_centre > 0 ? "on" : "off", uv_centred_draws / 300, uv_shift_largest,
						ge_serial > 0 ? "on (the frame end's wait is the GE's whole busy time)" : "off");
					uv_centred_draws = 0;
					uv_shift_largest = 0.0f;
					memset(angle_triangles, 0, sizeof(angle_triangles));
					angle_draws = angle_us = 0;
					memset(near_auto_chosen, 0, sizeof(near_auto_chosen));
					memset(near_auto_chosen_high, 0, sizeof(near_auto_chosen_high));
					near_auto_by_objects = near_auto_us = near_auto_frames = 0;
					level_draws_inside = level_draws_classified = angle_far_triangles = detail_passes_skipped_far = 0;
				}
				psp_vfpu_multiplies = 0;
				{
					/* build 47: the parts, us a frame and calls a frame, and
					what one timer reading costs */
					static unsigned int timer_cost_ns = 0;
					char parts[900];
					int written = 0;

					if (!timer_cost_ns)
					{
						unsigned int t0 = sceKernelGetSystemTimeLow(), n;

						for (n = 0; n < 1000; n++)
							(void)sceKernelGetSystemTimeLow();
						timer_cost_ns = sceKernelGetSystemTimeLow() - t0 + 1;
					}
					/* two lines: a log line holds 512 characters */
					for (k = 0; k < GE_PARTS && written < (int)sizeof(parts) - 80; k++)
					{
						if (k == 4)
						{
							platform_log("GE: parts, us a frame (calls a frame): %s; one timer reading %u ns", parts, timer_cost_ns);
							written = 0;
							parts[0] = 0;
						}
						written += snprintf(parts + written, sizeof(parts) - written, "%s%s %lu (%lu)", k != 4 && k ? ", " : "",
							ge_part_names[k], ge_part_us[k] / 300, ge_part_calls[k] / 300);
						ge_part_us[k] = ge_part_calls[k] = 0;
					}
					platform_log("GE: parts, us a frame (calls a frame): %s", parts);
					/* build 48: the list's cached regions and the stall updates */
					platform_log("GE: list: %lu regions a frame written cached (%lu KB a frame, %lu lines written back); stall updates %lu a frame, %lu passed on (%s), %.1f us each (%lu timed); decals under 3 pixels left out %lu",
						cached_takes / 300, cached_bytes / 300 / 1024, cached_lines_flushed / 300, stall_calls / 300,
						stall_forwarded / 300, stall_batch == 1 ? "batched" : "each", stall_timed ? (double)stall_timed_us / stall_timed : 0.0, stall_timed, decals_small);
					cached_takes = cached_bytes = cached_lines_flushed = stall_calls = stall_forwarded = 0;
				}
			}
		}
	}
}

int psp_ge_active(void)
{
	return ge_ready;
}

/* ---------- build 87: the Media Engine's render job (psp_me.c), measure only.
Filled in below once written; until then it does nothing */
unsigned int psp_ge_me_classify(const unsigned int *args, unsigned int *results)
{
	(void)args;
	(void)results;
	return 0;
}
