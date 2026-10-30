/*
halo_bench v12: what the DMA controller costs the main CPU while it copies,
measured with repeats, and the GE and the Media Engine beside it for
comparison.

The question: big_levers.md ("The DMA controller") carries "while the DMA
controller copies, a CPU cache miss takes 499 ns instead of 218, and the DMA
drops to 64 MB/s", from one run of a bench whose source is gone, with no
repeats and no record of the block size or of how busy the DMA was. No Sony
or community document describes the bus arbiter, so only a measurement can
settle it.

The contenders, each running while the main CPU measures:
  - none (the baseline);
  - sceDmacMemcpy, RAM to RAM and RAM to VRAM, in blocks of 4 KB, 16, 64,
    256 KB and 1 MB, the DMA busy 100, 50 or 25% of the time. The call
    blocks its caller until the copy is done, so it runs on a thread of its
    own above the main thread's priority: the copy goes on while the main
    thread runs, and the next copy is issued the moment one ends;
  - the GE sampling a 1 MB texture in main RAM, list after list (the
    comparison an earlier bench measured: misses at 424 ns);
  - the Media Engine streaming (reads) or copying (reads and write-backs)
    main RAM. The ME doesn't issue the DMA itself: the system DMA controller
    sceDmacMemcpy drives is the kernel's, and the ME has no system calls;
    me-core's own DMA helpers drive a different controller (DMACplus, the
    main bus to the ME's side, the display's scan-out among its users), so
    they would answer another question. The ME as a bus contender of its own
    is the same question asked of the other CPU.

What the main CPU measures under each contender:
  - idle (asleep): the contender's own rate with the bus to itself;
  - dependent misses: a pointer chase through 4 MB of random 64-byte lines,
    far larger than the 16 KB data cache, so every load misses;
  - streaming reads: one load a line through 2 MB, in address order;
  - cache hits: a pointer chase inside 8 KB. A hit never reaches the bus, so
    a contender that slows the hits is taking the CPU itself (thread
    switches, interrupts), not the bus: that check is what separates the
    two. With small DMA blocks the contender's thread wakes thousands of
    times a second, and the hits show what that costs.

Each measurement runs for a window of 150 ms in chunks (1,000 misses, 2,048
lines, 20,000 hits), each chunk timed by the system clock
(sceKernelGetSystemTimeLow, 1 us). The figure is the mean of the middle half
of the chunks, so a chunk that a thread switch interrupted doesn't count
unless switches are in most chunks; the plain mean over the window, which
includes everything, is printed beside it. Every row runs three times, the
rounds apart (the whole sweep, then again), and the table gives each
figure's median and spread (largest less smallest) of the three. The CPU
runs at 333 MHz, the bus at 166 (the PSP-1000's stock clocks; never more).
MB is 2^20 bytes. The DMA's rate is bytes over the time inside the call;
its duty is that time over the window.

Results print on screen and go to ms0:/halo/log/bench12.txt, with the
game's own logs. When it's done it waits for the run's time limit rather
than quitting (a test harness that launched it may not take the console
back from a program that quits by itself); by hand, X exits.

The Media Engine part runs on mcidclan's me-core-mapper library (MIT,
copyright (c) 2026 mcidclan (m-c/d, m-cid),
https://github.com/mcidclan/psp-media-engine-custom-core), as halo_bench10
does.
*/

#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <psppower.h>
#include <psputils.h>
#include <pspdmac.h>
#include <pspgu.h>
#include <pspiofilemgr.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <me-core-mapper/me-core.h>

PSP_MODULE_INFO("halo_bench12", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(14 * 1024);

/* ---------- output, timing */
static const char *log_path = "ms0:/halo/log/bench12.txt";
static volatile u32 sink;

static void out(const char *format, ...)
{
	char line[400];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line) - 2, format, arguments);
	va_end(arguments);
	pspDebugScreenPrintf("%s\n", line);
	SceUID file = sceIoOpen(log_path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
	if (file >= 0)
	{
		strcat(line, "\n");
		sceIoWrite(file, line, strlen(line));
		sceIoClose(file);
	}
}

