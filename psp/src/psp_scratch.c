/* psp_scratch.c - the scratchpad (build 108)

The PSP-1000's CPU has 16 KB of SRAM at 0x00010000 beside it, off the system
bus: a load from it costs what a data-cache hit costs (24 ns), and it never
waits for the GE's memory traffic and never evicts anything from the 16 KB
data cache (halo_bench v9, psp_hardware_reference.md). Nothing in the game
used it before this build.

What goes there is decided by measurement: the emulator's cache model
(PSP_DCACHE, tools/ppsspp_local_patches.diff) counts every data miss by the
data it touched and the code that touched it (tools/cachemap.py), so a
table earns a place by the misses it takes per byte. docs/overnight_b108.md
has the figures.

It is plain memory to every thread on the CPU, saved by nothing: so the
space is handed out once, at start-up, in fixed pieces (psp_scratch_take),
each with one job, and never freed or shared as working space. The pieces
(b108, 15.1 of 16 KB): the near-cut corner table (psp_ge.c's model_cut,
8,448 bytes) and the profiler's slots and front table (psp_platform.c, 6,656;
the heartbeat thread reads them for its report, as it read the old table).

On by default ("noscratchbsp" off): the level collision BSP's top nodes take the cut
corners' room (below, psp_scratchbsp.h): the census by misses per byte puts
them first in the tick, and the cut corners last.

sp-plan (3) (form D, the lead 2026-11-27 08:0x): the profiler's 6,656 bytes
are held, untaken (SCRATCH_HELD_BYTES below); its tables come from the heap
under "timerson" only. "profscratch": back here, as before.

keep.txt "noscratch": every piece comes from the heap instead, for the
hardware A/B (the same code, the same data, only its place differs). */
#include <pspkernel.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void *memalign(size_t alignment, size_t size); /* newlib's; the SDK headers leave it out */
void platform_log(const char *format, ...);
int psp_keep(const char *name);

#define SCRATCH_BASE 0x00010000u
#define SCRATCH_BYTES 0x4000u

static unsigned int scratch_used = 0;
static int scratch_off = -1;

/* bytes from the scratchpad (16-byte aligned), or from the heap with
"noscratch" or when it is full; never freed. what: for the log */
void *psp_scratch_take(unsigned int bytes, const char *what)
{
	unsigned int at = (scratch_used + 15) & ~15u;
	void *pointer;

	if (scratch_off < 0)
		scratch_off = psp_keep("noscratch");
	if (!scratch_off && at + bytes <= SCRATCH_BYTES)
	{
		pointer = (void *)(SCRATCH_BASE + at);
		scratch_used = at + bytes;
		memset(pointer, 0, bytes);
		platform_log("scratchpad: %u bytes for %s at %08x (%u of %u used)", bytes, what, SCRATCH_BASE + at,
			scratch_used, SCRATCH_BYTES);
		return pointer;
	}
	pointer = memalign(16, bytes);
	if (pointer)
		memset(pointer, 0, bytes);
	platform_log("scratchpad: %u bytes for %s from the heap (%s)", bytes, what,
		scratch_off ? "noscratch" : "the scratchpad is full");
	/* named for the cache map (tools/cachemap.py): with noscratch every piece
	is ranked by its misses per byte where the cache sees it */
	if (pointer)
		platform_log("scratch span: %08x %u piece: %s", (unsigned int)pointer, bytes, what);
	return pointer;
}

/* keep.txt "scratchcensus": the level's collision BSP named for the cache
map, array by array, and how many 64-byte lines its top levels hold, so the
census can say what the walks' first nodes and planes would earn in the
scratchpad. Logged as each collision BSP is set (a map's first and every
switch). Only addresses and counts are logged, never the BSP's contents.
The layout is the engine's (collision_bsp_definitions.h, bsp3d.h): eight tag
blocks of {count, address, definition} */
struct scratch_block
{
	long count;
	void *address;
	void *definition;
};

struct scratch_bsp3d_node
{
	long plane_designator;
	long children[2];
};

static int scratch_census = -1;

/* the lines a depth adds, logged as the cache map's sets ("scratch topset:"
lines, 30 addresses a line) */
struct scratch_topset
{
	unsigned int lines[30];
	int count, depth;
	char kind;
};

