/* VRAMCODE (docs/vramcode.md): the main CPU's hottest functions copied into a
room in VRAM and entered there by the calls in main RAM that only the main CPU
makes. VRAM's cached view runs code in user mode through the I-cache, and a
line filled from it costs 57.9 cycles against 72.3 from main RAM with the GE
idle, 148.8 against 215.0 with the GE reading main RAM, but 91.3 against 73.7
with the GE reading VRAM (hardware_facts.md, VRAM; the lab's
vram_code_line_fill_1000). Which wins in a frame's mix is the three-way's
question:
  - neither word: the game as without this unit;
  - "vramroom": the room taken from the texture cache, nothing put there (what
    the room costs the drawing alone);
  - "vramcode": the room, the copy in it, and the calls re-aimed.
tools/vramcode.py writes the table after the link. The originals stay whole,
so the ME (which can't see VRAM), function pointers and every call the tool
left alone run them as before */

#include <pspkernel.h>
#include <psputils.h>

int psp_keep(const char *name);
void platform_log(const char *format, ...);

#define VRAMCODE_TABLE_WORDS 6144
#define VRAMCODE_MAGIC 0x56524D43u
#define VRAMCODE_HEADER 16
/* the room's place, fixed so the tool can write the copy's own addresses:
the motion sensor's 16 KB, which blipdepth frees (psp_tex.c), and on up */
#define VRAMCODE_AT 0x040CC000u
#define VRAMCODE_START 0x000CC000u
#define VRAMCODE_ROOM_MOST 0x20000u

/* the tool's table; 'VRM0' until it's written. In .data: its words are heap
the game doesn't get, so it's sized to the lists in use */
unsigned int psp_vramcode_table[VRAMCODE_TABLE_WORDS] = { 0x56524D30u };

static int state = 0;          /* 0 not decided, 1 room only, 2 copy in place, -1 off */
static unsigned int base = 0;  /* the program's load address less its link address */
static unsigned int copy_sum = 0;
static int patched = 0;

static const unsigned int *callers_at(void)
{
	const unsigned int *t = psp_vramcode_table;
	unsigned int at = VRAMCODE_HEADER + 3 * t[6] + 2 * t[7] + 2 * t[8] + 2 * t[9] + 2 * t[10], k;

	for (k = 0; k < t[11]; k++)
		at += 2 + t[at + 1];
	return t + at;
}

static unsigned int sum_copy(void)
{
	const unsigned int *w = (const unsigned int *)VRAMCODE_AT;
	unsigned int k, sum = 0;

	for (k = 0; k < psp_vramcode_table[3] / 4; k++)
		sum += w[k];
	return sum;
}

/* each call in main RAM aimed at the copy (on) or at the original (off).
A call is one word, so a thread that runs it meanwhile takes one or the
other, and both are right while the copy is whole */
static void aim_callers(int on)
{
	const unsigned int *t = psp_vramcode_table, *c = callers_at();
	unsigned int k;

	for (k = 0; k < t[13]; k++, c++)
	{
		unsigned int *word = (unsigned int *)(base + ((*c & 0xFFFFFu) << 2));
		const unsigned int *f = t + VRAMCODE_HEADER + 3 * ((*c >> 20) & 0x3FFu);
		unsigned int to = on ? VRAMCODE_AT + f[2] : base + f[0];

		*word = (*word & 0xFC000000u) | ((to >> 2) & 0x03FFFFFFu);
	}
	sceKernelDcacheWritebackAll();
	sceKernelIcacheInvalidateAll();
	patched = on;
}