static inline u32 now_us(void)
{
	return sceKernelGetSystemTimeLow();
}

/* ---------- buffers */
#define CHASE_BYTES (4u * 1024 * 1024)
#define HIT_BYTES (8u * 1024)
#define STREAM_BYTES (2u * 1024 * 1024)
#define BLOCK_MAX (1024u * 1024)
#define TEX_SIDE 512
/* VRAM above the debug screen's frame (0 to 0x88000) and the GE's target
(0x88000 to 0x110000): a 1 MB block fits from 1 MB to the end of the 2 MB.
The GE and the DMA never run at once, so the overlap with the GE's unused
depth buffer doesn't matter */
#define VRAM_DMA 0x04100000u

static u32 *chase;      /* random 64-byte lines, a dependent chain */
static u32 *hits;       /* the same inside 8 KB: every load hits */
static u32 *streambuf;  /* read a line at a time in address order */
static u32 *dma_src;    /* the DMA's (and the ME's) source and destination */
static u32 *dma_dst;
static u32 *texture;    /* the GE samples it from main RAM */

/* a random cyclic chain through every line (each line holds the next's word
index): the LCG's multiplier is 1 mod 4 and its increment odd, so it visits
every line before it repeats */
static void build_chase(u32 *base, u32 bytes, u32 seed)
{
	u32 lines = bytes / 64, x = 0, i;

	for (i = 0; i < lines; i++)
	{
		u32 next = (x * 1103515245u + 12345u + seed * 64u) & (lines - 1);

		base[x * 16] = next * 16;
		x = next;
	}
}

/* ---------- the main CPU's measurements */
enum { W_IDLE, W_MISS, W_STREAM, W_HIT, W_COUNT };

#define WINDOW_US 150000u
#define MISS_CHUNK 1000u
#define HIT_CHUNK 20000u
#define STREAM_CHUNK_LINES 2048u
#define CHUNKS_MAX 4096
static u32 chunk_us[CHUNKS_MAX];
/* positions carried from chunk to chunk, so no chunk starts on lines a
previous one left in the cache */
static u32 chase_at, hit_at, stream_at;

static u32 run_chunk(int work)
{
	u32 start = now_us();

	if (work == W_MISS)
	{
		u32 i = chase_at, n = MISS_CHUNK;

		while (n--)
			i = *(volatile const u32 *)&chase[i];
		chase_at = i;
	}
	else if (work == W_HIT)
	{
		u32 i = hit_at, n = HIT_CHUNK;

		while (n--)
			i = *(volatile const u32 *)&hits[i];
		hit_at = i;
	}
	else
	{
		const volatile u32 *p = streambuf + stream_at;
		u32 n = STREAM_CHUNK_LINES, sum = 0;

		while (n--)
		{
			sum += p[0];
			p += 16;
		}
		sink = sum;
		stream_at += STREAM_CHUNK_LINES * 16;
		if (stream_at >= STREAM_BYTES / 4)
			stream_at = 0;
	}
	return now_us() - start;
}

static int compare_u32(const void *a, const void *b)
{
	u32 x = *(const u32 *)a, y = *(const u32 *)b;

	return x < y ? -1 : x > y;
}

/* ---------- the contenders */
enum { K_NONE, K_DMA_RAM, K_DMA_VRAM, K_GE, K_ME_IDLE, K_ME_STREAM, K_ME_COPY };

/* written only by the contender's thread, which runs above the main thread:
the main thread can't run in the middle of an update, so a snapshot is
always whole */
static volatile int contender_run;
static volatile u32 contender_units, contender_busy, contender_errors;
static volatile int contender_last_error;
/* a contender stops itself after this long: if a call ever kept the CPU
instead of sleeping, the main thread would never run to stop it */
static u32 contender_start, contender_life;
static void *dma_to;
static const void *dma_from;
static u32 dma_size, dma_duty;

