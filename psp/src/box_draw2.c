/* The far kind's sub-lists' arena (FARCALL; psp/include/box_draw2.h,
docs/box_draw_main.md section 4.2a). The main CPU's only: the Media Engine
reads none of it, and the GE reads a set's words once they are written back.

Taken from the heap at the first frame that draws (a map's load): three sets
of DRAW2_SET_BYTES, 36 KB. Without it every far model is drawn the old way */
#include <pspkernel.h>
#include <malloc.h>
#include <stddef.h>
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

void draw2_report(void)
{
	if (!draw2_arena)
		return;
	platform_log("GE: draw2: %lu far sub-lists in %u frames, the most words a frame %u of %d, %lu models drawn the old way for want of room",
		draw2_subs, draw2_frames, draw2_high, DRAW2_SET_WORDS, draw2_refused);
	draw2_subs = draw2_refused = 0;
	draw2_frames = draw2_high = 0;
}
