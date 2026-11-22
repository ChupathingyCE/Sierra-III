/*
Biped motion's slide, stage 5a: the results (box_biped_slide.h says what they
are). Plain C on plain floats: nothing here reads the engine's data.
*/
#include <string.h>
#include "box_biped_slide.h"

void platform_psp_log(const char *format, ...);

static struct box_bmslide_result results[BOX_BMSLIDE_RESULTS];
static unsigned char arena[BOX_BMSLIDE_ARENA] __attribute__((aligned(16)));
static unsigned char scratch[BOX_BMSLIDE_PLANES * BOX_BMSLIDE_PLANE_BYTES] __attribute__((aligned(16)));
static int result_count, arena_fill, arena_peak, result_peak;
/* each result's place by its biped's datum index (low bits), so the solve
finds it without a scan; an entry is trusted only when the result it names
holds this biped this tick */
#define RESULT_OF 512
static short result_of[RESULT_OF];
/* made, left inline, room spent, taken, no result at the call, inputs differ,
compared, compared and differ */
static unsigned long stat[8], ticks;

void box_bmslide_begin(void)
{
	result_count = 0;
	arena_fill = 0;
}

void *box_bmslide_scratch(void)
{
	return scratch;
}

void box_bmslide_left(void)
{
	stat[1]++;
}

int box_bmslide_store(const struct box_bmslide_call *call, short slot, short count,
	const float *new_position, const float *new_velocity)
{
	struct box_bmslide_result *result;
	int bytes = count * BOX_BMSLIDE_PLANE_BYTES;

	if (result_count >= BOX_BMSLIDE_RESULTS || arena_fill + bytes > BOX_BMSLIDE_ARENA)
	{
		stat[2]++;
		return 0;
	}
	result = &results[result_count];
	result->call = *call;
	result->slot = slot;
	result->count = count;
	result->planes = (unsigned short)arena_fill;
	result->taken = 0;
	result->new_position[0] = new_position[0];
	result->new_position[1] = new_position[1];
	result->new_position[2] = new_position[2];
	result->new_velocity[0] = new_velocity[0];
	result->new_velocity[1] = new_velocity[1];
	result->new_velocity[2] = new_velocity[2];
	memcpy(arena + arena_fill, scratch, bytes);
	/* each result's planes on a word boundary: their floats are read as words */
	arena_fill += (bytes + 3) & ~3;
	result_of[call->biped_index & (RESULT_OF - 1)] = (short)result_count;
	result_count++;
	stat[0]++;
	return 1;
}

/* bit for bit: the result is only the answer to the very same call, and a
float compare would let 0 and -0 pass */
static int same_floats(const float *a, const float *b, int n)
{
	unsigned int x, y;
	int k;

	for (k = 0; k < n; k++)
	{
		memcpy(&x, &a[k], 4);
		memcpy(&y, &b[k], 4);
		if (x != y)
			return 0;
	}
	return 1;
}

static int same_call(const struct box_bmslide_call *a, const struct box_bmslide_call *b)
{
	return a->flags == b->flags && a->maximum == b->maximum &&
		same_floats(a->position, b->position, 3) && same_floats(a->velocity, b->velocity, 3) &&
		same_floats(&a->height, &b->height, 1) && same_floats(&a->width, &b->width, 1);
}

struct box_bmslide_result *box_bmslide_find(const struct box_bmslide_call *call)
{
	struct box_bmslide_result *result = 0;
	int k = result_of[call->biped_index & (RESULT_OF - 1)];

	if (k >= 0 && k < result_count && results[k].call.biped_index == call->biped_index && !results[k].taken)
		result = &results[k];
	for (k = 0; !result && k < result_count; k++)
	{
		if (results[k].call.biped_index == call->biped_index && !results[k].taken)
			result = &results[k];
	}
	if (!result)
	{
		stat[4]++;
		return 0;
	}
	if (!same_call(&result->call, call))
	{
		stat[5]++;
		return 0;
	}
	result->taken = 1;
	stat[3]++;
	return result;
}

const void *box_bmslide_planes(const struct box_bmslide_result *result)
{
	return arena + result->planes;
}

void box_bmslide_compare(const struct box_bmslide_result *result, short count,
	const float *new_position, const float *new_velocity, const void *planes)
{
	stat[6]++;
	if (count != result->count || !same_floats(new_position, result->new_position, 3) ||
		!same_floats(new_velocity, result->new_velocity, 3) ||
		memcmp(planes, arena + result->planes, count * BOX_BMSLIDE_PLANE_BYTES))
		stat[7]++;
}

void box_bmslide_end(void)
{
	if (arena_fill > arena_peak)
		arena_peak = arena_fill;
	if (result_count > result_peak)
		result_peak = result_count;
	result_count = 0;
	if (++ticks % 500 == 0)
		platform_psp_log("bmslide: %lu staged ticks: slides made %lu, left inline %lu, room spent %lu; taken %lu, no result at the call %lu, inputs differ %lu; bmslidecheck compared %lu, differ %lu; arena peak %d of %d bytes, results peak %d of %d",
			ticks, stat[0], stat[1], stat[2], stat[3], stat[4], stat[5], stat[6], stat[7], arena_peak, BOX_BMSLIDE_ARENA, result_peak, BOX_BMSLIDE_RESULTS);
}