static int contender_expired(void)
{
	return now_us() - contender_start > contender_life;
}

static int dma_thread(SceSize args, void *argp)
{
	int debt = 0;

	while (contender_run && !contender_expired())
	{
		u32 start = now_us(), busy;
		int result = sceDmacMemcpy(dma_to, dma_from, dma_size);

		busy = now_us() - start;
		if (result < 0)
		{
			contender_errors++;
			contender_last_error = result;
			sceKernelDelayThread(1000);
			continue;
		}
		contender_busy += busy;
		contender_units += dma_size;
		if (dma_duty < 100)
		{
			/* the idle time owed, slept once it's worth a sleep, the
			oversleep carried: the average duty comes out right even when
			the scheduler's sleeps are coarser than a small block's copy */
			debt += (int)(busy * (100 - dma_duty) / dma_duty);
			if (debt >= 200)
			{
				u32 slept = now_us();

				sceKernelDelayThread(debt);
				debt -= (int)(now_us() - slept);
				if (debt < -100000)
					debt = -100000;
			}
		}
	}
	return 0;
}

static unsigned int __attribute__((aligned(16))) ge_list[16384];
struct TexVertex
{
	unsigned short u, v;
	short x, y, z;
};
#define GE_SPRITES_A_LIST 8

static void ge_init(void)
{
	sceGuInit();
	sceGuStart(GU_DIRECT, ge_list);
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
	sceGuFinish();
	sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
}

/* full-screen sprites, the whole 512 x 512 texture squeezed onto each, so
every list pulls most of the texture's lines over the bus; drawn off screen
(the debug text stays readable) */
static int ge_thread(SceSize args, void *argp)
{
	while (contender_run && !contender_expired())
	{
		u32 start = now_us();
		int k;

		sceGuStart(GU_DIRECT, ge_list);
		sceGuTexMode(GU_PSM_8888, 0, 0, 0);
		sceGuTexImage(0, TEX_SIDE, TEX_SIDE, TEX_SIDE, texture);
		sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
		sceGuTexFilter(GU_NEAREST, GU_NEAREST);
		for (k = 0; k < GE_SPRITES_A_LIST; k++)
		{
			TexVertex *v = (TexVertex *)sceGuGetMemory(2 * sizeof(TexVertex));

			v[0].u = 0;
			v[0].v = 0;
			v[0].x = 0;
			v[0].y = 0;
			v[0].z = 0;
			v[1].u = TEX_SIDE;
			v[1].v = TEX_SIDE;
			v[1].x = 480;
			v[1].y = 272;
			v[1].z = 0;
			sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
		}
		sceGuFinish();
		sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
		contender_busy += now_us() - start;
		contender_units += GE_SPRITES_A_LIST;
	}
	return 0;
}

/* ============================================================ Media Engine */

/* shared, uncached. The main CPU sets op and arguments, then the sequence
number; the ME acknowledges (the same number in ack), works until stop, then
puts the number in idle */
meLibSetSharedUncached32(16);
#define me_sequence (meLibSharedMemory[0])
#define me_op       (meLibSharedMemory[1])
#define me_arg0     (meLibSharedMemory[2])
#define me_arg1     (meLibSharedMemory[3])
#define me_arg2     (meLibSharedMemory[4])
#define me_ack      (meLibSharedMemory[5])
#define me_count    (meLibSharedMemory[6])
#define me_stop     (meLibSharedMemory[7])
#define me_alive    (meLibSharedMemory[8])
#define me_halted   (meLibSharedMemory[9])
#define me_idle     (meLibSharedMemory[10])
#define me_result   (meLibSharedMemory[11])

enum { OP_STREAM = 1, OP_COPY, OP_HALT };
#define ME_BLOCK (64u * 1024)

