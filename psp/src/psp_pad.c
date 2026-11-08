/* The pad recorder (docs/pad_recorder.md): a hand run's inputs written as
the game acts on them, so the rig can replay Sheepdog01's fights as benches.

"padrec": each frame's seconds_elapsed (main.c: how many ticks the frame
runs) and each local player's input_blob (player_control.c: the input after
the pad, the sensitivity, the aim assist and the inhibitions) go to
DATA_ROOT/log/pad_<map load>.bin, one file a map load. "padplay": a map
load reads DATA_ROOT/pad.bin whole and the frames take their time steps and
the players their blobs from it, so the replay runs the same ticks with the
same inputs however fast its build draws. With fixedseed on both sides the
game then plays the same (the PSP's two races fixed first: tock162 edc2d39,
74cd771) */
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp_paths.h"

int psp_keep(const char *name);
void platform_log(const char *format, ...);
extern int psp_map_generation;

#define PAD_FRAME 0x46u /* 'F': a frame's seconds_elapsed */
#define PAD_BLOB 0x42u  /* 'B': a local player's input_blob */
#define PAD_BLOB_BYTES 32
#define PAD_CHUNK (64 * 1024)

static int pad_mode = -1; /* 0 off, 1 recording, 2 replaying */
static int pad_generation = -1;
/* recording */
static unsigned char *pad_out = NULL;
static unsigned int pad_out_used = 0;
static SceUID pad_file = -1;
static unsigned long pad_frames = 0, pad_blobs = 0;
/* replaying */
static unsigned char *pad_in = NULL;
static unsigned int pad_in_size = 0, pad_in_at = 0;
static int pad_in_ended = 0;

static void pad_flush(void)
{
	if (pad_file >= 0 && pad_out_used)
		sceIoWrite(pad_file, pad_out, pad_out_used);
	pad_out_used = 0;
}

static void pad_close(void)
{
	if (pad_file >= 0)
	{
		pad_flush();
		sceIoClose(pad_file);
		platform_log("pad: recorded %lu frames, %lu player inputs (map load %d)", pad_frames, pad_blobs,
			pad_generation);
	}
	pad_file = -1;
	pad_frames = pad_blobs = 0;
}

/* a map load starts a recording, or a replay, from its own beginning */
static void pad_map_check(void)
{
	char path[96];

	if (pad_mode < 0)
	{
		pad_mode = psp_keep("padplay") ? 2 : psp_keep("padrec") ? 1 : 0;
		if (pad_mode == 1 && !(pad_out = malloc(PAD_CHUNK)))
			pad_mode = 0;
	}
	if (!pad_mode || pad_generation == psp_map_generation)
		return;
	pad_generation = psp_map_generation;
	if (pad_mode == 1)
	{
		pad_close();
		snprintf(path, sizeof(path), "%s/log/pad_%d.bin", PSP_DATA_ROOT, pad_generation);
		pad_file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
		platform_log("pad: recording to %s%s", path, pad_file < 0 ? " (couldn't open it: not recording)" : "");
		return;
	}
	/* a replay plays the file from the first map load only: a revert or the
	next map has no recording of its own */
	if (pad_in)
	{
		if (!pad_in_ended)
			platform_log("pad: replay ended at a map load, %u of %u bytes played", pad_in_at, pad_in_size);
		pad_in_ended = 1;
		return;
	}
	{
		SceUID file;
		SceOff size;

		snprintf(path, sizeof(path), "%s/pad.bin", PSP_DATA_ROOT);
		file = sceIoOpen(path, PSP_O_RDONLY, 0);
		size = file >= 0 ? sceIoLseek(file, 0, PSP_SEEK_END) : 0;
		if (file >= 0 && size > 0 && size < 16 * 1024 * 1024 && (pad_in = malloc((unsigned int)size)) != NULL)
		{
			sceIoLseek(file, 0, PSP_SEEK_SET);
			pad_in_size = (unsigned int)sceIoRead(file, pad_in, (unsigned int)size);
		}
		if (file >= 0)
			sceIoClose(file);
		pad_in_at = 0;
		pad_in_ended = !pad_in;
		platform_log("pad: replaying %s, %u bytes%s", path, pad_in_size, pad_in ? "" : " (none: the game's own input)");
	}
}

static void pad_put(unsigned int tag, const void *data, unsigned int bytes)
{
	if (pad_file < 0)
		return;
	if (pad_out_used + 1 + bytes > PAD_CHUNK)
		pad_flush();
	pad_out[pad_out_used++] = (unsigned char)tag;
	memcpy(pad_out + pad_out_used, data, bytes);
	pad_out_used += bytes;
}

/* the next record of the replay, which must be tag: 1 with its bytes, 0 at
the end or a mismatch (said once; the game's own input from then on) */
static int pad_take(unsigned int tag, void *data, unsigned int bytes)
{
	if (pad_in_ended || !pad_in)
		return 0;
	if (pad_in_at + 1 + bytes > pad_in_size || pad_in[pad_in_at] != tag)
	{
		platform_log("pad: replay %s at byte %u of %u (wanted '%c', found '%c')",
			pad_in_at >= pad_in_size ? "ended" : "parted from the recording", pad_in_at, pad_in_size, tag,
			pad_in_at < pad_in_size ? pad_in[pad_in_at] : '-');
		pad_in_ended = 1;
		return 0;
	}
	memcpy(data, pad_in + pad_in_at + 1, bytes);
	pad_in_at += 1 + bytes;
	return 1;
}

/* main.c, each frame: the frame's time step, recorded or replayed */
float psp_pad_seconds(float seconds)
{
	pad_map_check();
	if (pad_mode == 1)
	{
		pad_put(PAD_FRAME, &seconds, sizeof(seconds));
		pad_frames++;
	}
	else if (pad_mode == 2)
	{
		float recorded;

		if (pad_take(PAD_FRAME, &recorded, sizeof(recorded)))
			return recorded;
	}
	return seconds;
}

/* player_control.c, each local player each frame: the input the game acts
on, recorded or replaced by the recording's */
void psp_pad_blob(int local_player_index, void *blob, int bytes)
{
	unsigned char record[1 + PAD_BLOB_BYTES];

	if (pad_mode <= 0 || bytes != PAD_BLOB_BYTES)
		return;
	if (pad_mode == 1)
	{
		record[0] = (unsigned char)local_player_index;
		memcpy(record + 1, blob, PAD_BLOB_BYTES);
		pad_put(PAD_BLOB, record, sizeof(record));
		pad_blobs++;
	}
	else if (pad_take(PAD_BLOB, record, sizeof(record)) && record[0] == (unsigned char)local_player_index)
		memcpy(blob, record + 1, PAD_BLOB_BYTES);
}
