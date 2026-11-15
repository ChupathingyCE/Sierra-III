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
#include <pspthreadman.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp_paths.h"

int psp_keep(const char *name);
int psp_keep_number(const char *prefix, int fallback);
#ifdef PSP_PADPATH
static void path_steer(void *blob);
#endif
void platform_log(const char *format, ...);
extern int psp_map_generation;

#define PAD_FRAME 0x46u /* 'F': a frame's seconds_elapsed */
#define PAD_BLOB 0x42u  /* 'B': a local player's input_blob */
#define PAD_MENU 0x55u  /* 'U': what the menus did, when it changes (psp_pad_menu) */
/* 'H': the recording's head, its first record (2026-11-13; the lead 13:16):
a version byte (1), the game's difficulty (0 easy .. 3 legendary), then
init.txt's lines but its map_name, ';' between them, the rest zeros. A
replay reads it and says whether it plays at the recording's difficulty;
older files start with 'F' and have none */
#define PAD_HEAD 0x48u
#define PAD_HEAD_BYTES 64
short game_difficulty_level_get(void);
/* REDOSEED (psp_fixups.py, actors.c): -1 until the first tick decides it,
1 for each actor's own seed, 0 for the game's as before. A pad's head says
which way it was recorded (version 2: own seeds; 1 or no head: the game's),
and a replay plays it the same way, unless the keep word redoseednew asks
for the new seeds on an old pad */
extern int psp_redoseed_on;
static void pad_head_make(unsigned char *head);
static void pad_seed_follow(unsigned int version);
static void pad_put(unsigned int tag, const void *data, unsigned int bytes);
static void pad_window(unsigned int bytes);
static int pad_take(unsigned int tag, void *data, unsigned int bytes);
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

/* "lw2" (hand builds): the recording isn't written from the game thread
every 64 frames. Its 64 KB are two halves; a full half goes to a writer
thread of its own while the game fills the other, so the game thread waits
on the stick only if a half fills before the last one is written. In Sheepdog01's
hand recordings the recorder's writes held the game thread up to 1.45 s
(the lab's findings hand_logs_stick_lock_waits_psp1000) */
static int pad_halves = -1;
static unsigned char *pad_half_base[2];
static int pad_half_now = 0;
static SceUID pad_half_ready = -1, pad_half_free = -1;
static unsigned char *pad_half_out;
static unsigned int pad_half_bytes;
static unsigned long pad_half_waits = 0, pad_half_writes = 0;

static int pad_writer(SceSize args, void *argp)
{
	(void)args;
	(void)argp;
	for (;;)
	{
		sceKernelWaitSema(pad_half_ready, 1, NULL);
		if (pad_file >= 0 && pad_half_bytes)
			sceIoWrite(pad_file, pad_half_out, pad_half_bytes);
		pad_half_writes++;
		sceKernelSignalSema(pad_half_free, 1);
	}
	return 0;
}

static void pad_halves_start(void)
{
	SceUID thread;

	pad_halves = 0;
	if (!psp_keep("lw2") || !pad_out)
		return;
	pad_half_ready = sceKernelCreateSema("halo pad half", 0, 0, 1, NULL);
	pad_half_free = sceKernelCreateSema("halo pad free", 0, 1, 1, NULL);
	thread = pad_half_ready >= 0 && pad_half_free >= 0 ?
		sceKernelCreateThread("halo pad writer", pad_writer, 0x18, 0x1000, 0, NULL) : -1;
	if (thread < 0 || sceKernelStartThread(thread, 0, NULL) < 0)
	{
		platform_log("pad: lw2: no writer thread; written from the game thread as before");
		return;
	}
	pad_half_base[0] = pad_out;
	pad_half_base[1] = pad_out + PAD_CHUNK / 2;
	pad_halves = 1;
	platform_log("pad: lw2: the recording written by its own thread, %d KB at a time", PAD_CHUNK / 2 / 1024);
}

/* the half the game filled, to the writer: waits only for the one before */
static void pad_half_hand(void)
{
	if (sceKernelPollSema(pad_half_free, 1) < 0)
	{
		pad_half_waits++;
		sceKernelWaitSema(pad_half_free, 1, NULL);
	}
	pad_half_out = pad_out;
	pad_half_bytes = pad_out_used;
	if (pad_log_on)
		pad_log(pad_out, pad_out_used);
	sceKernelSignalSema(pad_half_ready, 1);
	pad_half_now ^= 1;
	pad_out = pad_half_base[pad_half_now];
	pad_out_used = 0;
}