static void scratch_topset_flush(struct scratch_topset *set)
{
	char text[400];
	int k, at = 0;

	if (!set->count)
		return;
	for (k = 0; k < set->count && at < (int)sizeof(text) - 12; k++)
		at += snprintf(text + at, sizeof(text) - at, k ? ",%08x" : "%08x", set->lines[k]);
	platform_log("scratch topset: %d %c %s", set->depth, set->kind, text);
	set->count = 0;
}

static unsigned int scratch_lines_mark(unsigned char *seen, unsigned int base, unsigned int address, unsigned int bytes,
	struct scratch_topset *set)
{
	unsigned int first = (address - base) >> 6, last = (address + bytes - 1 - base) >> 6, added = 0, k;

	for (k = first; k <= last; k++)
		if (!(seen[k >> 3] & (1u << (k & 7))))
		{
			seen[k >> 3] |= (unsigned char)(1u << (k & 7));
			added++;
			set->lines[set->count++] = base + (k << 6);
			if (set->count == 30)
				scratch_topset_flush(set);
		}
	return added;
}

void psp_scratch_census_bsp(const void *collision_bsp)
{
	static const char *const names[8] = {"bsp3d nodes", "bsp3d planes", "leaves", "bsp2d references", "bsp2d nodes",
		"surfaces", "edges", "vertices"};
	static const unsigned int sizes[8] = {12, 16, 8, 8, 20, 12, 24, 16};
	const struct scratch_block *blocks = (const struct scratch_block *)collision_bsp;
	const struct scratch_bsp3d_node *nodes;
	unsigned int node_base, plane_base, node_lines_total, plane_lines_total, node_lines = 0, plane_lines = 0;
	unsigned char *node_seen = NULL, *plane_seen = NULL;
	long *level = NULL, *next = NULL, level_count, next_count, total = 0, k;
	int depth;
	struct scratch_topset node_set, plane_set;

	if (scratch_census < 0)
		scratch_census = psp_keep("scratchcensus");
	if (!scratch_census || !blocks)
		return;
	for (k = 0; k < 8; k++)
		if (blocks[k].address && blocks[k].count > 0)
			platform_log("scratch span: %08x %u level bsp: %s", (unsigned int)blocks[k].address,
				(unsigned int)blocks[k].count * sizes[k], names[k]);
	nodes = (const struct scratch_bsp3d_node *)blocks[0].address;
	if (!nodes || blocks[0].count <= 0 || !blocks[1].address || blocks[1].count <= 0)
		return;
	node_base = (unsigned int)nodes & ~63u;
	plane_base = (unsigned int)blocks[1].address & ~63u;
	node_lines_total = ((unsigned int)nodes + blocks[0].count * 12u - node_base + 63) >> 6;
	plane_lines_total = ((unsigned int)blocks[1].address + blocks[1].count * 16u - plane_base + 63) >> 6;
	node_seen = calloc((node_lines_total + 7) / 8, 1);
	plane_seen = calloc((plane_lines_total + 7) / 8, 1);
	level = malloc(blocks[0].count * sizeof(long));
	next = malloc(blocks[0].count * sizeof(long));
	if (!node_seen || !plane_seen || !level || !next)
		goto done;
	/* breadth first from the root: depth d's line counts are what the top d
	levels would take, nodes and the planes they test */
	level[0] = 0;
	level_count = 1;
	node_set.count = plane_set.count = 0;
	node_set.kind = 'n';
	plane_set.kind = 'p';
	for (depth = 0; depth < 12 && level_count > 0; depth++)
	{
		next_count = 0;
		node_set.depth = plane_set.depth = depth;
		for (k = 0; k < level_count; k++)
		{
			const struct scratch_bsp3d_node *node = &nodes[level[k]];
			long plane = node->plane_designator & 0x7FFFFFFF, c;

			node_lines += scratch_lines_mark(node_seen, node_base, (unsigned int)node, 12, &node_set);
			if (plane < blocks[1].count)
				plane_lines += scratch_lines_mark(plane_seen, plane_base, (unsigned int)blocks[1].address + plane * 16u, 16,
					&plane_set);
			for (c = 0; c < 2; c++)
			{
				long child = node->children[c];

				if (child >= 0 && child < blocks[0].count && next_count < blocks[0].count)
					next[next_count++] = child;
			}
		}
		scratch_topset_flush(&node_set);
		scratch_topset_flush(&plane_set);
		total += level_count;
		platform_log("scratch census: level bsp3d depth %d: %ld nodes to here, %u node lines, %u plane lines (%u bytes) of %u and %u",
			depth, total, node_lines, plane_lines, (node_lines + plane_lines) * 64u, node_lines_total, plane_lines_total);
		{
			long *swap = level;

			level = next;
			next = swap;
			level_count = next_count;
		}
	}
done:
	free(node_seen);
	free(plane_seen);
	free(level);
	free(next);
}

