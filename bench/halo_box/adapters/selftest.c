/* The harness's own proof: a box small enough to check by hand, with a
static table, inputs, outputs, two kinds and a tolerance, so the capture's
reader and the comparison are exercised before a real box exists. Its
captures are made by tools/cap1.py selftest (numbers only, no game data).

The box: an entity's input is N floats and the key of a static record of N
floats; its output is input[k] * table[k] + (kind == 1 ? 1 : 0). */
#include <string.h>
#include "../box.h"

#define SELFTEST_MOST 256

static const struct box_static *tables;
static unsigned int table_count;
static const struct box_tick *now;
static float results[SELFTEST_MOST][64];
static const char *const kinds[] = { "plain", "plus one", 0 };

static int selftest_setup(const struct box_static *statics, unsigned int count)
{
	tables = statics;
	table_count = count;
	return (int)sizeof(results);
}

static void selftest_tick(const struct box_tick *tick) { now = tick; }

static __attribute__((noinline)) void selftest_run(int i)
{
	const struct box_entity *e = &now->entity[i];
	unsigned int key, count = (e->input_length - 4) / 4, k, t;
	const float *in = (const float *)(e->input + 4), *table = 0;

	memcpy(&key, e->input, 4);
	for (t = 0; t < table_count; t++)
		if (tables[t].key == key)
			table = (const float *)tables[t].bytes;
	if (!table || i >= SELFTEST_MOST || count > 64)
		return;
	for (k = 0; k < count; k++)
		results[i][k] = in[k] * table[k] + (e->kind == 1 ? 1.0f : 0.0f);
}

static void selftest_output(int i, const void **bytes, unsigned int *length)
{
	*bytes = results[i];
	*length = now->entity[i].input_length - 4;
}

const struct box_adapter box_selftest = {
	"selftest", kinds, 0, BOX_RELATIVE, 1e-6f, 1e-5f, 0.01f, 0.5f, selftest_setup, selftest_tick, selftest_run, selftest_output, 0,
};