/* everything written before the file closes */
static void pad_halves_drain(void)
{
	sceKernelWaitSema(pad_half_free, 1, NULL);
	if (pad_file >= 0 && pad_out_used)
	{
		sceIoWrite(pad_file, pad_out, pad_out_used);
		if (pad_log_on)
			pad_log(pad_out, pad_out_used);
	}
	pad_out_used = 0;
	sceKernelSignalSema(pad_half_free, 1);
}

static void pad_flush(void)
{
	if (pad_halves > 0)
	{
		pad_halves_drain();
		return;
	}
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
		if (pad_halves > 0)
			platform_log("pad: lw2: %lu halves written by the writer, the game waited for it %lu times", pad_half_writes,
				pad_half_waits);
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
		if (pad_mode == 1)
			pad_halves_start();
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
		{
			unsigned char head[PAD_HEAD_BYTES];

			pad_head_make(head);
			pad_put(PAD_HEAD, head, sizeof(head));
			platform_log("pad: its head: difficulty %d, init '%.62s'", head[1], (const char *)head + 2);
		}
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
		if (pad_in)
		{
			pad_window(1 + PAD_HEAD_BYTES);
			if (pad_in_fill > 0 && pad_in[0] == PAD_HEAD)
			{
				unsigned char head[PAD_HEAD_BYTES];
				int now = game_difficulty_level_get();

				if (pad_take(PAD_HEAD, head, sizeof(head)))
				{
					pad_seed_follow(head[0]);
					head[PAD_HEAD_BYTES - 1] = 0;
					platform_log("pad: recorded at difficulty %d, init '%s'; playing at %d%s", head[1], (const char *)head + 2,
						now, head[1] == now ? "" : ": NOT the recording's difficulty, the replay will part");
				}
			}
			else
			{
				pad_seed_follow(0);
				platform_log("pad: the recording has no head (made before 2026-11-13): its difficulty isn't known");
			}
		}
	}
}

/* a replay seeds the actors' thinking as its recording did (the head's
version byte; 0 for no head), decided before the map's first tick */
static void pad_seed_follow(unsigned int version)
{
	int old = version < 2 && !psp_keep("redoseednew");

	psp_redoseed_on = old ? 0 : !psp_keep("redoseedold");
	platform_log("pad: recorded with %s seeds: the replay thinks with %s%s", version < 2 ? "the game's" : "each actor's own",
		psp_redoseed_on ? "each actor's own" : "the game's", old ? "" : version < 2 ? " (redoseednew)" : "");
}

/* the head's bytes: init.txt read again, its map_name line left out */
static void pad_head_make(unsigned char *head)
{
	char text[256];
	SceUID file = sceIoOpen(PSP_DATA_ROOT "/init.txt", PSP_O_RDONLY, 0);
	int length = file >= 0 ? sceIoRead(file, text, sizeof(text) - 1) : 0;
	unsigned int at = 2;
	char *line, *save = NULL;

	if (file >= 0)
		sceIoClose(file);
	text[length > 0 ? length : 0] = 0;
	memset(head, 0, PAD_HEAD_BYTES);
	head[0] = psp_keep("redoseedold") ? 1 : 2;
	head[1] = (unsigned char)game_difficulty_level_get();
	for (line = strtok_r(text, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save))
	{
		unsigned int bytes = strlen(line);

		if (!bytes || !strncmp(line, "map_name", 8))
			continue;
		if (at > 2 && at < PAD_HEAD_BYTES - 1)
			head[at++] = ';';
		if (at + bytes > PAD_HEAD_BYTES - 1)
			bytes = PAD_HEAD_BYTES - 1 - at;
		memcpy(head + at, line, bytes);
		at += bytes;
	}
}

