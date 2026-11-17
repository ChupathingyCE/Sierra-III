/*
halo_box: the box harness on the PSP (bench/halo_box/README.md). It runs one box
of the rebuild alone over a capture, in three modes, alone and beside the GE,
and writes ms0:/halo/log/box_<name>.txt.

Keep words (ms0:/halo/keep.txt):
  box<name>   the box to run (boxnull, boxselftest, ...); the null box if none
  ge<N>       also run every mode with the GE reading main RAM N% of the
              time (ge35); up to three; without one, alone only
  gefake      the emulator only: the GE's three figures replaced by a PSP's,
              so the list's building and stopping run where the GE takes no time
  ticks<N>    stop after N ticks of the capture
  counters    one more pass in each mode with the CPU's own counters read
              either side of each entity (cycles, stalled cycles, misses):
              loads the bench's kernel module, halo_boxk
  spoil       boxselftest only: one output of its capture is written wrong,
              and the run must say which (the comparison's own proof)
  readtest    with the kernel module: 8 of the counters read one word each
              against the block, over the same work, and each read's cost
              (the frame picture's marks, docs/frame_picture.md §7)

The frame picture's prices are boxes with no capture: boxpriceimiss,
boxpricedclean, boxpriceddirty, boxpriceuncached (adapters/price.c), run
with counters.

Captures: every ms0:/halo/capture file ending .cap whose header names the
box, in name order. The null box runs without one, and the self-test box
writes its own (numbers only) to ms0:/halo/capture/selftest.cap first.

This file is the machine's side: the clock, the two ways of spoiling the
caches between entities, and the GE as a bus contender. harness.c is the rest.
*/
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <psppower.h>
#include <psputils.h>
#include <pspsdk.h>
#include <pspiofilemgr.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "harness.h"