/* whether a pointer is the scratchpad's (for the reports) */
int psp_scratch_holds(const void *pointer)
{
	unsigned int at = (unsigned int)pointer;

	return at >= SCRATCH_BASE && at < SCRATCH_BASE + SCRATCH_BYTES;
}

/* execution probe (keep.txt "spadexec"): can the CPU run code from the
scratchpad, and how fast? Nothing documents it (docs/research/
research_icache.md, open question 4). The tick's instruction misses are
mostly capacity misses (more code than the 16 KB cache holds), so code that
could run from the scratchpad without going through the instruction cache
would take the hottest few KB out of that competition. The same counting
loop runs from a heap buffer (through the cache) and from the scratchpad,
and the log says how long each took. A fault here leaves the "calling" line
last in the log and the crash handler's address in the scratchpad's range */
static const unsigned int probe_loop[] = {
	0x00001021, /* addu  v0, zero, zero */
	0x24420001, /* addiu v0, v0, 1 */
	0x1444fffe, /* bne   v0, a0, (the addiu) */
	0x00000000, /* nop */
	0x03e00008, /* jr    ra */
	0x00000000, /* nop */
};

static unsigned int probe_time(void *code, unsigned int count, unsigned int *result)
{
	unsigned int (*run)(unsigned int) = (unsigned int (*)(unsigned int))code;
	unsigned int start;

	(void)run(16); /* once to warm whichever cache it goes through */
	start = sceKernelGetSystemTimeLow();
	*result = run(count);
	return sceKernelGetSystemTimeLow() - start;
}

void psp_scratch_exec_probe(void)
{
	const unsigned int count = 3000000, bytes = sizeof(probe_loop);
	unsigned int ram_us, scratch_us, ram_result = 0, scratch_result = 0;
	void *ram, *scratch;

	if (!psp_keep("spadexec"))
		return;
	ram = memalign(64, 64);
	scratch = psp_scratch_take(64, "the execution probe");
	if (!ram || !psp_scratch_holds(scratch))
	{
		platform_log("spadexec: no room for the probe (heap %p, scratchpad %p)", ram, scratch);
		return;
	}
	memcpy(ram, probe_loop, bytes);
	memcpy(scratch, probe_loop, bytes);
	sceKernelDcacheWritebackAll();
	sceKernelIcacheInvalidateAll();
	ram_us = probe_time(ram, count, &ram_result);
	platform_log("spadexec: %u loops from main RAM (cached): %u us; calling the scratchpad copy at %p", count, ram_us, scratch);
	scratch_us = probe_time(scratch, count, &scratch_result);
	platform_log("spadexec: %u loops from the scratchpad: %u us (main RAM %u us); results %u and %u (3 instructions a loop: %.2f and %.2f cycles each at 333 MHz)",
		count, scratch_us, ram_us, scratch_result, ram_result,
		(double)scratch_us * 333.0 / (3.0 * count), (double)ram_us * 333.0 / (3.0 * count));
}

/* the second probe (keep.txt "spadexec2"): does code run from the
scratchpad go through the instruction cache? The first probe showed it runs
at cached speed, which a loop small enough to stay in the cache would show
either way. Two 12 KB straight-line routines, one in main RAM and one in the
scratchpad, are run in turn: if the scratchpad's fetches bypass the cache,
the main-RAM routine keeps its 12 KB of the 16 KB cache and stays fast; if
they are cached, the two (24 KB) evict each other every pass and the
main-RAM routine misses on every line. It takes 12 KB of the scratchpad
before the other pieces ask, so most of them go to the heap for that run */
#define PROBE2_WORDS 3072u

