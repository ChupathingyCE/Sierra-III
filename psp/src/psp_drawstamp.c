/* "drawstamp" (keep word; the lab's briefs/draw_main.md task dm-c): what two
levers would remove from the main CPU's drawing, timed. A log only.

(a) the part's shader-to-GE-state block in rasterizer_model_draw (SHREC);
(b) the level's from-bitvector triangle build, which only
structure_render_preprocess's call reaches (SURFRUN);
(c) structure_render_pass's cursor loop, in every pass (SURFRUN).
The clock pairs are placed by psp_fixups.py (drawstamp_cut), on
sceKernelGetSystemTimeLow as drawsplit's are, in whole microseconds: a short
interval reads 0 or 1, right on average over many, and each holds about one
clock read of its own, so the counts of pairs are logged to take it off.

The line, every 300 frames, a frame's mean:
  drawstamp: shader state U us a frame (N parts), surfaces build U (S
  surfaces, R runs, mean L), pass cursor U (K loops)
a run being surfaces of consecutive index (the set bits next to each other),
counted after the build's clock pair, from the list it wrote. With the word
off each hook is a flag's test, and the frame's end one more. */

int psp_keep(const char *name);
void platform_log(const char *format, ...);

#define WINDOW 300

int psp_drawstamp_on = 0;
unsigned int psp_drawstamp_state_us, psp_drawstamp_parts;
unsigned int psp_drawstamp_build_us, psp_drawstamp_surfaces, psp_drawstamp_runs;
unsigned int psp_drawstamp_cursor_us, psp_drawstamp_loops;

/* the build's list, ascending: its surfaces and its runs */
void psp_drawstamp_built(long const *surface_indices, int count)
{
	int k;

	for (k = 0; k < count; k++)
		if (!k || surface_indices[k] != surface_indices[k - 1] + 1)
			psp_drawstamp_runs++;
	if (count > 0)
		psp_drawstamp_surfaces += count;
}

static void clear(void)
{
	psp_drawstamp_state_us = psp_drawstamp_parts = 0;
	psp_drawstamp_build_us = psp_drawstamp_surfaces = psp_drawstamp_runs = 0;
	psp_drawstamp_cursor_us = psp_drawstamp_loops = 0;
}

/* a frame has been drawn (psp_frame_account) */
void psp_drawstamp_frame(void)
{
	static int word = -1;
	static unsigned int frames = 0;
	float n = (float)WINDOW;

	if (word < 0)
	{
		word = psp_keep("drawstamp");
		psp_drawstamp_on = word;
		if (word)
			platform_log("drawstamp: on (model parts' shader state, the level's triangle build, the passes' cursor loop; a line each %d frames)",
				WINDOW);
		return;
	}
	if (!word || ++frames < WINDOW)
		return;
	platform_log("drawstamp: shader state %.1f us a frame (%.1f parts), surfaces build %.1f (%.1f surfaces, %.1f runs, mean %.2f), pass cursor %.1f (%.1f loops)",
		psp_drawstamp_state_us / n, psp_drawstamp_parts / n, psp_drawstamp_build_us / n, psp_drawstamp_surfaces / n,
		psp_drawstamp_runs / n, psp_drawstamp_runs ? (float)psp_drawstamp_surfaces / psp_drawstamp_runs : 0.0f,
		psp_drawstamp_cursor_us / n, psp_drawstamp_loops / n);
	frames = 0;
	clear();
}

/* "surfstay" (keep word; the lab's task sr-3, price:SURFRUN): what of the
level's triangle build stays if its writes go. Once a frame, after it is drawn,
four items each in one clock pair, each started cold by a whole-cache
writeback-invalidate outside its pair, in an order rotating by frame:
  full  the engine's build again (structure_render.c's tail, psp_surfstay_full:
        lock, from_bitvector as preprocess calls it, unlock; counted code)
  copy+stores / copy-stores  the same loop uncounted, with its two writes or
        with a sum in their place: the difference is the stores alone
  scan  the flag-word walk alone, which stays
The line, every 300 frames, a ran frame's mean:
  surfstay: full F copy+stores A copy-stores B scan S us a frame, surfaces N,
  skipped K
A frame whose build made no triangles is skipped whole and counted. Off, one
test a frame. */
int psp_surfstay_full(void);
int psp_surfstay_copy(int stores);
int psp_surfstay_scan(void);
unsigned int sceKernelGetSystemTimeLow(void);
void sceKernelDcacheWritebackInvalidateAll(void);

void psp_surfstay_frame(void)
{
	static int word = -1;
	static unsigned int frames, ran, skipped, rotate;
	static unsigned int us[4], surfaces;
	unsigned int t[4], stamp;
	int n[4] = { 0, 0, 0, 0 }, k, item;

	if (word < 0)
	{
		word = psp_keep("surfstay");
		if (word)
			platform_log("surfstay: on (the level's triangle build again: full, copy with and without its stores, scan; a line each %d frames)",
				WINDOW);
		return;
	}
	if (!word)
		return;
	for (k = 0; k < 4; k++)
	{
		item = (rotate + k) & 3;
		sceKernelDcacheWritebackInvalidateAll();
		stamp = sceKernelGetSystemTimeLow();
		switch (item)
		{
		case 0: n[0] = psp_surfstay_full(); break;
		case 1: n[1] = psp_surfstay_copy(1); break;
		case 2: n[2] = psp_surfstay_copy(0); break;
		default: n[3] = psp_surfstay_scan(); break;
		}
		t[item] = sceKernelGetSystemTimeLow() - stamp;
	}
	rotate++;
	if (n[0] && n[1] && n[2])
	{
		for (k = 0; k < 4; k++)
			us[k] += t[k];
		surfaces += n[0];
		ran++;
	}
	else
		skipped++;
	if (++frames < WINDOW)
		return;
	{
		float r = ran ? (float)ran : 1.0f;

		platform_log("surfstay: full %.1f copy+stores %.1f copy-stores %.1f scan %.1f us a frame, surfaces %.1f, skipped %u",
			us[0] / r, us[1] / r, us[2] / r, us[3] / r, surfaces / r, skipped);
	}
	frames = ran = skipped = surfaces = 0;
	for (k = 0; k < 4; k++)
		us[k] = 0;
}