/* ---------- the ME's side: no kernel calls, no library code but mecore's */
__attribute__((noinline, aligned(4)))
void meLibOnProcess(void)
{
	u32 seen = 0;

	meLibDcacheWritebackInvalidateAll();
	for (;;)
	{
		u32 sequence = me_sequence;

		if (sequence == seen)
		{
			u32 k;

			/* each poll is an uncached read and write on the bus: spaced,
			so the idle ME stays off it while the main CPU measures */
			me_alive++;
			for (k = 0; k < 2000; k++)
				__asm__ volatile("nop");
			continue;
		}
		seen = sequence;
		u32 op = me_op, a0 = me_arg0, a1 = me_arg1, a2 = me_arg2;

		if (op == OP_HALT)
		{
			me_ack = sequence;
			me_halted = 1;
			meLibHalt();
			continue;
		}
		me_count = 0;
		me_ack = sequence;
		{
			u32 offset = 0, blocks = 0, sum = 0;

			/* 64 KB at a time through a 1 MB buffer: every line misses the
			ME's cache, and the count moves often enough to time a window */
			while (!me_stop)
			{
				const u32 *from = (const u32 *)(a0 + offset);
				u32 k;

				if (op == OP_STREAM)
					for (k = 0; k < ME_BLOCK / 4; k++)
						sum += from[k];
				else
				{
					u32 *to = (u32 *)(a1 + offset);

					for (k = 0; k < ME_BLOCK / 4; k++)
						to[k] = from[k];
				}
				offset += ME_BLOCK;
				if (offset >= a2)
					offset = 0;
				me_count = ++blocks;
			}
			/* the sum must be used, or the compiler drops the reads */
			me_result = sum;
		}
		meLibDcacheWritebackInvalidateAll();
		me_idle = sequence;
	}
}

/* ---------- the main CPU's side */
static u32 me_sequence_sent = 0;

static void me_issue(u32 op, u32 a0, u32 a1, u32 a2)
{
	me_op = op;
	me_arg0 = a0;
	me_arg1 = a1;
	me_arg2 = a2;
	__asm__ volatile("sync");
	me_sequence = ++me_sequence_sent;
}

/* waits asleep, so the main CPU's polls stay off the bus; 0 after 3 s */
static int me_wait(int idle)
{
	u32 start = now_us();

	while ((idle ? me_idle : me_ack) != me_sequence_sent)
	{
		if (now_us() - start > 3000000)
			return 0;
		sceKernelDelayThread(100);
	}
	return 1;
}

/* ============================================================ the sweep */

struct Row
{
	int kind;
	u32 size, duty;
	char name[32];
	u32 errors;
	int last_error;
	int failed;
};
struct Window
{
	float cpu;       /* misses, hits: ns an access; stream: MB/s (the chunks' middle half) */
	float cpu_mean;  /* the same over the whole window */
	float rate;      /* the contender's own: MB/s (DMA, ME), sprites a second (GE) */
	float duty;      /* % of the window the contender was busy */
};
#define ROWS_MAX 40
#define ROUNDS 3
static Row rows[ROWS_MAX];
static int row_count;
static Window results[ROWS_MAX][W_COUNT][ROUNDS];

static int add_row(int kind, u32 size, u32 duty, const char *name)
{
	Row *row = &rows[row_count];

	memset(row, 0, sizeof(*row));
	row->kind = kind;
	row->size = size;
	row->duty = duty;
	snprintf(row->name, sizeof(row->name), "%s", name);
	return row_count++;
}

static void snapshot(int kind, u32 *units, u32 *busy)
{
	if (kind == K_ME_STREAM || kind == K_ME_COPY)
	{
		*units = me_count;
		*busy = now_us();
	}
	else
	{
		*units = contender_units;
		*busy = contender_busy;
	}
}