/* the copy made from the loaded originals, then its patches */
__attribute__((noinline)) void psp_vramcode_install(void)
{
	const unsigned int *t = psp_vramcode_table;
	unsigned int at = VRAMCODE_HEADER, k, tramp_at = t[12];
	unsigned int copy = VRAMCODE_AT;

	base = (unsigned int)psp_vramcode_install - t[2];
	for (k = 0; k < t[6]; k++, at += 3)
	{
		const unsigned int *from = (const unsigned int *)(base + t[at]);
		unsigned int *to = (unsigned int *)(copy + t[at + 2]), w;

		for (w = 0; w < t[at + 1] / 4; w++)
			to[w] = from[w];
	}
	for (k = 0; k < t[7]; k++, at += 2)
	{
		unsigned int *word = (unsigned int *)(copy + t[at]);

		*word = (*word & 0xFC000000u) | (((copy + t[at + 1]) >> 2) & 0x03FFFFFFu);
	}
	for (k = 0; k < t[8]; k++, at += 2)
	{
		unsigned int *tramp = (unsigned int *)(copy + tramp_at + 8 * k);
		unsigned int to = t[at] ? copy + t[at + 1] : base + t[at + 1];

		tramp[0] = 0x08000000u | ((to >> 2) & 0x03FFFFFFu); /* j to */
		tramp[1] = 0;                                         /* nop */
	}
	for (k = 0; k < t[9]; k++, at += 2)
	{
		unsigned int *word = (unsigned int *)(copy + t[at]);
		unsigned int tramp = copy + tramp_at + 8 * t[at + 1];

		*word = (*word & 0xFFFF0000u) | (((tramp - (copy + t[at] + 4)) >> 2) & 0xFFFFu);
	}
	for (k = 0; k < t[10]; k++, at += 2)
	{
		unsigned int *word = (unsigned int *)(copy + t[at]);

		*word = (*word & 0xFFFF0000u) | (t[at + 1] & 0xFFFFu);
	}
	for (k = 0; k < t[11]; k++)
	{
		unsigned int *to = (unsigned int *)(copy + t[at]), words = t[at + 1], w;

		for (w = 0; w < words; w++)
			to[w] = t[at + 2 + w];
		at += 2 + words;
	}
	sceKernelDcacheWritebackRange((void *)copy, t[3]);
	sceKernelIcacheInvalidateRange((void *)copy, t[3]);
	copy_sum = sum_copy();
}

static int table_good(void)
{
	const unsigned int *t = psp_vramcode_table;

	return t[0] == VRAMCODE_MAGIC && t[1] == 1 && t[5] == VRAMCODE_AT && t[4] && t[4] <= VRAMCODE_ROOM_MOST &&
		t[3] <= t[4] && t[6];
}

/* psp_tex_close, as each map opens: the bytes the room takes from the start
of the texture cache (0 with neither word), the copy put in place the first
time and checked after. start is where the cache would begin */
unsigned int psp_vramcode_room(unsigned int start)
{
	const unsigned int *t = psp_vramcode_table;

	if (state < 0)
		return 0;
	if (!state)
	{
		int room = psp_keep("vramroom"), code = psp_keep("vramcode");

		if (!room && !code)
		{
			state = -1;
			return 0;
		}
		if (!table_good() || start != VRAMCODE_START || psp_keep("benchrun"))
		{
			platform_log("vramcode: refused (%s): the texture cache as before",
				!table_good() ? "no table in this build" : start != VRAMCODE_START ? "needs blipdepth's 16 KB at 0xcc000" : "benchrun");
			state = -1;
			return 0;
		}
		if (!code)
		{
			state = 1;
			platform_log("vramroom: %u KB of VRAM at %06x taken from the texture cache, nothing in it (the room's cost alone)",
				t[4] / 1024, VRAMCODE_START);
			return t[4];
		}
		psp_vramcode_install();
		aim_callers(1);
		state = 2;
		platform_log("vramcode: %u functions, %u bytes at VRAM %08x (room %u KB from the texture cache), %u calls in main RAM re-aimed, "
			"%u between copies, %u trampolines, %u jump tables; sum %08x",
			t[6], t[3], VRAMCODE_AT, t[4] / 1024, t[13], t[7], t[8], t[11], copy_sum);
		return t[4];
	}
	if (state == 2)
	{
		unsigned int now = sum_copy();

		if (now != copy_sum)
		{
			/* something wrote the room between maps: the calls back to the
			originals, and the copy left off for the rest of the run */
			if (patched)
				aim_callers(0);
			platform_log("vramcode: THE COPY CHANGED (sum %08x, was %08x): calls back to main RAM, off from here", now, copy_sum);
			state = 1;
		}
	}
	return t[4];
}

/* the sleep: the calls back to the originals before it, the copy checked
and put back after it (VRAM may not keep it), then the calls re-aimed */
void psp_vramcode_suspend(void)
{
	if (state == 2 && patched)
		aim_callers(0);
}

void psp_vramcode_resume(void)
{
	if (state != 2 || patched)
		return;
	if (sum_copy() != copy_sum)
	{
		psp_vramcode_install();
		platform_log("vramcode: the copy put back after the sleep");
	}
	aim_callers(1);
}