static void pad_put(unsigned int tag, const void *data, unsigned int bytes)
{
	if (pad_file < 0)
		return;
	if (pad_halves > 0)
	{
		if (pad_out_used + 1 + bytes > PAD_CHUNK / 2)
			pad_half_hand();
	}
	else if (pad_out_used + 1 + bytes > PAD_CHUNK)
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
		keeps all but its last few seconds (with lw2, a half at a time) */
		if ((++pad_frames & 63) == 0 && pad_halves <= 0)
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
	{
		memcpy(blob, record + 1, PAD_BLOB_BYTES);
#ifdef PSP_PADPATH
		if (local_player_index == 0)
			path_steer(blob);
#endif
	}
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
so two runs' populations compare window by window.

"padhold" (with padplay; it watches too): the replay is held to the path on
foot. The look he asked for is set when it is more than a quarter degree
off: it is control state, so nothing physical is touched. A player off the
path by more than "holddeadN" hundredths of a unit (5) is steered: the
replayed stick is bent toward the path, so he gets there by the biped's own
movement, through collision as a player would. Past "holdsnapN" hundredths
(50) flat or in height, or 45 ticks over half of it without closing, he is
set on the path with its velocity. Past "holdfarN" units (8) he is left
for 150 ticks first: that far is a script's move (a teleport behind a gate
the fight opens), which the game makes by itself a little later or sooner
than the recording did (d20's seeds: 60 units off for 72 ticks, then back
within one; the lab's padpath2). If no script brings him, he is walked
along the path from where he left it, PATH_SWEEP_STEP of its ticks a tick,
never set straight across: the level's scripts test where he is, not what
he crossed, and a jump over a trigger volume left posing B's build 9,000
ticks in a level whose BSP had never switched (the lab's padpath3). And
while the level's BSP isn't the path's, nothing is done at all: the
recording's script has moved him to a part of the level this run hasn't
loaded, and only this run's own script can take him there (padpath4: set
there by the hold, two variants never switched). Cinematics are left
alone.

Seats are held as state, since the vehicle's place is the world's and not
his: where the recording sits and the replay doesn't, PATH_SEAT_TICKS on,
he is put in that seat of the nearest vehicle that has it free, the
vehicle first set where the recording's stood when it isn't within 3
units (from anywhere in the level: within 30 only, posing B's build lost
c40's Banshee and walked the rest; the lab's padpath7); where the replay sits and the recording doesn't, he is let out.
Seated on both sides, the look is set as on foot (which is the steering,
for a driver) and what he rides is set on the path, with its axes and
velocity, past "holdseatsnapN" hundredths of a unit (200). "holdnoseat":
seated ticks left alone, as before.

The hold also keeps the deaths the player caused (docs/pad_path.md, "The
outcome hold"). His shots land only in his own game: on any other, the
replayed fire goes where the enemies were, they live about twice as long,
and the replay is a heavier fight than he played. pathrec writes each death
he is responsible for ('K': its tick, encounter, squad and place). Holding,
where the recording is a kill ahead in a squad PATH_KILL_LAG ticks after it
happened, the living member of that squad nearest that place is killed the
way the game's scripts kill; a squad with no one living is waited for
PATH_KILL_KEEP ticks, then let go. "holdnokill": the route alone, as
before. "holdkickN" (tests): he is pushed 0.3 units at game tick N and 3
units 300 ticks later, to show a steer and a snap. Build switch
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
void psp_path_snap(const float *position, const float *velocity);
void psp_path_view(float yaw, float pitch);
int psp_path_seat(int seat, const float *position, const float *forward, const float *up);
int psp_path_unseat(void);
void psp_path_root_set(const float *position, const float *forward, const float *up, const float *velocity);
int psp_path_kill_one(int encounter, int squad, const float *near); /* ai.c (psp_fixups.py) */

#define PATH_POINT 0x50u /* 'P': a tick's record */
#define PATH_HEAD 0x48u  /* 'H': the file's first record */
#define PATH_POINT_BYTES 32
#define PATH_KILL 0x4Bu  /* 'K': a death the player caused */
#define PATH_HEAD_BYTES 8
#define PATH_KILL_BYTES 20
#define PATH_VERSION 2 /* 1 had no 'K' records; it is still read */
#define PATH_KILL_LAG 45
#define PATH_KILL_KEEP 900
#define PATH_SQUADS 256
#define PATH_PENDING 64
#define PATH_OUT (16 * 1024)
#define PATH_REPORT 300 /* ticks a "watch:" and a "census:" line cover */
#define PATH_FLAG_CONTROL 1
#define PATH_FLAG_SEATED 2
#define PATH_FLAG_ZOOMED 4

#define PATH_STALL_TICKS 45
#define PATH_FAR_TICKS 150
#define PATH_SEAT_TICKS 15
#define PATH_RING 1024 /* the path's last ticks kept, to walk him back along */
#define PATH_SWEEP_STEP 4
#define PATH_VIEW_DEGREES 0.25f
#define PATH_HOLD_LOGGED 400

static int path_mode = -1; /* bit 0 recording, bit 1 watching, bit 2 holding */
/* holding: the bounds in units, the path's last record for its velocity,
and what the next frame's stick is bent by (the path less the player, flat,
and the look's yaw the stick is relative to) */
static float path_dead = 0.05f, path_snap = 0.5f, path_far = 8.f;
static int path_far_ticks = 0;
static int path_seats_held = 0, path_seat_ticks = 0;
static long path_seat_said = -1000000;
static float path_seat_snap = 2.f;
static float (*path_ring)[3] = NULL;
static unsigned long path_ring_count = 0; /* records put in the ring, ever */
static long path_sweep_at = -1;            /* the ring's record he is at while walked along, or -1 */
static long path_kick = 0;
static float path_before[3];
static long path_before_time = -1;
static int path_steer_on = 0, path_stall = 0;
static float path_steer_x = 0.f, path_steer_y = 0.f, path_steer_yaw = 0.f;
static unsigned long path_hold_logged = 0, path_kill_logged = 0;
/* the deaths: the game tick last seen, each squad's kills in the recording
so far and on this run (his own and the forced), and the recording's kills
not yet settled */
struct path_squad
{
	long key;
	unsigned short recorded, made;
};
static long path_clock = -1;
static int path_kills_held = 0;
static struct path_squad path_squads[PATH_SQUADS];
static int path_squad_count = 0;
static struct
{
	long time, key;
	unsigned short nth;
	float position[3];
} path_pending[PATH_PENDING];
static int path_pending_count = 0;
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
	/* the hold's */
	unsigned long held, held_over_snap, steered, snaps, view_sets, swept;
	unsigned long kills_own, kills_forced, kills_matched, kills_missing, bsp_waits;
	unsigned long seats_forced, seats_fetched, seats_lost, seats_left, root_sets;
	float error_sum, error_max, view_max;
} path_sum = { -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.f, 0.f, 0.f };

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
		path_mode = (psp_keep("pathrec") ? 1 : 0) | (psp_keep("padwatch") ? 2 : 0) | (psp_keep("padhold") ? 6 : 0);
		path_dead = (float)psp_keep_number("holddead", 5) * 0.01f;
		path_snap = (float)psp_keep_number("holdsnap", 50) * 0.01f;
		path_far = (float)psp_keep_number("holdfar", 8);
		if ((path_mode & 4) && !(path_ring = malloc(PATH_RING * sizeof(*path_ring))))
			path_mode &= ~4;
		path_kills_held = (path_mode & 4) && !psp_keep("holdnokill");
		path_seats_held = (path_mode & 4) && !psp_keep("holdnoseat");
		path_seat_snap = (float)psp_keep_number("holdseatsnap", 200) * 0.01f;
		path_kick = psp_keep_number("holdkick", 0);
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
		unsigned char head[PATH_HEAD_BYTES] = { 'P', 'T', 'H', PATH_VERSION, PATH_POINT_BYTES, PATH_KILL_BYTES, 0, 0 };

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
				(head[4] != 1 && head[4] != PATH_VERSION) || head[5] != PATH_POINT_BYTES;
			path_in_at = path_in_base = sizeof(head);
			path_in_fill = 0;
		}
		else if (path_in_file >= 0)
		{
			sceIoClose(path_in_file);
			path_in_file = -1;
		}
		path_last_record = path_last_tick = path_before_time = path_sweep_at = -1;
		path_ring_count = 0;
		path_squad_count = path_pending_count = 0;
		path_skip_to_restart = 0;
		platform_log("path: %s %s, %u bytes%s", (path_mode & 4) ? "holding the replay to" : "watching", path, path_in_size,
			!path_in ? " (none: nothing to watch)" : path_in_ended ? " (not a path this build reads)" : "");
		if (path_mode & 4)
			platform_log("path: steered past %.2f units, set on the path past %.2f, left 150 ticks first past %.0f",
				(double)path_dead, (double)path_snap, (double)path_far);
	}
	else if (path_mode & 2)
	{
		if (!path_in_ended)
			platform_log("path: the watch ended at a map load, %u of %u bytes read", path_in_at, path_in_size);
		path_in_ended = 1;
	}
}

