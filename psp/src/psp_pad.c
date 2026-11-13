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
#ifdef PSP_PADPATH
void psp_path_exit(void);
#endif

void psp_pad_exit(void)
{
	if (pad_mode == 1)
		pad_close();
#ifdef PSP_PADPATH
	psp_path_exit();
#endif
}

#ifdef PSP_PADPATH

/* The pad path (docs/pad_path.md): where the player was each tick, beside
his inputs, so a replay on a build that plays differently can be measured
against his route and, later, held to it.

"pathrec": one record a game tick to DATA_ROOT/log/path_<map load>.path
("pathlog": its bytes in the log too, as "path: data" lines). It only reads
the game, so a run plays the same with it; made while a pad replays on its
recording's build, the file is that pad's path. "padwatch": DATA_ROOT/pad.path
is read beside the game and the player's distance from it logged for every
PATH_REPORT ticks; nothing is corrected. Both log the same "census:" line,
so two runs' populations compare window by window. Build switch
PSP_PADPATH=1 */
struct psp_path_state
{
	long game_time;
	float position[3]; /* the object he rides, or his own on foot */
	float yaw, pitch;  /* the look he asks for (player_control's desired angles) */
	float forward[3], up[3];
	short seat, weapon, bsp;
	short actors, objects, effects;
	unsigned char flags;
};
int psp_path_state_get(struct psp_path_state *state); /* players.c (psp_fixups.py) */

#define PATH_POINT 0x50u /* 'P': a tick's record */
#define PATH_HEAD 0x48u  /* 'H': the file's first record */
#define PATH_POINT_BYTES 32
#define PATH_HEAD_BYTES 8
#define PATH_VERSION 1
#define PATH_OUT (16 * 1024)
#define PATH_REPORT 300 /* ticks a "watch:" and a "census:" line cover */
#define PATH_FLAG_CONTROL 1
#define PATH_FLAG_SEATED 2
#define PATH_FLAG_ZOOMED 4

static int path_mode = -1; /* bit 0 recording, bit 1 watching */
static int path_generation = -1;
static int path_log_on = 0;
/* recording */
static unsigned char *path_out = NULL;
static unsigned int path_out_used = 0;
static SceUID path_file = -1;
static unsigned long path_points = 0;
/* watching: the same window scheme as the pad's */
static unsigned char *path_in = NULL;
static unsigned int path_in_size = 0, path_in_at = 0, path_in_base = 0, path_in_fill = 0;
static SceUID path_in_file = -1;
static int path_in_ended = 1;
static long path_last_record = -1, path_last_tick = -1;
static int path_skip_to_restart = 0;

/* a window's sums, by game time */
static struct
{
	long window;
	unsigned long ticks, actors, objects, effects;
	/* the watcher's */
	unsigned long compared, no_record, over_small, over_snap, over_far, seat_differs, weapon_differs, bsp_differs;
	float error_sum, error_max, view_max;
} path_sum = { -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.f, 0.f, 0.f };

static void path_log(const unsigned char *data, unsigned int bytes)
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
		platform_log("path: data %s", line);
	}
}

static void path_flush(void)
{
	if (path_file >= 0 && path_out_used)
	{
		sceIoWrite(path_file, path_out, path_out_used);
		if (path_log_on)
			path_log(path_out, path_out_used);
	}
	path_out_used = 0;
}

static void path_put(unsigned int tag, const void *data, unsigned int bytes)
{
	if (path_file < 0)
		return;
	if (path_out_used + 1 + bytes > PATH_OUT)
		path_flush();
	path_out[path_out_used++] = (unsigned char)tag;
	memcpy(path_out + path_out_used, data, bytes);
	path_out_used += bytes;
}

static void path_close(void)
{
	if (path_file >= 0)
	{
		path_flush();
		sceIoClose(path_file);
		platform_log("path: recorded %lu ticks (map load %d)", path_points, path_generation);
	}
	path_file = -1;
	path_points = 0;
}