static void probe2_fill(unsigned int *code)
{
	unsigned int k;

	code[0] = 0x00001021; /* addu v0, zero, zero */
	for (k = 1; k < PROBE2_WORDS - 2; k++)
		code[k] = 0x24420001; /* addiu v0, v0, 1 */
	code[PROBE2_WORDS - 2] = 0x03e00008; /* jr ra */
	code[PROBE2_WORDS - 1] = 0x00000000; /* nop */
}

static unsigned int probe2_run(void *a, void *b, unsigned int passes)
{
	unsigned int (*run_a)(void) = (unsigned int (*)(void))a, (*run_b)(void) = (unsigned int (*)(void))b;
	unsigned int start = sceKernelGetSystemTimeLow(), k, sum = 0;

	for (k = 0; k < passes; k++)
	{
		sum += run_a();
		if (run_b)
			sum += run_b();
	}
	(void)sum;
	return sceKernelGetSystemTimeLow() - start;
}

void psp_scratch_exec_probe2(void)
{
	const unsigned int bytes = PROBE2_WORDS * 4u, passes = 200;
	unsigned int ram_alone, scratch_alone, both;
	void *ram, *scratch;

	if (!psp_keep("spadexec2"))
		return;
	ram = memalign(64, bytes);
	scratch = psp_scratch_take(bytes, "the second execution probe");
	if (!ram || !psp_scratch_holds(scratch))
	{
		platform_log("spadexec2: no room for the probe (heap %p, scratchpad %p)", ram, scratch);
		return;
	}
	probe2_fill((unsigned int *)ram);
	probe2_fill((unsigned int *)scratch);
	sceKernelDcacheWritebackAll();
	sceKernelIcacheInvalidateAll();
	(void)probe2_run(ram, NULL, 2);
	ram_alone = probe2_run(ram, NULL, passes);
	(void)probe2_run(scratch, NULL, 2);
	scratch_alone = probe2_run(scratch, NULL, passes);
	both = probe2_run(ram, scratch, passes);
	platform_log("spadexec2: %u passes of 12 KB: main RAM alone %u us, scratchpad alone %u us, both in turn %u us (alone summed %u us): %s",
		passes, ram_alone, scratch_alone, both, ram_alone + scratch_alone,
		both <= (ram_alone + scratch_alone) * 5 / 4 ? "the scratchpad's fetches leave the instruction cache alone" :
		"the scratchpad's fetches go through the instruction cache (the two evict each other)");
}

/* on by default, "noscratchbsp" off (psp_scratchbsp.h): the level collision BSP's top
nodes, each with its plane, in the room the profiler's tables leave; the
models' cut corners go to the heap (the census: the nodes nearest the root
take hundreds of misses a byte in the tick and the drawing, the cut corners
a few). The copy is made, breadth first from the root, as each collision
BSP is set (a map's first and every switch) and dropped as a map's tags
load, all on the game thread, the walks' only reader of it */
#include "psp_scratchbsp.h"

/* sp-plan (3) (Sheepdog01 2026-11-27 02:1x; form D, the lead 08:0x): room
this piece leaves and nothing takes, held for the passes' budgets, not given
to more nodes. It was the profiler's slots and front table (psp_platform.c:
512 of 12 bytes and 64 of 8), which now come from the heap, and only under
"timerson"; "profscratch" puts them back here, after this piece */
#define SCRATCH_HELD_BYTES 6656u

struct psp_sb_slot *psp_sb_slots = NULL;
void const *psp_sb_tree = NULL;
static unsigned int sb_room = 0;
/* the level's collision BSP as last set: a map's first is set before the
renderer starts and takes the room, so the take fills it then */
static const void *sb_level = NULL;
void psp_scratch_bsp_fill(const void *collision_bsp);

void psp_scratch_bsp_take(void)
{
	unsigned int at = (scratch_used + 15) & ~15u, bytes;
	void *pointer;

	if (psp_keep("noscratchbsp"))
		return;
#ifdef PSP_MAP_CHECKS
	/* the walks' node checks would refuse a copy's index: the walks read the tag data */
	platform_log("scratchbsp: left out in a PSP_MAP_CHECKS build");
	return;
#endif
	if (psp_keep("noscratch") || at + SCRATCH_HELD_BYTES + 64 * sizeof(struct psp_sb_slot) > SCRATCH_BYTES)
	{
		platform_log("scratchbsp: no room in the scratchpad: the walks read the tag data");
		return;
	}
	bytes = (SCRATCH_BYTES - SCRATCH_HELD_BYTES - at) / sizeof(struct psp_sb_slot) * sizeof(struct psp_sb_slot);
	pointer = psp_scratch_take(bytes, "the level BSP's top nodes");
	if (!psp_scratch_holds(pointer))
		return;
	psp_sb_slots = pointer;
	sb_room = bytes / sizeof(struct psp_sb_slot);
	if (sb_level)
		psp_scratch_bsp_fill(sb_level);
}