static void measure(int kind, int work, Window *result)
{
	u32 units0, busy0, units1, busy1, start, elapsed, chunks = 0, total = 0;

	memset(result, 0, sizeof(*result));
	if (work == W_HIT)
		run_chunk(W_HIT);
	snapshot(kind, &units0, &busy0);
	start = now_us();
	if (work == W_IDLE)
		sceKernelDelayThread(WINDOW_US);
	else
		while (now_us() - start < WINDOW_US)
		{
			u32 t = run_chunk(work);

			if (chunks < CHUNKS_MAX)
				chunk_us[chunks++] = t;
			total++;
		}
	elapsed = now_us() - start;
	snapshot(kind, &units1, &busy1);

	if (work != W_IDLE && chunks)
	{
		u32 low = chunks / 4, high = chunks - chunks / 4, k;
		double middle = 0;

		if (high <= low)
		{
			low = 0;
			high = chunks;
		}
		qsort(chunk_us, chunks, sizeof(chunk_us[0]), compare_u32);
		for (k = low; k < high; k++)
			middle += chunk_us[k];
		middle /= (high - low);
		if (work == W_MISS)
		{
			result->cpu = (float)(middle * 1000.0 / MISS_CHUNK);
			result->cpu_mean = (float)(elapsed * 1000.0 / ((double)total * MISS_CHUNK));
		}
		else if (work == W_HIT)
		{
			result->cpu = (float)(middle * 1000.0 / HIT_CHUNK);
			result->cpu_mean = (float)(elapsed * 1000.0 / ((double)total * HIT_CHUNK));
		}
		else
		{
			double bytes = STREAM_CHUNK_LINES * 64.0;

			result->cpu = middle > 0 ? (float)(bytes / middle * 1e6 / 1048576.0) : 0;
			result->cpu_mean = elapsed ? (float)(bytes * total / elapsed * 1e6 / 1048576.0) : 0;
		}
	}
	if (kind == K_DMA_RAM || kind == K_DMA_VRAM)
	{
		u32 bytes = units1 - units0, busy = busy1 - busy0;

		result->rate = busy ? (float)((double)bytes / busy * 1e6 / 1048576.0) : 0;
		result->duty = elapsed ? (float)(100.0 * busy / elapsed) : 0;
	}
	else if (kind == K_GE)
	{
		result->rate = elapsed ? (float)((double)(units1 - units0) * 1e6 / elapsed) : 0;
		result->duty = elapsed ? (float)(100.0 * (busy1 - busy0) / elapsed) : 0;
	}
	else if (kind == K_ME_STREAM || kind == K_ME_COPY)
	{
		u32 span = busy1 - busy0;

		result->rate = span ? (float)((double)(units1 - units0) * ME_BLOCK / span * 1e6 / 1048576.0) : 0;
		result->duty = 100;
	}
}

