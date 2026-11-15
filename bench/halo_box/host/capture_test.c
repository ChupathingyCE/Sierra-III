/* The capture writer's desktop test (psp/src/psp_capture.c built with
PSP_CAPTURE_HOST): the self-test box's records pushed through the writer's
calls as a hook would make them, with what a real capture meets: entities
that begin inside another, a file cap small enough to make several files, a
tick too large for its room (dropped whole), ticks that aren't sampled.
Prints how many entities the files should hold; host/test.sh runs the
harness over the files and compares. Files land in the current folder. */
#include <stdio.h>
#include <string.h>
#include "../../../psp/src/psp_capture.h"

static unsigned int seed = 7;
static float draw(float low, float high)
{
	seed = seed * 1664525u + 1013904223u;
	return low + (high - low) * (float)(seed >> 8) / 16777216.0f;
}

static float tables[4][40];

struct made { unsigned int key, kind, count; float in[40], out[40]; };

static void make(struct made *m)
{
	unsigned int table = (seed >> 13) & 3, k;

	m->key = 100 + table;
	m->kind = (seed >> 11) & 1;
	m->count = 16 + 8 * table;
	for (k = 0; k < m->count; k++)
	{
		volatile float product;

		m->in[k] = draw(-100, 100);
		product = m->in[k] * tables[table][k];
		m->out[k] = product + (m->kind == 1 ? 1.0f : 0.0f);
	}
}

static void enter(const struct made *m, unsigned int id)
{
	psp_capture_static(m->key, tables[m->key - 100], (16 + 8 * (m->key - 100)) * 4);
	psp_capture_begin(id, m->kind, 0);
	psp_capture_input(&m->key, 4);
	/* in two pieces, as a hook with several fields would */
	psp_capture_input(m->in, 8 * 4);
	psp_capture_input(m->in + 8, (m->count - 8) * 4);
}

static void leave(const struct made *m)
{
	psp_capture_output(m->out, m->count * 4);
	psp_capture_end();
}

int main(void)
{
	struct psp_capture_plan plan = { "selftest", "none", "writer test", 0x1234abcd, 10, 3, 2, 0, 48 * 1024, 16 * 1024 };
	unsigned int tick, i, k, expected = 0, sampled = 0;

	for (k = 0; k < 4; k++)
		for (i = 0; i < 40; i++)
			tables[k][i] = draw(-4, 4);
	if (!psp_capture_open(&plan))
		return 1;
	psp_capture_begin(1, 0, 0); /* outside any tick: nothing */
	psp_capture_end();
	for (tick = 0; tick < 70; tick++)
	{
		/* tick 40 is too large for its 16 KB of room */
		unsigned int entities = tick == 40 ? 200 : 20 + tick % 7, this_tick = 0;

		if (!psp_capture_tick(tick))
			continue;
		sampled++;
		for (i = 0; i < entities; i++)
		{
			struct made outer, inner, innermost;

			make(&outer);
			enter(&outer, 1000 + i);
			if (i % 5 == 4)
			{
				/* two more inside it, one inside the other */
				make(&inner);
				enter(&inner, 2000 + i);
				if (i % 10 == 9)
				{
					make(&innermost);
					enter(&innermost, 3000 + i);
					leave(&innermost);
					this_tick++;
				}
				leave(&inner);
				this_tick++;
			}
			leave(&outer);
			this_tick++;
		}
		psp_capture_tick_end();
		if (tick != 40)
			expected += this_tick;
	}
	psp_capture_close();
	printf("expected: %u entities in %u ticks\n", expected, sampled - 1);
	return 0;
}
