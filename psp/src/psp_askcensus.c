/* The askers' census ("askcensus" in keep.txt; PSP_RECON builds only): how often
each of the game's collision askers asks, by phase, so the collision box's service
(docs/box_collision.md) is sized by who asks how much. psp_fixups.py's
FIXES_ASKCENSUS routes every call site the lab's read found (projects/sierra/coord/briefs/
askers_read.md, or-job 43761478863: 62 sites, 38 askers) through psp_ask(site)
on its way in. The site numbers and their askers are in the lab's
projects/sierra/scripts/askcensus.py, which reads the log.

It counts, it doesn't time: run it in the emulator, where the count is exact
and the Media Engine's askers run on the main CPU and are counted with the rest.
On a PSP the ME's own increments would race the main CPU's. */

#ifdef PSP_RECON

#include <stdio.h>

#define ASK_SITES 64
#define ASK_EVERY 300   /* ticks a line, as the workcensus */

int psp_keep(const char *name);
void platform_psp_log(const char *format, ...);
extern volatile int psp_current_phase;

static int ask_on = -1;
/* by phase: 0 anything outside the tick and the drawing (and the frame's end), 1 the tick, 2 the drawing */
static unsigned long ask_count[3][ASK_SITES];
static unsigned long ask_ticks, ask_frames, ask_window_frames;

void psp_ask(int site)
{
	int phase;

	if (ask_on < 0)
		ask_on = psp_keep("askcensus");
	if (!ask_on || site <= 0 || site >= ASK_SITES)
		return;
	phase = psp_current_phase;
	ask_count[phase == 1 || phase == 2 ? phase : 0][site]++;
}

/* from psp_phase(): each tick's start counts a tick, each drawing's a frame;
every ASK_EVERY ticks one line, "askcensus: T ticks to N, F frames | site
tick/drawing/other ...", the sites that asked only */
void psp_ask_phase(int phase)
{
	char line[1536];
	int length, site;

	if (ask_on <= 0)
		return;
	if (phase == 2)
	{
		ask_frames++;
		ask_window_frames++;
		return;
	}
	if (phase != 1 || ++ask_ticks % ASK_EVERY)
		return;
	length = snprintf(line, sizeof(line), "askcensus: %d ticks to %lu, %lu frames |",
		ASK_EVERY, ask_ticks, ask_window_frames);
	for (site = 1; site < ASK_SITES && length < (int)sizeof(line) - 40; site++)
	{
		unsigned long t = ask_count[1][site], d = ask_count[2][site], o = ask_count[0][site];

		if (t | d | o)
			length += snprintf(line + length, sizeof(line) - length, " %d %lu/%lu/%lu", site, t, d, o);
		ask_count[0][site] = ask_count[1][site] = ask_count[2][site] = 0;
	}
	platform_psp_log("%s", line);
	ask_window_frames = 0;
}

#endif