static int path_clamp(float value, float scale, int low, int high)
{
	float scaled = value * scale;
	int whole = (int)(scaled < 0.f ? scaled - 0.5f : scaled + 0.5f);

	return whole < low ? low : whole > high ? high : whole;
}

/* a tick's record, 32 bytes, byte by byte so nothing depends on alignment:
game time 4, position 12, yaw 2 (a turn in 65,536ths), pitch 2 (a half turn
in +-32,768ths), seat 1, flags 1, weapon 2, bsp 1, forward 3, up 3, spare 1 */
static void path_pack(const struct psp_path_state *state, unsigned char *record)
{
	unsigned long time = (unsigned long)state->game_time;
	int yaw = path_clamp(state->yaw, 65536.f / 6.2831853f, 0, 65535);
	int pitch = path_clamp(state->pitch, 32768.f / 3.1415927f, -32768, 32767);
	int k;

	memset(record, 0, PATH_POINT_BYTES);
	record[0] = (unsigned char)time;
	record[1] = (unsigned char)(time >> 8);
	record[2] = (unsigned char)(time >> 16);
	record[3] = (unsigned char)(time >> 24);
	memcpy(record + 4, state->position, 12);
	record[16] = (unsigned char)yaw;
	record[17] = (unsigned char)(yaw >> 8);
	record[18] = (unsigned char)pitch;
	record[19] = (unsigned char)(pitch >> 8);
	record[20] = (unsigned char)state->seat;
	record[21] = state->flags;
	record[22] = (unsigned char)state->weapon;
	record[23] = (unsigned char)(state->weapon >> 8);
	record[24] = (unsigned char)state->bsp;
	for (k = 0; k < 3; k++)
	{
		record[25 + k] = (unsigned char)path_clamp(state->forward[k], 127.f, -127, 127);
		record[28 + k] = (unsigned char)path_clamp(state->up[k], 127.f, -127, 127);
	}
}

static void path_unpack(const unsigned char *record, struct psp_path_state *state)
{
	int k;

	memset(state, 0, sizeof(*state));
	state->game_time = (long)((unsigned long)record[0] | (unsigned long)record[1] << 8 |
		(unsigned long)record[2] << 16 | (unsigned long)record[3] << 24);
	memcpy(state->position, record + 4, 12);
	state->yaw = (float)(record[16] | record[17] << 8) * (6.2831853f / 65536.f);
	state->pitch = (float)(short)(record[18] | record[19] << 8) * (3.1415927f / 32768.f);
	state->seat = (signed char)record[20];
	state->flags = record[21];
	state->weapon = (short)(record[22] | record[23] << 8);
	state->bsp = (signed char)record[24];
	for (k = 0; k < 3; k++)
	{
		state->forward[k] = (float)(signed char)record[25 + k] * (1.f / 127.f);
		state->up[k] = (float)(signed char)record[28 + k] * (1.f / 127.f);
	}
}

