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

keep.txt "noscratch": every piece comes from the heap instead, for the
hardware A/B (the same code, the same data, only its place differs). */
#include <pspkernel.h>
#include <stdlib.h>
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
	return pointer;
}

/* whether a pointer is the scratchpad's (for the reports) */
int psp_scratch_holds(const void *pointer)
{
	unsigned int at = (unsigned int)pointer;

	return at >= SCRATCH_BASE && at < SCRATCH_BASE + SCRATCH_BYTES;
}
