/* "shreccount" (keep word; the lab's briefs/draw_main.md task sh-1,
price:SHREC reach, fallback and room): what a per-shader record would cover in
rasterizer_model_draw's per-part shader block, counted. A log only, no clocks.

A part is one pass through the block (psp_fixups.py shreccount_cut, placed
after the reflection call and refldump's test, before the main menu's detail
block). Counted a part:
  model  its shader is a model shader (soso, type 4 at +36: the type test
         that opens the block); these get SHREC's record
  other  any other shader (or none)
  glow   a model part whose self-illumination block runs: a multipurpose map
         (+200) and a non-zero period (+116), when the block's model_shader
         holds (type 4 and a base map key)
  fade   a model part whose reflection is faded by the camera's distance: a
         cube map (+368), cutoff (+320) non-zero and unlike falloff (+316)
The glow's and the fade's per-part work stays under SHREC.
Over the whole run: distinct model shader pointers seen, in a table of
TABLE_SIZE; those that don't fit are counted as over.

The line, every 300 frames, a frame's mean:
  shreccount: parts P model M other O glow G fade F distinct S (over Z)
With the word off each part is a flag's test, and the frame's end one more.

"shrecstay" (keep word; task sh-2, price:SHREC reach and added; with
drawstamp on): what stays per part under SHREC, timed. psp_fixups.py
shrecstay_cut puts main-CPU clock pairs (sceKernelGetSystemTimeLow, as
drawstamp's) round only the block's work that stays: the change colours,
the glow's phase and function and its colour mix, the camera-distance fade
with the lighting tint multiply, and the two GE state calls (their keys
worked out just outside the pairs). Once a part, a timed look-up of the
part's shader pointer in the table above (the one table read SHREC adds),
its result unused; a pointer not yet in it goes in, as a record made at
first use would, so this table is shared with shreccount's distinct count.
Each pair holds about one clock read of its own, so the pairs are logged.
The line, every 300 frames, a frame's mean:
  shrecstay: stays U us a frame (pairs N), lookup L us a frame (parts P)
Off, each hook is a flag's test. */

int psp_keep(const char *name);
void platform_log(const char *format, ...);

#define WINDOW 300
#define TABLE_SIZE 1024 /* a power of two; full at 3/4 */
#define TABLE_FULL (TABLE_SIZE * 3 / 4)

int psp_shreccount_on = 0;
static unsigned int parts, models, others, glows, fades;
static void const *seen[TABLE_SIZE];
static unsigned int distinct, over;

int psp_shrecstay_on = 0;
unsigned int psp_shrecstay_us, psp_shrecstay_pairs, psp_shrecstay_lookup_us, psp_shrecstay_lookups;

/* the shader's slot, put in if it isn't there yet; -1 for none or a full table */
static int see(void const *shader)
{
	unsigned int slot = ((unsigned int)(unsigned long)shader * 2654435761u) >> 22 & (TABLE_SIZE - 1);

	while (seen[slot])
	{
		if (seen[slot] == shader)
			return (int)slot;
		slot = (slot + 1) & (TABLE_SIZE - 1);
	}
	if (distinct >= TABLE_FULL)
	{
		over++;
		return -1;
	}
	seen[slot] = shader;
	distinct++;
	return (int)slot;
}

/* shrecstay's look-up, a part (timed at its call) */
int psp_shrecstay_lookup(void const *shader)
{
	psp_shrecstay_lookups++;
	return shader ? see(shader) : -1;
}

/* a part through the block: its shader, and the block's model_shader */
void psp_shreccount_part(void const *shader, int model_shader)
{
	char const *definition = (char const *)shader;

	parts++;
	if (!shader || *(short const *)(definition + 36) != 4)
	{
		others++;
		return;
	}
	models++;
	see(shader);
	if (!model_shader)
		return;
	if (*(long const *)(definition + 200) != -1 && *(float const *)(definition + 116) != 0.0f)
		glows++;
	if (*(long const *)(definition + 368) != -1)
	{
		float falloff = *(float const *)(definition + 316), cutoff = *(float const *)(definition + 320);

		if (cutoff != 0.0f && falloff != cutoff)
			fades++;
	}
}

/* a frame has been drawn (psp_frame_account) */
static void shrecstay_frame(void)
{
	static int word = -1;
	static unsigned int frames = 0;
	float n = (float)WINDOW;

	if (word < 0)
	{
		word = psp_keep("shrecstay");
		psp_shrecstay_on = word;
		if (word)
			platform_log("shrecstay: on (what stays per model part under SHREC, timed, and the record's look-up; a line each %d frames)",
				WINDOW);
		return;
	}
	if (!word || ++frames < WINDOW)
		return;
	platform_log("shrecstay: stays %.1f us a frame (pairs %.1f), lookup %.1f us a frame (parts %.1f)",
		psp_shrecstay_us / n, psp_shrecstay_pairs / n, psp_shrecstay_lookup_us / n, psp_shrecstay_lookups / n);
	frames = 0;
	psp_shrecstay_us = psp_shrecstay_pairs = psp_shrecstay_lookup_us = psp_shrecstay_lookups = 0;
}

void psp_shreccount_frame(void)
{
	static int word = -1;
	static unsigned int frames = 0;
	float n = (float)WINDOW;

	shrecstay_frame();
	if (word < 0)
	{
		word = psp_keep("shreccount");
		psp_shreccount_on = word;
		if (word)
			platform_log("shreccount: on (model parts' shader block by type, glow, fade, distinct model shaders; a line each %d frames)",
				WINDOW);
		return;
	}
	if (!word || ++frames < WINDOW)
		return;
	platform_log("shreccount: parts %.1f model %.1f other %.1f glow %.1f fade %.1f distinct %u (over %u)",
		parts / n, models / n, others / n, glows / n, fades / n, distinct, over);
	frames = 0;
	parts = models = others = glows = fades = 0;
}
