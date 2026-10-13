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
#include <stddef.h>
#include <stdio.h>
#include <malloc.h>
#include <pspge.h>
#include <stdarg.h>
#include "psp_me.h" /* build 118: psp_on_me, PSP_ME_WORK, the submit/finish jobs */

void platform_log(const char *format, ...);
/* build 112: the GE layer logs through ge_log (defined with the deferral's
machinery): platform_log on the main CPU; while the work is marked as the
Media Engine's (gemesim), only a count per message, written out by the main
CPU at the frame's end, since formatting and the log's file writes take
locks and system calls. (platform_log) in parentheses is the real one */
static void ge_log(const char *format, ...);
#define platform_log(...) ge_log(__VA_ARGS__)
#define PSP_CLAMP_255(x) ((x) < 0 ? 0 : (x) > 255 ? 255 : (x))

/* build 106: the renderer's own timers (a part's or an entry's time, for
the GE lines) read the system clock, a system call, hundreds of times a
frame: "notimers" in keep.txt skips them, and their reports then read 0.
The frame's own timing, which the budget lines need, stays */
extern int psp_timers_off;
/* build 112: 0 too while the work is marked as the Media Engine's
(gemesim): the main CPU's clock is a system call the ME can't make; the ME
version will read the ME's own cycle counter */
extern volatile unsigned int psp_me_context;
static inline unsigned int ge_clock(void)
{
	return psp_timers_off || PSP_ME_WORK() ? 0 : sceKernelGetSystemTimeLow();
}

/* build 112: a range written back from the data cache for the GE to read:
the firmware's call on the main CPU, as before; while the work is marked as
the Media Engine's (gemesim), the cache instruction on each line instead (no
system call; on the ME it writes back the ME's own cache, the one holding
the data), each line written back and dropped as the list's lines are */
static void ge_writeback(const void *start, unsigned int bytes)
{
	unsigned int line, end;

	if (!PSP_ME_WORK())
	{
		sceKernelDcacheWritebackRange(start, bytes);
		return;
	}
	end = (unsigned int)start + bytes;
	for (line = (unsigned int)start & ~63u; line < end; line += 64)
		__builtin_allegrex_cache(0x1B, line);
}

#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272
#define BUFFER_WIDTH 512
#define FRAME_BYTES (BUFFER_WIDTH * SCREEN_HEIGHT * 2)
/* 256 KB (build 33): on the PSP, firing at walls filled the 128 KB list
(decals pile up, shadows copy their receivers in), and the HUD, drawn last,
was what got cut */
/* 384 KB (build 40): sustained fire at a wall still filled 256 KB (decals
copy the wall's triangles under them, every spark is a sprite) */
/* 512 KB (build 107): b106 on b30's beach filled 384 KB in the fight (models
175 KB a frame, up to 235 model and 850 decal draws a frame skipped; the
flicker Sheepdog01 saw). Before the level's first draw the models may use only
the list less the HUD's, the see-through draws' and the level's reserves
(about 200 KB of 384). The 128 KB come from the heap; the start-up buffers
psp_memory.c now places outside it gave back more than that */
#define DISPLAY_LIST_BYTES 0x80000
/* build 113: 768 KB with "list768" (b113_candidates.md #19). b112 run 1's
approach to b30's beach skipped 46 KB of model draws a frame with the list
full; b112 run 5's cliffs 4,844 draws in 30 s. The list comes from the heap,
which b111's long run left about 900 KB of untaken. Set when the GE starts */
#define DISPLAY_LIST_BYTES_LARGE 0xC0000
/* build 114: 768 KB by default, "list512" for b113's. b113's Test 2 on the
PSP: no draw skipped with the list full (Test 1: 13,518 by game time 1,200,
the trees blinking), and the heap took 5,363 of 5,888 KB by game time 4,000
with sound1792 too */
static int display_list_size = DISPLAY_LIST_BYTES_LARGE;
/* build 93: the list's size this frame (benchrun's "display list in VRAM"
puts it in VRAM's last 256 KB, which psp_tex.c keeps free during benchrun) */
#define VRAM_BENCH_START 0x001C0000
#define VRAM_BENCH_BYTES 0x40000
/* build 103: benchrun's "level vertices in VRAM": the level's vertex copies
(the arrays the GE reads) built in the 256 KB below the bench room
(psp_tex.c keeps it out of the texture cache under benchrun). b102 run 5:
the GE's main-RAM reads cost the CPU 8-18 ms a frame, texels none of it,
and b87/b93's 16-bit level vertices (half the bytes) changed nothing: if the
cost is the number of scattered fetches rather than their bytes, only
taking the fetches off the main bus shows it */
#define VRAM_VERTEX_START 0x00180000
#define VRAM_VERTEX_BYTES 0x40000
static int level_vram = 0;
static unsigned int vram_vertex_next = 0;
static unsigned long vram_vertex_refused = 0, vram_vertex_streams = 0;

/* build 111: every piece of a level copy comes from copy_alloc and goes back
through copy_free: the copies' arena first, the general heap when it has no
room (the arena is with the copies, below) */
static void *copy_alloc(unsigned int bytes, void **owner);
static void copy_free(void *pointer);

/* the arrays the GE reads (the float copies): benchrun's VRAM room first.
owner: the field that will hold the address (the arena moves blocks) */
static void *stream_alloc(unsigned int bytes, void **owner)
{
	if (level_vram)
	{
		unsigned int at = (vram_vertex_next + 63) & ~63u;

		if (at + bytes <= VRAM_VERTEX_BYTES)
		{
			vram_vertex_next = at + bytes;
			vram_vertex_streams++;
			return (void *)(0x04000000 + VRAM_VERTEX_START + at);
		}
		vram_vertex_refused++;
	}
	return copy_alloc(bytes, owner);
}

static void stream_free(void *pointer)
{
	/* VRAM is given back all at once, when the level's copies are let go */
	if (((unsigned int)pointer & 0x0F000000u) == 0x04000000u)
		return;
	copy_free(pointer);
}
static int list_bytes = DISPLAY_LIST_BYTES;

/* build 93: benchrun's bus tests. The CPU builds every frame the same way;
these only stop the GE from drawing a kind of draw (and so from reading its
vertices): GE_SKIP_LEVEL the level's draws, GE_SKIP_MODELS the models'
parts, GE_SKIP_ALL every draw (the VRAM-only load). ge_category says which
hook is drawing */
#define GE_SKIP_LEVEL 1
#define GE_SKIP_MODELS 2
#define GE_SKIP_ALL 4
static int ge_skip = 0, ge_category = 0;
static unsigned long ge_skipped_draws = 0;

/* build 112: the ceiling bench's "GE layer skipped" (psp_bench.h). Where the
bus tests above keep the CPU's work and stop the GE, this drops every draw the
engine hands this file, on arrival: the engine still does all of its own
drawing work, and this file's CPU work and the GE's reading are gone. What it
saves is the most that moving this file's work to the Media Engine could save
(an upper bound: the ME's version keeps the GE's reading) */
#include "../include/psp_bench.h" /* the plain-C units have no -I for psp/include (psp_build.py) */
#include "../include/psp_seethrough.h" /* build 116: a see-through part's fade, packed with its blend */
#define GE_BENCH_SKIP(...) do { if (psp_bench_skip & PSP_SKIP_GE) return __VA_ARGS__; } while (0)

/* ---------- build 112: the GE layer's calls deferred (keep word "gedefer")

The first steps toward the Media Engine building the GE's lists
(docs/big_levers.md, lever L1). Every call the engine makes that writes the
list is written down as it arrives, with each array it points to copied
(node matrices, lights, colours, centroids, a sprite group's corners, a text
character's, a screen quad's), and carried out later in the same order:
when the buffer is full, when a call that must run at once arrives (it
carries out what is waiting first: GE_DEFER_DRAIN), or at the frame's end.

Kept as pointers, since they stay put until the frame ends: the geometry
cache's blocks (protected for RECENT_FRAMES, psp_geo.c), the level's vertices
and the frame's dynamic triangle lists, the decals' vertices (written in the
tick, read while drawing).

Run at once, after what is waiting: the frame's start and end
(psp_ge_window_begin decides the near plane from the level and the player;
psp_ge_frame_end hands the list over), and the calls whose answer the engine
uses (the level vertex queries) or
that change data a waiting call reads (psp_ge_text_changed), and the map
loads. A model with more nodes than DEFER_MAX_NODES runs as it comes
(defer_paused), since the engine reuses its node matrices' buffer for the
next model; so does an array too large for the buffer.

On the main CPU the list comes out the same (the same calls, in the same
order); only the CPU's work is reordered: the engine's half for a run of
calls, then this file's half for them, so each half's code stays in the
instruction cache longer. The ME step moves the carrying out to the ME. Off
(the default): one test a call. */
#define DEFER_WORDS (64 * 1024 / 4)
#define DEFER_LARGEST (DEFER_WORDS / 4) /* a record past this runs as it comes */
#define DEFER_MAX_NODES 64
#define NODE_FLOATS 13 /* a node matrix as the game keeps it (halo_matrix): scale, three axes, position */
#define SCREEN_VERTEX_WORDS 5 /* struct game_screen_vertex: x, y, u, v, argb */
#define SPRITE_VERTEX_WORDS 6 /* struct game_sprite_vertex: x, y, z, u, v, argb */
#define METER_WORDS 7 /* struct psp_seethrough_meter (the game's rasterizer_meter_parameters) */
enum
{
	DEFER_MODEL_BEGIN = 1, DEFER_MODEL_SHAPE, DEFER_MODEL_EFFECT, DEFER_MODEL_MULTIPURPOSE, DEFER_DRAW_MODEL,
	DEFER_QUEUE_TRANSPARENT, DEFER_FOG, DEFER_WINDOW_TARGET, DEFER_LEVEL_DETAIL, DEFER_DRAW_LEVEL, DEFER_SKY,
	DEFER_SPRITES, DEFER_WIDGET_BEGIN, DEFER_WIDGET_TEXTURE, DEFER_WIDGET_OCCLUSION, DEFER_WIDGET_SPRITE,
	DEFER_QUEUE_LEVEL_TRANSPARENT, DEFER_HUD_ZOOM, DEFER_UI_CENTERED, DEFER_UI_SPACE, DEFER_SCREEN_QUAD,
	DEFER_SHADOW_BEGIN, DEFER_SHADOW_DRAW, DEFER_SHADOW_END, DEFER_LIGHT_BEGIN, DEFER_LIGHT_DRAW, DEFER_SCREEN_FLASH,
	DEFER_DECAL, DEFER_BLIPS_BEGIN, DEFER_BLIP, DEFER_BLIPS_END, DEFER_TEXT_CHARACTER, DEFER_TEXT_END,
	DEFER_TEXT_BEGIN, DEFER_CONTRAIL, DEFER_ZOOM_MASK /* build 113 */,
	DEFER_FOG_PLANAR, DEFER_SEETHROUGH_TINT, DEFER_SEETHROUGH_METER /* build 116 */,
	DEFER_MODEL_POINT_LIGHT /* build 117 */
};
typedef union
{
	unsigned int u;
	int i;
	float f;
	const void *p;
} defer_word;
static defer_word *defer_buffer = NULL;
static float *defer_nodes = NULL; /* the carried-out model's node matrices: model_nodes points here */
/* "gemesim" (with gedefer): the carrying out marked as the Media Engine's
work while it still runs on the main CPU. This word holds the game thread's
id meanwhile; the emulator's ME-strict check (PSP_MESTRICT, the lab's
emu.sh) prints every system call that thread makes then: each is a gate the
ME version must not reach (big_levers.md, L1 step 3) */
volatile unsigned int psp_me_context = 0;
static unsigned int defer_me_thread = 0;
/* and the GE's stall address: while the work is the Media Engine's, the
newest one is kept here (the call is a system call; the stall wrapper, below)
and the main CPU passes it on when the run of waiting calls ends */
static int stall_me_qid = 0;
static void *stall_me_address = NULL;
static unsigned long stall_forwarded; /* (defined with the stall wrapper) */
int __real_sceGeListUpdateStallAddr(int qid, void *stall);
static PSP_MAIN_SIDE unsigned int defer_used = 0;
static PSP_MAIN_SIDE int defer_on = 0, defer_replaying = 0, defer_paused = 0;
/* gemesim or gemeon: the carrying out is (or stands for) the Media Engine's,
so what it can't do is done when the call is written down, on the main CPU
(the see-through level triangles' wall answers) */
static int defer_me_semantics = 0;
/* build 118: "gemeoverlap" (with gedefer and gemeon): L1 step 4, the Media
Engine carrying out one half of the buffer while the main CPU writes the
next calls into the other (docs/big_levers.md, section 4). A half goes to
the ME when it is full; the main CPU waits for it only when it needs the
other half back, or at a call that runs at once (the frame's start and end,
the level vertex queries, text updates), which then runs with the ME idle,
as without the overlap. The list comes out the same: the same calls, in the
same order, carried out by one CPU at a time.
What each CPU writes while a half is out, on cache lines of its own (each
CPU's cache writes back whole 64-byte lines, so a line both write loses one
CPU's bytes): the ME, the GE layer's state, pspgu's, the list, the wanted
lists; the main CPU, the records' other half and the recorder's words
(defer_used and the rest). The emulator runs no ME code: there the half is
carried out at once on the main CPU, marked as the ME's work (gemesim), and
psp_me_window says a half is out, for the emulator's write trace (keep word
mewrites, the lab's PSP_MEWRITES), which lists every line both write */
#define DEFER_HALF (DEFER_WORDS / 2)
static PSP_MAIN_SIDE int me_overlap = 0;      /* this session overlaps (decided once) */
static PSP_MAIN_SIDE int defer_half = 0;      /* the half being written */
static PSP_MAIN_SIDE defer_word *defer_base = NULL; /* its first word */
static PSP_MAIN_SIDE unsigned int defer_limit = DEFER_WORDS; /* its words */
static PSP_MAIN_SIDE int me_out = 0;          /* the other half is out with the ME */
static PSP_MAIN_SIDE int defer_me_on = -1; /* gemeon (asked once; mebench sets it) */
static PSP_MAIN_SIDE int me_wrote = 0;        /* the ME has carried a half out since the main CPU last dropped its cache */
static PSP_MAIN_SIDE unsigned long me_overlap_runs = 0, me_overlap_waits = 0, me_overlap_wait_us = 0, me_overlap_alone = 0;
/* a line of its own: only the main CPU writes it */
volatile unsigned int psp_me_window[16] __attribute__((aligned(64)));
/* build 118: "mebench": the three ways of running the GE layer's work, in
turns of ME_BENCH_FRAMES frames in one run, each frame's time (swap to swap,
the frame pacing off) added to its way: on the main CPU as it comes (gedefer
off), on the ME with the main CPU waiting (gemeon), and overlapped
(gemeoverlap). One build and one run answer what the overlap is worth on the
PSP, in the same stretch of play (the hardware runner's golden rule) */
#define ME_BENCH_FRAMES 60
static PSP_MAIN_SIDE int me_bench = -1, me_bench_way = 0, me_bench_settled = 0;
static PSP_MAIN_SIDE unsigned long me_bench_frame = 0, me_bench_frames[3], me_bench_us[3];
/* the wall answers a record carried, while it is carried out (the replay of
DEFER_QUEUE_LEVEL_TRANSPARENT sets it; overlay_answer reads it) */
static const unsigned char *overlay_answers = NULL;
static int overlay_answers_for(const void *vertices, int vertex_count, const unsigned short *indices,
	long triangle_count, unsigned char *out);
static PSP_MAIN_SIDE unsigned long defer_records = 0, defer_drains = 0, defer_drains_full = 0, defer_big_models = 0, defer_too_large = 0;
static void defer_drain(void);
static void defer_hand_over(void); /* build 118: gemeoverlap */
static void list_write_back(const void *stall); /* build 114: listcachedall */
static void lit_forget(int what); /* build 114: statememo */
/* which calls made the waiting calls run, for the log: each is a point where
the ME's version has to catch up with the main CPU */
#define DEFER_SITES 16
static PSP_MAIN_SIDE const char *defer_site[DEFER_SITES];
static PSP_MAIN_SIDE unsigned long defer_site_drains[DEFER_SITES];
static void defer_drain_for(const char *site)
{
	int k;

	for (k = 0; k < DEFER_SITES && defer_site[k] && defer_site[k] != site; k++)
		;
	if (k < DEFER_SITES)
	{
		defer_site[k] = site;
		defer_site_drains[k]++;
	}
	defer_drain();
}
/* a call that runs at once carries out what is waiting first, so the list's
order never changes. Only the game's thread writes the list, so only it
records and carries out; a call another thread makes (the heartbeat's
psp_ge_frame_window, psp_ge_budget, psp_ge_setting) never drains */
/* build 118: the calls are written down only on the main CPU, never while
carrying out. On the Media Engine (gemeoverlap) the main CPU's own words
(defer_used, defer_replaying) say what the main CPU is doing meanwhile, not
the ME: there the test is psp_on_me(), its own stack */
#define DEFER_RECORDING() (defer_on && !defer_replaying && !psp_on_me())
#define GE_DEFER_DRAIN() do { if (!psp_on_me() && defer_used && !defer_replaying) defer_drain_for(__func__); } while (0)
/* a deferred call's own test: deferring this frame, not already carrying
out, not inside a model too big to defer, and written down */
#define GE_DEFER(record, ...) do { if (DEFER_RECORDING() && !defer_paused && (record)) return __VA_ARGS__; } while (0)

int psp_keep(const char *name);
void psp_ge_model_begin(const float *node_matrices, int node_matrix_count, float u_scale, float v_scale,
	const float *ambient, int light_count, const float *light0, const float *light1);
void psp_ge_model_shape(int first_person, const float *centroid, float radius, float near_fp, float far_fp);
void psp_ge_model_effect(int type, float intensity);
void psp_ge_model_point_light(const float *position, unsigned int color);
void psp_ge_model_multipurpose(unsigned int multipurpose_key, float const *change, float const *glow);
void psp_ge_draw_model(void *vertices, int vertex_count, int uncompressed, const unsigned short *indices,
	long index_count, int strip, unsigned int color, unsigned int base_key, float u_scale, float v_scale,
	int alpha_tested);
void psp_ge_queue_transparent(void *vertices, int vertex_count, int uncompressed, const unsigned short *indices,
	long index_count, int strip, unsigned int key, float u_scale, float v_scale, float u_offset, float v_offset,
	short blend, const float *centroid);
void psp_ge_fog(const float *color, float minimum_distance, float maximum_distance, float density);
void psp_ge_fog_planar(float red, float green, float blue, float maximum_distance, float density);
void psp_ge_seethrough_tint(unsigned int tint);
void psp_ge_seethrough_shader(unsigned int shader);
void psp_ge_seethrough_meter(const void *meter);
static void seethrough_tint_set(unsigned int tint, int doubled, unsigned int shader);
unsigned int psp_tex_shader_tint(unsigned int shader, int *doubled);
int psp_tex_resident(unsigned int key);
void psp_ge_window_target(int target);
void psp_ge_level_detail(unsigned int primary_key, float primary_u, float primary_v,
	unsigned int micro_key, float micro_u, float micro_v);
void psp_ge_draw_level(const void *vertices, const void *lightmap_vertices, int vertex_count,
	const unsigned short *indices, long triangle_count, unsigned int color,
	unsigned int base_key, unsigned int lightmap_key, int alpha_tested);
int psp_ge_contrail(void const *vertices, int vertex_count, unsigned int bitmap_key, short blend);
void psp_ge_zoom_mask(unsigned int key, unsigned int strength);
void psp_ge_sky(int on);
int psp_ge_sprites(void const *vertices, int sprite_count, unsigned int bitmap_key, short blend, float const *view_to_world);
void psp_ge_widget_begin(int depth_test);
void psp_ge_widget_texture(unsigned int key);
void psp_ge_widget_occlusion(int visible);
void psp_ge_widget_sprite(float const *point, float radius, float scale_x, float scale_y, float rotation,
	unsigned long color);
void psp_ge_queue_level_transparent(const void *vertices, int vertex_count, const unsigned short *indices,
	long triangle_count, unsigned int key, float u_scale, float v_scale, float u_offset, float v_offset, short blend,
	const float *centroid);
void psp_ge_hud_zoom(float zoom);
void psp_ge_ui_centered(int centered);
void psp_ge_ui_space(int width, int height);
void psp_ge_screen_quad(void const *vertices, unsigned int key, short blend, float red, float green, float blue,
	float fade, float offset_x, float offset_y, void const *meter_parameters, int wrapped, float u_scale, float v_scale);
void psp_ge_shadow_begin(const float *matrix, const float *color, float radius);
void psp_ge_shadow_draw(const void *vertices, int vertex_count, const unsigned short *indices, long triangle_count);
void psp_ge_shadow_end(void);
void psp_ge_light_begin(const float *position, const float *forward, const float *color, float radius,
	float cosine_falloff, float cosine_cutoff);
void psp_ge_light_draw(const void *vertices, int vertex_count, const unsigned short *indices, long triangle_count,
	unsigned int base_key);
void psp_ge_screen_flash(short type, float intensity, float const *argb);
void psp_ge_decal(void const *vertices, int quad_count, unsigned int key, short blend, unsigned long color,
	int intensity, int alpha_tested, int permanent);
void psp_ge_blips_begin(void);
void psp_ge_blip(float x, float y, float radius, float red, float green, float blue, unsigned int key);
void psp_ge_blips_end(float center_x, float center_y, float scale, unsigned int sweep_key, unsigned int mask_key);
void psp_ge_text_character(void const *vertices);
void psp_ge_text_end(void);
int psp_ge_text_begin(void const *bitmap, short blend, float offset_x, float offset_y);

/* at each frame's start: whether this frame defers (the keep word, asked
once; the buffer taken from the heap the first time) */
static void defer_frame_begin(void)
{
	static int asked = -1;

	if (asked < 0)
	{
		/* build 118: mebench takes gedefer and gemeon with it */
		me_bench = psp_keep("mebench");
		asked = psp_keep("gedefer") || me_bench;
		if (asked)
		{
			defer_buffer = memalign(64, DEFER_WORDS * sizeof(defer_word));
			defer_nodes = memalign(64, DEFER_MAX_NODES * NODE_FLOATS * sizeof(float));
			if (!defer_buffer || !defer_nodes)
			{
				free(defer_buffer);
				free(defer_nodes);
				defer_buffer = NULL;
				defer_nodes = NULL;
				asked = 0;
				platform_log("GE: gedefer: no room for its buffer, off");
			}
			else
			{
				platform_log("GE: gedefer: the GE layer's calls deferred, %u KB of buffer",
					(unsigned int)(DEFER_WORDS * sizeof(defer_word) / 1024));
				if (psp_keep("gemesim"))
				{
					defer_me_thread = (unsigned int)sceKernelGetThreadId();
					platform_log("GE: gemesim: carrying out marked as the Media Engine's (thread %08x)", defer_me_thread);
					/* the check's own test: one known system call while
					marked, which the emulator's mestrict.txt must list
					(sceKernelGetSystemTimeLow from here), so a run with no
					other line proves the gates rather than a dead check */
					psp_me_context = defer_me_thread;
					(void)sceKernelGetSystemTimeLow();
					psp_me_context = 0;
				}
				defer_me_semantics = psp_keep("gemesim") || psp_keep("gemeon") || me_bench;
				if (me_bench)
					platform_log("GE: mebench: the GE layer's work on the main CPU, on the ME serially, and overlapped, in turns of %d frames; the frame pacing off",
						ME_BENCH_FRAMES);
				defer_base = defer_buffer;
				/* build 118: the overlap needs the ME's carrying out (gemeon) */
				if (psp_keep("gemeoverlap") && psp_keep("gemeon"))
				{
					me_overlap = 1;
					defer_limit = DEFER_HALF;
					platform_log("GE: gemeoverlap: the Media Engine carries out one half of the buffer (%u KB) while the main CPU writes the other",
						(unsigned int)(DEFER_HALF * sizeof(defer_word) / 1024));
				}
				else if (psp_keep("gemeoverlap"))
					platform_log("GE: gemeoverlap needs gemeon (and gedefer): off");
			}
		}
	}
	defer_on = asked;
	defer_paused = 0;
	/* build 118: mebench's way for this frame (the ME idle here: the frame's
	end carried everything out) */
	if (me_bench > 0 && defer_buffer && !defer_used)
	{
		int way = (int)((me_bench_frame++ / ME_BENCH_FRAMES) % 3);

		me_bench_settled = way == me_bench_way;
		me_bench_way = way;
		defer_on = way > 0;
		defer_me_on = 1;
		me_overlap = way == 2;
		defer_limit = me_overlap ? DEFER_HALF : DEFER_WORDS;
		defer_base = defer_buffer;
		defer_half = 0;
	}
}

/* room for a call of words words of arguments (what is waiting is carried
out first if the buffer is full); NULL if the call is too large to defer,
after carrying out what is waiting, so the call runs as it comes */
static defer_word *defer_take(int kind, unsigned int words)
{
	defer_word *at;

	if (words > DEFER_LARGEST)
	{
		defer_too_large++;
		GE_DEFER_DRAIN();
		return NULL;
	}
	if (defer_used + 1 + words > defer_limit)
	{
		defer_drains_full++;
		if (me_overlap)
			defer_hand_over(); /* build 118: the half to the ME, the other one next */
		else
			defer_drain();
	}
	at = defer_base + defer_used;
	at->u = (unsigned int)kind | (1 + words) << 8;
	defer_used += 1 + words;
	defer_records++;
	return at + 1;
}

/* n words copied, after one saying whether the pointer was NULL: 1 + n words */
static defer_word *defer_copy(defer_word *at, const void *p, int n)
{
	at->u = p != NULL;
	if (p)
		memcpy(at + 1, p, n * sizeof(defer_word));
	return at + 1 + n;
}

/* and back: the copy's address, or NULL if the pointer was */
static const void *defer_copy_back(const defer_word *at, const defer_word **next, int n)
{
	*next = at + 1 + n;
	return at->u ? (const void *)(at + 1) : NULL;
}

/* ----- the calls written down: 1 if it was, 0 if it must run now */

static int defer_model_begin(const float *node_matrices, int node_matrix_count, float u_scale, float v_scale,
	const float *ambient, int light_count, const float *light0, const float *light1)
{
	int lights = light_count < 0 ? 0 : light_count > 2 ? 2 : light_count;
	int nodes = node_matrices && node_matrix_count > 0 ? node_matrix_count : 0;
	defer_word *at;

	if (nodes > DEFER_MAX_NODES)
	{
		defer_big_models++;
		GE_DEFER_DRAIN();
		defer_paused = 1;
		return 0;
	}
	defer_paused = 0;
	at = defer_take(DEFER_MODEL_BEGIN, 5 + 4 + 7 + 7 + nodes * NODE_FLOATS);
	at[0].p = node_matrices;
	at[1].i = node_matrix_count;
	at[2].f = u_scale;
	at[3].f = v_scale;
	at[4].i = light_count;
	at = defer_copy(at + 5, ambient, 3);
	at = defer_copy(at, lights > 0 ? light0 : NULL, 6);
	at = defer_copy(at, lights > 1 ? light1 : NULL, 6);
	if (nodes)
		memcpy(at, node_matrices, nodes * NODE_FLOATS * sizeof(float));
	return 1;
}

static int defer_model_shape(int first_person, const float *centroid, float radius, float near_fp, float far_fp)
{
	defer_word *at = defer_take(DEFER_MODEL_SHAPE, 4 + 4);

	at[0].i = first_person;
	at[1].f = radius;
	at[2].f = near_fp;
	at[3].f = far_fp;
	defer_copy(at + 4, centroid, 3);
	return 1;
}

static int defer_model_effect(int type, float intensity)
{
	defer_word *at = defer_take(DEFER_MODEL_EFFECT, 2);

	at[0].i = type;
	at[1].f = intensity;
	return 1;
}

static int defer_model_point_light(const float *position, unsigned int color)
{
	defer_word *at = defer_take(DEFER_MODEL_POINT_LIGHT, 1 + 4);

	at[0].u = color;
	defer_copy(at + 1, position, 3);
	return 1;
}

static int defer_model_multipurpose(unsigned int multipurpose_key, float const *change, float const *glow)
{
	defer_word *at = defer_take(DEFER_MODEL_MULTIPURPOSE, 1 + 4 + 4);

	at[0].u = multipurpose_key;
	defer_copy(defer_copy(at + 1, change, 3), glow, 3);
	return 1;
}

static int defer_draw_model(void *vertices, int vertex_count, int uncompressed, const unsigned short *indices,
	long index_count, int strip, unsigned int color, unsigned int base_key, float u_scale, float v_scale,
	int alpha_tested)
{
	defer_word *at = defer_take(DEFER_DRAW_MODEL, 11);

	at[0].p = vertices;
	at[1].i = vertex_count;
	at[2].i = uncompressed;
	at[3].p = indices;
	at[4].i = (int)index_count;
	at[5].i = strip;
	at[6].u = color;
	at[7].u = base_key;
	at[8].f = u_scale;
	at[9].f = v_scale;
	at[10].i = alpha_tested;
	return 1;
}

static int defer_queue_transparent(void *vertices, int vertex_count, int uncompressed, const unsigned short *indices,
	long index_count, int strip, unsigned int key, float u_scale, float v_scale, float u_offset, float v_offset,
	short blend, const float *centroid)
{
	defer_word *at = defer_take(DEFER_QUEUE_TRANSPARENT, 12 + 4);

	at[0].p = vertices;
	at[1].i = vertex_count;
	at[2].i = uncompressed;
	at[3].p = indices;
	at[4].i = (int)index_count;
	at[5].i = strip;
	at[6].u = key;
	at[7].f = u_scale;
	at[8].f = v_scale;
	at[9].f = u_offset;
	at[10].f = v_offset;
	at[11].i = blend;
	defer_copy(at + 12, centroid, 3);
	return 1;
}

static int defer_fog(const float *color, float minimum_distance, float maximum_distance, float density)
{
	defer_word *at = defer_take(DEFER_FOG, 3 + 4);

	at[0].f = minimum_distance;
	at[1].f = maximum_distance;
	at[2].f = density;
	defer_copy(at + 3, color, 3);
	return 1;
}

/* a call of up to eight plain words (defer_bits for a float, DEFER_P
for a pointer: both are a word on the PSP) */
static int defer_words(int kind, int count, unsigned int a, unsigned int b, unsigned int c, unsigned int d,
	unsigned int e, unsigned int f, unsigned int g, unsigned int h)
{
	defer_word *at = defer_take(kind, count);
	unsigned int all[8] = { a, b, c, d, e, f, g, h };
	int k;

	for (k = 0; k < count; k++)
		at[k].u = all[k];
	return 1;
}
#define DEFER_P(pointer) ((unsigned int)(unsigned long)(pointer))

static unsigned int defer_bits(float value)
{
	defer_word w;

	w.f = value;
	return w.u;
}

static int defer_draw_level(const void *vertices, const void *lightmap_vertices, int vertex_count,
	const unsigned short *indices, long triangle_count, unsigned int color,
	unsigned int base_key, unsigned int lightmap_key, int alpha_tested)
{
	defer_word *at = defer_take(DEFER_DRAW_LEVEL, 9);

	at[0].p = vertices;
	at[1].p = lightmap_vertices;
	at[2].i = vertex_count;
	at[3].p = indices;
	at[4].i = (int)triangle_count;
	at[5].u = color;
	at[6].u = base_key;
	at[7].u = lightmap_key;
	at[8].i = alpha_tested;
	return 1;
}

static int defer_sprites(void const *vertices, int sprite_count, unsigned int bitmap_key, short blend,
	float const *view_to_world)
{
	int corners = vertices && sprite_count > 0 ? 4 * sprite_count : 0;
	defer_word *at = defer_take(DEFER_SPRITES, 3 + 1 + NODE_FLOATS + 1 + corners * SPRITE_VERTEX_WORDS);

	if (!at)
		return 0;
	at[0].i = sprite_count;
	at[1].u = bitmap_key;
	at[2].i = blend;
	at = defer_copy(at + 3, view_to_world, NODE_FLOATS);
	defer_copy(at, corners ? vertices : NULL, corners * SPRITE_VERTEX_WORDS);
	return 1;
}

/* build 113: a contrail's corners (render_contrails.c's contrail_vertex has
the sprite vertex's layout: x, y, z, u, v, argb) */
static int defer_contrail(void const *vertices, int vertex_count, unsigned int bitmap_key, short blend)
{
	int words = vertices && vertex_count > 0 ? vertex_count * SPRITE_VERTEX_WORDS : 0;
	defer_word *at = defer_take(DEFER_CONTRAIL, 3 + 1 + words);

	if (!at)
		return 0;
	at[0].i = vertex_count;
	at[1].u = bitmap_key;
	at[2].i = blend;
	defer_copy(at + 3, words ? vertices : NULL, words);
	return 1;
}

static int defer_widget_sprite(float const *point, float radius, float scale_x, float scale_y, float rotation,
	unsigned long color)
{
	defer_word *at = defer_take(DEFER_WIDGET_SPRITE, 5 + 4);

	at[0].f = radius;
	at[1].f = scale_x;
	at[2].f = scale_y;
	at[3].f = rotation;
	at[4].u = (unsigned int)color;
	defer_copy(at + 5, point, 3);
	return 1;
}

static int defer_queue_level_transparent(const void *vertices, int vertex_count, const unsigned short *indices,
	long triangle_count, unsigned int key, float u_scale, float v_scale, float u_offset, float v_offset, short blend,
	const float *centroid)
{
	/* under ME semantics, each triangle's wall answer goes with the call
	(worked out here: heap for the answers, the engine's collision rays);
	a word saying how many answers, then the answers, a byte each */
	long answers = defer_me_semantics && triangle_count > 0 ? triangle_count : 0;
	defer_word *at = defer_take(DEFER_QUEUE_LEVEL_TRANSPARENT, 10 + 4 + 1 + (answers + 3) / 4);

	if (!at)
		return 0;
	at[14].i = answers && overlay_answers_for(vertices, vertex_count, indices, triangle_count,
		(unsigned char *)(at + 15)) ? (int)answers : 0;

	at[0].p = vertices;
	at[1].i = vertex_count;
	at[2].p = indices;
	at[3].i = (int)triangle_count;
	at[4].u = key;
	at[5].f = u_scale;
	at[6].f = v_scale;
	at[7].f = u_offset;
	at[8].f = v_offset;
	at[9].i = blend;
	defer_copy(at + 10, centroid, 3);
	return 1;
}

static int defer_screen_quad(void const *vertices, unsigned int key, short blend, float red, float green, float blue,
	float fade, float offset_x, float offset_y, void const *meter_parameters, int wrapped, float u_scale, float v_scale)
{
	defer_word *at = defer_take(DEFER_SCREEN_QUAD, 11 + 1 + 4 * SCREEN_VERTEX_WORDS + 1 + METER_WORDS);

	at[0].u = key;
	at[1].i = blend;
	at[2].f = red;
	at[3].f = green;
	at[4].f = blue;
	at[5].f = fade;
	at[6].f = offset_x;
	at[7].f = offset_y;
	at[8].i = wrapped;
	at[9].f = u_scale;
	at[10].f = v_scale;
	at = defer_copy(at + 11, vertices, 4 * SCREEN_VERTEX_WORDS);
	defer_copy(at, meter_parameters, METER_WORDS);
	return 1;
}

static int defer_shadow_begin(const float *matrix, const float *color, float radius)
{
	defer_word *at = defer_take(DEFER_SHADOW_BEGIN, 1 + 1 + NODE_FLOATS + 4);

	at[0].f = radius;
	defer_copy(defer_copy(at + 1, matrix, NODE_FLOATS), color, 3);
	return 1;
}

static int defer_light_begin(const float *position, const float *forward, const float *color, float radius,
	float cosine_falloff, float cosine_cutoff)
{
	defer_word *at = defer_take(DEFER_LIGHT_BEGIN, 3 + 4 + 4 + 4);

	at[0].f = radius;
	at[1].f = cosine_falloff;
	at[2].f = cosine_cutoff;
	defer_copy(defer_copy(defer_copy(at + 3, position, 3), forward, 3), color, 3);
	return 1;
}

static int defer_screen_flash(short type, float intensity, float const *argb)
{
	defer_word *at = defer_take(DEFER_SCREEN_FLASH, 2 + 5);

	at[0].i = type;
	at[1].f = intensity;
	defer_copy(at + 2, argb, 4);
	return 1;
}

static int defer_decal(void const *vertices, int quad_count, unsigned int key, short blend, unsigned long color,
	int intensity, int alpha_tested, int permanent)
{
	defer_word *at = defer_take(DEFER_DECAL, 8);

	at[0].p = vertices;
	at[1].i = quad_count;
	at[2].u = key;
	at[3].i = blend;
	at[4].u = (unsigned int)color;
	at[5].i = intensity;
	at[6].i = alpha_tested;
	at[7].i = permanent;
	return 1;
}

static int defer_text_character(void const *vertices)
{
	defer_word *at = defer_take(DEFER_TEXT_CHARACTER, 1 + 4 * SCREEN_VERTEX_WORDS);

	defer_copy(at, vertices, 4 * SCREEN_VERTEX_WORDS);
	return 1;
}

/* ----- a run of records carried out in order: on the main CPU, or as the
Media Engine's job (psp_ge_me_replay) */
static void defer_replay(const defer_word *buffer, unsigned int used, int on_me)
{
	extern void psp_me_serve_audio(void); /* psp_me.c (build 113) */
	unsigned int at = 0, records = 0;

	while (at < used)
	{
		const defer_word *w = buffer + at + 1, *rest;
		int kind = buffer[at].u & 0xFF;
		const void *a, *b, *c;

		/* build 113: on the ME, an audio block asked for meanwhile is mixed
		between records rather than after the whole run (psp_me.c) */
		if (on_me && (++records & 31) == 0)
			psp_me_serve_audio();

		at += buffer[at].u >> 8;
		switch (kind)
		{
		case DEFER_MODEL_BEGIN:
		{
			int count = w[1].i, nodes = w[0].p && count > 0 ? count : 0;

			a = defer_copy_back(w + 5, &rest, 3);
			b = defer_copy_back(rest, &rest, 6);
			c = defer_copy_back(rest, &rest, 6);
			if (nodes)
				memcpy(defer_nodes, rest, nodes * NODE_FLOATS * sizeof(float));
			psp_ge_model_begin(w[0].p ? defer_nodes : NULL, count, w[2].f, w[3].f, a, w[4].i, b, c);
			break;
		}
		case DEFER_MODEL_SHAPE:
			psp_ge_model_shape(w[0].i, defer_copy_back(w + 4, &rest, 3), w[1].f, w[2].f, w[3].f);
			break;
		case DEFER_MODEL_EFFECT:
			psp_ge_model_effect(w[0].i, w[1].f);
			break;
		case DEFER_MODEL_POINT_LIGHT:
			psp_ge_model_point_light(defer_copy_back(w + 1, &rest, 3), w[0].u);
			break;
		case DEFER_MODEL_MULTIPURPOSE:
			a = defer_copy_back(w + 1, &rest, 3);
			psp_ge_model_multipurpose(w[0].u, a, defer_copy_back(rest, &rest, 3));
			break;
		case DEFER_DRAW_MODEL:
			psp_ge_draw_model((void *)w[0].p, w[1].i, w[2].i, (const unsigned short *)w[3].p, w[4].i, w[5].i,
				w[6].u, w[7].u, w[8].f, w[9].f, w[10].i);
			break;
		case DEFER_QUEUE_TRANSPARENT:
			psp_ge_queue_transparent((void *)w[0].p, w[1].i, w[2].i, (const unsigned short *)w[3].p, w[4].i, w[5].i,
				w[6].u, w[7].f, w[8].f, w[9].f, w[10].f, (short)w[11].i, defer_copy_back(w + 12, &rest, 3));
			break;
		case DEFER_FOG:
			psp_ge_fog(defer_copy_back(w + 3, &rest, 3), w[0].f, w[1].f, w[2].f);
			break;
		case DEFER_FOG_PLANAR: /* build 116 */
			psp_ge_fog_planar(w[0].f, w[1].f, w[2].f, w[3].f, w[4].f);
			break;
		case DEFER_SEETHROUGH_TINT: /* build 116 */
			seethrough_tint_set(w[0].u, (int)w[1].u, w[2].u);
			break;
		case DEFER_SEETHROUGH_METER: /* build 116 */
			psp_ge_seethrough_meter(w[0].u ? (const void *)(w + 1) : NULL);
			break;
		case DEFER_WINDOW_TARGET:
			psp_ge_window_target(w[0].i);
			break;
		case DEFER_LEVEL_DETAIL:
			psp_ge_level_detail(w[0].u, w[1].f, w[2].f, w[3].u, w[4].f, w[5].f);
			break;
		case DEFER_DRAW_LEVEL:
			psp_ge_draw_level(w[0].p, w[1].p, w[2].i, (const unsigned short *)w[3].p, w[4].i, w[5].u, w[6].u, w[7].u,
				w[8].i);
			break;
		case DEFER_SKY:
			psp_ge_sky(w[0].i);
			break;
		case DEFER_SPRITES:
			a = defer_copy_back(w + 3, &rest, NODE_FLOATS);
			psp_ge_sprites(defer_copy_back(rest, &rest, 0), w[0].i, w[1].u, (short)w[2].i, a);
			break;
		case DEFER_CONTRAIL: /* build 113 */
			psp_ge_contrail(defer_copy_back(w + 3, &rest, 0), w[0].i, w[1].u, (short)w[2].i);
			break;
		case DEFER_ZOOM_MASK: /* build 113 */
			psp_ge_zoom_mask(w[0].u, w[1].u);
			break;
		case DEFER_WIDGET_BEGIN:
			psp_ge_widget_begin(w[0].i);
			break;
		case DEFER_WIDGET_TEXTURE:
			psp_ge_widget_texture(w[0].u);
			break;
		case DEFER_WIDGET_OCCLUSION:
			psp_ge_widget_occlusion(w[0].i);
			break;
		case DEFER_WIDGET_SPRITE:
			psp_ge_widget_sprite(defer_copy_back(w + 5, &rest, 3), w[0].f, w[1].f, w[2].f, w[3].f, w[4].u);
			break;
		case DEFER_QUEUE_LEVEL_TRANSPARENT:
			overlay_answers = w[14].i ? (const unsigned char *)(w + 15) : NULL;
			psp_ge_queue_level_transparent(w[0].p, w[1].i, (const unsigned short *)w[2].p, w[3].i, w[4].u, w[5].f,
				w[6].f, w[7].f, w[8].f, (short)w[9].i, defer_copy_back(w + 10, &rest, 3));
			overlay_answers = NULL;
			break;
		case DEFER_HUD_ZOOM:
			psp_ge_hud_zoom(w[0].f);
			break;
		case DEFER_UI_CENTERED:
			psp_ge_ui_centered(w[0].i);
			break;
		case DEFER_UI_SPACE:
			psp_ge_ui_space(w[0].i, w[1].i);
			break;
		case DEFER_SCREEN_QUAD:
			a = defer_copy_back(w + 11, &rest, 4 * SCREEN_VERTEX_WORDS);
			psp_ge_screen_quad(a, w[0].u, (short)w[1].i, w[2].f, w[3].f, w[4].f, w[5].f, w[6].f, w[7].f,
				defer_copy_back(rest, &rest, METER_WORDS), w[8].i, w[9].f, w[10].f);
			break;
		case DEFER_SHADOW_BEGIN:
			a = defer_copy_back(w + 1, &rest, NODE_FLOATS);
			psp_ge_shadow_begin(a, defer_copy_back(rest, &rest, 3), w[0].f);
			break;
		case DEFER_SHADOW_DRAW:
			psp_ge_shadow_draw(w[0].p, w[1].i, (const unsigned short *)w[2].p, w[3].i);
			break;
		case DEFER_SHADOW_END:
			psp_ge_shadow_end();
			break;
		case DEFER_LIGHT_BEGIN:
			a = defer_copy_back(w + 3, &rest, 3);
			b = defer_copy_back(rest, &rest, 3);
			psp_ge_light_begin(a, b, defer_copy_back(rest, &rest, 3), w[0].f, w[1].f, w[2].f);
			break;
		case DEFER_LIGHT_DRAW:
			psp_ge_light_draw(w[0].p, w[1].i, (const unsigned short *)w[2].p, w[3].i, w[4].u);
			break;
		case DEFER_SCREEN_FLASH:
			psp_ge_screen_flash((short)w[0].i, w[1].f, defer_copy_back(w + 2, &rest, 4));
			break;
		case DEFER_DECAL:
			psp_ge_decal(w[0].p, w[1].i, w[2].u, (short)w[3].i, w[4].u, w[5].i, w[6].i, w[7].i);
			break;
		case DEFER_BLIPS_BEGIN:
			psp_ge_blips_begin();
			break;
		case DEFER_BLIP:
			psp_ge_blip(w[0].f, w[1].f, w[2].f, w[3].f, w[4].f, w[5].f, w[6].u);
			break;
		case DEFER_BLIPS_END:
			psp_ge_blips_end(w[0].f, w[1].f, w[2].f, w[3].u, w[4].u);
			break;
		case DEFER_TEXT_CHARACTER:
			psp_ge_text_character(defer_copy_back(w, &rest, 4 * SCREEN_VERTEX_WORDS));
			break;
		case DEFER_TEXT_END:
			psp_ge_text_end();
			break;
		case DEFER_TEXT_BEGIN:
			psp_ge_text_begin(w[0].p, (short)w[1].i, w[2].f, w[3].f);
			break;
		}
	}
}

/* build 112: "gemeon" (with gedefer): the run carried out as the Media
Engine's job (psp_me.c, ME_JOB_GE), L1 step 4's first form: the main CPU
waits for it, so it measures the ME on this work and proves it runs there;
overlapping comes next. args: the records, their words, the value
psp_me_context holds meanwhile (the game thread's id under gemesim, so the
emulator's ME-strict check also covers this path when it simulates the job) */
unsigned int psp_ge_me_replay(const unsigned int *args, unsigned int *results)
{
	/* build 118: on the ME itself nothing marks the work (PSP_ME_WORK sees
	its stack): psp_me_context is the main CPU's word, which a write here
	would share a cache line with while the main CPU runs on (gemeoverlap).
	Simulated (args[3] 0), the main CPU runs it: marked as before */
	if (!args[3])
		psp_me_context = args[2] ? args[2] : 1;
	defer_replay((const defer_word *)args[0], args[1], (int)args[3]);
	results[0] = args[1];
	/* build 118: gemeoverlap (args[4]): the newest stall address the run
	kept goes back in the slot (uncached), for the main CPU to pass on, and
	is forgotten here: only the ME writes stall_me_address meanwhile */
	if (args[4])
	{
		results[1] = (unsigned int)stall_me_address;
		results[2] = (unsigned int)stall_me_qid;
		stall_me_address = NULL;
	}
	/* (last: everything above is the ME's work, also when simulated) */
	if (!args[3])
		psp_me_context = 0;
	return 0;
}

static PSP_MAIN_SIDE unsigned long defer_me_jobs = 0, defer_me_failed = 0, defer_me_wait_us = 0, defer_me_own_us = 0;

/* 1 if the ME carried the run out */
static int defer_on_me(void)
{
	extern int psp_me_running(void);
	extern int psp_me_run(int which, unsigned int type, const unsigned int *args, int arg_count, unsigned int *results,
		unsigned int first_look_us);
	extern unsigned int psp_me_last_job_us(int which);
	unsigned int args[3], results[8], start;

	if (defer_me_on < 0)
		defer_me_on = psp_keep("gemeon");
	if (!defer_me_on || !psp_me_running())
		return 0;
	/* everything the job reads to RAM: the records, what they point to that
	the engine wrote, and this file's state as the main CPU left it */
	sceKernelDcacheWritebackAll();
	args[0] = (unsigned int)defer_buffer;
	args[1] = defer_used;
	args[2] = defer_me_thread;
	start = sceKernelGetSystemTimeLow();
	if (!psp_me_run(1 /* the render slot */, 4 /* ME_JOB_GE */, args, 3, results, 20))
	{
		defer_me_failed++;
		return 0;
	}
	defer_me_wait_us += sceKernelGetSystemTimeLow() - start;
	defer_me_own_us += psp_me_last_job_us(1);
	defer_me_jobs++;
	/* and back: what the ME wrote (this file's state, pspgu's, the list)
	read fresh from RAM */
	sceKernelDcacheWritebackInvalidateAll();
	return 1;
}

/* ----- build 118: gemeoverlap's two halves */

/* the half out with the ME, waited for, and the GE given what it wrote */
static void defer_wait_me(void)
{
	extern unsigned int psp_me_last_job_us(int which);
	unsigned int results[ME_JOB_RESULTS] = { 0 }, start;

	if (!me_out)
		return;
	me_out = 0;
	start = sceKernelGetSystemTimeLow();
	if (!psp_me_finish(ME_SLOT_RENDER, results))
	{
		/* the ME stopped (HOME, sleep, a job past 500 ms) with the half
		perhaps half carried out: the main CPU goes on alone from here,
		and this frame may draw wrong */
		psp_me_window[0] = 0;
		defer_me_failed++;
		me_overlap = 0;
		/* the half being written moves to the buffer's start, where the
		calls are written from now on */
		if (defer_base != defer_buffer)
			memmove(defer_buffer, defer_base, defer_used * sizeof(defer_word));
		defer_base = defer_buffer;
		defer_half = 0;
		defer_limit = DEFER_WORDS;
		platform_log("GE: gemeoverlap: the Media Engine failed with half the buffer out; the main CPU carries the calls out from here");
		return;
	}
	psp_me_window[0] = 0;
	me_overlap_waits++;
	me_overlap_wait_us += sceKernelGetSystemTimeLow() - start;
	defer_me_own_us += psp_me_last_job_us(ME_SLOT_RENDER);
	defer_me_jobs++;
	if (results[1])
	{
		stall_forwarded++;
		__real_sceGeListUpdateStallAddr((int)results[2], (void *)results[1]);
	}
}

/* the half being written handed to the ME, and the other one written next;
carried out here if the ME can't take it */
static void defer_hand_over(void)
{
	unsigned int args[5];

	defer_wait_me(); /* one half out at a time: the other is free after this */
	if (!defer_used)
		return;
	if (me_overlap && psp_me_running())
	{
		/* the records, what they point to that the engine wrote, and this
		file's state as the main CPU left it, to RAM for the ME (which drops
		its own cache as the job starts) */
		sceKernelDcacheWritebackAll();
		args[0] = (unsigned int)defer_base;
		args[1] = defer_used;
		args[2] = defer_me_thread;
		args[3] = 0; /* (psp_me.c sets it: on the ME or not) */
		args[4] = 1;
		psp_me_window[0] = 1;
		/* simulated, the job runs inside psp_me_submit, on this CPU */
		defer_replaying = 1;
		if (psp_me_submit(ME_SLOT_RENDER, ME_JOB_GE, args, 5))
		{
			defer_replaying = 0;
			me_out = 1;
			me_wrote = 1;
			me_overlap_runs++;
			defer_half ^= 1;
			defer_base = defer_buffer + defer_half * DEFER_HALF;
			defer_used = 0;
			return;
		}
		defer_replaying = 0;
		psp_me_window[0] = 0;
	}
	/* the ME can't take it: carried out here, as gedefer alone, after
	dropping the main CPU's stale copies of what the ME wrote */
	me_overlap_alone++;
	if (me_wrote)
	{
		sceKernelDcacheWritebackInvalidateAll();
		me_wrote = 0;
	}
	defer_replaying = 1;
	psp_me_context = defer_me_thread;
	defer_replay(defer_base, defer_used, 0);
	psp_me_context = 0;
	defer_replaying = 0;
	if (stall_me_address)
	{
		stall_forwarded++;
		__real_sceGeListUpdateStallAddr(stall_me_qid, stall_me_address);
		stall_me_address = NULL;
	}
	defer_used = 0;
}

/* ----- everything waiting, carried out in order */
static void defer_drain(void)
{
	if (me_overlap)
	{
		/* build 118: the half being written out too, then both done, and
		what the ME wrote (this file's state, pspgu's, the list) read fresh
		from RAM before the call that runs at once runs here */
		defer_hand_over();
		defer_wait_me();
		if (me_wrote)
		{
			sceKernelDcacheWritebackInvalidateAll();
			me_wrote = 0;
		}
		defer_drains++;
		return;
	}
	defer_replaying = 1;
	if (!defer_on_me())
	{
		psp_me_context = defer_me_thread;
		defer_replay(defer_buffer, defer_used, 0);
		psp_me_context = 0;
	}
	/* the GE given what the run wrote (the newest stall address it kept) */
	if (stall_me_address)
	{
		stall_forwarded++;
		list_write_back(stall_me_address); /* build 114: listcachedall */
		__real_sceGeListUpdateStallAddr(stall_me_qid, stall_me_address);
		stall_me_address = NULL;
	}
	defer_used = 0;
	defer_replaying = 0;
	defer_drains++;
}

/* the messages the Media Engine's work logged: each format with a count */
#define ME_LOG_FORMATS 16
static const char *me_log_format[ME_LOG_FORMATS];
static unsigned long me_log_count[ME_LOG_FORMATS], me_log_lost = 0;

static void ge_log(const char *format, ...)
{
	char line[512]; /* the log's own line limit (psp_platform.c) */
	va_list arguments;

	if (PSP_ME_WORK())
	{
		int k;

		for (k = 0; k < ME_LOG_FORMATS && me_log_format[k] && me_log_format[k] != format; k++)
			;
		if (k < ME_LOG_FORMATS)
		{
			me_log_format[k] = format;
			me_log_count[k]++;
		}
		else
			me_log_lost++;
		return;
	}
	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	(platform_log)("%s", line);
}

/* at the frame's end, on the main CPU: what the Media Engine's work logged */
static void me_log_write(void)
{
	int k;

	for (k = 0; k < ME_LOG_FORMATS && me_log_format[k]; k++)
	{
		(platform_log)("GE (the Media Engine's work, %lu times): %s", me_log_count[k], me_log_format[k]);
		me_log_format[k] = NULL;
		me_log_count[k] = 0;
	}
	if (me_log_lost)
		(platform_log)("GE (the Media Engine's work): %lu more messages of other kinds", me_log_lost);
	me_log_lost = 0;
}
static int list_in_vram = 0, vram_busy = 0, vram_busy_calibrating = 0, vram_busy_sprites = 8;
static unsigned int vram_busy_target_us = 15000;

static inline int ge_draw_skipped(void)
{
	if (!ge_skip || !((ge_skip & GE_SKIP_ALL) || (ge_skip & ge_category)))
		return 0;
	ge_skipped_draws++;
	return 1;
}
/* every draw in this file goes through the gate (a parenthesized name,
(sceGuDrawArray)(...), is the real call: the VRAM load) */
/* build 115: "gereads" counts what each draw makes the GE read (ge_reads_note,
after the copies' arena below); the arguments are taken once */
static int ge_reads_on = 0;
static void ge_reads_note(int vtype, int count, const void *indices, const void *vertices);
/* build 116: "vrammodelsN": a model buffer's VRAM copy, when it has one
(vm_redirect, by the model buffers' copies below) */
static int vm_on = -1; /* -1: decided at the next frame (vm_reset) */
static const void *vm_redirect(const void *vertices, int count);
static void vm_reset(void);
static void vm_frame_end(void);
static void vm_report(void);
#define sceGumDrawArray(p, v, c, i, d) do { if (!ge_draw_skipped()) { int ge_v_ = (v), ge_c_ = (c); \
	const void *ge_i_ = (i), *ge_d_ = (d); if (vm_on > 0) ge_d_ = vm_redirect(ge_d_, ge_c_); \
	if (ge_reads_on > 0) ge_reads_note(ge_v_, ge_c_, ge_i_, ge_d_); \
	sceGumDrawArray(p, ge_v_, ge_c_, ge_i_, ge_d_); } } while (0)
#define sceGuDrawArray(p, v, c, i, d) do { if (!ge_draw_skipped()) { int ge_v_ = (v), ge_c_ = (c); \
	const void *ge_i_ = (i), *ge_d_ = (d); if (vm_on > 0) ge_d_ = vm_redirect(ge_d_, ge_c_); \
	if (ge_reads_on > 0) ge_reads_note(ge_v_, ge_c_, ge_i_, ge_d_); \
	sceGuDrawArray(p, ge_v_, ge_c_, ge_i_, ge_d_); } } while (0)
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

/* build 116 (b113 #6): the world drawn with the GE's ordered dither, as a
16-bit frame needs to keep the Xbox's 32-bit gradients: without it the
dark areas and the multiply passes (lightmap, detail maps) went in visible
steps. The HUD and the 2D items keep flat colours (a dithered flat colour
is a speckle). Keep word "nodither": as before.

build 117: the dither only where a draw lays down a new colour. A blended
draw reads the frame back and rounds it again, and the dither moves each
pixel by the offset of its place in the 4x4 matrix: the same offset in
every pass. Where a pass changes a pixel little (a layer of smoke, a
shadow's quad outside the shadow, a detail map near its neutral), the
rounding goes the same way pass after pass, and the passes build a fixed
pattern: b116's test 1 showed it on shadows and particles, swinging up to
140 of 255 under the Pelican's dust [M]. So the dither is on for draws that
don't blend, and for the level's lightmap pass, which sets the level's
brightness (where b113 #6's steps showed); off for every other blended
draw. Keep word "ditherall": b116's dither on every draw of the world */
static int dither_off = -1, dither_all = -1;
static int dither_world = 0; /* the world's turn: the dither may be on */
static int dither_now = -1, blend_now = 0; /* what the GE was last given */

static void dither_set(int on)
{
	if (on == dither_now)
		return;
	if (on)
		sceGuEnable(GU_DITHER);
	else
		sceGuDisable(GU_DITHER);
	dither_now = on;
}

/* the world's turn begins (1) or ends (0) */
static void ge_dither(int on)
{
	static const ScePspIMatrix4 matrix = { { -4, 0, -3, 1 }, { 2, -2, 3, -1 }, { -3, 1, -4, 0 }, { 3, -1, 2, -2 } };

	if (dither_off < 0)
	{
		dither_off = psp_keep("nodither");
		dither_all = psp_keep("ditherall");
	}
	if (dither_off)
		return;
	if (on)
		sceGuSetDither(&matrix);
	dither_world = on;
	dither_now = -1; /* given again, so each frame starts from a known state */
	dither_set(on && (dither_all || !blend_now));
}

/* blending on or off, and the dither with it (build 117) */
static void ge_blend(int on)
{
	if (on)
		sceGuEnable(GU_BLEND);
	else
		sceGuDisable(GU_BLEND);
	blend_now = on;
	if (dither_world)
		dither_set(dither_all || !on);
}

/* blending on with the dither kept: the level's lightmap pass (build 117) */
static void ge_blend_dithered(void)
{
	sceGuEnable(GU_BLEND);
	blend_now = 1;
	if (dither_world)
		dither_set(1);
}

/* the HUD's turn: the reserve is its. The world's see-through draws are
flushed first, inside the world's share (build 34: in build 33 the flush
came after the switch and the effects of a firefight ate the HUD's room) */
static void list_hud_phase(void)
{
	if (hud_phase)
		return;
	transparent_flush();
	taa_resolve();
	ge_dither(0); /* build 116 */
	hud_phase = 1;
	list_limit = list_bytes;
}

int psp_keep(const char *name);

/* ---------- build 103: what fills the display list, and the level's room

Sheepdog01's b102 runs 3 and 6 (The Library): in the Flood fight the level
vanished (0 level draws while 340 KB of the list was in use; 25,000 draws
skipped), and in run 6 about 1,100 draws were skipped every frame for the
last half of the run, with Sheepdog01 idling in a corner. Which kind of draw fills
the list wasn't logged. Each public entry now says what kind of draw it is
(ge_kind); the bytes the list grows by are charged to the kind running,
with its skipped draws; the report gives the average and the fullest frame,
and the order the kinds came in that frame.

The level's room: until the level's first draw of the frame, every other
kind stops LEVEL_RESERVE bytes earlier, so what is drawn before the level
can't leave it nothing (run 3's 0 level draws). Keep word "nolevelreserve":
as before */
enum { KIND_OTHER, KIND_SKY, KIND_LEVEL, KIND_LIGHT, KIND_LEVEL_SEETHROUGH, KIND_MODEL, KIND_SEETHROUGH_QUEUE,
	KIND_DECAL, KIND_SPRITE, KIND_WIDGET, KIND_SHADOW, KIND_SEETHROUGH_FLUSH, KIND_SCREEN, KIND_HUD, KINDS };
static const char *const kind_names[KINDS] = { "other", "sky", "level", "lights", "level see-through (stencil bake)",
	"models", "see-through queued", "decals", "sprites (particles)", "flares", "shadows", "see-through drawn", "screen quads", "HUD" };
static const char kind_letters[KINDS] = { 'o', 'k', 'L', 'l', 'B', 'M', 'q', 'd', 'p', 'f', 's', 'T', 'Q', 'H' };
#define LEVEL_RESERVE 0x18000 /* 96 KB */
static int ge_kind_now = KIND_OTHER, level_reserve_on = -1, level_begun = 0;
static unsigned int kind_mark = 0, kind_seen = 0;
static unsigned int kind_frame_bytes[KINDS], kind_peak_bytes[KINDS], kind_frame_skips[KINDS], kind_peak_skips[KINDS];
static unsigned long long kind_bytes[KINDS];
static unsigned long kind_skips[KINDS], kind_frames = 0, level_reserve_held = 0;
static unsigned int kind_peak_total = 0;
/* build 107: what the models' share is made of (b106 on b30's beach: 175 KB a
frame of models, and up to 235 model draws a frame skipped). Each skinned
batch loads its slots' bone matrices (13 words each) for every pass */
static unsigned long model_bone_loads = 0, model_take_bytes = 0, model_part_calls = 0;
/* build 107: bone loads go into sub-lists that each pass calls (model_geometry) */
static unsigned long model_bone_calls = 0, model_bone_sub_bytes = 0;
/* build 107: rigid parts near the eye, and those drawn whole (rigid_part_whole);
bending batches near the eye drawn whole, with no copy */
static unsigned long near_parts = 0, near_parts_whole = 0, near_batches_whole = 0;
/* build 118: the near path's sphere test (batch_sphere_inside) */
static int near_sphere_full = -1, near_sphere_check = 0;
static unsigned long near_batches_by_sphere = 0, near_sphere_mismatches = 0;
/* build 119: the near path's run test (near_cluster_verdict), and the runs'
spheres on the heap */
#define NEAR_CLUSTER_TRIANGLES 16
static int near_cluster_full = -1, near_cluster_check = 0;
static unsigned long near_cluster_kept = 0, near_cluster_dropped = 0, near_cluster_mixed = 0, near_cluster_mismatches = 0;
static unsigned long near_cluster_bytes = 0;
static int skin_axis_loop = -1, skin_axis_check = 0; /* build 119: model_geometry's corner sums, "skinaxisloop" */
static unsigned long skin_axis_checked = 0, skin_axis_differ = 0;
static char kind_order[KINDS + 1], kind_peak_order[KINDS + 1];
static int kind_order_length = 0;

static void gebusy_kind_cut(int kind);
static void ge_kind(int kind)
{
	unsigned int now;

	gebusy_kind_cut(kind); /* build 115 */
	now = sceGuCheckList();

	if (now > kind_mark)
		kind_frame_bytes[ge_kind_now] += now - kind_mark;
	kind_mark = now;
	ge_kind_now = kind;
	if (!(kind_seen & (1u << kind)) && kind_order_length < KINDS)
	{
		kind_seen |= 1u << kind;
		kind_order[kind_order_length++] = kind_letters[kind];
	}
	if (kind == KIND_LEVEL)
		level_begun = 1;
}

static void ge_kinds_frame_begin(void)
{
	memset(kind_frame_bytes, 0, sizeof(kind_frame_bytes));
	memset(kind_frame_skips, 0, sizeof(kind_frame_skips));
	kind_mark = sceGuCheckList();
	kind_seen = 0;
	kind_order_length = 0;
	ge_kind_now = KIND_OTHER;
	level_begun = 0;
	if (level_reserve_on < 0)
		level_reserve_on = !psp_keep("nolevelreserve");
}

/* build 107: when the list is full, the smallest models on the screen go
first. b106 on b30's beach: with the list full, whichever draws came last
were skipped (up to 235 model draws a frame), a different set each frame, so
rocks, crates and trees nearby flickered in and out (Sheepdog01). Now a model whose
bounding sphere is smaller on the screen than model_size_floor (its
diameter, in pixels) isn't drawn. The floor rises by a quarter each frame
that skipped model draws for room, up to MODEL_SIZE_FLOOR_MOST, and comes
back down slowly once the list has room again, so what goes missing is the
farthest, smallest models, the same ones from frame to frame. The first-person
weapon and the sky are never left out. Keep word "nosizefloor": as before */
#define MODEL_SIZE_FLOOR_MOST 8.0f
static float model_size_floor = 0.0f, model_size_floor_peak = 0.0f, model_pixels = 1.0e9f;
static int size_floor_on = -1;
static unsigned long model_floor_left_out = 0;

static void model_size_floor_update(unsigned int total)
{
	if (size_floor_on < 0)
		size_floor_on = !psp_keep("nosizefloor");
	if (!size_floor_on)
		return;
	if (kind_frame_skips[KIND_MODEL])
		model_size_floor = model_size_floor < 2.0f ? 2.0f : model_size_floor * 1.25f;
	else if (total < (unsigned int)list_limit * 3 / 4)
		model_size_floor = model_size_floor < 1.5f ? 0.0f : model_size_floor * 0.95f;
	if (model_size_floor > MODEL_SIZE_FLOOR_MOST)
		model_size_floor = MODEL_SIZE_FLOOR_MOST;
	if (model_size_floor > model_size_floor_peak)
		model_size_floor_peak = model_size_floor;
}

static void ge_kinds_frame_end(void)
{
	unsigned int total = 0;
	int k;

	ge_kind(KIND_OTHER);
	for (k = 0; k < KINDS; k++)
	{
		kind_bytes[k] += kind_frame_bytes[k];
		kind_skips[k] += kind_frame_skips[k];
		total += kind_frame_bytes[k];
	}
	model_size_floor_update(total);
	kind_frames++;
	if (total >= kind_peak_total)
	{
		kind_peak_total = total;
		memcpy(kind_peak_bytes, kind_frame_bytes, sizeof(kind_peak_bytes));
		memcpy(kind_peak_skips, kind_frame_skips, sizeof(kind_peak_skips));
		memcpy(kind_peak_order, kind_order, kind_order_length);
		kind_peak_order[kind_order_length] = 0;
	}
}

static void ge_kinds_report(void)
{
	char line[600];
	int length, k;

	if (!kind_frames)
		return;
	length = snprintf(line, sizeof(line), "GE: display list by kind, KB a frame over %lu frames (fullest frame; draws skipped a frame):",
		kind_frames);
	for (k = 0; k < KINDS && length < (int)sizeof(line) - 60; k++)
		if (kind_bytes[k] || kind_skips[k])
			length += snprintf(line + length, sizeof(line) - length, " %s %.1f (%.1f; %.1f)", kind_names[k],
				kind_bytes[k] / 1024.0 / kind_frames, kind_peak_bytes[k] / 1024.0, (float)kind_skips[k] / kind_frames);
	platform_log("%s", line);
	if (kind_bytes[KIND_MODEL])
	{
		/* bone loads written in the list directly, or once into a sub-list (which list_take counted) */
		double frames_seen = (double)kind_frames;
		double bone = model_bone_loads * 52.0 + model_bone_sub_bytes + model_bone_calls * 8.0;

		platform_log("GE: the models' share, KB a frame: bone matrices %.1f (%.0f loads in the list, %.0f calls of sub-lists), data copied in %.1f (kept indices, cut corners, palettes), the rest %.1f (commands); %.0f part draws a frame",
			bone / 1024.0 / frames_seen, model_bone_loads / frames_seen, model_bone_calls / frames_seen,
			(model_take_bytes - model_bone_sub_bytes) / 1024.0 / frames_seen,
			((double)kind_bytes[KIND_MODEL] - bone - (model_take_bytes - model_bone_sub_bytes)) / 1024.0 / frames_seen,
			model_part_calls / frames_seen);
		platform_log("GE: models left out for room, smallest first: %.1f part draws a frame (the size floor now %.1f px, at most %.1f px since the last report); rigid parts near the eye %.1f a frame, %.1f of them drawn whole with no copy; bending batches near the eye drawn whole %.1f a frame, %.1f of them by their sphere (build 118)%s",
			model_floor_left_out / frames_seen, model_size_floor, model_size_floor_peak, near_parts / frames_seen,
			near_parts_whole / frames_seen, near_batches_whole / frames_seen, near_batches_by_sphere / frames_seen,
			near_sphere_check ? (near_sphere_mismatches ? "; nearspherecheck: SOME THE SPHERE PASSED WERE NOT WHOLE" : "; nearspherecheck: all whole") : "");
		if (near_sphere_check && near_sphere_mismatches)
			platform_log("GE: nearspherecheck: %lu batches the sphere passed were cut or dropped by the corner loop", near_sphere_mismatches);
		/* build 119 */
		platform_log("GE: near runs of %d triangles (%s): %.1f a frame kept whole, %.1f dropped whole, %.1f looked at corner by corner; their spheres %lu KB%s",
			NEAR_CLUSTER_TRIANGLES, near_cluster_full > 0 ? "off: nearclusterfull" : "on", near_cluster_kept / frames_seen,
			near_cluster_dropped / frames_seen, near_cluster_mixed / frames_seen, near_cluster_bytes / 1024,
			near_cluster_check ? (near_cluster_mismatches ? "; nearclustercheck: SOME TRIANGLES DISAGREED WITH THEIR RUN" : "; nearclustercheck: all agreed") : "");
		if (near_cluster_check && near_cluster_mismatches)
			platform_log("GE: nearclustercheck: %lu triangles disagreed with their run's verdict", near_cluster_mismatches);
		if (skin_axis_check)
			platform_log("GE: skinaxischeck: %lu corner sums compared with the loop's, %lu differed", skin_axis_checked, skin_axis_differ);
	}
	near_cluster_kept = near_cluster_dropped = near_cluster_mixed = near_cluster_mismatches = 0;
	skin_axis_checked = skin_axis_differ = 0;
	model_floor_left_out = near_parts = near_parts_whole = near_batches_whole = 0;
	near_batches_by_sphere = near_sphere_mismatches = 0;
	model_size_floor_peak = model_size_floor;
	model_bone_loads = model_take_bytes = model_part_calls = model_bone_calls = model_bone_sub_bytes = 0;
	platform_log("GE: fullest frame %u KB, kinds in order %s (L level, o other, k sky, l lights, B level see-through, M models, q see-through queued, d decals, p particles, f flares, s shadows, T see-through drawn, Q screen quads, H HUD); the level's reserve %s, held back %lu draws",
		kind_peak_total / 1024, kind_peak_order, level_reserve_on > 0 ? "on" : "off", level_reserve_held);
	memset(kind_bytes, 0, sizeof(kind_bytes));
	memset(kind_skips, 0, sizeof(kind_skips));
	kind_frames = 0;
	kind_peak_total = 0;
	level_reserve_held = 0;
}

/* the limit for the kind running: lower before the level has begun */
static int kind_limit(void)
{
	if (level_reserve_on > 0 && !level_begun && ge_kind_now != KIND_LEVEL && ge_kind_now != KIND_SKY && !hud_phase)
		return list_limit - LEVEL_RESERVE;
	return list_limit;
}

static int list_full(void)
{
	int limit = kind_limit();

	if (sceGuCheckList() < limit - DISPLAY_LIST_MARGIN)
		return 0;
	if (!list_full_skips++)
		platform_log("GE: the display list is full; draws skipped until the frame ends");
	kind_frame_skips[ge_kind_now]++;
	if (limit != list_limit && sceGuCheckList() < list_limit - DISPLAY_LIST_MARGIN)
		level_reserve_held++;
	return 1;
}

/* build 40: the first-person weapon's see-through parts (the assault
rifle's ammo display) are the nearest, so sorted far to near they came last,
and a full list skipped exactly them (Sheepdog01: the display went blank in
sustained fire). They may use a quarter of the HUD's reserve */
static int list_full_first_person(void)
{
	int saved = list_limit, full;

	if (list_limit < list_bytes - HUD_RESERVE * 3 / 4)
		list_limit = list_bytes - HUD_RESERVE * 3 / 4;
	full = list_full();
	list_limit = saved;
	return full;
}

/* room in the display list for a draw's own data, or NULL: a large draw
(a light over many triangles) can need more than the margin above, and
writing past the list's end corrupts memory (build 29) */
int psp_tex_bind_lightmap(unsigned int key);
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

/* build 114: "listcachedall" (lever F; optimization_ranking.md X18): the
command words through the cache too. pspgu writes every command word through
the list's uncached view (sceGuStart sets bit 30), one bus store each:
b113's Test 3 shows 880-980 uncached stores per million cycles while
drawing, and the PSP's level passes cost 175 us a draw against the
emulator's 18 (engine_performance.md), a gap only the PSP's bus explains
[U]. The GE takes only an address's low bits, so the list can be written
through the cached view as long as every line is in memory before the GE
may read it: each time the stall address is handed on (the two places that
call the kernel), the lines from the last hand-over to the new stall
address are written back and dropped ("cache 0x1B", as build 48's regions).
Off with gemeon (the ME writes the list, through its own cache), with
benchrun's list in VRAM, and with "listuncached". Exact: the same words
reach the GE. The emulator can't check the write-back (it has no caches):
only the PSP shows whether a line was missed (a GE hang or a garbled
frame) */
struct gu_list_view { unsigned int *start, *current; int parent_context; };
extern struct gu_list_view *gu_list; /* pspgu's (guInternal.h: GuDisplayList) */
static int list_all_cached = -1, list_cached_frame = 0;
static unsigned int list_back_from = 0;
/* while sceGuGetMemory runs: the region it hands out, not filled yet (its
own stall update may be handed to the GE); 0 otherwise */
static unsigned int list_filling_from = 0;
static unsigned long list_lines_back = 0;

/* right after the frame's sceGuStart */
static void list_cached_frame_start(int in_vram)
{
	if (list_all_cached < 0)
		list_all_cached = psp_keep("listcachedall") && !psp_keep("gemeon") && !psp_keep("listuncached");
	list_cached_frame = list_all_cached && !in_vram;
	if (!list_cached_frame)
		return;
	/* sceGuStart wrote its first words through the uncached view, into the
	line the next cached store reads in: they reach RAM first */
	__asm__ volatile("sync");
	gu_list->start = (unsigned int *)((unsigned int)gu_list->start & ~0x40000000u);
	gu_list->current = (unsigned int *)((unsigned int)gu_list->current & ~0x40000000u);
	list_back_from = (unsigned int)gu_list->current & ~63u;
}

/* before the GE is told it may read up to stall */
static void list_write_back(const void *stall)
{
	unsigned int end = (unsigned int)stall & ~0x40000000u, line;

	/* inside sceGuGetMemory: only up to its region (the JUMP over it is
	written; the region is filled after, and joins the pending range) */
	if (list_filling_from && list_filling_from < end)
		end = list_filling_from;
	if (!list_cached_frame || end <= list_back_from)
		return;
	for (line = list_back_from; line < end; line += 64)
		__builtin_allegrex_cache(0x1B, line);
	__asm__ volatile("sync");
	list_lines_back += (end - list_back_from + 63) / 64;
	/* the line holding the stall address may get more words */
	list_back_from = end & ~63u;
}

/* listcachedall: a region taken with sceGuGetMemory is filled after that
call moved the stall address, possibly past the region and handed on, so
list_write_back alone would never write it. Its lines join build 48's
pending range instead, written back at the next hand-over, before the GE
can reach the draw that reads it (that draw comes after the region).
sceGuGetMemory is wrapped (psp_build.py links it with --wrap), so every
region is covered, pspgu's own too (sceGuClear fills one itself) */
static void list_region_pending(const void *taken, int bytes)
{
	unsigned int start, end;

	if (!list_cached_frame || !taken)
		return;
	start = ((unsigned int)taken & ~0x40000000u) & ~63u;
	end = (((unsigned int)taken & ~0x40000000u) + bytes + 63) & ~63u;
	if (pending_end <= pending_start || start < pending_start)
		pending_start = start;
	if (end > pending_end)
		pending_end = end;
}

void *__real_sceGuGetMemory(int size);
void *__wrap_sceGuGetMemory(int size)
{
	void *taken;

	if (!list_cached_frame)
		return __real_sceGuGetMemory(size);
	/* the region starts two words past the list's current end */
	list_filling_from = ((unsigned int)gu_list->current & ~0x40000000u) + 8;
	taken = __real_sceGuGetMemory(size);
	list_filling_from = 0;
	list_region_pending(taken, size);
	return taken;
}

static void *list_take(int bytes)
{
	void *taken;

	if (sceGuCheckList() + bytes + 2048 + 128 > kind_limit())
	{
		if (!list_full_skips++)
			platform_log("GE: the display list is full (%d more bytes wanted); draws skipped until the frame ends", bytes);
		kind_frame_skips[ge_kind_now]++;
		return NULL;
	}
	if (ge_kind_now == KIND_MODEL)
		model_take_bytes += bytes;
	if (list_cached < 0)
		list_cached = !psp_keep("listuncached");
	if (!list_cached || bytes < LIST_CACHED_MINIMUM)
		return sceGuGetMemory(bytes); /* (build 114: listcachedall's pending range, in the wrapper) */
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

/* build 113: "gebusy": how long the GE works, timed by the GE itself
(b113_candidates.md, instruments). We log the main CPU's time and the ME's,
but the GE only through the frame end's wait for it. Each stall update
passed on (a chunk of up to STALL_BATCH draws) now ends with a SIGNAL
command; when the GE reaches it, it raises an interrupt and gebusy_signal
notes the time. A chunk's busy time runs from when the GE could start it
(handed over, or the chunk before finished, whichever is later) to then; the
frame's last chunk ends at the list's FINISH (gebusy_finish). Summed over
the heartbeat's window for the frameend: line. An interrupt per 16 draws,
a few microseconds each. Not while the ME builds the lists (gemeon): it
passes its stall updates on later. The emulator doesn't time the GE: only
the PSP's number means anything */
#define GEBUSY_SLOTS 256
static int gebusy_on = -1, gebusy_inserting = 0;
static void *gebusy_stall;
static PSP_MAIN_SIDE unsigned int gebusy_next = 0;
static PSP_MAIN_SIDE volatile unsigned int gebusy_handed[GEBUSY_SLOTS], gebusy_last_reached = 0, gebusy_finish_handed = 0;
static PSP_MAIN_SIDE volatile unsigned long gebusy_us = 0, gebusy_chunks = 0, gebusy_finishes = 0;
static PSP_MAIN_SIDE unsigned int gebusy_window_start = 0;
/* build 115: the GE's time by the kind of draw it ran (ge_kind_now's kinds):
each chunk is one kind's (a change of kind ends the chunk: gebusy_cut), so its
busy time goes to that kind. The emulator doesn't time the GE: PSP only */
static PSP_MAIN_SIDE volatile unsigned char gebusy_kind[GEBUSY_SLOTS];
static PSP_MAIN_SIDE volatile unsigned char gebusy_finish_kind = 0;
static PSP_MAIN_SIDE volatile unsigned long gebusy_kind_us[KINDS];
static int gebusy_cut = 0;

static void gebusy_add(unsigned int start, unsigned int now, int kind)
{
	if ((int)(gebusy_last_reached - start) > 0)
		start = gebusy_last_reached;
	if ((int)(now - start) > 0)
	{
		gebusy_us += now - start;
		if (kind >= 0 && kind < KINDS)
			gebusy_kind_us[kind] += now - start;
	}
	gebusy_last_reached = now;
}

/* the GE's interrupts (pspgu's callbacks: interrupt context, so only the
clock and a few sums) */
static void gebusy_signal(int id)
{
	gebusy_add(gebusy_handed[id & (GEBUSY_SLOTS - 1)], sceKernelGetSystemTimeLow(), gebusy_kind[id & (GEBUSY_SLOTS - 1)]);
	gebusy_chunks++;
}

static void gebusy_finish(int id)
{
	(void)id;
	gebusy_add(gebusy_finish_handed, sceKernelGetSystemTimeLow(), gebusy_finish_kind);
	gebusy_finishes++;
}

static void gebusy_start(void)
{
	ge_reads_on = psp_keep("gereads") ? 1 : 0; /* build 115 */
	if (ge_reads_on)
		platform_log("GE: gereads on (what each draw makes the GE read, by where it lives: a report every 300 frames)");
	gebusy_on = psp_keep("gebusy");
	if (!gebusy_on)
		return;
	sceGuSetCallback(GU_CALLBACK_SIGNAL, gebusy_signal);
	sceGuSetCallback(GU_CALLBACK_FINISH, gebusy_finish);
	gebusy_window_start = sceKernelGetSystemTimeLow();
	platform_log("GE: gebusy on (a SIGNAL every %d draws; the GE's busy time in the frameend: line)", STALL_BATCH);
}

/* the window's part of the frameend: line, then a new window */
static void gebusy_window(char *text, int size, unsigned long frames_in_window)
{
	unsigned int now = sceKernelGetSystemTimeLow(), wall = now - gebusy_window_start;
	unsigned long n = frames_in_window ? frames_in_window : 1;

	if (gebusy_on <= 0 || size <= 0)
		return;
	snprintf(text, size, "; GE busy %.1f ms a frame, %.1f%% of the clock (%lu chunks, %lu finishes)%s",
		gebusy_us / 1000.0 / n, wall ? 100.0 * gebusy_us / wall : 0.0, gebusy_chunks, gebusy_finishes,
		PSP_ME_WORK() ? " (not while the ME builds)" : "");
	{
		/* build 115: the same time by kind, ms a frame (a line of its own) */
		char line[420];
		int k, at = snprintf(line, sizeof(line), "gebusy by kind, ms a frame:");

		for (k = 0; k < KINDS && at < (int)sizeof(line) - 1; k++)
			if (gebusy_kind_us[k] >= 100)
				at += snprintf(line + at, sizeof(line) - at, " %s %.1f;", kind_names[k], gebusy_kind_us[k] / 1000.0 / n);
		if (gebusy_us)
			platform_log("%s", line);
		memset((void *)gebusy_kind_us, 0, sizeof(gebusy_kind_us));
	}
	gebusy_us = gebusy_chunks = gebusy_finishes = 0;
	gebusy_window_start = now;
}

int __wrap_sceGeListUpdateStallAddr(int qid, void *stall)
{
	/* build 113: gebusy's own SIGNAL (below) moves the stall address; it
	comes back here, and the new address is passed on with the chunk */
	if (gebusy_inserting)
	{
		gebusy_stall = stall;
		return 0;
	}
	stall_calls++;
	if (stall_batch < 0)
		stall_batch = !psp_keep("stalleach");
	if (ge_serial < 0)
		ge_serial = psp_keep("geserial");
	if (ge_serial && !stall_force)
		return 0;
	if (stall_batch && !stall_force && !gebusy_cut && ++stall_pending < STALL_BATCH)
		return 0;
	stall_pending = 0;
	list_flush_pending();
	if (PSP_ME_WORK())
	{
		stall_me_qid = qid;
		stall_me_address = stall;
		return 0;
	}
	if (gebusy_on > 0)
	{
		if (stall_force)
		{
			gebusy_finish_handed = sceKernelGetSystemTimeLow();
			gebusy_finish_kind = (unsigned char)ge_kind_now;
		}
		else
		{
			/* the chunk ends with a SIGNAL: the GE reports when it gets there */
			unsigned int slot = gebusy_next++ & (GEBUSY_SLOTS - 1);

			gebusy_kind[slot] = (unsigned char)ge_kind_now; /* build 115 */

			gebusy_inserting = 1;
			gebusy_stall = stall;
			sceGuSignal(GU_SIGNAL_NOWAIT, (int)slot);
			gebusy_inserting = 0;
			stall = gebusy_stall;
			gebusy_handed[slot] = sceKernelGetSystemTimeLow();
		}
	}
	stall_forwarded++;
	list_write_back(stall); /* build 114: listcachedall */
	if ((stall_forwarded & 63) == 0)
	{
		/* now and then, what the call itself costs */
		unsigned int t0 = ge_clock();
		int result = __real_sceGeListUpdateStallAddr(qid, stall);

		stall_timed_us += ge_clock() - t0;
		stall_timed++;
		return result;
	}
	return __real_sceGeListUpdateStallAddr(qid, stall);
}
/* build 115: under gebusy, a change of kind hands the draws so far on as a
chunk of the old kind (a stall update with its SIGNAL; sceGuGetMemory makes
pspgu update the stall address), so no chunk mixes kinds */
static void gebusy_kind_cut(int kind)
{
	if (gebusy_on <= 0 || kind == ge_kind_now || !stall_pending || PSP_ME_WORK())
		return;
	gebusy_cut = 1;
	sceGuGetMemory(4);
	gebusy_cut = 0;
}
#define SKY_COLOR 0xFFD8B080 /* ABGR: a pale blue */
#define FOG_NEAR 8.0f
#define FOG_FAR 160.0f
/* models are drawn unlit for now: their texture times this grey */
#define MODEL_LIGHT 0xFFB4B4B4

/* build 105: the display list (384 KB) is no longer part of the program's
zeroed data: the heap is sized to fill whatever the program leaves below the
game state, so those 384 KB went to the heap. The list itself is taken when
the GE starts, from the free space left in user RAM (b104 run 1: 552 KB free
in play) when a block that large is free there, else from the heap, which
leaves things as they were. The emulator shows why it waits for the GE: taken
first thing at start-up, the block landed where the tag window is placed
later (at a fixed address), and the map couldn't load. In play the emulator
had 576 KB free but no block over 308 KB, so there it comes from the heap;
the log says which, and how the free space lies, on the PSP */
static unsigned int *display_list = NULL;
static const char *display_list_where = "not yet";
static int display_list_free_kb = 0, display_list_largest_kb = 0;

/* at least this much stays free outside the heap for what is made later
(threads' stacks, the blackbox's buffer: b104 took 32 KB more after load) */
#define DISPLAY_LIST_LEAVES_FREE (128 * 1024)

static void transparent_table_acquire(void); /* build 114: the see-through table, with the list */

static void display_list_acquire(void)
{
	SceUID block = -1;

	if (display_list)
		return;
	if (psp_keep("list512"))
		display_list_size = DISPLAY_LIST_BYTES;
	if ((int)sceKernelTotalFreeMemSize() >= display_list_size + 64 + DISPLAY_LIST_LEAVES_FREE)
		block = sceKernelAllocPartitionMemory(2, "halo display list", PSP_SMEM_High, display_list_size + 64, NULL);
	if (block >= 0)
	{
		display_list = (unsigned int *)(((unsigned int)sceKernelGetBlockHeadAddr(block) + 63) & ~63u);
		display_list_where = "free user RAM outside the heap";
	}
	else
	{
		display_list = memalign(64, display_list_size);
		display_list_where = display_list ? "the heap (no room for it outside)" : "nowhere: no memory";
	}
	display_list_free_kb = sceKernelTotalFreeMemSize() / 1024;
	display_list_largest_kb = sceKernelMaxFreeMemSize() / 1024;
	/* build 114: no line of the block left in the cache from earlier use
	(listcachedall writes the list through the cache; a stale dirty line
	written back later would land over the GE's words) */
	if (display_list)
		sceKernelDcacheWritebackInvalidateRange(display_list, display_list_size);
	transparent_table_acquire();
}

static void display_list_report(void)
{
	platform_log("GE: display list %d KB at %08x, from %s; then %d KB free in user RAM (largest block %d KB), now %d KB (largest %d KB)",
		display_list_size / 1024, (unsigned int)display_list, display_list_where, display_list_free_kb,
		display_list_largest_kb, sceKernelTotalFreeMemSize() / 1024, sceKernelMaxFreeMemSize() / 1024);
}
static int ge_ready = 0, frame_open = 0;
/* the buffer being drawn (relative to VRAM), swapped each frame */
static void *draw_buffer = (void *)0;
static unsigned long frames = 0, draws_this_frame = 0, triangles_this_frame = 0;
static unsigned long clipped_this_frame = 0, culled_this_frame = 0, model_draws_this_frame = 0;

/* the camera, for the CPU's checks: position, and its right, up and forward
directions in the world */
static float camera_right[3], camera_up[3], camera_forward[3];
static float guard_x, guard_y;
/* build 118: the lengths of the guard band's side planes' normals as
outside() writes them (x = guard_x z has (1, 0, -guard_x)), set with
guard_x and guard_y, so a sphere can be tested against those planes
without a square root each time (model_sphere_inside) */
static float guard_x_length = 1.0f, guard_y_length = 1.0f;
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
/* build 118: the model's bounding sphere (the object's, which the game
culls it by) in camera space, for transparent_cut's fast path; set by
psp_ge_model_shape, radius 0 when there is none (the sky, a flag, the
first-person weapon, after the model ends or a new window begins) */
static float model_sphere[3], model_sphere_radius = 0.0f;
static unsigned long first_person_flushes = 0;
/* build 118: frames by how long they were shown (psp_ge_frame_end) */
static unsigned int swap_last = 0, swap_longest = 0;
static unsigned long swap_shown[5];
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
	part_mark = ge_clock();
}
static inline void part_add(int part)
{
	unsigned int now = ge_clock();

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
/* build 116: the colour each frame is cleared to, as the Xbox clears a
window: its atmospheric fog's colour, fog on or off (rasterizer_xbox.c,
_rasterizer_window_begin), here the last window's (one frame late at most).
Builds 14-115 cleared to a fixed pale blue, which showed wherever nothing
was drawn: a10's space, gaps indoors. Keep word "clearblue": as before */
static unsigned int window_clear_color = SKY_COLOR;
static int clear_blue = -1;
/* build 116: the window's planar fog, as it reaches the eye (psp_fixups.py,
rasterizer.c): the colour, and where it would be all fog (0: none). The
Xbox draws planar fog as a pass of its own over the level and the objects,
from the depth below the fog's plane and the distance
(rasterizer_xbox_environment_fog.c); with the eye inside the fog (a cluster
"fully fogged", c10's swamp, or the eye below the plane) that comes down to
a distance fog in the planar colour, which the GE has. psp_ge_fog fogs with
whichever of the two is thicker. Builds 23-115 had the atmospheric fog
only: c10's swamp ended in a hard edge at its far clip. Keep word
"planarfogoff": as before */
static float planar_far = 0.0f;
static unsigned int planar_color = 0;
static int planar_fog_off = -1;
static unsigned long planar_fog_windows = 0;

/* build 114: "drawdistN" (B9): the far clip at N world units outside
cutscenes, the fog closing at it (render.c, psp_fixups.py FIXES_114); 0 off */
float psp_draw_distance(void)
{
	extern int psp_keep_loaded(void);
	extern int psp_keep_number(const char *prefix, int fallback);
	static float limit = -1.0f;

	if (limit < 0.0f && psp_keep_loaded())
	{
		int n = psp_keep_number("drawdist", 0);

		limit = n > 0 ? (float)n : 0.0f;
		if (limit > 0.0f && !PSP_ME_WORK())
			platform_log("GE: drawdist: the far clip at %d world units outside cutscenes, the fog closing at it", n);
	}
	return limit > 0.0f ? limit : 0.0f;
}

void psp_ge_fog(const float *color, float minimum_distance, float maximum_distance, float density)
{
	GE_DEFER(defer_fog(color, minimum_distance, maximum_distance, density)); /* build 112: gedefer */
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
	window_clear_color = c;
	/* build 116: the planar fog, where it is thicker (above) */
	if (planar_far > 0.0f && planar_far < far)
	{
		near = 0.0f;
		far = planar_far;
		c = planar_color;
		planar_fog_windows++;
	}
	if (near != fog_near || far != fog_far || c != fog_color)
	{
		if (fog_changes++ < 8)
			platform_log("GE: fog %.1f to %.1f (density %.2f); the GE's %.1f to %.1f, colour %06x%s", minimum_distance,
				maximum_distance, density, near, far, c & 0xFFFFFF, c == planar_color && near == 0.0f ? " (planar)" : "");
		fog_near = near;
		fog_far = far;
		fog_color = c;
	}
	if (frame_open)
		sceGuFog(fog_near, fog_far, fog_color);
}

/* build 116: the window's planar fog (above), called just before psp_ge_fog
each window: density is the fog's maximum density times how deep the eye is
in it (0 to 1), 0 for none */
void psp_ge_fog_planar(float red, float green, float blue, float maximum_distance, float density)
{
	GE_DEFER(defer_words(DEFER_FOG_PLANAR, 5, defer_bits(red), defer_bits(green), defer_bits(blue),
		defer_bits(maximum_distance), defer_bits(density), 0, 0, 0));
	int r, g, b;

	if (planar_fog_off < 0)
		planar_fog_off = psp_keep("planarfogoff");
	planar_far = 0.0f;
	if (planar_fog_off || density <= 0.001f || maximum_distance <= 0.0f)
		return;
	if (density > 1.0f)
		density = 1.0f;
	/* all fog where a full fade would end, so the fog reaches the planar
	density at its maximum distance, as psp_ge_fog does the atmospheric */
	planar_far = maximum_distance / density;
	r = (int)(red * 255.0f); g = (int)(green * 255.0f); b = (int)(blue * 255.0f);
	r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
	planar_color = 0xFF000000 | (b << 16) | (g << 8) | r;
}

/* the GE's own state, once: buffers, viewport, depth, fog */
static void model_cut_take(void); /* build 108: below, by model_cut */

static void ge_initialize(void)
{
	/* build 108: the scratchpad's pieces (psp_scratch.c), keep.txt read by now */
	extern void psp_profile_tables_take(void);

	model_cut_take();
	psp_profile_tables_take();
	display_list_acquire();
	display_list_report();
	sceGuInit();
	gebusy_start(); /* build 113 */
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
	lit_forget(0); /* build 114: statememo */
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
static int player_seated = 0;
static float near_plane_auto(const float *eye, float game_near)
{
	extern int psp_player_seated(void);
	unsigned int start = ge_clock();
	int scale, level_scale = 1, object_scale;

	/* build 90: for psp_ge_model_shape this frame */
	player_seated = psp_player_seated();
	near_object_distance_last = near_object_distance;
	near_object_distance = 1e30f;
	/* build 106: the nearest model's limit first, and the level tested
	only at the scales it leaves (each test is a sphere query of the level,
	1.2 ms a frame in the emulator at b30's beach, where the marines held
	the plane at x1 anyway). The same choice as testing the level first: a
	sphere clear of the level leaves every smaller one about the same
	centre clear too */
	for (object_scale = 16; object_scale > 1 && near_object_distance_last < game_near * object_scale * 1.4f;)
		object_scale /= 2;
	if (object_scale < 16)
		near_auto_by_objects++;
	for (scale = object_scale; scale > 1; scale /= 2)
		if (!collision_test_sphere(eye, game_near * scale * 1.4f, -1))
		{
			level_scale = scale;
			break;
		}
	scale = level_scale;
	near_auto_chosen[scale >= 4 ? 2 : scale == 2 ? 1 : 0]++;
	if (scale >= 8)
		near_auto_chosen_high[scale == 16 ? 1 : 0]++;
	near_auto_frames++;
	near_auto_us += ge_clock() - start;
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
		ge_blend(1);
	}
	else
	{
		ge_blend(0);
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

	/* (build 112: never in the Media Engine's work: it reads VRAM, which the
	ME can't see; temporal blend is settled off anyway) */
	if (taa_done || !frame_open || PSP_ME_WORK())
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
void psp_ge_forget_level(void);
static int stencil_mode;
static int level_compact;
/* build 91: detail passes levelled by distance only (see the detail pass) */
static int detail_by_distance = 1;
static int decal_project;
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
	case 20:
		/* build 93: benchrun: the GE skips a kind of draw (GE_SKIP_*) */
		if (set)
			ge_skip = value;
		return ge_skip;
	case 21:
		/* build 93: benchrun: the display list in VRAM (needs benchrun's
		VRAM room) */
		if (set)
			list_in_vram = value && psp_keep("benchrun");
		return list_in_vram;
	case 22:
		/* build 93: benchrun: the GE busy on VRAM only. value > 0: on, sized
		toward value tenths of a ms of GE time (timed alone: serial); -1: the
		size held and the GE overlapped again (the measurement); 0: off */
		if (set)
		{
			if (value > 0 && psp_keep("benchrun"))
			{
				vram_busy = 1;
				vram_busy_target_us = (unsigned int)value * 100;
				vram_busy_calibrating = 1;
				ge_skip = GE_SKIP_ALL;
				ge_serial = 1;
			}
			else if (value < 0 && vram_busy)
			{
				vram_busy_calibrating = 0;
				ge_serial = 0;
			}
			else if (!value)
			{
				if (vram_busy)
				{
					ge_skip = 0;
					ge_serial = 0;
				}
				vram_busy = vram_busy_calibrating = 0;
			}
		}
		return vram_busy ? vram_busy_sprites : 0;
	case 23:
		/* build 102: benchrun: every texture bind (1) or the lightmaps' (2)
		replaced by one 16 x 16 texture in VRAM: the GE reads no texels from
		main RAM (psp_tex.c). Splits b93's "models and the level each carry
		half the bus cost" into texels and vertices */
		if (set)
		{
			extern int psp_tex_standin;

			psp_tex_standin = value && psp_keep("benchrun") ? value : 0;
		}
		{
			extern int psp_tex_standin;

			return psp_tex_standin;
		}
	case 24:
		/* build 102: benchrun: every mipped texture one level smaller (the
		GE's level bias +1): a quarter of the texels for the same pixels */
		{
			static float saved_bias = -1.0f;

			if (set)
			{
				if (saved_bias < 0.0f)
					saved_bias = tex_bias;
				tex_bias = saved_bias + (value ? 1.0f : 0.0f);
				sceGuTexLevelMode(tex_level_mode, tex_bias);
			}
			return saved_bias >= 0.0f && tex_bias > saved_bias;
		}
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
	case 16:
		/* build 87: the stencil bake. Changing it lets the level's copies go,
		so they are rebuilt with or without the pieces' vertices */
		if (set && value != stencil_mode)
		{
			stencil_mode = value < 0 ? 0 : value > 2 ? 2 : value;
			psp_ge_forget_level();
		}
		return stencil_mode;
	case 19:
		/* build 91: the detail passes' texture level by distance alone (1),
		or with the slant buckets per triangle (0, b64-b90) */
		if (set)
			detail_by_distance = value != 0;
		return detail_by_distance;
	case 18:
		/* build 90: the permanent decals on the level's own triangles */
		if (set)
			decal_project = value != 0;
		return decal_project;
	case 25:
		/* build 103: benchrun: the level's vertex copies in VRAM; rebuilt */
		if (set && (value != 0 && psp_keep("benchrun")) != (level_vram != 0))
		{
			level_vram = value != 0 && psp_keep("benchrun");
			psp_ge_forget_level();
			vram_vertex_refused = vram_vertex_streams = 0;
		}
		return level_vram;
	case 26:
		/* build 103: the VRAM copies' use, for the bench's RESULT line:
		value 0 KB used, 1 copies placed, 2 copies refused (no room) */
		return value == 0 ? (int)(vram_vertex_next / 1024) : value == 1 ? (int)vram_vertex_streams : (int)vram_vertex_refused;
	case 17:
		/* build 87: the level's vertices at 16 bits; the copies are rebuilt */
		if (level_compact < 0)
			level_compact = psp_keep("levelcompact");
		if (set && (value != 0) != (level_compact != 0))
		{
			level_compact = value != 0;
			psp_ge_forget_level();
		}
		return level_compact;
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

/* build 93: benchrun's "GE busy on VRAM only": every real draw skipped,
and at the frame's start the GE given work that touches only VRAM (a 512 x
256 16-bit texture in VRAM's benchrun room, copied over itself as sprites,
the list's own few hundred bytes aside), as long as this view's normal frame
keeps it busy. If the CPU still slows down, main RAM's bus isn't why */

static void vram_busy_load(void)
{
	struct { unsigned short u, v; short x, y, z; } *corners;
	int k;

	sceGuDrawBufferList(GU_PSM_5650, (void *)VRAM_BENCH_START, 512);
	sceGuScissor(0, 0, 512, 256);
	sceGuDisable(GU_DEPTH_TEST);
	ge_blend(0);
	sceGuDisable(GU_FOG);
	sceGuDisable(GU_ALPHA_TEST);
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(GU_PSM_5650, 0, 0, 0);
	sceGuTexImage(0, 512, 256, 512, (void *)(0x04000000 + VRAM_BENCH_START));
	sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);
	sceGuTexScale(1.0f, 1.0f);
	sceGuTexOffset(0.0f, 0.0f);
	corners = sceGuGetMemory(2 * sizeof(*corners));
	corners[0].u = 0; corners[0].v = 0; corners[0].x = 0; corners[0].y = 0; corners[0].z = 0;
	corners[1].u = 512; corners[1].v = 256; corners[1].x = 512; corners[1].y = 256; corners[1].z = 0;
	for (k = 0; k < vram_busy_sprites; k++)
		(sceGuDrawArray)(GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, NULL, corners);
	sceGuDrawBufferList(GU_PSM_5650, draw_buffer, BUFFER_WIDTH);
	sceGuScissor(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
	sceGuEnable(GU_DEPTH_TEST);
	sceGuEnable(GU_FOG);
	sceGuDisable(GU_TEXTURE_2D);
	/* overlapped: the GE told at once (the skipped draws make few stall
	updates, and they are passed on in batches) */
	if (!vram_busy_calibrating)
	{
		stall_force = 1;
		sceGuGetMemory(4);
		stall_force = 0;
	}
}

/* the load's size, from this frame's wait for the GE while calibrating (the
GE alone then: serial, and every real draw skipped) */
static void vram_busy_calibrate(unsigned int waited_us)
{
	int wanted;

	if (!vram_busy || !vram_busy_calibrating)
		return;
	/* too little to time: double it */
	if (waited_us < 200)
		wanted = vram_busy_sprites * 2;
	else
		wanted = (int)((float)vram_busy_sprites * (float)vram_busy_target_us / (float)waited_us + 0.5f);
	/* halfway each frame, within 1..400 sprites */
	wanted = (vram_busy_sprites + wanted) / 2;
	vram_busy_sprites = wanted < 1 ? 1 : wanted > 400 ? 400 : wanted;
}

extern int psp_game_running(void); /* psp_fixups.py: players.c */
static unsigned long win_other_targets = 0; /* windows for another target than the screen, for frameend: */

/* build 108: each window's target, from the game's rasterizer_window_begin
(psp_fixups.py). A window for another target than the screen (the Xbox's
mirrors render into a second one first) is counted for the frameend: line:
the GE has the screen alone, so such a window's draws land on it. b107 drew
b30's floor mirrors that way, the whole level seen from the mirror's camera
under the floor (render.c no longer renders mirrors, psp_fixups.py) */
void psp_ge_window_target(int target)
{
	GE_DEFER(defer_words(DEFER_WINDOW_TARGET, 1, (unsigned int)target, 0, 0, 0, 0, 0, 0, 0)); /* build 112: gedefer */
	if (target != 0)
		win_other_targets++;
}

void psp_ge_window_begin(const float *position, const float *forward, const float *up,
	float vertical_field_of_view, float z_near, float z_far)
{
	ScePspFVector3 eye, center, upward;

	/* one window a frame: the GE's frame takes the first window's camera,
	and the later ones (letterbox bars, the non-player overlays: 2-D, on
	the same screen) draw into it. (build 112: they change nothing, so
	they don't make the waiting calls run either; the first does) */
	if (frame_open)
		return;
	GE_DEFER_DRAIN(); /* build 112: gedefer */
	if (!ge_ready)
		ge_initialize();
	{
		/* build 113: "shoteveryN": a screenshot every N seconds of game time,
		at most 24 (emulator runs have no hands for select + right). Asked
		for as the frame begins, so "tdump" lists the frame's see-through
		draws too */
		extern volatile int psp_screenshot_requested;
		extern int psp_keep_number(const char *prefix, int fallback);
		extern long psp_game_time_peek(void);
		static int shot_every = -2, shots_asked = 0;
		static long shot_next = -1;

		if (shot_every == -2)
			shot_every = psp_keep_number("shotevery", 0);
		if (shot_every > 0 && shots_asked < 24)
		{
			long now = psp_game_time_peek(), period = (long)shot_every * 30;

			if (shot_next >= 0 && now >= shot_next)
			{
				psp_screenshot_requested = 1;
				shots_asked++;
			}
			if (shot_next < 0 || now >= shot_next)
				shot_next = (now / period + 1) * period;
		}
	}
	/* build 93: the list in VRAM for benchrun's switch (from the next frame) */
	list_bytes = list_in_vram ? VRAM_BENCH_BYTES : display_list_size;
	sceGuStart(GU_DIRECT, list_in_vram ? (void *)(0x04000000 + VRAM_BENCH_START) : (void *)display_list);
	list_cached_frame_start(list_in_vram); /* build 114: listcachedall */
	lit_forget(2); /* build 114: statememo */
	sceGuFog(fog_near, fog_far, fog_color);
	list_limit = list_bytes - HUD_RESERVE - TRANSPARENT_RESERVE;
	if (vram_busy)
		vram_busy_load();
	hud_phase = 0;
	ge_kinds_frame_begin(); /* build 103 */
	defer_frame_begin(); /* build 112 */
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
	if (clear_blue < 0)
		clear_blue = psp_keep("clearblue");
	sceGuClearColor(magenta ? 0xFFFF00FF : clear_blue ? SKY_COLOR : window_clear_color);
	sceGuClearDepth(0);
	sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
	ge_dither(1); /* build 116: the world's draws */
	if (vm_on < 0)
		vm_reset(); /* build 116: vrammodels, after a new map or BSP */
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
		/* build 108: nearauto only in a game. Between maps (a load's
		frames, the error loop when a map is missing) the level and the
		player are gone, and its tests read them (Sheepdog01's b107 b30 run: a bus
		error at the level's end); the game's own plane then */
		if (near_auto && psp_game_running())
			z_near = near_plane_auto(position, z_near);
		else if (!near_auto)
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
	model_sphere_radius = 0.0f; /* build 118: a new camera; the next model sets its own */
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
		guard_x_length = sqrtf(1.0f + guard_x * guard_x); /* build 118 */
		guard_y_length = sqrtf(1.0f + guard_y * guard_y);
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

/* build 118: whether a camera-space sphere lies wholly inside the planes
the corner tests use (outside(), camera_corner_out()): its nearest depth at
or past near, and its centre inside each side plane by its radius (those
planes pass through the eye; their normals, as outside() writes them, are
guard_x_length and guard_y_length long). The radius gets 1% and 0.01 units
more for the rounding between the corners' transform and the centre's.
Written so a NaN fails. transparent_cut's model_sphere_inside and the near
path's batch_sphere_inside both ask this */
static int sphere_inside(const float *c, float radius, float near)
{
	float r = radius * 1.01f + 0.01f;

	return radius >= 0.0f && c[2] - r >= near &&
		fabsf(c[0]) + r * guard_x_length <= guard_x * c[2] &&
		fabsf(c[1]) + r * guard_y_length <= guard_y * c[2];
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
/* build 106: 64 (was 32): a bending part's corners keep six bits (build
106's model_geometry), a rigid one's five */
#define CORNER_STAMP_STEP 64
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
/* build 107: a rigid part's camera transform, one product (psp_ge_draw_model_body) */
static float rigid_rows[3][4];
static int near_check = -1;
static unsigned long near_check_corners = 0, near_check_differ = 0;

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
	/* build 89: the lightmap vertices' packed incident radiosity, for object
	lighting (object_lights.c via psp_ge_level_vertex_lighting; NULL without
	a lightmap) */
	unsigned int *radiosity;
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
	/* build 87: the stencil bake's pieces for this material (NULL: none);
	count then includes the vertices the bake added */
	struct bake_wall *bake;
	int original_count;
	/* build 87: "levelcompact": the same vertices at 16 bits (texture
	coordinates times a scale, positions on a lattice; NULL: floats only) */
	struct compact_vertex *compact_base, *compact_lightmap;
	float compact_origin[3], compact_uv_scale[2];
};
#define LEVEL_STREAMS 512

/* build 87: "levelcompact" (launch menu, or the debug menu's "Level
vertices"). Memory traffic is this port's crux (memory_traffic.md): the GE
reads every level vertex from RAM once a pass, 20 bytes each (two float
texture coordinates, three float positions), up to four passes a triangle.
At 16 bits a vertex is 10 bytes:
	- positions on one lattice of 1/256 unit (3.9 mm) for every material,
	  each material's origin a lattice point: the same world point lands on
	  the same lattice point whichever material holds it (no cracks), and the
	  GE's 16-bit position (q / 32768) times the model matrix's 128 plus the
	  origin is exact in floats. The float copies are snapped to the lattice
	  too, so the cut-up triangles, shadows, lights and stencils lie on the
	  very same points;
	- texture coordinates as q / 32768 times a per-material scale (the
	  material's largest coordinate), applied with sceGuTexScale; lightmap
	  coordinates are 16-bit in the map already.
A material wider than the lattice reaches (+-128 units around its origin)
stays in floats */
struct compact_vertex
{
	short u, v, x, y, z;
};
#define COMPACT_STEP (1.0f / 256.0f)
static int level_compact = -1;
static unsigned long compact_streams = 0, compact_refused = 0, compact_draws = 0;
/* the compact draw context of the level draw under way (NULL: floats) */
static const struct level_stream *compact_now = NULL;
static float pass_scale_u = 1.0f, pass_scale_v = 1.0f;
static void world_model(void);

/* the material's own model matrix (lattice to world, camera-relative) and
texture scale for a compact pass */
static void compact_begin(int lightmap)
{
	ScePspFMatrix4 m;
	float k = COMPACT_STEP * 32768.0f;

	gumLoadIdentity(&m);
	m.x.x = m.y.y = m.z.z = k;
	m.w.x = compact_now->compact_origin[0];
	m.w.y = compact_now->compact_origin[1];
	m.w.z = compact_now->compact_origin[2];
	relative(&m);
	sceGumMatrixMode(GU_MODEL);
	sceGumLoadMatrix(&m);
	if (lightmap)
		sceGuTexScale(pass_scale_u * (32768.0f / 32767.0f), pass_scale_v * (32768.0f / 32767.0f));
	else
		sceGuTexScale(pass_scale_u * compact_now->compact_uv_scale[0], pass_scale_v * compact_now->compact_uv_scale[1]);
	compact_draws++;
}

static void compact_end(void)
{
	world_model();
	sceGuTexScale(pass_scale_u, pass_scale_v);
}

/* builds a stream's 16-bit copies, snapping its float copies to the lattice;
0 when the material is too wide for the lattice (it stays in floats) */
static int compact_build(struct level_stream *stream)
{
	float low[3], high[3], umax = 1e-6f, vmax = 1e-6f;
	int i, axis;

	for (axis = 0; axis < 3; axis++)
	{
		low[axis] = stream->low[axis];
		high[axis] = stream->high[axis];
		stream->compact_origin[axis] = floorf((low[axis] + high[axis]) * 0.5f / COMPACT_STEP + 0.5f) * COMPACT_STEP;
		if (high[axis] - stream->compact_origin[axis] > 32760.0f * COMPACT_STEP ||
			stream->compact_origin[axis] - low[axis] > 32760.0f * COMPACT_STEP)
		{
			compact_refused++;
			return 0;
		}
	}
	for (i = 0; i < stream->count; i++)
	{
		if (fabsf(stream->base[i].u) > umax) umax = fabsf(stream->base[i].u);
		if (fabsf(stream->base[i].v) > vmax) vmax = fabsf(stream->base[i].v);
	}
	stream->compact_uv_scale[0] = umax * (32768.0f / 32767.0f);
	stream->compact_uv_scale[1] = vmax * (32768.0f / 32767.0f);
	/* build 111: in the copies' arena (16-byte aligned, as memalign gave) */
	stream->compact_base = copy_alloc(stream->count * sizeof(struct compact_vertex), (void **)&stream->compact_base);
	stream->compact_lightmap = stream->lightmap ?
		copy_alloc(stream->count * sizeof(struct compact_vertex), (void **)&stream->compact_lightmap) : NULL;
	if (!stream->compact_base || (stream->lightmap && !stream->compact_lightmap))
	{
		copy_free(stream->compact_base);
		copy_free(stream->compact_lightmap);
		stream->compact_base = stream->compact_lightmap = NULL;
		compact_refused++;
		return 0;
	}
	for (i = 0; i < stream->count; i++)
	{
		struct ge_vertex *b = &stream->base[i];
		struct compact_vertex *c = &stream->compact_base[i];
		float *p = &b->x;
		short q[3];

		for (axis = 0; axis < 3; axis++)
		{
			q[axis] = (short)floorf((p[axis] - stream->compact_origin[axis]) / COMPACT_STEP + 0.5f);
			/* the float copy on the lattice point itself (exact in floats) */
			p[axis] = stream->compact_origin[axis] + (float)q[axis] * COMPACT_STEP;
		}
		c->x = q[0]; c->y = q[1]; c->z = q[2];
		c->u = (short)floorf(b->u / stream->compact_uv_scale[0] * 32768.0f + 0.5f);
		c->v = (short)floorf(b->v / stream->compact_uv_scale[1] * 32768.0f + 0.5f);
		if (stream->lightmap)
		{
			struct ge_vertex *l = &stream->lightmap[i];
			struct compact_vertex *cl = &stream->compact_lightmap[i];
			float lu = l->u < 0.0f ? 0.0f : l->u > 1.0f ? 1.0f : l->u, lv = l->v < 0.0f ? 0.0f : l->v > 1.0f ? 1.0f : l->v;

			l->x = b->x; l->y = b->y; l->z = b->z;
			cl->x = q[0]; cl->y = q[1]; cl->z = q[2];
			cl->u = (short)floorf(lu * 32767.0f + 0.5f);
			cl->v = (short)floorf(lv * 32767.0f + 0.5f);
		}
	}
	ge_writeback(stream->compact_base, stream->count * sizeof(struct compact_vertex));
	if (stream->compact_lightmap)
		ge_writeback(stream->compact_lightmap, stream->count * sizeof(struct compact_vertex));
	compact_streams++;
	return 1;
}
static struct level_stream level_streams[LEVEL_STREAMS];
static unsigned long stream_bytes = 0;
static unsigned int stream_frame = 1;
static unsigned long streams_let_go = 0;
/* build 68: the copies' budget while a geometry pack is open (its blocks come
and go; the copies are what stays) */
#define LEVEL_STREAM_BUDGET (1024 * 1024)

/* ---------- build 111: the copies' arena

b108 run 1 (hardware_b108.md): the copies are let go and rebuilt as the view
moves (level_streams_trim), and each rebuild asked the general heap for large
blocks again (two of 106 KB for a material of 5,439 vertices) while the
game's small, long-lived allocations settled into the holes the last ones
left. After the hunter fight no hole was large enough: 1.4 MB free in 293
pieces, and 5,290 materials went undrawn to the level's end.

Every piece of a copy (the float copies, the normals, the radiosity, the
16-bit copies) now comes from one block, carved out of the heap at the first
copy and kept, so the copies' churn never reaches the general heap. When a
piece doesn't fit, the least recently drawn copies are let go until it does,
never one drawn this frame (the trim's rule: the GE may still be reading
those). If this frame's copies fill the arena, the piece comes from the
general heap, as before b111. What is drawn doesn't change.

Its size is the copies' budget plus 32 KB for the blocks' headers and some
slack against the arena's own fragmentation: what the copies already took
from the heap. Everything else shares what's left. In b108 run 1 that was
tight: at the peak (game time 25,072) the heap held 5,856 KB, the copies
about 980 of it, so the rest needed about 4,876 KB; with a 6,016 KB heap,
32 KB of slack leaves it about 4,960 (128 KB would leave about 4,864).

Blocks lie end to end, each behind a header holding its size and the size of
the block before it, so a freed block merges with free neighbours on both
sides. A request takes the smallest free block that holds it (best fit),
split when what's left is worth keeping. "noarena" (keep word): the general
heap only, as before. */
#define ARENA_BYTES (LEVEL_STREAM_BUDGET + 32 * 1024)
#define ARENA_ALIGN 16
#define ARENA_SPLIT_LEAST 64
struct arena_block
{
	unsigned int size;     /* bytes, this header included; a multiple of ARENA_ALIGN */
	unsigned int previous; /* the size of the block before it (0: the first) */
	unsigned int used;
	void **owner;          /* the field holding the block's address (arena_compact moves it) */
};
static char *arena = NULL;
static int arena_state = -1; /* -1: not started; 0: off ("noarena", or no room); 1: on */
static unsigned long arena_used = 0, arena_let_go = 0, arena_from_heap = 0;
/* pieces the general heap holds now (from_heap counts all ever sent there) */
static unsigned long arena_heap_live = 0;
/* the largest free block at the last frame's end, for the log */
static unsigned int arena_largest = 0;
static int level_stream_let_go_oldest(void);
static unsigned int stream_size(const struct level_stream *stream);

static struct arena_block *arena_next(struct arena_block *block)
{
	char *next = (char *)block + block->size;

	return next < arena + ARENA_BYTES ? (struct arena_block *)next : NULL;
}

static void __attribute__((cold)) arena_start(void)
{
	struct arena_block *first;

	arena_state = 0;
	if (psp_keep("noarena"))
	{
		platform_log("GE: the copies' arena: off (noarena); copies from the general heap");
		return;
	}
	arena = memalign(ARENA_ALIGN, ARENA_BYTES);
	if (!arena)
	{
		platform_log("GE: the copies' arena: no room for %u KB; copies from the general heap", ARENA_BYTES / 1024);
		return;
	}
	first = (struct arena_block *)arena;
	first->size = ARENA_BYTES;
	first->previous = 0;
	first->used = 0;
	first->owner = NULL;
	arena_state = 1;
	platform_log("GE: the copies' arena: %u KB", ARENA_BYTES / 1024);
}

/* the smallest free block holding size bytes (header included), split if
the rest is worth keeping; NULL: none */
static void *arena_take(unsigned int size, void **owner)
{
	struct arena_block *block, *best = NULL;

	for (block = (struct arena_block *)arena; block; block = arena_next(block))
		if (!block->used && block->size >= size && (!best || block->size < best->size))
			best = block;
	if (!best)
		return NULL;
	if (best->size - size >= ARENA_SPLIT_LEAST)
	{
		struct arena_block *rest = (struct arena_block *)((char *)best + size), *after;

		rest->size = best->size - size;
		rest->previous = size;
		rest->used = 0;
		rest->owner = NULL;
		best->size = size;
		after = arena_next(rest);
		if (after)
			after->previous = rest->size;
	}
	best->used = 1;
	best->owner = owner;
	arena_used += best->size;
	return best + 1;
}

static void *copy_alloc(unsigned int bytes, void **owner)
{
	unsigned int size = (bytes + sizeof(struct arena_block) + ARENA_ALIGN - 1) & ~(ARENA_ALIGN - 1);
	void *pointer = NULL;

	if (arena_state < 0)
		arena_start();
	if (arena_state > 0 && size <= ARENA_BYTES)
	{
		while (!(pointer = arena_take(size, owner)) && level_stream_let_go_oldest())
			arena_let_go++;
		if (pointer)
			return pointer;
		arena_from_heap++;
		arena_heap_live++;
	}
	/* 16-byte aligned like the arena's blocks (the 16-bit copies had memalign) */
	return memalign(ARENA_ALIGN, bytes);
}

static void copy_free(void *pointer)
{
	struct arena_block *block, *next;

	if (!pointer)
		return;
	if (!arena || (char *)pointer < arena || (char *)pointer >= arena + ARENA_BYTES)
	{
		if (arena_state > 0 && arena_heap_live)
			arena_heap_live--;
		free(pointer);
		return;
	}
	block = (struct arena_block *)pointer - 1;
	block->used = 0;
	block->owner = NULL;
	arena_used -= block->size;
	next = arena_next(block);
	if (next && !next->used)
		block->size += next->size;
	if (block->previous)
	{
		struct arena_block *before = (struct arena_block *)((char *)block - block->previous);

		if (!before->used)
		{
			before->size += block->size;
			block = before;
		}
	}
	next = arena_next(block);
	if (next)
		next->previous = block->size;
}

/* build 111: keeping the arena whole between frames.

The copies drawn in a frame can't move or go while it's drawn, so when the
view brings many new copies at once (a BSP switch, a turn in a big room) they
land in whatever holes the earlier copies left, and the arena can fill up in
pieces too small for the next big copy while plenty is free. b30 replayed on
run 1's route: after the BSP switch into the facility, 89 pieces went to the
general heap with about 960 KB of copies against the arena's 1,056.

At the frame's end the GE is done and no copy is being built, so the arena's
used blocks can slide down to its start (arena_compact): each block's owner
field (the level copy's pointer to it) is repointed, and the GE's arrays are
written back from the CPU's cache. Nothing outside level_streams keeps a
copy's address past the frame: see-through draws are flushed within it, cut
corners live in the display list, the decal cache and the overlays' answers
keep the stream or the indices' identity [D]. Then copies the general heap
holds come back into the arena (arena_migrate), a bounded amount a frame.

Compacting moves up to the arena's 1 MB, so it runs only when the free space
is badly split (its largest block under half of it, at least 64 KB free), at
most every 30 frames, or at the end of a frame that sent pieces to the heap.
"nocompact": neither (the arena as first built) */
#define ARENA_COMPACT_EVERY 30
#define ARENA_MIGRATE_BYTES (128 * 1024)
static int arena_compact_on = -1;
static unsigned int arena_last_compact = 0;
static unsigned long arena_seen_from_heap = 0;
static unsigned long arena_compactions = 0, arena_moved_kb = 0, arena_migrated = 0;

static void __attribute__((cold)) arena_compact(void)
{
	struct arena_block *block, *next;
	char *to = arena, *first_moved = NULL;
	unsigned int previous = 0;
	unsigned long moved = 0;

	for (block = (struct arena_block *)arena; block; block = next)
	{
		unsigned int size = block->size;

		/* the next block's header lies past this one: moving this one down
		never overwrites it */
		next = arena_next(block);
		if (!block->used)
			continue;
		if ((char *)block != to)
		{
			memmove(to, block, size);
			if (!first_moved)
				first_moved = to;
			moved += size;
		}
		block = (struct arena_block *)to;
		block->previous = previous;
		if (block->owner)
			*block->owner = block + 1;
		previous = size;
		to += size;
	}
	if (to < arena + ARENA_BYTES)
	{
		block = (struct arena_block *)to;
		block->size = (unsigned int)(arena + ARENA_BYTES - to);
		block->previous = previous;
		block->used = 0;
		block->owner = NULL;
	}
	if (first_moved)
		ge_writeback(first_moved, (unsigned int)(to - first_moved));
	arena_compactions++;
	arena_moved_kb += moved / 1024;
}

/* a piece the general heap holds, moved into the arena if a free block takes
it as it is (no letting go); the GE's arrays written back. 0: no room,
1: nothing to move (none, VRAM, or already in the arena), 2: moved */
static int __attribute__((cold)) arena_migrate_piece(void **field, unsigned int bytes, int ge_reads)
{
	char *piece = (char *)*field;
	void *to;

	if (!piece || (((unsigned int)piece & 0x0F000000u) == 0x04000000u) ||
		(piece >= arena && piece < arena + ARENA_BYTES))
		return 1;
	to = arena_take((bytes + sizeof(struct arena_block) + ARENA_ALIGN - 1) & ~(ARENA_ALIGN - 1), field);
	if (!to)
		return 0;
	memcpy(to, piece, bytes);
	if (ge_reads)
		ge_writeback(to, bytes);
	*field = to;
	free(piece);
	if (arena_heap_live)
		arena_heap_live--;
	return 2;
}

/* the general heap's copies brought into the arena, up to ARENA_MIGRATE_BYTES
a frame; a copy whose pieces don't all fit keeps the rest where they are */
static void __attribute__((cold)) arena_migrate(void)
{
	static const struct { unsigned short offset; unsigned char size, ge_reads; } pieces[6] = {
		{ offsetof(struct level_stream, base), sizeof(struct ge_vertex), 1 },
		{ offsetof(struct level_stream, lightmap), sizeof(struct ge_vertex), 1 },
		{ offsetof(struct level_stream, normals), sizeof(unsigned int), 0 },
		{ offsetof(struct level_stream, radiosity), sizeof(unsigned int), 0 },
		{ offsetof(struct level_stream, compact_base), sizeof(struct compact_vertex), 1 },
		{ offsetof(struct level_stream, compact_lightmap), sizeof(struct compact_vertex), 1 },
	};
	long budget = ARENA_MIGRATE_BYTES;
	int index, k, moved, result;

	for (index = 0; index < LEVEL_STREAMS && budget > 0 && arena_heap_live; index++)
	{
		struct level_stream *stream = &level_streams[index];

		for (moved = k = 0; k < 6; k++)
		{
			result = arena_migrate_piece((void **)((char *)stream + pieces[k].offset),
				(unsigned int)stream->count * pieces[k].size, pieces[k].ge_reads);
			if (!result)
				return;
			moved |= result == 2;
		}
		if (moved)
		{
			arena_migrated++;
			budget -= (long)stream_size(stream);
		}
	}
}

/* at the frame's end, after the trim (the GE has finished the frame). The
arena's housekeeping is marked cold: rare, compiled for size, kept out of the
hot code (build 111's first compaction build crossed a 64 KB heap step) */
/* ---------- build 115: "gereads" (big_levers.md section 0, "Measure first")

What each draw makes the GE read from memory, and where those bytes live:
the vertices (count times the format's size: for an indexed draw, a vertex
fetched for every index; any reuse inside the GE would make it less [U]) and
the indices. By where they live: VRAM, the display list (data written into
it each frame: decals' and cut pieces' corners, kept indices), the level's
copies (the arena above), the geometry cache (the models), the rest of the
volatile partition, and the heap or the program. Each vertex buffer (by its
address) keeps the bytes read from it and its extent; every 300 frames the
report gives the totals a frame by place and by kind of draw, the buffers
that would give most back per KB of VRAM, and the fit curve: the share of the
GE's reads from main RAM that VRAM would take if it held the best buffers,
200 to 1,000 KB of them. The per-frame data in the list can't be held, so it
stays out of the curve */
enum { GR_VRAM, GR_LIST, GR_LEVEL, GR_GEOMETRY, GR_VOLATILE, GR_OTHER, GR_PLACES };
static const char *const gr_place_names[GR_PLACES] = { "VRAM", "display list (this frame's data)", "level copies",
	"geometry cache (models)", "volatile, other", "heap or program" };
struct gr_entry { unsigned int key, read, size; unsigned char place, kind; };
#define GR_SLOTS 2048
static struct gr_entry *gr_table = NULL;
static unsigned long long gr_place_bytes[GR_PLACES], gr_kind_bytes[KINDS], gr_index_bytes = 0;
static unsigned long gr_draws = 0, gr_dropped = 0, gr_frames_mark = 0;
void psp_geo_range(unsigned int *start, unsigned int *end); /* psp_geo.c */

/* a GE vertex's size from its format word (the GE's own layout: weights,
texture coordinates, colour, normal, position, each aligned to its size, the
whole to its largest) */
static int gr_vertex_size(int vtype)
{
	static const unsigned char sizes[4] = { 0, 1, 2, 4 };
	int size = 0, align = 1, t;

	if ((t = (vtype >> 9) & 3) != 0)
	{
		size = (size + sizes[t] - 1) & ~(sizes[t] - 1);
		size += sizes[t] * (((vtype >> 14) & 7) + 1);
		if (sizes[t] > align) align = sizes[t];
	}
	if ((t = vtype & 3) != 0)
	{
		size = (size + sizes[t] - 1) & ~(sizes[t] - 1);
		size += sizes[t] * 2;
		if (sizes[t] > align) align = sizes[t];
	}
	if ((t = (vtype >> 2) & 7) != 0)
	{
		int c = t == 7 ? 4 : 2;

		size = (size + c - 1) & ~(c - 1);
		size += c;
		if (c > align) align = c;
	}
	if ((t = (vtype >> 5) & 3) != 0)
	{
		size = (size + sizes[t] - 1) & ~(sizes[t] - 1);
		size += sizes[t] * 3;
		if (sizes[t] > align) align = sizes[t];
	}
	if ((t = (vtype >> 7) & 3) != 0)
	{
		size = (size + sizes[t] - 1) & ~(sizes[t] - 1);
		size += sizes[t] * 3;
		if (sizes[t] > align) align = sizes[t];
	}
	return (size + align - 1) & ~(align - 1);
}

static int gr_place(unsigned int address)
{
	unsigned int a = address & ~0x40000000u, geo_start = 0, geo_end = 0;

	if ((a & 0x0F000000u) == 0x04000000u)
		return GR_VRAM;
	if (display_list && a >= ((unsigned int)display_list & ~0x40000000u) &&
		a < ((unsigned int)display_list & ~0x40000000u) + (unsigned int)display_list_size)
		return GR_LIST;
	if (arena && a >= (unsigned int)arena && a < (unsigned int)arena + ARENA_BYTES)
		return GR_LEVEL;
	psp_geo_range(&geo_start, &geo_end);
	if (geo_end > geo_start && a >= (geo_start & ~0x40000000u) && a < (geo_end & ~0x40000000u))
		return GR_GEOMETRY;
	if (a >= 0x08400000u && a < 0x08800000u)
		return GR_VOLATILE;
	return GR_OTHER;
}

static void ge_reads_note(int vtype, int count, const void *indices, const void *vertices)
{
	int vsize = gr_vertex_size(vtype), isize = ((vtype >> 11) & 3) == 1 ? 1 : ((vtype >> 11) & 3) == 2 ? 2 : 0;
	unsigned int bytes, extent, key, h;
	int place;

	if (count <= 0 || !vertices || vsize <= 0)
		return;
	if (!gr_table)
	{
		gr_table = calloc(GR_SLOTS, sizeof(struct gr_entry));
		if (!gr_table)
		{
			ge_reads_on = 0;
			platform_log("GE: gereads: no room for its table; off");
			return;
		}
	}
	gr_draws++;
	bytes = (unsigned int)count * (unsigned int)vsize;
	place = gr_place((unsigned int)vertices);
	gr_place_bytes[place] += bytes;
	gr_kind_bytes[ge_kind_now] += bytes;
	if (indices && isize)
	{
		gr_index_bytes += (unsigned int)count * (unsigned int)isize;
		gr_place_bytes[gr_place((unsigned int)indices)] += (unsigned int)count * (unsigned int)isize;
	}
	/* only buffers that could be held go in the table: this frame's data in
	the list is new every frame (its addresses would fill the table), and
	what's in VRAM is there already */
	if (place == GR_LIST || place == GR_VRAM)
		return;
	/* the buffer's extent: the vertices an index reaches, or the draw's own */
	extent = bytes;
	if (indices && isize)
	{
		unsigned int most = 0;
		int k;

		if (isize == 2)
			for (k = 0; k < count; k++) { if (((const unsigned short *)indices)[k] > most) most = ((const unsigned short *)indices)[k]; }
		else
			for (k = 0; k < count; k++) { if (((const unsigned char *)indices)[k] > most) most = ((const unsigned char *)indices)[k]; }
		extent = (most + 1) * (unsigned int)vsize;
	}
	key = (unsigned int)vertices & ~0x40000000u;
	h = (key >> 4) * 2654435761u;
	for (unsigned int probe = 0; probe < 16; probe++)
	{
		struct gr_entry *e = &gr_table[(h + probe) & (GR_SLOTS - 1)];

		if (e->key == key || !e->key)
		{
			if (!e->key)
			{
				e->key = key;
				e->place = (unsigned char)place;
				e->kind = (unsigned char)ge_kind_now;
			}
			e->read += bytes;
			if (extent > e->size)
				e->size = extent;
			return;
		}
	}
	gr_dropped++;
}

static int gr_by_value(const void *a, const void *b)
{
	const struct gr_entry *x = a, *y = b;
	/* reads per byte, best first (cross-multiplied, no division) */
	unsigned long long l = (unsigned long long)y->read * (x->size ? x->size : 1);
	unsigned long long r = (unsigned long long)x->read * (y->size ? y->size : 1);

	return l > r ? 1 : l < r ? -1 : 0;
}

static void ge_reads_report(unsigned long frames_now)
{
	extern long psp_game_time_peek(void);
	unsigned long span = frames_now - gr_frames_mark;
	unsigned long long ram = 0, taken;
	char line[512];
	int k, n = 0, at, step;
	static const unsigned int steps_kb[5] = { 200, 400, 600, 800, 1000 };

	if (ge_reads_on <= 0 || !gr_table || !span)
		return;
	at = snprintf(line, sizeof(line), "gereads: game time %ld, %lu frames, %lu draws; KB a frame by place:", psp_game_time_peek(), span, gr_draws);
	for (k = 0; k < GR_PLACES && at < (int)sizeof(line) - 1; k++)
		at += snprintf(line + at, sizeof(line) - at, " %s %.1f;", gr_place_names[k], gr_place_bytes[k] / 1024.0 / span);
	if (at < (int)sizeof(line) - 1)
		snprintf(line + at, sizeof(line) - at, " (indices %.1f of it)", gr_index_bytes / 1024.0 / span);
	platform_log("%s", line);
	at = snprintf(line, sizeof(line), "gereads: vertex KB a frame by kind:");
	for (k = 0; k < KINDS && at < (int)sizeof(line) - 1; k++)
		if (gr_kind_bytes[k])
			at += snprintf(line + at, sizeof(line) - at, " %s %.1f;", kind_names[k], gr_kind_bytes[k] / 1024.0 / span);
	platform_log("%s", line);
	/* the buffers that could be held: in main RAM, not this frame's data */
	for (k = 0; k < GR_SLOTS; k++)
		if (gr_table[k].key && gr_table[k].place != GR_VRAM && gr_table[k].place != GR_LIST)
		{
			gr_table[n] = gr_table[k];
			ram += gr_table[n].read;
			n++;
		}
	qsort(gr_table, n, sizeof(struct gr_entry), gr_by_value);
	at = snprintf(line, sizeof(line), "gereads: fit curve (%d buffers in main RAM, %.1f KB a frame read from them): VRAM holding the best",
		n, ram / 1024.0 / span);
	for (step = 0; step < 5 && at < (int)sizeof(line) - 1; step++)
	{
		unsigned long long held = 0;

		taken = 0;
		for (k = 0; k < n; k++)
			if (held + gr_table[k].size <= steps_kb[step] * 1024ull)
			{
				held += gr_table[k].size;
				taken += gr_table[k].read;
			}
		at += snprintf(line + at, sizeof(line) - at, " %u KB: %.0f%%;", steps_kb[step], ram ? 100.0 * taken / ram : 0.0);
	}
	platform_log("%s", line);
	at = snprintf(line, sizeof(line), "gereads: best buffers (place, kind, KB, KB read a frame):");
	for (k = 0; k < n && k < 8 && at < (int)sizeof(line) - 1; k++)
		at += snprintf(line + at, sizeof(line) - at, " %s/%s %.1f %.1f;", gr_place_names[gr_table[k].place], kind_names[gr_table[k].kind],
			gr_table[k].size / 1024.0, gr_table[k].read / 1024.0 / span);
	platform_log("%s%s", line, gr_dropped ? " (some buffers past the table's room weren't kept)" : "");
	memset(gr_table, 0, GR_SLOTS * sizeof(struct gr_entry));
	memset(gr_place_bytes, 0, sizeof(gr_place_bytes));
	memset(gr_kind_bytes, 0, sizeof(gr_kind_bytes));
	gr_index_bytes = 0;
	gr_draws = gr_dropped = 0;
	gr_frames_mark = frames_now;
}

static void __attribute__((cold)) arena_frame_end(void)
{
	struct arena_block *block;
	unsigned int free_total = 0, largest = 0;
	int overflowed;

	if (arena_state <= 0)
		return;
	for (block = (struct arena_block *)arena; block; block = arena_next(block))
		if (!block->used)
		{
			free_total += block->size;
			if (block->size > largest)
				largest = block->size;
		}
	arena_largest = largest;
	if (arena_compact_on < 0)
		arena_compact_on = !psp_keep("nocompact");
	if (!arena_compact_on)
		return;
	overflowed = arena_from_heap != arena_seen_from_heap;
	arena_seen_from_heap = arena_from_heap;
	if (free_total >= 64 * 1024 && largest < free_total / 2 &&
		(overflowed || stream_frame - arena_last_compact >= ARENA_COMPACT_EVERY))
	{
		arena_compact();
		arena_last_compact = stream_frame;
	}
	if (arena_heap_live)
		arena_migrate();
}

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

/* ---------- build 87: the stencil bake (tools/stencilbake.py, <map>.pstn)

A stencil (a light strip on a base wall, the cave's fungus) is its own
see-through surface a few millimetres in front of a wall: the Xbox's 24-bit
depth keeps them apart, the PSP's 16 bits can't, and no nudge was right at
every distance and slant (builds 52-85). The bake cuts each wall triangle
under a stencil along the stencil's outline (a constrained triangulation,
no T-junctions with the triangles beside it). Here:
	- a wall triangle the bake replaced is drawn as its pieces, in every pass
	  (base, lightmap, detail; shadows and lights too), from vertices added
	  to the material's copies;
	- the stencil is drawn on the very same pieces (same positions, same
	  matrices), only where its wall triangle was drawn this frame, with
	  depth EQUAL, as the lightmap and detail passes are: exact, no nudge;
	- its triangles the bake took are left out of the old path; any it
	  couldn't (not on a wall: the teleporter's beam) keep the old nudge.
"Stencils" in the debug menu: baked with EQUAL / baked with GEQUAL and two
depth steps of slack / the old nudge (the bake off) */

struct bake_affected
{
	unsigned short a, b, c, first, count, pad;
};

struct bake_wall
{
	unsigned int key, original, added, affected_count, piece_count;
	const float *added_vertices;          /* 8 words each: x y z u v lu lv, packed normal */
	const struct bake_affected *affected;
	const unsigned short *pieces;         /* 3 each, into the extended copies */
	unsigned char *flags;                 /* per original vertex: in a replaced triangle */
	unsigned short *hash;                 /* replaced triangle + 1, by its triple */
	unsigned int hash_mask;
	unsigned int *drawn_frame;            /* per replaced triangle: the frame it was drawn in */
};

struct bake_layer
{
	unsigned int wall_key, stencil_key, vertex_count, tri_count;
	struct ge_vertex *vertices;           /* the stencil's texture coordinates on the pieces' positions */
	const unsigned short *tris;           /* 4 each: three vertices, the wall's replaced triangle */
	struct bake_wall *wall;
};

struct bake_stencil
{
	unsigned int key, count;
	const unsigned short *triples;
	unsigned short *hash;
	unsigned int hash_mask;
};

static struct
{
	void *file;
	int wall_count, layer_count, stencil_count;
	struct bake_wall *walls;
	struct bake_layer *layers;
	struct bake_stencil *stencils;
} bake;
/* 0 baked, EQUAL; 1 baked, GEQUAL with STENCIL_SLACK steps; 2 the old nudge */
static int stencil_mode = 0;
#define STENCIL_SLACK 2
static unsigned long bake_substituted = 0, bake_pieces_drawn = 0, bake_layer_draws = 0, bake_layer_triangles = 0,
	bake_old_path_triangles = 0;

static unsigned int bake_triple_hash(unsigned int a, unsigned int b, unsigned int c)
{
	return (a * 73856093u) ^ (b * 19349663u) ^ (c * 83492791u);
}

static void bake_forget(void)
{
	int k;

	for (k = 0; k < bake.wall_count; k++)
	{
		free(bake.walls[k].flags);
		free(bake.walls[k].hash);
		free(bake.walls[k].drawn_frame);
	}
	for (k = 0; k < bake.stencil_count; k++)
		free(bake.stencils[k].hash);
	free(bake.walls);
	free(bake.layers);
	free(bake.stencils);
	free(bake.file);
	memset(&bake, 0, sizeof(bake));
}

static struct bake_wall *bake_wall_of(unsigned int key)
{
	int k;

	if (stencil_mode == 2)
		return NULL;
	for (k = 0; k < bake.wall_count; k++)
		if (bake.walls[k].key == key)
			return &bake.walls[k];
	return NULL;
}

static struct bake_stencil *bake_stencil_of(unsigned int key)
{
	int k;

	if (stencil_mode == 2)
		return NULL;
	for (k = 0; k < bake.stencil_count; k++)
		if (bake.stencils[k].key == key)
			return &bake.stencils[k];
	return NULL;
}

/* the map's .pstn, when its geometry pack opens (psp_geo.c) */
void psp_ge_stencils_open(const char *name, unsigned int map_crc)
{
	GE_DEFER_DRAIN(); /* build 112: gedefer */
	char path[128];
	SceUID file;
	int size, k;
	unsigned int header[16];
	const unsigned char *at, *end;

	bake_forget();
	snprintf(path, sizeof(path), "ms0:/halo/maps/%s.pstn", name);
	file = sceIoOpen(path, PSP_O_RDONLY, 0);
	if (file < 0)
	{
		platform_log("stencils: no %s: the old nudge for every stencil", path);
		return;
	}
	size = sceIoLseek32(file, 0, PSP_SEEK_END);
	sceIoLseek32(file, 0, PSP_SEEK_SET);
	bake.file = size > 64 ? memalign(64, size) : NULL;
	if (!bake.file || sceIoRead(file, bake.file, size) != size)
	{
		sceIoClose(file);
		platform_log("stencils: %s unreadable (%d bytes)", path, size);
		bake_forget();
		return;
	}
	sceIoClose(file);
	memcpy(header, bake.file, sizeof(header));
	if (header[0] != 0x4E545350 /* PSTN */ || header[1] != 1)
	{
		platform_log("stencils: %s is not a stencil bake", path);
		bake_forget();
		return;
	}
	if (map_crc && header[2] != map_crc)
	{
		platform_log("stencils: %s was baked from another build of the map (crc %08x, map %08x): not used", path,
			header[2], map_crc);
		bake_forget();
		return;
	}
	bake.walls = calloc(header[3] + 1, sizeof(struct bake_wall));
	bake.layers = calloc(header[4] + 1, sizeof(struct bake_layer));
	bake.stencils = calloc(header[5] + 1, sizeof(struct bake_stencil));
	if (!bake.walls || !bake.layers || !bake.stencils)
	{
		bake_forget();
		return;
	}
	at = (const unsigned char *)bake.file + 64;
	end = (const unsigned char *)bake.file + size;
	/* build 89: every record checked against the file's end and its indices
	against their arrays (a damaged or half-copied file is refused whole),
	and stored at the count of good ones: b87-b88 stored at the loop index
	while counting only successes, so a wall whose allocation failed sat
	inside the counted range with NULL tables (review) */
#define BAKE_ROOM(bytes) ((unsigned int)(end - at) >= (unsigned int)(bytes))
	for (k = 0; k < (int)header[3]; k++)
	{
		struct bake_wall *w = &bake.walls[bake.wall_count];
		const unsigned int *h = (const unsigned int *)at;
		unsigned int i, slots, vertex_limit;

		if (!BAKE_ROOM(20) || h[2] > 65535 || h[3] > 65535 || h[4] > 1000000 ||
			!BAKE_ROOM(20 + h[2] * 32 + h[3] * 12 + h[4] * 6))
			goto damaged;
		w->key = h[0]; w->original = h[1]; w->added = h[2]; w->affected_count = h[3]; w->piece_count = h[4];
		at += 20;
		w->added_vertices = (const float *)at;
		at += w->added * 32;
		w->affected = (const struct bake_affected *)at;
		at += w->affected_count * 12;
		w->pieces = (const unsigned short *)at;
		at += w->piece_count * 6;
		at = (const unsigned char *)(((unsigned int)at + 3) & ~3u);
		if (at > end || w->original > 65535)
			goto damaged;
		vertex_limit = w->original + w->added;
		for (i = 0; i < w->affected_count; i++)
			if ((unsigned int)w->affected[i].first + w->affected[i].count > w->piece_count)
				goto damaged;
		for (i = 0; i < w->piece_count * 3; i++)
			if (w->pieces[i] >= vertex_limit)
				goto damaged;
		for (slots = 16; slots < w->affected_count * 2; slots <<= 1)
			;
		w->hash_mask = slots - 1;
		w->hash = calloc(slots, sizeof(unsigned short));
		w->flags = calloc(w->original ? w->original : 1, 1);
		w->drawn_frame = calloc(w->affected_count ? w->affected_count : 1, sizeof(unsigned int));
		if (!w->hash || !w->flags || !w->drawn_frame)
		{
			free(w->hash);
			free(w->flags);
			free(w->drawn_frame);
			memset(w, 0, sizeof(*w));
			continue;
		}
		for (i = 0; i < w->affected_count; i++)
		{
			const struct bake_affected *t = &w->affected[i];
			unsigned int slot = bake_triple_hash(t->a, t->b, t->c) & w->hash_mask;

			while (w->hash[slot])
				slot = (slot + 1) & w->hash_mask;
			w->hash[slot] = (unsigned short)(i + 1);
			if (t->a < w->original) w->flags[t->a] = 1;
			if (t->b < w->original) w->flags[t->b] = 1;
			if (t->c < w->original) w->flags[t->c] = 1;
		}
		bake.wall_count++;
	}
	for (k = 0; k < (int)header[4]; k++)
	{
		struct bake_layer *l = &bake.layers[bake.layer_count];
		const unsigned int *h = (const unsigned int *)at;
		unsigned int i;
		int w;

		if (!BAKE_ROOM(16) || h[2] > 65535 || h[3] > 65535 || !BAKE_ROOM(16 + h[2] * 20 + h[3] * 8))
			goto damaged;
		memset(l, 0, sizeof(*l));
		l->wall_key = h[0]; l->stencil_key = h[1]; l->vertex_count = h[2]; l->tri_count = h[3];
		at += 16;
		l->vertices = (struct ge_vertex *)at;
		at += l->vertex_count * 20;
		l->tris = (const unsigned short *)at;
		at += l->tri_count * 8;
		for (i = 0; i < l->tri_count; i++)
			if (l->tris[4 * i] >= l->vertex_count || l->tris[4 * i + 1] >= l->vertex_count ||
				l->tris[4 * i + 2] >= l->vertex_count)
				goto damaged;
		for (w = 0; w < bake.wall_count; w++)
			if (bake.walls[w].key == l->wall_key)
				l->wall = &bake.walls[w];
		if (l->wall)
			bake.layer_count++;
	}
	for (k = 0; k < (int)header[5]; k++)
	{
		struct bake_stencil *st = &bake.stencils[bake.stencil_count];
		const unsigned int *h = (const unsigned int *)at;
		unsigned int i, slots;

		if (!BAKE_ROOM(8) || h[1] > 65535 || !BAKE_ROOM(8 + h[1] * 6))
			goto damaged;
		st->key = h[0]; st->count = h[1];
		at += 8;
		st->triples = (const unsigned short *)at;
		at += st->count * 6;
		at = (const unsigned char *)(((unsigned int)at + 3) & ~3u);
		if (at > end)
			goto damaged;
		for (slots = 16; slots < st->count * 2; slots <<= 1)
			;
		st->hash_mask = slots - 1;
		st->hash = calloc(slots, sizeof(unsigned short));
		if (!st->hash)
			continue;
		for (i = 0; i < st->count; i++)
		{
			unsigned int slot = bake_triple_hash(st->triples[3 * i], st->triples[3 * i + 1], st->triples[3 * i + 2]) & st->hash_mask;

			while (st->hash[slot])
				slot = (slot + 1) & st->hash_mask;
			st->hash[slot] = (unsigned short)(i + 1);
		}
		bake.stencil_count++;
	}
	/* the layers' vertices are read by the GE */
	ge_writeback(bake.file, size);
	platform_log("stencils: %s: %d walls, %d stencil layers, %d stencil materials baked", path, bake.wall_count,
		bake.layer_count, bake.stencil_count);
	return;
damaged:
	platform_log("stencils: %s is damaged (a record runs past the file or names a vertex it doesn't have): not used, the old nudge for every stencil", path);
	bake_forget();
#undef BAKE_ROOM
}

/* which replaced triangle a triple is (-1: none) */
static int bake_find(const struct bake_wall *w, unsigned int a, unsigned int b, unsigned int c)
{
	unsigned int slot;

	if (a >= w->original || b >= w->original || c >= w->original || !w->flags[a] || !w->flags[b] || !w->flags[c])
		return -1;
	slot = bake_triple_hash(a, b, c) & w->hash_mask;
	while (w->hash[slot])
	{
		const struct bake_affected *t = &w->affected[w->hash[slot] - 1];

		if (t->a == a && t->b == b && t->c == c)
			return w->hash[slot] - 1;
		slot = (slot + 1) & w->hash_mask;
	}
	return -1;
}

static void *list_take(int bytes);

/* a draw's triangles with the replaced ones swapped for their pieces (in the
display list's memory); the game's own list when none are in it. mark: the
pieces are being drawn (the stencils on them may be drawn this frame) */
static const unsigned short *bake_substitute(struct level_stream *stream, const unsigned short *indices,
	long *triangle_count, int mark)
{
	struct bake_wall *w = stream->bake;
	long t, extra = 0, found = 0, out = 0;
	unsigned short *list;

	if (!w || stencil_mode == 2)
		return indices;
	for (t = 0; t < *triangle_count; t++)
	{
		int r = bake_find(w, indices[3 * t], indices[3 * t + 1], indices[3 * t + 2]);

		if (r >= 0)
		{
			extra += w->affected[r].count - 1;
			found++;
		}
	}
	if (!found)
		return indices;
	list = (unsigned short *)list_take((int)((*triangle_count + extra) * 3 * sizeof(unsigned short) + 3) & ~3);
	if (!list)
		return indices;
	for (t = 0; t < *triangle_count; t++)
	{
		const unsigned short *tri = indices + 3 * t;
		int r = bake_find(w, tri[0], tri[1], tri[2]);

		if (r < 0)
		{
			list[3 * out] = tri[0];
			list[3 * out + 1] = tri[1];
			list[3 * out + 2] = tri[2];
			out++;
			continue;
		}
		memcpy(list + 3 * out, w->pieces + 3 * w->affected[r].first, w->affected[r].count * 3 * sizeof(unsigned short));
		out += w->affected[r].count;
		if (mark)
		{
			w->drawn_frame[r] = stream_frame;
			bake_pieces_drawn += w->affected[r].count;
		}
	}
	if (mark)
		bake_substituted += found;
	*triangle_count = out;
	return list;
}

/* build 89: what a material's copies take: the float copies, the packed
normals and incident radiosity, the 16-bit copies (the budget's count; the
16-bit ones and the radiosity were left out before) */
static unsigned int stream_size(const struct level_stream *stream)
{
	unsigned int count = (unsigned int)stream->count, size;

	size = count * sizeof(struct ge_vertex) * (stream->lightmap ? 2 : 1) + count * sizeof(unsigned int);
	if (stream->radiosity)
		size += count * sizeof(unsigned int);
	if (stream->compact_base)
		size += count * sizeof(struct compact_vertex);
	if (stream->compact_lightmap)
		size += count * sizeof(struct compact_vertex);
	return size;
}

/* build 112: level copies that work marked as the Media Engine's needed and
couldn't build (gemesim): building reads the geometry cache, which may start
a stick read, and may fall back on the heap, all system calls. The main CPU
builds them at the frame's end (psp_ge_frame_end), so they're there for the
next frame; the draw is skipped meanwhile, as for geometry still on its way.
Build 113: 128 (b112's Test 3 asked for 113 in a frame and 14 didn't fit;
12 bytes each) */
#define STREAMS_WANTED 128
static struct
{
	const void *vertices, *lightmap_vertices;
	int count;
} streams_wanted[STREAMS_WANTED];
static int streams_wanted_count = 0;
static unsigned long streams_wanted_total = 0, streams_wanted_dropped = 0;

/* a level copy already built, or NULL; changes nothing (with the shadows) */
static struct level_stream *level_stream_find(const void *vertices);

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
	if (PSP_ME_WORK())
	{
		int k;

		for (k = 0; k < streams_wanted_count && streams_wanted[k].vertices != vertices; k++)
			;
		if (k == streams_wanted_count)
		{
			if (k < STREAMS_WANTED)
			{
				streams_wanted[k].vertices = vertices;
				streams_wanted[k].lightmap_vertices = lightmap_vertices;
				streams_wanted[k].count = count;
				streams_wanted_count++;
				streams_wanted_total++;
			}
			else
				streams_wanted_dropped++;
		}
		return NULL;
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
	stream->original_count = count;
	stream->last_frame = stream_frame;
	stream->uv_per_unit = 0.0f;
	/* build 87: room for the stencil bake's pieces' vertices after the
	material's own */
	stream->bake = bake_wall_of((unsigned int)key);
	if (stream->bake && (stream->bake->original != (unsigned int)count || count + stream->bake->added > 65535))
		stream->bake = NULL;
	{
		int total = count + (stream->bake ? (int)stream->bake->added : 0);

		stream->base = stream_alloc(total * sizeof(struct ge_vertex), (void **)&stream->base);
		stream->lightmap = lightmap_vertices ? stream_alloc(total * sizeof(struct ge_vertex), (void **)&stream->lightmap) : NULL;
		/* build 111: in the copies' arena too (not VRAM: the CPU reads these) */
		stream->normals = copy_alloc(total * sizeof(unsigned int), (void **)&stream->normals);
		stream->radiosity = lightmap_vertices ? copy_alloc(total * sizeof(unsigned int), (void **)&stream->radiosity) : NULL;
		if (stream->radiosity)
			memset(stream->radiosity, 0, total * sizeof(unsigned int));
	}
	if (!stream->base || (lightmap_vertices && (!stream->lightmap || !stream->radiosity)) || !stream->normals)
	{
		platform_log("GE: no memory for a material's vertices (%d)", count);
		stream_free(stream->base);
		stream_free(stream->lightmap);
		copy_free(stream->normals);
		copy_free(stream->radiosity);
		stream->base = stream->lightmap = NULL;
		stream->normals = stream->radiosity = NULL;
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
			stream->radiosity[index] = *(const unsigned int *)((const char *)lightmap_vertices + 8 * index);
		}
	}
	if (stream->bake)
	{
		/* build 87: the pieces' vertices (inside the wall, so the box holds) */
		unsigned int added;

		for (added = 0; added < stream->bake->added; added++)
		{
			const float *a = stream->bake->added_vertices + 8 * added;
			struct ge_vertex *b = &stream->base[count + added];

			b->x = a[0]; b->y = a[1]; b->z = a[2];
			b->u = a[3]; b->v = a[4];
			stream->normals[count + added] = ((const unsigned int *)a)[7];
			if (stream->lightmap)
			{
				struct ge_vertex *l = &stream->lightmap[count + added];

				l->x = a[0]; l->y = a[1]; l->z = a[2];
				l->u = a[5]; l->v = a[6];
			}
		}
		count += stream->bake->added;
		stream->count = count;
	}
	if (level_compact < 0)
		level_compact = psp_keep("levelcompact");
	stream->compact_base = stream->compact_lightmap = NULL;
	if (level_compact)
		compact_build(stream);
	ge_writeback(stream->base, count * sizeof(struct ge_vertex));
	if (stream->lightmap)
		ge_writeback(stream->lightmap, count * sizeof(struct ge_vertex));
	stream_bytes += stream_size(stream);
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

/* build 107: the tangent of half the field of view, worked out when the
field of view changes (the decals asked for it twice a call, 800 calls a
frame in b30's fight: tanf was 0.4% of the beach's time) */
static float fov_tan_half(void)
{
	static float fov = -1.0f, value = 1.0f;

	if (projection_fov != fov)
	{
		fov = projection_fov;
		value = tanf(projection_fov * 0.5f);
	}
	return value;
}

static float pixels_per_unit(void)
{
	return (SCREEN_HEIGHT * 0.5f) / fov_tan_half();
}

/* texture coordinate units per world unit over a draw's triangles */
static float uv_density(const struct ge_vertex *stream, const unsigned short *indices, long triangle_count)
{
	/* build 102: in single precision (doubles are software on the PSP) */
	float uv_area = 0.0f, world_area = 0.0f;
	long t;

	for (t = 0; t < triangle_count && t < 512; t++)
	{
		const struct ge_vertex *a = &stream[indices[3 * t]], *b = &stream[indices[3 * t + 1]], *c = &stream[indices[3 * t + 2]];
		float e1[3] = { b->x - a->x, b->y - a->y, b->z - a->z }, e2[3] = { c->x - a->x, c->y - a->y, c->z - a->z };
		float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };

		world_area += sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		uv_area += fabsf((b->u - a->u) * (c->v - a->v) - (c->u - a->u) * (b->v - a->v));
	}
	return world_area > 0.0f ? sqrtf(uv_area / world_area) : 0.0f;
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

/* build 87: one triangle's slant bucket (plus ANGLE_BUCKETS when wholly
past the detail fade). The main CPU's loop below and the Media Engine's
measure job (psp_ge_me_angle) both use this, so the two compute the very same
expressions in the same order (no fused multiply-adds: -ffp-contract=off) and
must agree bit for bit. No library calls: the ME runs it. Build 119:
always written in (GCC kept it a call, 5-8.5% of the drawing's instructions
on a30, c20 and d20 by the cache model: hotlines119); the same operations in
the same order, so the same buckets */
static inline __attribute__((always_inline)) int angle_bucket_one(const struct ge_vertex *stream, const unsigned short *triangle, float ex, float ey,
	float ez, int far_possible, float far_squared)
{
	const struct ge_vertex *a = &stream[triangle[0]], *p = &stream[triangle[1]], *q = &stream[triangle[2]];
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

		if (sa > far_squared)
		{
			float dp[3] = { p->x - ex, p->y - ey, p->z - ez };
			float sp = dp[0] * dp[0] + dp[1] * dp[1] + dp[2] * dp[2];

			if (sp > far_squared)
			{
				float dq[3] = { q->x - ex, q->y - ey, q->z - ez };

				if (dq[0] * dq[0] + dq[1] * dq[1] + dq[2] * dq[2] > far_squared)
					bucket += ANGLE_BUCKETS;
			}
		}
	}
	return bucket;
}

/* build 87, the like-for-like: the same function on the main CPU over the
same range, its cache emptied of the range first as the ME's is, timed, and
its answers against the ME's */
static unsigned long me_classify_sc_us = 0, me_classify_sc_mismatches = 0;
static unsigned long me_classify_me_us = 0;
/* build 87: the second measure job, the slant buckets (angle_sort's loop:
three vertices fetched per triangle from wherever they lie, the shape of the
renderer's most expensive CPU step on hardware, 3.8 ms a frame in b86) */
#if PSP_DEBUG_TOOLS
static unsigned char me_buckets[ANGLE_TRIANGLES + 64] __attribute__((aligned(64)));
static float me_angle_view[16] __attribute__((aligned(64)));
#endif
static unsigned long me_angle_jobs = 0, me_angle_triangles = 0, me_angle_mismatches = 0, me_angle_failures = 0,
	me_angle_wait_us = 0, me_angle_sc_us = 0;
static unsigned long me_angle_me_us = 0;
static void me_angle_measure(const struct ge_vertex *stream, const unsigned short *indices, long count,
	int far_possible);
static unsigned long angle_loop_us = 0;

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
	{
		unsigned int loop_start = ge_clock();

		for (t = 0; t < count; t++)
		{
			int bucket = angle_bucket_one(stream, indices + 3 * t, ex, ey, ez, far_possible, detail_far_squared);

			angle_bucket_of[t] = (unsigned char)bucket;
			total[bucket]++;
		}
		angle_loop_us += ge_clock() - loop_start;
	}
	/* build 87: the ME works out the same buckets ("meclassify", measure only) */
	me_angle_measure(stream, indices, count, far_possible);
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
	if (index_count > 0 && compact_now && (lightmap ? compact_now->compact_lightmap : compact_now->compact_base))
	{
		/* build 87: the 16-bit copies, through the material's own matrix */
		compact_begin(lightmap);
		sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_INDEX_16BIT | GU_TRANSFORM_3D,
			index_count, indices, lightmap ? compact_now->compact_lightmap : compact_now->compact_base);
		compact_end();
	}
	else if (index_count > 0)
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
	if (compact_now && compact_now->compact_base && !lightmap)
		compact_begin(0);
	for (b = 0; b < buckets; b++)
	{
		if (!angle_count[b])
			continue;
		sceGuTexSlope(slope * angle_factor[b % ANGLE_BUCKETS]);
		if (compact_now && compact_now->compact_base && !lightmap)
			sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_INDEX_16BIT | GU_TRANSFORM_3D,
				(int)angle_count[b] * 3, indices + 3 * angle_first[b], compact_now->compact_base);
		else
			sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
				(int)angle_count[b] * 3, indices + 3 * angle_first[b], stream);
	}
	if (compact_now && compact_now->compact_base && !lightmap)
		compact_end();
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

/* build 116 (b113 #7): the grey a detail pass fades to, its texels' own
neutral: texpack.py bakes a detail map around 0.5, which a 16-bit (5650)
palette holds as 0x7B7D7B. Builds 31-115 faded to 0x808080, about 4%
brighter a pass, so the terrain and the walls brightened in a band from 20
to 60 units (b113 #7). A 32-bit palette holds 0x808080 itself. Keep word
"detailgrey80": as before */
static unsigned int detail_neutral(unsigned int key)
{
	extern int psp_tex_palette16(unsigned int key, const unsigned short **palette, int *count);
	static int old = -1;
	const unsigned short *palette;
	int count;

	if (old < 0)
		old = psp_keep("detailgrey80");
	return !old && psp_tex_palette16(key, &palette, &count) ? 0xFF7B7D7B : 0xFF808080;
}

void psp_ge_level_detail(unsigned int primary_key, float primary_u, float primary_v,
	unsigned int micro_key, float micro_u, float micro_v)
{
	GE_DEFER(defer_words(DEFER_LEVEL_DETAIL, 6, primary_key, defer_bits(primary_u), defer_bits(primary_v), micro_key,
		defer_bits(micro_u), defer_bits(micro_v), 0, 0)); /* build 112: gedefer */
	detail_key[0] = primary_key; detail_u[0] = primary_u; detail_v[0] = primary_v;
	detail_key[1] = micro_key; detail_u[1] = micro_u; detail_v[1] = micro_v;
}

#define CODE_UNKNOWN 0x80
static unsigned char vertex_codes[65536];
static int box_test = -1;
static unsigned long budget_renderer_us = 0, budget_wait_us = 0;
/* build 87: the same over the heartbeat's own window (benchrun's results
need the window they name; the 300-frame figures lag it) */
static PSP_MAIN_SIDE unsigned long hb_renderer_us = 0, hb_wait_us = 0, hb_frames = 0, entry_snapshot = 0;

/* build 64: the renderer's CPU time and the wait for the GE, us a frame, as
last logged (the heartbeat's budget line) */
void psp_ge_budget(unsigned long *renderer_us, unsigned long *wait_us)
{
	if (hb_frames)
	{
		*renderer_us = hb_renderer_us / hb_frames;
		*wait_us = hb_wait_us / hb_frames;
		hb_renderer_us = hb_wait_us = hb_frames = 0;
		return;
	}
	*renderer_us = budget_renderer_us;
	*wait_us = budget_wait_us;
}
#define MAXIMUM_CUT_CORNERS 384 /* the peak seen on the PSP is 249 (build 32); build 60: 768 to 512, room for model_cut */
static float cut_corners[MAXIMUM_CUT_CORNERS][CORNER_FLOATS];

static int level_cull = -1;

static void me_classify_measure(const struct level_stream *stream, unsigned int lowest, unsigned int highest);
static unsigned long me_classify_jobs, me_classify_vertices, me_classify_mismatches, me_classify_failures,
	me_classify_wait_us;

/* build 90: this frame's level draws as the base pass drew them (stream,
triangle list, cut-up corners), for the permanent decals (psp_ge_decal),
which are drawn afterwards on these very triangles with depth EQUAL: the
stencil bake's method, without a bake (the game builds these decals as the
map loads). Sheepdog01's b87 run 2: the scenario's painted symbols on the base
blocks partly sank into them on the PSP (whole in the emulator) */
#define FRAME_LEVEL_DRAWS 96
static struct frame_level_draw
{
	const struct level_stream *stream;
	const unsigned short *indices;
	int kept;
	const struct ge_vertex *cut;
	int cut_count;
} frame_level[FRAME_LEVEL_DRAWS];
static int frame_level_count = 0;
static unsigned int frame_level_stamp = 0;

static void frame_level_record(const struct level_stream *stream, const unsigned short *indices, int kept,
	const struct ge_vertex *cut, int cut_count)
{
	if (frame_level_stamp != stream_frame)
	{
		frame_level_stamp = stream_frame;
		frame_level_count = 0;
	}
	if (frame_level_count >= FRAME_LEVEL_DRAWS)
		return;
	frame_level[frame_level_count].stream = stream;
	frame_level[frame_level_count].indices = indices;
	frame_level[frame_level_count].kept = kept;
	frame_level[frame_level_count].cut = cut;
	frame_level[frame_level_count].cut_count = cut ? cut_count : 0;
	frame_level_count++;
}

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
	/* build 87: the stencil bake's pieces in place of the triangles it cut */
	if (stream->bake)
	{
		const unsigned short *swapped = bake_substitute(stream, indices, &triangle_count, 1);

		if (swapped != indices)
		{
			indices = draw = swapped;
			vertex_count = stream->count;
		}
	}
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
				unsigned int cut_start = ge_clock();

				cut_count += clip_triangle(corners, bits[0] | bits[1] | bits[2], cut_corners + cut_count,
					MAXIMUM_CUT_CORNERS - cut_count);
				cut_us += ge_clock() - cut_start;
			}
		}
	}
	ge_part_us[2] += cut_us;
	ge_part_calls[2]++;
	part_add(1);
	ge_part_us[1] -= cut_us;
	/* build 87: the ME classifies the same range again ("meclassify") */
	if (!all_inside)
		me_classify_measure(stream, lowest, highest);
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
		ge_writeback(indices, triangle_count * 3 * sizeof(unsigned short));
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
		unsigned int angle_start = ge_clock();

		draw = angle_sort(stream->base, draw, kept_count, far_possible);
		angle_us += ge_clock() - angle_start;
		/* build 89: the pieces cut at the eye are near by definition: with
		every kept triangle past the detail fade, the detail passes were
		skipped for them too (the ground at the feet lost its detail map when
		the rest of a large material's visible triangles were far; review) */
		if (cut_count > 0)
			angle_near_any = 1;
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

		/* build 87: the 16-bit copies for this draw's passes, when built */
		compact_now = stream->compact_base ? stream : NULL;
		pass_scale_u = pass_scale_v = 1.0f;
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
		if (!skip_lightmap && stream->lightmap && psp_tex_bind_lightmap(lightmap_key))
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
			only the detail maps are doubled (build 35: was 2 x, overbright).
			Dithered: this pass sets the level's brightness (build 117) */
			sceGuBlendFunc(GU_ADD, GU_DST_COLOR, GU_FIX, 0, 0);
			ge_blend_dithered();
			draw_pass(stream->lightmap, draw, (int)kept_count * 3, cut_corners, cut_count, 1);
			ge_blend(0);
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
				pass_scale_u = detail_u[pass];
				pass_scale_v = detail_v[pass];
				if (uv_centre)
					uv_shift(detail_u[pass], detail_v[pass]);
				sceGuDepthFunc(GU_EQUAL);
				sceGuDepthMask(GU_TRUE);
				sceGuFog(near, far > near ? far : near + 1.0f, detail_neutral(detail_key[pass]));
				sceGuBlendFunc(GU_ADD, GU_DST_COLOR, GU_SRC_COLOR, 0, 0);
				ge_blend(1);
				/* build 91: a detail map's smaller levels are faded toward a
				colour other than their top level's average (Bungie's detail
				fade: measured on Blood Gulch, "detail metal sratches" loses
				43% of its brightness by its eighth level, "detail grass" turns
				purple), so two triangles side by side in different slant
				buckets showed a step in brightness along their shared edge
				(Sheepdog01, b90 run 1, the base floors). The GE's slope mode picks the
				level per pixel by depth, which changes smoothly; the buckets
				only add the slant, per triangle. Base maps keep the buckets
				(their levels keep their average). "slant per triangle (old)"
				in the debug menu restores them here */
				if (detail_by_distance)
				{
					sceGuTexSlope(detail_pass_slope);
					draw_pass(stream->base, draw, (int)kept_count * 3, cut_corners, cut_count, 0);
				}
				else
					draw_pass_angled(stream->base, draw, (int)kept_count * 3, cut_corners, cut_count, 0, detail_pass_slope, 1);
				detail_passes++;
			}
			ge_blend(0);
			sceGuTexLevelMode(tex_level_mode, tex_bias);
			sceGuTexScale(1.0f, 1.0f);
			pass_scale_u = pass_scale_v = 1.0f;
			sceGuTexOffset(0.0f, 0.0f);
			sceGuFog(fog_near, fog_far, fog_color);
			sceGuDepthMask(GU_FALSE);
			sceGuDepthFunc(GU_GEQUAL);
		}
		sceGuDisable(GU_TEXTURE_2D);
		sceGuTexLevelMode(tex_level_mode, tex_bias);
		/* build 90: for the permanent decals (not with the 16-bit copies,
		which go through another vertex path) */
		if (!compact_now && !alpha_tested)
			frame_level_record(stream, draw, (int)kept_count,
				cut_count > 0 && cut_count == cut_vertices_count ? cut_vertices_prepared[0] : NULL, cut_count);
		compact_now = NULL;
	}
	else
	{
		/* not baked, or still on its way from the Memory Stick: flat */
		sceGuColor(color);
		compact_now = stream->compact_base ? stream : NULL;
		pass_scale_u = pass_scale_v = 1.0f;
		draw_pass(stream->base, draw, (int)kept_count * 3, cut_corners, cut_count, 0);
		compact_now = NULL;
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
	GE_DEFER(defer_draw_level(vertices, lightmap_vertices, vertex_count, indices, triangle_count, color, base_key,
		lightmap_key, alpha_tested)); /* build 112: gedefer */
	GE_BENCH_SKIP();
	ge_kind(KIND_LEVEL); /* build 103 */
	unsigned int start = ge_clock();

	ge_category = GE_SKIP_LEVEL;
	psp_ge_draw_level_body(vertices, lightmap_vertices, vertex_count, indices, triangle_count, color, base_key, lightmap_key, alpha_tested);
	ge_category = 0;
	ge_entry_us[0] += ge_clock() - start;
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
/* build 117: and up to two point lights (the GE's lights 2 and 3), each a
place and a colour already scaled by its reach at the model's centre
(psp_fixups.py, the model's begin) */
static int model_point_count = 0;
static unsigned int model_point_color[2];
static ScePspFVector3 model_point_position[2];
static unsigned long model_point_parts = 0;

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
	GE_BENCH_SKIP();
	if (DEFER_RECORDING() &&
		defer_model_begin(node_matrices, node_matrix_count, u_scale, v_scale, ambient, light_count, light0, light1))
		return;
	int index;

	model_nodes = node_matrices;
	model_node_count = node_matrix_count;
	model_u_scale = u_scale != 0.0f ? u_scale : 1.0f;
	model_v_scale = v_scale != 0.0f ? v_scale : 1.0f;
	model_ambient = ambient ? color_from_rgb(ambient) : MODEL_LIGHT;
	model_light_count = light_count < 0 ? 0 : light_count > 2 ? 2 : light_count;
	model_point_count = 0; /* build 117: psp_ge_model_point_light follows */
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

/* build 117: one of the object's point lights, after psp_ge_model_begin */
void psp_ge_model_point_light(const float *position, unsigned int color)
{
	GE_BENCH_SKIP();
	GE_DEFER(defer_model_point_light(position, color));
	if (model_point_count >= 2 || !position)
		return;
	/* the GE lights after the model matrix, and this file's model matrices
	have the camera at the origin (relative), so the light's place is
	given from the camera too (an independent review; at replay, after
	GE_DEFER, the camera is the one the model is drawn from) */
	model_point_position[model_point_count].x = position[0] - camera_eye[0];
	model_point_position[model_point_count].y = position[1] - camera_eye[1];
	model_point_position[model_point_count].z = position[2] - camera_eye[2];
	model_point_color[model_point_count] = color;
	model_point_count++;
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
	GE_BENCH_SKIP();
	GE_DEFER(defer_model_shape(first_person, centroid, radius, near_fp, far_fp));
	model_sphere_radius = 0.0f; /* build 118: none until this model's is known */
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
	model_pixels = 1.0e9f;
	if (!first_person && centroid && radius > 0.0f)
	{
		float c[3];

		to_camera(centroid, c);
		/* build 118: kept for the model's see-through parts (transparent_cut) */
		model_sphere[0] = c[0];
		model_sphere[1] = c[1];
		model_sphere[2] = c[2];
		model_sphere_radius = radius;
		/* build 107: the sphere's diameter on the screen, for the size floor */
		if (c[2] > radius)
			model_pixels = 2.0f * radius * (SCREEN_HEIGHT * 0.5f) / (fov_tan_half() * c[2]); /* build 114: the remembered tangent (b112 cache model: tanf 14,000 instructions a frame) */
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
			near_plane_auto). Build 90: only while the player sits in a
			vehicle (the one he is in). On foot against the tank, the eye is
			inside its large sphere too, and b84-b89 then let the near plane
			out to 25 cm and cut the hull open (Sheepdog01's b87 shot 029): on foot
			such a sphere counts as touching the eye. Build 116: and in a
			first-person seat (psp_player_seated 2: a Pelican's bench) */
			if (reach < 0.0f && player_seated != 1)
				reach = 0.0f;
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
	GE_BENCH_SKIP();
	GE_DEFER(defer_model_effect(type, intensity));
	model_camouflage = type == 1 && intensity > 0.0f ? (intensity > 1.0f ? 1.0f : intensity) : 0.0f;
}

/* build 114: "statememo" for lighting: the GE's lighting state as this file
last set it, so a part lit as the GE already is sends nothing. Each part
turns lighting on with the ambient and both lights (about 17 command words
and a dozen pspgu calls), draws, and turns it off for its later passes; the
parts of one object share their lighting. Only this file sets the GE's
lighting: model_lighting, the start-up's "off" (lit_forget(0)), and the
two-sided pass's flipped lights (lit_forget(1)). Forgotten at each frame's
start, and never used in the Media Engine's work */
static int lit_memo = -1, lit_enabled = -1, lit_params = 0, lit_on[2];
static unsigned int lit_ambient, lit_color[2];
static ScePspFVector3 lit_toward[2];
/* build 117: the point lights' (lights 2 and 3) */
static int lit_point_on[2];
static unsigned int lit_point_color[2];
static ScePspFVector3 lit_point_position[2];
/* whether the GE may have light 2 or 3 on, memo or not, so a model without
point lights sends no disables when they're already off (nopointlights
then sends what b116 sent) */
static int lit_points_maybe_on = 0;
static unsigned long lit_sets_sent = 0, lit_sets_memo = 0;

/* lighting changed outside model_lighting: 0 turned off; 1 the lights'
parameters; 2 everything unknown (a frame's start) */
static void lit_forget(int what)
{
	lit_enabled = what == 0 ? 0 : what == 2 ? -1 : lit_enabled;
	lit_params = 0;
}

static void model_lighting(int on)
{
	int index, sent = 0;

	if (lit_memo < 0)
		lit_memo = psp_keep("statememo") || psp_keep("memolight"); /* memolight: this half alone */
	if (!lit_memo || PSP_ME_WORK())
	{
		lit_enabled = -1;
		lit_params = 0;
	}
	if (!on || (!model_light_count && !model_point_count && model_ambient == 0xFF000000))
	{
		if (lit_enabled != 0)
			sceGuDisable(GU_LIGHTING);
		if (lit_memo && !PSP_ME_WORK())
			lit_enabled = 0;
		return;
	}
	if (lit_enabled != 1)
		sceGuEnable(GU_LIGHTING); /* (each part turns lighting off for its later passes: not counted) */
	if (!lit_params || lit_ambient != model_ambient)
	{
		sceGuAmbient(model_ambient);
		sent = 1;
	}
	for (index = 0; index < 2; index++)
	{
		if (index < model_light_count)
		{
			if (!lit_params || memcmp(&lit_toward[index], &model_light_toward[index], sizeof(ScePspFVector3)) != 0)
			{
				sceGuLight(index, GU_DIRECTIONAL, GU_DIFFUSE, &model_light_toward[index]);
				sent = 1;
			}
			if (!lit_params || lit_color[index] != model_light_color[index])
			{
				sceGuLightColor(index, GU_DIFFUSE, model_light_color[index]);
				sent = 1;
			}
			if (!lit_params || !lit_on[index])
			{
				sceGuEnable(GU_LIGHT0 + index);
				sent = 1;
			}
		}
		else if (!lit_params || lit_on[index])
		{
			sceGuDisable(GU_LIGHT0 + index);
			sent = 1;
		}
	}
	/* build 117: the point lights, as lights 2 and 3: a place and a colour
	already scaled by the reach at the model's centre, so no attenuation;
	the GE shades each vertex by its angle to the light */
	for (index = 0; index < 2; index++)
	{
		if (index < model_point_count)
		{
			if (!lit_params || !lit_point_on[index] ||
				memcmp(&lit_point_position[index], &model_point_position[index], sizeof(ScePspFVector3)) != 0)
			{
				sceGuLight(2 + index, GU_POINTLIGHT, GU_DIFFUSE, &model_point_position[index]);
				sceGuLightAtt(2 + index, 1.0f, 0.0f, 0.0f);
				sent = 1;
			}
			if (!lit_params || !lit_point_on[index] || lit_point_color[index] != model_point_color[index])
			{
				sceGuLightColor(2 + index, GU_DIFFUSE, model_point_color[index]);
				sent = 1;
			}
			if (!lit_params || !lit_point_on[index])
			{
				sceGuEnable(GU_LIGHT2 + index);
				sent = 1;
			}
		}
		else if (lit_points_maybe_on && (!lit_params || lit_point_on[index]))
		{
			sceGuDisable(GU_LIGHT2 + index);
			sent = 1;
		}
	}
	lit_points_maybe_on = model_point_count > 0;
	if (on && model_point_count)
		model_point_parts++;
	if (sent)
		lit_sets_sent++;
	else
		lit_sets_memo++;
	if (!lit_memo || PSP_ME_WORK())
		return;
	lit_enabled = 1;
	lit_params = 1;
	lit_ambient = model_ambient;
	for (index = 0; index < 2; index++)
	{
		lit_on[index] = index < model_light_count;
		if (lit_on[index])
		{
			lit_toward[index] = model_light_toward[index];
			lit_color[index] = model_light_color[index];
		}
		lit_point_on[index] = index < model_point_count;
		if (lit_point_on[index])
		{
			lit_point_position[index] = model_point_position[index];
			lit_point_color[index] = model_point_color[index];
		}
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
	/* build 118: a sphere round the batch's vertices in the part's own
	space (before bending), for the near path's fast test (batch_sphere);
	worked out the first time the batch is drawn near the eye, radius < 0
	until then */
	float center[3], radius;
	/* build 119: its triangles in runs of NEAR_CLUSTER_TRIANGLES, each with
	a sphere like the batch's (centre, radius), for the corner loop's run
	test (near_cluster_verdict); made the first time the loop runs on the
	batch, -1 until then, 0 when there was no room */
	float (*clusters)[4];
	int cluster_count;
};

struct skin_part
{
	int weights, stride, batch_count;
	struct skin_batch *batches;
	int bytes; /* build 112: the one allocation's size, for the live count */
};

static struct skin_part *skinned[CONVERTED_BUFFERS];
/* each node's slot in the part being planned */
static signed char skin_slot[SKIN_NODES];
static unsigned long skinned_parts = 0, skin_batches = 0, skin_failures = 0, skin_bytes = 0;
/* build 112: the plans' bytes on the heap now (skin_bytes counts all ever
made), and the highest that reached */
static unsigned long skin_live_bytes = 0, skin_live_peak = 0;

static void skin_free(struct skin_part *part)
{
	if (!part)
		return;
	skin_live_bytes -= (unsigned long)part->bytes;
	{
		int b;

		/* build 119: the runs' spheres (near_clusters_make) */
		for (b = 0; b < part->batch_count; b++)
			if (part->batches[b].clusters)
			{
				near_cluster_bytes -= (unsigned long)part->batches[b].cluster_count * sizeof(float[4]);
				free(part->batches[b].clusters);
			}
	}
	free(part);
}

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
		part->bytes = bytes;
		skin_live_bytes += (unsigned long)bytes;
		if (skin_live_bytes > skin_live_peak)
			skin_live_peak = skin_live_bytes;
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
			part->batches[b].radius = -1.0f; /* build 118: its sphere not yet worked out */
			part->batches[b].clusters = NULL; /* build 119: nor its runs' */
			part->batches[b].cluster_count = -1;
			for (t = 0; t < triangle_count; t++)
				if (batch_of[t] == b)
				{
					out[0] = list[3 * t]; out[1] = list[3 * t + 1]; out[2] = list[3 * t + 2];
					out += 3;
					part->batches[b].index_count += 3;
				}
			ge_writeback(part->batches[b].indices, part->batches[b].index_count * 2);
		}
	}
	free(batch_of);
	free(list);
	skinned_parts++;
	skin_batches += batch_count;
	return part;
}

/* build 93: the table forgets what is gone. An entry whose geometry-cache
block has since been loaded with something else (its generation is no longer
any resident block's) or let go is dead: nothing there is converted any
more. When the table is three-quarters full the live entries are kept and
put back (open addressing can't simply delete), and the dead ones' skinning
plans freed. b68-b92 forgot only at a map change, so a long campaign session
churning the cache filled all 2,048 slots and model parts stopped drawing
(code review b89). Entries in the tags (generation 0) are always live */
static int converted_used = 0;
static unsigned long converted_sweeps = 0, converted_forgotten = 0;
/* build 112: sweep sooner. The dead entries' skinning plans stay on the heap
until a sweep: in b108 run 1 the plans made reached 1,160 KB by game time
20,000, and one sweep forgot 1,426 dead entries, so the heap carried up to
about a megabyte of plans nothing would use again while the level's copies
looked for room. The next sweep now comes 256 entries after what the last
one kept, from a quarter of the table up to the three quarters it always
used; a scene whose live parts pass the mark doesn't sweep on every part,
because the mark moves past what was kept. A sweep's cost grows with the
entries it checks, so smaller sweeps cost less each. "sweeplate": at three
quarters only, as before */
#define CONVERTED_SWEEP_STEP 256
static int converted_sweep_mark = CONVERTED_BUFFERS / 4;
static int converted_sweep_late = -1;
unsigned int psp_geo_generation(const void *ptr);

static void converted_sweep(void)
{
	static const void *keep_address[CONVERTED_BUFFERS];
	static unsigned int keep_generation[CONVERTED_BUFFERS];
	static struct skin_part *keep_skin[CONVERTED_BUFFERS];
	int index, kept = 0;

	for (index = 0; index < CONVERTED_BUFFERS; index++)
	{
		if (!converted[index])
			continue;
		if (psp_geo_generation(converted[index]) == converted_generation[index])
		{
			keep_address[kept] = converted[index];
			keep_generation[kept] = converted_generation[index];
			keep_skin[kept] = skinned[index];
			kept++;
		}
		else
		{
			skin_free(skinned[index]);
			converted_forgotten++;
		}
	}
	memset(converted, 0, sizeof(converted));
	memset(converted_generation, 0, sizeof(converted_generation));
	memset(skinned, 0, sizeof(skinned));
	for (index = 0; index < kept; index++)
	{
		unsigned int slot = ((unsigned int)keep_address[index] >> 4) % CONVERTED_BUFFERS;

		while (converted[slot])
			slot = (slot + 1) % CONVERTED_BUFFERS;
		converted[slot] = keep_address[index];
		converted_generation[slot] = keep_generation[index];
		skinned[slot] = keep_skin[index];
	}
	converted_used = kept;
	converted_sweeps++;
}

/* build 112: model buffers that work marked as the Media Engine's needed
converted (gemesim): converting plans a bending part's skinning on the heap
and sweeps the table, which frees plans, all behind the heap's lock. The
main CPU converts them at the frame's end (psp_ge_frame_end), so they're
ready next frame; the part isn't drawn meanwhile, as for geometry still on
its way */
#define CONVERTS_WANTED 64
static struct
{
	void *vertices;
	const unsigned short *indices;
	int count, uncompressed, index_count, strip, skin;
} converts_wanted[CONVERTS_WANTED];
static int converts_wanted_count = 0;
static unsigned long converts_wanted_total = 0, converts_wanted_dropped = 0;

static void convert_want(void *vertices, int count, int uncompressed, const unsigned short *indices, int index_count,
	int strip, int skin)
{
	int k;

	for (k = 0; k < converts_wanted_count && converts_wanted[k].vertices != vertices; k++)
		;
	if (k < converts_wanted_count)
		return;
	if (k >= CONVERTS_WANTED)
	{
		converts_wanted_dropped++;
		return;
	}
	converts_wanted[k].vertices = vertices;
	converts_wanted[k].indices = indices;
	converts_wanted[k].count = count;
	converts_wanted[k].uncompressed = uncompressed;
	converts_wanted[k].index_count = index_count;
	converts_wanted[k].strip = strip;
	converts_wanted[k].skin = skin;
	converts_wanted_count++;
	converts_wanted_total++;
}

/* ---------- build 116: "vrammodelsN", the model buffers' VRAM copies

big_levers.md section 0, the VRAM swap's second step. b115 [E, gereads]: the
models are 69-88% of what the GE reads from main RAM, and a few small buffers
carry most of it (an 11 KB buffer read 586 KB a frame); ~600 KB of VRAM takes
82-92% of those reads. b115 test 3 [M]: texels from main RAM cost the GE
~1.5 ms a frame, so the textures can give that VRAM up. With the word, the top
N KB of VRAM (psp_tex.c, psp_tex_vram_models) hold copies of the converted
model buffers the GE reads most per byte, and the draw gate (sceGuDrawArray,
above) hands the GE the copy instead. The same bytes: the same pixels.

- A buffer is known from its conversion (model_convert: its address, size and
  the geometry cache's generation of its block). A copy is good while the
  generation holds; the gate drops it when the cache has read something else
  into the block.
- Copies are made at the frame's end, the most-read per byte first, at most
  128 KB a frame; reads halve every 32 frames, so the set follows the view.
  Room a dropped copy used isn't reused for two frames (a list the GE is
  still drawing may read it).
- Never from the Media Engine's side (it can't see VRAM, and the tables are
  the main CPU's): the gate leaves its draws alone.
Off by default: Sheepdog01's A/B (a cinematic's GE time, gebusy by kind). */
#define VM_ENTRIES 1024
#define VM_HASH 2048
#define VM_NONE 0xFFFFFFFFu
#define VM_COPY_BUDGET (128 * 1024)
#define VM_QUARANTINE_FRAMES 2
struct vm_entry
{
	unsigned int source, generation, bytes, at, reads, frame, released;
};
static struct vm_entry *vm = NULL;
static unsigned short *vm_hash = NULL, *vm_order = NULL;
static int vm_count = 0;
static unsigned int vm_base = 0, vm_size = 0, vm_frame = 0;
static unsigned long vm_draws = 0, vm_redirects = 0, vm_copied = 0, vm_dropped = 0, vm_full = 0;

/* the entry for a buffer's address, or NULL (adding it when asked) */
static struct vm_entry *vm_find(unsigned int source, int add)
{
	unsigned int h = (source >> 4) & (VM_HASH - 1), probe;

	for (probe = 0; probe < VM_HASH; probe++)
	{
		unsigned short slot = vm_hash[(h + probe) & (VM_HASH - 1)];

		if (!slot)
		{
			if (!add || vm_count >= VM_ENTRIES)
				return NULL;
			vm[vm_count].source = source;
			vm[vm_count].at = VM_NONE;
			vm[vm_count].released = 0;
			vm[vm_count].reads = 0;
			vm_hash[(h + probe) & (VM_HASH - 1)] = (unsigned short)(++vm_count);
			return &vm[vm_count - 1];
		}
		if (vm[slot - 1].source == source)
			return &vm[slot - 1];
	}
	return NULL;
}

/* at a new map or BSP (and the first time): the room psp_tex.c gave, and
empty tables */
static void vm_reset(void)
{
	extern unsigned int psp_tex_vram_models(unsigned int *start);

	vm_size = psp_tex_vram_models(&vm_base);
	vm_on = 0;
	if (!vm_size)
		return;
	if (!vm)
		vm = malloc(VM_ENTRIES * sizeof(struct vm_entry));
	if (!vm_hash)
		vm_hash = malloc(VM_HASH * sizeof(unsigned short));
	if (!vm_order)
		vm_order = malloc(VM_ENTRIES * sizeof(unsigned short));
	if (!vm || !vm_hash || !vm_order)
	{
		platform_log("GE: vrammodels: no room for its tables; off");
		return;
	}
	memset(vm_hash, 0, VM_HASH * sizeof(unsigned short));
	vm_count = 0;
	vm_on = 1;
}

/* a copy let go: its room comes back after the quarantine */
static void vm_release(struct vm_entry *e)
{
	if (e->at != VM_NONE && !e->released)
	{
		e->released = vm_frame + VM_QUARANTINE_FRAMES + 1;
		vm_dropped++;
	}
}

/* model_convert's buffer, converted: its size and its block's generation */
static void vm_note(void *vertices, unsigned int bytes)
{
	struct vm_entry *e;

	if (vm_on <= 0 || PSP_ME_WORK())
		return;
	e = vm_find((unsigned int)vertices, 1);
	if (!e)
		return;
	if (e->at != VM_NONE && (e->bytes != bytes || e->generation != psp_geo_generation(vertices)))
		vm_release(e);
	e->bytes = bytes;
	e->generation = psp_geo_generation(vertices);
}

/* the gate: the copy's address for a buffer that has a good one */
static const void *vm_redirect(const void *vertices, int count)
{
	struct vm_entry *e;

	if (PSP_ME_WORK() || !vertices)
		return vertices;
	e = vm_find((unsigned int)vertices, 0);
	if (!e)
		return vertices;
	vm_draws++;
	if (psp_geo_generation(vertices) != e->generation)
	{
		/* the cache has read other bytes into the block: model_convert
		notes it afresh when it converts them */
		vm_release(e);
		e->reads = 0;
		return vertices;
	}
	e->reads += (unsigned int)count;
	e->frame = vm_frame;
	if (e->at == VM_NONE || e->released)
		return vertices;
	vm_redirects++;
	return (const void *)(vm_base + e->at);
}

/* the first gap of the given size in the copies' room, or VM_NONE; copies
whose quarantine is over give their room back first */
static unsigned int vm_place(unsigned int bytes)
{
	unsigned short *order = vm_order;
	int n = 0, k, j;
	unsigned int at = 0;

	for (k = 0; k < vm_count; k++)
	{
		struct vm_entry *e = &vm[k];

		if (e->at == VM_NONE)
			continue;
		if (e->released && vm_frame >= e->released)
		{
			e->at = VM_NONE;
			e->released = 0;
			continue;
		}
		/* insertion by place: a few hundred copies at most */
		for (j = n; j > 0 && vm[order[j - 1]].at > e->at; j--)
			order[j] = order[j - 1];
		order[j] = (unsigned short)k;
		n++;
	}
	for (k = 0; k < n; k++)
	{
		struct vm_entry *e = &vm[order[k]];

		if (e->at >= at + bytes)
			return at;
		at = (e->at + e->bytes + 15) & ~15u;
	}
	return at + bytes <= vm_size ? at : VM_NONE;
}

/* the frame's end: the best buffers without a copy get one, room allowing;
when there's none, the least-read copy not drawn lately makes way */
static void vm_frame_end(void)
{
	unsigned int budget = VM_COPY_BUDGET;
	int k, round;

	if (vm_on <= 0)
		return;
	vm_frame++;
	if ((vm_frame & 31) == 0)
		for (k = 0; k < vm_count; k++)
			vm[k].reads >>= 1;
	for (round = 0; round < 16 && budget; round++)
	{
		struct vm_entry *best = NULL, *worst = NULL;
		unsigned int at;

		/* the most reads a byte among the buffers drawn this frame or the
		last without a copy (cross-multiplied, no division) */
		for (k = 0; k < vm_count; k++)
		{
			struct vm_entry *e = &vm[k];

			if (e->at != VM_NONE || !e->reads || !e->bytes || e->bytes > vm_size || e->bytes > budget ||
				vm_frame - e->frame > 2)
				continue;
			if (!best || (unsigned long long)e->reads * best->bytes > (unsigned long long)best->reads * e->bytes)
				best = e;
		}
		if (!best)
			break;
		at = vm_place(best->bytes);
		if (at == VM_NONE)
		{
			/* the least read a byte of the copies not drawn these 30 frames,
			if it's worth less than the newcomer */
			for (k = 0; k < vm_count; k++)
			{
				struct vm_entry *e = &vm[k];

				if (e->at == VM_NONE || e->released || vm_frame - e->frame < 30)
					continue;
				if (!worst || (unsigned long long)e->reads * worst->bytes < (unsigned long long)worst->reads * e->bytes)
					worst = e;
			}
			if (!worst || (unsigned long long)worst->reads * best->bytes >= (unsigned long long)best->reads * worst->bytes)
			{
				vm_full++;
				break;
			}
			vm_release(worst);
			break; /* its room comes back after the quarantine */
		}
		memcpy((void *)((vm_base + at) | 0x40000000), (const void *)best->source, best->bytes);
		best->at = at;
		best->released = 0;
		budget -= best->bytes;
		vm_copied += best->bytes;
	}
}

static void vm_report(void)
{
	unsigned int resident = 0, bytes = 0;
	int k;

	if (vm_on <= 0)
		return;
	for (k = 0; k < vm_count; k++)
		if (vm[k].at != VM_NONE && !vm[k].released)
		{
			resident++;
			bytes += vm[k].bytes;
		}
	platform_log("GE: vrammodels: %u buffers in %u of %u KB of VRAM; draws from a copy %lu of %lu, %lu KB copied, %lu copies let go, %lu times full (last 300 frames; %d buffers known)",
		resident, bytes / 1024, vm_size / 1024, vm_redirects, vm_draws, vm_copied / 1024, vm_dropped, vm_full, vm_count);
	vm_draws = vm_redirects = vm_copied = vm_dropped = vm_full = 0;
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
	if (converted_sweep_late < 0)
		converted_sweep_late = psp_keep("sweeplate");
	if (converted_used >= (converted_sweep_late ? CONVERTED_BUFFERS * 3 / 4 : converted_sweep_mark) &&
		!PSP_ME_WORK()) /* build 112: the sweep frees plans: the main CPU's */
	{
		converted_sweep();
		/* build 112: the next mark, past what this sweep kept */
		converted_sweep_mark = converted_used + CONVERTED_SWEEP_STEP;
		if (converted_sweep_mark < CONVERTED_BUFFERS / 4)
			converted_sweep_mark = CONVERTED_BUFFERS / 4;
		if (converted_sweep_mark > CONVERTED_BUFFERS * 3 / 4)
			converted_sweep_mark = CONVERTED_BUFFERS * 3 / 4;
		slot = ((unsigned int)vertices >> 4) % CONVERTED_BUFFERS;
	}
	while (converted[slot] && converted[slot] != vertices)
	{
		slot = (slot + 1) % CONVERTED_BUFFERS;
		if (++tries >= CONVERTED_BUFFERS)
		{
			/* build 89: said once. Entries are only forgotten at a map
			change, and every address a model's block is read to adds one:
			a long session churning the geometry cache (a campaign map) fills
			it, and model parts then stop drawing (review; the real fix,
			keeping this beside the cache's own block, is for the campaign
			work) */
			static int said = 0;

			if (!said)
			{
				said = 1;
				platform_log("GE: the converted-buffer table is FULL (%d addresses since the map loaded): model parts that need converting won't draw until the next map",
					CONVERTED_BUFFERS);
			}
			return 0;
		}
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
		if (PSP_ME_WORK())
		{
			convert_want(vertices, count, uncompressed, indices, index_count, strip, skin != NULL);
			return 0;
		}
		/* the cache has read other vertices (or these again) into the block
		since: converted afresh, the old plan let go */
		skin_free(skinned[slot]);
		skinned[slot] = NULL;
		converted_generation[slot] = generation;
		reconverted_count++;
	}
	else
	{
		if (PSP_ME_WORK())
		{
			convert_want(vertices, count, uncompressed, indices, index_count, strip, skin != NULL);
			return 0;
		}
		converted_generation[slot] = psp_geo_generation(vertices);
	}
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
	if (!converted[slot])
		converted_used++;
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
	ge_writeback(vertices, count * (part ? part->stride : 24));
	vm_note(vertices, (unsigned int)count * (part ? part->stride : 24)); /* build 116: vrammodels */
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
/* build 108: in the scratchpad (psp_scratch.c; ge_initialize): written and
read back for every model part cut at the near plane, and the emulator's
cache model counted 0.8 M misses on it over b30's beach fight */
static float (*model_cut)[CORNER_FLOATS];

void *psp_scratch_take(unsigned int bytes, const char *what); /* psp_scratch.c */
static void model_cut_take(void)
{
	if (!model_cut)
		model_cut = psp_scratch_take(sizeof(float[MAXIMUM_MODEL_CUT_CORNERS][CORNER_FLOATS]), "the models' cut corners");
}
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
	GE_BENCH_SKIP();
	GE_DEFER(defer_model_multipurpose(multipurpose_key, change, glow));
	change_key = multipurpose_key;
	change_color = abgr_from_rgb(change);
	glow_color = abgr_from_rgb(glow) & 0xFFFFFF;
}

/* a pass over the part's triangles with the multipurpose map's mask: 0 the
change colour (white toward the colour by the blue, multiplied in), 1 the
self-illumination (the colour by the green, added) */
/* build 66: the worked-out palettes kept, by map and pass. A slot is reused
only when nothing this frame still needs it (the GE reads a palette when the
list runs), as the meters' (METER_CACHE). Build 106: the palette is the
mask alone (grey), and the GE applies the colour: the change pass with its
blend texture function (fragment white x (1 - mask) + colour x mask, the
colour as the environment colour), the glow pass modulating the colour by
the mask. Until b105 the colour was worked into each palette entry, so every
marine's own armour colour was a palette of its own, more than the cache's
six slots in a fight: 6% of b30's beach fight in the emulator's PC samples.
Build 107: 40 slots (40 KB). With six, b30's fight still had more masks in
use a frame than slots (about 60 palettes worked out a frame, 13 reused), and
each one past the slots was worked out again into the display list: about
1 KB each, up to half of the 100-160 KB of data the models copied into the
list a frame */
#define MULTIPURPOSE_CACHE 40
static unsigned int multipurpose_cache_palette[MULTIPURPOSE_CACHE][256] __attribute__((aligned(64)));
static struct
{
	unsigned int key;
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
	/* a 16-bit palette's 5- and 6-bit channels to 8 bits (v * 255 / 31, / 63) */
	static unsigned char expand_5[32], expand_6[64];

	if (!expand_5[31])
	{
		for (entry = 0; entry < 32; entry++)
			expand_5[entry] = (unsigned char)(entry * 255 / 31);
		for (entry = 0; entry < 64; entry++)
			expand_6[entry] = (unsigned char)(entry * 255 / 63);
	}

	/* the Xbox's channel order, read off the Xbox disc's own Blood Gulch
	(the cyborg's map: the armour plates solid in blue, alpha all white): red
	the reflection mask, green self-illumination, blue the change colour,
	alpha the detail mask. Gearbox's PC release reordered them */
	if (!psp_tex_bind(change_key, NULL) ||
		!(psp_tex_palette(change_key, &palette, &count) || psp_tex_palette16(change_key, &palette16, &count)))
		return 0;
	for (slot = 0; slot < MULTIPURPOSE_CACHE; slot++)
	{
		if (multipurpose_cache[slot].key == change_key &&
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
		unsigned int mask;

		if (which)
			mask = palette ? (palette[entry] >> 8) & 0xFF : expand_6[(palette16[entry] >> 5) & 0x3F];
		else
			mask = palette ? (palette[entry] >> 16) & 0xFF : expand_5[palette16[entry] >> 11];
		computed[entry] = 0xFF000000 | (mask << 16) | (mask << 8) | mask;
	}
	if (!hit)
		ge_writeback(computed, count * 4);
	sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
	sceGuClutLoad(count / 8 > 0 ? count / 8 : 1, computed);
	if (which)
	{
		/* the colour by the mask */
		sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
		sceGuColor(0xFF000000 | color);
	}
	else
	{
		/* white toward the colour by the mask */
		sceGuTexFunc(GU_TFX_BLEND, GU_TCC_RGB);
		sceGuTexEnvColor(color);
		sceGuColor(0xFFFFFFFF);
	}
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
	ge_blend(1);
	model_geometry(vertices, indices, index_count, strip, skin, model);
	ge_blend(0);
	sceGuFog(fog_near, fog_far, fog_color);
	sceGuDepthMask(GU_FALSE);
	sceGuDepthFunc(GU_GEQUAL);
	sceGuTexEnvColor(0xFFFFFFFF);
	sceGuColor(0xFFFFFFFF);
	return 1;
}

/* build 87: "skincut" (on by default; "noskincut": dropped, as builds 41-86).
A bending model's triangle reaching past the near plane used to be left
out whole: the tank's treads and the warthog's wheels opened holes when the
driving camera came close (Sheepdog01's b79 shots 5 and 6, and the distance where
it stops). Now its three corners are bent on the CPU, by the same weights
and node matrices the GE uses, and cut at the near plane and guard band as
the level's triangles are; the pieces are drawn already bent (the model
matrix is the identity for a bending part), their normals turned the same
way so the GE lights them alike */
static int skin_cut = -1;
static unsigned long skin_cut_triangles = 0, skin_cut_pieces = 0;

/* one skinned corner: bent position (camera-relative, the GE's space for a
bending part), bent normal (8-bit units), texture coordinates */
static void skin_corner(const unsigned char *record, int weights, int uv_at, const ScePspFMatrix4 *bone,
	float *position, float *normal, float *uv)
{
	const float *p = (const float *)(record + uv_at + 12), *t = (const float *)(record + uv_at);
	const signed char *n = (const signed char *)(record + uv_at + 8);
	int slot, axis;

	for (axis = 0; axis < 3; axis++)
		position[axis] = normal[axis] = 0.0f;
	for (slot = 0; slot < weights; slot++)
	{
		const ScePspFMatrix4 *m = &bone[slot];
		float w = record[slot] * (1.0f / 128.0f);

		if (!record[slot])
			continue;
		position[0] += w * (m->x.x * p[0] + m->y.x * p[1] + m->z.x * p[2] + m->w.x);
		position[1] += w * (m->x.y * p[0] + m->y.y * p[1] + m->z.y * p[2] + m->w.y);
		position[2] += w * (m->x.z * p[0] + m->y.z * p[1] + m->z.z * p[2] + m->w.z);
		normal[0] += w * (m->x.x * n[0] + m->y.x * n[1] + m->z.x * n[2]);
		normal[1] += w * (m->x.y * n[0] + m->y.y * n[1] + m->z.y * n[2]);
		normal[2] += w * (m->x.z * n[0] + m->y.z * n[1] + m->z.z * n[2]);
	}
	uv[0] = t[0];
	uv[1] = t[1];
}

/* build 106: a bending corner's camera-space position on the VFPU. Each of
a batch's slots (its node matrices' rows along the camera's right, up and
forward: depth_row) is loaded once a batch as a VFPU matrix, M100 to M700,
row a of the batch's numbers in column a, so vhtfm4 gives the three dot
products with (p, 1) at once (the probe, psp_vfpu.c: with M, element a is
column a dotted with (p, 1)). A corner is then one transform per weighted
slot, scaled by its weight and summed: on b30's beach these corners were
most of model_geometry, 9% of the emulator's time. Up to 7 slots (M000
holds the corner's working columns); 8-slot batches stay on the FPU.
The sums run in the VFPU's own order, so a corner can differ from the FPU's
in its last bit: a corner exactly on the near margin may then fall the
other way (b30's beach: 2,193,417 corners compared, none did).
Off unless keep.txt says "vfpuskin": in the emulator it came out 2-4%
slower over the frame (the emulator's VFPU costs are its own), so it waits
for the PSP's A/B. "skinvfpucheck": both, and the corners whose verdicts
differ counted in the log */
extern int psp_vfpu_on;
static int skin_vfpu = -1, skin_vfpu_check = 0;
static unsigned long skin_vfpu_corners = 0, skin_vfpu_differ = 0;

#define SKIN_VFPU_LOAD(m, rows) __asm__ volatile( \
	"lv.q C" #m "00, 0(%0)\n" "lv.q C" #m "10, 16(%0)\n" "lv.q C" #m "20, 32(%0)\n" "vzero.q C" #m "30\n" \
	: : "r"(rows) : "memory")
#define SKIN_VFPU_SLOT(m, weight) __asm__ volatile( \
	"mtv %0, S030\n" "vhtfm4.q C010, M" #m "00, C000\n" "vscl.t C010, C010, S030\n" "vadd.t C020, C020, C010\n" \
	: : "r"(weight))

/* the batch's slots into M100 onward (rows: 16-byte aligned, [slot][3][4]) */
static void skin_vfpu_load(const float (*rows)[3][4], int weights)
{
	switch (weights > 7 ? 7 : weights)
	{
	case 7: SKIN_VFPU_LOAD(7, rows[6]); /* fall through */
	case 6: SKIN_VFPU_LOAD(6, rows[5]); /* fall through */
	case 5: SKIN_VFPU_LOAD(5, rows[4]); /* fall through */
	case 4: SKIN_VFPU_LOAD(4, rows[3]); /* fall through */
	case 3: SKIN_VFPU_LOAD(3, rows[2]); /* fall through */
	case 2: SKIN_VFPU_LOAD(2, rows[1]); /* fall through */
	case 1: SKIN_VFPU_LOAD(1, rows[0]); /* fall through */
	default: break;
	}
}

/* one corner: p its position, record its weight bytes; at[3] out */
static void skin_vfpu_corner(const unsigned char *record, int weights, const float *p, float *at)
{
	int slot;

	__asm__ volatile("lv.s S000, 0(%0)\n" "lv.s S001, 4(%0)\n" "lv.s S002, 8(%0)\n" "vzero.t C020\n"
		: : "r"(p) : "memory");
	for (slot = 0; slot < weights; slot++)
		if (record[slot])
		{
			union { float f; unsigned int u; } w;

			w.f = record[slot] * (1.0f / 128.0f);
			switch (slot)
			{
			case 0: SKIN_VFPU_SLOT(1, w.u); break;
			case 1: SKIN_VFPU_SLOT(2, w.u); break;
			case 2: SKIN_VFPU_SLOT(3, w.u); break;
			case 3: SKIN_VFPU_SLOT(4, w.u); break;
			case 4: SKIN_VFPU_SLOT(5, w.u); break;
			case 5: SKIN_VFPU_SLOT(6, w.u); break;
			default: SKIN_VFPU_SLOT(7, w.u); break;
			}
		}
	__asm__ volatile("sv.s S020, 0(%0)\n" "sv.s S021, 4(%0)\n" "sv.s S022, 8(%0)\n" : : "r"(at) : "memory");
}

/* build 106: a bending part near the eye: what its first pass worked out
(each batch's triangles kept whole, and its pieces cut at the near plane and
guard band), drawn again by the part's other passes (the two-sided pass's
back faces, the change and glow colour passes) from the same lists, which
stay in the display list for the frame. Each pass used to test every corner
again: a swaying tree over the player on b30's beach (two-sided leaves)
cost 24% of the game thread in the emulator's PC samples. Valid for the part
being drawn only (psp_ge_draw_model_body forgets it first) */
static const struct skin_part *near_memo_skin = NULL;
static int near_memo_complete = 0;
static struct
{
	const unsigned short *draw;
	int draw_count;
	const unsigned char *pieces;
	int cut_count;
} near_memo[64];

/* build 107: a bending part's bone matrices, once a part. Each batch loads
its slots' matrices (13 words a slot) before its draw, and every pass of the
part (the two-sided pass, the change and glow colour passes) loaded them all
again: b30's beach fight put 66-74 KB of bone matrices in the list a frame
(1,300-1,450 loads), a third of the models' share when the list was full.
The first pass now writes each batch's loads into the list as a sub-list
ending in a return, and every pass calls it (2 words). The matrices are
worked out on the CPU once a part, too. Valid for the part being drawn only
(psp_ge_draw_model_body forgets it first, as near_memo). Keep word
"bonesdirect": loaded in the list by every pass, as before */
#define BONE_MEMO_BATCHES 64
static const struct skin_part *bone_memo_skin = NULL;
static const unsigned int *bone_memo[BONE_MEMO_BATCHES];
static int bones_direct = -1;

/* a slot's matrix as the GE's bone commands (pspgu's sceGuBoneMatrix: the
slot's place, then the 4 rows' first 3 columns as 24-bit floats) */
static unsigned int *bone_commands(unsigned int *at, int slot, const ScePspFMatrix4 *matrix)
{
	const float *m = (const float *)matrix;
	int row, column;

	*at++ = (0x2Au << 24) | (unsigned int)(slot * 12);
	for (row = 0; row < 4; row++)
		for (column = 0; column < 3; column++)
		{
			union { float f; unsigned int u; } v;

			v.f = m[row * 4 + column];
			*at++ = (0x2Bu << 24) | (v.u >> 8);
		}
	return at;
}

/* build 118: the near path's fast test for a bending batch. Every vertex
of the batch lies in its sphere (the part's own space); each slot's matrix
takes that sphere to a sphere in camera space, its radius stretched by the
matrix's largest stretch; and a bent vertex is a weighted mean of its
slots' images of it, so it lies in the hull of those spheres. All of them
wholly inside the corner tests' planes, every corner passes them, and no
triangle is dropped or cut: the batch is drawn whole, as the corner loop
would have found. "nearspherecheck" runs the loop anyway and counts the
batches the test passed that it didn't draw whole; "nearspherefull": the
loop for every batch, as before (their counters: by near_parts) */

/* the batch's sphere, worked out once from its vertices (the bending
part's records: the position after the texture coordinate and normal) */
static void batch_sphere(struct skin_batch *b, const struct skin_part *skin, const void *vertices)
{
	float low[3] = { 0.0f, 0.0f, 0.0f }, high[3] = { 0.0f, 0.0f, 0.0f }, reach = 0.0f;
	int uv_at = skin->stride - 24, i, axis;

	for (i = 0; i < b->index_count; i++)
	{
		const float *p = (const float *)((const unsigned char *)vertices + skin->stride * b->indices[i] + uv_at + 12);

		for (axis = 0; axis < 3; axis++)
		{
			if (i == 0 || p[axis] < low[axis]) low[axis] = p[axis];
			if (i == 0 || p[axis] > high[axis]) high[axis] = p[axis];
		}
	}
	for (axis = 0; axis < 3; axis++)
		b->center[axis] = (low[axis] + high[axis]) * 0.5f;
	for (i = 0; i < b->index_count; i++)
	{
		const float *p = (const float *)((const unsigned char *)vertices + skin->stride * b->indices[i] + uv_at + 12);
		float dx = p[0] - b->center[0], dy = p[1] - b->center[1], dz = p[2] - b->center[2];
		float d = dx * dx + dy * dy + dz * dz;

		if (d > reach)
			reach = d;
	}
	b->radius = sqrtf(reach);
}

/* whether every used slot's image of the batch's sphere lies wholly inside
the corner tests' planes, near being the larger of their two near limits.
depth_row: each slot's matrix as rows along the camera's right, up and
forward (model_geometry). The largest stretch of a slot's 3x3 part is
bounded by its Gram matrix's largest row sum (Gershgorin), which is exact
for a rotation with one scale, as the game's nodes are */
static int batch_sphere_inside(const struct skin_batch *b, int weights, const float (*depth_row)[3][4], float near)
{
	int slot;

	for (slot = 0; slot < weights; slot++)
	{
		const float (*row)[4] = depth_row[slot];
		float c[3], gram[3][3], stretch = 0.0f;
		int i, j;

		if (b->node[slot] == 0xFF)
			continue;
		for (i = 0; i < 3; i++)
		{
			c[i] = row[i][0] * b->center[0] + row[i][1] * b->center[1] + row[i][2] * b->center[2] + row[i][3];
			for (j = 0; j < 3; j++)
				gram[i][j] = row[i][0] * row[j][0] + row[i][1] * row[j][1] + row[i][2] * row[j][2];
		}
		for (i = 0; i < 3; i++)
		{
			float sum = fabsf(gram[i][0]) + fabsf(gram[i][1]) + fabsf(gram[i][2]);

			if (sum > stretch)
				stretch = sum;
		}
		if (!sphere_inside(c, b->radius * sqrtf(stretch), near))
			return 0;
	}
	return 1;
}

/* build 119: the near path's run test ("NC"). The corner loop works out a
corner's verdict only to find the triangles with a corner behind the near
margin or past a guard plane (camera_corner_out): those are dropped or cut,
and every other triangle is kept as it is. A bending batch round the eye
(a30's trees over the player: 22 ms a frame of corner loop on the PSP)
fails the batch's sphere test, yet most of its triangles lie wholly in
front, and many wholly behind. So the batch's triangles are taken in runs
of NEAR_CLUSTER_TRIANGLES, each with its own sphere (worked out once, as
batch_sphere does), and each slot's image of it is tested as
batch_sphere_inside does. Every image wholly in front of the margin and
inside the guard planes: no corner of the run is out, and its triangles are
kept with none worked out. Every image wholly beyond one and the same plane
(behind the nearer of the two near limits, or past a guard plane): every
corner is out on that plane, so each triangle is dropped, as the loop would
drop it. "nearclusterfull": the loop for every triangle, as before;
"nearclustercheck": the loop runs anyway and counts the triangles whose own
verdict disagrees with their run's */

/* the runs' spheres, in the part's own space (the batch's records) */
static void near_clusters_make(struct skin_batch *b, const struct skin_part *skin, const void *vertices)
{
	int triangles = b->index_count / 3, count = (triangles + NEAR_CLUSTER_TRIANGLES - 1) / NEAR_CLUSTER_TRIANGLES;
	int uv_at = skin->stride - 24, c;

	b->cluster_count = 0;
	b->clusters = count > 0 ? malloc(count * sizeof(float[4])) : NULL;
	if (!b->clusters)
		return;
	b->cluster_count = count;
	near_cluster_bytes += (unsigned long)count * sizeof(float[4]);
	for (c = 0; c < count; c++)
	{
		int first = c * NEAR_CLUSTER_TRIANGLES * 3, last = first + NEAR_CLUSTER_TRIANGLES * 3, i, axis;
		float low[3] = { 0.0f, 0.0f, 0.0f }, high[3] = { 0.0f, 0.0f, 0.0f }, reach = 0.0f;

		if (last > triangles * 3)
			last = triangles * 3;
		for (i = first; i < last; i++)
		{
			const float *p = (const float *)((const unsigned char *)vertices + skin->stride * b->indices[i] + uv_at + 12);

			for (axis = 0; axis < 3; axis++)
			{
				if (i == first || p[axis] < low[axis]) low[axis] = p[axis];
				if (i == first || p[axis] > high[axis]) high[axis] = p[axis];
			}
		}
		for (axis = 0; axis < 3; axis++)
			b->clusters[c][axis] = (low[axis] + high[axis]) * 0.5f;
		for (i = first; i < last; i++)
		{
			const float *p = (const float *)((const unsigned char *)vertices + skin->stride * b->indices[i] + uv_at + 12);
			float dx = p[0] - b->clusters[c][0], dy = p[1] - b->clusters[c][1], dz = p[2] - b->clusters[c][2];
			float d = dx * dx + dy * dy + dz * dz;

			if (d > reach)
				reach = d;
		}
		b->clusters[c][3] = sqrtf(reach);
	}
}

/* each used slot's largest stretch (batch_sphere_inside's bound); -1 for a
slot the batch doesn't use */
static void near_slot_stretch(const struct skin_batch *b, int weights, const float (*depth_row)[3][4], float *scale)
{
	int slot;

	for (slot = 0; slot < weights; slot++)
	{
		const float (*row)[4] = depth_row[slot];
		float gram[3][3], stretch = 0.0f;
		int i, j;

		scale[slot] = -1.0f;
		if (b->node[slot] == 0xFF)
			continue;
		for (i = 0; i < 3; i++)
			for (j = 0; j < 3; j++)
				gram[i][j] = row[i][0] * row[j][0] + row[i][1] * row[j][1] + row[i][2] * row[j][2];
		for (i = 0; i < 3; i++)
		{
			float sum = fabsf(gram[i][0]) + fabsf(gram[i][1]) + fabsf(gram[i][2]);

			if (sum > stretch)
				stretch = sum;
		}
		scale[slot] = sqrtf(stretch);
	}
}

/* the planes a camera-space sphere lies wholly beyond, as outside()'s bits:
1 the near limit given, 2 and 4 the guard planes left and right, 8 and 16
up and down. The radius gets sphere_inside's margin; a NaN gives none */
static int sphere_beyond(const float *c, float radius, float near)
{
	float r = radius * 1.01f + 0.01f;
	int bits = 0;

	if (!(radius >= 0.0f))
		return 0;
	if (c[2] + r < near) bits |= 1;
	if (c[0] - guard_x * c[2] > r * guard_x_length) bits |= 2;
	if (-c[0] - guard_x * c[2] > r * guard_x_length) bits |= 4;
	if (c[1] - guard_y * c[2] > r * guard_y_length) bits |= 8;
	if (-c[1] - guard_y * c[2] > r * guard_y_length) bits |= 16;
	return bits;
}

/* a run's verdict: 1 every corner in (kept whole), 2 every corner out on
one plane (dropped), 0 either */
static int near_cluster_verdict(const struct skin_batch *b, int c, int weights, const float (*depth_row)[3][4],
	const float *scale, float near_in, float near_out)
{
	const float *sphere = b->clusters[c];
	int slot, inside = 1, beyond = 0x1F, used = 0;

	for (slot = 0; slot < weights; slot++)
	{
		const float (*row)[4] = depth_row[slot];
		float centre[3], radius;
		int i;

		if (scale[slot] < 0.0f)
			continue;
		used++;
		for (i = 0; i < 3; i++)
			centre[i] = row[i][0] * sphere[0] + row[i][1] * sphere[1] + row[i][2] * sphere[2] + row[i][3];
		radius = sphere[3] * scale[slot];
		if (inside && !sphere_inside(centre, radius, near_in))
			inside = 0;
		if (beyond)
			beyond &= sphere_beyond(centre, radius, near_out);
		if (!inside && !beyond)
			return 0;
	}
	return !used ? 0 : inside ? 1 : beyond ? 2 : 0;
}

static void model_geometry(void *vertices, const unsigned short *indices, long index_count, int strip,
	struct skin_part *skin, ScePspFMatrix4 *model)
{
	if (skin)
	{
		int batch, slot;
		ScePspFMatrix4 bone[SKIN_SLOTS];
		int near = model_near && !model_first_person;
		int from_memo = near && near_memo_complete && near_memo_skin == skin, memo_failed = 0;

		if (skin_cut < 0)
			skin_cut = !psp_keep("noskincut");

		for (batch = 0; batch < skin->batch_count && !list_full(); batch++)
		{
			struct skin_batch *b = &skin->batches[batch];
			const unsigned short *draw = b->indices;
			int draw_count = b->index_count;
			/* each slot's matrix as the depth it gives a point: its rows
			dotted with the camera's forward direction (build 41) */
			/* static: 16-byte aligned for the VFPU's quad loads (build 106);
			this renderer runs on one thread */
			static float depth_row[SKIN_SLOTS][3][4] __attribute__((aligned(16)));
			int on_vfpu;
			/* the CPU needs the matrices only to test the corners near the eye */
			int cpu_matrices = near && !from_memo;
			const unsigned int *sub = bone_memo_skin == skin && batch < BONE_MEMO_BATCHES ? bone_memo[batch] : NULL;
			unsigned int *writing = NULL, *cursor = NULL;

			if (bones_direct < 0)
				bones_direct = psp_keep("bonesdirect");
			if (!sub && !bones_direct && batch < BONE_MEMO_BATCHES)
			{
				int bytes = (skin->weights * 13 + 1) * 4;

				writing = list_take(bytes);
				if (writing)
				{
					model_bone_sub_bytes += bytes;
					if (bone_memo_skin != skin)
					{
						bone_memo_skin = skin;
						memset(bone_memo, 0, sizeof(bone_memo));
					}
					cursor = writing;
				}
			}
			for (slot = 0; slot < skin->weights; slot++)
			{
				int node = b->node[slot];

				memset(depth_row[slot], 0, sizeof(depth_row[slot]));
				if (node != 0xFF)
				{
					int axis;

					if (sub && !cpu_matrices)
						continue;
					halo_matrix(model_nodes + 13 * (node < model_node_count ? node : 0), model);
					if (cursor)
						cursor = bone_commands(cursor, slot, model);
					else if (!sub)
					{
						sceGuBoneMatrix(slot, model);
						model_bone_loads++;
					}
					bone[slot] = *model;
					if (!cpu_matrices)
						continue;
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
			if (cursor)
			{
				/* the sub-list ends with a return; this pass calls it too */
				*cursor++ = 0x0Bu << 24;
				bone_memo[batch] = writing;
				sub = writing;
			}
			if (sub)
			{
				/* BASE and CALL, as pspgu's sceGuCallList (without its stall update) */
				sceGuSendCommandi(0x10, ((unsigned int)sub >> 8) & 0xF0000);
				sceGuSendCommandi(0x0A, (unsigned int)sub & 0xFFFFFF);
				model_bone_calls++;
			}
			if (from_memo && batch < 64)
			{
				/* the first pass's lists */
				draw = near_memo[batch].draw;
				draw_count = near_memo[batch].draw_count;
				if (near_memo[batch].cut_count)
				{
					if (draw_count)
						sceGumDrawArray(GU_TRIANGLES, GU_WEIGHTS(skin->weights) | GU_WEIGHT_8BIT | GU_TEXTURE_32BITF |
							GU_NORMAL_8BIT | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D, draw_count, draw,
							vertices);
					sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_NORMAL_8BIT | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
						near_memo[batch].cut_count, NULL, near_memo[batch].pieces);
					continue;
				}
				if (!draw_count)
					continue;
			}
			else if (near)
			{
				/* build 41: a bending model at the eye (the warthog round the
				camera behind it: its roll bars, turret, axles) drops the
				triangles not wholly in front of the near plane, as rigid ones
				have since build 38. A corner behind the eye, given to the GE,
				lands on the far side of the screen: the long spikes Sheepdog01 saw.
				The CPU works out each corner's depth from its weights */
				/* build 107: the copy of the triangles drawn as they are is taken
				at the first one that isn't (dropped or cut), with those before it;
				a batch drawn whole uses its own indices. Taken first, as before,
				every near batch was copied: b30 beside the Pelicans and the
				marines, 70-160 KB of copied data in the list a frame */
				unsigned short *kept = NULL;
				float limit = projection_near * 1.25f;
				int t, k, out = 0, uv_at = skin->stride - 24, cut_count = 0, kept_failed = 0;
				int whole_by_sphere = 0;
				/* build 119: the runs (near_cluster_verdict) */
				float slot_scale[SKIN_SLOTS];
				int use_clusters = 0, cluster_end = 0, cluster_state = 0;

				/* build 118: the batch's sphere first (batch_sphere_inside): wholly
				inside, the corner loop is skipped and the batch drawn whole */
				if (near_sphere_full < 0)
				{
					near_sphere_full = psp_keep("nearspherefull");
					near_sphere_check = psp_keep("nearspherecheck");
				}
				if (!near_sphere_full)
				{
					if (b->radius < 0.0f)
						batch_sphere(b, skin, vertices);
					whole_by_sphere = batch_sphere_inside(b, skin->weights, (const float (*)[3][4])depth_row,
						limit > NEAR_CLIP ? limit : NEAR_CLIP);
				}

				/* each vertex's verdict once a batch (build 43: worked out per
				corner, it cost up to 6 ms a frame on the PSP in build 42) */
				corner_verdict_reset();
				/* build 106: the corners on the VFPU (skin_vfpu_corner) */
				if (skin_vfpu < 0)
				{
					skin_vfpu = psp_vfpu_on && psp_keep("vfpuskin");
					skin_vfpu_check = psp_keep("skinvfpucheck");
				}
				/* (build 112: never in the Media Engine's work: the ME has no VFPU) */
				on_vfpu = skin_vfpu && skin->weights <= 7 && !PSP_ME_WORK();
				if (on_vfpu)
					skin_vfpu_load((const float (*)[3][4])depth_row, skin->weights);
				if (skin_axis_loop < 0)
				{
					skin_axis_loop = psp_keep("skinaxisloop");
					skin_axis_check = psp_keep("skinaxischeck");
				}
				/* build 119: the runs' spheres (near_cluster_verdict) */
				if (near_cluster_full < 0)
				{
					near_cluster_full = psp_keep("nearclusterfull");
					near_cluster_check = psp_keep("nearclustercheck");
				}
				if (!near_cluster_full && !whole_by_sphere)
				{
					if (b->cluster_count < 0)
						near_clusters_make(b, skin, vertices);
					if (b->cluster_count > 0)
					{
						near_slot_stretch(b, skin->weights, (const float (*)[3][4])depth_row, slot_scale);
						use_clusters = 1;
					}
				}
				for (t = 0; (!whole_by_sphere || near_sphere_check) && t + 2 < draw_count; t += 3)
				{
					/* build 106: each corner's verdict is its planes as bits
					(outside(): near, right, left, top, bottom) and 32 for
					"behind" (camera_corner_out, the near test's margin); a
					triangle wholly outside one plane is dropped here, without
					bending its corners on the CPU only to find the same (the
					tree over the player on b30's beach: most of its behind
					triangles were wholly behind) */
					int behind = 0, bits[3];

					/* build 119: a new run, its verdict */
					if (use_clusters && t >= cluster_end)
					{
						int c = t / (3 * NEAR_CLUSTER_TRIANGLES);

						cluster_end = (c + 1) * 3 * NEAR_CLUSTER_TRIANGLES;
						cluster_state = c < b->cluster_count ? near_cluster_verdict(b, c, skin->weights,
							(const float (*)[3][4])depth_row, slot_scale, limit, limit < NEAR_CLIP ? limit : NEAR_CLIP) : 0;
						if (cluster_state == 1)
							near_cluster_kept++;
						else if (cluster_state == 2)
							near_cluster_dropped++;
						else
							near_cluster_mixed++;
					}
					if (cluster_state == 1 && !near_cluster_check)
					{
						/* every corner in: kept as it is */
						if (kept)
						{
							kept[out++] = draw[t];
							kept[out++] = draw[t + 1];
							kept[out++] = draw[t + 2];
						}
						continue;
					}
					if (cluster_state == 2 && !near_cluster_check)
					{
						/* every corner out on one plane: dropped, as below */
						if (!kept)
						{
							kept = list_take((draw_count * 2 + 3) & ~3);
							if (!kept)
							{
								kept_failed = 1;
								break;
							}
							memcpy(kept, draw, t * sizeof(*kept));
							out = t;
						}
						near_triangles_dropped++;
						continue;
					}

					for (k = 0; k < 3; k++)
					{
						const unsigned char *record = (const unsigned char *)vertices + skin->stride * draw[t + k];
						int known = draw[t + k] < CORNER_VERDICTS ? corner_verdict[draw[t + k]] : -1, code;

						if (known >= corner_stamp && known < corner_stamp + CORNER_STAMP_STEP)
							code = known - corner_stamp;
						else
						{
							const float *p = (const float *)(record + uv_at + 12);
							float at[3] = { 0.0f, 0.0f, 0.0f };
							int axis;

							if (on_vfpu)
								skin_vfpu_corner(record, skin->weights, p, at);
							if (!on_vfpu || skin_vfpu_check)
							{
								float fpu[3] = { 0.0f, 0.0f, 0.0f };

								/* build 119: the three axes written out (they were a loop the
								release build doesn't unroll), so the FPU works on the three
								sums at once rather than one after another: each is the same
								expression as before, in the same order, so the same floats.
								"skinaxisloop": the loop, as before */
								if (skin_axis_loop)
								{
									for (slot = 0; slot < skin->weights; slot++)
										if (record[slot])
											for (axis = 0; axis < 3; axis++)
												fpu[axis] += record[slot] * (1.0f / 128.0f) * (depth_row[slot][axis][0] * p[0] +
													depth_row[slot][axis][1] * p[1] + depth_row[slot][axis][2] * p[2] +
													depth_row[slot][axis][3]);
								}
								else
									for (slot = 0; slot < skin->weights; slot++)
										if (record[slot])
										{
											const float (*row)[4] = depth_row[slot];

											fpu[0] += record[slot] * (1.0f / 128.0f) * (row[0][0] * p[0] + row[0][1] * p[1] +
												row[0][2] * p[2] + row[0][3]);
											fpu[1] += record[slot] * (1.0f / 128.0f) * (row[1][0] * p[0] + row[1][1] * p[1] +
												row[1][2] * p[2] + row[1][3]);
											fpu[2] += record[slot] * (1.0f / 128.0f) * (row[2][0] * p[0] + row[2][1] * p[1] +
												row[2][2] * p[2] + row[2][3]);
										}
								if (skin_axis_check)
								{
									/* "skinaxischeck": the loop as before, the sums compared bit for bit */
									float again[3] = { 0.0f, 0.0f, 0.0f };

									for (slot = 0; slot < skin->weights; slot++)
										if (record[slot])
											for (axis = 0; axis < 3; axis++)
												again[axis] += record[slot] * (1.0f / 128.0f) * (depth_row[slot][axis][0] * p[0] +
													depth_row[slot][axis][1] * p[1] + depth_row[slot][axis][2] * p[2] +
													depth_row[slot][axis][3]);
									skin_axis_checked++;
									if (memcmp(again, fpu, sizeof(again)))
										skin_axis_differ++;
								}
								if (on_vfpu)
								{
									/* "skinvfpucheck": the two verdicts compared */
									skin_vfpu_corners++;
									if (((camera_corner_out(at[0], at[1], at[2], limit) ? 32 : 0) | outside(at)) !=
										((camera_corner_out(fpu[0], fpu[1], fpu[2], limit) ? 32 : 0) | outside(fpu)))
										skin_vfpu_differ++;
								}
								else
									memcpy(at, fpu, sizeof(at));
							}
							code = (camera_corner_out(at[0], at[1], at[2], limit) ? 32 : 0) | outside(at);
							if (draw[t + k] < CORNER_VERDICTS)
								corner_verdict[draw[t + k]] = (unsigned short)(corner_stamp + code);
						}
						bits[k] = code & 31;
						behind |= code & 32;
					}
					/* build 119: "nearclustercheck": the run's verdict against the triangle's own */
					if ((cluster_state == 1 && behind) || (cluster_state == 2 && !(behind && (bits[0] & bits[1] & bits[2]))))
						near_cluster_mismatches++;
					if (behind)
					{
						if (!kept)
						{
							kept = list_take((draw_count * 2 + 3) & ~3);
							if (!kept)
							{
								kept_failed = 1;
								break;
							}
							memcpy(kept, draw, t * sizeof(*kept));
							out = t;
						}
						near_triangles_dropped++;
						if (bits[0] & bits[1] & bits[2])
							continue;
						if (skin_cut && cut_count < MAXIMUM_MODEL_CUT_CORNERS - 9)
						{
							/* build 87: bent here and cut, as the level's are */
							float polygon[3][CORNER_FLOATS];
							int bits[3];

							memset(polygon, 0, sizeof(polygon));
							for (k = 0; k < 3; k++)
							{
								const unsigned char *record = (const unsigned char *)vertices + skin->stride * draw[t + k];
								float at[3], normal[3], uv[2], world[3];

								skin_corner(record, skin->weights, uv_at, bone, at, normal, uv);
								/* camera-relative to world, for the camera-space corner */
								world[0] = at[0] + camera_eye[0];
								world[1] = at[1] + camera_eye[1];
								world[2] = at[2] + camera_eye[2];
								to_camera(world, polygon[k]);
								polygon[k][3] = uv[0];
								polygon[k][4] = uv[1];
								polygon[k][5] = normal[0];
								polygon[k][6] = normal[1];
								polygon[k][10] = normal[2];
								polygon[k][7] = at[0];
								polygon[k][8] = at[1];
								polygon[k][9] = at[2];
								bits[k] = outside(polygon[k]);
							}
							if (!(bits[0] & bits[1] & bits[2]))
							{
								if (!(bits[0] | bits[1] | bits[2]))
								{
									/* inside every plane, only inside the margin: drawn whole */
									memcpy(model_cut[cut_count], polygon[0], sizeof(polygon[0]));
									memcpy(model_cut[cut_count + 1], polygon[1], sizeof(polygon[0]));
									memcpy(model_cut[cut_count + 2], polygon[2], sizeof(polygon[0]));
									cut_count += 3;
								}
								else
									cut_count += clip_triangle(polygon, bits[0] | bits[1] | bits[2], model_cut + cut_count,
										MAXIMUM_MODEL_CUT_CORNERS - cut_count);
								skin_cut_triangles++;
							}
						}
						continue;
					}
					if (kept)
					{
						kept[out++] = draw[t];
						kept[out++] = draw[t + 1];
						kept[out++] = draw[t + 2];
					}
				}
				if (whole_by_sphere)
				{
					/* build 118: "nearspherecheck": the loop ran anyway, and must
					have found the batch whole */
					near_batches_by_sphere++;
					if (near_sphere_check && (kept || cut_count || kept_failed))
						near_sphere_mismatches++;
				}
				if (kept_failed)
				{
					memo_failed = 1;
					continue;
				}
				if (kept)
				{
					draw = kept;
					draw_count = out;
				}
				else
					near_batches_whole++;
				if (cut_count)
				{
					/* the bent pieces, after the batch's own triangles: GE vertices
					(texture coordinates, 8-bit normal, position) already in the
					bending part's space */
					unsigned char *pieces = list_take(cut_count * 24);
					int c;

					if (pieces)
					{
						for (c = 0; c < cut_count; c++)
						{
							unsigned char *record = pieces + 24 * c;
							float uv[2] = { model_cut[c][3], model_cut[c][4] };
							int axis;

							memcpy(record, uv, 8);
							for (axis = 0; axis < 3; axis++)
							{
								float v = model_cut[c][axis == 2 ? 10 : 5 + axis];

								record[8 + axis] = (unsigned char)(signed char)(v > 127.0f ? 127 : v < -127.0f ? -127 : (int)v);
							}
							record[11] = 0;
							memcpy(record + 12, &model_cut[c][7], 12);
						}
						if (draw_count)
							sceGumDrawArray(GU_TRIANGLES, GU_WEIGHTS(skin->weights) | GU_WEIGHT_8BIT | GU_TEXTURE_32BITF |
								GU_NORMAL_8BIT | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D, draw_count, draw,
								vertices);
						sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_NORMAL_8BIT | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
							cut_count, NULL, pieces);
						skin_cut_pieces += cut_count / 3;
						if (batch < 64)
						{
							near_memo[batch].draw = draw;
							near_memo[batch].draw_count = draw_count;
							near_memo[batch].pieces = pieces;
							near_memo[batch].cut_count = cut_count;
						}
						continue;
					}
				}
				if (batch < 64)
				{
					near_memo[batch].draw = draw;
					near_memo[batch].draw_count = draw_count;
					near_memo[batch].pieces = NULL;
					near_memo[batch].cut_count = 0;
				}
				if (!draw_count)
					continue;
			}
			sceGumDrawArray(GU_TRIANGLES, GU_WEIGHTS(skin->weights) | GU_WEIGHT_8BIT | GU_TEXTURE_32BITF |
				GU_NORMAL_8BIT | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D, draw_count, draw,
				vertices);
		}
		if (near && !from_memo)
		{
			/* every batch worked out (none cut short by a full list): the
			part's other passes draw from this */
			near_memo_skin = skin;
			near_memo_complete = !memo_failed && batch == skin->batch_count && skin->batch_count <= 64;
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

/* a rigid part's corner, as plane bits (outside(): near, sides; with
"modeldrop", 1 when behind the near test's margin), worked out once a part
and kept in corner_verdict. Build 107: the part's matrix and the camera's
axes made one transform for the part (rigid_rows, rigid_rows_for), so a
corner costs one three-row product instead of two (model_to_camera).
Rounded differently, so a corner exactly on a plane may fall the other way.
"nearcheck" in keep.txt: both, and the corners whose verdicts differ
counted in the log */
static void rigid_rows_for(const ScePspFMatrix4 *model)
{
	int axis;

	if (near_check < 0)
		near_check = psp_keep("nearcheck");
	for (axis = 0; axis < 3; axis++)
	{
		const float *d = axis == 0 ? camera_right : axis == 1 ? camera_up : camera_forward;

		rigid_rows[axis][0] = model->x.x * d[0] + model->x.y * d[1] + model->x.z * d[2];
		rigid_rows[axis][1] = model->y.x * d[0] + model->y.y * d[1] + model->y.z * d[2];
		rigid_rows[axis][2] = model->z.x * d[0] + model->z.y * d[1] + model->z.z * d[2];
		rigid_rows[axis][3] = model->w.x * d[0] + model->w.y * d[1] + model->w.z * d[2];
	}
}

static int rigid_corner_bits(const void *vertices, unsigned short corner, const ScePspFMatrix4 *model, float limit)
{
	int known = corner < CORNER_VERDICTS ? corner_verdict[corner] : -1, bits;
	const float *p;
	float camera_point[3];

	if (known >= corner_stamp && known < corner_stamp + CORNER_STAMP_STEP)
		return known - corner_stamp;
	p = (const float *)((const char *)vertices + 24 * corner + 12);
	camera_point[0] = rigid_rows[0][0] * p[0] + rigid_rows[0][1] * p[1] + rigid_rows[0][2] * p[2] + rigid_rows[0][3];
	camera_point[1] = rigid_rows[1][0] * p[0] + rigid_rows[1][1] * p[1] + rigid_rows[1][2] * p[2] + rigid_rows[1][3];
	camera_point[2] = rigid_rows[2][0] * p[0] + rigid_rows[2][1] * p[1] + rigid_rows[2][2] * p[2] + rigid_rows[2][3];
	bits = model_drop ? (camera_corner_out(camera_point[0], camera_point[1], camera_point[2], limit) ? 1 : 0) :
		outside(camera_point);
	if (near_check)
	{
		float two_step[3];

		model_to_camera(model, p, two_step);
		near_check_corners++;
		if (bits != (model_drop ? (camera_corner_out(two_step[0], two_step[1], two_step[2], limit) ? 1 : 0) :
			outside(two_step)))
			near_check_differ++;
	}
	if (corner < CORNER_VERDICTS)
		corner_verdict[corner] = (unsigned short)(corner_stamp + bits);
	return bits;
}

/* a triangle's corners, from a list or a strip (0: a strip's empty triangle) */
static int part_triangle(const unsigned short *indices, int strip, long triangle, unsigned short *corner)
{
	if (strip)
	{
		corner[0] = indices[triangle];
		corner[1] = indices[triangle + ((triangle & 1) ? 2 : 1)];
		corner[2] = indices[triangle + ((triangle & 1) ? 1 : 2)];
		return !(corner[0] == corner[1] || corner[1] == corner[2] || corner[0] == corner[2]);
	}
	corner[0] = indices[3 * triangle];
	corner[1] = indices[3 * triangle + 1];
	corner[2] = indices[3 * triangle + 2];
	return 1;
}

/* build 107: whether every triangle of a rigid part near the eye is wholly
inside, so the part draws its own indices with no copy in the list. A model
is "near" when its bounding sphere reaches the near plane or past the GE's
guard band, which a large model's (a Pelican's) does while most of its parts'
triangles are well inside: b30 beside the Pelicans copied up to 160 KB of
kept indices and cut corners into the list a frame. The verdicts stay in
corner_verdict for the copy, when one is needed */
static int rigid_part_whole(const void *vertices, const unsigned short *indices, long index_count, int strip,
	const ScePspFMatrix4 *model, float limit)
{
	long triangle, triangles = strip ? index_count - 2 : index_count / 3;
	unsigned short corner[3];

	if (model_drop < 0)
		model_drop = psp_keep("modeldrop");
	corner_verdict_reset();
	rigid_rows_for(model);
	near_parts++;
	for (triangle = 0; triangle < triangles; triangle++)
		if (part_triangle(indices, strip, triangle, corner) &&
			(rigid_corner_bits(vertices, corner[0], model, limit) | rigid_corner_bits(vertices, corner[1], model, limit) |
			rigid_corner_bits(vertices, corner[2], model, limit)))
			return 0;
	near_parts_whole++;
	return 1;
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
	near_memo_skin = NULL; /* build 106: model_geometry's lists are this part's */
	near_memo_complete = 0;
	bone_memo_skin = NULL; /* build 107: and its bone sub-lists */
	if (!frame_open || index_count < 3 || !vertices || !indices || !model_nodes || model_node_count <= 0 ||
		vertex_count <= 0 || list_full() || (in_sky && magenta == 1))
		return;
	if (model_size_floor > 0.0f && model_pixels < model_size_floor && !model_first_person && !in_sky)
	{
		model_floor_left_out++;
		return;
	}
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
		if (model_near && !model_first_person &&
			!rigid_part_whole(vertices, indices, index_count, strip, &model, projection_near * 1.25f))
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
			/* (rigid_part_whole has reset corner_verdict, set rigid_rows and
			worked out the corners up to the first triangle not wholly inside) */
			for (triangle = 0; triangle < triangles; triangle++)
			{
				unsigned short corner[3];
				int k, bits[3];

				if (!part_triangle(indices, strip, triangle, corner))
					continue;
				for (k = 0; k < 3; k++)
					bits[k] = rigid_corner_bits(vertices, corner[k], &model, limit);
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
		ge_blend(1);
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
		/* build 117: a point light can't be flipped the same way (it lights
		by place): the back faces go without */
		for (index = 0; index < model_point_count; index++)
			sceGuDisable(GU_LIGHT2 + index);
		lit_points_maybe_on = 0; /* light 3 was already off when only light 2 was used */
		lit_forget(1); /* build 114: statememo */
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
		ge_blend(0);
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
	GE_BENCH_SKIP();
	GE_DEFER(defer_draw_model(vertices, vertex_count, uncompressed, indices, index_count, strip, color, base_key,
		u_scale, v_scale, alpha_tested));
	ge_kind(in_sky ? KIND_SKY : KIND_MODEL); /* build 103 */
	unsigned int start = ge_clock();

	ge_category = GE_SKIP_MODELS;
	model_part_calls++;
	psp_ge_draw_model_body(vertices, vertex_count, uncompressed, indices, index_count, strip, color, base_key, u_scale, v_scale, alpha_tested);
	ge_category = 0;
	ge_entry_us[1] += ge_clock() - start;
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
	GE_DEFER(defer_words(DEFER_SKY, 1, (unsigned int)on, 0, 0, 0, 0, 0, 0, 0)); /* build 112: gedefer */
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
	GE_DEFER_DRAIN(); /* build 112: gedefer */
	int index;

	if (vm_on > 0)
		vm_on = -1; /* build 116: vrammodels' tables again at the next frame */
	for (index = 0; index < LEVEL_STREAMS; index++)
	{
		stream_free(level_streams[index].base);
		stream_free(level_streams[index].lightmap);
		copy_free(level_streams[index].normals);
		copy_free(level_streams[index].radiosity);
		copy_free(level_streams[index].compact_base);
		copy_free(level_streams[index].compact_lightmap);
	}
	memset(level_streams, 0, sizeof(level_streams));
	vram_vertex_next = 0; /* build 103 */
	/* build 64: the overlays' on-a-wall answers belong to this BSP */
	overlay_backing_forget();
	stream_bytes = 0;
	platform_log("GE: the level's vertex copies let go (a new BSP or map)");
}

void psp_ge_forget_map(void)
{
	GE_DEFER_DRAIN(); /* build 112: gedefer */
	int index;

	for (index = 0; index < CONVERTED_BUFFERS; index++)
		skin_free(skinned[index]);
	memset(skinned, 0, sizeof(skinned));
	memset(converted, 0, sizeof(converted));
	memset(converted_generation, 0, sizeof(converted_generation));
	converted_used = 0;
	converted_sweep_mark = CONVERTED_BUFFERS / 4; /* build 112 */
	converted_count = reconverted_count = skinned_parts = skin_batches = skin_bytes = skin_failures = 0;
	psp_ge_forget_level();
	/* build 87: the last map's stencil bake (the next map's pack brings its own) */
	bake_forget();
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
	/* build 116: the shader's fade (psp_limits.h PSP_SEETHROUGH_*): one
	colour for the whole draw (0xFFFFFFFF: none), or, when the fade depends
	on the angle the surface is seen at, a colour per vertex in the record's
	normal bytes (colored) */
	unsigned int color;
	unsigned char colored;
	/* build 116: the shader's tint (psp_ge_seethrough_tint), already in
	color or the per-vertex colours; kept for the near-plane cut */
	unsigned int tint;
	unsigned char sky, sprites;
	/* a widget sprite's (sky 2): whether it is hidden behind the level */
	unsigned char depth_test;
	/* the level's (build 23): 1 indexed into a level stream, 2 cut-up corners */
	unsigned char level;
	/* build 38: a first-person part (its own projection and depth slice) */
	unsigned char first_person;
	/* build 116: a meter's palette (clut, below), in the GE's 8-entry blocks */
	unsigned char clut_blocks;
	/* build 116: drawn with the GE's colour doubling (the tint is half) */
	unsigned char doubled;
	/* after b116: the shader (its tag's definition) the part was queued
	with, for tdump; 0 for sprites and parts queued without one */
	unsigned int shader;
	/* build 60: a see-through model part's triangles cut at the near plane
	or the guard band: GE vertices in the part's space, drawn after its list */
	void *cut_vertices;
	int cut_count;
	/* build 60: a level overlay's nudge toward the eye, in depth steps
	(0: worked out from distance when drawn, as builds 54-59) */
	short depth_units;
	/* build 116: a meter shader's draw (b113 #16): the palette worked out
	for its value (meter_palette), drawn in place of the map's own, with
	blend BLEND_METER_TINT or BLEND_METER_BRIGHTNESS and the meter's tint
	colour (ARGB) in tint; NULL for every other draw */
	const unsigned int *clut;
};

/* build 116: a meter shader's blends, as the Xbox sets them
(rasterizer_xbox_transparent_geometry.c): in tint mode 2 the frame gets
colour x tint + frame x alpha, as the HUD's meters; otherwise colour x the
meter's brightness + frame x tint, with the alpha test standing in for the
Xbox's alpha kill. Past the blend functions' 0-7 */
#define BLEND_METER_TINT 14
#define BLEND_METER_BRIGHTNESS 15

/* build 116: the see-through fade (defined with transparent_slot) */
/* build 116: a see-through shader's tint (texpack.py works it out: the
shader's stages with each map at its mean colour, over the mean of the map
the GE draws), for the next queue call's draws. The engine side sets it just
before each call (psp_fixups.py, psp_ge_seethrough_tint); the call's pieces
all take it, and it goes back to white when the call returns. Builds 21-115
drew the one map untinted: b30's ocean white */
static unsigned int seethrough_next_tint = 0xFFFFFFFF;
static unsigned int seethrough_next_shader = 0; /* after b116: for tdump */
/* build 116: the tint is half the shader's colour, drawn with the GE's
colour doubling (a shader brighter than its map: Cortana, the holograms) */
static unsigned char seethrough_next_doubled = 0;
static unsigned long seethrough_doubled_draws = 0;
/* build 116 (b113 #16): a meter shader's parameters, for the next queue
call, set as the tint is (psp_ge_seethrough_meter; seethrough_meter_next 0:
not a meter). Builds 21-115 dropped meter shaders: the plasma weapons' heat
gauges and the Warthog's meters weren't drawn */
static struct psp_seethrough_meter seethrough_next_meter;
static int seethrough_meter_next = 0;
static unsigned long meter_parts = 0, meter_parts_dropped = 0;
static unsigned int *meter_palette(unsigned int key, struct psp_seethrough_meter const *meter, int *count_out);
static int seethrough_fades(int blend);
static unsigned int seethrough_color(int f, int blend);
static unsigned int seethrough_tinted(unsigned int color, unsigned int tint);
static int seethrough_fade_of(const unsigned char *rgba, const struct transparent_draw *draw);
static float seethrough_fade(short packed);
static void seethrough_vertex_colors(struct transparent_draw *draw, int vertex_count, int placed, int mode,
	float fade);
static unsigned int seethrough_lit(unsigned int color, const float *wn, float nn);
/* the see-through fade mode the engine side packs for a glass shader's
diffuse map (psp_transparent_map): lit by the model's light (after b116) */
#define SEETHROUGH_LIT 3

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
/* build 114: 320 (160 before; "seethrough160" for it). b113's Test 1 dropped
494 see-through draws with the table full, Test 3 with the tracers 712
(hardware_b113.md): smoke, sparks and shots not drawn. 128 bytes each, so
the table comes from the heap when the GE starts (40 KB): as a static array
it pushed the program past one of the heap's 64 KB steps (5,888 -> 5,824 KB) */
#define MAXIMUM_TRANSPARENT_DRAWS 320
#define TRANSPARENT_DRAWS_B113 160
static struct transparent_draw *transparent = NULL;
static int transparent_capacity = 0;
static int transparent_count = 0, transparent_order[MAXIMUM_TRANSPARENT_DRAWS];
static unsigned long transparent_drawn = 0, transparent_dropped = 0;

static int transparent_full(void)
{
	return transparent_count >= transparent_capacity;
}

/* once, with the display list (display_list_acquire) */
static void transparent_table_acquire(void)
{
	int wanted = psp_keep("seethrough160") ? TRANSPARENT_DRAWS_B113 : MAXIMUM_TRANSPARENT_DRAWS;

	if (transparent)
		return;
	transparent = memalign(16, wanted * sizeof(*transparent));
	transparent_capacity = transparent ? wanted : 0;
	platform_log("GE: the see-through table: %d draws a frame, %d KB from the heap%s", transparent_capacity,
		(int)(wanted * sizeof(*transparent) / 1024), transparent ? "" : " (no memory: none drawn)");
}

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
	if (transparent_full())
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
	draw->color = 0xFFFFFFFF; /* build 116: no fade unless the filler sets one */
	draw->colored = 0;
	draw->tint = 0xFFFFFFFF;
	draw->clut = NULL; /* build 116: a meter's own palette, or none */
	draw->doubled = 0;
	draw->shader = 0;
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
	GE_DEFER(defer_sprites(vertices, sprite_count, bitmap_key, blend, view_to_world), 1); /* build 112: gedefer */
	GE_BENCH_SKIP(0);
	ge_kind(KIND_SPRITE); /* build 103 */
	unsigned int start = ge_clock();
	int result = psp_ge_sprites_body(vertices, sprite_count, bitmap_key, blend, view_to_world);

	ge_entry_us[4] += ge_clock() - start;
	return result;
}

/* ---------- contrails (build 113, b113_candidates.md #1; keep word "contrails")

The trail behind a projectile (the plasma rifle's and pistol's bolts, the
needler's needles): render_contrails.c builds a strip of quads along the
trail's points, two corners a point, in world coordinates, and hands it to
the transparent-geometry groups, which the PSP never draws. Here the strip
is queued with the see-through draws as a triangle strip in the sprites'
format (the corners in the game's own order: its two triangles a segment are
a strip's), under a matrix that moves the world so the camera is at its
origin, with the contrail shader's texture and blend. 48 bytes of list a
segment. The game has already faded the corners (the trail's ends, the angle
to the eye) in their alpha */
static unsigned long contrails_queued = 0, contrail_segments = 0;
/* build 114: a contrail's pairs of corners the GE can take, and for each
pair the one it is drawn from (itself, or the nearest one before it) */
#define CONTRAIL_PAIRS 256
static unsigned char contrail_pair_ok[CONTRAIL_PAIRS];
static short contrail_pair_from[CONTRAIL_PAIRS];
static unsigned long contrail_pairs_folded = 0, contrails_out_of_view = 0;

static int psp_ge_contrail_body(void const *vertices, int vertex_count, unsigned int bitmap_key, short blend)
{
	struct game_sprite_vertex const *in = vertices;
	struct sprite_vertex *out;
	struct transparent_draw *draw;
	int segments = vertex_count / 2 - 1, corner;
	float cx = 0.0f, cy = 0.0f, cz = 0.0f;

	if (!frame_open || !vertices || segments <= 0 || !bitmap_key || list_full())
		return 0;
	if (transparent_full())
	{
		transparent_dropped++;
		return 0;
	}
	vertex_count = 2 * (segments + 1);
	if (vertex_count > 2 * CONTRAIL_PAIRS)
		vertex_count = 2 * CONTRAIL_PAIRS;
	{
		/* build 114: the pairs the GE can't take (b113 Test 3: Sheepdog01 saw the
		plasma rifle's shots "streak badly and don't always render"). A
		corner behind the near plane, or so far to a side that its screen
		position passes the GE's 2048 pixels, wraps the triangle into a spike
		across the screen or loses it (camera_corner_out; the player's own
		shots trail back past the camera). Such a pair is folded onto the
		nearest pair the GE can take, so the triangles that touch it have no
		area; the trail stops where it leaves the view */
		int pairs = vertex_count / 2, pair, last_good = -1, any_good = 0;
		/* the models' margin (psp_ge_draw_model_body) */
		float limit = projection_near * 1.25f;

		for (pair = 0; pair < pairs; pair++)
		{
			float a[3], b[3];

			to_camera(&in[2 * pair].x, a);
			to_camera(&in[2 * pair + 1].x, b);
			contrail_pair_ok[pair] = !camera_corner_out(a[0], a[1], a[2], limit) &&
				!camera_corner_out(b[0], b[1], b[2], limit);
			any_good |= contrail_pair_ok[pair];
		}
		if (!any_good)
		{
			contrails_out_of_view++;
			return 0;
		}
		for (pair = 0; pair < pairs && !contrail_pair_ok[pair]; pair++)
			;
		last_good = pair;
		for (pair = 0; pair < pairs; pair++)
		{
			if (contrail_pair_ok[pair])
				last_good = pair;
			else
				contrail_pairs_folded++;
			contrail_pair_from[pair] = (short)last_good;
		}
	}
	out = list_take(vertex_count * sizeof(struct sprite_vertex));
	if (!out)
		return 0;
	for (corner = 0; corner < vertex_count; corner++)
	{
		struct game_sprite_vertex const *v = &in[2 * contrail_pair_from[corner / 2] + (corner & 1)];
		struct sprite_vertex *o = &out[corner];
		unsigned int argb = v->argb;

		o->u = v->u;
		o->v = v->v;
		o->color = (argb & 0xFF00FF00) | ((argb >> 16) & 0xFF) | ((argb & 0xFF) << 16);
		o->x = v->x; o->y = v->y; o->z = v->z;
		cx += v->x; cy += v->y; cz += v->z;
	}
	draw = &transparent[transparent_count++];
	draw->color = 0xFFFFFFFF; /* build 116: no fade unless the filler sets one */
	draw->colored = 0;
	draw->tint = 0xFFFFFFFF;
	draw->clut = NULL; /* build 116: a meter's own palette, or none */
	draw->doubled = 0;
	draw->shader = 0;
	memset(&draw->matrix, 0, sizeof(draw->matrix));
	draw->matrix.x.x = draw->matrix.y.y = draw->matrix.z.z = draw->matrix.w.w = 1.0f;
	relative(&draw->matrix);
	draw->vertices = out;
	draw->indices = NULL;
	draw->index_count = vertex_count;
	draw->strip = 1;
	draw->key = bitmap_key;
	draw->u_scale = draw->v_scale = 1.0f;
	draw->u_offset = draw->v_offset = 0.0f;
	draw->blend = blend;
	draw->sky = 0;
	draw->sprites = 1;
	draw->level = 0;
	draw->first_person = 0;
	draw->cut_vertices = NULL;
	draw->cut_count = 0;
	/* from the eye, squared, as the other see-through draws sort */
	cx = cx / vertex_count - camera_eye[0];
	cy = cy / vertex_count - camera_eye[1];
	cz = cz / vertex_count - camera_eye[2];
	draw->distance = cx * cx + cy * cy + cz * cz;
	contrails_queued++;
	contrail_segments += segments;
	return 1;
}

int psp_ge_contrail(void const *vertices, int vertex_count, unsigned int bitmap_key, short blend)
{
	GE_DEFER(defer_contrail(vertices, vertex_count, bitmap_key, blend), 1);
	GE_BENCH_SKIP(0);
	ge_kind(KIND_SPRITE);
	unsigned int start = ge_clock();
	int result = psp_ge_contrail_body(vertices, vertex_count, bitmap_key, blend);

	ge_entry_us[4] += ge_clock() - start;
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
	GE_DEFER(defer_words(DEFER_WIDGET_BEGIN, 1, (unsigned int)depth_test, 0, 0, 0, 0, 0, 0, 0)); /* build 112: gedefer */
	widget_depth = depth_test;
	widget_begins++;
}

void psp_ge_widget_texture(unsigned int key)
{
	GE_DEFER(defer_words(DEFER_WIDGET_TEXTURE, 1, key, 0, 0, 0, 0, 0, 0, 0)); /* build 112: gedefer */
	widget_key = key;
	if (!key)
		widget_untextured++;
}

/* the occlusion rays' tally, for the log */
void psp_ge_widget_occlusion(int visible)
{
	GE_DEFER(defer_words(DEFER_WIDGET_OCCLUSION, 1, (unsigned int)visible, 0, 0, 0, 0, 0, 0, 0)); /* build 112: gedefer */
	occlusion_tests++;
	if (visible)
		occlusion_visible++;
}

void psp_ge_widget_sprite(float const *point, float radius, float scale_x, float scale_y, float rotation,
	unsigned long color)
{
	GE_DEFER(defer_widget_sprite(point, radius, scale_x, scale_y, rotation, color)); /* build 112: gedefer */
	GE_BENCH_SKIP();
	ge_kind(KIND_WIDGET); /* build 103 */
	static const float corner_uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	static const int fan[6] = { 0, 1, 2, 0, 2, 3 };
	struct sprite_vertex *out;
	struct transparent_draw *draw;
	float centre[3], offset[4][2], camera[3], axis_x, axis_y, alpha, limit, pull = 1.0f;
	unsigned int r, g, b;
	int corner;

	if (!frame_open || !widget_key || radius <= 0.0f || list_full())
		return;
	if (transparent_full())
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
	draw->color = 0xFFFFFFFF; /* build 116: no fade unless the filler sets one */
	draw->colored = 0;
	draw->tint = 0xFFFFFFFF;
	draw->clut = NULL; /* build 116: a meter's own palette, or none */
	draw->doubled = 0;
	draw->shader = 0;
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

/* build 118: the sphere first. Most see-through parts are wholly in view,
and for those transparent_cut (below) transforms every corner and tests
every triangle only to leave the draw as it was (1.3-3.5 ms a frame in the
emulator, b117_candidates.md [E]). The model's bounding sphere decides it
first (psp_ge_model_shape: the object's, its root node's bounding offset
and the tag's bounding radius times its scale, the sphere the game culls the
object by). If the sphere lies wholly inside every plane outside() tests,
so does every corner within it, no triangle has a bit set, and the loop
would return with the draw untouched: it is skipped.
That holds only while every corner lies within the sphere, which nothing
guarantees: a tag's radius set too small, a part animated out past it, a
blended corner placed by its first node alone (the rigid record keeps only
that, psp_ge_queue_transparent_body). The opaque parts have trusted the
same sphere since build 56 (model_near: a model whose sphere is clear has
no triangle checked). Parts without a sphere (the sky's, a flag's: radius
0) take the loop as before. Plain floats and no system call: this can run
on the Media Engine.
"tcutcheck": the loop runs anyway after the sphere says inside, and the
log counts the parts where it found a triangle outside (the first few with
how far out their farthest corner lies). "tcutfull": no sphere test, as
builds 60-117 */
static int tcut_full = -1, tcut_check = -1;
static unsigned long tcut_parts = 0, tcut_no_sphere = 0, tcut_inside = 0, tcut_mismatches = 0;
static unsigned long tcut_mismatches_logged = 0;

/* whether the model's sphere lies wholly inside the planes outside()
tests (sphere_inside, at the near plane outside() uses) */
static int model_sphere_inside(void)
{
	return model_sphere_radius > 0.0f && sphere_inside(model_sphere, model_sphere_radius, NEAR_CLIP);
}

/* "tcutcheck": a part its model's sphere called wholly in view has a
triangle outside some plane (bits, outside()'s). Counted; the first few
logged, with the part's corner farthest from the sphere's centre */
static void transparent_sphere_missed(const struct transparent_draw *draw, int vertex_count, int bits)
{
	float farthest = 0.0f;
	long i;

	tcut_mismatches++;
	if (tcut_mismatches_logged >= 8)
		return;
	tcut_mismatches_logged++;
	for (i = 0; i < draw->index_count; i++)
	{
		float c[3], dx, dy, dz;

		if (draw->indices[i] >= vertex_count)
			continue;
		model_to_camera(&draw->matrix, (const float *)((const unsigned char *)draw->vertices + 24 * draw->indices[i] + 12), c);
		dx = c[0] - model_sphere[0];
		dy = c[1] - model_sphere[1];
		dz = c[2] - model_sphere[2];
		if (dx * dx + dy * dy + dz * dz > farthest)
			farthest = dx * dx + dy * dy + dz * dz;
	}
	/* (ge_log: from the ME's work, counted only) */
	ge_log("GE: tcutcheck: a see-through part (key %08x, %d vertices) its model's sphere (radius %.3f, %.2f ahead) called wholly in view has a triangle outside (planes %d); its farthest corner is %.3f from the centre",
		draw->key, vertex_count, model_sphere_radius, model_sphere[2], bits, sqrtf(farthest));
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
	int k, any = 0, cut = 0, sphere_inside = 0;

	/* build 118: the model's sphere first (above) */
	if (tcut_full < 0)
		tcut_full = psp_keep("tcutfull");
	if (tcut_check < 0)
		tcut_check = psp_keep("tcutcheck");
	tcut_parts++;
	if (!tcut_full)
	{
		if (model_sphere_radius <= 0.0f)
			tcut_no_sphere++;
		else if (model_sphere_inside())
		{
			tcut_inside++;
			if (!tcut_check)
				return;
			sphere_inside = 1;
		}
	}
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
			if (sphere_inside)
				transparent_sphere_missed(draw, vertex_count, bits[0] | bits[1] | bits[2]);
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
				/* build 116: a coloured draw's fade in the normal's place:
				the pieces' colour is rebuilt from it */
				polygon[k][5] = draw->colored ? (float)seethrough_fade_of(record + 8, draw) : (float)(signed char)record[8];
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
			if (draw->colored)
				*(unsigned int *)(record + 8) = seethrough_tinted(seethrough_color((int)(model_cut[c][5] + 0.5f),
					draw->blend), draw->tint);
			else
			{
				record[8] = (unsigned char)(signed char)model_cut[c][5];
				record[9] = (unsigned char)(signed char)model_cut[c][6];
				record[10] = (unsigned char)(signed char)model_cut[c][10];
				record[11] = 0;
			}
			memcpy(record + 12, &model_cut[c][7], 12);
		}
		draw->cut_vertices = out;
		draw->cut_count = cut;
	}
}

/* build 118: whether a see-through part's triangles use more than one node
(the scan below, build 42's), remembered per part. The scan read every
index's vertex every frame for every see-through part, and its answer only
changes with the part's data: c40's see-through queue was 18% of its
drawing's instructions (nearwins118). Kept under the vertex and index
buffers, their counts, and the geometry cache's generation for each buffer
(an address the cache loads with something else gets a new one). "multiscan"
(keep.txt): scanned every frame, as before; "multicheck": a remembered
answer is scanned again and compared */
#define MULTI_MEMO 64
static struct
{
	const void *vertices;
	const unsigned short *indices;
	long index_count;
	int vertex_count;
	unsigned int vertices_generation, indices_generation;
	int multi;
} multi_memo[MULTI_MEMO];
static int multi_scan = -1, multi_check = 0;
static unsigned long multi_scans = 0, multi_remembered = 0, multi_mismatches = 0;

static int transparent_part_scan(const unsigned char *base, const unsigned short *indices, long index_count, int vertex_count)
{
	int first = indices[0] < vertex_count ? base[24 * indices[0] + 11] : 0;
	long i;

	for (i = 0; i < index_count; i++)
		if (indices[i] < vertex_count && base[24 * indices[i] + 11] != first)
			return 1;
	return 0;
}

static int transparent_part_multi(const unsigned char *base, const unsigned short *indices, long index_count, int vertex_count)
{
	unsigned int slot = (((unsigned int)base >> 4) ^ ((unsigned int)indices >> 2) ^ (unsigned int)index_count) & (MULTI_MEMO - 1);
	unsigned int vertices_generation, indices_generation;
	int multi;

	if (multi_scan < 0)
	{
		multi_scan = psp_keep("multiscan");
		multi_check = psp_keep("multicheck");
	}
	if (multi_scan)
		return transparent_part_scan(base, indices, index_count, vertex_count);
	vertices_generation = psp_geo_generation(base);
	indices_generation = psp_geo_generation(indices);
	if (multi_memo[slot].vertices == base && multi_memo[slot].indices == indices &&
		multi_memo[slot].index_count == index_count && multi_memo[slot].vertex_count == vertex_count &&
		multi_memo[slot].vertices_generation == vertices_generation && multi_memo[slot].indices_generation == indices_generation)
	{
		multi_remembered++;
		if (multi_check && transparent_part_scan(base, indices, index_count, vertex_count) != multi_memo[slot].multi)
			multi_mismatches++;
		return multi_memo[slot].multi;
	}
	multi_scans++;
	multi = transparent_part_scan(base, indices, index_count, vertex_count);
	multi_memo[slot].vertices = base;
	multi_memo[slot].indices = indices;
	multi_memo[slot].index_count = index_count;
	multi_memo[slot].vertex_count = vertex_count;
	multi_memo[slot].vertices_generation = vertices_generation;
	multi_memo[slot].indices_generation = indices_generation;
	multi_memo[slot].multi = multi;
	return multi;
}

static void psp_ge_queue_transparent_body(void *vertices, int vertex_count, int uncompressed, const unsigned short *indices,
	long index_count, int strip, unsigned int key, float u_scale, float v_scale, float u_offset, float v_offset,
	short blend, const float *centroid)
{
	struct transparent_draw *draw;

	unsigned int *clut = NULL;
	int clut_count = 0;

	if (!frame_open || index_count < 3 || !vertices || !indices || !model_nodes || model_node_count <= 0 ||
		vertex_count <= 0 || !model_convert(vertices, vertex_count, uncompressed, NULL, 0, 0, NULL))
		return;
	if (transparent_full())
	{
		transparent_dropped++;
		return;
	}
	if (seethrough_meter_next)
	{
		/* build 116: a meter's palette for its value now (the cache slot is
		this frame's until the list has run), from its map's, brought into
		memory first (the flush binds it later); none (a map without a 32-bit
		palette, not loaded yet, the list full): the part is left out */
		meter_parts++;
		clut = psp_tex_resident(key) ? meter_palette(key, &seethrough_next_meter, &clut_count) : NULL;
		if (!clut || clut_count < 8)
		{
			meter_parts_dropped++;
			return;
		}
	}
	draw = &transparent[transparent_count++];
	draw->color = 0xFFFFFFFF; /* build 116: no fade unless the filler sets one */
	draw->colored = 0;
	draw->tint = 0xFFFFFFFF;
	draw->clut = NULL; /* build 116: a meter's own palette, or none */
	draw->doubled = 0;
	draw->shader = 0;
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
		int multi = transparent_part_multi(base, indices, index_count, vertex_count); /* build 118: remembered */

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
	draw->blend = PSP_SEETHROUGH_BLEND(blend);
	draw->tint = seethrough_next_tint;
	draw->shader = seethrough_next_shader;
	draw->doubled = seethrough_next_doubled;
	draw->color = draw->tint;
	draw->colored = 0;
	if (clut)
	{
		/* build 116: a meter (the palette does its colours; no fade) */
		draw->clut = clut;
		draw->clut_blocks = (unsigned char)(clut_count / 8);
		draw->blend = seethrough_next_meter.tint_mode_2 ? BLEND_METER_TINT : BLEND_METER_BRIGHTNESS;
		draw->tint = seethrough_next_meter.tint_color;
		draw->color = 0xFFFFFFFF;
		draw->doubled = 0;
	}
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
	/* build 116: the shader's fade (the source's value, active camouflage,
	the angle), before the cut so the cut pieces carry it */
	if (seethrough_fades(draw->blend))
	{
		float fade = seethrough_fade(blend);
		int mode = PSP_SEETHROUGH_MODE(blend);

		if (mode != 0 && vertex_count <= 4096)
			seethrough_vertex_colors(draw, vertex_count, draw->vertices != vertices, mode, fade);
		else if (fade < 1.0f)
			draw->color = seethrough_tinted(seethrough_color((int)(fade * 255.0f + 0.5f), draw->blend), draw->tint);
	}
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
	GE_BENCH_SKIP();
	GE_DEFER(defer_queue_transparent(vertices, vertex_count, uncompressed, indices, index_count, strip, key,
		u_scale, v_scale, u_offset, v_offset, blend, centroid));
	ge_kind(KIND_SEETHROUGH_QUEUE); /* build 103 */
	unsigned int start = ge_clock();

	psp_ge_queue_transparent_body(vertices, vertex_count, uncompressed, indices, index_count, strip, key, u_scale, v_scale, u_offset, v_offset, blend, centroid);
	seethrough_next_tint = 0xFFFFFFFF; /* build 116: this call's tint is used */
	seethrough_next_shader = 0;
	seethrough_next_doubled = 0;
	seethrough_meter_next = 0; /* and its meter */
	ge_entry_us[2] += ge_clock() - start;
}

/* the level's see-through surfaces (build 23): as the solid level, each
triangle checked against the camera (kept, skipped, or cut up, the pieces
turned back into the world's coordinates), then queued with an identity
matrix: the kept ones indexed into the material's stream, the pieces as
corners */
static unsigned long level_transparent_draws = 0;
static unsigned long ge_wait_us = 0, ge_wait_worst = 0;
static unsigned long frame_end_us[3];

/* build 116: a see-through shader's fade as a GE colour that the texture is
multiplied by (TFX_MODULATE): for an alpha blend the alpha, for the added and
subtracted blends the colour and alpha alike. The multiplied and min/max
blends would need a fade toward their neutral colour, which a modulate can't
do: they stay at full strength (seethrough_fades) */
static int seethrough_fades(int blend)
{
	return blend == 0 || blend == 3 || blend == 4 || blend == 7;
}

static unsigned int seethrough_color(int f, int blend)
{
	f = f < 0 ? 0 : f > 255 ? 255 : f;
	if (blend == 0)
		return ((unsigned int)f << 24) | 0x00FFFFFF;
	return ((unsigned int)f << 24) | ((unsigned int)f << 16) | ((unsigned int)f << 8) | (unsigned int)f;
}

/* build 116: the next queue call's tint, and whether it is drawn doubled;
after b116 also its shader (the tag's definition, for tdump; 0 for none) */
static void seethrough_tint_set(unsigned int tint, int doubled, unsigned int shader)
{
	GE_DEFER(defer_words(DEFER_SEETHROUGH_TINT, 3, tint, (unsigned int)doubled, shader, 0, 0, 0, 0, 0)); /* build 112: gedefer */
	seethrough_next_tint = tint;
	seethrough_next_doubled = (unsigned char)(doubled != 0);
	seethrough_next_shader = shader;
}

void psp_ge_seethrough_tint(unsigned int tint)
{
	seethrough_tint_set(tint, 0, 0);
}

/* the engine side's call before each see-through queue call: the shader's
tint from the texture pack (psp_tex.c; white for a shader it has none for) */
void psp_ge_seethrough_shader(unsigned int shader)
{
	int doubled = 0;
	unsigned int tint = psp_tex_shader_tint(shader, &doubled);

	seethrough_tint_set(tint, doubled, shader);
}

/* build 116: the next queue call's meter (struct psp_seethrough_meter), or
NULL for a part that isn't one. The engine side calls it before every model
part it queues (psp_fixups.py), so a meter never carries over to the next */
void psp_ge_seethrough_meter(const void *meter)
{
	const unsigned int *w = meter;

	GE_DEFER(w ? defer_words(DEFER_SEETHROUGH_METER, 1 + METER_WORDS, 1, w[0], w[1], w[2], w[3], w[4], w[5], w[6]) :
		defer_words(DEFER_SEETHROUGH_METER, 1, 0, 0, 0, 0, 0, 0, 0, 0)); /* build 112: gedefer */
	seethrough_meter_next = meter != NULL;
	if (meter)
		memcpy(&seethrough_next_meter, meter, sizeof(seethrough_next_meter));
}

/* two colours multiplied channel by channel (ABGR, 0-255 each) */
static unsigned int seethrough_tinted(unsigned int color, unsigned int tint)
{
	unsigned int out = 0;
	int shift;

	if (tint == 0xFFFFFFFF)
		return color;
	for (shift = 0; shift < 32; shift += 8)
		out |= ((((color >> shift) & 255) * ((tint >> shift) & 255) + 127) / 255) << shift;
	return out;
}

/* the fade (0-255) a tinted per-vertex colour was made from, for the
near-plane cut: through the alpha for an alpha blend, otherwise through the
tint's strongest channel (an added blend's colour is the fade in every
channel) */
static int seethrough_fade_of(const unsigned char *rgba, const struct transparent_draw *draw)
{
	int channel = 3, k, t;

	if (draw->blend != 0)
		for (k = 0; k < 3; k++)
			if (((draw->tint >> (8 * k)) & 255) > ((draw->tint >> (8 * channel)) & 255))
				channel = k;
	t = (int)((draw->tint >> (8 * channel)) & 255);
	if (t <= 0)
		return 0;
	t = (rgba[channel] * 255 + t / 2) / t;
	return t > 255 ? 255 : t;
}

/* the fade a packed blend asks for, times active camouflage's (the Xbox's
1 - intensity, unless the shader says to keep it), 0-1 (model_camouflage:
psp_ge_model_effect) */
static float seethrough_fade(short packed)
{
	float fade = PSP_SEETHROUGH_FADE(packed);

	if (!PSP_SEETHROUGH_KEEP_WITH_CAMOUFLAGE(packed))
		fade *= 1.0f - model_camouflage;
	return fade;
}

/* build 116: the fade by the angle a see-through part is seen at, a colour
per vertex in a copy of its records (the 8-bit normal's 4 bytes become the
colour; the flush draws it with GU_COLOR_8888). As the Xbox's vertex program
for transparent shaders works it out (rasterizer_xbox_vertex_shaders_data.inc,
program 24, translated by the port's nv2a_vsh.c): c = |n . the camera's
forward direction| (1 seen face on, 0 seen edge on); its second colour
carries c in blue and 1 - c in alpha, and "fade when perpendicular" (mode 1)
takes the alpha, "fade when parallel" (mode 2) the blue. So a surface that
fades when perpendicular vanishes seen face on (a light volume looked
straight into), and one that fades when parallel vanishes edge on. The first
version had the two the other way round (c40's pale discs stayed). Placed
records (several nodes, already in the camera's space) keep their node in
byte 11; the others go through the draw's matrix */
static unsigned long seethrough_angle_draws = 0, seethrough_lit_draws = 0;
/* after build 116: a glass shader's diffuse map lit as the Xbox lights it
(its vertex program for glass's diffuse pass): the model's ambient colour
plus each of its distant lights by the normal's angle to it, times the
colour. Glass drew at full brightness in dark rooms (a10's bridge monitor:
a bright green pattern); the shader's reflection (a cube map) and tint
passes aren't drawn yet. Colours are the GE's ABGR; nn is the world
normal's squared length */
static unsigned int seethrough_lit(unsigned int color, const float *wn, float nn)
{
	float light[3], inverse = nn > 0.0f ? 1.0f / sqrtf(nn) : 0.0f;
	unsigned int out = color & 0xFF000000u;
	int index, channel;

	for (channel = 0; channel < 3; channel++)
		light[channel] = (float)((model_ambient >> (8 * channel)) & 0xFF);
	for (index = 0; index < model_light_count; index++)
	{
		float d = (wn[0] * model_light_toward[index].x + wn[1] * model_light_toward[index].y +
			wn[2] * model_light_toward[index].z) * inverse;

		if (d > 0.0f)
			for (channel = 0; channel < 3; channel++)
				light[channel] += d * (float)((model_light_color[index] >> (8 * channel)) & 0xFF);
	}
	for (channel = 0; channel < 3; channel++)
	{
		float value = (float)((color >> (8 * channel)) & 0xFF) * (light[channel] > 255.0f ? 255.0f : light[channel]) /
			255.0f;

		out |= (unsigned int)(value + 0.5f) << (8 * channel);
	}
	return out;
}

static void seethrough_vertex_colors(struct transparent_draw *draw, int vertex_count, int placed, int mode,
	float fade)
{
	static ScePspFMatrix4 at_node[SKIN_NODES];
	static unsigned char done[SKIN_NODES];
	unsigned char *records = (unsigned char *)draw->vertices;
	int v;

	if (!placed)
	{
		unsigned char *copy = list_take(vertex_count * 24);

		if (!copy)
			return;
		memcpy(copy, records, vertex_count * 24);
		records = copy;
	}
	memset(done, 0, sizeof(done));
	for (v = 0; v < vertex_count; v++)
	{
		unsigned char *record = records + 24 * v;
		float n[3] = { (signed char)record[8], (signed char)record[9], (signed char)record[10] };
		const ScePspFMatrix4 *m = &draw->matrix;
		float wn[3], nn, c, a;

		if (placed)
		{
			int node = record[11] / 3;

			if (node >= SKIN_NODES || node >= model_node_count)
				node = 0;
			if (!done[node])
			{
				halo_matrix(model_nodes + 13 * node, &at_node[node]);
				done[node] = 1;
			}
			m = &at_node[node];
		}
		wn[0] = m->x.x * n[0] + m->y.x * n[1] + m->z.x * n[2];
		wn[1] = m->x.y * n[0] + m->y.y * n[1] + m->z.y * n[2];
		wn[2] = m->x.z * n[0] + m->y.z * n[1] + m->z.z * n[2];
		nn = wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2];
		if (mode == SEETHROUGH_LIT)
		{
			*(unsigned int *)(record + 8) = seethrough_lit(seethrough_tinted(seethrough_color((int)(fade * 255.0f + 0.5f),
				draw->blend), draw->tint), wn, nn);
			continue;
		}
		c = nn > 0.0f ? fabsf(wn[0] * camera_forward[0] + wn[1] * camera_forward[1] + wn[2] * camera_forward[2]) /
			sqrtf(nn) : 1.0f;
		a = mode == 1 ? 1.0f - c : c;
		*(unsigned int *)(record + 8) = seethrough_tinted(seethrough_color((int)(fade * a * 255.0f + 0.5f), draw->blend),
			draw->tint);
	}
	draw->vertices = records;
	draw->colored = 1;
	if (mode == SEETHROUGH_LIT)
		seethrough_lit_draws++;
	else
		seethrough_angle_draws++;
}

static struct transparent_draw *transparent_slot(unsigned int key, float u_scale, float v_scale, float u_offset,
	float v_offset, short blend, const float *centroid)
{
	struct transparent_draw *draw;

	if (transparent_full())
	{
		transparent_dropped++;
		return NULL;
	}
	draw = &transparent[transparent_count++];
	draw->color = 0xFFFFFFFF; /* build 116: no fade unless the filler sets one */
	draw->colored = 0;
	draw->tint = 0xFFFFFFFF;
	draw->clut = NULL; /* build 116: a meter's own palette, or none */
	draw->doubled = 0;
	draw->shader = 0;
	memset(&draw->matrix, 0, sizeof(draw->matrix));
	draw->matrix.x.x = draw->matrix.y.y = draw->matrix.z.z = draw->matrix.w.w = 1.0f;
	relative(&draw->matrix);
	draw->key = key;
	draw->u_scale = u_scale != 0.0f ? u_scale : 1.0f;
	draw->v_scale = v_scale != 0.0f ? v_scale : 1.0f;
	draw->u_offset = u_offset;
	draw->v_offset = v_offset;
	/* build 116: the level's see-through surfaces take the fade's one
	colour; their fade by angle isn't drawn yet (no normals in the level's
	GE copies) */
	draw->blend = PSP_SEETHROUGH_BLEND(blend);
	draw->tint = seethrough_next_tint;
	draw->shader = seethrough_next_shader;
	draw->doubled = seethrough_next_doubled;
	draw->color = seethrough_tinted(seethrough_fades(draw->blend) && PSP_SEETHROUGH_FADE(blend) < 1.0f ?
		seethrough_color((int)(PSP_SEETHROUGH_FADE(blend) * 255.0f + 0.5f), draw->blend) : 0xFFFFFFFF, draw->tint);
	draw->colored = 0;
	draw->sky = 0;
	draw->sprites = 0;
	draw->strip = 0;
	draw->depth_units = 0;
	draw->cut_vertices = NULL;
	draw->cut_count = 0;
	/* build 89: nothing of a previous frame's draw left in the slot: its
	filler sets these, and a slot left unfilled is skipped at the flush */
	draw->vertices = NULL;
	draw->indices = NULL;
	draw->index_count = 0;
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
static float slope_units_capped(float units, const float *a, const float *b, const float *c, float reference)
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
	/* build 118: the depth the cap is measured at, when the caller gives one
	(the triangle's farthest corner, overlay_units) */
	if (reference > 0.0f)
		nearest = reference;
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
the frame's edge) the solid side now wins the tie. Returned negative.
build 115: "overlayfreefar" (A/B; Sheepdog01's b114 run: b30's ocean "clips through
the terrain under the water surface to varying degrees" in the flight). The
ocean is free (the collision has no surface at the water: water is a fog
plane there), and a draw goes out at the LARGEST push of its triangles. A
triangle with a corner at the near plane asks 0.005 units of lift there,
307/near steps (2,457 at x2, the 4,096 cap at x1): hundreds of units at
70 units off, so the water lost to any seabed under it; triangles wholly in
view asked the 3-step floor, about 2 units at 70 off at x2. With the word:
the lift is measured at the triangle's FARTHEST corner, the floor is one
step (the solid side still wins an exact tie), and a draw takes the
SMALLEST push of its free triangles (one constant offset is a lift that
grows with the square of the distance, so the far end decides).
build 116: on by default, a restoration (the ocean behind the seabed, as
the Xbox draws it: Sheepdog01's b115 test 5 on the PSP); keep word
"overlayfreenear": as builds 65-115 */
static int overlay_free_far = -1, overlay_wall_near = -1;
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
	{
		if (overlay_free_far < 0)
			overlay_free_far = !psp_keep("overlayfreenear");
		if (overlay_free_far)
		{
			/* build 115: "overlayfreefar" (above; the default from build 116): measured at the far corner */
			float farthest = a[2], lift;

			if (b[2] > farthest) farthest = b[2];
			if (c[2] > farthest) farthest = c[2];
			if (farthest < projection_near)
				farthest = projection_near;
			lift = nudge_lift_share * farthest;
			if (lift > nudge_lift_ceiling)
				lift = nudge_lift_ceiling;
			if (lift < 0.005f)
				lift = 0.005f;
			units = lift * 61439.0f * projection_near / (farthest * farthest);
			return units < 1.0f ? -1 : units > 4096.0f ? -4096 : -(int)units;
		}
		return units < 3.0f ? -3 : units > 4096.0f ? -4096 : -(int)units;
	}
	/* build 118: an on-wall triangle's nudge measured at its FARTHEST corner,
	as build 115 did for free ones. One depth offset is a lift that grows
	with the square of the distance, so a triangle reaching from the near
	plane outward, its nudge sized at the near end (up to the 4,096-step
	cap), was lifted ~10 units at 3 units off: b30's bunker glass floor
	drawn through the hallway wall (b113 #13, b116 test 1, lab batch
	pointlights118 [E]), and likely c40's bridge glass through Chief's legs
	(b116 test 3) [U]. Keep word "overlaywallnear": as before */
	if (overlay_wall_near < 0)
		overlay_wall_near = psp_keep("overlaywallnear");
	if (!overlay_wall_near)
	{
		float farthest = a[2], lift;

		if (b[2] > farthest) farthest = b[2];
		if (c[2] > farthest) farthest = c[2];
		if (farthest < projection_near)
			farthest = projection_near;
		lift = nudge_lift_share * farthest;
		if (lift > nudge_lift_ceiling)
			lift = nudge_lift_ceiling;
		if (lift < 0.005f)
			lift = 0.005f;
		units = lift * 61439.0f * projection_near / (farthest * farthest);
		units += slope_units_capped(slope_units(a, b, c), a, b, c, farthest) + nudge_floor;
		return units < 3.0f ? 3 : units > 4096.0f ? 4096 : (int)units;
	}
	units += slope_units_capped(slope_units(a, b, c), a, b, c, 0.0f) + nudge_floor;
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
} overlay_backing[OVERLAY_BACKINGS] PSP_MAIN_SIDE; /* build 118: the main CPU's, as calls are written down */
static PSP_MAIN_SIDE int overlay_backing_count = 0, overlay_all_walls = -1;
/* after b116: the last list's flags, so its triangles don't search the
table again each (it holds up to 2,048 lists; an independent review) */
static PSP_MAIN_SIDE unsigned int overlay_cached_key = 0;
static PSP_MAIN_SIDE long overlay_cached_count = -1;
static PSP_MAIN_SIDE unsigned char *overlay_cached_flags = NULL;

static void overlay_backing_forget(void)
{
	int index;

	for (index = 0; index < overlay_backing_count; index++)
		free(overlay_backing[index].flags);
	overlay_backing_count = 0;
	overlay_cached_flags = NULL;
}
static PSP_MAIN_SIDE unsigned long overlay_wall_asks = 0, overlay_on_walls = 0, overlay_free = 0, overlay_ask_us = 0;

static PSP_MAIN_SIDE unsigned long overlay_backing_full = 0;

/* build 112: triangles whose answer work marked as the Media Engine's needed
and didn't have (gemesim): finding it takes heap for the answers and casts
the engine's collision rays, which read the game's data while the main CPU
may be moving it. The main CPU asks them at the frame's end
(psp_ge_frame_end); meanwhile the triangle counts as on a wall, as it does
when there's no memory for its answers */
#define WALLS_WANTED 64
static struct
{
	const struct ge_vertex *stream;
	const unsigned short *indices;
	long triangle_count, triangle;
} walls_wanted[WALLS_WANTED];
static int walls_wanted_count = 0;
static unsigned long walls_wanted_total = 0, walls_wanted_dropped = 0;

static int overlay_want(const struct ge_vertex *stream, const unsigned short *indices, long triangle_count, long triangle)
{
	if (walls_wanted_count < WALLS_WANTED)
	{
		walls_wanted[walls_wanted_count].stream = stream;
		walls_wanted[walls_wanted_count].indices = indices;
		walls_wanted[walls_wanted_count].triangle_count = triangle_count;
		walls_wanted[walls_wanted_count].triangle = triangle;
		walls_wanted_count++;
		walls_wanted_total++;
	}
	else
		walls_wanted_dropped++;
	return 1;
}

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
	/* build 89: keyed by the list's CONTENT (its indices hashed, with the
	material's copies): the see-through level lists come from the game's
	per-frame triangle pool, never the geometry cache, so b81's pack-address
	key was the raw pointer here, the Nth slot of this frame's pool rather
	than a surface, and another overlay with the same triangle count could
	inherit its answers (review). Worked out once per list per frame */
	{
		static PSP_MAIN_SIDE const unsigned short *last_list = NULL;
		static PSP_MAIN_SIDE const struct ge_vertex *last_stream = NULL;
		static PSP_MAIN_SIDE unsigned int last_frame_seen = 0, last_key = 0;
		static PSP_MAIN_SIDE long last_count = -1;

		if (indices != last_list || stream != last_stream || stream_frame != last_frame_seen ||
			triangle_count != last_count)
		{
			unsigned int h = 2166136261u ^ (unsigned int)stream;
			long i;

			for (i = 0; i < triangle_count * 3; i++)
				h = (h ^ indices[i]) * 16777619u;
			last_list = indices;
			last_stream = stream;
			last_frame_seen = stream_frame;
			last_count = triangle_count;
			last_key = h | 1;
		}
		key = last_key;
	}
	/* (the main CPU's alone: the Media Engine's cache doesn't see its writes) */
	if (!PSP_ME_WORK() && overlay_cached_flags && key == overlay_cached_key && triangle_count == overlay_cached_count)
		flags = overlay_cached_flags;
	else
		for (slot = 0; slot < overlay_backing_count; slot++)
			if (overlay_backing[slot].key == key && overlay_backing[slot].count == triangle_count)
			{
				flags = overlay_backing[slot].flags;
				break;
			}
	if (!flags && PSP_ME_WORK())
		return overlay_want(stream, indices, triangle_count, triangle);
	if (!flags)
	{
		if (!(flags = calloc(triangle_count, 1)))
			return 1;
		if (overlay_backing_count >= OVERLAY_BACKINGS)
		{
			static PSP_MAIN_SIDE int victim = 0;

			slot = victim;
			victim = (victim + 1) % OVERLAY_BACKINGS;
			if (overlay_cached_flags == overlay_backing[slot].flags)
				overlay_cached_flags = NULL;
			free(overlay_backing[slot].flags);
			overlay_backing_full++;
		}
		else
			slot = overlay_backing_count++;
		overlay_backing[slot].key = key;
		overlay_backing[slot].count = triangle_count;
		overlay_backing[slot].flags = flags;
	}
	if (!PSP_ME_WORK())
	{
		overlay_cached_key = key;
		overlay_cached_count = triangle_count;
		overlay_cached_flags = flags;
	}
	if (!flags[triangle] && PSP_ME_WORK())
		return overlay_want(stream, indices, triangle_count, triangle);
	if (!flags[triangle])
	{
		const struct ge_vertex *a = &stream[indices[3 * triangle]], *b = &stream[indices[3 * triangle + 1]],
			*c = &stream[indices[3 * triangle + 2]];
		float e1[3] = { b->x - a->x, b->y - a->y, b->z - a->z }, e2[3] = { c->x - a->x, c->y - a->y, c->z - a->z };
		float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
		float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		unsigned int start = ge_clock();

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
				static PSP_MAIN_SIDE int walldump = -1;

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
		overlay_ask_us += ge_clock() - start;
	}
	return flags[triangle] == 1;
}

/* build 112: a triangle's wall answer: the one its record carried while
the Media Engine's work carries the call out (overlay_answers), else asked
here */
static int overlay_answer(const struct ge_vertex *stream, const unsigned short *indices, long triangle_count,
	long triangle)
{
	if (overlay_answers)
		return overlay_answers[triangle];
	/* (the ME's work without answers touches none of the answers' tables,
	which the main CPU fills as calls are written down: it only wants) */
	if (PSP_ME_WORK())
		return overlay_want(stream, indices, triangle_count, triangle);
	return overlay_on_a_wall(stream, indices, triangle_count, triangle);
}

/* every triangle's answer of a see-through level list, worked out on the
main CPU as the call is written down (ME semantics): 1 if out[] holds them,
0 if the level copy isn't there to ask from (the carrying out then asks, or
wants, as before) */
static int overlay_answers_for(const void *vertices, int vertex_count, const unsigned short *indices,
	long triangle_count, unsigned char *out)
{
	/* an existing copy only: building one here would change the copies'
	table and arena while the ME may be reading them (copies are built at
	the frame's end) */
	struct level_stream *stream = level_stream_find(vertices);
	long t;

	if (!stream)
		return 0;
	for (t = 0; t < triangle_count; t++)
		out[t] = (unsigned char)overlay_on_a_wall(stream->base, indices, triangle_count, t);
	return 1;
}

/* build 87: a baked stencil's layers: its triangles on the pieces of the wall
triangles drawn this frame, cut at the near plane and guard band as those
pieces were (the same positions, the same cuts), queued as see-through draws
that the flush tests with depth EQUAL */
static void bake_queue_layers(unsigned int stencil_key, unsigned int key, float u_scale, float v_scale, float u_offset,
	float v_offset, short blend, const float *centroid)
{
	int k;

	for (k = 0; k < bake.layer_count; k++)
	{
		struct bake_layer *l = &bake.layers[k];
		struct transparent_draw *draw;
		unsigned short *kept;
		long t, kept_count = 0;
		int cut_count = 0;

		if (l->stencil_key != stencil_key || !l->wall || !l->wall->drawn_frame)
			continue;
		kept = (unsigned short *)list_take((int)(l->tri_count * 3 * sizeof(unsigned short) + 3) & ~3);
		if (!kept)
			return;
		for (t = 0; t < (long)l->tri_count; t++)
		{
			const unsigned short *tri = l->tris + 4 * t;
			int bits[3], corner;

			if (tri[3] >= l->wall->affected_count || l->wall->drawn_frame[tri[3]] != stream_frame)
				continue;
			for (corner = 0; corner < 3; corner++)
			{
				float point[3];

				to_camera(&l->vertices[tri[corner]].x, point);
				bits[corner] = outside(point);
			}
			if (!(bits[0] | bits[1] | bits[2]))
			{
				kept[3 * kept_count] = tri[0];
				kept[3 * kept_count + 1] = tri[1];
				kept[3 * kept_count + 2] = tri[2];
				kept_count++;
			}
			else if (!(bits[0] & bits[1] & bits[2]))
			{
				float corners[3][CORNER_FLOATS] = { { 0.0f } };

				for (corner = 0; corner < 3; corner++)
				{
					const struct ge_vertex *v = &l->vertices[tri[corner]];

					to_camera_corner(&v->x, corners[corner]);
					corners[corner][3] = v->u;
					corners[corner][4] = v->v;
				}
				cut_count += clip_triangle(corners, bits[0] | bits[1] | bits[2], cut_corners + cut_count,
					MAXIMUM_CUT_CORNERS - cut_count);
			}
		}
		if (kept_count && (draw = transparent_slot(key, u_scale, v_scale, u_offset, v_offset, blend, centroid)) != NULL)
		{
			draw->vertices = l->vertices;
			draw->indices = kept;
			draw->index_count = (int)kept_count * 3;
			draw->level = 3;
			bake_layer_draws++;
			bake_layer_triangles += kept_count;
		}
		if (cut_count && (draw = transparent_slot(key, u_scale, v_scale, u_offset, v_offset, blend, centroid)) != NULL)
		{
			struct ge_vertex *out = list_take(cut_count * sizeof(struct ge_vertex));
			int index;

			if (!out)
			{
				/* build 89: the slot given back (it held a previous
				frame's pointers, drawn as garbage at the flush; review) */
				transparent_count--;
				return;
			}
			for (index = 0; index < cut_count; index++)
			{
				const float *c = cut_corners[index];

				out[index].u = c[3];
				out[index].v = c[4];
				out[index].x = c[7];
				out[index].y = c[8];
				out[index].z = c[9];
			}
			draw->vertices = out;
			draw->indices = NULL;
			draw->index_count = cut_count;
			draw->level = 4;
			bake_layer_draws++;
		}
	}
}

static int bake_stencil_has(const struct bake_stencil *st, const unsigned short *tri)
{
	unsigned int slot = bake_triple_hash(tri[0], tri[1], tri[2]) & st->hash_mask;

	while (st->hash[slot])
	{
		const unsigned short *b = st->triples + 3 * (st->hash[slot] - 1);

		if (b[0] == tri[0] && b[1] == tri[1] && b[2] == tri[2])
			return 1;
		slot = (slot + 1) & st->hash_mask;
	}
	return 0;
}

/* build 116: a level see-through surface that fades by the angle it is seen
at (the volume lights' cones and shafts: c40's pale discs, drawn flat at
full strength until now). The level's GE copies have no normals, so each
triangle's own plane stands in: its corners take the fade from the angle
between that plane and the line to the eye, in a coloured copy of the
triangles in view (and the pieces of those cut at the screen's edges),
drawn as the cut-up corners are (level 2). The engine side leaves the fade
mode out under "seethroughflat", and this path with it */
/* at most this many triangles a surface take the coloured copy (36 KB of
the display list); a bigger one draws unfaded, as before (review: 2,048
was 150 KB a surface; c40's light volumes are 16 triangles each) */
#define LEVEL_ANGLE_TRIANGLES 512
static unsigned long level_angle_draws = 0;

static void level_angle_colors(struct sprite_vertex *t, int mode, float fade, int blend, unsigned int tint)
{
	float e1[3] = { t[1].x - t[0].x, t[1].y - t[0].y, t[1].z - t[0].z };
	float e2[3] = { t[2].x - t[0].x, t[2].y - t[0].y, t[2].z - t[0].z };
	float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
	float nn = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
	int k;

	/* the models' rule (seethrough_vertex_colors): against the camera's
	forward direction, mode 1 vanishing face on, mode 2 edge on */
	float c = nn > 0.0f ? fabsf(n[0] * camera_forward[0] + n[1] * camera_forward[1] + n[2] * camera_forward[2]) /
		sqrtf(nn) : 1.0f;
	float a = mode == 1 ? 1.0f - c : c;
	unsigned int color = seethrough_tinted(seethrough_color((int)(fade * a * 255.0f + 0.5f), blend), tint);

	for (k = 0; k < 3; k++)
		t[k].color = color;
}

/* 1: queued (or nothing in view); 0: left to the usual path (no room). The
caller has reset vertex_codes over the triangles' range */
static int level_transparent_angle(const struct level_stream *stream, const unsigned short *indices,
	long triangle_count, unsigned int key, float u_scale, float v_scale, float u_offset, float v_offset, short blend,
	const float *centroid)
{
	struct transparent_draw *draw;
	struct sprite_vertex *out;
	long index, kept_count = 0;
	int cut_count = 0, units, on_wall = 0, free_push = 0, mode = PSP_SEETHROUGH_MODE(blend), total, at = 0, corner;
	float fade = PSP_SEETHROUGH_FADE(blend);

	/* which triangles are in view, the pushes they ask, the cut pieces */
	for (index = 0; index < triangle_count; index++)
	{
		const unsigned short *triangle = indices + 3 * index;
		int bits[3];
		float p[3][CORNER_FLOATS] = { { 0.0f } };

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
		if (bits[0] & bits[1] & bits[2])
			continue;
		for (corner = 0; corner < 3; corner++)
		{
			const struct ge_vertex *b = &stream->base[triangle[corner]];

			to_camera_corner(&b->x, p[corner]);
			p[corner][3] = b->u;
			p[corner][4] = b->v;
		}
		units = overlay_units(p[0], p[1], p[2], overlay_answer(stream->base, indices, triangle_count, index));
		if (units > on_wall)
			on_wall = units;
		if (units < 0 && (overlay_free_far > 0 ? !free_push || -units < free_push : -units > free_push))
			free_push = -units;
		if (!(bits[0] | bits[1] | bits[2]))
			kept_count++;
		else
			cut_count += clip_triangle(p, bits[0] | bits[1] | bits[2], cut_corners + cut_count,
				MAXIMUM_CUT_CORNERS - cut_count);
	}
	total = (int)kept_count * 3 + cut_count;
	if (!total)
		return 1;
	out = list_take(total * (int)sizeof(struct sprite_vertex));
	if (!out)
		return 0;
	if ((draw = transparent_slot(key, u_scale, v_scale, u_offset, v_offset, blend, centroid)) == NULL)
		return 1;
	/* the triangles wholly in view, then the cut pieces (world positions) */
	for (index = 0; index < triangle_count && kept_count; index++)
	{
		const unsigned short *triangle = indices + 3 * index;

		if (vertex_codes[triangle[0]] | vertex_codes[triangle[1]] | vertex_codes[triangle[2]])
			continue;
		for (corner = 0; corner < 3; corner++)
		{
			const struct ge_vertex *b = &stream->base[triangle[corner]];

			out[at].u = b->u;
			out[at].v = b->v;
			out[at].x = b->x;
			out[at].y = b->y;
			out[at].z = b->z;
			at++;
		}
	}
	for (index = 0; index < cut_count; index++)
	{
		const float *c = cut_corners[index];

		out[at].u = c[3];
		out[at].v = c[4];
		out[at].x = c[7];
		out[at].y = c[8];
		out[at].z = c[9];
		at++;
	}
	for (index = 0; index + 2 < at; index += 3)
		level_angle_colors(out + index, mode, fade, draw->blend, draw->tint);
	draw->vertices = out;
	draw->indices = NULL;
	draw->index_count = at - at % 3;
	draw->level = 2;
	draw->colored = 1;
	draw->depth_units = (short)(on_wall > 0 ? on_wall : -free_push);
	level_transparent_draws++;
	level_angle_draws++;
	seethrough_angle_draws++;
	return 1;
}

static void psp_ge_queue_level_transparent_body(const void *vertices, int vertex_count, const unsigned short *indices,
	long triangle_count, unsigned int key, float u_scale, float v_scale, float u_offset, float v_offset, short blend,
	const float *centroid)
{
	const struct bake_stencil *baked = NULL;
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
	/* build 87: a baked stencil is drawn on its walls' pieces; the triangles
	the bake didn't take keep the old path below */
	baked = bake_stencil_of((unsigned int)stream_key(vertices));
	if (baked)
		bake_queue_layers(baked->key, key, u_scale, v_scale, u_offset, v_offset, blend, centroid);
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
	/* build 116: a surface that fades by the angle it is seen at (above) */
	if (PSP_SEETHROUGH_MODE(blend) != 0 && seethrough_fades(PSP_SEETHROUGH_BLEND(blend)) && !baked &&
		triangle_count <= LEVEL_ANGLE_TRIANGLES &&
		level_transparent_angle(stream, indices, triangle_count, key, u_scale, v_scale, u_offset, v_offset, blend,
			centroid))
		return;
	kept = (unsigned short *)list_take((int)(triangle_count * 3 * sizeof(unsigned short) + 3) & ~3);
	if (!kept)
		return;
	for (index = 0; index < triangle_count; index++)
	{
		const unsigned short *triangle = indices + 3 * index;
		int bits[3], corner;

		if (baked && bake_stencil_has(baked, triangle))
			continue;
		if (baked)
			bake_old_path_triangles++;
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
				units = overlay_units(p[0], p[1], p[2], overlay_answer(stream->base, indices, triangle_count, index));
				overlay_triangle_bucket[kept_count] = overlay_bucket_of(units);
				/* build 115: overlayfreefar (the default from build 116), the free bucket at its smallest push */
				if (units < 0 && overlay_free_far > 0)
				{
					if (!bucket_units[0] || -units < bucket_units[0])
						bucket_units[0] = (unsigned short)-units;
				}
				else if (abs(units) > bucket_units[overlay_triangle_bucket[kept_count]])
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
				overlay_answer(stream->base, indices, triangle_count, index));
			if (units > cut_units)
				cut_units = units;
			if (units < 0 && (overlay_free_far > 0 ? !cut_units_free || -units < cut_units_free : -units > cut_units_free))
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
		{
			/* build 89: the slot given back (review) */
			transparent_count--;
			return;
		}
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
	GE_DEFER(defer_queue_level_transparent(vertices, vertex_count, indices, triangle_count, key, u_scale, v_scale,
		u_offset, v_offset, blend, centroid)); /* build 112: gedefer */
	GE_BENCH_SKIP();
	ge_kind(KIND_LEVEL_SEETHROUGH); /* build 103 */
	unsigned int start = ge_clock();

	psp_ge_queue_level_transparent_body(vertices, vertex_count, indices, triangle_count, key, u_scale, v_scale, u_offset, v_offset, blend, centroid);
	seethrough_next_tint = 0xFFFFFFFF; /* build 116: this call's tint is used */
	seethrough_next_shader = 0;
	seethrough_next_doubled = 0;
	ge_entry_us[3] += ge_clock() - start;
}

#define LEVEL_OVERLAY_OFFSET 24
static int overlay_scaled = -1;
static int overlays_off = -1;
extern volatile int psp_screenshot_requested;
static int tdump = -1;
static long tskip = -1; /* "tskipN" (transparent_flush_body) */

static void transparent_flush_body(void)
{
	int saved_limit = list_limit;

	int index, other;

	if (!transparent_count)
		return;
	if (list_limit < list_bytes - HUD_RESERVE)
		list_limit = list_bytes - HUD_RESERVE;
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

		/* build 89: a slot whose filling never finished (see transparent_slot) */
		if (!draw->vertices && !draw->cut_vertices)
			continue;
		/* build 63: "tdump": each see-through draw of the frame a screenshot
		is asked for, with the state it goes out under */
		if (tdump < 0)
			tdump = psp_keep("tdump");
		if (tdump && psp_screenshot_requested)
		{
			extern char const *psp_bitmap_tag_name(void const *bitmap); /* hud_draw.c (psp_fixups.py) */
			unsigned int state_depth = sceGuGetStatus(GU_DEPTH_TEST);
			/* and the map's bitmap tag (after b116: a10's bridge window); a
			walk over the tags, so not on the Media Engine (gemeon) */
			char const *name = PSP_ME_WORK() ? NULL : psp_bitmap_tag_name((void const *)draw->key);
			extern char const *psp_shader_tag_name(void const *shader); /* hud_draw.c (psp_fixups.py) */
			char const *shader_name = PSP_ME_WORK() || !draw->shader ? NULL : psp_shader_tag_name((void const *)draw->shader);

			if (shader_name)
				platform_log("tdump: %d/%d shader %s", index, transparent_count, shader_name);
			platform_log("tdump: %d/%d key %08x (%s) sky %d level %d fp %d sprites %d blend %d dist %.1f count %d cut %d units %d at %.2f %.2f %.2f depth test %u mfp %d",
				index, transparent_count, draw->key, name ? name : "no tag", draw->sky, draw->level, draw->first_person,
				draw->sprites, draw->blend, sqrtf(draw->distance), draw->index_count, draw->cut_count, draw->depth_units,
				draw->matrix.w.x, draw->matrix.w.y, draw->matrix.w.z, state_depth, model_first_person);
		}
		/* "tskipN": the see-through draws whose map is the bitmap_data at
		address N (decimal; tdump prints it in hex) left out, to tell which
		draw a thing on screen is (after b116: c40's pale discs). The tags
		sit at the same addresses in every build, for the same map */
		if (tskip < 0)
		{
			extern int psp_keep_number(const char *prefix, int fallback);

			tskip = (long)psp_keep_number("tskip", 0);
		}
		if (tskip > 0 && draw->key == (unsigned int)tskip)
			continue;
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
		if (draw->clut)
		{
			/* build 116: a meter, with its own palette (meter_palette) in
			place of the map's, its colours drawn as they are */
			unsigned int tint = draw->tint, rgb = ((tint & 0xFF) << 16) | (tint & 0xFF00) | ((tint >> 16) & 0xFF);

			sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
			sceGuClutLoad(draw->clut_blocks, draw->clut);
			sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
			ge_blend(1);
			if (draw->blend == BLEND_METER_TINT)
				sceGuBlendFunc(GU_ADD, GU_FIX, GU_SRC_ALPHA, rgb, 0);
			else
			{
				unsigned int brightness = tint >> 24;

				sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, (brightness << 16) | (brightness << 8) | brightness, rgb);
				sceGuAlphaFunc(GU_GREATER, 0, 0xFF);
				sceGuEnable(GU_ALPHA_TEST);
			}
		}
		else
			hud_blend(draw->blend);
		if (draw->first_person)
			use_projection(1);
		/* build 35: the level's see-through surfaces (the base pillars'
		painted symbols) lie on solid ones; the Xbox's 24-bit depth sorts
		that out, the PSP's 16 bits flickered between them by the view.
		Nudged nearer, as decals are */
		if (draw->level >= 3)
		{
			/* build 87: a baked stencil on its wall's own pieces: the same
			depth exactly (mode 1: two steps of slack, in case; and always
			with levelcompact, whose walls go through the GE as 16-bit
			vertices and another matrix, the stencil's as floats) */
			if (stencil_mode == 1 || level_compact > 0)
				sceGuDepthOffset(STENCIL_SLACK);
			else
				sceGuDepthFunc(GU_EQUAL);
		}
		else if (draw->level)
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
		if (draw->doubled)
		{
			/* build 116: a shader brighter than its map, its tint halved */
			sceGuEnable(GU_FRAGMENT_2X);
			seethrough_doubled_draws++;
		}
		/* build 116: the shader's fade as one colour (sprites and coloured
		draws carry their own) */
		if (draw->color != 0xFFFFFFFF && !draw->sprites && !draw->colored)
			sceGuColor(draw->color);
		if (draw->sprites)
			/* (build 113: a contrail is a strip; sprites and widgets lists) */
			sceGumDrawArray(draw->strip ? GU_TRIANGLE_STRIP : GU_TRIANGLES,
				GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, draw->index_count, NULL,
				draw->vertices);
		else if (draw->level == 1 || draw->level == 3)
			sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
				draw->index_count, draw->indices, draw->vertices);
		else if (draw->level == 2 || draw->level == 4)
			/* (build 116: a level surface faded by angle carries a colour a corner) */
			sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | (draw->colored ? GU_COLOR_8888 : 0) | GU_VERTEX_32BITF |
				GU_TRANSFORM_3D, draw->index_count, NULL, draw->vertices);
		else
		{
			/* build 116: a fade per vertex replaces the normal with a colour */
			int middle = draw->colored ? GU_COLOR_8888 : GU_NORMAL_8BIT;

			if (draw->index_count > 0)
				sceGumDrawArray(draw->strip ? GU_TRIANGLE_STRIP : GU_TRIANGLES,
					GU_TEXTURE_32BITF | middle | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
					draw->index_count, draw->indices, draw->vertices);
			if (draw->cut_count > 0 && draw->cut_vertices)
				sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | middle | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
					draw->cut_count, NULL, draw->cut_vertices);
		}
		if (draw->color != 0xFFFFFFFF && !draw->sprites && !draw->colored)
			sceGuColor(0xFFFFFFFF);
		if (draw->clut)
		{
			sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
			sceGuDisable(GU_ALPHA_TEST);
		}
		if (draw->doubled)
			sceGuDisable(GU_FRAGMENT_2X);
		if (draw->level)
			sceGuDepthOffset(0);
		if (draw->level >= 3)
			sceGuDepthFunc(GU_GEQUAL);
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
	ge_blend(0);
	sceGuDisable(GU_TEXTURE_2D);
	sceGuEnable(GU_FOG);
	sceGuDepthMask(GU_FALSE);
	transparent_count = 0;
	list_limit = saved_limit;
}

static void transparent_flush(void)
{
	unsigned int start = ge_clock();
	int kind = ge_kind_now;

	ge_kind(KIND_SEETHROUGH_FLUSH); /* build 103 */
	transparent_flush_body();
	ge_kind(kind);
	ge_entry_us[8] += ge_clock() - start;
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

/* the game's rasterizer_meter_parameters (colours ARGB): build 116 keeps
the layout in psp_seethrough.h (struct psp_seethrough_meter), shared with the
models' meter shaders; its seven words are what the defer queue copies */
typedef char meter_words_check[sizeof(struct psp_seethrough_meter) == METER_WORDS * 4 ? 1 : -1];

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
	GE_DEFER(defer_words(DEFER_HUD_ZOOM, 1, defer_bits(zoom), 0, 0, 0, 0, 0, 0, 0)); /* build 112: gedefer */
	hud_zoom = zoom > 0.0f ? zoom : 1.0f;
}

static int ui_centered = 0;

void psp_ge_ui_centered(int centered)
{
	GE_DEFER(defer_words(DEFER_UI_CENTERED, 1, (unsigned int)centered, 0, 0, 0, 0, 0, 0, 0)); /* build 112: gedefer */
	ui_centered = centered;
}

void psp_ge_ui_space(int width, int height)
{
	GE_DEFER(defer_words(DEFER_UI_SPACE, 2, (unsigned int)width, (unsigned int)height, 0, 0, 0, 0, 0, 0)); /* build 112: gedefer */
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
	ge_blend(1);
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
	ge_kind(KIND_HUD); /* build 103 */
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
/* build 116: ten slots (six before), as the models' meter shaders share
them: a plasma rifle's gauge, the Warthogs' meters in view (4 KB more) */
#define METER_CACHE 10
static unsigned int meter_cache_palette[METER_CACHE][256] __attribute__((aligned(64)));
static struct
{
	unsigned int key;
	struct psp_seethrough_meter meter;
	unsigned long frame;
	int count;
} meter_cache[METER_CACHE];
static unsigned long meter_reused = 0, meter_worked = 0;

static unsigned long hud_quad_us = 0, text_us = 0;
static void screen_quad(void const *vertices, unsigned int key, short blend, float red, float green, float blue,
	float fade, float offset_x, float offset_y, void const *meter_parameters, int wrapped, float u_scale, float v_scale);

/* "hdump": each HUD quad of a frame a screenshot is asked for, with its
bitmap's tag, its corners in the game's 640x480 screen and its colour, before
the defer queue (the tag name is looked up on this thread). For a HUD element
drawn where the Xbox doesn't draw it (a50's white bars, b116) */
static void hud_quad_dump(void const *vertices, unsigned int key, short blend, float red, float green, float blue,
	float fade, void const *meter_parameters, int wrapped)
{
	extern char const *psp_bitmap_tag_name(void const *bitmap); /* hud_draw.c (psp_fixups.py) */
	static PSP_MAIN_SIDE int hdump = -1; /* build 118: set as the calls are written down */
	struct game_screen_vertex const *in = vertices;
	const unsigned char *bitmap = (const unsigned char *)key;
	float x0, y0, x1, y1;
	char const *name;
	int corner;

	if (hdump < 0)
		hdump = psp_keep("hdump");
	/* not again when the defer queue replays the quad (gedefer), which may
	run on the Media Engine (gemeon) */
	if (!hdump || defer_replaying || psp_on_me() || !psp_screenshot_requested || !vertices || !key)
		return;
	x0 = x1 = in[0].x;
	y0 = y1 = in[0].y;
	for (corner = 1; corner < 4; corner++)
	{
		x0 = fminf(x0, in[corner].x);
		x1 = fmaxf(x1, in[corner].x);
		y0 = fminf(y0, in[corner].y);
		y1 = fmaxf(y1, in[corner].y);
	}
	name = psp_bitmap_tag_name(bitmap);
	platform_log("hdump: %s (bitmap %dx%d) at %.0f,%.0f to %.0f,%.0f, uv %.3f,%.3f to %.3f,%.3f; colour %.2f %.2f %.2f "
		"fade %.2f argb %08x; blend %d%s%s",
		name ? name : "(no tag)", *(const short *)(bitmap + 4), *(const short *)(bitmap + 6), x0, y0, x1, y1,
		in[0].u, in[0].v, in[2].u, in[2].v, red, green, blue, fade, in[0].argb, blend, meter_parameters ? ", meter" : "",
		wrapped ? ", wrapped" : "");
}

/* the HUD's quads, timed for the log (build 31) */
void psp_ge_screen_quad(void const *vertices, unsigned int key, short blend, float red, float green, float blue,
	float fade, float offset_x, float offset_y, void const *meter_parameters, int wrapped, float u_scale, float v_scale)
{
	hud_quad_dump(vertices, key, blend, red, green, blue, fade, meter_parameters, wrapped);
	GE_DEFER(defer_screen_quad(vertices, key, blend, red, green, blue, fade, offset_x, offset_y, meter_parameters,
		wrapped, u_scale, v_scale)); /* build 112: gedefer */
	GE_BENCH_SKIP();
	ge_kind(KIND_SCREEN); /* build 103 */
	unsigned int start = ge_clock();

	screen_quad(vertices, key, blend, red, green, blue, fade, offset_x, offset_y, meter_parameters, wrapped, u_scale,
		v_scale);
	hud_quad_us += ge_clock() - start;
}

static unsigned long screen_quad_bailed = 0;

/* build 113: a zoomed weapon's mask (b113_candidates.md #20; keep word
"zoommask"). The HUD's screen effect hands its full-screen bitmap to
rasterizer_screen_effect (rasterizer.c, psp_fixups.py). What the Xbox does
with it (rasterizer_xbox_screen_effect.c), by the pixel shaders [D]:
  - with no blur, its last pass darkens the frame by the mask's alpha:
    frame x (1 - alpha);
  - with a blur (the pistol's and the sniper rifle's zoom: "warp"), the mask
    says where the blur goes: the last combiner mixes the sharp frame and
    three offset copies by the mask's alpha, so the middle stays sharp and
    the edges blur.
The PSP has no cheap blur, so with one this darkens by a third of the
mask's alpha instead, a stand-in for the soft edge (strength 85 of 255);
without one, the whole alpha, as the Xbox. One quad over the 3D view, before
the HUD. The mask is drawn for 4:3 (640x480): its square is kept round in
the middle of the 480x272 screen, and the sides take its edge texels
(clamped). The emulator's first run (38088924310) darkened the pistol's
zoom by the whole alpha: a clear circle about half the screen's height in a
black surround, which the Xbox's zoom doesn't show [U: as the game is
remembered; the shaders say the mask weighs the blur] */
static unsigned long zoom_masks = 0, zoom_masks_missing = 0;

void psp_ge_zoom_mask(unsigned int key, unsigned int strength)
{
	GE_DEFER(defer_words(DEFER_ZOOM_MASK, 2, key, strength, 0, 0, 0, 0, 0, 0));
	GE_BENCH_SKIP();
	ge_kind(KIND_SCREEN);
	struct hud_vertex *out;
	int width, height;
	/* the 4:3 square's width on the screen, and the texture's span across
	the whole screen that makes it so */
	float span = (float)SCREEN_WIDTH / (SCREEN_HEIGHT * 4.0f / 3.0f);

	if (!frame_open || !key)
		return;
	list_hud_phase();
	transparent_flush();
	if (list_full())
		return;
	if (!psp_tex_size(key, &width, &height) || !psp_tex_bind(key, NULL))
	{
		zoom_masks_missing++;
		return;
	}
	out = list_take(2 * sizeof(struct hud_vertex));
	if (!out)
		return;
	out[0].u = (0.5f - span * 0.5f) * width;
	out[0].v = 0.0f;
	out[1].u = (0.5f + span * 0.5f) * width;
	out[1].v = (float)height;
	out[0].color = out[1].color = ((strength > 255 ? 255 : strength) << 24) | 0x00FFFFFF;
	out[0].x = 0.0f; out[0].y = 0.0f; out[0].z = 0.0f;
	out[1].x = (float)SCREEN_WIDTH; out[1].y = (float)SCREEN_HEIGHT; out[1].z = 0.0f;
	sceGuDisable(GU_DEPTH_TEST);
	sceGuDisable(GU_FOG);
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexWrap(GU_CLAMP, GU_CLAMP);
	sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
	sceGuTexScale(1.0f, 1.0f);
	sceGuTexOffset(0.0f, 0.0f);
	ge_blend(1);
	sceGuBlendFunc(GU_ADD, GU_FIX, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
	sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, out);
	ge_blend(0);
	sceGuDisable(GU_TEXTURE_2D);
	sceGuEnable(GU_FOG);
	sceGuEnable(GU_DEPTH_TEST);
	zoom_masks++;
}

/* a meter's palette (build 23; a routine of its own from build 116, for the
models' meter shaders as well as the HUD's meters). The Xbox works a meter
out per pixel in four register-combiner stages; with a palette texture the
same sum can be done once per palette entry instead. The texture's grey (its
blue) is the position along the meter and its alpha the shape:
  shape 0                    nothing drawn (the Xbox's alpha kill)
  position past the flash    the empty colour
  otherwise                  min to max colour along the meter,
                             plus the flash colour past the fill
each multiplied by the shape; the frame then gets
colour x tint + frame x alpha (alpha: the fade, or the empty colour's).
NULL when the map has no 32-bit palette or the display list is full;
*count_out: the palette's entries */
static unsigned int *meter_palette(unsigned int key, struct psp_seethrough_meter const *meter, int *count_out)
{
	const unsigned int *palette;
	unsigned int *computed = NULL, fill = meter->gradient_min_color >> 24, flash_end = meter->flash_color >> 24;
	int count, entry, slot, free_slot = -1, hit = 0;

	if (!psp_tex_palette(key, &palette, &count))
		return NULL;
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
				return NULL;
			computed = (unsigned int *)(((unsigned int)computed + 15) & ~15u);
		}
		meter_worked++;
	}
	for (entry = hit ? count : 0; entry < count; entry++)
	{
		unsigned int texel = palette[entry], shape = texel >> 24, position = (texel >> 16) & 0xFF;
		int channel, out[3], alpha_out;

		/* build 116: outside tint mode 2 (the models' meters only; the HUD's
		are all in it) the blend takes no alpha, so the alpha is the alpha
		test's: 0 where the Xbox kills the pixel, otherwise all */
		if (!shape)
		{
			computed[entry] = meter->tint_mode_2 ? 0xFF000000 : 0;
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
			/* build 116: the colour's run along the meter, by its gradient
			(stage 0's alpha: the position over the gradient, at least an
			eighth; the HUD's gradient is always 1, the position itself) */
			int along = (int)position;

			if (meter->gradient != 1.0f)
			{
				float gradient = meter->gradient > 0.125f ? meter->gradient : 0.125f;

				along = (int)((float)position / gradient + 0.5f);
				along = along > 255 ? 255 : along;
			}
			if (past < 0)
				past = 0;
			for (channel = 0; channel < 3; channel++)
			{
				int low = (meter->gradient_min_color >> (16 - 8 * channel)) & 0xFF;
				int high = (meter->gradient_max_color >> (16 - 8 * channel)) & 0xFF;
				int flash = (meter->flash_color >> (16 - 8 * channel)) & 0xFF;

				out[channel] = low + (high - low) * along / 255 +
					(meter->flash_color_is_negative ? -1 : 1) * flash * past / 255;
				out[channel] = PSP_CLAMP_255(out[channel]);
			}
			alpha_out = meter->tint_color >> 24;
		}
		if (!meter->tint_mode_2)
			alpha_out = 255;
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
		ge_writeback(computed, count * 4);
	*count_out = count;
	return computed;
}

static void screen_quad(void const *vertices, unsigned int key, short blend, float red, float green, float blue,
	float fade, float offset_x, float offset_y, void const *meter_parameters, int wrapped, float u_scale, float v_scale)
{
	list_hud_phase();
	struct game_screen_vertex const *in = vertices;
	struct psp_seethrough_meter const *meter = meter_parameters;
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
		/* the meter's own palette (meter_palette, above), drawn as the
		Xbox blends it: colour x tint + frame x alpha */
		unsigned int *computed;
		int count = 0;

		/* build 63: the depth test and fog back on the way out, as at the
		end; returned with them off, everything after in the frame drew
		through walls until the next HUD quad switched them back */
		computed = meter_palette(key, meter, &count);
		if (!computed)
		{
			screen_quad_bailed++;
			sceGuEnable(GU_FOG);
			sceGuEnable(GU_DEPTH_TEST);
			return;
		}
		sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
		sceGuClutLoad(count / 8, computed);
		sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
		ge_blend(1);
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
	ge_blend(0);
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
	GE_DEFER(defer_shadow_begin(matrix, color, radius)); /* build 112: gedefer */
	GE_BENCH_SKIP();
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
		ge_writeback(blob_texture, sizeof(blob_texture));
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
	/* build 112: gedefer. Waiting calls run first so the answer is the one
	the frame so far gives; under ME semantics they needn't: copies are built
	only at the frame's end there, so this reads what is built already */
	if (!defer_me_semantics)
		GE_DEFER_DRAIN();
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

/* build 89: a vertex's packed normal and lightmap incident radiosity (the
map's 32-bit forms) for object lighting, which in a map built with its
geometry outside the tags read them at the Xbox's 32-byte stride from the
6-byte position copy: garbage, and reads past the copy (review). 1 if the
copies are there; radiosity 0 without a lightmap */
int psp_ge_level_vertex_lighting(unsigned int key, int vertex_index, unsigned int *normal, unsigned int *radiosity)
{
	/* build 112: gedefer. Waiting calls run first so the answer is the one
	the frame so far gives; under ME semantics they needn't: copies are built
	only at the frame's end there, so this reads what is built already */
	if (!defer_me_semantics)
		GE_DEFER_DRAIN();
	unsigned int slot = (key >> 5) % LEVEL_STREAMS, tries = 0;
	const struct level_stream *stream;

	while (level_streams[slot].source && (unsigned int)level_streams[slot].source != key && tries++ < LEVEL_STREAMS)
		slot = (slot + 1) % LEVEL_STREAMS;
	stream = &level_streams[slot];
	if ((unsigned int)stream->source != key || !stream->base || !stream->normals || vertex_index < 0 ||
		vertex_index >= stream->count)
		return 0;
	*normal = stream->normals[vertex_index];
	*radiosity = stream->radiosity ? stream->radiosity[vertex_index] : 0;
	return 1;
}

/* build 68: with a geometry pack open, the copies over their budget: the
least recently drawn go, never one drawn this frame. Open addressing can't
drop a slot without breaking the chains through it, so a dropped slot keeps
its key with no copies, and the next draw of it rebuilds in place */
static void level_stream_let_go(struct level_stream *stream)
{
	stream_bytes -= stream_size(stream);
	stream_free(stream->base);
	stream_free(stream->lightmap);
	copy_free(stream->normals);
	copy_free(stream->radiosity);
	/* build 89: the 16-bit copies too (leaked on every let-go and rebuild
	before; review) */
	copy_free(stream->compact_base);
	copy_free(stream->compact_lightmap);
	stream->base = stream->lightmap = NULL;
	stream->normals = stream->radiosity = NULL;
	stream->compact_base = stream->compact_lightmap = NULL;
	streams_let_go++;
}

/* build 111: the least recently drawn copy let go, never one drawn this
frame; 0 when none is left to let go. The trim's choice, now shared with the
copies' arena (copy_alloc) */
static int level_stream_let_go_oldest(void)
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
		return 0;
	level_stream_let_go(&level_streams[oldest]);
	return 1;
}

static void level_streams_trim(void)
{
	while (stream_bytes > LEVEL_STREAM_BUDGET && level_stream_let_go_oldest())
		;
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
	/* build 87: the pieces the level drew, where the stencil bake cut */
	if (stream->bake)
	{
		const unsigned short *swapped = bake_substitute(stream, indices, &triangle_count, 0);

		if (swapped != indices)
		{
			indices = swapped;
			vertex_count = stream->count;
		}
	}
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
	ge_blend(1);
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
	ge_blend(0);
	sceGuDepthMask(GU_FALSE);
	sceGuDepthFunc(GU_GEQUAL);
	if (shadow_mode == 2)
		sceGuDepthOffset(0);
	sceGuDisable(GU_TEXTURE_2D);
	shadows_drawn++;
}

void psp_ge_shadow_draw(const void *vertices, int vertex_count, const unsigned short *indices, long triangle_count)
{
	GE_DEFER(defer_words(DEFER_SHADOW_DRAW, 4, DEFER_P(vertices), (unsigned int)vertex_count, DEFER_P(indices),
		(unsigned int)triangle_count, 0, 0, 0, 0)); /* build 112: gedefer */
	GE_BENCH_SKIP();
	ge_kind(KIND_SHADOW); /* build 103 */
	unsigned int start = ge_clock();

	psp_ge_shadow_draw_body(vertices, vertex_count, indices, triangle_count);
	ge_entry_us[7] += ge_clock() - start;
}

void psp_ge_shadow_end(void)
{
	GE_DEFER(defer_words(DEFER_SHADOW_END, 0, 0, 0, 0, 0, 0, 0, 0, 0)); /* build 112: gedefer */
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
	GE_DEFER(defer_light_begin(position, forward, color, radius, cosine_falloff, cosine_cutoff)); /* build 112: gedefer */
	GE_BENCH_SKIP();
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
/* after b116: the level light pass's vertices for the draw in hand, by the
vertex's low bits: its reach and, once asked for, its place from the camera
(5 KB from bss; a level vertex sits in 3-6 triangles, each of which worked
both out again; an independent review) */
#define LIGHT_MEMO 256
struct light_memo_entry
{
	unsigned int tag; /* the draw's number << 16 | the vertex */
	float reach, camera[3];
	int camera_known;
};
static struct light_memo_entry light_memo[LIGHT_MEMO];
static unsigned int light_memo_draw = 0;
static int light_memo_check = -1; /* keep.txt's "lightmemocheck" */
static unsigned long light_memo_checks = 0, light_memo_mismatches = 0;

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
/* build 89: the texels first and 16-aligned in EVERY slot. The aligned
attribute was on the array only: with the two floats first each slot was
1,032 bytes and slots 0 and 2 had their texels 8 bytes past a 16-byte
boundary, where the GE (which ignores a texture address's low 4 bits) read
the cone from 8 bytes early: the disc shifted a quarter of its width and the
clamped edge no longer black, bright bands across lit walls (code review) */
static struct
{
	unsigned char texels[CONE_SIZE * CONE_SIZE] __attribute__((aligned(16)));
	float falloff, cutoff;
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
		ge_writeback(cone_ramp, sizeof(cone_ramp));
	}
	ge_writeback(cones[slot].texels, sizeof(cones[slot].texels));
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

/* build 89: the light pass's nudge (mode 2), as b85's shadow test: a lift
of 1% of the distance, at most 0.2 units, in depth steps at that distance;
and the distance bands it is drawn in (squared distances 4, 16, 64, 256,
1024: bands 2, 4, 8, 16, 32 units and beyond) */
#define LIGHT_BANDS 6
static int light_nudge_units(float distance)
{
	float lift, units;

	if (distance < projection_near)
		distance = projection_near;
	lift = 0.01f * distance;
	if (lift > 0.2f)
		lift = 0.2f;
	units = lift * 61439.0f * projection_near / (distance * distance);
	return units < 3.0f ? 3 : units > 4096.0f ? 4096 : (int)units;
}

/* build 117 (b113 #13): the light's pieces cut at the near plane, each
corner moved toward the eye along its own line of sight (so it stays on its
pixel) by the lift light_nudge_units gives its own distance. b116 nudged
all of a draw's cut pieces by one depth offset, sized by the corner nearest
the eye; but a piece cut at the near plane reaches from there to 8 units
and more, and a nudge sized for 0.1 units lifts the floor 8 units off by
about 1 unit, past the wall in front of it: the floor's light seen through
the map room bunker's hallway wall (b108-b116 [M]). Measured along the
view, as the depth buffer measures; a corner never moves past the near
plane. Keep word "lightcutone": as before */
static int light_cut_one = -1;

static void light_lift_corner(struct shadow_vertex *corner)
{
	float dx = corner->x - camera_eye[0], dy = corner->y - camera_eye[1], dz = corner->z - camera_eye[2];
	float depth = dx * camera_forward[0] + dy * camera_forward[1] + dz * camera_forward[2], lift, scale;

	if (depth <= projection_near)
		return;
	/* light_nudge_units' depth steps at this depth, in world units */
	lift = (float)light_nudge_units(depth) * depth * depth / (61439.0f * projection_near);
	if (lift > depth - projection_near)
		lift = depth - projection_near;
	/* along the line of sight, the depth shrinks by the same share */
	scale = (depth - lift) / depth;
	corner->x = camera_eye[0] + dx * scale;
	corner->y = camera_eye[1] + dy * scale;
	corner->z = camera_eye[2] + dz * scale;
}

static int light_band(float squared)
{
	return squared < 4.0f ? 0 : squared < 16.0f ? 1 : squared < 64.0f ? 2 : squared < 256.0f ? 3 :
		squared < 1024.0f ? 4 : 5;
}

/* ---------- build 103: lit triangles split near the light

Sheepdog01, b102 (The Library): the flashlight barely showed on floors and lit
walls only from very close. The Xbox measures the light's falloff per pixel
(a 32x32x32 attenuation texture indexed by the pixel's offset from the
light, 0.5/radius: rasterizer_xbox_environment.c); the GE pass measures it
at the triangle's three corners and blends between them, so a floor
triangle several metres across, lit in its middle, had its corners out of
reach or behind the light: dim, or skipped whole. A spotlight's triangle
with an edge longer than a fifth of the light's radius is now split into
four, again and again (four levels at most) where it can reach the light,
and each piece lit at its own corners. Keep word "nolightsplit": as before */
static int light_split_on = -1;
static unsigned long light_split_triangles = 0, light_split_pieces = 0;

static void light_emit_spot(float corners[3][CORNER_FLOATS], struct shadow_vertex *out, long *kept_count, int *cut_count)
{
	/* the projected cone mirrors behind the light, so the part of the
	triangle there is cut off (build 36), as psp_ge_light_draw_body does */
	float polygon[4][CORNER_FLOATS], q[3];
	int count = 0, k, corner;

	for (corner = 0; corner < 3; corner++)
		q[corner] = (corners[corner][7] - light.position[0]) * light.forward[0] +
			(corners[corner][8] - light.position[1]) * light.forward[1] +
			(corners[corner][9] - light.position[2]) * light.forward[2] - 0.05f;
	if (q[0] <= 0.0f && q[1] <= 0.0f && q[2] <= 0.0f)
		return;
	if (q[0] >= 0.0f && q[1] >= 0.0f && q[2] >= 0.0f)
	{
		light_emit(corners, out, kept_count, cut_count);
		return;
	}
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
		float piece[3][CORNER_FLOATS];

		memcpy(piece[0], polygon[0], sizeof(piece[0]));
		memcpy(piece[1], polygon[k], sizeof(piece[0]));
		memcpy(piece[2], polygon[k + 1], sizeof(piece[0]));
		light_emit(piece, out, kept_count, cut_count);
	}
}

static float distance_squared3(const float *a, const float *b)
{
	float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];

	return x * x + y * y + z * z;
}

static float light_split_tangent = 1.0f;

static void light_split(float p[3][3], float uv[3][2], const unsigned int normal[3], int depth,
	struct shadow_vertex *out, long *kept_count, int *cut_count)
{
	float longest = 0.0f, nearest = 1e30f, split;
	int corner;

	if (*kept_count >= SCRATCH_TRIANGLES)
		return;
	/* a piece wholly outside the cone's pyramid lights nothing (the first
	emulator test split 460 triangles a light, most of them outside it) */
	if (depth > 0 && outside_cone(p[0], p[1], p[2], light_split_tangent))
		return;
	for (corner = 0; corner < 3; corner++)
	{
		float edge = distance_squared3(p[corner], p[(corner + 1) % 3]);
		float reach = distance_squared3(p[corner], light.position);

		if (edge > longest)
			longest = edge;
		if (reach < nearest)
			nearest = reach;
	}
	longest = sqrtf(longest);
	nearest = sqrtf(nearest);
	/* no point of the triangle is nearer the light than its nearest corner
	less its longest edge: beyond the radius, nothing of it is lit */
	if (nearest - longest > light.radius)
		return;
	/* finer near the light, where the falloff changes fastest: pieces about
	a fifth of the radius across there, about half their distance further
	out; three levels at most (64 pieces) */
	split = 0.5f * nearest;
	if (split < 0.2f * light.radius)
		split = 0.2f * light.radius;
	if (depth < 3 && longest > split)
	{
		float m[3][3], muv[3][2], piece[3][3], piece_uv[3][2];
		unsigned int piece_normal[3];
		int k;

		for (corner = 0; corner < 3; corner++)
		{
			int next = (corner + 1) % 3;

			for (k = 0; k < 3; k++)
				m[corner][k] = 0.5f * (p[corner][k] + p[next][k]);
			muv[corner][0] = 0.5f * (uv[corner][0] + uv[next][0]);
			muv[corner][1] = 0.5f * (uv[corner][1] + uv[next][1]);
		}
		light_split_pieces += 3;
		/* the three corner pieces and the middle one; a midpoint takes the
		normal of the corner it starts from (level surfaces are flat) */
		for (corner = 0; corner < 3; corner++)
		{
			int previous = (corner + 2) % 3;

			memcpy(piece[0], p[corner], sizeof(piece[0]));
			memcpy(piece[1], m[corner], sizeof(piece[0]));
			memcpy(piece[2], m[previous], sizeof(piece[0]));
			memcpy(piece_uv[0], uv[corner], sizeof(piece_uv[0]));
			memcpy(piece_uv[1], muv[corner], sizeof(piece_uv[0]));
			memcpy(piece_uv[2], muv[previous], sizeof(piece_uv[0]));
			piece_normal[0] = piece_normal[1] = piece_normal[2] = normal[corner];
			light_split(piece, piece_uv, piece_normal, depth + 1, out, kept_count, cut_count);
		}
		piece_normal[0] = normal[0]; piece_normal[1] = normal[1]; piece_normal[2] = normal[2];
		light_split(m, muv, piece_normal, depth + 1, out, kept_count, cut_count);
		return;
	}
	{
		float corners[3][CORNER_FLOATS], reach[3];

		for (corner = 0; corner < 3; corner++)
			reach[corner] = light_reach(p[corner], normal[corner], 0);
		if (reach[0] <= 0.0f && reach[1] <= 0.0f && reach[2] <= 0.0f)
			return;
		memset(corners, 0, sizeof(corners));
		for (corner = 0; corner < 3; corner++)
		{
			to_camera_corner(p[corner], corners[corner]);
			corners[corner][3] = uv[corner][0];
			corners[corner][4] = uv[corner][1];
			corners[corner][5] = reach[corner];
		}
		light_emit_spot(corners, out, kept_count, cut_count);
	}
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
	float reach_sum = 0.0f;
	unsigned int average = 0xFFFFFFFF;
	unsigned char *cone = NULL;
	ScePspFMatrix4 cone_projection;
	float tangent = 0.0f, tint[3] = { 1.0f, 1.0f, 1.0f }, saved[3], near_saved;

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
	/* build 87: the pieces the level drew, where the stencil bake cut */
	if (stream->bake)
	{
		const unsigned short *swapped = bake_substitute(stream, indices, &triangle_count, 0);

		if (swapped != indices)
		{
			indices = swapped;
			vertex_count = stream->count;
		}
	}
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
	/* build 117: the light's own pieces cut a hair past the near plane, so
	a corner on the cut has room for its 1% lift toward the eye
	(light_lift_corner) without crossing the plane; at the plane itself it
	would get none, where a wall beside the player needs the most (an
	independent review). The sliver left out is 1% of the near plane's
	distance deep */
	if (light_cut_one < 0)
		light_cut_one = psp_keep("lightcutone");
	near_saved = near_clip;
	if (light_mode == 2 && !light_cut_one)
		near_clip = projection_near * 1.0102f;
	/* after b116: this draw's vertices remembered (light_memo) */
	if (light_memo_check < 0)
		light_memo_check = psp_keep("lightmemocheck");
	if (light_memo_check && light_memo_checks >= 200000)
	{
		platform_log("GE: light memo checked %lu times, %lu mismatches", light_memo_checks, light_memo_mismatches);
		light_memo_checks = light_memo_mismatches = 0;
	}
	if (++light_memo_draw > 0xFFFF)
	{
		memset(light_memo, 0, sizeof(light_memo));
		light_memo_draw = 1;
	}
	for (index = 0; index < triangle_count && kept_count < SCRATCH_TRIANGLES; index++)
	{
		const unsigned short *triangle = indices + 3 * index;
		/* after b116: no clear of all 33 floats a triangle; each corner's
		fields are written below before they're read, but 6 and 10 (an
		independent review) */
		float corners[3][CORNER_FLOATS], reach[3];
		int corner;
		const float *source[3];

		if (triangle[0] >= vertex_count || triangle[1] >= vertex_count || triangle[2] >= vertex_count)
		{
			near_clip = near_saved;
			return;
		}
		/* build 68: from the copies (position, and the packed normal kept
		beside them), not the map's vertices, which may be in the pack */
		for (corner = 0; corner < 3; corner++)
			source[corner] = &stream->base[triangle[corner]].x;
		if (spot && outside_cone(source[0], source[1], source[2], tangent))
		{
			light_cone_skipped++;
			continue;
		}
		if (spot)
		{
			/* build 103: a large triangle split near the light */
			if (light_split_on < 0)
				light_split_on = !psp_keep("nolightsplit");
			if (light_split_on)
			{
				float p[3][3], uv[3][2], longest = 0.0f;
				unsigned int normal[3];

				for (corner = 0; corner < 3; corner++)
				{
					p[corner][0] = source[corner][0];
					p[corner][1] = source[corner][1];
					p[corner][2] = source[corner][2];
					uv[corner][0] = stream->base[triangle[corner]].u;
					uv[corner][1] = stream->base[triangle[corner]].v;
					normal[corner] = stream->normals[triangle[corner]];
				}
				for (corner = 0; corner < 3; corner++)
				{
					float edge = distance_squared3(p[corner], p[(corner + 1) % 3]);

					if (edge > longest)
						longest = edge;
				}
				if (longest > 0.04f * light.radius * light.radius)
				{
					float nearest = 1e30f;

					for (corner = 0; corner < 3; corner++)
					{
						float reach = distance_squared3(p[corner], light.position);

						if (reach < nearest)
							nearest = reach;
					}
					/* out of the light's reach altogether: as before */
					if (sqrtf(nearest) - sqrtf(longest) > light.radius)
						continue;
					light_split_triangles++;
					light_split_tangent = tangent;
					light_split(p, uv, normal, 0, out, &kept_count, &cut_count);
					continue;
				}
			}
		}
		for (corner = 0; corner < 3; corner++)
		{
			/* after b116: a vertex shared by several triangles (3-6 in a
			level's lists) worked out once a draw: its reach, and its place
			from the camera when a triangle first needs it (the same
			functions on the same inputs: the same floats) */
			unsigned int vertex = triangle[corner], tag = (light_memo_draw << 16) | vertex;
			struct light_memo_entry *memo = &light_memo[vertex & (LIGHT_MEMO - 1)];

			if (memo->tag != tag)
			{
				memo->tag = tag;
				memo->reach = light_reach(source[corner], stream->normals[vertex], !spot);
				memo->camera_known = 0;
			}
			reach[corner] = memo->reach;
			if (light_memo_check)
			{
				/* "lightmemocheck": worked out again and compared, bit for bit */
				float again = light_reach(source[corner], stream->normals[vertex], !spot);

				if (memcmp(&again, &memo->reach, sizeof(again)))
					light_memo_mismatches++;
				light_memo_checks++;
			}
		}
		if (reach[0] <= 0.0f && reach[1] <= 0.0f && reach[2] <= 0.0f)
			continue;
		for (corner = 0; corner < 3; corner++)
		{
			struct light_memo_entry *memo = &light_memo[triangle[corner] & (LIGHT_MEMO - 1)];
			float direct[3];

			/* two corners of one triangle can share a slot (their low bits
			alike): the later one took it, so the earlier is worked out
			here instead (lightmemocheck found it on d20) */
			if (memo->tag != ((light_memo_draw << 16) | triangle[corner]))
			{
				to_camera(&stream->base[triangle[corner]].x, direct);
				memo = NULL;
			}
			else if (!memo->camera_known)
			{
				to_camera(&stream->base[triangle[corner]].x, memo->camera);
				memo->camera_known = 1;
			}
			else if (light_memo_check)
			{
				float again[3];

				to_camera(&stream->base[triangle[corner]].x, again);
				if (memcmp(again, memo->camera, sizeof(again)))
					light_memo_mismatches++;
				light_memo_checks++;
			}
			corners[corner][0] = memo ? memo->camera[0] : direct[0];
			corners[corner][1] = memo ? memo->camera[1] : direct[1];
			corners[corner][2] = memo ? memo->camera[2] : direct[2];
			corners[corner][7] = stream->base[triangle[corner]].x;
			corners[corner][8] = stream->base[triangle[corner]].y;
			corners[corner][9] = stream->base[triangle[corner]].z;
			corners[corner][3] = stream->base[triangle[corner]].u;
			corners[corner][4] = stream->base[triangle[corner]].v;
			corners[corner][5] = reach[corner];
			corners[corner][6] = 0.0f;
			corners[corner][10] = 0.0f;
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
	near_clip = near_saved;
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
	/* mode 2: GEQUAL, the nudge set per batch below (build 89) */
	sceGuDepthFunc(light_mode == 2 ? GU_GEQUAL : GU_EQUAL);
	sceGuDepthMask(GU_TRUE);
	ge_blend(1);
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
	if (kept_count && light_mode == 2)
	{
		/* build 89: in distance bands, each with the nudge its own nearest
		corner needs. b86-b88 sized one nudge for the whole draw from its
		nearest corner, even one behind the eye or off screen: a floor corner
		under the camera gave hundreds of depth steps, which lift a wall 5-10
		units off by 0.5-2 units, so the light was painted over whatever
		stood that close in front of it (review). Within a band (distances
		doubling) the lift is at most 4 times the one asked for */
		unsigned char band_of[SCRATCH_TRIANGLES];
		long band_count[LIGHT_BANDS], t;
		float band_near[LIGHT_BANDS];
		int b;

		for (b = 0; b < LIGHT_BANDS; b++)
		{
			band_count[b] = 0;
			band_near[b] = 1e30f;
		}
		for (t = 0; t < kept_count; t++)
		{
			float nearest = 1e30f;
			int c;

			for (c = 0; c < 3; c++)
			{
				const struct shadow_vertex *v = &out[3 * t + c];
				float dx = v->x - camera_eye[0], dy = v->y - camera_eye[1], dz = v->z - camera_eye[2];
				float d2 = dx * dx + dy * dy + dz * dz;

				if (d2 < nearest)
					nearest = d2;
			}
			b = light_band(nearest);
			band_of[t] = (unsigned char)b;
			band_count[b]++;
			if (nearest < band_near[b])
				band_near[b] = nearest;
		}
		for (b = 0; b < LIGHT_BANDS; b++)
		{
			struct shadow_vertex *copy;
			long n = 0;

			if (!band_count[b])
				continue;
			copy = list_take((int)band_count[b] * 3 * sizeof(struct shadow_vertex));
			if (!copy)
				break;
			for (t = 0; t < kept_count; t++)
				if (band_of[t] == b)
					memcpy(copy + 3 * n++, out + 3 * t, 3 * sizeof(struct shadow_vertex));
			sceGuDepthOffset(light_nudge_units(sqrtf(band_near[b])));
			sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
				(int)n * 3, NULL, copy);
			light_nudged_draws++;
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
		float nearest = 1e30f;

		if (light_cut_one < 0)
			light_cut_one = psp_keep("lightcutone");
		for (index = 0; pieces && index < cut_count; index++)
		{
			const float *c = cut_corners[index];

			pieces[index].u = c[3];
			pieces[index].v = c[4];
			pieces[index].color = light_color(c[5]);
			pieces[index].x = c[7];
			pieces[index].y = c[8];
			pieces[index].z = c[9];
			if (light_mode == 2 && !light_cut_one)
				light_lift_corner(&pieces[index]);
			else if (light_mode == 2)
			{
				float dx = c[7] - camera_eye[0], dy = c[8] - camera_eye[1], dz = c[9] - camera_eye[2];
				float d2 = dx * dx + dy * dy + dz * dz;

				if (d2 < nearest)
					nearest = d2;
			}
		}
		if (pieces && light_mode == 2)
		{
			/* build 117: the corners carry their own lift; no nudge */
			sceGuDepthOffset(light_cut_one ? light_nudge_units(sqrtf(nearest)) : 0);
			light_nudged_draws++;
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
	ge_blend(0);
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
	GE_DEFER(defer_words(DEFER_LIGHT_DRAW, 5, DEFER_P(vertices), (unsigned int)vertex_count, DEFER_P(indices),
		(unsigned int)triangle_count, base_key, 0, 0, 0)); /* build 112: gedefer */
	GE_BENCH_SKIP();
	ge_kind(KIND_LIGHT); /* build 103 */
	unsigned int start = ge_clock();

	psp_ge_light_draw_body(vertices, vertex_count, indices, triangle_count, base_key);
	ge_entry_us[6] += ge_clock() - start;
}

/* ---------- the screen flash (build 23): a colour over the whole picture,
lightening (taking damage, pickups), darkening or tinting it, at the
game's intensity (rasterizer_xbox_screen_effect.c's sums). Build 116 (b113
#9): max, min and invert as the Xbox draws them (the GE has its MAX and MIN
blends and the factors invert needs); builds 23-115 drew max as a lighten
and min as a darken, so a needle's pink flash went solid over the picture
where the Xbox's only lifts what is darker than it. Keep word
"flashnearest": as before */

static unsigned long flashes_drawn = 0;
static int flash_nearest = -1;

/* a flash colour channel as a byte, clamped */
static unsigned int flash_byte(float value)
{
	return (unsigned int)PSP_CLAMP_255((int)(value * 255.0f + 0.5f));
}

void psp_ge_screen_flash(short type, float intensity, float const *argb)
{
	GE_DEFER(defer_screen_flash(type, intensity, argb)); /* build 112: gedefer */
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
	ge_blend(1);
	if (flash_nearest < 0)
		flash_nearest = psp_keep("flashnearest");
	if (!flash_nearest && (type == 3 || type == 4 || type == 5))
	{
		/* the Xbox's source colour for each (its combiner's sums of the
		flash colour f and its alpha fa, each channel clamped) */
		float f[3] = { r, g, b }, source[3];
		int k;

		for (k = 0; k < 3; k++)
			source[k] = type == 3 ? f[k] - a : type == 4 ? f[k] + a : f[k] * (2.0f * f[k] - 1.0f) + a;
		color = 0xFF000000 | (flash_byte(source[2]) << 16) | (flash_byte(source[1]) << 8) | flash_byte(source[0]);
		if (type == 3) /* max: the brighter of the picture and the colour */
			sceGuBlendFunc(GU_MAX, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF);
		else if (type == 4) /* min: the darker */
			sceGuBlendFunc(GU_MIN, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF);
		else /* invert: the colour times (1 - the picture), plus the picture times (1 - f) */
			sceGuBlendFunc(GU_ADD, GU_ONE_MINUS_DST_COLOR, GU_FIX, 0,
				(flash_byte(1.0f - b) << 16) | (flash_byte(1.0f - g) << 8) | flash_byte(1.0f - r));
	}
	else switch (type)
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
	ge_blend(0);
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
static unsigned long decals_small = 0, decals_offscreen = 0;
/* build 106: decal calls with no quad on the screen, left before the bind */
static unsigned long decals_wholly_left_out = 0;
/* "decalnocull": quads off the screen go through the work too (to time it) */
static int decal_cull = -1;

#define DECAL_MINIMUM_PIXELS 2.0f /* build 58: 2 (was 3: a little too far out, Sheepdog01 b57) */


/* build 90: a permanent decal (the level's painted symbols) drawn on the
level's own triangles under it, as the base pass drew them this frame
(frame_level), with depth EQUAL: the texture is put on them by the GE's
texture matrix from each position (the decal quad's own mapping, worked out
from three of its corners), and the decal texture's clamped edge (black for
an added symbol) leaves the rest of each triangle as it was. A decal over
several planes (wrapped round an edge) is drawn per plane. 0 if nothing
under it was drawn this frame: the caller draws it the usual way */
/* build 106: off unless the debug menu turns it on. In every log since
(The Library on the PSP in b105, b30 and The Library in the emulator) it
drew none on the level's triangles ("0 drawn on the level's own
triangles"): each permanent decal fell back to the usual draw after the
scan, which cost 0.8 ms a frame at b30's beach in the emulator */
static int decal_project = 0;
static unsigned long decals_projected = 0, decal_projected_triangles = 0, decals_project_fallback = 0;
static unsigned short project_indices[3 * 1024];
/* build 91: what lies under each decal plane, remembered. b90 tested every
triangle the level drew this frame against every plane, each frame: 3-4 ms
a frame at blue base and 15 ms in front of red base (b90 benchrun). The
level's triangles under a permanent decal never change, so the hits are kept
per plane (the union over the frames that scanned) and drawn from the list;
a plane is scanned again every DECAL_RESCAN frames (every DECAL_RESCAN_EMPTY
while it has nothing), so a symbol coming into view is complete within half
a second. Each
hit keeps its stream's pack address, so a stream let go and rebuilt for
other vertices isn't drawn from. The near-plane pieces are still found each
frame (they change with the eye) */
#define DECAL_CACHE_PLANES 32
#define DECAL_CACHE_HITS 96
#define DECAL_RESCAN 16
/* a plane with nothing under it yet (out of view, or not drawn): looked at
again every 4 frames */
#define DECAL_RESCAN_EMPTY 4
struct decal_cache_hit
{
	const struct level_stream *stream;
	const void *source;
	unsigned short tri[3];
};
static struct decal_cache_slot
{
	unsigned int key;
	unsigned int scanned;
	int count;
	struct decal_cache_hit hit[DECAL_CACHE_HITS];
} decal_cache[DECAL_CACHE_PLANES];
static int decal_cache_next = 0;
static unsigned long decal_cache_scans = 0, decal_cache_uses = 0, decal_project_us = 0;
/* build 106: the decals' timers, on one call in DECAL_TIMED_ONE_IN and
counted that many times over. The clock is a system call, and b30's beach
draws hundreds of decals a frame: timing each one was 6% of the game
thread in the emulator's PC samples (tools/pcprofile.py) */
#define DECAL_TIMED_ONE_IN 16
static int decal_timed = 0;
static inline unsigned int decal_clock(void)
{
	return decal_timed ? ge_clock() : 0;
}

static unsigned int decal_plane_key(const float *n, float d, const float *low, const float *high)
{
	unsigned int h = 2166136261u;
	int k;

	for (k = 0; k < 3; k++)
	{
		h = (h ^ (unsigned int)(int)(n[k] * 1000.0f)) * 16777619u;
		h = (h ^ (unsigned int)(int)(low[k] * 100.0f)) * 16777619u;
		h = (h ^ (unsigned int)(int)(high[k] * 100.0f)) * 16777619u;
	}
	h = (h ^ (unsigned int)(int)(d * 100.0f)) * 16777619u;
	return h ? h : 1;
}

static struct decal_cache_slot *decal_cache_find(unsigned int key)
{
	int i;
	struct decal_cache_slot *slot;

	for (i = 0; i < DECAL_CACHE_PLANES; i++)
		if (decal_cache[i].key == key)
			return &decal_cache[i];
	slot = &decal_cache[decal_cache_next];
	decal_cache_next = (decal_cache_next + 1) % DECAL_CACHE_PLANES;
	slot->key = key;
	slot->count = 0;
	slot->scanned = stream_frame - DECAL_RESCAN;
	return slot;
}

struct decal_plane
{
	float n[3], d, gu[3], gv[3], cu, cv, low[3], high[3];
};

static int decal_project_hit(const float *a, const float *b, const float *c, const struct decal_plane *g)
{
	float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
	float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
	float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		float lo = a[axis], hi = a[axis];

		if (b[axis] < lo) lo = b[axis];
		if (c[axis] < lo) lo = c[axis];
		if (b[axis] > hi) hi = b[axis];
		if (c[axis] > hi) hi = c[axis];
		if (hi < g->low[axis] || lo > g->high[axis])
			return 0;
	}
	if (length <= 0.0f || fabsf(n[0] * g->n[0] + n[1] * g->n[1] + n[2] * g->n[2]) < 0.95f * length)
		return 0;
	return fabsf(a[0] * g->n[0] + a[1] * g->n[1] + a[2] * g->n[2] - g->d) < 0.08f &&
		fabsf(b[0] * g->n[0] + b[1] * g->n[1] + b[2] * g->n[2] - g->d) < 0.08f &&
		fabsf(c[0] * g->n[0] + c[1] * g->n[1] + c[2] * g->n[2] - g->d) < 0.08f;
}

static int decal_project_draw(const unsigned char *in, int quad_count)
{
	struct decal_plane planes[8];
	int plane_count = 0, quad, p, r, triangles = 0, scan;
	struct decal_cache_slot *slot;

	if (frame_level_stamp != stream_frame || !frame_level_count)
		return 0;
	for (quad = 0; quad < quad_count; quad++)
	{
		const float *v[4];
		float uv[4][2], e1[3], e2[3], n[3], length, a11, a12, a22, det;
		int k;
		struct decal_plane g;

		for (k = 0; k < 4; k++)
		{
			const unsigned char *x = in + 16 * (4 * quad + k);

			v[k] = (const float *)x;
			uv[k][0] = *(const unsigned short *)(x + 12) * (1.0f / 32768.0f);
			uv[k][1] = *(const unsigned short *)(x + 14) * (1.0f / 32768.0f);
		}
		for (k = 0; k < 3; k++)
		{
			e1[k] = v[1][k] - v[0][k];
			e2[k] = v[3][k] - v[0][k];
		}
		n[0] = e1[1] * e2[2] - e1[2] * e2[1];
		n[1] = e1[2] * e2[0] - e1[0] * e2[2];
		n[2] = e1[0] * e2[1] - e1[1] * e2[0];
		length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		a11 = e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2];
		a12 = e1[0] * e2[0] + e1[1] * e2[1] + e1[2] * e2[2];
		a22 = e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2];
		det = a11 * a22 - a12 * a12;
		if (length < 1e-6f || det < 1e-12f)
			continue;
		for (k = 0; k < 3; k++)
			g.n[k] = n[k] / length;
		g.d = g.n[0] * v[0][0] + g.n[1] * v[0][1] + g.n[2] * v[0][2];
		{
			/* the gradient of u (and v) in the quad's plane: g = a e1 + b e2
			with g.e1 = du1 and g.e2 = du2 */
			float du1 = uv[1][0] - uv[0][0], du2 = uv[3][0] - uv[0][0];
			float dv1 = uv[1][1] - uv[0][1], dv2 = uv[3][1] - uv[0][1];
			float au = (du1 * a22 - du2 * a12) / det, bu = (du2 * a11 - du1 * a12) / det;
			float av = (dv1 * a22 - dv2 * a12) / det, bv = (dv2 * a11 - dv1 * a12) / det;

			for (k = 0; k < 3; k++)
			{
				g.gu[k] = au * e1[k] + bu * e2[k];
				g.gv[k] = av * e1[k] + bv * e2[k];
			}
			g.cu = uv[0][0] - (g.gu[0] * v[0][0] + g.gu[1] * v[0][1] + g.gu[2] * v[0][2]);
			g.cv = uv[0][1] - (g.gv[0] * v[0][0] + g.gv[1] * v[0][1] + g.gv[2] * v[0][2]);
		}
		for (k = 0; k < 3; k++)
		{
			int c;

			g.low[k] = g.high[k] = v[0][k];
			for (c = 1; c < 4; c++)
			{
				if (v[c][k] < g.low[k]) g.low[k] = v[c][k];
				if (v[c][k] > g.high[k]) g.high[k] = v[c][k];
			}
		}
		/* the same plane as an earlier quad: one draw, its box grown */
		for (p = 0; p < plane_count; p++)
			if (planes[p].n[0] * g.n[0] + planes[p].n[1] * g.n[1] + planes[p].n[2] * g.n[2] > 0.999f &&
				fabsf(planes[p].d - g.d) < 0.01f)
				break;
		if (p < plane_count)
		{
			for (k = 0; k < 3; k++)
			{
				if (g.low[k] < planes[p].low[k]) planes[p].low[k] = g.low[k];
				if (g.high[k] > planes[p].high[k]) planes[p].high[k] = g.high[k];
			}
		}
		else if (plane_count < 8)
			planes[plane_count++] = g;
	}
	if (!plane_count)
		return 0;
	sceGuDepthFunc(GU_EQUAL);
	sceGuDepthOffset(0);
	if (level_cull)
	{
		sceGuFrontFace(level_cull == 1 ? LEVEL_FRONT : (LEVEL_FRONT == GU_CW ? GU_CCW : GU_CW));
		sceGuEnable(GU_CULL_FACE);
	}
	sceGuTexMapMode(GU_TEXTURE_MATRIX, 0, 0);
	sceGuTexProjMapMode(GU_POSITION);
	for (p = 0; p < plane_count; p++)
	{
		struct decal_plane *g = &planes[p];
		ScePspFMatrix4 m;
		int k;

		for (k = 0; k < 3; k++)
		{
			g->low[k] -= 0.05f;
			g->high[k] += 0.05f;
		}
		memset(&m, 0, sizeof(m));
		m.x.x = g->gu[0]; m.y.x = g->gu[1]; m.z.x = g->gu[2]; m.w.x = g->cu;
		m.x.y = g->gv[0]; m.y.y = g->gv[1]; m.z.y = g->gv[2]; m.w.y = g->cv;
		m.w.z = 1.0f;
		m.w.w = 1.0f;
		/* straight to the GE (pspgum's texture matrix slot isn't sent; as the
		spotlight's cone) */
		sceGuSetMatrix(GU_TEXTURE, &m);
		slot = decal_cache_find(decal_plane_key(g->n, g->d, g->low, g->high));
		scan = (int)(stream_frame - slot->scanned) >= (slot->count ? DECAL_RESCAN : DECAL_RESCAN_EMPTY);
		if (scan)
		{
			slot->scanned = stream_frame;
			decal_cache_scans++;
		}
		else
			decal_cache_uses++;
		/* from memory: the remembered hits, a draw per stream, for streams
		drawn this frame with no near-plane pieces (those are scanned below) */
		if (!scan)
		{
			int h = 0;

			while (h < slot->count)
			{
				const struct level_stream *stream = slot->hit[h].stream;
				int count = 0, drawn = 0, q;

				for (q = 0; q < frame_level_count && !drawn; q++)
					if (frame_level[q].stream == stream)
						drawn = 1;
				/* the hits are stored stream by stream (one scan's order). A
				remembered triangle with a corner outside the near plane or the
				guard band was cut up or left out by the base pass this frame
				(the classifier's own test): its pieces are found below */
				for (; h < slot->count && slot->hit[h].stream == stream; h++)
					if (drawn && slot->hit[h].source == stream->source)
					{
						int c, out = 0;

						for (c = 0; c < 3 && !out; c++)
						{
							float camera[3];

							to_camera(&stream->base[slot->hit[h].tri[c]].x, camera);
							out = outside(camera);
						}
						if (out)
							continue;
						project_indices[3 * count] = slot->hit[h].tri[0];
						project_indices[3 * count + 1] = slot->hit[h].tri[1];
						project_indices[3 * count + 2] = slot->hit[h].tri[2];
						count++;
					}
				if (count)
				{
					unsigned short *list = list_take((count * 3 * sizeof(unsigned short) + 3) & ~3);

					if (list)
					{
						memcpy(list, project_indices, count * 3 * sizeof(unsigned short));
						sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
							count * 3, list, stream->base);
						triangles += count;
					}
				}
			}
		}
		for (r = 0; r < frame_level_count; r++)
		{
			const struct frame_level_draw *f = &frame_level[r];
			const struct level_stream *stream = f->stream;
			int count = 0, t, h, scan_here;

			if (stream->high[0] < g->low[0] || stream->low[0] > g->high[0] || stream->high[1] < g->low[1] ||
				stream->low[1] > g->high[1] || stream->high[2] < g->low[2] || stream->low[2] > g->high[2])
				continue;
			/* a draw with triangles cut at the near plane is scanned: a
			remembered triangle may be one of them this frame, drawn as its
			pieces (whole, its EQUAL draw would stripe and its pieces add on
			top) */
			scan_here = scan;
			for (t = 0; scan_here && t < f->kept && count < 1024; t++)
			{
				const unsigned short *tri = f->indices + 3 * t;

				if (decal_project_hit(&stream->base[tri[0]].x, &stream->base[tri[1]].x, &stream->base[tri[2]].x, g))
				{
					project_indices[3 * count] = tri[0];
					project_indices[3 * count + 1] = tri[1];
					project_indices[3 * count + 2] = tri[2];
					count++;
					/* remembered (once) */
					for (h = 0; h < slot->count; h++)
						if (slot->hit[h].stream == stream && slot->hit[h].tri[0] == tri[0] &&
							slot->hit[h].tri[1] == tri[1] && slot->hit[h].tri[2] == tri[2])
							break;
					if (h == slot->count && slot->count < DECAL_CACHE_HITS)
					{
						slot->hit[h].stream = stream;
						slot->hit[h].source = stream->source;
						slot->hit[h].tri[0] = tri[0];
						slot->hit[h].tri[1] = tri[1];
						slot->hit[h].tri[2] = tri[2];
						slot->count++;
					}
				}
			}
			if (count)
			{
				unsigned short *list = list_take((count * 3 * sizeof(unsigned short) + 3) & ~3);

				if (list)
				{
					memcpy(list, project_indices, count * 3 * sizeof(unsigned short));
					sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
						count * 3, list, stream->base);
					triangles += count;
				}
			}
			/* the corners cut at the near plane, as the base pass drew them */
			count = 0;
			for (t = 0; f->cut && t + 2 < f->cut_count && count < 1024; t += 3)
				if (decal_project_hit(&f->cut[t].x, &f->cut[t + 1].x, &f->cut[t + 2].x, g))
				{
					project_indices[3 * count] = (unsigned short)t;
					project_indices[3 * count + 1] = (unsigned short)(t + 1);
					project_indices[3 * count + 2] = (unsigned short)(t + 2);
					count++;
				}
			if (count)
			{
				unsigned short *list = list_take((count * 3 * sizeof(unsigned short) + 3) & ~3);

				if (list)
				{
					memcpy(list, project_indices, count * 3 * sizeof(unsigned short));
					sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
						count * 3, list, f->cut);
					triangles += count;
				}
			}
		}
	}
	sceGuTexMapMode(GU_TEXTURE_COORDS, 0, 0);
	sceGuDisable(GU_CULL_FACE);
	sceGuDepthFunc(GU_GEQUAL);
	if (triangles)
	{
		decals_projected++;
		decal_projected_triangles += (unsigned long)triangles;
	}
	return triangles;
}

static void psp_ge_decal_body(void const *vertices, int quad_count, unsigned int key, short blend, unsigned long color,
	int intensity, int alpha_tested, int permanent)
{
	const unsigned char *in = vertices;
	struct decal_vertex_ge *out;
	static const int fan[6] = { 0, 1, 2, 0, 2, 3 };
	float i = intensity * (1.0f / 255.0f), c[3];
	int quad, corner, alpha = 0, k;
	unsigned int fragment = 0xFFFFFFFF, constant = 0xFFFFFFFF, fog = fog_color;
	/* build 76: each quad's own slope part is applied as its own pull (one
	depth offset for the draw took its largest quad's: a stencil seen
	edge-on beside the eye gave every decal of its batch, far ones too,
	hundreds of steps; far off that is tens of units); the largest is kept
	for "decalbatch" */
	float decal_units = 0.0f;
	int original_quads = quad_count;
	unsigned int project_start;
	int project_done;
	/* build 106: the first quad kept, as the game gave it (its texture level
	is worked out from it before the pull moves its corners) */
	float level_corners[3][3];
	short level_uv[3][2];

	if (!frame_open || !vertices || quad_count <= 0 || quad_count > 256 || intensity <= 0 || list_full())
		return;
	c[0] = ((color >> 16) & 0xFF) * (1.0f / 255.0f);
	c[1] = ((color >> 8) & 0xFF) * (1.0f / 255.0f);
	c[2] = (color & 0xFF) * (1.0f / 255.0f);
	if (decal_pull < 0)
	{
		decal_pull = !psp_keep("decaloffset");
		/* build 55, a test: a nudge four times larger plus 1% of the
		distance. If bullet holes still flicker with it, the cause isn't
		the depth test */
		decal_big = psp_keep("decalbig");
		decal_batch = psp_keep("decalbatch");
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
	{
		/* build 57: decals smaller than DECAL_MINIMUM_PIXELS across on the
		screen are left out. A distant bullet hole covers a pixel or two, and
		at 272 lines it lit a pixel one frame and none the next as the view
		moved: the blinking Sheepdog01 saw at a distance (b50-b56; a larger depth
		nudge changed nothing, so it wasn't the depth test). "decalall": all */
		/* build 106: and quads wholly outside the view are left out before
		any of the work below (b30's beach: 6-11 ms a frame in decals by the
		end of the fight, in the emulator, most of it bullet holes behind the
		camera or to the sides); the pull is worked out once a corner, not
		once for each of the two triangles' copies of it. The kept quads come
		out as before.
		Build 106 too: in two passes, which quads are kept first, then the
		texture bound and the list taken for those alone. A decal wholly off
		the screen (most of the 400 calls a frame at b30's beach) no longer
		binds its texture (a texture fetch for nothing, if it wasn't in VRAM)
		or takes display list room for all its quads, which it then left
		unused: decals were 131 KB of b30's 282 KB list a frame */
		static unsigned char kept_quad[256];
		static float kept_units[256];
		int kept = 0, k_quad;
		float half_y = fov_tan_half(), half_x = half_y * (float)SCREEN_WIDTH / SCREEN_HEIGHT;
		float pixels_per_unit = (SCREEN_HEIGHT * 0.5f) / half_y;
		/* a sphere is wholly outside a side plane when its centre is beyond
		it by more than its radius: the planes' normals, unnormalised, have
		these lengths */
		float side_x = sqrtf(1.0f + half_x * half_x), side_y = sqrtf(1.0f + half_y * half_y);
		float per_step = 1.0f / (projection_near * 61439.0f);

		if (decal_all < 0)
			decal_all = psp_keep("decalall");
		if (decal_cull < 0)
			decal_cull = !psp_keep("decalnocull");
		if (decal_slope < 0)
			decal_slope = !psp_keep("decalnoslope");
		for (quad = 0; quad < quad_count; quad++)
		{
			const float *p[4];
			float centre[3], camera[3], radius2 = 0.0f, radius, units = 0.0f;

			for (corner = 0; corner < 4; corner++)
				p[corner] = (const float *)(in + 16 * (4 * quad + corner));
			centre[0] = (p[0][0] + p[2][0]) * 0.5f;
			centre[1] = (p[0][1] + p[2][1]) * 0.5f;
			centre[2] = (p[0][2] + p[2][2]) * 0.5f;
			to_camera(centre, camera);
			if (!decal_all)
			{
				float dx = p[2][0] - p[0][0], dy = p[2][1] - p[0][1], dz = p[2][2] - p[0][2];

				if (camera[2] > 0.0f &&
					sqrtf(dx * dx + dy * dy + dz * dz) * pixels_per_unit < DECAL_MINIMUM_PIXELS * camera[2])
				{
					decals_small++;
					continue;
				}
			}
			for (corner = 0; corner < 4; corner++)
			{
				float dx = p[corner][0] - centre[0], dy = p[corner][1] - centre[1], dz = p[corner][2] - centre[2];
				float d2 = dx * dx + dy * dy + dz * dz;

				if (d2 > radius2)
					radius2 = d2;
			}
			radius = sqrtf(radius2) * 1.01f + 0.01f;
			if (!decal_cull)
				;
			else if (camera[2] < -radius ||
				camera[0] - half_x * camera[2] > radius * side_x || -camera[0] - half_x * camera[2] > radius * side_x ||
				camera[1] - half_y * camera[2] > radius * side_y || -camera[1] - half_y * camera[2] > radius * side_y)
			{
				decals_offscreen++;
				continue;
			}
			/* build 65: the quad's own depth slope (as the level's painted
			symbols since b60), the largest of the decal's quads */
			if (decal_slope)
			{
				float q[3][3];

				for (corner = 0; corner < 3; corner++)
					to_camera(p[corner], q[corner]);
				units = slope_units_capped(slope_units(q[0], q[1], q[2]), q[0], q[1], q[2], 0.0f) + nudge_floor;
				if (units > decal_units)
					decal_units = units;
			}
			if (!kept)
				for (corner = 0; corner < 3; corner++)
				{
					memcpy(level_corners[corner], p[corner], 12);
					memcpy(level_uv[corner], (const unsigned char *)p[corner] + 12, 4);
				}
			kept_quad[kept] = (unsigned char)quad;
			kept_units[kept] = units;
			kept++;
		}
		if (!kept)
		{
			decals_wholly_left_out++;
			return;
		}
		if (!psp_tex_bind(key, &alpha))
			return;
		out = list_take(kept * 6 * sizeof(struct decal_vertex_ge));
		if (!out)
			return;
		for (k_quad = 0; k_quad < kept; k_quad++)
		{
			const float *p[4];
			float pulled[4][3];

			for (corner = 0; corner < 4; corner++)
				p[corner] = (const float *)(in + 16 * (4 * kept_quad[k_quad] + corner));
			/* the pull: each corner toward the eye along its line of sight
			(build 52's comment above) */
			for (corner = 0; corner < 4; corner++)
			{
				float d[3] = { p[corner][0] - camera_eye[0], p[corner][1] - camera_eye[1], p[corner][2] - camera_eye[2] };

				memcpy(pulled[corner], p[corner], 12);
				if (decal_pull)
				{
					float length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
					float steps = 2.0f + (decal_batch ? 0.0f : kept_units[k_quad]);
					float shift = decal_big ? 8.0f * length * length * per_step + 0.01f * length + 0.01f :
						steps * length * length * per_step + 0.003f;

					if (length > 2.0f * shift)
					{
						float scale = 1.0f - shift / length;

						pulled[corner][0] = camera_eye[0] + d[0] * scale;
						pulled[corner][1] = camera_eye[1] + d[1] * scale;
						pulled[corner][2] = camera_eye[2] + d[2] * scale;
					}
				}
			}
			for (corner = 0; corner < 6; corner++)
			{
				struct decal_vertex_ge *o = &out[6 * k_quad + corner];

				o->x = pulled[fan[corner]][0];
				o->y = pulled[fan[corner]][1];
				o->z = pulled[fan[corner]][2];
				memcpy(&o->u, (const unsigned char *)p[fan[corner]] + 12, 4);
			}
		}
		quad_count = kept;
	}
	{
		/* build 58: the texture level by distance, as for the level (a hole
		on the ground swam with the ground's own texture): from the first
		quad kept, as the game gave it */
		const float *a = level_corners[0], *b = level_corners[1], *c = level_corners[2];
		float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
		float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
		float world_area = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		float uv_area = fabsf((float)(level_uv[1][0] - level_uv[0][0]) * (float)(level_uv[2][1] - level_uv[0][1]) -
			(float)(level_uv[2][0] - level_uv[0][0]) * (float)(level_uv[1][1] - level_uv[0][1])) *
			(1.0f / 32768.0f) * (1.0f / 32768.0f);
		int width = 0, height = 0;

		psp_tex_size(key, &width, &height);
		texture_levels_by_distance(world_area > 0.0f ? sqrtf(uv_area / world_area) : 0.0f, width, height, 1.0f, tex_bias);
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
	ge_blend(1);
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
	if (permanent && decal_project && level_compact <= 0 &&
		(project_start = decal_clock(), project_done = decal_project_draw(in, original_quads),
		decal_project_us += (decal_clock() - project_start) * DECAL_TIMED_ONE_IN, project_done))
		;
	else
	{
		if (permanent && decal_project)
			decals_project_fallback++;
		sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_16BIT | GU_VERTEX_32BITF | GU_TRANSFORM_3D, quad_count * 6, NULL, out);
	}
	sceGuTexLevelMode(tex_level_mode, tex_bias);
	sceGuFog(fog_near, fog_far, fog_color);
	sceGuDisable(GU_ALPHA_TEST);
	sceGuTexEnvColor(0xFFFFFFFF);
	ge_blend(0);
	sceGuDepthOffset(0);
	sceGuDepthMask(GU_FALSE);
	sceGuDisable(GU_TEXTURE_2D);
	decals_drawn++;
}

void psp_ge_decal(void const *vertices, int quad_count, unsigned int key, short blend, unsigned long color,
	int intensity, int alpha_tested, int permanent)
{
	GE_DEFER(defer_decal(vertices, quad_count, key, blend, color, intensity, alpha_tested, permanent)); /* build 112: gedefer */
	GE_BENCH_SKIP();
	static unsigned int decal_calls = 0;

	ge_kind(KIND_DECAL); /* build 103 */
	/* build 106: one call in DECAL_TIMED_ONE_IN timed (decal_clock) */
	decal_timed = (decal_calls++ % DECAL_TIMED_ONE_IN) == 0;
	if (decal_timed)
	{
		unsigned int start = decal_clock(), took;

		psp_ge_decal_body(vertices, quad_count, key, blend, color, intensity, alpha_tested, permanent);
		took = (decal_clock() - start) * DECAL_TIMED_ONE_IN;
		ge_entry_us[5] += took;
		ge_part_us[10] += took;
		decal_timed = 0;
	}
	else
		psp_ge_decal_body(vertices, quad_count, key, blend, color, intensity, alpha_tested, permanent);
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
	GE_DEFER(defer_words(DEFER_BLIPS_BEGIN, 0, 0, 0, 0, 0, 0, 0, 0, 0)); /* build 112: gedefer */
	ge_kind(KIND_HUD); /* build 103 */
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
	ge_blend(0);
	sceGuDisable(GU_TEXTURE_2D);
	/* black, drawn rather than cleared (a clear covers the whole screen's
	size, past the target's end) */
	blip_quad(nothing, everywhere, 0xFF000000);
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexWrap(GU_CLAMP, GU_CLAMP);
	sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
	ge_blend(1);
	blips_open = 1;
}

void psp_ge_blip(float x, float y, float radius, float red, float green, float blue, unsigned int key)
{
	GE_DEFER(defer_words(DEFER_BLIP, 7, defer_bits(x), defer_bits(y), defer_bits(radius), defer_bits(red),
		defer_bits(green), defer_bits(blue), key, 0)); /* build 112: gedefer */
	GE_BENCH_SKIP();
	ge_kind(KIND_HUD); /* build 103 */
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
	GE_DEFER(defer_words(DEFER_BLIPS_END, 5, defer_bits(center_x), defer_bits(center_y), defer_bits(scale), sweep_key,
		mask_key, 0, 0, 0)); /* build 112: gedefer */
	ge_kind(KIND_HUD); /* build 103 */
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
	ge_blend(0);
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
	GE_DEFER_DRAIN(); /* build 112: gedefer */
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

		ge_writeback((void *)start, end - start);
	}
	text_changed = 1;
	text_changes++;
}

int psp_ge_text_begin(void const *bitmap, short blend, float offset_x, float offset_y)
{
	/* build 112: gedefer. The engine needs the answer now (whether to send
	the characters), so it is worked out here from what the main CPU knows:
	the frame and the font's picture. Whether the list has room is the
	carrying out's to judge: if it hasn't, the text stays closed then and
	the characters written down after this are dropped there, as the engine
	would have left them out. The same list either way */
	if (DEFER_RECORDING() && !defer_paused)
	{
		const unsigned char *picture = bitmap;
		int answer = frame_open && picture && *(void *const *)(picture + 44) && *(const short *)(picture + 12) == 9 &&
			*(const short *)(picture + 4) > 0 && *(const short *)(picture + 6) > 0 &&
			*(const short *)(picture + 4) <= 512 && *(const short *)(picture + 6) <= 512;

		defer_words(DEFER_TEXT_BEGIN, 4, DEFER_P(bitmap), (unsigned int)blend, defer_bits(offset_x), defer_bits(offset_y),
			0, 0, 0, 0);
		return answer;
	}
	ge_kind(KIND_HUD); /* build 103 */
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
	GE_DEFER(defer_text_character(vertices)); /* build 112: gedefer */
	GE_BENCH_SKIP();
	ge_kind(KIND_HUD); /* build 103 */
	unsigned int start = ge_clock();

	text_character(vertices);
	text_us += ge_clock() - start;
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
	GE_DEFER(defer_words(DEFER_TEXT_END, 0, 0, 0, 0, 0, 0, 0, 0, 0)); /* build 112: gedefer */
	if (!text_open)
		return;
	text_open = 0;
	ge_blend(0);
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

/* build 92: each shot stamped with the build, the run's seconds (the T of
the overlay and the log's [t ...]) and the frame, burned into the bottom
centre and put in the file name (shot004_t108.bmp). Not the PSP's clock:
Sheepdog01's is unreliable. A 5 x 7 font of the characters it needs */
static const unsigned char stamp_font[][7] = {
	{ 14, 17, 19, 21, 25, 17, 14 }, { 4, 12, 4, 4, 4, 4, 14 }, { 14, 17, 1, 2, 4, 8, 31 },
	{ 31, 2, 4, 2, 1, 17, 14 }, { 2, 6, 10, 18, 31, 2, 2 }, { 31, 16, 30, 1, 1, 17, 14 },
	{ 6, 8, 16, 30, 17, 17, 14 }, { 31, 1, 2, 4, 8, 8, 8 }, { 14, 17, 17, 14, 17, 17, 14 },
	{ 14, 17, 17, 15, 1, 2, 12 },
	{ 16, 16, 22, 25, 17, 17, 30 },	/* b */
	{ 8, 8, 28, 8, 8, 9, 6 },	/* t */
	{ 6, 9, 8, 28, 8, 8, 8 },	/* f */
	{ 0, 0, 0, 0, 0, 12, 12 },	/* . */
	{ 0, 0, 0, 0, 0, 0, 0 },	/* space */
};
#define STAMP_HEIGHT 11

static int stamp_glyph(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	return c == 'b' ? 10 : c == 't' ? 11 : c == 'f' ? 12 : c == '.' ? 13 : 14;
}

/* 1: white text, 2: the black box round it, 0: the picture */
static unsigned char stamp_mask[STAMP_HEIGHT][SCREEN_WIDTH];

static void stamp_build(const char *text)
{
	int length = (int)strlen(text), width = length * 6 + 3, left = (SCREEN_WIDTH - width) / 2, i, y, x;

	memset(stamp_mask, 0, sizeof(stamp_mask));
	for (y = 0; y < STAMP_HEIGHT; y++)
		for (x = left; x < left + width && x < SCREEN_WIDTH; x++)
			stamp_mask[y][x] = 2;
	for (i = 0; i < length; i++)
	{
		const unsigned char *glyph = stamp_font[stamp_glyph(text[i])];

		for (y = 0; y < 7; y++)
			for (x = 0; x < 5; x++)
				if (glyph[y] & (16 >> x))
					stamp_mask[2 + y][left + 2 + 6 * i + x] = 1;
	}
}

int psp_build_number(void);

static void screenshot(void)
{
	static unsigned char rows[4 * SCREEN_WIDTH * 3];
	const unsigned short *frame = (const unsigned short *)(0x44000000 + (unsigned int)draw_buffer);
	unsigned char header[54];
	unsigned int image = SCREEN_WIDTH * 3 * SCREEN_HEIGHT, size = 54 + image;
	char path[64], text[48];
	SceUID file, folder;
	SceIoDirent entry;
	int number = 0, y, x, row = 0;
	float seconds = psp_debug_seconds();

	/* the next number after the highest shotNNN* already there */
	sceIoMkdir("ms0:/halo/shots", 0777);
	folder = sceIoDopen("ms0:/halo/shots");
	if (folder >= 0)
	{
		memset(&entry, 0, sizeof(entry));
		while (sceIoDread(folder, &entry) > 0)
		{
			if (strncmp(entry.d_name, "shot", 4) == 0 && entry.d_name[4] >= '0' && entry.d_name[4] <= '9')
			{
				int n = atoi(entry.d_name + 4);

				if (n + 1 > number)
					number = n + 1;
			}
			memset(&entry, 0, sizeof(entry));
		}
		sceIoDclose(folder);
	}
	snprintf(path, sizeof(path), "ms0:/halo/shots/shot%03d_t%d.bmp", number, (int)seconds);
	snprintf(text, sizeof(text), "b%d  t %.1f  f %lu", psp_build_number(), seconds, frames);
	stamp_build(text);
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
		int stamp_row = y - (SCREEN_HEIGHT - STAMP_HEIGHT);

		for (x = 0; x < SCREEN_WIDTH; x++)
		{
			unsigned int v = frame[y * BUFFER_WIDTH + x];
			int m = stamp_row >= 0 ? stamp_mask[stamp_row][x] : 0;

			if (m)
			{
				out[3 * x] = out[3 * x + 1] = out[3 * x + 2] = m == 1 ? 255 : 0;
				continue;
			}
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
	platform_log("[t %.1f] screenshot: %s (frame %lu) from %.2f %.2f %.2f looking %.2f %.2f %.2f", seconds, path, frames,
		camera_eye[0], camera_eye[1], camera_eye[2], camera_forward[0], camera_forward[1], camera_forward[2]);
}

/* build 74: the frame end's parts over each heartbeat window, for the log's
frameend: line (psp_platform.c). Sheepdog01's b72/b73 runs: the step holding the
frame end took 0.8 s a frame, growing by ~15 ms each frame, with no Memory
Stick reads; the per-300-frame report never came at 1 fps */
static PSP_MAIN_SIDE unsigned long win_frames = 0, win_flush_us = 0, win_writeback_us = 0, win_ge_us = 0, win_ge_worst = 0,
	win_swap_us = 0, win_after_us = 0, win_list_bytes = 0, win_triangles = 0, win_draws = 0, win_transparent = 0;
void psp_ge_frame_window(char *text, int size)
{
	/* (no GE_DEFER_DRAIN: the heartbeat's thread asks for this line, and
	waiting calls run only on the game's thread) */
	unsigned long n = win_frames ? win_frames : 1;

	snprintf(text, size, "frameend: %lu frames; a frame: see-through flush %.1f ms, cache write-back %.1f, waiting for the GE %.1f (worst %.1f), swap %.1f, caches %.1f; display list %lu KB, %lu level draws, %lu triangles, %lu see-through draws",
		win_frames, win_flush_us / 1000.0 / n, win_writeback_us / 1000.0 / n, win_ge_us / 1000.0 / n, win_ge_worst / 1000.0,
		win_swap_us / 1000.0 / n, win_after_us / 1000.0 / n, win_list_bytes / 1024 / n, win_draws / n, win_triangles / n,
		win_transparent / n);
	if (win_other_targets)
	{
		size_t used = strlen(text);

		snprintf(text + used, size > (int)used ? size - used : 0,
			"; %lu windows for another target than the screen (drawn on it)", win_other_targets);
		win_other_targets = 0;
	}
	{
		size_t used = strlen(text);

		gebusy_window(text + used, size - (int)used, win_frames); /* build 113 */
	}
	win_frames = win_flush_us = win_writeback_us = win_ge_us = win_ge_worst = win_swap_us = win_after_us = 0;
	win_list_bytes = win_triangles = win_draws = win_transparent = 0;
}

void psp_ge_frame_end(void)
{
	GE_DEFER_DRAIN(); /* build 112: gedefer */
	unsigned int flush_mark;

	if (!frame_open)
		return;
	flush_mark = sceKernelGetSystemTimeLow();
	win_transparent += transparent_count;
	transparent_flush();
	taa_resolve();
	ge_kinds_frame_end(); /* build 103 */
	vm_frame_end(); /* build 116: vrammodels' copies for the next frame */
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
			if (used > display_list_size * 7 / 8)
				platform_log("GE: display list %d of %d bytes used", used, display_list_size);
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
		vram_busy_calibrate(waited);
		{
			/* build 87: this frame's renderer CPU and GE wait, for the
			heartbeat's window (psp_ge_budget) */
			unsigned long sum = 0;
			int k;

			for (k = 0; k < GE_ENTRIES; k++)
				sum += ge_entry_us[k];
			hb_renderer_us += sum >= entry_snapshot ? sum - entry_snapshot : sum;
			entry_snapshot = sum;
			hb_wait_us += waited;
			hb_frames++;
		}
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
		/* build 118: how long each frame stayed on the screen, in display
		refreshes (60 a second): frames are shown at refreshes, so a frame
		that misses 33.3 ms shows for 50 (20 fps for that frame), not 34.
		The heartbeat's 5 s averages hide these single-frame hitches */
		{
			unsigned int now = sceKernelGetSystemTimeLow();

			if (swap_last)
			{
				unsigned int shown = now - swap_last;
				int refreshes = shown < 25000 ? 0 : shown < 41667 ? 1 : shown < 58333 ? 2 : shown < 75000 ? 3 : 4;

				swap_shown[refreshes]++;
				if (shown > swap_longest)
					swap_longest = shown;
				/* build 118: mebench (a turn's first frame left out: it
				started under the last way) */
				if (me_bench > 0 && me_bench_settled)
				{
					me_bench_frames[me_bench_way]++;
					me_bench_us[me_bench_way] += shown;
				}
			}
			swap_last = now;
		}
		win_swap_us += sceKernelGetSystemTimeLow() - mark;
		frame_open = 0;
		mark = sceKernelGetSystemTimeLow();
		psp_tex_frame_end();
		/* build 68: the geometry cache's reads, and the level copies' budget */
		psp_geo_frame_end();
		/* build 112: the copies the Media Engine's work wanted, built now */
		me_log_write();
		{
			int k;

			for (k = 0; k < streams_wanted_count; k++)
				level_stream_get(streams_wanted[k].vertices, streams_wanted[k].lightmap_vertices, streams_wanted[k].count);
			streams_wanted_count = 0;
			/* the see-through level triangles it wanted asked */
			for (k = 0; k < walls_wanted_count; k++)
				overlay_on_a_wall(walls_wanted[k].stream, walls_wanted[k].indices, walls_wanted[k].triangle_count,
					walls_wanted[k].triangle);
			walls_wanted_count = 0;
			/* and the model buffers it wanted converted */
			for (k = 0; k < converts_wanted_count; k++)
			{
				struct skin_part *plan;

				model_convert(converts_wanted[k].vertices, converts_wanted[k].count, converts_wanted[k].uncompressed,
					converts_wanted[k].indices, converts_wanted[k].index_count, converts_wanted[k].strip,
					converts_wanted[k].skin ? &plan : NULL);
			}
			converts_wanted_count = 0;
		}
		if (psp_geo_open_count())
			level_streams_trim();
		arena_frame_end(); /* build 111: compaction, the heap's copies brought back */
		stream_frame++;
		frame_end_us[2] += sceKernelGetSystemTimeLow() - mark;
		win_after_us += sceKernelGetSystemTimeLow() - mark;
	}
	if (++frames == 1 || frames % 300 == 0)
	{
		platform_log("GE: sky drawn %lu times, %lu solid parts; %lu see-through draws, %lu dropped; %lu sprites; %lu HUD quads, %lu characters, %lu blips; skinned parts %lu (%lu batches, %lu KB, %lu left rigid); level see-through %lu; decals %lu; flashes %lu; shadows %lu begun, %lu requests, %lu draws; light draws %lu (%lu with a projected cone, %lu triangles outside cones skipped, %lu split near the light, %lu pieces added); bending triangles cut at the near plane %lu (%lu pieces); two-sided parts %lu; models at the near plane %lu (%lu triangles left out)",
			sky_draws, sky_parts, transparent_drawn, transparent_dropped, sprites_queued, hud_quads, text_characters,
			blips_drawn, skinned_parts, skin_batches, skin_bytes / 1024, skin_failures, level_transparent_draws, decals_drawn, flashes_drawn, shadow_begins, shadow_requests, shadows_drawn, lights_drawn, light_cone_draws, light_cone_skipped, light_split_triangles, light_split_pieces, skin_cut_triangles, skin_cut_pieces, two_sided_draws, near_models, near_triangles_dropped);
		/* build 87: its own line (the one above passed the log's line length) */
		/* build 89: the counters over these 300 frames (they were never
		reset, so a bench window's line carried every earlier window's;
		review), and the ME's part on a line of its own (512 characters) */
		platform_log("GE: light pass mode %d: %lu triangles as the level drew them, %lu nudged draws, %lu skipped; stencil bake (mode %d): %lu wall triangles drawn as %lu pieces, %lu stencil layer draws (%lu triangles), %lu stencil triangles on the old path; level vertices %s: %lu materials at 16 bits, %lu too wide, %lu 16-bit passes (last 300 frames)",
			light_mode, light_index_triangles, light_nudged_draws, lights_skipped, stencil_mode, bake_substituted,
			bake_pieces_drawn, bake_layer_draws, bake_layer_triangles, bake_old_path_triangles,
			level_compact > 0 ? "16-bit (levelcompact)" : "float", compact_streams, compact_refused, compact_draws);
		{
			/* build 119: the light pass's surface lists sorted (psp_fixups.py, structure_render.c#surfacesort) */
			extern unsigned long psp_surface_sorts, psp_surface_sorts_in_order, psp_surface_sort_mismatches,
				psp_surface_sorts_by_bits;

			platform_log("GE: surface lists sorted %lu (%lu through the surface bits, %lu already in order)%s (last 300 frames)",
				psp_surface_sorts, psp_surface_sorts_by_bits, psp_surface_sorts_in_order, psp_keep("surfacesortcheck") ? (psp_surface_sort_mismatches ?
				"; surfacesortcheck: SOME LISTS CAME OUT DIFFERENT" : "; surfacesortcheck: all the same") : "");
			if (psp_surface_sort_mismatches)
				platform_log("GE: surfacesortcheck: %lu lists came out different", psp_surface_sort_mismatches);
			psp_surface_sorts = psp_surface_sorts_in_order = psp_surface_sort_mismatches = psp_surface_sorts_by_bits = 0;
		}
		/* build 116: the restorations' own counters */
		/* build 118: the frames' time on screen (above) */
		platform_log("GE: frames shown for 1 display refresh (60 fps) %lu, 2 (30 fps) %lu, 3 (20 fps) %lu, 4 (15 fps) %lu, 5 or more %lu; the longest %u ms (last 300 frames)",
			swap_shown[0], swap_shown[1], swap_shown[2], swap_shown[3], swap_shown[4], swap_longest / 1000);
		memset(swap_shown, 0, sizeof(swap_shown));
		swap_longest = 0;
		if (me_bench > 0)
		{
			int way;

			platform_log("GE: mebench, last 300 frames: on the main CPU %lu frames, %.1f ms a frame; on the ME serially %lu, %.1f ms; overlapped %lu, %.1f ms",
				me_bench_frames[0], me_bench_frames[0] ? me_bench_us[0] / 1000.0f / me_bench_frames[0] : 0.0f,
				me_bench_frames[1], me_bench_frames[1] ? me_bench_us[1] / 1000.0f / me_bench_frames[1] : 0.0f,
				me_bench_frames[2], me_bench_frames[2] ? me_bench_us[2] / 1000.0f / me_bench_frames[2] : 0.0f);
			for (way = 0; way < 3; way++)
				me_bench_frames[way] = me_bench_us[way] = 0;
		}
		platform_log("GE: build 116 restorations, last 300 frames: see-through parts faded by angle %lu (%lu of them the level's); windows in planar fog %lu; meter parts %lu (%lu left out: map not loaded yet or no palette); see-through draws doubled %lu",
			seethrough_angle_draws, level_angle_draws, planar_fog_windows, meter_parts, meter_parts_dropped,
			seethrough_doubled_draws);
		seethrough_angle_draws = 0;
		level_angle_draws = 0;
		seethrough_doubled_draws = 0;
		if (seethrough_lit_draws || model_point_parts)
			platform_log("GE: after build 116, last 300 frames: glass parts lit by their model's light %lu; model parts lit by point lights %lu",
				seethrough_lit_draws, model_point_parts);
		seethrough_lit_draws = 0;
		model_point_parts = 0;
		/* build 118: transparent_cut's sphere test (the words read here too:
		until a part has been cut they are still -1) */
		if (tcut_full < 0)
			tcut_full = psp_keep("tcutfull");
		if (tcut_check < 0)
			tcut_check = psp_keep("tcutcheck");
		if (tcut_full)
			platform_log("GE: see-through parts to cut, last 300 frames: %lu, each checked triangle by triangle (tcutfull)",
				tcut_parts);
		else if (tcut_check)
			platform_log("GE: see-through parts to cut, last 300 frames: %lu; wholly in view by their model's sphere %lu, no sphere %lu; tcutcheck: of the %lu the sphere called in view, checked triangle by triangle anyway, %lu had a triangle outside",
				tcut_parts, tcut_inside, tcut_no_sphere, tcut_inside, tcut_mismatches);
		else
			platform_log("GE: see-through parts to cut, last 300 frames: %lu; wholly in view by their model's sphere %lu (left whole, no triangle checked), no sphere %lu",
				tcut_parts, tcut_inside, tcut_no_sphere);
		tcut_parts = tcut_no_sphere = tcut_inside = tcut_mismatches = 0;
		/* build 118: the see-through parts' node scans (transparent_part_multi) */
		platform_log("GE: see-through parts' node scans, last 300 frames: %lu scanned, %lu remembered%s%lu",
			multi_scans, multi_remembered, multi_check ? "; multicheck: remembered answers that differed " : "; ", multi_mismatches);
		multi_scans = multi_remembered = multi_mismatches = 0;
		planar_fog_windows = 0;
		meter_parts = meter_parts_dropped = 0;
		light_index_triangles = light_nudged_draws = lights_skipped = 0;
		bake_substituted = bake_pieces_drawn = bake_layer_draws = bake_layer_triangles = bake_old_path_triangles = 0;
		compact_draws = 0;
		/* build 90 */
		platform_log("GE: permanent decals (%s): %lu drawn on the level's own triangles (%lu triangles), %lu the usual way (last 300 frames); planes scanned %lu, from memory %lu; %lu us a frame",
			decal_project ? "on the level's triangles" : "nudged, as before", decals_projected, decal_projected_triangles,
			decals_project_fallback, decal_cache_scans, decal_cache_uses, decal_project_us / 300);
		decals_projected = decal_projected_triangles = decals_project_fallback = 0;
		decal_cache_scans = decal_cache_uses = decal_project_us = 0;
		/* build 113 */
		if (contrails_queued || contrails_out_of_view)
			platform_log("GE: contrails (contrails): %lu queued, %lu segments; %lu pairs of corners folded away (behind the "
				"near plane or past the GE's range), %lu contrails wholly out of view (last 300 frames)", contrails_queued,
				contrail_segments, contrail_pairs_folded, contrails_out_of_view);
		contrails_queued = contrail_segments = contrail_pairs_folded = contrails_out_of_view = 0;
		if (zoom_masks || zoom_masks_missing)
			platform_log("GE: zoom masks (zoommask): %lu drawn, %lu not in the texture pack (last 300 frames)", zoom_masks,
				zoom_masks_missing);
		zoom_masks = zoom_masks_missing = 0;
		if (me_classify_jobs || me_classify_failures)
			platform_log("GE: ME classify (meclassify, measure only): %lu jobs, %lu vertices, the main CPU waited %lu us in all, %lu answers differing, %lu failed",
				me_classify_jobs, me_classify_vertices, me_classify_wait_us, me_classify_mismatches, me_classify_failures);
		/* build 87: the like-for-like of the two measure jobs (ME vs main CPU,
		the same function on the same data, both starting with the data out of
		their caches) */
		if (me_classify_jobs || me_angle_jobs || me_angle_failures)
		{
			platform_log("GE: ME measure (meclassify): classify %lu jobs, %lu vertices: the ME's code %lu us (%lu ns a vertex), the main CPU's %lu us (%lu ns a vertex), answers differing %lu; slant buckets %lu jobs, %lu triangles: the ME's code %lu us (%lu ns a triangle), the main CPU's %lu us (%lu ns a triangle), answers differing %lu, %lu failed; the main CPU waited %lu us for both",
				me_classify_jobs, me_classify_vertices, me_classify_me_us,
				me_classify_vertices ? (unsigned long)(1000ull * me_classify_me_us / me_classify_vertices) : 0,
				me_classify_sc_us, me_classify_vertices ? (unsigned long)(1000ull * me_classify_sc_us / me_classify_vertices) : 0,
				me_classify_sc_mismatches, me_angle_jobs, me_angle_triangles, me_angle_me_us,
				me_angle_triangles ? (unsigned long)(1000ull * me_angle_me_us / me_angle_triangles) : 0,
				me_angle_sc_us, me_angle_triangles ? (unsigned long)(1000ull * me_angle_sc_us / me_angle_triangles) : 0,
				me_angle_mismatches, me_angle_failures, me_classify_wait_us + me_angle_wait_us);
			me_classify_jobs = me_classify_vertices = me_classify_me_us = me_classify_sc_us = me_classify_sc_mismatches = 0;
			me_classify_wait_us = me_classify_mismatches = me_classify_failures = 0;
			me_angle_jobs = me_angle_triangles = me_angle_me_us = me_angle_sc_us = me_angle_mismatches = 0;
			me_angle_failures = me_angle_wait_us = 0;
		}
		/* build 89: in two lines; the one line passed the log's 512
		characters and the peaks (the cut-corner one is the only sign that
		MAXIMUM_CUT_CORNERS ran out) never reached the log (review) */
		platform_log("GE: frame %lu, %lu level draws (%lu textured, %lu flat so far), %lu triangles; %lu cut up near the camera, %lu skipped; %lu model parts (%lu buffers converted, %lu again after the cache moved them); level streams %lu KB (%lu let go); detail passes %lu; peaks: list %d of %d bytes, cut corners %d of %d; looking (%.2f %.2f %.2f)",
			frames, draws_this_frame, textured_draws, flat_draws, triangles_this_frame, clipped_this_frame, culled_this_frame,
			model_draws_this_frame, converted_count, reconverted_count, stream_bytes / 1024, streams_let_go, detail_passes,
			list_peak, display_list_size, clip_peak, MAXIMUM_CUT_CORNERS, camera_forward[0], camera_forward[1], camera_forward[2]);
		/* build 111 */
		if (arena_state > 0)
			platform_log("GE: the copies' arena: %lu of %u KB used, largest free %u KB; let go for room %lu; pieces to the heap %lu (%lu there now); compacted %lu times, %lu KB; brought back %lu",
				arena_used / 1024, ARENA_BYTES / 1024, arena_largest / 1024, arena_let_go, arena_from_heap, arena_heap_live,
				arena_compactions, arena_moved_kb, arena_migrated);
		platform_log("GE: widget sprites %lu (%lu begun, %lu untextured; %lu of %lu flare answers clear, %lu rays cast, %lu answers kept); change colour passes %lu, self-illumination %lu; meter palettes %lu worked out, %lu reused; model palettes %lu worked out, %lu reused; HUD quads %lu us a frame (with the see-through flush inside), characters %lu (picture changed %lu times)",
			widget_sprites, widget_begins, widget_untextured, occlusion_visible, occlusion_tests, psp_occlusion_rays_cast, psp_occlusion_rays_kept, change_passes, glow_passes, meter_worked, meter_reused, multipurpose_worked, multipurpose_reused, hud_quad_us / (frames ? frames : 1), text_us / (frames ? frames : 1), text_changes);
		if (frames > 1)
		{
			platform_log("GE: waited for the GE %lu us a frame (worst %lu) over the last 300 frames; frame end: cache write-back %lu us, buffer swap %lu, textures %lu; draws skipped with the display list full %lu in all; see-through parts placed by the CPU %lu; buffers on several nodes drawn rigid %lu; see-through flushes under the first-person projection %lu; HUD quads that left early with the depth test off %lu",
				ge_wait_us / 300, ge_wait_worst, frame_end_us[0] / 300, frame_end_us[1] / 300, frame_end_us[2] / 300,
				list_full_skips, transparent_placed, multi_node_rigid, first_person_flushes, screen_quad_bailed);
			ge_kinds_report(); /* build 103 */
			ge_reads_report(frames); /* build 115 */
			vm_report(); /* build 116 */
			/* build 112: every 300 frames, with the skinning plans' heap */
			platform_log("GE: converted-buffer table: %lu sweeps, %lu dead entries forgotten, %d in use of %d (next sweep at %d); skinning plans %lu KB on the heap (peak %lu KB)",
				converted_sweeps, converted_forgotten, converted_used, CONVERTED_BUFFERS,
				converted_sweep_late > 0 ? CONVERTED_BUFFERS * 3 / 4 : converted_sweep_mark,
				skin_live_bytes / 1024, skin_live_peak / 1024);
			converted_sweeps = converted_forgotten = 0;
			/* build 112: gedefer's 300 frames */
			if (defer_on)
				platform_log("GE: gedefer: %lu calls deferred, carried out in %lu runs (%lu when the buffer filled); ran as they came: %lu models with more than %d nodes, %lu calls too large",
					defer_records, defer_drains, defer_drains_full, defer_big_models, DEFER_MAX_NODES, defer_too_large);
			if (defer_on)
			{
				char sites[400];
				int length = 0, k;

				sites[0] = 0;
				for (k = 0; k < DEFER_SITES && defer_site[k]; k++)
				{
					length += snprintf(sites + length, sizeof(sites) - length, " %s %lu;", defer_site[k], defer_site_drains[k]);
					defer_site_drains[k] = 0;
					if (length >= (int)sizeof(sites) - 1)
						break;
				}
				platform_log("GE: gedefer: carried out for:%s", sites);
				if (defer_me_jobs || defer_me_failed)
					platform_log("GE: gemeon: %lu runs on the Media Engine (%lu failed): the main CPU waited %lu us a run, the ME's own time %lu us",
						defer_me_jobs, defer_me_failed, defer_me_jobs ? defer_me_wait_us / defer_me_jobs : 0,
						defer_me_jobs ? defer_me_own_us / defer_me_jobs : 0);
				defer_me_jobs = defer_me_failed = defer_me_wait_us = defer_me_own_us = 0;
				/* build 118: gemeoverlap. waited: the main CPU's time in
				psp_me_finish a half (simulated: the half's own time) */
				if (me_overlap_runs || me_overlap_alone)
					platform_log("GE: gemeoverlap: %lu halves carried out by the Media Engine while the main CPU wrote the next, the main CPU waited %lu us a half; %lu carried out on the main CPU (the ME couldn't take them)",
						me_overlap_runs, me_overlap_waits ? me_overlap_wait_us / me_overlap_waits : 0, me_overlap_alone);
				me_overlap_runs = me_overlap_waits = me_overlap_wait_us = me_overlap_alone = 0;
				if (walls_wanted_total || walls_wanted_dropped)
					platform_log("GE: gemesim: %lu see-through level triangles wanted asked by the Media Engine's work and asked at the frame's end, %lu more than the list held",
						walls_wanted_total, walls_wanted_dropped);
				if (converts_wanted_total || converts_wanted_dropped)
					platform_log("GE: gemesim: %lu model buffers wanted converted by the Media Engine's work and converted at the frame's end, %lu more than the list held",
						converts_wanted_total, converts_wanted_dropped);
				if (streams_wanted_total || streams_wanted_dropped)
					platform_log("GE: gemesim: %lu level copies wanted by the Media Engine's work and built at the frame's end, %lu more than the list held",
						streams_wanted_total, streams_wanted_dropped);
			}
			defer_records = defer_drains = defer_drains_full = defer_big_models = defer_too_large = 0;
			if (ge_skip || list_in_vram || vram_busy || ge_skipped_draws)
			{
				platform_log("GE: bench test: GE skipping %s; draws skipped %lu; display list in %s; VRAM-only load %s (%d sprites, %s)",
					ge_skip & GE_SKIP_ALL ? "every draw" : ge_skip == GE_SKIP_LEVEL ? "the level" : ge_skip == GE_SKIP_MODELS ? "models" : "nothing",
					ge_skipped_draws, list_in_vram ? "VRAM" : "RAM", vram_busy ? "on" : "off", vram_busy_sprites,
					vram_busy_calibrating ? "sizing, GE alone" : "held, overlapped");
				ge_skipped_draws = 0;
			}
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
				entry_snapshot = 0;
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
					platform_log("GE: angle buckets: %lu draws, %lu us a frame sorting (the bucket loop alone %lu); triangles by 1/cos (1, 1.4, 2, 2.8, 4, 5.7, 8): %lu%% %lu%% %lu%% %lu%% %lu%% %lu%% %lu%%; overlays: %lu asked of the collision (%lu on a wall, %lu free, %lu us in all; %d draws remembered, %lu replaced); slope nudges %lu, held to the lift limit (%d%% of the distance) %lu%s",
						angle_draws, angle_us / 300, angle_loop_us / 300, all ? angle_triangles[0] * 100 / all : 0, all ? angle_triangles[1] * 100 / all : 0,
						all ? angle_triangles[2] * 100 / all : 0, all ? angle_triangles[3] * 100 / all : 0,
						all ? angle_triangles[4] * 100 / all : 0, all ? angle_triangles[5] * 100 / all : 0,
						all ? angle_triangles[6] * 100 / all : 0, overlay_wall_asks, overlay_on_walls, overlay_free, overlay_ask_us, overlay_backing_count, overlay_backing_full, nudges_asked, nudge_percent, nudges_clamped,
						nudge_wide > 0 ? " (nudgewide: not held)" : "");
					nudges_asked = nudges_clamped = 0;
					platform_log("GE: near plane%s: %lu frames at x4 or more (x8 %lu, x16 %lu), %lu at x2, %lu at x1 (%lu with a model near enough to cap it), %lu us a frame deciding; level draws a frame: %lu wholly inside the view (no classifying), %lu classified; triangles past the detail fade %lu a frame, detail passes skipped for whole draws %lu a frame",
						near_auto ? " (nearauto)" : " (fixed)", near_auto_chosen[2], near_auto_chosen_high[0], near_auto_chosen_high[1], near_auto_chosen[1], near_auto_chosen[0],
						near_auto_by_objects, near_auto_frames ? near_auto_us / near_auto_frames : 0, level_draws_inside / 300,
						level_draws_classified / 300, angle_far_triangles / 300, detail_passes_skipped_far / 300);
					platform_log("GE: uv centring %s: %lu draws, largest shift %.0f repeats; GE serial %s",
						uv_centre > 0 ? "on" : "off", uv_centred_draws / 300, uv_shift_largest,
						ge_serial > 0 ? "on (the frame end's wait is the GE's whole busy time)" : "off");
					/* build 106: the near bending corners' VFPU check */
					if (skin_vfpu_check)
						platform_log("GE: bending corners near the eye on the %s; skinvfpucheck: %lu compared with the FPU, %lu verdicts differed",
							skin_vfpu > 0 ? "VFPU" : "FPU", skin_vfpu_corners, skin_vfpu_differ);
					/* build 107: the rigid near corners' one-product transform, checked */
					if (near_check > 0)
						platform_log("GE: nearcheck: %lu rigid corners near the eye compared with the two-step transform, %lu verdicts differed",
							near_check_corners, near_check_differ);
					uv_centred_draws = 0;
					uv_shift_largest = 0.0f;
					memset(angle_triangles, 0, sizeof(angle_triangles));
					angle_draws = angle_us = 0;
					angle_loop_us = 0;
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
					platform_log("GE: list: %lu regions a frame written cached (%lu KB a frame, %lu lines written back); command words %s (%lu lines a frame written back); stall updates %lu a frame, %lu passed on (%s), %.1f us each (%lu timed); decal quads under 2 pixels left out %lu, wholly off the screen %lu (since the start); decal calls with nothing on the screen %lu a frame",
						cached_takes / 300, cached_bytes / 300 / 1024, cached_lines_flushed / 300,
						list_cached_frame ? "cached (listcachedall)" : "uncached", list_lines_back / 300, stall_calls / 300,
						stall_forwarded / 300, stall_batch == 1 ? "batched" : "each", stall_timed ? (double)stall_timed_us / stall_timed : 0.0, stall_timed, decals_small, decals_offscreen,
						decals_wholly_left_out / 300);
					decals_wholly_left_out = 0;
					cached_takes = cached_bytes = cached_lines_flushed = stall_calls = stall_forwarded = 0;
					list_lines_back = 0;
					if (lit_memo > 0 || psp_keep("memotex"))
					{
						/* build 114: statememo (or one half: memotex, memolight) */
						extern unsigned long psp_tex_binds_memo, psp_tex_binds_sent;

						platform_log("GE: state memo: texture binds %lu a frame sent, %lu left out; model lighting (ambient and lights) %lu a frame set, %lu left out",
							psp_tex_binds_sent / 300, psp_tex_binds_memo / 300, lit_sets_sent / 300, lit_sets_memo / 300);
						psp_tex_binds_sent = psp_tex_binds_memo = lit_sets_sent = lit_sets_memo = 0;
					}
				}
			}
		}
	}
}

int psp_ge_active(void)
{
	return ge_ready;
}

/* ---------- build 87: the Media Engine's first render job, measure only
("meclassify" in the launch menu; off by default). Build 105: only in a
build made with PSP_DEBUG_TOOLS=1 (psp_build.py); elsewhere it does nothing.

The level draw's first CPU step is classifying each vertex against the
near plane and the guard band (to_camera + outside, ~1-3 ms a frame on the
PSP in b47). It is pure single-precision arithmetic on 12 bytes a vertex:
the shape of job the ME suits (no VFPU, no library calls, little memory
traffic per unit of work). Here the ME classifies the same vertices again,
after the main CPU has, and the two answers are compared: what it measures
is the ME's own time a vertex against the main CPU's, and whether the ME's
FPU gives the same bits. Nothing it computes is used yet, and the main CPU
waits for it, so it costs frame time while on: a measurement, not a speedup.
The step after this (if the numbers say so) hands the classification to the
ME for the whole visible set at the start of the level's drawing */
#if PSP_DEBUG_TOOLS
static unsigned char me_codes[65536 + 64] __attribute__((aligned(64)));
static float me_view[16] __attribute__((aligned(64)));
#endif
static int me_classify_on = -1;
static unsigned long me_classify_jobs, me_classify_vertices, me_classify_mismatches, me_classify_failures,
	me_classify_wait_us;


/* runs on the ME (or the main CPU when simulated): no system calls, no
library calls, no VFPU. args: the stream's vertices, first and last index,
the output, the view block (eye, right, up, forward, guard x and y, near) */
unsigned int psp_ge_me_classify(const unsigned int *args, unsigned int *results)
{
	const struct ge_vertex *base = (const struct ge_vertex *)args[0];
	unsigned int first = args[1], last = args[2], v;
	unsigned char *out = (unsigned char *)args[3];
	const float *view = (const float *)args[4];
	float ex = view[0], ey = view[1], ez = view[2];
	float rx = view[3], ry = view[4], rz = view[5], ux = view[6], uy = view[7], uz = view[8];
	float fx = view[9], fy = view[10], fz = view[11], gx = view[12], gy = view[13], near = view[14];

	for (v = first; v <= last; v++)
	{
		const struct ge_vertex *b = &base[v];
		float d0 = b->x - ex, d1 = b->y - ey, d2 = b->z - ez;
		float cx = d0 * rx + d1 * ry + d2 * rz, cy = d0 * ux + d1 * uy + d2 * uz, cz = d0 * fx + d1 * fy + d2 * fz;
		unsigned char bits = 0;

		if (cz < near) bits |= 1;
		if (cx > gx * cz) bits |= 2;
		if (cx < -gx * cz) bits |= 4;
		if (cy > gy * cz) bits |= 8;
		if (cy < -gy * cz) bits |= 16;
		out[v] = bits;
	}
	results[0] = last - first + 1;
	return last - first + 1;
}

/* runs on the ME (or the main CPU when simulated): args: the stream's
vertices, the triangles' indices, the count, the output (a bucket a
triangle), the view block (eye, detail fade squared), far possible */
unsigned int psp_ge_me_angle(const unsigned int *args, unsigned int *results)
{
	const struct ge_vertex *stream = (const struct ge_vertex *)args[0];
	const unsigned short *indices = (const unsigned short *)args[1];
	unsigned int count = args[2], t;
	unsigned char *out = (unsigned char *)args[3];
	const float *view = (const float *)args[4];
	int far_possible = (int)args[5];
	float ex = view[0], ey = view[1], ez = view[2], far_squared = view[3];

	for (t = 0; t < count; t++)
		out[t] = (unsigned char)angle_bucket_one(stream, indices + 3 * t, ex, ey, ez, far_possible, far_squared);
	results[0] = count;
	return count;
}

/* the main CPU's side of the slant measure: the same triangles on the ME,
its buckets against the main CPU's (angle_bucket_of), and the same function
again on the main CPU with the triangles' vertices out of its cache first */
#if PSP_DEBUG_TOOLS
static void me_angle_measure(const struct ge_vertex *stream, const unsigned short *indices, long count,
	int far_possible)
{
	extern int psp_me_running(void);
	extern int psp_me_run(int which, unsigned int type, const unsigned int *args, int arg_count, unsigned int *results,
		unsigned int first_look_us);
	extern unsigned int psp_me_last_job_us(int which);
	unsigned int args[6], results[8], start, lowest = 0xFFFF, highest = 0;
	long t;

	if (me_classify_on < 0)
		me_classify_on = psp_keep("meclassify");
	if (!me_classify_on || !psp_me_running() || count <= 0 || count > ANGLE_TRIANGLES)
		return;
	me_angle_view[0] = camera_eye[0];
	me_angle_view[1] = camera_eye[1];
	me_angle_view[2] = camera_eye[2];
	me_angle_view[3] = detail_far_squared;
	/* everything the ME reads (the vertices, a list written this frame, the
	view) to RAM; nothing of the output left in this cache */
	sceKernelDcacheWritebackAll();
	sceKernelDcacheInvalidateRange(me_buckets, (unsigned int)((count + 63) & ~63));
	args[0] = (unsigned int)stream;
	args[1] = (unsigned int)indices;
	args[2] = (unsigned int)count;
	args[3] = (unsigned int)me_buckets;
	args[4] = (unsigned int)me_angle_view;
	args[5] = (unsigned int)far_possible;
	start = sceKernelGetSystemTimeLow();
	if (!psp_me_run(1 /* the render slot */, 3 /* ME_JOB_ANGLE */, args, 6, results, 50))
	{
		me_angle_failures++;
		return;
	}
	me_angle_wait_us += sceKernelGetSystemTimeLow() - start;
	me_angle_me_us += psp_me_last_job_us(1);
	sceKernelDcacheInvalidateRange(me_buckets, (unsigned int)((count + 63) & ~63));
	me_angle_jobs++;
	me_angle_triangles += (unsigned long)count;
	for (t = 0; t < count; t++)
		if (me_buckets[t] != angle_bucket_of[t])
			me_angle_mismatches++;
	/* the main CPU, the same function, the vertices out of its cache first */
	for (t = 0; t < count * 3; t++)
	{
		if (indices[t] < lowest)
			lowest = indices[t];
		if (indices[t] > highest)
			highest = indices[t];
	}
	sceKernelDcacheWritebackInvalidateRange((void *)((unsigned int)&stream[lowest] & ~63u),
		(unsigned int)((highest - lowest + 1) * sizeof(struct ge_vertex) + 64));
	start = sceKernelGetSystemTimeLow();
	for (t = 0; t < count; t++)
		me_buckets[t] = (unsigned char)angle_bucket_one(stream, indices + 3 * t, me_angle_view[0], me_angle_view[1],
			me_angle_view[2], far_possible, me_angle_view[3]);
	me_angle_sc_us += sceKernelGetSystemTimeLow() - start;
}

/* the main CPU's side: after a draw's own classification, the same range on
the ME; the answers compared where the main CPU worked one out */
static void me_classify_measure(const struct level_stream *stream, unsigned int lowest, unsigned int highest)
{
	extern int psp_me_running(void);
	extern int psp_me_run(int which, unsigned int type, const unsigned int *args, int arg_count, unsigned int *results,
		unsigned int first_look_us);
	extern unsigned int psp_me_last_job_us(int which);
	unsigned int args[5], results[8], v, start;
	unsigned int line_first = lowest & ~63u, line_bytes = ((highest + 64) & ~63u) - line_first;

	if (me_classify_on < 0)
		me_classify_on = psp_keep("meclassify");
	if (!me_classify_on || !psp_me_running() || highest < lowest)
		return;
	me_view[0] = camera_eye[0]; me_view[1] = camera_eye[1]; me_view[2] = camera_eye[2];
	me_view[3] = camera_right[0]; me_view[4] = camera_right[1]; me_view[5] = camera_right[2];
	me_view[6] = camera_up[0]; me_view[7] = camera_up[1]; me_view[8] = camera_up[2];
	me_view[9] = camera_forward[0]; me_view[10] = camera_forward[1]; me_view[11] = camera_forward[2];
	me_view[12] = guard_x; me_view[13] = guard_y; me_view[14] = NEAR_CLIP;
	/* what the ME reads, to RAM (the view, and the vertices: a stream made
	this frame may not be there yet); no copy of what it writes left here */
	sceKernelDcacheWritebackAll();
	sceKernelDcacheWritebackInvalidateRange(me_codes + line_first, line_bytes);
	args[0] = (unsigned int)stream->base;
	args[1] = lowest;
	args[2] = highest;
	args[3] = (unsigned int)me_codes;
	args[4] = (unsigned int)me_view;
	start = sceKernelGetSystemTimeLow();
	if (!psp_me_run(1 /* the render slot */, 2 /* ME_JOB_CLASSIFY */, args, 5, results, 50))
	{
		me_classify_failures++;
		return;
	}
	me_classify_wait_us += sceKernelGetSystemTimeLow() - start;
	me_classify_me_us += psp_me_last_job_us(1);
	sceKernelDcacheInvalidateRange(me_codes + line_first, line_bytes);
	me_classify_jobs++;
	me_classify_vertices += highest - lowest + 1;
	for (v = lowest; v <= highest; v++)
		if (!(vertex_codes[v] & CODE_UNKNOWN) && vertex_codes[v] != me_codes[v])
			me_classify_mismatches++;
	/* build 87: the same function on the main CPU, the range out of its cache
	first (as the ME starts), into vertex_codes (done with for this draw: every
	user clears its range first) */
	sceKernelDcacheWritebackInvalidateRange((void *)((unsigned int)&stream->base[lowest] & ~63u),
		(highest - lowest + 1) * sizeof(struct ge_vertex) + 64);
	args[3] = (unsigned int)vertex_codes;
	start = sceKernelGetSystemTimeLow();
	psp_ge_me_classify(args, results);
	me_classify_sc_us += sceKernelGetSystemTimeLow() - start;
	for (v = lowest; v <= highest; v++)
		if (vertex_codes[v] != me_codes[v])
			me_classify_sc_mismatches++;
}
#else /* PSP_DEBUG_TOOLS: the instrument is left out (psp_build.py) */
static void me_angle_measure(const struct ge_vertex *stream, const unsigned short *indices, long count,
	int far_possible)
{
	(void)stream; (void)indices; (void)count; (void)far_possible;
}

static void me_classify_measure(const struct level_stream *stream, unsigned int lowest, unsigned int highest)
{
	(void)stream; (void)lowest; (void)highest;
}
#endif