static struct path_squad *path_squad(long key)
{
	int k;

	for (k = 0; k < path_squad_count; k++)
		if (path_squads[k].key == key)
			return &path_squads[k];
	if (path_squad_count == PATH_SQUADS)
		return &path_squads[PATH_SQUADS - 1]; /* past the table: counted with its last squad */
	path_squads[path_squad_count].key = key;
	path_squads[path_squad_count].recorded = path_squads[path_squad_count].made = 0;
	return &path_squads[path_squad_count++];
}

/* ai.c's death hook: a death the local player is responsible for. Written
to the path being made; counted for its squad on a held run */
void psp_path_kill_event(int encounter, int squad, const float *position)
{
	long key = ((long)(encounter & 0xFFFF) << 16) | (long)(squad & 0xFFFF);

	if (path_mode <= 0)
		return;
	if (path_mode & 1)
	{
		unsigned char record[PATH_KILL_BYTES];
		unsigned long time = (unsigned long)(path_clock + 1); /* the tick now running */

		record[0] = (unsigned char)time;
		record[1] = (unsigned char)(time >> 8);
		record[2] = (unsigned char)(time >> 16);
		record[3] = (unsigned char)(time >> 24);
		record[4] = (unsigned char)encounter;
		record[5] = (unsigned char)(encounter >> 8);
		record[6] = (unsigned char)squad;
		record[7] = (unsigned char)(squad >> 8);
		memcpy(record + 8, position, 12);
		path_put(PATH_KILL, record, sizeof(record));
	}
	if (path_kills_held)
	{
		path_squad(key)->made++;
		path_sum.kills_own++;
	}
}

