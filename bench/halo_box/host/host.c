/* The harness on a desktop: the capture's reader, the comparison and the
report's arithmetic, with nothing of the PSP behind them. Its times are the
desktop's and mean nothing. bench/halo_box/host/test.sh runs it. */
#include <stdarg.h>
#include <stdio.h>
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
	struct harness_options options = { 0, { 0, 50 }, 2, 8, 91 };
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
	return harness_run(box, (const char *const *)argv + 2, argc - 2, &options);
}
