/* The harness on a desktop: the capture's reader, the comparison and the
report's arithmetic, with nothing of the PSP behind them. Its times are the
desktop's and mean nothing. bench/halo_box/host/test.sh runs it. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../harness.h"

unsigned int plat_now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (unsigned int)(t.tv_sec * 1000000u + t.tv_nsec / 1000);
}
void plat_empty(void) {}
void plat_crowd(void) {}
void plat_ge_start(int duty, unsigned int span_us) { (void)duty; (void)span_us; }
int plat_ge_stop(void) { return 1; }
int plat_ge_spent(double *span_us, double *other_us) { *span_us = 100; *other_us = 39; return 1; }
/* made-up counters, so the report's arithmetic runs: 100 cycles, 1 data miss and 40 stalled cycles a read */
void plat_counters(unsigned int out[COUNTERS])
{
	static unsigned int reads;

	memset(out, 0, COUNTERS * sizeof(*out));
	reads++;
	out[C_CYCLES] = reads * 100;
	out[C_D_MISS] = reads;
	out[C_STALL_MEMORY] = reads * 40;
}
/* the Media Engine as the emulator stands in for it (me.c): each pass here,
the eDRAM places' room in this program's memory, so the hand-over's set-up,
an adapter's me_place and the checks run on a desktop (HALO_BOX_ME=1) */
#define ROOM_BYTES 0xC0000u
static void *room;
int plat_me_start(void *crowd_data, void (*crowd_code)(void), int want_copy)
{
	(void)crowd_data, (void)crowd_code, (void)want_copy;
	room = malloc(ROOM_BYTES);
	return room ? 2 : 0;
}
int plat_me_copied(void) { return room != 0; }
void *plat_me_where(void *function, int place) { (void)place; return function; }
int plat_me_pass(void (*run)(int), int (*place)(void *, unsigned int, int), unsigned int calls, int mode, int where,
	unsigned int *cycles, struct me_busy *busy)
{
	unsigned int i;

	(void)mode;
	if (where != PLACE_ME_RAM && place && place(room, ROOM_BYTES, 0) < 0)
		return -3;
	for (i = 0; i < calls; i++)
	{
		unsigned int before = plat_now();

		run((int)i);
		cycles[i] = (plat_now() - before) * 333u;
	}
	if (where != PLACE_ME_RAM && place)
		place(room, ROOM_BYTES, 1);
	if (busy)
	{
		busy->us += 1;
		busy->lines += 64;
	}
	return 0;
}
void plat_me_busy_alone(struct me_busy *into, unsigned int lines) { into->us += 1; into->lines += lines; }
int plat_me_jobs(void (*run)(int), unsigned int calls, int mode, int where, unsigned int *us, unsigned int *cycles,
	unsigned int *writeback_us)
{
	unsigned int i;

	(void)mode, (void)where;
	for (i = 0; i < calls; i++)
	{
		unsigned int before = plat_now();

		run((int)i);
		us[i] = plat_now() - before;
		cycles[i] = us[i] * 333u;
		writeback_us[i] = 0;
	}
	return 0;
}
unsigned int plat_me_at(void) { return 0; }
int plat_me_placed(void) { return 0; }
void plat_out(const char *format, ...)
{
	va_list list;

	va_start(list, format);
	vprintf(format, list);
	va_end(list);
	putchar('\n');
}
void plat_step(const char *format, ...)
{
	va_list list;

	va_start(list, format);
	printf("step: ");
	vprintf(format, list);
	va_end(list);
	putchar('\n');
}

int main(int argc, char **argv)
{
	struct harness_options options = { 0, { 0, 50 }, 2, 8, 91, 1, 0, 0 };
	const struct box_adapter *box = 0;
	int k;

	if (argc == 4 && !strcmp(argv[1], "--write"))
		return !selftest_capture(argv[2], 32, 40, argv[3][0] == 's' ? 7 : -1, 13);
	if (argc < 2)
		return printf("usage: host <box> [capture ...]   or   host --write <file> good|spoiled\n"), 2;
	for (k = 0; box_adapters[k]; k++)
		if (!strcmp(box_adapters[k]->name, argv[1]))
			box = box_adapters[k];
	if (!box)
		return printf("no box '%s'\n", argv[1]), 2;
	if (getenv("HALO_BOX_ME") && plat_me_start(0, 0, 1))
	{
		options.on_me = (1 << PLACE_ME_RAM) | (1 << PLACE_ME_EDRAM) | (1 << PLACE_ME_BUSY);
		options.me_jobs = 1;
	}
	return harness_run(box, (const char *const *)argv + 2, argc - 2, &options);
}