static void run_row(int index, int round)
{
	Row *row = &rows[index];
	SceUID thread = -1;
	int work;

	contender_units = contender_busy = contender_errors = 0;
	contender_last_error = 0;
	contender_run = 1;
	contender_start = now_us();
	contender_life = W_COUNT * WINDOW_US + 2000000;
	if (row->kind == K_DMA_RAM || row->kind == K_DMA_VRAM)
	{
		dma_to = row->kind == K_DMA_RAM ? (void *)dma_dst : (void *)VRAM_DMA;
		dma_from = dma_src;
		dma_size = row->size;
		dma_duty = row->duty;
		thread = sceKernelCreateThread("bench_dma", dma_thread, 0x18, 0x4000, PSP_THREAD_ATTR_USER, 0);
	}
	else if (row->kind == K_GE)
		thread = sceKernelCreateThread("bench_ge", ge_thread, 0x18, 0x10000, PSP_THREAD_ATTR_USER, 0);
	else if (row->kind == K_ME_STREAM || row->kind == K_ME_COPY)
	{
		me_stop = 0;
		me_issue(row->kind == K_ME_STREAM ? OP_STREAM : OP_COPY, (u32)dma_src, (u32)dma_dst, BLOCK_MAX);
		if (!me_wait(0))
		{
			row->failed = 1;
			out("%s: the ME did not start", row->name);
			return;
		}
	}
	if (row->kind == K_DMA_RAM || row->kind == K_DMA_VRAM || row->kind == K_GE)
	{
		if (thread < 0)
		{
			row->failed = 1;
			out("%s: no thread (%08x)", row->name, (unsigned)thread);
			return;
		}
		sceKernelStartThread(thread, 0, 0);
	}
	/* the contender under way before the first window */
	sceKernelDelayThread(3000);
	for (work = 0; work < W_COUNT; work++)
		measure(row->kind, work, &results[index][work][round]);
	contender_run = 0;
	if (thread >= 0)
	{
		SceUInt timeout = 3000000;

		if (sceKernelWaitThreadEnd(thread, &timeout) < 0)
			out("%s: the contender did not end", row->name);
		sceKernelDeleteThread(thread);
	}
	if (row->kind == K_ME_STREAM || row->kind == K_ME_COPY)
	{
		me_stop = 1;
		if (!me_wait(1))
			out("%s: the ME did not stop", row->name);
	}
	row->errors += contender_errors;
	if (contender_errors)
		row->last_error = contender_last_error;
	{
		Window *w = &results[index][W_IDLE][round];
		Window *m = &results[index][W_MISS][round], *s = &results[index][W_STREAM][round], *h = &results[index][W_HIT][round];

		out("r%d %-17s miss %6.1f (%6.1f) ns | stream %6.1f (%6.1f) MB/s | hit %5.2f (%5.2f) ns | own %6.1f/%6.1f/%6.1f/%6.1f, duty %3.0f/%3.0f/%3.0f/%3.0f%%%s",
			round + 1, row->name, m->cpu, m->cpu_mean, s->cpu, s->cpu_mean, h->cpu, h->cpu_mean, w->rate, m->rate, s->rate,
			h->rate, w->duty, m->duty, s->duty, h->duty, contender_errors ? " ERRORS" : "");
	}
}

static void median3(const float *v, float *median, float *spread)
{
	float a = v[0], b = v[1], c = v[2], t;

	if (a > b) { t = a; a = b; b = t; }
	if (b > c) { t = b; b = c; c = t; }
	if (a > b) { t = a; a = b; b = t; }
	*median = b;
	*spread = c - a;
}

static void cell(int row, int work, int field, float *median, float *spread)
{
	float v[ROUNDS];
	int r;

	for (r = 0; r < ROUNDS; r++)
	{
		const Window *w = &results[row][work][r];

		v[r] = field == 0 ? w->cpu : field == 1 ? w->cpu_mean : field == 2 ? w->rate : w->duty;
	}
	median3(v, median, spread);
}

