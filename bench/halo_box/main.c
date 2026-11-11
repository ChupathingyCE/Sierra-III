/*
halo_box: the box harness on the PSP (bench/halo_box/README.md). It runs one box
of the rebuild alone over a capture, in three modes, alone and beside the GE,
and writes ms0:/halo/log/box_<name>.txt.

Keep words (ms0:/halo/keep.txt):
  box<name>   the box to run (boxnull, boxselftest, ...); the null box if none
  ge<N>       also run every mode with the GE reading main RAM N% of the
              time (ge35); up to three; without one, alone only
  ticks<N>    stop after N ticks of the capture
  counters    one more pass in each mode with the CPU's own counters read
              either side of each entity (cycles, stalled cycles, misses):
              loads the bench's kernel module, halo_boxk
  spoil       boxselftest only: one output of its capture is written wrong,
              and the run must say which (the comparison's own proof)

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

void plat_out(const char *format, ...)
{
	va_list list;

	va_start(list, format);
	log_line("", format, list);
	va_end(list);
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
so the RAM one takes `duty` percent of a block's time when the GE has the bus
to itself: the GE stays busy throughout, and reads main RAM for that share.
Nothing wakes the CPU while the list runs. Each block opens with two no-ops;
writing FINISH and END over them stops the list at the next block. */
#define GE_LIST_BYTES (512 * 1024)
#define GE_BLOCKS_MOST 2048
#define RAM_SIDE 512
#define VRAM_LINES 256
static unsigned int __attribute__((aligned(64))) ge_list[GE_LIST_BYTES / 4];
static unsigned int *ge_stop[GE_BLOCKS_MOST];
static unsigned int ge_blocks;
static unsigned int *ram_texture;
#define VRAM_TEXTURE ((void *)0x04154000) /* after the three 512 x 272 buffers */
static float ram_us_a_line, vram_us_a_line;
static int ge_ready;
/* the span in flight: when its list started, what one of its blocks spends on VRAM alone, and what the last stop found */
static unsigned int ge_started;
static float ge_block_vram_us;
static double ge_met = -1;
/* a state word no sprite reads (the specular power: lighting is off) carries each block's number, so a stop can ask how far the list got */
#define GE_MARK 0x5b

struct ge_vertex { unsigned short u, v; short x, y, z; };

static void ge_sprite(int first_line, int lines)
{
	struct ge_vertex *v = (struct ge_vertex *)sceGuGetMemory(2 * sizeof(struct ge_vertex));

	v[0].u = 0; v[0].v = (unsigned short)first_line; v[0].x = 0; v[0].y = 0; v[0].z = 0;
	v[1].u = 480; v[1].v = (unsigned short)(first_line + lines); v[1].x = 480; v[1].y = (short)lines; v[1].z = 0;
	sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
}

static unsigned int ge_time(int from_ram, int sprites)
{
	unsigned int before;
	int k;

	sceGuStart(GU_DIRECT, ge_list);
	if (from_ram)
		sceGuTexImage(0, RAM_SIDE, RAM_SIDE, RAM_SIDE, ram_texture);
	else
		sceGuTexImage(0, 512, VRAM_LINES, 512, VRAM_TEXTURE);
	before = plat_now();
	for (k = 0; k < sprites; k++)
		ge_sprite(0, VRAM_LINES);
	sceGuFinish();
	sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
	return plat_now() - before;
}

static void ge_init(void)
{
	unsigned int k;

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
	ge_time(1, 4);
	ram_us_a_line = (float)ge_time(1, 32) / (32.0f * VRAM_LINES);
	vram_us_a_line = (float)ge_time(0, 32) / (32.0f * VRAM_LINES);
	plat_out("GE: a 480-pixel line textured from main RAM %.3f us, from VRAM %.3f us (the bus to itself)", (double)ram_us_a_line,
		(double)vram_us_a_line);
	ge_ready = ram_us_a_line > 0 && vram_us_a_line > 0;
	if (!ge_ready)
		plat_out("GE: its lists take no time here (an emulator?): the GE runs are left out");
}