/* the window holds bytes more of the path from where it is read, read on
when it doesn't: 0 at the file's end */
static int path_fill(unsigned int bytes)
{
	unsigned int keep, got;
	int read;

	if (path_in_at + bytes > path_in_base + path_in_fill && path_in_file >= 0)
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
	return path_in_at + bytes <= path_in_base + path_in_fill;
}

/* the path's next tick without taking it: 1 with it unpacked, 0 at the end.
The kills written before it are taken on the way: each waits its lag */
static int path_peek(struct psp_path_state *state)
{
	if (path_in_ended || !path_in)
		return 0;
	while (path_fill(1 + PATH_KILL_BYTES) && path_in[path_in_at - path_in_base] == PATH_KILL)
	{
		const unsigned char *record = path_in + (path_in_at - path_in_base) + 1;

		if (path_kills_held)
		{
			long key = ((long)(record[4] | record[5] << 8) << 16) | (long)(record[6] | record[7] << 8);

			if (path_pending_count == PATH_PENDING)
			{
				memmove(path_pending, path_pending + 1, (PATH_PENDING - 1) * sizeof(path_pending[0]));
				path_pending_count--;
				path_sum.kills_missing++;
			}
			path_pending[path_pending_count].time = (long)((unsigned long)record[0] | (unsigned long)record[1] << 8 |
				(unsigned long)record[2] << 16 | (unsigned long)record[3] << 24);
			path_pending[path_pending_count].key = key;
			path_pending[path_pending_count].nth = ++path_squad(key)->recorded;
			memcpy(path_pending[path_pending_count].position, record + 8, 12);
			path_pending_count++;
		}
		path_in_at += 1 + PATH_KILL_BYTES;
	}
	if (!path_fill(1 + PATH_POINT_BYTES) || path_in[path_in_at - path_in_base] != PATH_POINT)
	{
		int more = path_in_at < path_in_base + path_in_fill;

		platform_log("path: the watch %s at byte %u of %u", !more ? "ended" :
			path_in[path_in_at - path_in_base] == PATH_HEAD ? "ended at the path's next map load" : "lost the file's records",
			path_in_at, path_in_size);
		path_in_ended = 1;
		return 0;
	}
	path_unpack(path_in + (path_in_at - path_in_base) + 1, state);
	return 1;
}

