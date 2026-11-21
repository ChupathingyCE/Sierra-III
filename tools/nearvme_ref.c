/* nearvme_ref.c: the near-vme kernel's arithmetic on the host, word for word
(psp/src/vme/near_verdicts.vmeg.c; docs/box_near_vme.md section 6).

The kernel never resets its accumulators. Each result is the difference of
two running sums, each floored by the MAC's shift and kept in 24 bits, so a
result depends on the floor of the running sum before it as well as on its own
group. This program keeps the same running sums, from zero at the stream's
start (the lead-in reads zeros), and writes what the kernel writes.

Input (stdin, whitespace separated): rows slots vertices k1 k2, then
vertices x 4 vertex words (x, y, z, w), rows x slots x 4 row words in stream
order (row, slot, component), vertices x slots byte weights.
Output: "d" and stage 1's words in stream order (vertex, row, slot), then
"p" and the plane words in order (vertex, row), as signed decimals. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define MAX_ROWS 5
#define MAX_SLOTS 8
#define MAX_VERTICES 2048

/* a VME word: 24 bits, sign-extended from bit 23 */
static int32_t word24(int64_t x)
{
	uint32_t low = (uint32_t)x & 0xffffffu;
	return (low & 0x800000u) ? (int32_t)low - 0x1000000 : (int32_t)low;
}

/* the MAC's shift floors (lab findings vme_mac_shift, the PSP-3000), whatever
the host's >> does with a negative number */
static int64_t floor_shift(int64_t x, int k)
{
	return x >= 0 ? x >> k : -((-x + ((int64_t)1 << k) - 1) >> k);
}

static int read_all(long *v, long n)
{
	long i;

	for (i = 0; i < n; i++)
		if (scanf("%ld", &v[i]) != 1)
			return 0;
	return 1;
}

int main(void)
{
	static long vertex[MAX_VERTICES][4], row[MAX_ROWS][MAX_SLOTS][4], weight[MAX_VERTICES][MAX_SLOTS];
	static int32_t dot[MAX_VERTICES][MAX_ROWS][MAX_SLOTS];
	long head[5];
	int rows, slots, vertices, k1, k2, i, r, s, c;
	int64_t sum, before;

	if (!read_all(head, 5))
		return 2;
	rows = (int)head[0]; slots = (int)head[1]; vertices = (int)head[2]; k1 = (int)head[3]; k2 = (int)head[4];
	if (rows < 1 || rows > MAX_ROWS || slots < 1 || slots > MAX_SLOTS || vertices < 1 || vertices > MAX_VERTICES)
		return 2;
	for (i = 0; i < vertices; i++)
		if (!read_all(vertex[i], 4))
			return 2;
	for (r = 0; r < rows; r++)
		for (s = 0; s < slots; s++)
			if (!read_all(row[r][s], 4))
				return 2;
	for (i = 0; i < vertices; i++)
		if (!read_all(weight[i], slots))
			return 2;

	/* stage 1: the words go in as 24-bit values; the running sum is wide
	(vmee's model, and the PSP's at least to 32 bits: see the box's page) */
	sum = 0;
	printf("d");
	for (i = 0; i < vertices; i++)
		for (r = 0; r < rows; r++)
			for (s = 0; s < slots; s++)
			{
				before = sum;
				for (c = 0; c < 4; c++)
					sum += (int64_t)word24(row[r][s][c]) * word24(vertex[i][c]);
				dot[i][r][s] = word24((int64_t)word24(floor_shift(sum, k1)) - word24(floor_shift(before, k1)));
				printf(" %d", dot[i][r][s]);
			}
	printf("\n");

	/* stage 2: stage 1's words, weighted, summed over the slots */
	sum = 0;
	printf("p");
	for (i = 0; i < vertices; i++)
		for (r = 0; r < rows; r++)
		{
			before = sum;
			for (s = 0; s < slots; s++)
				sum += (int64_t)dot[i][r][s] * word24(weight[i][s]);
			printf(" %d", word24((int64_t)word24(floor_shift(sum, k2)) - word24(floor_shift(before, k2))));
		}
	printf("\n");
	return 0;
}