void plat_ge_start(int duty, unsigned int span_us)
{
	/* the two sprites' lines: 256 between them, split so the RAM sprite's time is duty% of the block's */
	float ram_weight = (float)duty / ram_us_a_line, vram_weight = (float)(100 - duty) / vram_us_a_line;
	int ram_lines = (int)(VRAM_LINES * ram_weight / (ram_weight + vram_weight) + 0.5f), vram_lines, first = 0;
	float block_us;
	unsigned int blocks, k;
	static int told[101];

	if (!ge_ready)
		return;
	ram_lines = ram_lines < 1 ? 1 : ram_lines > VRAM_LINES ? VRAM_LINES : ram_lines;
	vram_lines = duty >= 100 ? 0 : VRAM_LINES - ram_lines < 1 ? 1 : VRAM_LINES - ram_lines;
	block_us = ram_lines * ram_us_a_line + vram_lines * vram_us_a_line;
	blocks = (unsigned int)((float)span_us / block_us) + 3;
	if (blocks > GE_BLOCKS_MOST)
		blocks = GE_BLOCKS_MOST;
	if (duty >= 0 && duty <= 100 && !told[duty])
	{
		told[duty] = 1;
		plat_out("GE %d%%: blocks of %d lines from main RAM and %d from VRAM, %.0f us a block, %.1f%% of it on main RAM", duty, ram_lines,
			vram_lines, (double)block_us, (double)(100.0f * ram_lines * ram_us_a_line / block_us));
	}
	ge_block_vram_us = vram_lines * vram_us_a_line;
	ge_started = plat_now();
	sceGuStart(GU_DIRECT, ge_list);
	for (k = 0; k < blocks; k++)
	{
		/* (the list is written through its uncached address, so the stop needs no write-back) */
		ge_stop[k] = (unsigned int *)(0x40000000u | ((unsigned int)ge_list + (unsigned int)sceGuCheckList()));
		sceGuSendCommandi(0, 0);
		sceGuSendCommandi(0, 0);
		sceGuSendCommandi(GE_MARK, (int)k);
		sceGuTexImage(0, RAM_SIDE, RAM_SIDE, RAM_SIDE, ram_texture);
		ge_sprite(first, ram_lines);
		first = (first + ram_lines) % (RAM_SIDE - VRAM_LINES); /* the reads move through the megabyte */
		if (vram_lines)
		{
			sceGuTexImage(0, 512, VRAM_LINES, 512, VRAM_TEXTURE);
			ge_sprite(0, vram_lines);
		}
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
		/* The blocks finished in the span, against what they take alone: the
		VRAM sprite's time doesn't stretch under the CPU's misses, the main-RAM
		one's does, so whatever of the span the VRAM sprites don't account for
		was spent on main RAM. */
		unsigned int span = plat_now() - ge_started, done = sceGeGetCmd(GE_MARK) & 0xffffffu;

		ge_met = busy && done >= 4 && span ? 100.0 * (1.0 - (double)done * ge_block_vram_us / (double)span) : -1;
	}
	for (k = 0; k < ge_blocks; k++)
	{
		ge_stop[k][0] = 0x0f000000u; /* FINISH */
		ge_stop[k][1] = 0x0c000000u; /* END */
	}
	sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
	return busy;
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

double plat_ge_met(void) { return ge_ready ? ge_met : -1; }

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
	int file_count, k, result, spoil = 0;

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
		else if (!strcmp(word, "counters"))
			options.counted = 1;
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
	if (options.counted)
		options.counted = counters_load();
	if (box == &box_selftest)
	{
		plat_step("writing the self-test box's capture%s", spoil ? ", one output spoiled (tick 210, entity 1013, element 3)" : "");
		sceIoMkdir("ms0:/halo/capture", 0777);
		if (!selftest_capture("ms0:/halo/capture/selftest.cap", 32, 40, spoil ? 7 : -1, 13))
			plat_out("the self-test capture couldn't be written");
	}
	file_count = box == &box_null ? 0 : capture_files();
	result = harness_run(box, files, file_count, &options);
	plat_step("halo_box: finished (%s)", result ? "not clean" : "clean");
	sceKernelDelayThread(2 * 1000 * 1000);
	sceKernelExitGame();
	return 0;
}
