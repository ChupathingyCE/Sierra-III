/* "drawstamp2" (keep word; the lab's briefs/draw_main.md task dm-e,
findings/case_dmd_2026-11-28.md section 2): the main CPU's drawing in the
objects' walk (A1-A8), the shadow pass (S1-S9) and the level's per-material
walk (C1-C7), timed. A log only.

The clock pairs are placed by psp_fixups.py (drawstamp2_cut), on
sceKernelGetSystemTimeLow as drawsplit's are, in whole microseconds. Times
are inclusive: a part's remainder is a subtraction. Each pair holds about one
clock read of its own, so each id's pairs are logged to take it off. A7 is in
every render_model call (lit, shadow and any other caller's); A5 counts the
lit pass's loop only (S2 holds the shadow pass's).

Every 300 frames, a frame's means, a line each of A, S and C:
  drawstamp2: A1 U us N calls; A2 U us N calls; ...
With the word off each hook is a flag's test, and the frame's end one more. */

#include <stdio.h>

int psp_keep(const char *name);
void platform_log(const char *format, ...);

#define WINDOW 300
#define IDS 24 /* psp_fixups.py's DS2_IDS: A1-A8, S1-S9, C1-C7 */

int psp_ds2_on = 0;
unsigned int psp_ds2_us[IDS], psp_ds2_n[IDS];

/* the ids from first, parts of one letter, on one line */
static void line(char letter, int first, int parts)
{
	char text[480];
	int used = 0, k;

	for (k = 0; k < parts && used < (int)sizeof(text); k++)
		used += snprintf(text + used, sizeof(text) - used, "%s%c%d %.1f us %.1f calls", k ? "; " : "", letter, k + 1,
			psp_ds2_us[first + k] / (double)WINDOW, psp_ds2_n[first + k] / (double)WINDOW);
	platform_log("drawstamp2: %s", text);
}

/* a frame has been drawn (psp_frame_account) */
void psp_drawstamp2_frame(void)
{
	static int word = -1;
	static unsigned int frames = 0;
	int k;

	if (word < 0)
	{
		word = psp_keep("drawstamp2");
		psp_ds2_on = word;
		if (word)
			platform_log("drawstamp2: on (the objects' walk, the shadow pass, the level's per-material walk; a line each of A, S, C every %d frames)",
				WINDOW);
		return;
	}
	if (!word || ++frames < WINDOW)
		return;
	line('A', 0, 8);
	line('S', 8, 9);
	line('C', 17, 7);
	frames = 0;
	for (k = 0; k < IDS; k++)
		psp_ds2_us[k] = psp_ds2_n[k] = 0;
}
