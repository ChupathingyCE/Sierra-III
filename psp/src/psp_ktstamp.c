/* Kill test 2's stage stamps ("ktstamp"; docs/ktstamp.md, the lab's decisions.md 2026-11-27 03:0x): the fields'
sums, the sampled clock and the report. Built only with PSP_KTSTAMP=1 (psp_build.py); counting only with keep word
ktstamp. The clock is sceKernelGetSystemTimeLow read directly (psp_microseconds answers 0 under notimers, the
campaign's default), on the ticks psp_object_sample times (one in 8); a field's time is the mean over those timed
ticks, its calls over the same ticks. The owed makes are also counted by caller site on every tick (ask A's
census, on the emulator). One line a 300-tick window of game time, as bpcount's (psp_fixups.py BP_HEAD) */
#include <string.h>
#include <stdio.h>
#include "psp_ktstamp.h"

int psp_keep(const char *name);
int psp_keep_loaded(void);
void platform_psp_log(const char *format, ...);
long game_time_get(void);
unsigned int sceKernelGetSystemTimeLow(void);

int psp_kt_on = 0, psp_kt_count = 0;
unsigned long psp_kt_marks[PSP_KT_MARKS];

static int kt_keep = -1;
static long kt_window = -1;
static unsigned long kt_ticks, kt_timed;
static unsigned long kt_us[PSP_KT_FIELDS], kt_calls[PSP_KT_FIELDS];
/* the owed makes by caller site, every tick: as psp_nmr_site's 12 (FIXES_134), the rest in the last */
#define KT_SITES 12
static unsigned long kt_site[KT_SITES], kt_site_calls[KT_SITES], kt_owed_all;
static const char *const kt_names[PSP_KT_FIELDS] = {"pass", "I", "G", "M", "settle", "owed", "bupdate", "bphys"};

unsigned long psp_kt_clock(void)
{
	return sceKernelGetSystemTimeLow();
}

void psp_kt_add(int field, unsigned long mark)
{
	kt_us[field] += (unsigned long)sceKernelGetSystemTimeLow() - mark;
	kt_calls[field]++;
}

/* store_biped_pass's end: its stage edges 0-5 (store_coll.c) made into the five fields */
void psp_kt_pass(void)
{
	unsigned long end = sceKernelGetSystemTimeLow();

	kt_us[PSP_KT_PASS_FIELD] += end - psp_kt_marks[0];
	kt_us[PSP_KT_I] += psp_kt_marks[2] - psp_kt_marks[1];
	kt_us[PSP_KT_G] += psp_kt_marks[3] - psp_kt_marks[2];
	kt_us[PSP_KT_M] += psp_kt_marks[4] - psp_kt_marks[3];
	kt_us[PSP_KT_SETTLE] += end - psp_kt_marks[4];
	kt_calls[PSP_KT_PASS_FIELD]++;
	kt_calls[PSP_KT_I]++;
	kt_calls[PSP_KT_G]++;
	kt_calls[PSP_KT_M]++;
	kt_calls[PSP_KT_SETTLE]++;
}

/* a make store_pose_make_skipped did: by site every tick, timed on a timed tick (mark 0: not timed) */
void psp_kt_owed(void *site, unsigned long mark)
{
	unsigned long at = (unsigned long)site;
	int k;

	if (mark)
		psp_kt_add(PSP_KT_OWED, mark);
	kt_owed_all++;
	for (k = 0; k < KT_SITES - 1 && kt_site[k] && kt_site[k] != at; k++)
		;
	if (!kt_site[k])
		kt_site[k] = at;
	kt_site_calls[k]++;
}

static void kt_report(long from)
{
	char line[640], sites[400];
	int at = 0, k;
	unsigned long timed = kt_timed ? kt_timed : 1, ticks = kt_ticks ? kt_ticks : 1;

	for (k = 0; k < PSP_KT_FIELDS; k++)
	{
		unsigned long us = kt_us[k] / timed, hundredths = kt_calls[k] * 100 / timed;

		if (!kt_calls[k])
			continue;
		at += snprintf(line + at, sizeof(line) - at, "%s%s %lu.%03lu (%lu.%02lu) [%lu]", at ? "; " : "", kt_names[k],
			us / 1000, us % 1000, hundredths / 100, hundredths % 100, kt_us[k]);
		if (at >= (int)sizeof(line))
			break;
	}
	platform_psp_log("ktstamp: gt %ld-%ld, %lu ticks (%lu timed): ms a timed tick (calls a timed tick) [us summed]: %s",
		from, from + 300, kt_ticks, kt_timed, at ? line : "none");
	if (kt_owed_all)
	{
		at = 0;
		for (k = 0; k < KT_SITES && kt_site[k] && at < (int)sizeof(sites); k++)
			at += snprintf(sites + at, sizeof(sites) - at, " %08lx:%lu", kt_site[k], kt_site_calls[k]);
		platform_psp_log("ktstamp: owed makes, every tick: %lu (%lu.%02lu a tick); by caller site (the last slot takes the rest):%s",
			kt_owed_all, kt_owed_all / ticks, kt_owed_all * 100 / ticks % 100, sites);
	}
}

/* psp_object_sample's tick (psp_platform.c): this tick's scale passes through; the window's report when game time
crosses a 300-tick boundary; after a jump in game time (a load) the window it left is dropped, not reported */
unsigned long psp_kt_tick(unsigned long scale)
{
	long window;

	if (kt_keep < 0)
	{
		if (!psp_keep_loaded())
			return scale;
		kt_keep = psp_keep("ktstamp") != 0;
		if (kt_keep)
			platform_psp_log("ktstamp: on (docs/ktstamp.md): the biped stages' clock on psp_object_sample's timed ticks, a line a 300-tick window");
	}
	psp_kt_count = kt_keep;
	psp_kt_on = kt_keep && scale != 0;
	if (!kt_keep)
		return scale;
	window = game_time_get() / 300;
	if (window != kt_window)
	{
		if (kt_window >= 0 && window == kt_window + 1)
			kt_report(kt_window * 300);
		memset(kt_us, 0, sizeof(kt_us));
		memset(kt_calls, 0, sizeof(kt_calls));
		memset(kt_site, 0, sizeof(kt_site));
		memset(kt_site_calls, 0, sizeof(kt_site_calls));
		kt_owed_all = kt_ticks = kt_timed = 0;
		kt_window = window;
	}
	kt_ticks++;
	if (psp_kt_on)
		kt_timed++;
	return scale;
}