/* each tick of a held run: the recording's kills whose lag has run. One the
run has made itself is matched; one it hasn't is forced on the squad's
living member nearest where the recording's died; a squad with none living
is waited for, then let go */
static void path_kills(long now)
{
	int k = 0;

	while (k < path_pending_count)
	{
		long key = path_pending[k].key;
		int encounter = (int)((key >> 16) & 0xFFFF), squad = (int)(short)(key & 0xFFFF), done = 0;

		if (now >= path_pending[k].time + PATH_KILL_LAG)
		{
			if (path_squad(key)->made >= path_pending[k].nth)
			{
				path_sum.kills_matched++;
				done = 1;
			}
			else if (psp_path_kill_one(encounter, squad, path_pending[k].position))
			{
				path_squad(key)->made++;
				path_sum.kills_forced++;
				done = 1;
				if (path_kill_logged++ < 2 * PATH_HOLD_LOGGED)
					platform_log("hold: a kill of tick %ld forced at tick %ld (encounter %d squad %d)", path_pending[k].time, now,
						encounter, squad);
			}
			else if (now >= path_pending[k].time + PATH_KILL_KEEP)
			{
				path_sum.kills_missing++;
				done = 1;
				if (path_kill_logged++ < 2 * PATH_HOLD_LOGGED)
					platform_log("hold: a kill of tick %ld let go at tick %ld: no one of encounter %d squad %d living",
						path_pending[k].time, now, encounter, squad);
			}
		}
		if (done)
		{
			memmove(path_pending + k, path_pending + k + 1, (path_pending_count - k - 1) * sizeof(path_pending[0]));
			path_pending_count--;
		}
		else
			k++;
	}
}

static float path_abs(float value)
{
	return value < 0.f ? -value : value;
}

/* psp_pad_blob, each frame of a replay: the recording's stick bent toward
the path by what the last tick found. The stick is in the look's frame:
forward along the yaw, left a quarter turn from it (bipeds.c's movement).
Half a unit off asks for the whole stick, and the sum is cut to the stick's
circle as the game cuts the pad's */
static void path_steer(void *blob)
{
	float throttle[2], cosine, sine, length;

	if (!path_steer_on || pad_in_ended)
		return;
	memcpy(throttle, blob, sizeof(throttle));
	cosine = cosf(path_steer_yaw);
	sine = sinf(path_steer_yaw);
	throttle[0] += 2.f * (path_steer_x * cosine + path_steer_y * sine);
	throttle[1] += 2.f * (path_steer_y * cosine - path_steer_x * sine);
	length = throttle[0] * throttle[0] + throttle[1] * throttle[1];
	if (length > 1.f)
	{
		length = 1.f / sqrtf(length);
		throttle[0] *= length;
		throttle[1] *= length;
	}
	memcpy(blob, throttle, sizeof(throttle));
}