static void print_table(int first, int last, int baseline)
{
	float base_miss, base_hit, base_stream, spread;
	int r;

	cell(baseline, W_MISS, 0, &base_miss, &spread);
	cell(baseline, W_HIT, 0, &base_hit, &spread);
	cell(baseline, W_STREAM, 0, &base_stream, &spread);
	out("contender         | miss ns (spread) xbase mean | stream MB/s (spread) xbase | hit ns (spread) xbase | own rate: idle, with misses, stream, hits | duty with misses");
	for (r = first; r <= last; r++)
	{
		float miss, miss_sp, miss_mean, mm_sp, stream, stream_sp, hit, hit_sp, idle_rate, ir_sp, miss_rate, mr_sp,
			stream_rate, sr_sp, hit_rate, hr_sp, duty, duty_sp;

		if (rows[r].failed)
		{
			out("%-17s | failed", rows[r].name);
			continue;
		}
		cell(r, W_MISS, 0, &miss, &miss_sp);
		cell(r, W_MISS, 1, &miss_mean, &mm_sp);
		cell(r, W_STREAM, 0, &stream, &stream_sp);
		cell(r, W_HIT, 0, &hit, &hit_sp);
		cell(r, W_IDLE, 2, &idle_rate, &ir_sp);
		cell(r, W_MISS, 2, &miss_rate, &mr_sp);
		cell(r, W_STREAM, 2, &stream_rate, &sr_sp);
		cell(r, W_HIT, 2, &hit_rate, &hr_sp);
		cell(r, W_MISS, 3, &duty, &duty_sp);
		out("%-17s | %6.1f (%5.1f) x%4.2f %6.1f | %6.1f (%5.1f) x%4.2f | %5.2f (%4.2f) x%4.2f | %6.1f (%5.1f), %6.1f (%5.1f), %6.1f, %6.1f | %3.0f%%%s",
			rows[r].name, miss, miss_sp, base_miss > 0 ? miss / base_miss : 0, miss_mean, stream, stream_sp,
			base_stream > 0 ? stream / base_stream : 0, hit, hit_sp, base_hit > 0 ? hit / base_hit : 0, idle_rate, ir_sp,
			miss_rate, mr_sp, stream_rate, hit_rate, duty, rows[r].errors ? " (DMA ERRORS)" : "");
		if (rows[r].errors)
			out("    %lu failed calls, the last %08x", (unsigned long)rows[r].errors, (unsigned)rows[r].last_error);
	}
}

/* the DMA's copies checked once, so a fast copy that silently copies
nothing can't pass for a result */
static void dma_check(void)
{
	const u32 bytes = 64 * 1024;
	u32 k, bad = 0, bad_vram = 0;
	int result, result_vram;
	const volatile u32 *vram = (const volatile u32 *)(0x40000000u | VRAM_DMA);

	for (k = 0; k < bytes / 4; k++)
		dma_src[k] = k * 2654435761u + 1;
	sceKernelDcacheWritebackInvalidateRange(dma_src, bytes);
	sceKernelDcacheWritebackInvalidateRange(dma_dst, bytes);
	result = sceDmacMemcpy(dma_dst, dma_src, bytes);
	for (k = 0; k < bytes / 4; k++)
		if (dma_dst[k] != k * 2654435761u + 1)
			bad++;
	result_vram = sceDmacMemcpy((void *)VRAM_DMA, dma_src, bytes);
	for (k = 0; k < bytes / 4; k++)
		if (vram[k] != k * 2654435761u + 1)
			bad_vram++;
	sceKernelDcacheWritebackInvalidateRange(dma_dst, bytes);
	out("DMA check, 64 KB: RAM to RAM returned %08x, %lu words wrong; RAM to VRAM returned %08x, %lu words wrong",
		(unsigned)result, (unsigned long)bad, (unsigned)result_vram, (unsigned long)bad_vram);
}