PSP_MODULE_INFO("halo_box", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
/* all but 1 MB: a capture's static section and one tick are held in memory */
PSP_HEAP_SIZE_KB(-1024);

/* ---------- the report ---------- */

static char log_path[64] = "ms0:/halo/log/box.txt";
static char pending[8192];
static unsigned int pending_bytes;

static void log_flush(void)
{
	SceUID file;

	if (!pending_bytes)
		return;
	file = sceIoOpen(log_path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
	if (file >= 0)
	{
		sceIoWrite(file, pending, pending_bytes);
		sceIoClose(file);
	}
	pending_bytes = 0;
}

static void log_line(const char *prefix, const char *format, va_list list)
{
	char line[512];
	int length = snprintf(line, sizeof(line), "%s", prefix);

	length += vsnprintf(line + length, sizeof(line) - length - 2, format, list);
	if (length > (int)sizeof(line) - 2)
		length = sizeof(line) - 2;
	line[length++] = '\n';
	line[length] = 0;
	pspDebugScreenPrintf("%.134s%s", line, length > 134 ? "\n" : "");
	if (pending_bytes + length > sizeof(pending))
		log_flush();
	memcpy(pending + pending_bytes, line, length);
	pending_bytes += length;
}

/* on the stick before it returns, as a step is: a run that hangs or outlives
its wall loses nothing it printed (no line is printed inside a timed pass) */
void plat_out(const char *format, ...)
{
	va_list list;

	va_start(list, format);
	log_line("", format, list);
	va_end(list);
	log_flush();
}

/* a step's name is on the stick before the step starts: after a hang, the
log's last line says where */
void plat_step(const char *format, ...)
{
	va_list list;

	va_start(list, format);
	log_line("step: ", format, list);
	va_end(list);
	log_flush();
}

/* ---------- the clock and the caches ---------- */

unsigned int plat_now(void) { return sceKernelGetSystemTimeLow(); }

void plat_empty(void)
{
	sceKernelDcacheWritebackInvalidateAll();
	sceKernelIcacheInvalidateAll();
}

/* Other work's lines in every set of both caches, the data lines dirty: what
a box finds in play, where a miss's victim is usually someone else's line
and has to be written back first. Twice each cache's 16 KB, so both ways of
every set are taken. */
#define CROWD_BYTES 32768u
static unsigned int *crowd_data;
static void (*crowd_code)(void);

static void crowd_init(void)
{
	unsigned int *code = (unsigned int *)memalign(64, CROWD_BYTES), line;

	crowd_data = (unsigned int *)memalign(64, CROWD_BYTES);
	if (!code || !crowd_data)
	{
		plat_out("STOPPED: no memory for the crowded mode's walks");
		log_flush();
		sceKernelExitGame();
	}
	memset(code, 0, CROWD_BYTES); /* nops */
	/* a line's first instruction jumps to the next line's; the last returns */
	for (line = 0; line < CROWD_BYTES / 64; line++)
		code[line * 16] = line + 1 < CROWD_BYTES / 64 ? 0x08000000u | ((((unsigned int)code + (line + 1) * 64) & 0x0fffffffu) >> 2)
		                                              : 0x03e00008u;
	sceKernelDcacheWritebackRange(code, CROWD_BYTES);
	sceKernelIcacheInvalidateRange(code, CROWD_BYTES);
	crowd_code = (void (*)(void))code;
}

void plat_crowd(void)
{
	unsigned int *word = crowd_data, *end = crowd_data + CROWD_BYTES / 4;

	for (; word < end; word += 16)
		*word = (unsigned int)word;
	crowd_code();
}

/* ---------- the GE beside ---------- */

/* Blocks of two sprites, one textured from main RAM and one from VRAM, sized
so the main-RAM one takes `duty` percent of a block's time when the GE has the
bus to itself: the GE stays busy throughout, and reads main RAM for that
share. Nothing wakes the CPU while the list runs. Each block opens with two
no-ops; writing FINISH and END over them stops the list at the next block.

A block is about 200 us, far shorter than a pass over a tick: with long
blocks a short pass sits wholly inside one sprite (the first run on a PSP had
10 ms blocks, and its "50%" passes ran beside the main-RAM sprite alone). */
#define GE_LIST_BYTES (512 * 1024)
#define GE_BLOCKS_MOST 2048
#define GE_BLOCK_US 200.0f
#define RAM_SIDE 512
#define VRAM_LINES 256
static unsigned int __attribute__((aligned(64))) ge_list[GE_LIST_BYTES / 4];
static unsigned int *ge_stop[GE_BLOCKS_MOST];
static unsigned int ge_blocks;
static unsigned int *ram_texture;
#define VRAM_TEXTURE ((void *)0x04154000) /* after the three 512 x 272 buffers */
/* measured at the start, the bus to itself: a pixel textured from each place, and a block's two sprites when they draw nothing */
static float ram_us_a_pixel, vram_us_a_pixel, block_overhead_us;
static int ge_ready, ge_fake;
/* a duty's block, tuned on its first use: the two sprites' shapes, and what a block takes alone, in all and off main RAM */
static struct ge_shape { int tuned, ram_lines, ram_width, vram_lines, vram_width; float block_us, other_us; } ge_shapes[101];
/* the span in flight: when its list started, its duty, and what the last stop found */
static unsigned int ge_started;
static int ge_duty;
static double ge_span_us, ge_other_us;
static int ge_known;
/* a state word no sprite reads (the specular power: lighting is off) carries each block's number, so a stop can ask how far the list got */
#define GE_MARK 0x5b

struct ge_vertex { unsigned short u, v; short x, y, z; };

static void ge_sprite(int first_line, int lines, int width)
{
	struct ge_vertex *v = (struct ge_vertex *)sceGuGetMemory(2 * sizeof(struct ge_vertex));

	v[0].u = 0; v[0].v = (unsigned short)first_line; v[0].x = 0; v[0].y = 0; v[0].z = 0;
	v[1].u = (unsigned short)width; v[1].v = (unsigned short)(first_line + lines); v[1].x = (short)width; v[1].y = (short)lines; v[1].z = 0;
	sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
}

/* a sprite of about `pixels`: as many 480-pixel lines as it takes, the last share spread over them */
static void ge_shape(float pixels, int *lines, int *width)
{
	int whole = pixels < 1 ? 1 : pixels > 480.0f * VRAM_LINES ? 480 * VRAM_LINES : (int)(pixels + 0.5f);

	*lines = (whole + 479) / 480;
	*width = whole / *lines;
	if (*width < 1)
		*width = 1;
}

/* one block: its number, the main-RAM sprite, the VRAM one (0 lines: none) */
static void ge_block(unsigned int number, int *first, int ram_lines, int ram_width, int vram_lines, int vram_width)
{
	sceGuSendCommandi(GE_MARK, (int)number);
	sceGuTexImage(0, RAM_SIDE, RAM_SIDE, RAM_SIDE, ram_texture);
	ge_sprite(*first, ram_lines, ram_width);
	*first = (*first + ram_lines) % (RAM_SIDE - VRAM_LINES); /* the reads move through the megabyte */
	if (vram_lines)
	{
		sceGuTexImage(0, 512, VRAM_LINES, 512, VRAM_TEXTURE);
		ge_sprite(0, vram_lines, vram_width);
	}
}

/* microseconds for `blocks` blocks of the given shape, waited for */
static unsigned int ge_time(unsigned int blocks, int ram_lines, int ram_width, int vram_lines, int vram_width)
{
	unsigned int before, k;
	int first = 0;

	sceGuStart(GU_DIRECT, ge_list);
	before = plat_now();
	for (k = 0; k < blocks; k++)
		ge_block(k, &first, ram_lines, ram_width, vram_lines, vram_width);
	sceGuFinish();
	sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
	return plat_now() - before;
}

static void ge_init(void)
{
	unsigned int k;
	float small, big_ram, big_vram;

	ram_texture = (unsigned int *)memalign(64, RAM_SIDE * RAM_SIDE * 4);
	if (!ram_texture)
	{
		plat_out("GE: no memory for the main-RAM texture; the GE runs are left out");
		return;
	}
	for (k = 0; k < RAM_SIDE * RAM_SIDE; k++)
		ram_texture[k] = k * 2654435761u;
	sceKernelDcacheWritebackInvalidateRange(ram_texture, RAM_SIDE * RAM_SIDE * 4);
	for (k = 0; k < 512 * VRAM_LINES; k++)
		((unsigned int *)(0x40000000u | (unsigned int)VRAM_TEXTURE))[k] = k * 2246822519u;
	sceGuInit();
	sceGuStart(GU_DIRECT, ge_list);
	/* drawn where nothing is shown, so the screen's text stays */
	sceGuDrawBuffer(GU_PSM_8888, (void *)0x88000, 512);
	sceGuDispBuffer(480, 272, (void *)0, 512);
	sceGuDepthBuffer((void *)0x110000, 512);
	sceGuOffset(2048 - 240, 2048 - 136);
	sceGuViewport(2048, 2048, 480, 272);
	sceGuScissor(0, 0, 480, 272);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuDisable(GU_DEPTH_TEST);
	sceGuDepthMask(GU_TRUE);
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(GU_PSM_8888, 0, 0, 0);
	sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
	sceGuTexFilter(GU_NEAREST, GU_NEAREST);
	sceGuFinish();
	sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
	ge_time(64, 1, 1, 1, 1);
	/* 256 blocks whose sprites are one pixel each, then the same with 16 lines more of one sprite or the other */
	small = (float)ge_time(256, 1, 1, 1, 1);
	big_ram = (float)ge_time(256, 17, 480, 1, 1);
	big_vram = (float)ge_time(256, 1, 1, 17, 480);
	block_overhead_us = small / 256.0f;
	ram_us_a_pixel = (big_ram - small) / (256.0f * 16 * 480);
	vram_us_a_pixel = (big_vram - small) / (256.0f * 16 * 480);
	plat_out("GE: a pixel textured from main RAM %.4f us, from VRAM %.4f us; a block's two sprites drawing nothing %.1f us (the bus to itself)",
		(double)ram_us_a_pixel, (double)vram_us_a_pixel, (double)block_overhead_us);
	if (ge_fake)
	{
		/* the list's building, stopping and waiting exercised where the GE takes no time: a PSP-3000's figures stand in */
		ram_us_a_pixel = 0.33f;
		vram_us_a_pixel = 0.049f;
		block_overhead_us = 15.0f;
		plat_out("GE: gefake: those three replaced by 0.33, 0.049 and 15; nothing beside this GE is a measurement");
	}
	/* (a PSP's are 0.33 and 0.05: under a hundredth of that is no GE at all, and sprites sized from it would never end) */
	ge_ready = ram_us_a_pixel > 0.003f && vram_us_a_pixel > 0.0005f;
	if (!ge_ready)
		plat_out("GE: its sprites take no time here (an emulator?): the GE runs are left out");
}

/* A sprite's cost is not its pixels times a rate: small sprites cost less a
pixel than the big ones the rates were taken from (the first PSP run with
200 us blocks finished them in ~120). So a duty's block is tuned by timing
it: the whole block, and the block with its main-RAM sprite shrunk to one
pixel, whose difference is the time on main RAM; the two sprites are resized
until that is the duty asked for. */
static void ge_tune(int duty)
{
	struct ge_shape *g = &ge_shapes[duty];
	float target = block_overhead_us * 4 > GE_BLOCK_US ? block_overhead_us * 4 : GE_BLOCK_US;
	float ram_target = target * (float)duty / 100.0f, other_target = target - ram_target;
	float ram_pixels = ram_target / ram_us_a_pixel;
	float vram_pixels = other_target > block_overhead_us ? (other_target - block_overhead_us) / vram_us_a_pixel : 0;
	int round;

	g->tuned = 1;
	for (round = 0; round < 5; round++)
	{
		float whole, other, ram;

		ge_shape(ram_pixels, &g->ram_lines, &g->ram_width);
		g->vram_lines = g->vram_width = 0;
		if (duty < 100 && vram_pixels >= 1)
			ge_shape(vram_pixels, &g->vram_lines, &g->vram_width);
		if (ge_fake)
		{
			g->other_us = block_overhead_us + g->vram_lines * g->vram_width * vram_us_a_pixel;
			g->block_us = g->other_us + g->ram_lines * g->ram_width * ram_us_a_pixel;
			break;
		}
		whole = (float)ge_time(128, g->ram_lines, g->ram_width, g->vram_lines, g->vram_width) / 128.0f;
		other = (float)ge_time(128, 1, 1, g->vram_lines, g->vram_width) / 128.0f;
		ram = whole - other;
		g->block_us = whole;
		g->other_us = other;
		if (whole <= 0 || ram <= 0)
			break;
		if (100.0f * ram / whole > (float)duty - 2 && 100.0f * ram / whole < (float)duty + 2 && whole > target * 0.7f && whole < target * 1.4f)
			break;
		ram_pixels *= ram_target / ram;
		if (duty < 100)
			vram_pixels = other > block_overhead_us + 1 ? vram_pixels * (other_target - block_overhead_us) / (other - block_overhead_us)
			                                              : vram_pixels * 2 + 64;
	}
	plat_out("GE %d%%: blocks of %d x %d pixels from main RAM and %d x %d from VRAM; timed alone %.0f us a block, %.0f of it off main RAM: %.1f%% on main RAM (%d rounds of tuning); at most %.0f ms a list",
		duty, g->ram_width, g->ram_lines, g->vram_width, g->vram_lines, (double)g->block_us, (double)g->other_us,
		g->block_us > 0 ? (double)(100.0f * (g->block_us - g->other_us) / g->block_us) : 0.0, round < 5 ? round + 1 : 5,
		(double)(GE_BLOCKS_MOST * g->block_us / 1000.0f));
}

void plat_ge_start(int duty, unsigned int span_us)
{
	struct ge_shape *g;
	unsigned int blocks, k;
	int first = 0;

	if (!ge_ready || duty < 1 || duty > 100)
		return;
	g = &ge_shapes[duty];
	if (!g->tuned)
		ge_tune(duty);
	blocks = g->block_us > 0 ? (unsigned int)((float)span_us / g->block_us) + 3 : GE_BLOCKS_MOST;
	if (blocks > GE_BLOCKS_MOST)
		blocks = GE_BLOCKS_MOST;
	ge_duty = duty;
	ge_started = plat_now();
	sceGuStart(GU_DIRECT, ge_list);
	for (k = 0; k < blocks; k++)
	{
		/* (the list is written through its uncached address, so the stop needs no write-back) */
		ge_stop[k] = (unsigned int *)(0x40000000u | ((unsigned int)ge_list + (unsigned int)sceGuCheckList()));
		sceGuSendCommandi(0, 0);
		sceGuSendCommandi(0, 0);
		ge_block(k, &first, g->ram_lines, g->ram_width, g->vram_lines, g->vram_width);
	}
	ge_blocks = blocks;
	sceGuFinish();
}

int plat_ge_stop(void)
{
	unsigned int k;
	int busy;

	if (!ge_ready)
		return 1;
	busy = sceGeDrawSync(1) != PSP_GE_LIST_DONE;
	{
		/* The blocks finished in the span, against what such a block spends
		off main RAM when timed alone: the VRAM sprite and the block's overhead
		don't stretch under the CPU's misses, the main-RAM sprite does, so
		whatever of the span they don't account for was spent on main RAM. The
		harness sums both over a mode's passes. */
		unsigned int span = plat_now() - ge_started, done = sceGeGetCmd(GE_MARK) & 0xffffffu;

		ge_known = busy && span;
		ge_span_us = span;
		ge_other_us = (double)done * ge_shapes[ge_duty].other_us;
	}
	for (k = 0; k < ge_blocks; k++)
	{
		ge_stop[k][0] = 0x0f000000u; /* FINISH */
		ge_stop[k][1] = 0x0c000000u; /* END */
	}
	sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
	return busy;
}

int plat_ge_spent(double *span_us, double *other_us)
{
	*span_us = ge_span_us;
	*other_us = ge_other_us;
	return ge_ready && ge_known;
}

/* ---------- the CPU's counters ---------- */

extern unsigned char embedded_boxk[];
extern unsigned int embedded_boxk_len;
int halo_boxk_counters(unsigned int *out); /* the module's system call */

static int counters_load(void)
{
	static const char path[] = "ms0:/halo/halo_boxk.prx";
	SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777), module;
	unsigned int probe[COUNTERS], again[COUNTERS];
	volatile unsigned int spin;
	int written;

	if (file < 0)
		return plat_out("counters: %s can't be written (%08x)", path, (unsigned int)file), 0;
	written = sceIoWrite(file, embedded_boxk, embedded_boxk_len);
	sceIoClose(file);
	if (written != (int)embedded_boxk_len)
		return plat_out("counters: %s was cut short", path), 0;
	plat_step("loading the kernel module (%u bytes)", embedded_boxk_len);
	module = pspSdkLoadStartModule(path, PSP_MEMORY_PARTITION_KERNEL);
	if (module < 0)
		return plat_out("counters: the kernel module didn't load (%08x): the counted pass is left out", (unsigned int)module), 0;
	plat_step("the counters' first read");
	halo_boxk_counters(probe);
	for (spin = 0; spin < 10000; spin++)
		;
	halo_boxk_counters(again);
	plat_out("counters: on; %u cycles over a spin of 10,000 turns", again[C_CYCLES] - probe[C_CYCLES]);
	if (again[C_CYCLES] == probe[C_CYCLES])
		return plat_out("counters: they don't count here (an emulator?): the counted pass is left out"), 0;
	return 1;
}

void plat_counters(unsigned int out[COUNTERS]) { halo_boxk_counters(out); }

/* "readtest" (docs/frame_picture.md §7): the frame picture's marks read 8 of
the 19 counters, one word each. Two questions: does a word read alone give
the counts the block gives, over the same work; and what does a read cost.
The work is the price box's clean walk (adapters/price.c), 100 entities */
unsigned int halo_boxk_counter(int index); /* the module's second system call */

static const int readtest_words[8] = { C_CYCLES, C_STALL_MEMORY, C_SLEEP, C_I_MISS, C_D_MISS, C_D_WRITEBACK,
	C_UNCACHED_LOAD, C_UNCACHED_STORE };

static void readtest(void)
{
	unsigned int block_before[COUNTERS], block_after[COUNTERS], single_before[8], single_after[8];
	unsigned int start, block_us, single_us, k, round;
	char line[512];
	int at;

	for (round = 0; round < 3; round++)
	{
		halo_boxk_counters(block_before);
		for (k = 0; k < 100; k++)
			box_price_dclean.run((int)k);
		halo_boxk_counters(block_after);
		for (k = 0; k < 8; k++)
			single_before[k] = halo_boxk_counter(readtest_words[k]);
		for (k = 0; k < 100; k++)
			box_price_dclean.run((int)k);
		for (k = 0; k < 8; k++)
			single_after[k] = halo_boxk_counter(readtest_words[k]);
		at = snprintf(line, sizeof(line), "readtest: round %u, the same work (100 clean walks) read by the block, then by single words:", round + 1);
		for (k = 0; k < 8 && at < (int)sizeof(line) - 40; k++)
			at += snprintf(line + at, sizeof(line) - at, " [%d] %u / %u", readtest_words[k],
				block_after[readtest_words[k]] - block_before[readtest_words[k]], single_after[k] - single_before[k]);
		plat_out("%s", line);
	}
	start = sceKernelGetSystemTimeLow();
	for (k = 0; k < 10000; k++)
		halo_boxk_counters(block_before);
	block_us = sceKernelGetSystemTimeLow() - start;
	start = sceKernelGetSystemTimeLow();
	for (k = 0; k < 10000; k++)
		for (round = 0; round < 8; round++)
			single_before[round] = halo_boxk_counter(readtest_words[round]);
	single_us = sceKernelGetSystemTimeLow() - start;
	plat_out("readtest: a read of all 19 (one system call) %.3f us; 8 single words (8 system calls) %.3f us, %.3f us each",
		block_us / 10000.0, single_us / 10000.0, single_us / 80000.0);
}

/* ---------- keep words and captures ---------- */

static char keep_text[1024];

static void keep_read(void)
{
	SceUID file = sceIoOpen("ms0:/halo/keep.txt", PSP_O_RDONLY, 0);
	int length = 0, k;

	if (file >= 0)
	{
		length = sceIoRead(file, keep_text, sizeof(keep_text) - 1);
		sceIoClose(file);
	}
	keep_text[length < 0 ? 0 : length] = 0;
	for (k = 0; keep_text[k]; k++)
		if (keep_text[k] == '\n' || keep_text[k] == '\r' || keep_text[k] == '\t')
			keep_text[k] = ' ';
}

#define FILES_MOST 32
static char file_names[FILES_MOST][96];
static const char *files[FILES_MOST];

static int by_name(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }

static int capture_files(void)
{
	SceUID folder = sceIoDopen("ms0:/halo/capture");
	SceIoDirent entry;
	int count = 0, seen = 0, k;

	if (folder < 0)
		return plat_out("ms0:/halo/capture can't be opened (%08x)", (unsigned int)folder), 0;
	memset(&entry, 0, sizeof(entry));
	while (count < FILES_MOST && sceIoDread(folder, &entry) > 0)
	{
		size_t length = strlen(entry.d_name);

		seen++;
		/* (a stick's short names come back in capitals) */
		if (length > 4 && length < 64 && !strcasecmp(entry.d_name + length - 4, ".cap"))
			snprintf(file_names[count++], sizeof(file_names[0]), "ms0:/halo/capture/%s", entry.d_name);
		else if (entry.d_name[0] != '.')
			plat_out("ms0:/halo/capture: '%.60s' is not a capture by its name", entry.d_name);
		memset(&entry, 0, sizeof(entry));
	}
	sceIoDclose(folder);
	plat_out("ms0:/halo/capture: %d entries, %d of them captures", seen, count);
	qsort(file_names, count, sizeof(file_names[0]), by_name);
	for (k = 0; k < count; k++)
		files[k] = file_names[k];
	return count;
}

int main(void)
{
	struct harness_options options = { 0, { 0 }, 1, 256, 91, 0 };
	const struct box_adapter *box = &box_null;
	char *word, *rest = keep_text;
	int file_count, k, result, spoil = 0, read_test = 0;

	pspDebugScreenInit();
	scePowerSetClockFrequency(333, 333, 166);
	keep_read();
	while ((word = strtok(rest, " ")) != 0)
	{
		rest = 0;
		if (!strncmp(word, "box", 3))
		{
			for (k = 0; box_adapters[k]; k++)
				if (!strcmp(box_adapters[k]->name, word + 3))
					box = box_adapters[k];
		}
		else if (!strncmp(word, "ge", 2) && word[2] >= '0' && word[2] <= '9' && options.duty_count < DUTIES_MOST)
		{
			int duty = atoi(word + 2);

			if (duty >= 1 && duty <= 100)
				options.duties[options.duty_count++] = duty;
		}
		else if (!strncmp(word, "ticks", 5) && word[5] >= '0' && word[5] <= '9')
			options.ticks_most = atoi(word + 5);
		else if (!strcmp(word, "spoil"))
			spoil = 1;
		else if (!strcmp(word, "gefake"))
			ge_fake = 1;
		else if (!strcmp(word, "counters"))
			options.counted = 1;
		else if (!strcmp(word, "readtest"))
			read_test = 1;
	}
	sceIoMkdir("ms0:/halo", 0777);
	sceIoMkdir("ms0:/halo/log", 0777);
	snprintf(log_path, sizeof(log_path), "ms0:/halo/log/box_%s.txt", box->name);
	sceIoRemove(log_path);
	plat_step("halo_box: box %s; CPU %d MHz, bus %d MHz", box->name, scePowerGetCpuClockFrequency(), scePowerGetBusClockFrequency());
	crowd_init();
	if (options.duty_count > 1)
	{
		plat_step("the GE's set-up and its two rates");
		ge_init();
		if (!ge_ready)
			options.duty_count = 1;
	}
	if (options.counted || read_test)
	{
		int loaded = counters_load();

		if (read_test && loaded)
			readtest();
		options.counted = options.counted && loaded;
	}
	if (box == &box_selftest)
	{
		plat_step("writing the self-test box's capture%s", spoil ? ", one output spoiled (tick 210, entity 1013, element 3)" : "");
		sceIoMkdir("ms0:/halo/capture", 0777);
		if (!selftest_capture("ms0:/halo/capture/selftest.cap", 32, 40, spoil ? 7 : -1, 13))
			plat_out("the self-test capture couldn't be written");
	}
	file_count = box->no_capture ? 0 : capture_files();
	result = harness_run(box, files, file_count, &options);
	plat_step("halo_box: finished (%s)", result ? "not clean" : "clean");
	sceKernelDelayThread(2 * 1000 * 1000);
	sceKernelExitGame();
	return 0;
}