/* a tick of the hold, on foot with the player in control on both sides */
static void path_hold(const struct psp_path_state *now, const struct psp_path_state *then, float flat, float height,
	float view)
{
	float (*slot)[3] = &path_ring[path_ring_count % PATH_RING];

	path_steer_on = 0;
	if ((!path_seats_held && (now->seat != -1 || then->seat != -1)) || !(now->flags & then->flags & PATH_FLAG_CONTROL) ||
		pad_in_ended)
	{
		path_seat_ticks = 0;
		path_stall = path_far_ticks = 0;
		path_sweep_at = -1;
		path_ring_count = 0; /* the path across a seat or a cinematic isn't one to walk along */
		return;
	}
	if (now->bsp != then->bsp)
	{
		static long said = -1000000;

		/* the path is in a part of the level this run hasn't switched to:
		wait for this run's own script */
		path_stall = path_far_ticks = 0;
		path_sweep_at = -1;
		path_ring_count = 0;
		path_sum.bsp_waits++;
		if (now->game_time - said >= 900 || now->game_time < said)
		{
			said = now->game_time;
			platform_log("hold: waiting at tick %ld: the path is in BSP %d and the level is in %d", now->game_time, (int)then->bsp,
				(int)now->bsp);
		}
		return;
	}
	if (now->seat != then->seat)
	{
		/* a seat on one side only: forced after its grace, and nothing else
		held meanwhile (his place and the path's aren't the same thing's) */
		path_stall = path_far_ticks = 0;
		path_sweep_at = -1;
		path_ring_count = 0;
		if (++path_seat_ticks > PATH_SEAT_TICKS)
		{
			int did = then->seat == -1 ? (psp_path_unseat() ? 3 : -1) :
				now->seat != -1 ? (psp_path_unseat() ? 3 : -1) : /* the wrong seat: out first */
				psp_path_seat(then->seat, then->position, then->forward, then->up);

			path_seat_ticks = 0;
			path_sum.seats_forced += did == 1;
			path_sum.seats_fetched += did == 2;
			path_sum.seats_left += did == 3;
			path_sum.seats_lost += did == 0;
			/* a seat not found is said once in 900 ticks, not at every try */
			if (did > 0 || (did == 0 && (now->game_time - path_seat_said >= 900 || now->game_time < path_seat_said)))
			{
				if (did == 0)
					path_seat_said = now->game_time;
				platform_log("hold: tick %ld, the path %s (seat %d): %s", now->game_time, then->seat == -1 ? "is on foot" : "is seated",
					(int)then->seat, did == 1 ? "seated in the vehicle there" : did == 2 ? "a vehicle fetched to the path, and seated" :
					did == 3 ? "let out of his seat" : "no vehicle in the level with that seat free");
			}
		}
		return;
	}
	path_seat_ticks = 0;
	if (then->seat != -1)
	{
		/* seated on both sides: the look, and what he rides past its bound */
		path_sum.held++;
		path_sum.held_over_snap += flat > path_seat_snap || height > path_seat_snap;
		path_ring_count = 0;
		if (view > PATH_VIEW_DEGREES)
		{
			psp_path_view(then->yaw, then->pitch);
			path_sum.view_sets++;
		}
		if (flat > path_seat_snap || height > path_seat_snap)
		{
			float velocity[3] = { 0.f, 0.f, 0.f };

			if (path_before_time == then->game_time - 1)
			{
				velocity[0] = then->position[0] - path_before[0];
				velocity[1] = then->position[1] - path_before[1];
				velocity[2] = then->position[2] - path_before[2];
			}
			psp_path_root_set(then->position, then->forward, then->up, velocity);
			path_sum.root_sets++;
			if (path_hold_logged++ < PATH_HOLD_LOGGED)
				platform_log("hold: what he rides set on the path at tick %ld: %.2f off flat, %.2f in height", now->game_time,
					(double)flat, (double)height);
		}
		return;
	}
	memcpy(*slot, then->position, sizeof(*slot));
	path_ring_count++;
	path_sum.held++;
	path_sum.held_over_snap += flat > path_snap || height > path_snap;
	if (path_sweep_at >= 0)
	{
		/* walked along: the next records of the ring, until he is at the newest */
		path_sweep_at += PATH_SWEEP_STEP;
		if ((unsigned long)path_sweep_at >= path_ring_count - 1)
		{
			path_sweep_at = -1;
			platform_log("hold: walked along the path up to tick %ld", now->game_time);
		}
		else
		{
			psp_path_snap(path_ring[path_sweep_at % PATH_RING], NULL);
			path_sum.swept++;
			return;
		}
	}
	if (view > PATH_VIEW_DEGREES)
	{
		psp_path_view(then->yaw, then->pitch);
		path_sum.view_sets++;
	}
	if (flat > path_far || height > path_far)
	{
		unsigned long kept = path_ring_count < PATH_RING ? path_ring_count : PATH_RING, k, nearest = 0;
		float best = 1e30f;

		if (++path_far_ticks <= PATH_FAR_TICKS)
		{
			path_stall = 0;
			return;
		}
		/* where he left the path: the kept record nearest to him. From there
		he is walked along it; when none is near, he never was on this
		stretch, and he is set on the path as below */
		for (k = 0; k < kept; k++)
		{
			const float *at = path_ring[(path_ring_count - 1 - k) % PATH_RING];
			float dx = path_abs(at[0] - now->position[0]), dy = path_abs(at[1] - now->position[1]);
			float dz = path_abs(at[2] - now->position[2]);
			float d = dx > dy ? dx + 0.41f * dy : dy + 0.41f * dx;

			if (dz > d)
				d = dz;
			if (d < best)
			{
				best = d;
				nearest = k;
			}
		}
		if (best <= path_far && nearest > PATH_SWEEP_STEP)
		{
			path_sweep_at = (long)(path_ring_count - 1 - nearest);
			path_far_ticks = path_stall = 0;
			platform_log("hold: %.1f off at tick %ld and no script brought him: walked along the path's last %lu ticks",
				(double)(flat > height ? flat : height), now->game_time, nearest);
			return;
		}
	}
	else
		path_far_ticks = 0;
	if (flat > path_snap || height > path_snap || path_stall > PATH_STALL_TICKS)
	{
		float velocity[3] = { 0.f, 0.f, 0.f };

		/* the path's own step into this tick, when its last record is the tick before */
		if (path_before_time == then->game_time - 1)
		{
			velocity[0] = then->position[0] - path_before[0];
			velocity[1] = then->position[1] - path_before[1];
			velocity[2] = then->position[2] - path_before[2];
		}
		psp_path_snap(then->position, velocity);
		path_sum.snaps++;
		/* the small ones are capped; one past 2 units is always said */
		if (flat > 2.f || height > 2.f || path_hold_logged++ < PATH_HOLD_LOGGED)
			platform_log("hold: set on the path at tick %ld: %.2f off flat, %.2f in height%s", now->game_time, (double)flat,
				(double)height, path_stall > PATH_STALL_TICKS ? " (steered 45 ticks without closing)" :
				path_far_ticks ? " (far for 150 ticks, and never near the path's last ticks)" : "");
		path_stall = path_far_ticks = 0;
		return;
	}
	if (flat > path_dead)
	{
		path_steer_on = 1;
		path_steer_x = then->position[0] - now->position[0];
		path_steer_y = then->position[1] - now->position[1];
		path_steer_yaw = now->yaw;
		path_sum.steered++;
	}
	path_stall = flat > 0.5f * path_snap ? path_stall + 1 : 0;
}