int main(void)
{
	static const u32 sizes[5] = {4096, 16384, 65536, 262144, 1048576};
	static const char *size_names[5] = {"4K", "16K", "64K", "256K", "1M"};
	static const u32 duties[3] = {100, 50, 25};
	SceCtrlData pad;
	int r, round, first_dma, last_ge, me_none, me_last, after_me, s, d;
	u32 bench_start;

	pspDebugScreenInit();
	scePowerSetClockFrequency(333, 333, 166);
	sceIoMkdir("ms0:/halo", 0777);
	sceIoMkdir("ms0:/halo/log", 0777);
	sceIoRemove(log_path);
	bench_start = now_us();
	out("==== halo_bench v12: the bus under DMA, the GE and the ME; CPU %d MHz, bus %d MHz ====",
		scePowerGetCpuClockFrequencyInt(), scePowerGetBusClockFrequencyInt());
	chase = (u32 *)memalign(64, CHASE_BYTES);
	hits = (u32 *)memalign(64, HIT_BYTES);
	streambuf = (u32 *)memalign(64, STREAM_BYTES);
	dma_src = (u32 *)memalign(64, BLOCK_MAX);
	dma_dst = (u32 *)memalign(64, BLOCK_MAX);
	texture = (u32 *)memalign(64, TEX_SIDE * TEX_SIDE * 4);
	if (!chase || !hits || !streambuf || !dma_src || !dma_dst || !texture)
		out("could not allocate the buffers");
	else
	{
		u32 k;

		build_chase(chase, CHASE_BYTES, 0);
		build_chase(hits, HIT_BYTES, 3);
		memset(streambuf, 1, STREAM_BYTES);
		memset(dma_dst, 0, BLOCK_MAX);
		for (k = 0; k < TEX_SIDE * TEX_SIDE; k++)
			texture[k] = k * 2654435761u;
		sceKernelDcacheWritebackInvalidateAll();
		dma_check();
		ge_init();

		add_row(K_NONE, 0, 0, "none");
		first_dma = row_count;
		for (s = 0; s < 5; s++)
			for (d = 0; d < 3; d++)
			{
				char name[32];

				snprintf(name, sizeof(name), "DMA RAM %s %lu%%", size_names[s], (unsigned long)duties[d]);
				add_row(K_DMA_RAM, sizes[s], duties[d], name);
			}
		for (s = 0; s < 5; s++)
			for (d = 0; d < 3; d++)
			{
				char name[32];

				snprintf(name, sizeof(name), "DMA VRAM %s %lu%%", size_names[s], (unsigned long)duties[d]);
				add_row(K_DMA_VRAM, sizes[s], duties[d], name);
			}
		last_ge = add_row(K_GE, 0, 0, "GE texturing");
		me_none = add_row(K_ME_IDLE, 0, 0, "none, ME polling");
		add_row(K_ME_STREAM, 0, 0, "ME streams RAM");
		me_last = add_row(K_ME_COPY, 0, 0, "ME copies RAM");
		after_me = add_row(K_NONE, 0, 0, "none, ME halted");
		(void)first_dma;

		out("-- the DMA and the GE: %d rows, %d rounds, %u ms a window --", last_ge + 1, ROUNDS, WINDOW_US / 1000);
		for (round = 0; round < ROUNDS; round++)
			for (r = 0; r <= last_ge; r++)
				run_row(r, round);

		out("-- the Media Engine (mecore) --");
		{
			int table = meLibDefaultInit();
			u32 alive = me_alive;

			out("meLibDefaultInit: %d; bus %d MHz", table, scePowerGetBusClockFrequencyInt());
			sceKernelDelayThread(100000);
			if (me_alive == alive)
			{
				out("the ME isn't running; skipping its rows");
				for (r = me_none; r <= me_last; r++)
					rows[r].failed = 1;
			}
			else
			{
				for (round = 0; round < ROUNDS; round++)
					for (r = me_none; r <= me_last; r++)
						run_row(r, round);
				me_stop = 1;
				me_issue(OP_HALT, 0, 0, 0);
				for (k = 0; k < 20 && !me_halted; k++)
					sceKernelDelayThread(50000);
				out("the ME %s", me_halted ? "stopped cleanly" : "did NOT acknowledge the stop");
			}
		}
		for (round = 0; round < ROUNDS; round++)
			run_row(after_me, round);

		out("");
		out("==== the table: median of %d rounds, spread = largest less smallest; x = against the row named as base ====", ROUNDS);
		out("-- base: none --");
		print_table(0, last_ge, 0);
		out("-- base: none, ME polling (the ME's rows measured after meLibDefaultInit) --");
		print_table(me_none, me_last, me_none);
		out("-- base: none (the start), to show the ME's set-up left the bus as it was --");
		print_table(after_me, after_me, 0);
		out("bench time %.1f s", (now_us() - bench_start) / 1e6);
	}
	out("done");
	for (;;)
	{
		sceCtrlPeekBufferPositive(&pad, 1);
		if (pad.Buttons & PSP_CTRL_CROSS)
			break;
		sceKernelDelayThread(100000);
	}
	sceKernelExitGame();
	return 0;
}
