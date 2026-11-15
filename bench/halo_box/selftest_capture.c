/* Writes the self-test box's capture (numbers only), so a run can prove the
capture's reader, the comparison and the stick's file calls with nothing
copied onto the stick first. It is also the format's worked example in C:
header, static records, tick sections on 64-byte boundaries. tools/cap1.py
selftest makes the same kind of file on a desktop. */
#include <stdio.h>
#include <string.h>
#include "harness.h"
#include "../../psp/src/psp_cap1.h"

static unsigned int seed;
static float draw(float low, float high)
{
	seed = seed * 1664525u + 1013904223u;
	return low + (high - low) * (float)(seed >> 8) / 16777216.0f;
}

static void pad_to(FILE *file, unsigned int boundary)
{
	static const char zeros[64];
	long at = ftell(file);

	if (at % boundary)
		fwrite(zeros, 1, boundary - at % boundary, file);
}

int selftest_capture(const char *path, unsigned int ticks, unsigned int entities, int spoil_tick, int spoil_entity)
{
	static float tables[4][40];
	struct cap1_header header;
	FILE *file = fopen(path, "wb");
	unsigned int t, i, k, key;

	if (!file)
		return 0;
	seed = 1;
	memset(&header, 0, sizeof(header));
	header.magic = CAP1_MAGIC;
	header.version = CAP1_VERSION;
	memcpy(header.box, "selftest", 8);
	strncpy(header.map, "none", sizeof(header.map));
	strncpy(header.bench, "selftest", sizeof(header.bench));
	header.ticks = ticks;
	header.entities = ticks * entities;
	for (k = 0; k < 4; k++)
		header.static_bytes += sizeof(struct cap1_static) + (16 + 8 * k) * 4;
	fwrite(&header, 1, sizeof(header), file);
	for (k = 0; k < 4; k++)
	{
		struct cap1_static record = { 100 + k, (16 + 8 * k) * 4 };

		for (i = 0; i < 16 + 8 * k; i++)
			tables[k][i] = draw(-4, 4);
		fwrite(&record, 1, sizeof(record), file);
		fwrite(tables[k], 4, 16 + 8 * k, file);
	}
	for (t = 0; t < ticks; t++)
	{
		struct cap1_tick tick = { CAP1_TICK, 30 * t, entities, 0 };
		long head, end;

		pad_to(file, 64);
		head = ftell(file);
		fwrite(&tick, 1, sizeof(tick), file);
		for (i = 0; i < entities; i++)
		{
			unsigned int table = (seed >> 13) & 3, count = 16 + 8 * table;
			struct cap1_entity record = { 1000 + i, (unsigned short)((seed >> 11) & 1), 0, 4 + count * 4, count * 4 };
			float in[40], out[40];

			for (k = 0; k < count; k++)
			{
				/* volatile: the product rounded to single before the add, as the box's own loop stores it */
				volatile float product;

				in[k] = draw(-100, 100);
				product = in[k] * tables[table][k];
				out[k] = product + (record.kind == 1 ? 1.0f : 0.0f);
			}
			if ((int)t == spoil_tick && (int)i == spoil_entity)
				out[3] += 0.5f;
			key = 100 + table;
			fwrite(&record, 1, sizeof(record), file);
			fwrite(&key, 1, 4, file);
			fwrite(in, 4, count, file);
			fwrite(out, 4, count, file);
		}
		end = ftell(file);
		tick.bytes = (unsigned int)(end - head - (long)sizeof(tick));
		fseek(file, head, SEEK_SET);
		fwrite(&tick, 1, sizeof(tick), file);
		fseek(file, end, SEEK_SET);
	}
	return fclose(file) == 0;
}