/* a map load starts a path file of its own, as it starts a pad's; the path
watched is read from the first map load only, as the pad replayed is */
static void path_map_check(void)
{
	char path[96];

	if (path_mode < 0)
	{
		path_mode = (psp_keep("pathrec") ? 1 : 0) | (psp_keep("padwatch") ? 2 : 0);
		if ((path_mode & 1) && !(path_out = malloc(PATH_OUT)))
			path_mode &= ~1;
		path_log_on = (path_mode & 1) && psp_keep("pathlog");
	}
	if (!path_mode || path_generation == psp_map_generation)
		return;
	path_generation = psp_map_generation;
	path_sum.window = -1;
	if (path_mode & 1)
	{
		unsigned char head[PATH_HEAD_BYTES] = { 'P', 'T', 'H', PATH_VERSION, PATH_POINT_BYTES, 0, 0, 0 };

		path_close();
		snprintf(path, sizeof(path), "%s/log/path_%d.path", PSP_DATA_ROOT, path_generation);
		path_file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
		platform_log("path: recording to %s%s", path, path_file < 0 ? " (couldn't open it: not recording)" : "");
		path_put(PATH_HEAD, head, sizeof(head));
	}
	if ((path_mode & 2) && !path_in)
	{
		SceOff size;
		unsigned char head[1 + PATH_HEAD_BYTES];

		snprintf(path, sizeof(path), "%s/pad.path", PSP_DATA_ROOT);
		path_in_file = sceIoOpen(path, PSP_O_RDONLY, 0);
		size = path_in_file >= 0 ? sceIoLseek(path_in_file, 0, PSP_SEEK_END) : 0;
		if (path_in_file >= 0 && size > (SceOff)sizeof(head) && size < 16 * 1024 * 1024 &&
			(path_in = malloc(PAD_WINDOW)) != NULL)
		{
			sceIoLseek(path_in_file, 0, PSP_SEEK_SET);
			path_in_size = (unsigned int)size;
			path_in_ended = sceIoRead(path_in_file, head, sizeof(head)) != (int)sizeof(head) ||
				head[0] != PATH_HEAD || head[1] != 'P' || head[2] != 'T' || head[3] != 'H' ||
				head[4] != PATH_VERSION || head[5] != PATH_POINT_BYTES;
			path_in_at = path_in_base = sizeof(head);
			path_in_fill = 0;
		}
		else if (path_in_file >= 0)
		{
			sceIoClose(path_in_file);
			path_in_file = -1;
		}
		path_last_record = path_last_tick = -1;
		path_skip_to_restart = 0;
		platform_log("path: watching %s, %u bytes%s", path, path_in_size,
			!path_in ? " (none: nothing to watch)" : path_in_ended ? " (not a path this build reads)" : "");
	}
	else if (path_mode & 2)
	{
		if (!path_in_ended)
			platform_log("path: the watch ended at a map load, %u of %u bytes read", path_in_at, path_in_size);
		path_in_ended = 1;
	}
}

/* the path's next record without taking it: 1 with it unpacked, 0 at the end */
static int path_peek(struct psp_path_state *state)
{
	unsigned int keep, got;
	int read;

	if (path_in_ended || !path_in)
		return 0;
	if (path_in_at + 1 + PATH_POINT_BYTES > path_in_base + path_in_fill && path_in_file >= 0)
	{
		keep = path_in_base + path_in_fill - path_in_at;
		memmove(path_in, path_in + (path_in_at - path_in_base), keep);
		path_in_base = path_in_at;
		path_in_fill = keep;
		read = sceIoRead(path_in_file, path_in + keep, PAD_WINDOW - keep);
		got = read > 0 ? (unsigned int)read : 0;
		path_in_fill += got;
		if (path_in_base + path_in_fill >= path_in_size || !got)
		{
			sceIoClose(path_in_file);
			path_in_file = -1;
		}
	}
	if (path_in_at + 1 + PATH_POINT_BYTES > path_in_base + path_in_fill ||
		path_in[path_in_at - path_in_base] != PATH_POINT)
	{
		platform_log("path: the watch %s at byte %u of %u", path_in_at >= path_in_size ? "ended" : "lost the file's records",
			path_in_at, path_in_size);
		path_in_ended = 1;
		return 0;
	}
	path_unpack(path_in + (path_in_at - path_in_base) + 1, state);
	return 1;
}

static float path_abs(float value)
{
	return value < 0.f ? -value : value;
}

