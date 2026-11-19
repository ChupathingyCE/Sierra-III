/* The frame picture's prices (docs/frame_picture.md §6): boxes whose only
work is one kind of memory wait, so the counted pass (`counters`) gives the
cycles and the memory stall a miss of each kind costs, on the PSP that runs
them. Each entity does a fixed count of its kind:

- priceimiss: 512 lines of straight-line code, twice the I-cache, so every
  line is fetched again each entity (P_I);
- pricedclean: 256 loads, one a line, walking forward through a 1 MB buffer
  that's only ever read, so every miss evicts a clean line (P_D, clean);
- priceddirty: 256 stores, one a line, on the same walk through a buffer
  that's only ever written, so every miss evicts a dirty line (P_D, all
  dirty: the write-back inside the miss);
- priceuncached: 256 loads through the uncached view (P_UL).

The frame picture applies P_D between the clean and dirty prices by a box's
own write-backs against its misses, and gives its split as a range over
them. Nothing here reads a capture. */
#include "../box.h"

#define LINE 64
#define WALK_LINES 256
#define BUFFER_BYTES (1024u * 1024u)

static unsigned char read_buffer[BUFFER_BYTES] __attribute__((aligned(64)));
static unsigned char write_buffer[BUFFER_BYTES] __attribute__((aligned(64)));
static unsigned char uncached_buffer[WALK_LINES * LINE] __attribute__((aligned(64)));
static unsigned int read_at = 0, write_at = 0;
static volatile unsigned int sink;

static int price_setup(const struct box_static *statics, unsigned int count)
{
	(void)statics;
	(void)count;
	return (int)(2 * BUFFER_BYTES);
}
static void price_tick(const struct box_tick *tick) { (void)tick; }
static void price_output(int i, const void **bytes, unsigned int *length) { (void)i; *bytes = 0; *length = 0; }

/* 64 bytes of code: 16 instructions that do nothing the pipeline waits on */
#define LINE16 "nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\t"
#define LINES8 LINE16 LINE16 LINE16 LINE16 LINE16 LINE16 LINE16 LINE16
#define LINES64 LINES8 LINES8 LINES8 LINES8 LINES8 LINES8 LINES8 LINES8
#define LINES512 LINES64 LINES64 LINES64 LINES64 LINES64 LINES64 LINES64 LINES64

static __attribute__((noinline, aligned(64))) void imiss_run(int i)
{
	(void)i;
#if defined(__mips__)
	__asm__ __volatile__(".set push\n\t.set noreorder\n\t" LINES512 ".set pop\n\t");
#endif
	/* the desktop test builds every adapter but runs no price box: its
	assembler takes no MIPS directives */
}

static __attribute__((noinline)) void dclean_run(int i)
{
	unsigned int k, sum = 0;

	(void)i;
	for (k = 0; k < WALK_LINES; k++)
	{
		sum += *(volatile unsigned int *)(read_buffer + read_at);
		read_at = (read_at + LINE) & (BUFFER_BYTES - 1);
	}
	sink = sum;
}

static __attribute__((noinline)) void ddirty_run(int i)
{
	unsigned int k;

	for (k = 0; k < WALK_LINES; k++)
	{
		*(volatile unsigned int *)(write_buffer + write_at) = (unsigned int)i + k;
		write_at = (write_at + LINE) & (BUFFER_BYTES - 1);
	}
}

static __attribute__((noinline)) void uncached_run(int i)
{
	const volatile unsigned int *word = (const volatile unsigned int *)((unsigned int)uncached_buffer | 0x40000000u);
	unsigned int k, sum = 0;

	(void)i;
	for (k = 0; k < WALK_LINES; k++)
		sum += word[k * (LINE / 4)];
	sink = sum;
}

const struct box_adapter box_price_imiss = {
	"priceimiss", 0, 0, 1, BOX_EXACT, 0, 0, 0, 0, price_setup, price_tick, imiss_run, price_output, 0,
};
const struct box_adapter box_price_dclean = {
	"pricedclean", 0, 0, 1, BOX_EXACT, 0, 0, 0, 0, price_setup, price_tick, dclean_run, price_output, 0,
};
const struct box_adapter box_price_ddirty = {
	"priceddirty", 0, 0, 1, BOX_EXACT, 0, 0, 0, 0, price_setup, price_tick, ddirty_run, price_output, 0,
};
const struct box_adapter box_price_uncached = {
	"priceuncached", 0, 0, 1, BOX_EXACT, 0, 0, 0, 0, price_setup, price_tick, uncached_run, price_output, 0,
};
