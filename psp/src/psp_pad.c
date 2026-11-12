/* The pad recorder (docs/pad_recorder.md): a hand run's inputs written as
the game acts on them, so the rig can replay Sheepdog01's fights as benches.

"padrec": each frame's seconds_elapsed (main.c: how many ticks the frame
runs) and each local player's input_blob (player_control.c: the input after
the pad, the sensitivity, the aim assist and the inhibitions) go to
DATA_ROOT/log/pad_<map load>.pad, one file a map load. "padplay": a map
load reads DATA_ROOT/pad.pad and the frames take their time steps and the
players their blobs from it, PAD_WINDOW bytes at a time (read whole, b30's
264 KB fight took that much heap from the game it benched: R's milestone
pairs lost the copies' arena on the PSP, the lab's sierra-hw 42706253796 and
42713237567), so the replay runs the same ticks with the
same inputs however fast its build draws. With fixedseed on both sides the
game then plays the same (the PSP's two races fixed first: tock162 edc2d39,
74cd771) */
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp_paths.h"

int psp_keep(const char *name);
int psp_keep_number(const char *prefix, int fallback);
void platform_log(const char *format, ...);
extern int psp_map_generation;

#define PAD_FRAME 0x46u /* 'F': a frame's seconds_elapsed */
#define PAD_BLOB 0x42u  /* 'B': a local player's input_blob */
#define PAD_MENU 0x55u  /* 'U': what the menus did, when it changes (psp_pad_menu) */
#define PAD_BLOB_BYTES 32
#define PAD_CHUNK (64 * 1024)

static int pad_mode = -1; /* 0 off, 1 recording, 2 replaying */
static int pad_generation = -1;
/* recording */
static unsigned char *pad_out = NULL;
static unsigned int pad_out_used = 0;
static SceUID pad_file = -1;
static unsigned long pad_frames = 0, pad_blobs = 0;
/* replaying: the file's bytes from pad_in_base, pad_in_fill of them, in a
window read again as the replay reaches its end (16 KB: ~230 ticks of one
player's inputs, a read every few seconds) */
#define PAD_WINDOW (16 * 1024)
static unsigned char *pad_in = NULL;
static unsigned int pad_in_size = 0, pad_in_at = 0, pad_in_base = 0, pad_in_fill = 0;
static SceUID pad_in_file = -1;
static int pad_in_ended = 0;
/* the menus' state last written, or last read back: a file starts at 0 */
static int pad_menu_state = 0;
static unsigned long pad_menu_changes = 0;

static int pad_log_on = 0;
/* a log line's bytes: the log cuts longer lines (padrec1's 120-byte lines
came back short) */
#define PAD_LOG_BYTES 48

/* "padlog": the recording's bytes also in the log, in hex, so an emulator
run's recording comes back with its log (the results take no binary files):
the lab's psp/sierra/padfromlog.py rebuilds the file */
static void pad_log(const unsigned char *data, unsigned int bytes)
{
	static const char hex[] = "0123456789abcdef";
	char line[2 * PAD_LOG_BYTES + 1];
	unsigned int at, k;

	for (at = 0; at < bytes; at += PAD_LOG_BYTES)
	{
		for (k = 0; k < PAD_LOG_BYTES && at + k < bytes; k++)
		{
			line[2 * k] = hex[data[at + k] >> 4];
			line[2 * k + 1] = hex[data[at + k] & 15];
		}
		line[2 * k] = 0;
		platform_log("pad: data %s", line);
	}
}

