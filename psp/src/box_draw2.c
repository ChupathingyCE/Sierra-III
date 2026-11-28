/* The far kind's sub-lists' arena (FARCALL; psp/include/box_draw2.h,
docs/box_draw_main.md section 4.2a). The main CPU's only: the Media Engine
reads none of it, and the GE reads a set's words once they are written back.

Taken from the heap at the first frame that draws (a map's load): three sets
of DRAW2_SET_BYTES, 36 KB. Without it every far model is drawn the old way */
#include <pspkernel.h>
#include <malloc.h>
#include <stddef.h>
#include <string.h>
#include "../include/box_draw2.h"

/* the SDK's malloc.h doesn't declare it under the port's flags (farcall1's
build 4, sierra-batch 46087213827); as psp_dsound.c does */
void *memalign(size_t alignment, size_t size);
void platform_log(const char *format, ...);

static unsigned int *draw2_arena = NULL;
static int draw2_failed = 0;
static unsigned int draw2_set = 0, draw2_used = 0; /* this frame's set and its words */
static unsigned int draw2_high = 0, draw2_frames = 0;
static unsigned long draw2_refused = 0, draw2_subs = 0;

#define DRAW2_SET_WORDS (DRAW2_SET_BYTES / 4)

void draw2_frame_begin(void)
{
	if (!draw2_arena && !draw2_failed)
	{
		draw2_arena = memalign(64, DRAW2_SET_BYTES * DRAW2_SETS);
		draw2_failed = !draw2_arena;
		platform_log(draw2_arena ? "GE: draw2: the far sub-lists' arena, %d bytes" :
			"GE: draw2: no heap for the far sub-lists' arena (%d bytes): far models drawn the old way",
			DRAW2_SET_BYTES * DRAW2_SETS);
	}
	if (draw2_used > draw2_high)
		draw2_high = draw2_used;
	draw2_set = (draw2_set + 1) % DRAW2_SETS;
	draw2_used = 0;
	draw2_frames++;
}

int draw2_open(struct draw2_sub *sub, unsigned int max_words)
{
	unsigned int *set;

	if (!draw2_arena || draw2_used + max_words > DRAW2_SET_WORDS)
	{
		if (draw2_arena)
			draw2_refused++;
		return 0;
	}
	set = draw2_arena + draw2_set * DRAW2_SET_WORDS;
	sub->start = sub->at = set + draw2_used;
	sub->end = sub->start + max_words;
	return 1;
}

unsigned int draw2_close(struct draw2_sub *sub)
{
	unsigned int bytes;

	*sub->at++ = 0x0Bu << 24; /* RET */
	bytes = (unsigned int)((char *)sub->at - (char *)sub->start);
	/* the next sub-list starts on its own line, so a write-back never takes a
	line the next model's words are still being written into */
	draw2_used += ((bytes + 63) & ~63u) / 4;
	sceKernelDcacheWritebackRange(sub->start, bytes);
	draw2_subs++;
	return bytes;
}

static unsigned long draw2_why_models[DRAW2_WHYS], draw2_why_words[DRAW2_WHYS];
static unsigned int draw2_old_frame = 0, draw2_old_high = 0, draw2_old_at = 0;

int draw2_tally(int why, const void *block, unsigned int mask_low, unsigned int mask_high)
{
	const unsigned char *at;
	unsigned int group, words = 1;

	for (at = block, group = 0; at && group < 64; group++)
	{
		const unsigned int *head = (const unsigned int *)at;
		int shown = group < 32 ? (mask_low >> group) & 1 : (mask_high >> (group - 32)) & 1;

		if (shown && head[2] && head[4] >= 3)
		{
			unsigned int source = (head[15] >> 16) & 7;

			words += (source >= 1 && source <= 4 ? 4 : 1) + 6 +
				13 * ((head[1] & 0x20000u) ? (head[11] > 8 ? 8 : head[11]) + 1 : 1);
		}
		at = head[14] ? at + head[14] : NULL;
	}
	draw2_why_models[why]++;
	draw2_why_words[why] += words;
	if (draw2_old_at != draw2_frames)
	{
		draw2_old_at = draw2_frames;
		draw2_old_frame = 0;
	}
	draw2_old_frame += words;
	if (draw2_old_frame > draw2_old_high)
		draw2_old_high = draw2_old_frame;
	return 1;
}

static unsigned long draw2_stamp_us[DRAW2_STAMPS], draw2_stamp_calls[DRAW2_STAMPS], draw2_stamp_words = 0;

void draw2_stamp(int part, unsigned int us, unsigned int words)
{
	draw2_stamp_us[part] += us;
	draw2_stamp_calls[part]++;
	draw2_stamp_words += words;
}

void draw2_report(void)
{
	if (!draw2_arena)
		return;
	platform_log("GE: draw2: the emit on the main CPU in %u frames, us (calls): words written %lu (%lu), %lu words; the CALL's record %lu (%lu); refused %lu (%lu)",
		draw2_frames, draw2_stamp_us[DRAW2_STAMP_WRITE], draw2_stamp_calls[DRAW2_STAMP_WRITE], draw2_stamp_words,
		draw2_stamp_us[DRAW2_STAMP_CALL], draw2_stamp_calls[DRAW2_STAMP_CALL],
		draw2_stamp_us[DRAW2_STAMP_REFUSED], draw2_stamp_calls[DRAW2_STAMP_REFUSED]);
	memset(draw2_stamp_us, 0, sizeof(draw2_stamp_us));
	memset(draw2_stamp_calls, 0, sizeof(draw2_stamp_calls));
	draw2_stamp_words = 0;
	platform_log("GE: draw2: %lu far sub-lists in %u frames, the most words a frame %u of %d, %lu models drawn the old way for want of room",
		draw2_subs, draw2_frames, draw2_high, DRAW2_SET_WORDS, draw2_refused);
	platform_log("GE: draw2: old-way far models (words) in %u frames: rso sent %lu (%lu), rso capturing %lu (%lu), draw2_far 0 %lu (%lu); the most words a frame %u",
		draw2_frames, draw2_why_models[DRAW2_RSO_SENT], draw2_why_words[DRAW2_RSO_SENT],
		draw2_why_models[DRAW2_RSO_CAUGHT], draw2_why_words[DRAW2_RSO_CAUGHT],
		draw2_why_models[DRAW2_FELL], draw2_why_words[DRAW2_FELL], draw2_old_high);
	draw2_subs = draw2_refused = 0;
	draw2_frames = draw2_high = 0;
	memset(draw2_why_models, 0, sizeof(draw2_why_models));
	memset(draw2_why_words, 0, sizeof(draw2_why_words));
	draw2_old_frame = draw2_old_high = 0;
	draw2_old_at = 0;
}
