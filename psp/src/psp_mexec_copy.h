/* psp_mexec_copy.h - MEXEC's copy (docs/mexec.md), run on the Media Engine:
the table's functions copied from their loaded code into the eDRAM, the calls
between them and the branches out of them re-aimed, the copy's lines written
back and dropped from the I-cache, for a program that carries a table written
by tools/mexec.py. The box harness (bench/halo_box/me.c) uses it. psp_me.c
still has its own copy of the same steps (me_mexec_install): moving it here
changes that function's code under the profile, and a strict build refuses a
function without its counts. It moves at the next profile cut (docs/mexec.md,
"Then"). A call to a function not copied stands as the loader
relocated it: the ME runs its code through the cached alias, and jal keeps the
PC's top four bits */
#ifndef PSP_MEXEC_COPY_H
#define PSP_MEXEC_COPY_H

#define MEXEC_MAGIC 0x4D455843u
#define MEXEC_HEADER 16

struct mexec_copied
{
	unsigned int bytes, patches, sum;
};

/* `table` as tools/mexec.py wrote it; `entry` the loaded address of the
table's first function (the program's load base is it less the table's link
address for it); `copy` where in the ME's cached eDRAM the copy goes, with
`room` bytes there. Returns the entry's address in the copy, or 0 when the
table doesn't fit or isn't one */
static inline unsigned int mexec_copy(const unsigned int *t, unsigned int entry, unsigned int copy, unsigned int room,
	struct mexec_copied *out)
{
	unsigned int base, at = MEXEC_HEADER, k, sum = 0, tramp_at, line;

	if (t[0] != MEXEC_MAGIC || t[1] != 1 || t[3] > room || !t[4])
		return 0;
	base = entry - t[2];
	for (k = 0; k < t[4]; k++, at += 3)
	{
		const unsigned int *from = (const unsigned int *)(base + t[at]);
		unsigned int *to = (unsigned int *)(copy + t[at + 2]), w;

		for (w = 0; w < t[at + 1] / 4; w++)
			to[w] = from[w];
	}
	for (k = 0; k < t[5]; k++, at += 2)
	{
		unsigned int *word = (unsigned int *)(copy + t[at]);

		*word = (*word & 0xFC000000u) | (((copy + t[at + 1]) >> 2) & 0x03FFFFFFu);
	}
	tramp_at = t[11];
	for (k = 0; k < t[6]; k++, at += 2)
	{
		unsigned int *tramp = (unsigned int *)(copy + tramp_at + 8 * k);
		unsigned int to = t[at] ? copy + t[at + 1] : base + t[at + 1];

		tramp[0] = 0x08000000u | ((to >> 2) & 0x03FFFFFFu); /* j to */
		tramp[1] = 0;                                         /* nop */
	}
	for (k = 0; k < t[7]; k++, at += 2)
	{
		unsigned int *word = (unsigned int *)(copy + t[at]);
		unsigned int tramp = copy + tramp_at + 8 * t[at + 1];

		*word = (*word & 0xFFFF0000u) | (((tramp - (copy + t[at] + 4)) >> 2) & 0xFFFFu);
	}
	/* the lui/%lo pairs that load a jump table's address, re-aimed at its copy */
	for (k = 0; k < t[8]; k++, at += 2)
	{
		unsigned int *word = (unsigned int *)(copy + t[at]);

		*word = (*word & 0xFFFF0000u) | (t[at + 1] & 0xFFFFu);
	}
	/* the jump tables, their words the copy's addresses */
	for (k = 0; k < t[10]; k++)
	{
		unsigned int *to = (unsigned int *)(copy + t[at]), words = t[at + 1], w;

		for (w = 0; w < words; w++)
			to[w] = t[at + 2 + w];
		at += 2 + words;
	}
	for (k = 0; k < t[3] / 4; k++)
		sum += ((const unsigned int *)copy)[k];
	__asm__ __volatile__("sync");
	for (line = copy; line < copy + t[3]; line += 64)
		__asm__ __volatile__("cache 0x1b, 0(%0)" : : "r"(line));
	__asm__ __volatile__("sync");
	for (line = copy; line < copy + t[3]; line += 64)
		__asm__ __volatile__("cache 0x08, 0(%0)" : : "r"(line));
	__asm__ __volatile__("sync");
	out->bytes = t[3];
	out->patches = t[5] + t[7];
	out->sum = sum;
	return copy + t[MEXEC_HEADER + 2];
}

/* where function `f` (a loaded address) sits in the copy, or 0 if the table
doesn't copy it: for re-aiming a pointer at its copy (the box harness's
me_aim). `entry` as for mexec_copy */
static inline unsigned int mexec_copy_of(const unsigned int *t, unsigned int entry, unsigned int copy, unsigned int f)
{
	unsigned int base = entry - t[2], k, at = MEXEC_HEADER;

	for (k = 0; k < t[4]; k++, at += 3)
		if (base + t[at] == f)
			return copy + t[at + 2];
	return 0;
}

#endif