void psp_scratch_bsp_forget(void)
{
	psp_sb_tree = NULL;
	sb_level = NULL;
}

/* sp-plan (3): the room left untaken where the profiler's tables were (form
D), logged as held once the renderer's pieces are taken, so the start-up's
piece list shows it. Called by psp_profile_tables_take, unless "profscratch" */
void psp_scratch_held_log(void)
{
	unsigned int at = (scratch_used + 15) & ~15u;

	if (scratch_off < 0)
		scratch_off = psp_keep("noscratch");
	if (scratch_off || at >= SCRATCH_BYTES)
	{
		platform_log("scratchpad: none held (%s)", scratch_off ? "noscratch" : "the scratchpad is full");
		return;
	}
	platform_log("scratchpad: %u bytes held at %08x, for the passes' budgets (SCRATCH_HELD_BYTES %u; %u of %u used)",
		SCRATCH_BYTES - at, SCRATCH_BASE + at, SCRATCH_HELD_BYTES, scratch_used, SCRATCH_BYTES);
}

/* bytes from the heap, never the scratchpad (16-byte aligned, zeroed, never
freed): the profiler's tables under "timerson" (form D). Named for the cache
map (tools/cachemap.py) as the scratchpad's heap pieces are */
void *psp_heap_take(unsigned int bytes, const char *what)
{
	void *pointer = memalign(16, bytes);

	if (pointer)
		memset(pointer, 0, bytes);
	platform_log("scratchpad: %u bytes for %s from the heap (the scratchpad's room is held)", bytes, what);
	if (pointer)
		platform_log("scratch span: %08x %u piece: %s", (unsigned int)pointer, bytes, what);
	return pointer;
}

void psp_scratch_bsp_fill(const void *collision_bsp)
{
	const struct scratch_block *blocks = (const struct scratch_block *)collision_bsp;
	const struct scratch_bsp3d_node *nodes;
	const float *planes;
	unsigned int used = 1, k, depth_end = 1, depth = 0;

	psp_sb_tree = NULL;
	sb_level = collision_bsp;
	if (!psp_sb_slots || !blocks)
		return;
	nodes = (const struct scratch_bsp3d_node *)blocks[0].address;
	planes = (const float *)blocks[1].address;
	if (!nodes || blocks[0].count <= 0 || !planes || blocks[1].count <= 0 ||
		nodes[0].plane_designator < 0 || nodes[0].plane_designator >= blocks[1].count)
		return;
	/* slot 0 is the root; each copied node's children that are nodes take
	the next slots while there is room (so the copy is the tree's top,
	breadth first), and a child left out keeps its index into the tag data */
	psp_sb_slots[0].node_index = 0;
	for (k = 0; k < used; k++)
	{
		struct psp_sb_slot *slot = &psp_sb_slots[k];
		const struct scratch_bsp3d_node *node = &nodes[slot->node_index];
		int c;

		if (k == depth_end)
		{
			depth++;
			depth_end = used;
		}
		memcpy(slot->plane, planes + 4 * node->plane_designator, sizeof(slot->plane));
		slot->designator = node->plane_designator;
		for (c = 0; c < 2; c++)
		{
			long child = node->children[c];

			slot->children[c] = child;
			if (child >= 0 && child < blocks[0].count && used < sb_room &&
				nodes[child].plane_designator >= 0 && nodes[child].plane_designator < blocks[1].count)
			{
				psp_sb_slots[used].node_index = child;
				slot->children[c] = PSP_SB_TAG | (long)used;
				used++;
			}
		}
	}
	psp_sb_tree = collision_bsp;
	platform_log("scratchbsp: %u of the level BSP's %ld nodes in the scratchpad (to depth %u), %u bytes", used,
		blocks[0].count, depth, used * (unsigned int)sizeof(struct psp_sb_slot));
}