static void pad_flush(void)
{
	if (pad_file >= 0 && pad_out_used)
	{
		sceIoWrite(pad_file, pad_out, pad_out_used);
		if (pad_log_on)
			pad_log(pad_out, pad_out_used);
	}
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
		pad_log_on = pad_mode == 1 && psp_keep("padlog");
	}
	if (!pad_mode || pad_generation == psp_map_generation)
		return;
	pad_generation = psp_map_generation;
	if (pad_mode == 1)
	{
		pad_close();
		snprintf(path, sizeof(path), "%s/log/pad_%d.pad", PSP_DATA_ROOT, pad_generation);
		pad_file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
		pad_menu_state = 0;
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
		SceOff size;

		snprintf(path, sizeof(path), "%s/pad.pad", PSP_DATA_ROOT);
		pad_in_file = sceIoOpen(path, PSP_O_RDONLY, 0);
		size = pad_in_file >= 0 ? sceIoLseek(pad_in_file, 0, PSP_SEEK_END) : 0;
		if (pad_in_file >= 0 && size > 0 && size < 16 * 1024 * 1024 && (pad_in = malloc(PAD_WINDOW)) != NULL)
		{
			sceIoLseek(pad_in_file, 0, PSP_SEEK_SET);
			pad_in_size = (unsigned int)size;
		}
		else if (pad_in_file >= 0)
		{
			sceIoClose(pad_in_file);
			pad_in_file = -1;
		}
		pad_in_at = pad_in_base = pad_in_fill = 0;
		pad_in_ended = !pad_in;
		pad_menu_state = 0;
		platform_log("pad: replaying %s, %u bytes, %u at a time%s", path, pad_in_size, (unsigned int)PAD_WINDOW,
			pad_in ? "" : " (none: the game's own input)");
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

/* the window holds the file's bytes [pad_in_at, pad_in_at + bytes), read
on when it doesn't: what's left moves to its start and the rest is filled */
static void pad_window(unsigned int bytes)
{
	unsigned int keep, got;
	int read;

	if (pad_in_at + bytes <= pad_in_base + pad_in_fill || pad_in_file < 0)
		return;
	keep = pad_in_base + pad_in_fill - pad_in_at;
	memmove(pad_in, pad_in + (pad_in_at - pad_in_base), keep);
	pad_in_base = pad_in_at;
	pad_in_fill = keep;
	read = sceIoRead(pad_in_file, pad_in + keep, PAD_WINDOW - keep);
	got = read > 0 ? (unsigned int)read : 0;
	pad_in_fill += got;
	if (pad_in_base + pad_in_fill >= pad_in_size || !got)
	{
		sceIoClose(pad_in_file);
		pad_in_file = -1;
	}
}

/* the next record of the replay, which must be tag: 1 with its bytes, 0 at
the end or a mismatch (said once; the game's own input from then on) */
static int pad_take(unsigned int tag, void *data, unsigned int bytes)
{
	unsigned char const *at;

	if (pad_in_ended || !pad_in)
		return 0;
	pad_window(1 + bytes);
	at = pad_in + (pad_in_at - pad_in_base);
	if (pad_in_at + 1 + bytes > pad_in_base + pad_in_fill || at[0] != tag)
	{
		int in = pad_in_at < pad_in_base + pad_in_fill;

		platform_log("pad: replay %s at byte %u of %u (wanted '%c', found '%c')",
			pad_in_at >= pad_in_size ? "ended" : "parted from the recording", pad_in_at, pad_in_size, tag,
			in ? at[0] : '-');
		pad_in_ended = 1;
		return 0;
	}
	memcpy(data, at + 1, bytes);
	pad_in_at += 1 + bytes;
	return 1;
}

/* psp_platform.c's tick hook ("padend"): a replay that has run out of its
recording; the frames after it take the clock's steps and no input, so two
builds' runs past it differ by their speed alone */
int psp_pad_replay_ended(void)
{
	return pad_mode == 2 && pad_in_ended;
}

/* main.c, each frame: the frame's time step, recorded or replayed */
float psp_pad_seconds(float seconds)
{
	pad_map_check();
	if (pad_mode == 1)
	{
		pad_put(PAD_FRAME, &seconds, sizeof(seconds));
		/* written every 64 frames (~9 KB), so a run that ends by quitting
		keeps all but its last few seconds */
		if ((++pad_frames & 63) == 0)
			pad_flush();
	}
	else if (pad_mode == 2)
	{
		float recorded;

		if (pad_take(PAD_FRAME, &recorded, sizeof(recorded)))
			return recorded;
	}
	return seconds;
}

/* main.c, each frame after the menus' update and before the players'
input: what the menus did that the input blobs don't carry (the pause, and
the pause menu's "restart level" and "restart at checkpoint", whose flags
main.c acts on at the next frame's start). state is the game's: bit 0
paused, bit 1 a revert asked, bit 2 a restart asked. Recording, a change is
written ('U' and a byte); a replay returns the recording's state at this
frame, for main.c to set, or -1 when it isn't replaying. A recording made
before 'U' existed has none, and its replay keeps the state at 0, as the
pause was ignored then */
int psp_pad_menu(int state)
{
	if (pad_mode == 1 && pad_file >= 0)
	{
		/* "padmenutestN" (tests only, recording): the menus' work done here
		as the pause menu would, so the emulator can record one: paused at
		recorded frame N, let go 45 frames on, the level restarted 45 after */
		static int test = -2;

		if (test == -2)
			test = psp_keep_number("padmenutest", 0);
		if (test > 0 && (pad_frames == (unsigned long)test || pad_frames == (unsigned long)test + 45 ||
			pad_frames == (unsigned long)test + 90))
		{
			extern void game_time_set_paused(unsigned char paused);
			extern void main_reset_map(void);

			if (pad_frames == (unsigned long)test + 90)
			{
				main_reset_map();
				state |= 4;
			}
			else
			{
				int pause = pad_frames == (unsigned long)test;

				game_time_set_paused((unsigned char)pause);
				state = (state & ~1) | pause;
			}
			platform_log("pad: padmenutest at frame %lu: state %d", pad_frames, state);
		}
		if (state != pad_menu_state)
		{
			unsigned char byte = (unsigned char)state;

			pad_put(PAD_MENU, &byte, 1);
			pad_menu_state = state;
			if (pad_menu_changes++ < 32)
				platform_log("pad: the menus' state %d recorded at frame %lu", state, pad_frames);
		}
		return -1;
	}
	if (pad_mode != 2 || pad_in_ended || !pad_in)
		return -1;
	pad_window(2);
	if (pad_in_at + 2 <= pad_in_base + pad_in_fill && pad_in[pad_in_at - pad_in_base] == PAD_MENU)
	{
		pad_menu_state = pad_in[pad_in_at - pad_in_base + 1];
		pad_in_at += 2;
		if (pad_menu_changes++ < 32)
			platform_log("pad: the menus' state %d replayed", pad_menu_state);
	}
	return pad_menu_state;
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

/* the exit (exitatN, ladderN, the level's end): the recording's last frames
written and the file closed */
void psp_pad_exit(void)
{
	if (pad_mode == 1)
		pad_close();
}