/* the player against the path's record of this tick. Game time going back
(a revert, a restart) is followed on both sides: the path's records are
skipped to where its own time went back */
static void path_watch(const struct psp_path_state *now)
{
	struct psp_path_state then;
	float dx, dy, dz, flat, height, error, yaw, pitch, view;

	if (path_last_tick >= 0 && now->game_time < path_last_tick)
	{
		path_skip_to_restart = 1;
		path_squad_count = path_pending_count = 0; /* a revert or restart: its kills are counted again */
	}
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
		flat = path_abs(dx) > path_abs(dy) ? path_abs(dx) + 0.41f * path_abs(dy) : path_abs(dy) + 0.41f * path_abs(dx);
		height = path_abs(dz);
		error = height > flat ? height : flat;
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
		if (path_mode & 4)
			path_hold(now, &then, flat, height, view);
		memcpy(path_before, then.position, sizeof(path_before));
		path_before_time = then.game_time;
		return;
	}
	path_steer_on = 0;
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
	if (path_mode & 4)
		platform_log("hold: ticks %ld-%ld | steered %lu ticks | set on the path %lu times | view set %lu times"
			" | held %lu ticks, %lu of them over the snap bound | walked along %lu ticks"
			" | kills: his own %lu, the recording's matched unforced %lu, forced %lu, let go %lu"
			" | waited for the level's BSP %lu ticks"
			" | seats: taken %lu, vehicle fetched %lu, none found %lu, let out %lu, ride set on the path %lu", from,
			from + PATH_REPORT - 1, path_sum.steered, path_sum.snaps, path_sum.view_sets, path_sum.held,
			path_sum.held_over_snap, path_sum.swept, path_sum.kills_own, path_sum.kills_matched, path_sum.kills_forced,
			path_sum.kills_missing, path_sum.bsp_waits, path_sum.seats_forced, path_sum.seats_fetched, path_sum.seats_lost,
			path_sum.seats_left, path_sum.root_sets);
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
	path_clock = now.game_time;
	if (path_kills_held && !pad_in_ended)
		path_kills(now.game_time);
	if ((path_mode & 4) && path_kick > 0 && (now.game_time == path_kick || now.game_time == path_kick + 300) && now.seat == -1)
	{
		float to[3];

		memcpy(to, now.position, sizeof(to));
		to[0] += now.game_time == path_kick ? 0.3f : 3.f;
		psp_path_snap(to, NULL);
		platform_log("hold: holdkick pushed the player %.1f units at tick %ld", now.game_time == path_kick ? 0.3 : 3.0,
			now.game_time);
	}
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
