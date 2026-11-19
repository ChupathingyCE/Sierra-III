/* What a box gives the harness: its adapter (bench/halo_box/README.md, "The adapter"). */
#ifndef HALO_BOX_H
#define HALO_BOX_H

struct box_static { unsigned int key, length; const unsigned char *bytes; };
struct box_entity
{
	unsigned int id, kind, flags;
	unsigned int input_length, output_length;
	const unsigned char *input, *output; /* output: what the old code wrote */
};
struct box_tick
{
	unsigned int tick, entities;
	const struct box_entity *entity;
};

enum { BOX_EXACT, BOX_ABSOLUTE, BOX_RELATIVE };
#define BOX_ALL (-1) /* run(BOX_ALL): a batching box does the whole tick's entities in one walk */

struct box_adapter
{
	const char *name;
	const char *const *kinds; /* names for the capture's kind numbers, ended by a null; or null */
	int batch;                /* 1: run() is called once a tick, with BOX_ALL */
	int no_capture;           /* 1: the box reads nothing, and runs over entities that carry nothing when there is no capture */
	/* the comparison of an entity's output with the capture's, as 32-bit
	floats unless exact: absolute, within eps_absolute; relative, within
	eps_relative of the larger magnitude or within eps_absolute */
	int tolerance;
	float eps_absolute, eps_relative;
	/* the box page's budgets, 0 where it gives none */
	float budget_ms_a_tick, budget_us_an_entity;
	/* once a capture file: build the box's own tables. Returns the bytes
	the box owns afterwards, or a negative number to refuse the file */
	int (*setup)(const struct box_static *statics, unsigned int count);
	/* once a tick, not timed: put the tick's inputs where the box reads them */
	void (*tick)(const struct box_tick *tick);
	/* timed: the box's work for entity i of the tick */
	void (*run)(int i);
	/* not timed: where entity i's output is */
	void (*output)(int i, const void **bytes, unsigned int *length);
	/* optional, in place of the comparison above: 0 when `got` is within
	the box's tolerance of `want`; fills the largest differences and the
	element they were found at */
	int (*compare)(int i, const void *got, const void *want, unsigned int length, float *largest_absolute,
		float *largest_relative, unsigned int *element);
	/* optional: the capture box this one reads, when it isn't its own name (a
	box's second form over the first's capture) */
	const char *capture;
	/* optional, for the Media Engine's places (README, "On the Media
	Engine"): re-aim any function pointer the box calls through at
	`where(it)`, its copy in the ME's eDRAM for those places and itself
	otherwise; called on the main CPU before each pass on the ME */
	void (*me_aim)(void *(*where)(void *function));
	/* optional, run on the ME before (back 0) and after (back 1) an eDRAM
	pass, untimed: back 0 copies the tick's working set into `room` and points
	the box at it, returning the bytes used (negative: it didn't fit); back 1
	copies what the pass wrote back to main RAM and points the box there
	again. No allocation and no system call: it runs on the ME */
	int (*me_place)(void *room, unsigned int bytes, int back);
};

extern const struct box_adapter box_null, box_selftest, box_posing;
/* the frame picture's prices (adapters/price.c) */
extern const struct box_adapter box_price_imiss, box_price_dclean, box_price_ddirty, box_price_uncached;
/* the collision service: the BSP walk's forms (adapters/collbsp.c) and the pill sweep's (adapters/sweep.c) */
extern const struct box_adapter box_collbsp, box_collbspa, box_collbspw, box_collbspr, box_collbspb, box_collbspm,
	box_sweep0, box_sweepa, box_sweepf, box_sweepl, box_sweepb, box_sweepb25, box_sweepb100, box_sweepb25f, box_sweepb25l,
	box_sweepp25, box_sweepb25m;
/* every adapter built in, ended by a null (adapters.c) */
extern const struct box_adapter *const box_adapters[];

#endif