/* the player against the path's record of this tick. Game time going back
(a revert, a restart) is followed on both sides: the path's records are
skipped to where its own time went back */
static void path_watch(const struct psp_path_state *now)
{
	struct psp_path_state then;
	float dx, dy, dz, error, yaw, pitch, view;

	if (path_last_tick >= 0 && now->game_time < path_last_tick)
		path_skip_to_restart = 1;
	path_last_tick = now->game_time;
	for (;;)
	{
		if (!path_peek(&then))
			return;
		if (path_last_record >= 0 && then.game_time < path_last_record)
		{
			if (!path_skip_to_restart)
				break; /* the path went back and the game hasn't: wait for it */
			path_skip_to_restart = 0;
			path_last_record = -1;
		}
		if (path_skip_to_restart || then.game_time < now->game_time)
		{
			path_last_record = then.game_time;
			path_in_at += 1 + PATH_POINT_BYTES;
			continue;
		}
		if (then.game_time > now->game_time)
			break;
		path_last_record = then.game_time;
		path_in_at += 1 + PATH_POINT_BYTES;
		dx = now->position[0] - then.position[0];
		dy = now->position[1] - then.position[1];
		dz = now->position[2] - then.position[2];
		/* a bound, not the distance: the flat part within 8% over, or the
		height if larger, is enough to sort a tick into its band */
		error = path_abs(dx) > path_abs(dy) ? path_abs(dx) + 0.41f * path_abs(dy) : path_abs(dy) + 0.41f * path_abs(dx);
		if (path_abs(dz) > error)
			error = path_abs(dz);
		yaw = path_abs(now->yaw - then.yaw);
		if (yaw > 3.1415927f)
			yaw = 6.2831853f - yaw;
		pitch = path_abs(now->pitch - then.pitch);
		view = (yaw > pitch ? yaw : pitch) * 57.29578f;
		path_sum.compared++;
		path_sum.error_sum += error;
		if (error > path_sum.error_max)
			path_sum.error_max = error;
		if (view > path_sum.view_max)
			path_sum.view_max = view;
		path_sum.over_small += error > 0.05f;
		path_sum.over_snap += error > 0.5f;
		path_sum.over_far += error > 2.f;
		path_sum.seat_differs += now->seat != then.seat;
		path_sum.weapon_differs += now->weapon != then.weapon;
		path_sum.bsp_differs += now->bsp != then.bsp;
		return;
	}
	path_sum.no_record++;
}

static void path_report(void)
{
	long from = path_sum.window * PATH_REPORT;

	if (path_sum.window < 0 || !path_sum.ticks)
		return;
	platform_log("census: ticks %ld-%ld (%lu) | actors %lu | objects %lu | effects %lu (each summed over the ticks)", from,
		from + PATH_REPORT - 1, path_sum.ticks, path_sum.actors, path_sum.objects, path_sum.effects);
	if (path_mode & 2)
		platform_log("watch: ticks %ld-%ld | compared %lu, no record %lu | off the path: mean %.3f max %.3f | ticks over 0.05: %lu, "
			"over 0.5: %lu, over 2: %lu | view max %.2f deg | seat differs %lu | weapon differs %lu | bsp differs %lu",
			from, from + PATH_REPORT - 1, path_sum.compared, path_sum.no_record,
			path_sum.compared ? (double)(path_sum.error_sum / (float)path_sum.compared) : 0.0, (double)path_sum.error_max,
			path_sum.over_small, path_sum.over_snap, path_sum.over_far, (double)path_sum.view_max, path_sum.seat_differs,
			path_sum.weapon_differs, path_sum.bsp_differs);
	memset(&path_sum, 0, sizeof(path_sum));
	path_sum.window = -1;
}

/* psp_platform.c, after each game tick */
void psp_path_tick(void)
{
	struct psp_path_state now;
	unsigned char record[PATH_POINT_BYTES];
	long window;

	if (path_mode == 0)
		return;
	path_map_check();
	if (!path_mode || !psp_path_state_get(&now))
		return;
	window = now.game_time / PATH_REPORT;
	if (window != path_sum.window)
	{
		path_report();
		path_sum.window = window;
	}
	path_sum.ticks++;
	path_sum.actors += (unsigned long)now.actors;
	path_sum.objects += (unsigned long)now.objects;
	path_sum.effects += (unsigned long)now.effects;
	if (path_mode & 1)
	{
		path_pack(&now, record);
		path_put(PATH_POINT, record, sizeof(record));
		/* written every 256 ticks, so a run that ends by quitting keeps
		all but its last seconds */
		if ((++path_points & 255) == 0)
			path_flush();
	}
	if (path_mode & 2)
		path_watch(&now);
}

/* the exit: the last window's lines, the path's last records written */
void psp_path_exit(void)
{
	if (path_mode > 0)
	{
		path_report();
		if (path_mode & 1)
			path_close();
	}
}
#endif /* PSP_PADPATH */
