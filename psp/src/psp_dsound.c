/*
PSP_DSOUND.C

Xbox DirectSound for the PSP (build 25): the Linux port's software mixer
(dsound_sdl.c), reworked for a 333 MHz CPU, out through the PSP's own audio
hardware.

The game plays everything through DirectSound streams of Xbox ADPCM (mono
or stereo, 22 or 44 kHz) or 16-bit PCM, each a queue of packets straight
from its sound cache. The Linux mixer decodes every packet to PCM when it is
submitted (a malloc and a whole packet's work at once) and mixes in floats
at 48 kHz. Here:
	- nothing is decoded ahead: a packet's memory stays the game's until
	  the packet completes, so the mixer decodes it 64 samples (one ADPCM
	  block) at a time as it plays;
	- the mix runs at 22,050 Hz, the rate of nearly all of Halo's sounds,
	  in integers (16-bit samples, 14-bit gains, 32-bit sums), and the
	  PSP's sample-rate converter (sceAudioSRC) takes it to the output;
	- it runs on its own thread above the game's, 512 samples (23 ms) at a
	  time, double buffered.
Gains are worked out once a block per voice, in floats as on Linux
(DirectSound's inverse distance rolloff, an equal power pan, the I3DL2
direct path), then ramped across the block so changes don't click.

Packets the mixer has finished are completed from DirectSoundDoWork, which
the game calls every frame, and from Flush, never from the audio thread.
*/

#include "platform.h"
#include "port_config.h"

#include <pspkernel.h>
#include <pspaudio.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp_sound.h"   /* what the engine tells the mixer: stop reasons, classes */
#include "psp_paths.h"   /* ms0:/halo or ms0:/halomp, and the maps */
#include "psp_me.h"      /* the eDRAM map and the sound copy job (soundedram) */

void platform_log(const char *format, ...);
void psp_vfpu_sincosf(float angle, float *sine, float *cosine); /* psp_vfputrig.c */

#define OUTPUT_RATE 22050
#define OUTPUT_CHANNELS 2
#define MIX_FRAMES 512
#define MAXIMUM_STREAM_PACKETS 64

#define XBOX_ADPCM_BLOCK_BYTES 36
#define XBOX_ADPCM_BLOCK_SAMPLES 64

#define GAIN_ONE 16384 /* 14-bit gains */

/* ---------- voices */

struct voice_packet
{
	const unsigned char *data;
	unsigned long frames;
	BOOL finished;            /* played out by the mixer, not yet completed */
#if PSP_MUSIC
	/* a music stub's packet (psp_music.c): its chain + 1, where it starts in
	the chain at 44.1 kHz, and 44.1 kHz frames per frame of the stream */
	int music;
	unsigned long music_start, music_scale;
#endif
#if PSP_SNDSTREAM
	/* a streamed sound's packet (SNDSTREAMLONG): the sound's id (0: none),
	where the packet starts in its samples, and whether the cache holds the
	samples too (sndstreamcheck: the piece is only compared) */
	unsigned long snd_id, snd_offset;
	int snd_check;
#endif
};

struct psp_stream
{
	/* must be first: in C an IDirectSoundStream is just { lpVtbl } */
	IDirectSoundStream object;
	LPFNXMEDIAOBJECTCALLBACK callback;
	LPVOID context;

	/* format */
	BOOL adpcm;
	unsigned long channels;
	DWORD sample_rate;

	unsigned long packet_head;
	unsigned long packet_count;
	/* packets completed so far (the game's own count of them is in stream_game) */
	unsigned long popped;
	/* position inside the head unfinished packet: whole frames and a 16-bit fraction */
	unsigned long frame;
	unsigned long fraction;
	/* two decoded blocks (even and odd, so interpolating across a block's
	end decodes each once): which packet and block each is, both channels
	interleaved */
	const struct voice_packet *decoded_packet[2];
	long decoded_block[2];
	short decoded[2][XBOX_ADPCM_BLOCK_SAMPLES * 2];
	/* gains the mixer ramps from, to avoid clicks */
	int current_left, current_right;
	BOOL gains_valid;
	/* build 86: worked out by the main CPU before each block (prepare_voice),
	so the mix itself is integers only and can run on the Media Engine */
	unsigned long step;
	int target_left, target_right;
	/* build 87: frames this voice had nothing queued to play while the game
	still had it playing (written by the mixer, on either CPU): a gap, if
	the game then queues more (b86's run 2: regular breaks heard with
	either mixer, the output never late) */
	unsigned long dry_frames;
	/* frames this voice was mixed (written by the mixer, on either CPU):
	the loading chant's line says whether its voices played at all */
	unsigned long mixed_frames;
	/* the loudest gain it was prepared with, and how many blocks were
	prepared: whether the loading chant was ever loud enough to hear */
	int peak_gain;
	unsigned long prepared_blocks;
	/* "silentoverrun": a silent voice advances a whole block even past what
	is queued, as before (set by the main CPU with the gains, read by the
	mixer on either CPU) */
	int silent_overrun;
	/* "adpcmold": the header's sample left out and the padding nibble played,
	as before (set with silent_overrun) */
	int adpcm_old;
	/* "imatableoff" (or a table that failed its check): ADPCM decoded by the
	formula, as before (set with silent_overrun) */
	int ima_fast_old;
	/* "memixold": the heard voice mixed by the per-sample loop, as before
	(set with silent_overrun; mix_voice) */
	int memix_old;
	/* "mixkinds" (H1, on by default; mixkindsoff for the old loop): a mono ADPCM voice
	mixed by a loop for its kind, each run inside one decoded block, so the
	per-frame tests that answer the same for the whole run are asked once
	(set with silent_overrun; mix_heard) */
	int mix_kinds;
	/* the approximate paths (AUDIO_APPROX_*) this voice is mixed with:
	audio_approx in play; "audiocheck" mixes each block with none and with
	these, and measures the difference (set with silent_overrun) */
	unsigned int approx;
	/* muffling: the I3DL2 source's high-frequency loss (obstruction,
	occlusion, the direct path's own) as a one-pole low-pass, so a sound
	behind a wall is duller as well as quieter, as on the Xbox. The main CPU
	sets the coefficient (Q14, GAIN_ONE: no filter); the mixer keeps the
	filter's state, on either CPU. "audioold": no filter */
	LONG lowpass_millibels;
	int lowpass_coefficient;
	int lowpass_left, lowpass_right;
#if PSP_MUSIC
	/* this block's head packet is a music stub's: the voice advances as a
	silent one and reads no samples (its sound is psp_music.c's) */
	int music_block;
#define MUSIC_BLOCK(stream) ((stream)->music_block)
#else
	/* a build without the music path (PSP_MUSIC) has no music voices */
#define MUSIC_BLOCK(stream) 0
#endif
#if PSP_SNDSTREAM
	/* the piece this voice last decoded from (a guess for the next block),
	and its blocks since the audio thread last collected them: decoded from
	pieces, not in RAM in time (played silent), and with sndstreamcheck the
	pieces compared with the cache's copy, equal or not */
	int snd_hint;
	unsigned long snd_blocks, snd_late, snd_checked, snd_mismatched, snd_unaligned;
	/* the first block that differed since the last collection: its sound and
	where it lies in the samples (sndstreamcheck) */
	unsigned long snd_differ_id, snd_differ_at;
#endif

	/* last, so a hand-over by ranges covers the fields above in a few whole
	lines and only the queued packets' entries of this (psp_dsound_me_lines) */
	/* the ring's size less one (a power of two: MAXIMUM_STREAM_PACKETS, or
	fewer where "streamrecN" made the record smaller), and where the game's
	record follows this one (a smaller record ends its entries early) */
	unsigned long packet_mask, game_offset;
	struct voice_packet packets[MAXIMUM_STREAM_PACKETS];
};
/* build 86: a voice takes whole 64-byte cache lines (memalign, the size
rounded up): the Media Engine writes voices back a cache line at a time, and
a line shared with other data would carry stale copies of its bytes */
#define STREAM_BYTES ((sizeof(struct psp_stream) + 63) & ~(size_t)63)
void *memalign(size_t alignment, size_t size);

static pthread_mutex_t mixer_lock = PTHREAD_MUTEX_INITIALIZER;
/* the game links and unlinks with single stores (the links are in stream_game)
and the audio thread walks the list without the lock: see MIXLOCK below */
static struct psp_stream *volatile streams;

/* build 86: the game's side of the mixer's lock, timed when it has to wait:
the audio thread holds it through a block's mix, and with the Media Engine
mixing, the game runs on meanwhile and may come for it */
static unsigned long game_lock_waits = 0, game_lock_wait_us = 0, game_lock_worst_us = 0;

static void game_lock(void)
{
	SceUInt64 start;
	unsigned long waited;

	if (pthread_mutex_trylock(&mixer_lock) == 0)
		return;
	start = sceKernelGetSystemTimeWide();
	pthread_mutex_lock(&mixer_lock);
	waited = (unsigned long)(sceKernelGetSystemTimeWide() - start);
	game_lock_waits++;
	game_lock_wait_us += waited;
	if (waited > game_lock_worst_us)
		game_lock_worst_us = waited;
}

/* the listener, in DirectSound's left-handed +y up space */
static struct
{
	float position[3];
	float front[3];
	float top[3];
	float rolloff_factor;
	float distance_factor;
} listener = { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, 1.0f, 1.0f };

static float master_volume = 1.0f;

/* the mixer's own accounting, for the log */
static unsigned long mix_blocks = 0, mix_microseconds = 0, mix_voice_blocks = 0, mix_peak_voices = 0;
/* build 40 (sound cutting out in the heaviest fighting): which of
three it is. The worst block's mixing time and the longest wait for the
mixer's lock (the game holds it to start and change sounds); blocks that
reached the output late (a gap heard); and blocks that hit the 16-bit limit
(distortion, not a gap) */
/* build 87: voices that ran dry and were then fed more (a gap heard) */
static unsigned long dry_gaps = 0, dry_gap_frames = 0, dry_gap_worst = 0;
/* why the game stopped the sounds counted in cut_sounds (psp_sound.h), so
the heard cut-offs can be told from sounds the game retired as the Xbox did */
int psp_sound_stop_reason = PSP_STOP_OTHER;
long psp_sound_queueing_class = -1;
static unsigned long cut_by_reason[PSP_STOP_REASONS];
/* the first dry gaps of each heartbeat window, kept to be logged there (the
gap is seen with the mixer's lock held, where a log write could hold up the
mixer): the channel, the sound's class, the silence and the frame before it,
the long frames being the suspected cause */
#define GAP_RECORDS 6
static struct { long context, sound_class; unsigned long dry_ms, frame_ms; } gap_records[GAP_RECORDS];
static unsigned long gap_record_count = 0;
/* packets that didn't continue their permutation where the last one ended
(sound skipped without a gap), and unplayed packets a flush threw away */
static struct { long context, sound_class, bytes; unsigned long queued; } jump_records[GAP_RECORDS];
static unsigned long jump_record_count = 0, flushed_unplayed = 0;
/* permutations queued to a channel with nothing left to send (psp_stream_link) */
static unsigned long stream_links = 0;
static int stream_link_off = -1;
extern unsigned long psp_last_frame_us; /* psp_platform.c, psp_frame_account */
static unsigned long cut_sounds = 0, mix_worst_us = 0, lock_worst_us = 0, late_blocks = 0, clipped_blocks = 0, gap_worst_us = 0;

/* MIXLOCK. The record above (struct psp_stream) is the mixer's: the Media
Engine reads and writes it a cache line at a time while a block mixes, so
only the audio thread touches it, and only between jobs. The game's side of a
voice never waits for the mixer: setters write this record, on cache lines of
its own beside the mixer's, and the queue's changes (a packet queued,
completed, the stream flushed) go through a ring the audio thread empties
before it prepares each block, so each reaches the mix at the block it reached
when the game held the mixer's lock for it. "mixlockold": the lock held
through the job and every call under it, as before (the ring's commands
applied at once, under it) */
/* a queued packet as the game gave it, and (PSP_SNDSTREAM) its streamed sound
and where it starts in it, as the mixer's entries have them: the reader's
view of what is to play */
struct game_packet
{
	XMEDIAPACKET packet;
#if PSP_SNDSTREAM
	unsigned long snd_id, snd_offset;
#endif
};

struct stream_game
{
	/* the mixer's list, walked by the audio thread: single stores, no lock */
	struct psp_stream *volatile next;
	ULONG reference_count;
	DWORD sample_rate; /* a copy of the mixer's, for SetFrequency(0) */
	/* what the game sets and prepare_voice reads, on the main CPU only: written
	whole with interrupts off (set_enter), as the audio thread preempts the game */
	BOOL paused;
	DWORD frequency;
	float volume;             /* SetVolume */
	float mix_left, mix_right;
	BOOL has_3d;
	DWORD mode;
	float position[3];
	float minimum_distance, maximum_distance;
	float i3dl2_gain;
	LONG hf_millibels;
	float velocity[3];
	float cone_forward[3];
	DWORD cone_inside, cone_outside;
	LONG cone_outside_millibels;
	/* the game's view of the queue, as the mixer's record will hold it once the
	ring is empty: the packets as queued (the mixer keeps their data and frames),
	and how many were completed since the stream began */
	unsigned long head, count, popped;
	/* set by the audio thread after each block that ended: the packets before
	this number (counted from the stream's start) are played out */
	volatile unsigned long finished_to;
	const unsigned char *queued_end;
	const void *queued_context;
	long sound_class;
	/* once released: kept until a block that began after has ended */
	struct psp_stream *zombie_next;
	unsigned long zombie_stamp;
	/* last, so a smaller record ("streamrecN") ends its entries early */
	struct game_packet entry[MAXIMUM_STREAM_PACKETS];
};
#define GAME_BYTES ((sizeof(struct stream_game) + 63) & ~(size_t)63)
/* a record holding that many entries, each part in whole cache lines (above) */
#define stream_bytes(count) ((offsetof(struct psp_stream, packets) + (count) * sizeof(struct voice_packet) + 63) & ~(size_t)63)
#define game_bytes(count) ((offsetof(struct stream_game, entry) + (count) * sizeof(struct game_packet) + 63) & ~(size_t)63)
#define GAME_OF(stream) ((struct stream_game *)((unsigned char *)(stream) + (stream)->game_offset))

#define MIX_COMMANDS 512 /* a power of two */
/* CMD_MOVE: a sound's bytes moved (sndtierback): every packet in data's
frames bytes pointed at to */
enum { CMD_PUSH, CMD_POP, CMD_RESET, CMD_MOVE };
struct mix_command
{
	struct psp_stream *stream;
	const unsigned char *data, *to;
	unsigned long type, frames;
	long sound_class;
#if PSP_MUSIC
	int music;
	unsigned long music_start, music_scale;
#endif
#if PSP_SNDSTREAM
	unsigned long snd_id, snd_offset;
	int snd_check;
#endif
};
static struct mix_command commands[MIX_COMMANDS];
/* the game puts at the tail, the audio thread takes at the head */
static volatile unsigned long command_head, command_tail;
/* "mixlockold", read once by audio_start before any stream or the audio thread exists */
static int mixlock_old = 0;
/* the audio thread is waiting for the Media Engine's job: the game's calls
meanwhile are the ones the lock held up. Blocks begun and ended, for freeing */
static volatile int job_running = 0;
static volatile unsigned long blocks_begun = 0, blocks_ended = 0;
/* for the log: the game's calls into the mixer, those that came during a job,
commands queued, the ring's fullest, times the game waited for room in it */
static unsigned long game_calls = 0, would_waits = 0, commands_queued = 0, ring_fullest = 0, ring_full_waits = 0;
static struct psp_stream *zombies;

/* one command applied to the mixer's record: by the audio thread before it prepares
a block, or at once by the game under the lock (mixlockold); what the game did to these fields before */
/* the packets that point into a moved sound, pointed at its new place: on
the audio thread, in the commands' order, so a packet queued before the move
is moved too */
static void packets_move(const unsigned char *from, const unsigned char *to, unsigned long bytes)
{
	struct psp_stream *stream;
	unsigned long position;

	for (stream = streams; stream; stream = GAME_OF(stream)->next)
		for (position = 0; position < stream->packet_count && position <= stream->packet_mask; position++)
		{
			struct voice_packet *packet = &stream->packets[(stream->packet_head + position) & stream->packet_mask];

			if (packet->data >= from && (unsigned long)(packet->data - from) < bytes)
			{
				packet->data = to + (packet->data - from);
#if PSP_SNDSTREAM
				/* a streamed sound read again whole (the tier's rescue): its
				packets play the samples at the new place */
				packet->snd_id = 0;
#endif
			}
		}
}

static void mix_apply(const struct mix_command *command)
{
	struct psp_stream *stream = command->stream;

	if (command->type == CMD_MOVE)
	{
		packets_move(command->data, command->to, command->frames);
		return;
	}
	if (command->type == CMD_PUSH)
	{
		struct voice_packet *entry = &stream->packets[(stream->packet_head + stream->packet_count) & stream->packet_mask];

		entry->data = command->data;
		entry->frames = command->frames;
		entry->finished = FALSE;
#if PSP_MUSIC
		entry->music = command->music;
		entry->music_start = command->music_start;
		entry->music_scale = command->music_scale;
#endif
#if PSP_SNDSTREAM
		entry->snd_id = command->snd_id;
		entry->snd_offset = command->snd_offset;
		entry->snd_check = command->snd_check;
#endif
		if (stream->packet_count && stream->dry_frames)
		{
			if (gap_record_count < GAP_RECORDS)
			{
				gap_records[gap_record_count].context = (long)stream->context;
				gap_records[gap_record_count].sound_class = command->sound_class;
				gap_records[gap_record_count].dry_ms = stream->dry_frames * 1000 / OUTPUT_RATE;
				gap_records[gap_record_count].frame_ms = psp_last_frame_us / 1000;
			}
			gap_record_count++;
			dry_gaps++;
			dry_gap_frames += stream->dry_frames;
			if (stream->dry_frames > dry_gap_worst)
				dry_gap_worst = stream->dry_frames;
		}
		stream->dry_frames = 0;
		if (!stream->packet_count)
		{
			/* a stream that ran dry starts over */
			stream->frame = 0;
			stream->fraction = 0;
			stream->gains_valid = FALSE;
		}
		stream->packet_count++;
	}
	else if (command->type == CMD_POP)
	{
		struct voice_packet *entry = &stream->packets[stream->packet_head];

		if (stream->decoded_packet[0] == entry)
			stream->decoded_packet[0] = NULL;
		if (stream->decoded_packet[1] == entry)
			stream->decoded_packet[1] = NULL;
		entry->data = NULL;
		entry->finished = FALSE;
		stream->packet_head = (stream->packet_head + 1) & stream->packet_mask;
		stream->packet_count--;
		stream->popped++;
	}
	else
	{
		stream->frame = 0;
		stream->fraction = 0;
		stream->dry_frames = 0;
		stream->decoded_packet[0] = stream->decoded_packet[1] = NULL;
	}
}

/* the audio thread, before it prepares a block */
static void drain_commands(void)
{
	while (command_head != command_tail)
	{
		mix_apply(&commands[command_head % MIX_COMMANDS]);
		command_head++;
	}
}

/* the game's side. mixlockold: applied at once, the caller holds the lock.
Otherwise queued, the tail moved with interrupts off (the audio thread preempts
the game); the game waits for room only when 512 are out, which the audio
thread empties every block */
#if PSP_MUSIC || PSP_SNDSTREAM
static void mix_queue(const struct mix_command *made);
#endif
/* CMD_MOVE's destination, set by its one caller (tier_back_moved) */
static const unsigned char *move_to = NULL;
static void mix_command(struct psp_stream *stream, unsigned long type, const unsigned char *data, unsigned long frames,
	long sound_class)
{
	struct mix_command command;

	command.stream = stream;
	command.type = type;
	command.data = data;
	command.to = type == CMD_MOVE ? move_to : NULL;
	command.frames = frames;
	command.sound_class = sound_class;
#if PSP_MUSIC || PSP_SNDSTREAM
#if PSP_MUSIC
	command.music = 0;
	command.music_start = command.music_scale = 0;
#endif
#if PSP_SNDSTREAM
	command.snd_id = command.snd_offset = 0;
	command.snd_check = 0;
#endif
	mix_queue(&command);
}

/* a command made whole by the caller (a music stub's or a streamed sound's packet) */
static void mix_queue(const struct mix_command *made)
{
	struct mix_command command = *made;

#endif
	if (mixlock_old)
	{
		mix_apply(&command);
		return;
	}
	for (;;)
	{
		unsigned int flags = sceKernelCpuSuspendIntr();
		unsigned long used = command_tail - command_head;

		if (used < MIX_COMMANDS)
		{
			commands[command_tail % MIX_COMMANDS] = command;
			__asm__ __volatile__("" : : : "memory");
			command_tail++;
			commands_queued++;
			if (used + 1 > ring_fullest)
				ring_fullest = used + 1;
			sceKernelCpuResumeIntr(flags);
			return;
		}
		sceKernelCpuResumeIntr(flags);
		ring_full_waits++;
		sceKernelDelayThread(1000);
	}
}

/* the audio thread, after a block: how many packets from the stream's start are played out */
static void publish_finished(struct psp_stream *stream)
{
	unsigned long k = 0;

	while (k < stream->packet_count && stream->packets[(stream->packet_head + k) & stream->packet_mask].finished)
		k++;
	GAME_OF(stream)->finished_to = stream->popped + k;
}

/* whether the packet `position` places from the head is played out: the mixer's
flag under the lock (mixlockold); otherwise what the last block that ended
published, so a call that comes while a block mixes sees the block before it
(DirectSoundDoWork completes it a frame later) */
static int packet_finished(struct psp_stream *stream, unsigned long position)
{
	struct stream_game *game = GAME_OF(stream);

	if (mixlock_old)
		return stream->packets[(stream->packet_head + position) & stream->packet_mask].finished;
	return (long)(game->finished_to - (game->popped + position)) > 0;
}

/* the game's entry into a setter: the lock (mixlockold), or interrupts off for
its few stores. Counts the calls that came while the Media Engine mixed: each
would have waited for the lock until the job ended */
static unsigned int set_enter(void)
{
	if (mixlock_old)
	{
		game_lock();
		return 0;
	}
	game_calls++;
	would_waits += job_running;
	return sceKernelCpuSuspendIntr();
}

static void set_leave(unsigned int flags)
{
	if (mixlock_old)
		pthread_mutex_unlock(&mixer_lock);
	else
		sceKernelCpuResumeIntr(flags);
}

/* the game's entry into the queue's calls: the lock (mixlockold), or nothing:
its side is its own record and the ring. Counted as set_enter counts */
static void queue_enter(void)
{
	if (mixlock_old)
	{
		game_lock();
		return;
	}
	game_calls++;
	would_waits += job_running;
}

static void queue_leave(void)
{
	if (mixlock_old)
		pthread_mutex_unlock(&mixer_lock);
}

/* a released stream's record is the Media Engine's while a block that took it
into its list runs, and the ring may still hold its commands: freed once a
block that began after the unlink has ended (zombie_stamp, set by stream_unlink) */
static void reap_streams(void)
{
	struct psp_stream **link = &zombies;

	while (*link)
	{
		struct psp_stream *stream = *link;
		struct stream_game *game = GAME_OF(stream);

		if ((long)(blocks_ended - game->zombie_stamp) >= 0)
		{
			*link = game->zombie_next;
			free(stream);
		}
		else
			link = &game->zombie_next;
	}
}

static void retire_stream(struct psp_stream *stream)
{
	if (mixlock_old)
	{
		free(stream);
		return;
	}
	GAME_OF(stream)->zombie_next = zombies;
	zombies = stream;
	reap_streams();
}

static float gain_from_millibels(LONG millibels)
{
	if (millibels <= DSBVOLUME_MIN)
		return 0.0f;
	return powf(10.0f, (float)millibels / 2000.0f);
}

/* ---------- decoding */

static const signed char ima_index_table[16] =
{
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8,
};

static const short ima_step_table[89] =
{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
	253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
	1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
	11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767,
};

static inline int ima_magnitude(int step, int nibble)
{
	int difference = step >> 3;

	if (nibble & 1) difference += step >> 2;
	if (nibble & 2) difference += step >> 1;
	if (nibble & 4) difference += step;
	return difference;
}

static inline int ima_next_index(int index, int nibble)
{
	index += ima_index_table[nibble];
	if (index < 0) index = 0;
	if (index > 88) index = 88;
	return index;
}

static inline int ima_expand(int nibble, int *predictor, int *index)
{
	int difference = ima_magnitude(ima_step_table[*index], nibble);

	if (nibble & 8) difference = -difference;
	*predictor += difference;
	if (*predictor > 32767) *predictor = 32767;
	if (*predictor < -32768) *predictor = -32768;
	*index = ima_next_index(*index, nibble);
	return *predictor;
}

/* IMATABLE: every answer of ima_expand but the add, for each step index and
the code's three magnitude bits: the difference's magnitude (at most 61,436)
in the low 16 bits, the next index's row (index * 8) above. The index table
doesn't depend on the sign bit, so 89 x 8 entries cover all 16 codes. The
decoder's bit tests branch on the data, which the in-order core can't
foresee; a load and a sign flip don't. Built from ima_magnitude and
ima_next_index themselves, so it can't disagree with them; checked at the
audio's start all the same. "imatableoff" decodes as before */
/* the approximate paths in play: mixes that sound the same but aren't the
same bits, each switched per voice so that "audiocheck" can mix a block both
ways. None yet: each lever adds its bit and its keep word here */
static unsigned int audio_approx = 0;
/* "audiocheckcal": the check's own calibration, a 1 kHz tone at -60 dB of
full scale (peak 33) added to the approximate side, so the check must report
it: in the mid band, peak 33 */
#define AUDIO_APPROX_CAL 0x80000000u

#define IMA_ROWS 89
static unsigned int ima_fast[IMA_ROWS * 8] __attribute__((aligned(64)));
static int ima_fast_old = 1;
/* "upfuseold": the half-band filter and the full-rate add as two passes
(upsample_full); read uncached: set on the main CPU, read on the ME */
static int up_fuse_old = 0;

static inline int ima_expand_fast(int nibble, int *predictor, int *row)
{
	unsigned int entry = ima_fast[*row + (nibble & 7)];
	int sign = -((nibble >> 3) & 1);
	int value = *predictor + (((int)(entry & 0xffff) ^ sign) - sign);

	value = value > 32767 ? 32767 : value;
	value = value < -32768 ? -32768 : value;
	*predictor = value;
	*row = (int)(entry >> 16);
	return value;
}

/* on the main CPU before the mixer runs; the check decodes every code from
every index at the predictors where the clamps act, both ways */
static void ima_fast_build(void)
{
	extern int psp_keep(const char *word);
	static const int predictors[] = { -32768, -32767, -30000, -1, 0, 1, 30000, 32766, 32767 };
	unsigned long index, code, which, differed = 0;

	for (index = 0; index < IMA_ROWS; index++)
		for (code = 0; code < 8; code++)
			ima_fast[index * 8 + code] = (unsigned int)ima_magnitude(ima_step_table[index], (int)code)
				| (unsigned int)(ima_next_index((int)index, (int)code) * 8) << 16;
	for (index = 0; index < IMA_ROWS; index++)
		for (code = 0; code < 16; code++)
			for (which = 0; which < sizeof(predictors) / sizeof(predictors[0]); which++)
			{
				int slow_predictor = predictors[which], slow_index = (int)index;
				int fast_predictor = predictors[which], fast_row = (int)index * 8;
				int slow = ima_expand((int)code, &slow_predictor, &slow_index);
				int fast = ima_expand_fast((int)code, &fast_predictor, &fast_row);

				if (slow != fast || fast_row != slow_index * 8)
					differed++;
			}
	ima_fast_old = psp_keep("imatableoff") != 0 || differed != 0;
	*(volatile int *)((unsigned int)&up_fuse_old | 0x40000000u) = psp_keep("upfuseold") != 0;
	sceKernelDcacheWritebackRange(ima_fast, sizeof(ima_fast));
	platform_log("audio: ADPCM decoded %s; its table checked against the formula at %lu codes, %lu differed",
		ima_fast_old ? (differed ? "by the formula: the table differed" : "by the formula, as before (imatableoff)")
			: "by one table lookup a sample (IMATABLE)",
		(unsigned long)IMA_ROWS * 16 * (sizeof(predictors) / sizeof(predictors[0])), differed);
}

#if PSP_SNDSTREAM
/* ---------- SNDSTREAMLONG: the mixer's side (the reader is at the end of this file)

A streamed sound (a long ambient loop, "sndstream") keeps a one-page stub in
the sound cache; its samples come from the stick in pieces, each a whole
number of ADPCM blocks of either channel count, into a pool outside the
cache. A packet of it carries the sound's id and where it starts in the
samples, so a block's bytes are found by their place in the sound, not by
the packet's address (which runs past the stub). This runs on whichever CPU
mixes, the ME included: it reads only the piece table and the pieces, which
only the main CPU writes. The reader changes a piece only once no voice is
before it (it is behind every voice playing the sound), and marks it ready
last, after its bytes are in RAM, so a mix sees a piece whole or not at all */
#define SNDSTREAM_PIECE_BYTES (113ul * 2 * XBOX_ADPCM_BLOCK_BYTES) /* 8,136: whole mono and stereo blocks */
#define SNDSTREAM_PIECE_ROOM 8192ul /* the reader rounds a read up to 512-byte sectors */
#define SNDSTREAM_PIECES_MAX 128
#define PIECE_FREE 0
#define PIECE_READING 1
#define PIECE_READY 2
struct sndstream_piece
{
	/* what the mixer reads: whose piece it is (the sound's id, the piece's
	number in its samples), its bytes, and whether they are in */
	unsigned long id;
	long index;
	const unsigned char *buffer;
	volatile long state;
	/* the read's completion flag: the piece's own, or for a stub's first
	piece the engine's loaded flag (the game may wait for it on its own
	thread, where the reader runs). A piece being read whose flag says done
	is whole in RAM, so the mixer takes it before the reader marks it */
	const volatile unsigned char *flag;
	/* the reader's own */
	unsigned long stamp;
	long bytes;
	SceUInt64 issued_us;
	volatile unsigned char done; /* the stick reader's completion flag */
	unsigned char first; /* a stub's first piece: sets its loaded flag */
};
static struct sndstream_piece sndstream_pieces[SNDSTREAM_PIECES_MAX] __attribute__((aligned(64)));
static int sndstream_piece_count = 0;
static int sndstream_pieces_kept = -1;
static int sndstream_dialog = 0, sndstream_devices = 0;

static unsigned long sndstream_packet(const void *permutation, const unsigned char *data, unsigned long *offset,
	int *check);
static void sndstream_collect(struct psp_stream *stream);

static inline const unsigned char *piece_bytes(const struct sndstream_piece *piece)
{
	long state = piece->state;

	return state == PIECE_READY || (state == PIECE_READING && *piece->flag == 1) ? piece->buffer : NULL;
}

/* the piece of sound id numbered index, if its bytes are in */
static const unsigned char *stream_piece(struct psp_stream *stream, unsigned long id, long index)
{
	int k = stream->snd_hint;

	if (k >= 0 && k < sndstream_piece_count && sndstream_pieces[k].id == id && sndstream_pieces[k].index == index)
		return piece_bytes(&sndstream_pieces[k]);
	for (k = 0; k < sndstream_piece_count; k++)
		if (sndstream_pieces[k].id == id && sndstream_pieces[k].index == index)
		{
			stream->snd_hint = k;
			return piece_bytes(&sndstream_pieces[k]);
		}
	return NULL;
}

/* a streamed packet's block: its bytes in a piece, or NULL (late: played
silent). With sndstreamcheck the cache holds the samples too: the block is
played from the cache's copy, and the piece's is compared with it */
static const unsigned char *stream_block(struct psp_stream *stream, const struct voice_packet *packet, long block)
{
	unsigned long bytes = XBOX_ADPCM_BLOCK_BYTES * stream->channels;
	unsigned long at = packet->snd_offset + (unsigned long)block * bytes;
	long index = (long)(at / SNDSTREAM_PIECE_BYTES);
	unsigned long within = at - (unsigned long)index * SNDSTREAM_PIECE_BYTES;
	const unsigned char *piece = NULL;

	if (within + bytes > SNDSTREAM_PIECE_BYTES)
		stream->snd_unaligned++;
	else
		piece = stream_piece(stream, packet->snd_id, index);
	if (packet->snd_check)
	{
		const unsigned char *real = packet->data + block * bytes;

		if (!piece)
			stream->snd_late++;
		else
		{
			/* word by word: both are 4-byte aligned (blocks of 36 bytes from
			page-aligned blocks and pieces), and the ME calls nothing */
			const unsigned int *a = (const unsigned int *)real, *b = (const unsigned int *)(piece + within);
			unsigned long k, differ = 0;

			for (k = 0; k < bytes / 4; k++)
				differ |= a[k] ^ b[k];
			if (differ)
			{
				if (!stream->snd_mismatched)
				{
					stream->snd_differ_id = packet->snd_id;
					stream->snd_differ_at = at;
				}
				stream->snd_mismatched++;
			}
			else
				stream->snd_checked++;
		}
		return real;
	}
	if (!piece)
	{
		stream->snd_late++;
		return NULL;
	}
	stream->snd_blocks++;
	return piece + within;
}

/* a late block's samples: silence (a loop, not memset: the ME calls nothing) */
__attribute__((optimize("no-tree-loop-distribute-patterns")))
static void stream_silence(struct psp_stream *stream, const struct voice_packet *packet, long block)
{
	short *decoded = stream->decoded[block & 1];
	unsigned long k;

	for (k = 0; k < XBOX_ADPCM_BLOCK_SAMPLES * 2; k++)
		decoded[k] = 0;
	stream->decoded_packet[block & 1] = packet;
	stream->decoded_block[block & 1] = block;
}
#endif

/* one Xbox ADPCM block of a packet into the stream's decoded block: per
channel a 4-byte header (the block's first sample, step index), then 4-byte
groups of eight nibbles, low nibble first, alternating between channels; 64
samples per channel. The nibbles code the 63 samples after the header's: the
64th only pads the block. Decoding it in place of the header's sample put a
wrong sample in every 64, a buzz at 344 Hz in 22 kHz sounds (after
ChupathingyCE's dsound_sdl.c decode_adpcm, Jeff Clark). "adpcmold" decodes
as before. The decoded block is always two channels (a mono one twice) */
static void decode_block_fast(struct psp_stream *stream, const struct voice_packet *packet, long block,
	const unsigned char *data)
{
	unsigned long channels = stream->channels;
	short *decoded = stream->decoded[block & 1];
	unsigned long channel, byte;

	for (channel = 0; channel < channels; channel++)
	{
		const unsigned char *header = data + channel * 4;
		int predictor = (short)(header[0] | (header[1] << 8));
		int row = (header[2] > 88 ? 88 : header[2]) * 8;
		/* a mono block goes into both channels as it decodes, not by a second pass */
		short *output = decoded + channel;
		unsigned long other = channels == 1 ? 1 : 0;

		output[0] = (short)predictor;
		output[other] = (short)predictor;
		/* byte b of the 32 holds samples 2b + 1 and 2b + 2: the last byte's
		high nibble only pads the block */
		for (byte = 0; byte < 32; byte++)
		{
			unsigned int nibbles = data[4 * channels + ((byte >> 2) * channels + channel) * 4 + (byte & 3)];
			short *pair = output + 2 * (2 * byte + 1);
			short sample = (short)ima_expand_fast((int)(nibbles & 0xf), &predictor, &row);

			pair[0] = sample;
			pair[other] = sample;
			if (byte == 31)
				break;
			sample = (short)ima_expand_fast((int)(nibbles >> 4), &predictor, &row);
			pair[2] = sample;
			pair[2 + other] = sample;
		}
	}
	stream->decoded_packet[block & 1] = packet;
	stream->decoded_block[block & 1] = block;
}

static void decode_block(struct psp_stream *stream, const struct voice_packet *packet, long block)
{
	unsigned long channels = stream->channels;
	const unsigned char *data = packet->data + block * XBOX_ADPCM_BLOCK_BYTES * channels;
	unsigned long channel, group, byte;

#if PSP_SNDSTREAM
	if (packet->snd_id)
	{
		data = stream_block(stream, packet, block);
		if (!data)
		{
			/* its piece isn't in RAM: the block plays silent, and the next
			decodes as usual, since each block carries its own predictor */
			stream_silence(stream, packet, block);
			return;
		}
	}
#endif
	if (!stream->ima_fast_old && !stream->adpcm_old)
	{
		decode_block_fast(stream, packet, block, data);
		return;
	}
	for (channel = 0; channel < channels; channel++)
	{
		const unsigned char *header = data + channel * 4;
		int predictor = (short)(header[0] | (header[1] << 8));
		int index = header[2] > 88 ? 88 : header[2];
		short *output = stream->decoded[block & 1] + channel;
		unsigned long first = stream->adpcm_old ? 0 : 1;

		if (first)
			output[0] = (short)predictor;
		for (group = 0; group < 8; group++)
		{
			const unsigned char *nibbles = data + 4 * channels + (group * channels + channel) * 4;

			for (byte = 0; byte < 4; byte++)
			{
				/* nibble n codes sample n + first */
				unsigned long sample = group * 8 + byte * 2 + first;

				output[2 * sample] = (short)ima_expand(nibbles[byte] & 0xf, &predictor, &index);
				if (sample + 1 < XBOX_ADPCM_BLOCK_SAMPLES)
					output[2 * (sample + 1)] = (short)ima_expand(nibbles[byte] >> 4, &predictor, &index);
			}
		}
	}
	if (channels == 1)
	{
		unsigned long sample;

		for (sample = 0; sample < XBOX_ADPCM_BLOCK_SAMPLES; sample++)
			stream->decoded[block & 1][2 * sample + 1] = stream->decoded[block & 1][2 * sample];
	}
	stream->decoded_packet[block & 1] = packet;
	stream->decoded_block[block & 1] = block;
}

/* a frame of a packet, left and right */
static inline void packet_frame(struct psp_stream *stream, const struct voice_packet *packet, unsigned long frame,
	int *left, int *right)
{
	if (stream->adpcm)
	{
		long block = (long)(frame >> 6);
		const short *pair;

		if (block != stream->decoded_block[block & 1] || packet != stream->decoded_packet[block & 1])
			decode_block(stream, packet, block);
		pair = stream->decoded[block & 1] + 2 * (frame & 63);
		*left = pair[0];
		*right = pair[1];
	}
	else
	{
		const short *samples = (const short *)packet->data + frame * stream->channels;

		*left = samples[0];
		*right = samples[stream->channels - 1];
	}
}

/* ---------- 3D */

static float dot3(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static int audio_old = -1; /* "audioold": muffling, Doppler and cones off */
/* voice blocks each item changed, for the log: a count of 0 with the items
on means the game never asked for it on that stretch */
static unsigned long fx_prepared = 0, fx_muffled = 0, fx_shifted = 0, fx_coned = 0;

/* DirectSound's cone: full inside half the inside angle of the sound's
facing, the outside volume past half the outside angle, between them a
straight line in millibels. offset: listener to sound */
static float cone_gain(const struct stream_game *stream, const float *offset)
{
	float length, facing, cosine, angle, inside, outside;

	if (audio_old || stream->cone_inside >= 360 || stream->cone_outside_millibels >= 0)
		return 1.0f;
	length = sqrtf(dot3(offset, offset));
	if (length < 1.0e-4f)
		return 1.0f;
	/* the facing as the game gives it, not taken to be of unit length */
	facing = sqrtf(dot3(stream->cone_forward, stream->cone_forward));
	if (facing < 1.0e-4f)
		return 1.0f;
	cosine = -dot3(offset, stream->cone_forward) / (length * facing);
	angle = acosf(cosine < -1.0f ? -1.0f : cosine > 1.0f ? 1.0f : cosine) * (180.0f / 3.14159265f);
	inside = 0.5f * (float)stream->cone_inside;
	outside = 0.5f * (float)(stream->cone_outside > stream->cone_inside ? stream->cone_outside : stream->cone_inside);
	if (angle <= inside)
		return 1.0f;
	fx_coned++;
	if (angle >= outside || outside <= inside)
		return gain_from_millibels(stream->cone_outside_millibels);
	return gain_from_millibels((LONG)(stream->cone_outside_millibels * (angle - inside) / (outside - inside)));
}

/* DirectSound's Doppler shift (factor 1) from the velocity along the line
to the listener, held to an octave either way; 1 for a sound that isn't 3D */
static float doppler_ratio(const struct stream_game *stream)
{
	float offset[3], length, closing;
	int axis;

	if (audio_old || !stream->has_3d || stream->mode == DS3DMODE_DISABLE)
		return 1.0f;
	for (axis = 0; axis < 3; axis++)
		offset[axis] = stream->mode == DS3DMODE_HEADRELATIVE ? stream->position[axis] :
			stream->position[axis] - listener.position[axis];
	length = sqrtf(dot3(offset, offset));
	if (length < 1.0e-4f)
		return 1.0f;
	/* metres a second away from the listener */
	closing = dot3(stream->velocity, offset) / length * listener.distance_factor;
	if (closing < -171.5f)
		closing = -171.5f;
	if (closing > 343.0f)
		closing = 343.0f;
	return 343.0f / (343.0f + closing);
}

static void spatialize(const struct stream_game *stream, float *left, float *right)
{
	float offset[3], right_axis[3], distance, attenuation, pan, side, ahead;
	int axis;

	if (stream->mode == DS3DMODE_HEADRELATIVE)
	{
		for (axis = 0; axis < 3; axis++)
			offset[axis] = stream->position[axis];
		side = offset[0];
		ahead = offset[2];
	}
	else
	{
		for (axis = 0; axis < 3; axis++)
			offset[axis] = stream->position[axis] - listener.position[axis];
		/* left-handed: right = top x front */
		right_axis[0] = listener.top[1] * listener.front[2] - listener.top[2] * listener.front[1];
		right_axis[1] = listener.top[2] * listener.front[0] - listener.top[0] * listener.front[2];
		right_axis[2] = listener.top[0] * listener.front[1] - listener.top[1] * listener.front[0];
		side = dot3(offset, right_axis);
		ahead = dot3(offset, listener.front);
	}
	distance = sqrtf(dot3(offset, offset)) * listener.distance_factor;

	/* DirectSound's inverse distance law, held beyond the maximum distance */
	attenuation = 1.0f;
	if (distance > stream->minimum_distance && stream->minimum_distance > 0.0f)
	{
		float clamped = distance < stream->maximum_distance ? distance : stream->maximum_distance;

		attenuation = stream->minimum_distance /
			(stream->minimum_distance + listener.rolloff_factor * (clamped - stream->minimum_distance));
	}

	/* equal power pan; close sources and sources straight ahead or behind
	stay centred, and neither ear drops below a quarter */
	{
		float horizontal = sqrtf(side * side + ahead * ahead);
		float angle;

		pan = horizontal > 1.0e-4f ? side / horizontal : 0.0f;
		if (distance < stream->minimum_distance && stream->minimum_distance > 0.0f)
			pan *= distance / stream->minimum_distance;
		pan *= 0.75f;
		angle = (pan + 1.0f) * 0.25f * 3.14159265f;
		/* E17 (keep word vfputrig, psp_vfputrig.c): the panning on the VFPU,
		which no ear tells from the libm's */
		psp_vfpu_sincosf(angle, right, left);
	}
	attenuation *= stream->i3dl2_gain * cone_gain(stream, offset);
	*left *= attenuation;
	*right *= attenuation;
}

static int gain_to_fixed(float gain)
{
	int value = (int)(gain * GAIN_ONE);

	return value < 0 ? 0 : value > 2 * GAIN_ONE ? 2 * GAIN_ONE : value;
}

static void voice_gains(const struct stream_game *stream, int *left, int *right)
{
	float l, r;

	if (stream->has_3d && stream->mode != DS3DMODE_DISABLE)
		spatialize(stream, &l, &r);
	else
	{
		l = stream->mix_left;
		r = stream->mix_right;
	}
	*left = gain_to_fixed(l * stream->volume * master_volume);
	*right = gain_to_fixed(r * stream->volume * master_volume);
}

/* ---------- mixing */

/* the head packet that still has frames to play, the stream's position
moved into it; NULL when the stream has run dry */
static struct voice_packet *current_packet(struct psp_stream *stream)
{
	for (;;)
	{
		struct voice_packet *packet = NULL;
		unsigned long position;

		for (position = 0; position < stream->packet_count; position++)
		{
			struct voice_packet *candidate = &stream->packets[(stream->packet_head + position) & stream->packet_mask];

			if (!candidate->finished)
			{
				packet = candidate;
				break;
			}
		}
		if (!packet || stream->frame < packet->frames)
			return packet;
		stream->frame -= packet->frames;
		packet->finished = TRUE;
	}
}

/* the one-pole low-pass y += a (x - y) whose gain at I3DL2's reference
frequency (5 kHz) is the source's high-frequency loss; Q14 (GAIN_ONE: none) */
static int lowpass_coefficient(LONG millibels)
{
	float gain, c, g2, p, b;

	if (millibels > -30)
		return GAIN_ONE;
	gain = gain_from_millibels(millibels);
	if (gain < 0.05f)
		gain = 0.05f;
	g2 = gain * gain;
	c = cosf(2.0f * 3.14159265f * 5000.0f / OUTPUT_RATE);
	p = 1.0f - g2 * c;
	/* b = 1 - a, the smaller root of (1 - g2) b^2 - 2 p b + (1 - g2) = 0 */
	b = (p - sqrtf(p * p - (1.0f - g2) * (1.0f - g2))) / (1.0f - g2);
	return (int)((1.0f - b) * GAIN_ONE);
}

/* build 86: a voice's step and gains for the next block, on the main CPU
(floats and its maths library); 0 when it won't play (paused, nothing queued) */
static int prepare_voice(struct psp_stream *stream)
{
	struct stream_game *game = GAME_OF(stream);

	if (game->paused || !stream->packet_count || !stream->sample_rate)
		return 0;
	{
		extern int psp_keep(const char *name);
		static int overrun = -1;

		static int adpcm_old = -1, memix_old = -1, mix_kinds = -1;

		if (overrun < 0)
		{
			overrun = psp_keep("silentoverrun") != 0;
			adpcm_old = psp_keep("adpcmold") != 0;
			audio_old = psp_keep("audioold") != 0;
			memix_old = psp_keep("memixold") != 0;
			mix_kinds = psp_keep("mixkinds") != 0;
		}
		stream->silent_overrun = overrun;
		stream->adpcm_old = adpcm_old;
		stream->ima_fast_old = ima_fast_old;
		stream->approx = audio_approx;
		stream->memix_old = memix_old;
		stream->mix_kinds = mix_kinds;
		if (audio_old || !game->has_3d)
			stream->lowpass_coefficient = GAIN_ONE;
		else if (game->hf_millibels != stream->lowpass_millibels || !stream->lowpass_coefficient)
		{
			stream->lowpass_millibels = game->hf_millibels;
			stream->lowpass_coefficient = lowpass_coefficient(game->hf_millibels);
		}
	}
	{
		unsigned long rate = game->frequency ? game->frequency : stream->sample_rate;
		float doppler = doppler_ratio(game);

		if (doppler != 1.0f)
		{
			rate = (unsigned long)(rate * doppler + 0.5f);
			fx_shifted++;
		}
#ifndef PSP_DBLSOFT_OFF
		/* rate * 65536 / OUTPUT_RATE in 32 bits (a doubled rate passes 2^32
		shifted): the whole part of rate / OUTPUT_RATE, shifted, plus the
		remainder's share, which is the same floor (FIXES_DBLSOFT, dsoft3: the
		64-bit divide was a libgcc call on every stream's update) */
		stream->step = ((rate / OUTPUT_RATE) << 16) + ((rate % OUTPUT_RATE) << 16) / OUTPUT_RATE;
#else
		stream->step = (unsigned long)(((unsigned long long)rate << 16) / OUTPUT_RATE);
#endif
	}
	voice_gains(game, &stream->target_left, &stream->target_right);
	if (stream->target_left > stream->peak_gain)
		stream->peak_gain = stream->target_left;
	stream->prepared_blocks++;
	fx_prepared++;
	if (stream->lowpass_coefficient < GAIN_ONE)
		fx_muffled++;
	return 1;
}

/* the ME's cycle counter (coprocessor 0 register 9). Only the ME may read
it: the main CPU runs in user mode, where the read faults. So it is read
only through a split pointer, which only the ME's audio job passes */
static inline unsigned int me_count(void)
{
	unsigned int count;

	__asm__ __volatile__("mfc0 %0, $9" : "=r"(count));
	return count;
}

/* a decoded block's frames, decoding it if its buffer holds another; with
decode_cycles (the ME only), the decode's cycles are added to it */
static inline const short *decoded_frames(struct psp_stream *stream, const struct voice_packet *packet, long block,
	unsigned int *decode_cycles)
{
	if (block != stream->decoded_block[block & 1] || packet != stream->decoded_packet[block & 1])
	{
		if (decode_cycles)
		{
			unsigned int start = me_count();

			decode_block(stream, packet, block);
			*decode_cycles += me_count() - start;
		}
		else
			decode_block(stream, packet, block);
	}
	return stream->decoded[block & 1];
}

/* MEMIX: a heard voice into output, the same integer steps as mix_voice's
loop under "memixold", so the same samples and the same state after; only
the work is less. The ME mixes every voice every 23 ms block, and what it
spends here it can't spend on the draws it replays:
- the position, its fraction and the decoded block's address stay in
  registers through a run inside one packet, stored once at the run's end
  (current_packet reads the position). In the old loop every sample loaded
  and stored them through the record, and asked packet_frame twice where the
  answer changes once in 64 frames;
- a mono voice is the same sample on both sides until its gains (a mono
  ADPCM block is decoded into both channels; a mono PCM frame is read twice),
  so it is interpolated and filtered once. Only when the filter's two states
  agree, which they do for any voice that has only ever been mono.
"memixcheck" mixes each block both ways and counts any difference */
static unsigned long mix_heard(struct psp_stream *stream, int *output, unsigned long frames, int left, int right,
	int ramp_left, int ramp_right, int lowpass, int *lowpass_left_io, int *lowpass_right_io, unsigned int *decode_cycles)
{
	unsigned long done = 0, step = stream->step, channels = stream->channels;
	int lowpass_left = *lowpass_left_io, lowpass_right = *lowpass_right_io;
	int mono = channels == 1 && lowpass_left == lowpass_right;
	/* H1: every heard voice in play is mono ADPCM, so the general loop's
	tests of pcm, mono, the block, the edge and the filter answer the same
	for a whole run inside one decoded block; a kind's loop asks them once a
	run. A zero step would never leave its frame: the general loop keeps it */
	int kinds = stream->mix_kinds && mono && stream->adpcm && step;

	while (done < frames)
	{
		struct voice_packet *packet = current_packet(stream);
		unsigned long last, frame, fraction;
		const short *pcm = NULL, *base = NULL;
		long block = -1;

		if (!packet)
			break;
		last = packet->frames - 1;
		frame = stream->frame;
		fraction = stream->fraction;
		if (!stream->adpcm)
			pcm = (const short *)packet->data;
		while (done < frames && frame <= last)
		{
			const short *pair, *next = NULL;
			int l0, r0, sample_left, sample_right;

			if (kinds)
			{
				/* the run: this block's frames up to its last or the
				packet's, whichever comes first */
				unsigned long limit = frame | 63;
				int *out = output + 2 * done;

				if ((long)(frame >> 6) != block)
				{
					block = (long)(frame >> 6);
					base = decoded_frames(stream, packet, block, decode_cycles);
				}
				if (limit > last)
					limit = last;
				if (step == 0x10000 && !fraction)
				{
					/* at 1:1 with no fraction the fraction stays 0: nothing
					to interpolate, and every frame to the limit is read
					from this block */
					unsigned long n = limit - frame + 1;
					const short *in = base + 2 * (frame & 63);
					int *end;

					if (n > frames - done)
						n = frames - done;
					end = out + 2 * n;
					if (lowpass < GAIN_ONE)
						for (; out < end; out += 2, in += 2)
						{
							lowpass_left += ((in[0] - lowpass_left) * lowpass) >> 14;
							out[0] += (lowpass_left * (left >> 8)) >> 14;
							out[1] += (lowpass_left * (right >> 8)) >> 14;
							left += ramp_left;
							right += ramp_right;
						}
					else
					{
						for (; out < end; out += 2, in += 2)
						{
							int sample = in[0];

							out[0] += (sample * (left >> 8)) >> 14;
							out[1] += (sample * (right >> 8)) >> 14;
							left += ramp_left;
							right += ramp_right;
						}
						/* so a filter that starts next block starts from the sound */
						lowpass_left = in[-2];
					}
					frame += n;
					done += n;
					continue;
				}
				{
					/* pitched: a frame before the limit has its next frame
					in this block, and with no fraction the interpolation
					adds (d * 0) >> 15 = 0, the general loop's answer without
					one. The limit's own frame (the next across the block's
					end, or none at the packet's last) takes the general
					loop below */
					int *end = output + 2 * frames;

					if (lowpass < GAIN_ONE)
						for (; out < end && frame < limit; out += 2)
						{
							const short *in = base + 2 * (frame & 63);
							int l = in[0], sample = l + (((in[2] - l) * ((int)fraction >> 1)) >> 15);

							lowpass_left += ((sample - lowpass_left) * lowpass) >> 14;
							out[0] += (lowpass_left * (left >> 8)) >> 14;
							out[1] += (lowpass_left * (right >> 8)) >> 14;
							left += ramp_left;
							right += ramp_right;
							fraction += step;
							frame += fraction >> 16;
							fraction &= 0xFFFF;
						}
					else
						for (; out < end && frame < limit; out += 2)
						{
							const short *in = base + 2 * (frame & 63);
							int l = in[0], sample = l + (((in[2] - l) * ((int)fraction >> 1)) >> 15);

							lowpass_left = sample;
							out[0] += (sample * (left >> 8)) >> 14;
							out[1] += (sample * (right >> 8)) >> 14;
							left += ramp_left;
							right += ramp_right;
							fraction += step;
							frame += fraction >> 16;
							fraction &= 0xFFFF;
						}
					done = (unsigned long)(out - output) / 2;
					/* past the limit (a step over the block's end) the next
					block's run starts; at it, one frame of the general loop */
					if (done >= frames || frame != limit)
						continue;
				}
			}
			if (pcm)
			{
				pair = pcm + frame * channels;
				if (fraction && frame < last)
					next = pair + channels;
				l0 = pair[0];
				r0 = pair[channels - 1];
			}
			else
			{
				if ((long)(frame >> 6) != block)
				{
					block = (long)(frame >> 6);
					base = decoded_frames(stream, packet, block, decode_cycles);
				}
				pair = base + 2 * (frame & 63);
				/* the next frame, in the other buffer past a block's end */
				if (fraction && frame < last)
					next = (frame & 63) != 63 ? pair + 2 : decoded_frames(stream, packet, block + 1, decode_cycles);
				l0 = pair[0];
				r0 = pair[1];
			}
			if (mono)
			{
				int sample = l0;

				if (next)
					sample = l0 + (((next[0] - l0) * ((int)fraction >> 1)) >> 15);
				if (lowpass < GAIN_ONE)
				{
					lowpass_left += ((sample - lowpass_left) * lowpass) >> 14;
					sample = lowpass_left;
				}
				else
					lowpass_left = sample;
				output[2 * done] += (sample * (left >> 8)) >> 14;
				output[2 * done + 1] += (sample * (right >> 8)) >> 14;
			}
			else
			{
				sample_left = l0;
				sample_right = r0;
				if (next)
				{
					int l1 = next[0], r1 = pcm ? next[channels - 1] : next[1];

					sample_left = l0 + (((l1 - l0) * ((int)fraction >> 1)) >> 15);
					sample_right = r0 + (((r1 - r0) * ((int)fraction >> 1)) >> 15);
				}
				if (lowpass < GAIN_ONE)
				{
					lowpass_left += ((sample_left - lowpass_left) * lowpass) >> 14;
					lowpass_right += ((sample_right - lowpass_right) * lowpass) >> 14;
					sample_left = lowpass_left;
					sample_right = lowpass_right;
				}
				else
				{
					lowpass_left = sample_left;
					lowpass_right = sample_right;
				}
				output[2 * done] += (sample_left * (left >> 8)) >> 14;
				output[2 * done + 1] += (sample_right * (right >> 8)) >> 14;
			}
			left += ramp_left;
			right += ramp_right;
			fraction += step;
			frame += fraction >> 16;
			fraction &= 0xFFFF;
			done++;
		}
		stream->frame = frame;
		stream->fraction = fraction;
	}
	*lowpass_left_io = lowpass_left;
	*lowpass_right_io = mono ? lowpass_left : lowpass_right;
	return done;
}

/* mixes one prepared voice into output (frames of stereo 32-bit sums);
returns whether it played. Integers only, no calls out of this file: the
Media Engine runs it (build 86) */
static int mix_voice(struct psp_stream *stream, int *output, unsigned long frames, unsigned int *decode_cycles)
{
	unsigned long step = stream->step, done = 0;
	int target_left = stream->target_left, target_right = stream->target_right, left, right, ramp_left, ramp_right;
	/* the filter's state in registers: output may alias the record, so
	fields written in the loop would be stored every sample */
	int lowpass = stream->lowpass_coefficient, lowpass_left = stream->lowpass_left, lowpass_right = stream->lowpass_right;

	if (!stream->gains_valid)
	{
		stream->current_left = target_left;
		stream->current_right = target_right;
		stream->gains_valid = TRUE;
	}
	/* 16 bits of fraction for the ramp */
	left = stream->current_left << 8;
	right = stream->current_right << 8;
	ramp_left = ((target_left - stream->current_left) << 8) / (int)frames;
	ramp_right = ((target_right - stream->current_right) << 8) / (int)frames;
	stream->current_left = target_left;
	stream->current_right = target_right;
	if (MUSIC_BLOCK(stream) || (!target_left && !target_right && !left && !right))
	{
		/* silent: just advance, and never past the end of what's queued.
		A voice that runs out inside the block stops at the end, as a heard
		one does (not counted as a gap: nothing was heard): an overshoot left in frame would be
		played off the start of the packet the game queues next (finished
		packets wait for the game to collect them, so the queue doesn't look
		empty and the position isn't reset) */
		unsigned long long advance;
		unsigned long position, queued = 0;

		if (!current_packet(stream))
			return 0;
		for (position = 0; position < stream->packet_count; position++)
		{
			const struct voice_packet *candidate = &stream->packets[(stream->packet_head + position) & stream->packet_mask];

			if (!candidate->finished)
				queued += candidate->frames;
		}
		queued -= stream->frame;
		advance = (unsigned long long)step * frames + stream->fraction;
		if ((advance >> 16) < queued || stream->silent_overrun)
		{
			stream->frame += (unsigned long)(advance >> 16);
			stream->fraction = (unsigned long)(advance & 0xFFFF);
			stream->mixed_frames += frames;
		}
		else
		{
			unsigned long long reach = ((unsigned long long)queued << 16) - stream->fraction;
			unsigned long played = (unsigned long)((reach + step - 1) / step);

			if (played > frames)
				played = frames;
			stream->frame += queued;
			stream->fraction = 0;
			stream->mixed_frames += played;
		}
		current_packet(stream);
		return 0;
	}
	if (!stream->memix_old)
		done = mix_heard(stream, output, frames, left, right, ramp_left, ramp_right, lowpass, &lowpass_left, &lowpass_right,
			decode_cycles);
	else
	{
		while (done < frames)
		{
			struct voice_packet *packet = current_packet(stream);
			unsigned long last;

			if (!packet)
				break;
			last = packet->frames - 1;
			/* as many frames as stay inside this packet */
			while (done < frames && stream->frame <= last)
			{
				int l0, r0, l1, r1, sample_left, sample_right;
				int fraction = (int)stream->fraction;

				packet_frame(stream, packet, stream->frame, &l0, &r0);
				if (fraction && stream->frame < last)
				{
					packet_frame(stream, packet, stream->frame + 1, &l1, &r1);
					/* build 89: the fraction halved first. (l1 - l0) reaches
					+-65535 and the fraction 65535, so the product overflowed 32
					bits on loud, fast-changing sound at a pitch other than 1:
					a full-scale click of the wrong sign (code review) */
					sample_left = l0 + (((l1 - l0) * (fraction >> 1)) >> 15);
					sample_right = r0 + (((r1 - r0) * (fraction >> 1)) >> 15);
				}
				else
				{
					sample_left = l0;
					sample_right = r0;
				}
				/* 16-bit samples: a difference under 2^16 times a Q14
				coefficient stays inside 32 bits */
				if (lowpass < GAIN_ONE)
				{
					lowpass_left += ((sample_left - lowpass_left) * lowpass) >> 14;
					lowpass_right += ((sample_right - lowpass_right) * lowpass) >> 14;
					sample_left = lowpass_left;
					sample_right = lowpass_right;
				}
				else
				{
					/* so a filter that starts next block starts from the sound */
					lowpass_left = sample_left;
					lowpass_right = sample_right;
				}
				output[2 * done] += (sample_left * (left >> 8)) >> 14;
				output[2 * done + 1] += (sample_right * (right >> 8)) >> 14;
				left += ramp_left;
				right += ramp_right;
				stream->fraction += step;
				stream->frame += stream->fraction >> 16;
				stream->fraction &= 0xFFFF;
				done++;
			}
		}
	}
	stream->lowpass_left = lowpass_left;
	stream->lowpass_right = lowpass_right;
	stream->mixed_frames += done;
	if (done < frames)
		stream->dry_frames += frames - done;
	return 1;
}

/* build 86: one block of the listed (prepared) voices into output, through
sums (frames of stereo 32-bit sums); returns the voices heard, and whether
any sample hit the 16-bit limit. The same code on either CPU: the main
CPU's mix below, and the Media Engine's job (psp_meaudio.c). No library
calls (the zeroing is a loop, not memset).
split (the ME's audio job only; NULL on the main CPU): where the ME's cycles
went, so its audio time can be taken apart on the PSP: [0] decoding, [1] the
voices' mixing including their decoding, [2] zeroing the sums and writing
the output, [3] voice-frames heard */
__attribute__((optimize("no-tree-loop-distribute-patterns")))
unsigned long psp_dsound_mix_split(void *const *list, unsigned long count, int *sums, short *output,
	unsigned long frames, unsigned long *clipped, unsigned int *split)
{
	unsigned long sample, k, voices = 0;
	unsigned int mark = split ? me_count() : 0, ends = 0;
	int over = 0;

	for (sample = 0; sample < frames * OUTPUT_CHANNELS; sample++)
		sums[sample] = 0;
	if (split)
	{
		unsigned int now = me_count();

		ends += now - mark;
		mark = now;
	}
	for (k = 0; k < count; k++)
		voices += mix_voice((struct psp_stream *)list[k], sums, frames, split ? &split[0] : NULL);
	if (split)
	{
		unsigned int now = me_count();

		split[1] += now - mark;
		mark = now;
	}
	for (sample = 0; sample < frames * OUTPUT_CHANNELS; sample++)
	{
		int value = sums[sample];

		if (value > 32767 || value < -32768)
			over = 1;
		output[sample] = (short)(value > 32767 ? 32767 : value < -32768 ? -32768 : value);
	}
	if (split)
	{
		split[2] += ends + (me_count() - mark);
		split[3] += voices * frames;
	}
	*clipped = over;
	return voices;
}

unsigned long psp_dsound_mix_list(void *const *list, unsigned long count, int *sums, short *output,
	unsigned long frames, unsigned long *clipped)
{
	return psp_dsound_mix_split(list, count, sums, output, frames, clipped, NULL);
}

/* ---------- the Media Engine's cache hand-over by ranges

Each audio block used to cost the whole data cache three times: the main CPU
wrote back all of its own before the job, and the ME dropped all of its own
before and after it, so a ring job the block was mixed between or inside (the
perception rays' BSP walks) started over from main RAM. Instead, the lines the
mix can touch:
- it reads and writes a voice's fields above packets[] (STREAM_HEAD_BYTES:
  position, decoded blocks, gains) and the queued packets' entries, from
  packet_head on, packet_count of them (current_packet; a finished packet's
  flag is the only write there). The game's side changes the rest only
  under the mixer's lock, which the audio thread holds through the job;
- it reads, of each queued packet's samples, only the stretch this block can
  reach (sample_lines);
- it writes its output block, and its sums (on the ME's own stack, or
  psp_me.c's array that only the ME uses).
Nothing else: no calls out of this file, and the step and gains are worked
out on the main CPU beforehand (prepare_voice). Past 16 KB of lines, a range
costs more instructions than the whole cache (YAPSPD's rule, b4 research),
so the whole cache it is, as before. Off unless "meaudioranges": the rig
showed no shorter mix on the ME, and c40's cinematic ticks were longer with
it among the words on. Either CPU may choose the whole cache on its own:
each side's ranges cover every line the other can write back */
#define STREAM_HEAD_BYTES offsetof(struct psp_stream, packets)
#define HANDOVER_WHOLE_BYTES (16 * 1024)
#define LINES_COUNT 0     /* only measure */
#define LINES_WRITEBACK 1 /* the main CPU, before the job */
#define LINES_DROP 2      /* write back and drop: the ME before and after, the main CPU after */

/* the whole lines holding [start, start + bytes), the op on each; returns
their bytes. Through the uncached view, no cache holds them */
static unsigned long cache_lines(const void *start, unsigned long bytes, int op)
{
	unsigned int line, first = (unsigned int)start & ~63u, end = (unsigned int)start + bytes;

	/* (the eDRAM, "soundedram": no main-RAM line holds it; sndpart's window, a
	kernel address only the ME uses, whose own writes are all it holds) */
	if (!bytes || ((unsigned int)start & 0xC0000000u) || ((unsigned int)start & 0x1FFFFFFFu) < 0x08000000u)
		return 0;
	if (op == LINES_WRITEBACK)
		for (line = first; line < end; line += 64)
			__builtin_allegrex_cache(0x1A, line);
	else if (op == LINES_DROP)
		for (line = first; line < end; line += 64)
			__builtin_allegrex_cache(0x1B, line);
	return ((end + 63) & ~63u) - first;
}

/* the queued packets' entries: one stretch of the ring, or two where it wraps */
static unsigned long entry_lines(struct psp_stream *stream, int op)
{
	unsigned long size = stream->packet_mask + 1, first = stream->packet_head & stream->packet_mask;
	unsigned long count = stream->packet_count, run;

	if (count > size)
		count = size;
	run = count < size - first ? count : size - first;
	return cache_lines(&stream->packets[first], run * sizeof(struct voice_packet), op) +
		cache_lines(&stream->packets[0], (count - run) * sizeof(struct voice_packet), op);
}

/* the samples one block of mix_voice can read. Output frame k reads the
packet frame at ((fraction + k * step) >> 16) past the first one and the
frame after it, so all reads lie within reach + 2 frames from the first,
reach = (fraction + frames * step) >> 16, counted on across packets as
current_packet moves through them (skipping finished ones, and those the
position is already past). An ADPCM frame reads its whole block */
static unsigned long sample_lines(struct psp_stream *stream, unsigned long frames, int op)
{
	unsigned long long need = (((unsigned long long)stream->step * frames + stream->fraction) >> 16) + 2;
	unsigned long position, frame = stream->frame, bytes = 0;

	if (need > HANDOVER_WHOLE_BYTES * 2)
		return HANDOVER_WHOLE_BYTES + 1; /* a pitch far past any the game asks for: the whole cache */
	for (position = 0; position < stream->packet_count && need; position++)
	{
		const struct voice_packet *packet = &stream->packets[(stream->packet_head + position) & stream->packet_mask];
		unsigned long last, start, end;

		if (packet->finished)
			continue;
		if (frame >= packet->frames)
		{
			frame -= packet->frames;
			continue;
		}
#if PSP_SNDSTREAM
		/* a streamed packet reads pieces, past these ranges: the whole cache */
		if (packet->snd_id)
			return HANDOVER_WHOLE_BYTES + 1;
#endif
		last = packet->frames - 1;
		if (need - 1 < last - frame)
			last = frame + (unsigned long)need - 1;
		need -= last - frame + 1;
		if (stream->adpcm)
		{
			start = (frame >> 6) * XBOX_ADPCM_BLOCK_BYTES * stream->channels;
			end = ((last >> 6) + 1) * XBOX_ADPCM_BLOCK_BYTES * stream->channels;
		}
		else
		{
			start = frame * 2 * stream->channels;
			end = (last + 1) * 2 * stream->channels;
		}
		bytes += cache_lines(packet->data + start, end - start, op);
		frame = 0;
	}
	return bytes;
}

static unsigned long voice_lines(struct psp_stream *stream, unsigned long frames, int op)
{
	return cache_lines(stream, STREAM_HEAD_BYTES, op) + entry_lines(stream, op) +
		(MUSIC_BLOCK(stream) ? 0 : sample_lines(stream, frames, op));
}

/* the ME's side (psp_me.c's me_serve), before the job (after 0) and after it
(after 1, only when before returned 1): 1 done by ranges, 0 past 16 KB (the
caller drops the whole cache). Before, each voice's fields are dropped and
then read, so its entries and samples are worked out from the main CPU's
values, not from a copy left in this cache */
int psp_dsound_me_lines(void *const *list, unsigned long count, unsigned long frames, int after)
{
	unsigned long k, bytes = count * ((STREAM_HEAD_BYTES + 63) & ~(size_t)63);

	if (bytes > HANDOVER_WHOLE_BYTES)
		return 0;
	for (k = 0; k < count; k++)
		cache_lines(list[k], STREAM_HEAD_BYTES, LINES_DROP);
	__asm__ __volatile__("sync" : : : "memory");
	for (k = 0; k < count; k++)
	{
		struct psp_stream *stream = (struct psp_stream *)list[k];

		bytes += entry_lines(stream, LINES_DROP);
		if (after)
			continue;
		__asm__ __volatile__("sync" : : : "memory");
		bytes += sample_lines(stream, frames, LINES_DROP);
		if (bytes > HANDOVER_WHOLE_BYTES)
			return 0;
	}
	__asm__ __volatile__("sync" : : : "memory");
	return 1;
}

/* ---------- R2: the sound cache in the Media Engine's eDRAM ("soundedram")

docs/me_programme.md, section 5. The sound cache moves out of main RAM into
the ME's eDRAM (psp_me.h's ME_EDRAM_SOUND), and the main RAM it took is free:
the heap's, or with "soundgeo" the geometry cache's (psp_geo.c).
- The engine sees the cache at its eDRAM address, which only the ME can
  reach. The mixer runs there, and nothing on the main CPU reads a sound's
  bytes (cache_lines leaves them alone).
- The stick reader can't write eDRAM either, so a sound is read a piece at a
  time into a small staging area in main RAM, and an ME job
  (ME_JOB_SOUNDCOPY) copies each piece into place. The engine's "loaded" is
  set once the last piece is in, so a sound stays "loading" a frame or two
  longer than it did.
- The main CPU can't mix from it. Once the ME has stopped (a failure, a
  sleep: the eDRAM is then taken as lost, even if a wake restarts the ME),
  every voice playing from the cache is mixed silent and new loads are
  marked failed: the sound goes quiet, and nothing reads eDRAM from the main
  CPU. The log says so once.
- The emulator has no eDRAM. There (mode 2) the cache stays in main RAM at
  the eDRAM's size, and the staging and the copies run as the simulated ME's
  jobs, so all of the PSP's path but the eDRAM itself is exercised. */
#define SOUND_BLOCK_DEFAULT_BYTES (1792ul * 1024) /* psp_platform.c's SOUND_CACHE_DEFAULT_KB: what the cache took in main RAM */
#define STAGE_SLOTS 2
#define STAGE_SLOT_BYTES (64 * 1024)
/* a whole number of the reader's 512-byte sectors; the last piece of a sound,
which the reader rounds up to them, still fits its slot */
#define STAGE_PIECE_BYTES (60 * 1024)
#define UPLOADS_MAX 256
#define READ_FAILED 2 /* cache_files.h's _cache_file_read_failed */

short cache_file_read(long tag_index, long offset, long size, void *buffer, unsigned char *completion_flag,
	unsigned char blocking);
int psp_sound_cache_alive(void *datum, short identifier, void *sound, unsigned long address);
unsigned long psp_sound_cache_bytes(void);
int psp_me_start(void);
int psp_keep(const char *word);

struct upload
{
	unsigned char *dest;   /* the sound's block in the cache */
	unsigned char *loaded; /* the engine's flag */
	void *datum, *sound;
	long tag_index, file_offset, size, issued, copied, block_index;
	SceUInt64 start_us;
	short identifier;
	unsigned char outstanding, failed, dead, finished;
};

#define SLOT_FREE 0
#define SLOT_READING 1
#define SLOT_COPYING 2
struct stage_slot
{
	unsigned char *buffer;
	volatile unsigned char done; /* the reader's completion flag: 0 reading, 1 read, READ_FAILED */
	unsigned char state;
	int upload;
	long offset, bytes;
	unsigned int sequence;
};

static int edram_mode = -1; /* 0 off, 1 the ME's eDRAM, 2 simulated (the emulator) */
static unsigned char *sound_block = NULL, *spare_start = NULL;
static unsigned long block_bytes = 0, spare_bytes = 0;
/* the cache's bytes, when the main CPU must not read them (modes 1 and 2) */
static const unsigned char *edram_lo = NULL, *edram_hi = NULL;
static unsigned int stops_at_start = 0;
static int edram_lost = 0, copy_out = 0;
static struct upload uploads[UPLOADS_MAX];
static unsigned int upload_head = 0, upload_count = 0, next_sequence = 0;
static struct stage_slot stage[STAGE_SLOTS];
static unsigned long up_sounds = 0, up_bytes = 0, up_pieces = 0, up_jobs = 0, up_failed = 0, up_dropped = 0,
	up_full = 0, up_after_loss = 0, up_wait_us = 0, up_worst_us = 0, muted_blocks = 0, muted_said = 0;

/* ---------- SNDEDRAM2: the sound cache's last pages in the ME's eDRAM ("sndedram")

docs/plan_sound_edram.md. The cache stays one run of pages of its usual size,
the first ones main RAM (the main tier), the last ones the eDRAM at
psp_me.h's ME_EDRAM_SNDTIER (the tier); the lruv cache keeps every block on
one side of the boundary (its fence, psp_fixups.py FIXES_SNDEDRAM). A sound
placed in the tier is loaded through R2's staging above, and each piece goes
in by R2's copy job, the ME writing its own memory, so no DMA crosses the bus
while the ME works ("sndtierdma": by DMACplus from this thread instead, which
hung the PSP-3000 at b30's opening with the ME busy). The rest of R2's
rules hold for the tier's sounds: only the ME reads them, a block the main CPU
mixes plays them silent, and once the ME has stopped the eDRAM is taken as
lost. A loss closes the tier rather than the sound: the fence locks its
pages, the sounds in it are given up as they stop playing, and the main tier
goes on alone. In the emulator (mode 2) the tier is main RAM after the main
tier and the DMA a copy, so everything but the eDRAM and the DMA runs */
#define TIER_MIN_KB 64
#if PSP_MUSIC
#define TIER_END (ME_EDRAM_MUSIC < ME_EDRAM_SNDTIER_END ? ME_EDRAM_MUSIC : ME_EDRAM_SNDTIER_END)
#else
#define TIER_END ME_EDRAM_SNDTIER_END
#endif
/* a 60 KB piece takes ~240 us at the measured 256 MB/s */
#define TIER_DMA_TIMEOUT_US 2000
/* failed fills before the tier is closed for the session */
#define TIER_FAILS_MAX 3
/* a copy job still running this long after the ME started it counts as a
failure: its pieces are given up when it ends (the staging can't be reused
before), and its sounds read again. Its two pieces copy in well under a
millisecond, so this bounds the copy alone. Its wait to be started is bounded
apart (TIER_JOB_WAIT_US): the ME serves its slots between ring jobs, and a
heavy frame's drawing there runs past a second on the PSP, which isn't a
failure of the copy. Both are seen only when the sound thread polls, so the
collected figures carry the poll's gap (a frame or more at low frame rates) */
#define TIER_JOB_LATE_US 250000
#define TIER_JOB_WAIT_US 2000000
static int tier_mode = -1; /* 0 off, 1 the ME's eDRAM, 2 simulated (the emulator) */
/* "sndpart": more of the tier in the ME partition (docs/plan_sound_edram.md,
"sndpart"; the lab's projects/sierra/coord/handoff/me_partition_tenant_design.md). The
cache's pages run main RAM, then the eDRAM, then the partition's window, a
boundary between each that no block crosses (lruv's fence and fence2), and the
main tier is that much smaller: the heap's. Like the eDRAM, the window is the
ME's alone (the main CPU in user mode reads 0xCCCCCCCC there), filled by the
same copy job and read by the mixer on the ME through its cache. Nothing
else touches the partition in play, across a sleep or at a relaunch (the
lab's partmap runs on both PSPs and Sheepdog01's hand sleep), and nothing allocates
from it, so the window is all of it but its first page: that page holds the
witness word (+0x18) the ME library reads to pick its table at a restart.
The rest of Sony's image there is entered only by mehalt's and mewake's
paths, so sndpart is refused with either. Its bottom 960 KB is claimed from sysmem at
start (psp_me_claim_partition), so no allocator can be handed it.
Every piece is hashed as copied and as held (sndedramcheck's check, always on
with it), a mismatch failing the fill as any failed copy */
#define SNDPART_ME_BASE (0x88300000u + 4096u) /* the window as the ME sees it, cached */
#define SNDPART_MOST (956ul * 1024) /* halo_claim.prx claims the bottom 960 KB; the first 4 KB page is Sony's */
#define SNDPART_LEAST (64ul * 1024)
static unsigned long part_bytes = 0;
static unsigned char *part_base = NULL;
/* the tier's memories together, eDRAM then window: one span of addresses
that holds no main-RAM address on the PSP (0x80... to 0x88400000), and in
the emulator one stretch of the block */
static unsigned char *me_lo = NULL, *me_hi = NULL;
/* mode 2 with the tier where the PSP's is: the emulator maps that part of the
eDRAM, so the heap gives the sound block only what a PSP's takes */
static int tier_mapped = 0;
static unsigned long tier_bytes = 0, tier_main_bytes = 0;
static unsigned char *cache_base = NULL, *tier_base = NULL;
static int tier_check = 0, tier_fails = 0, tier_left = -1, tier_dma = 0;
static unsigned long tier_jobs = 0, tier_job_pieces = 0, tier_job_us = 0, tier_job_worst_us = 0, tier_job_late = 0,
	tier_job_failed = 0, tier_job_me_us = 0, tier_job_me_worst_us = 0, tier_job_begun_worst_us = 0, tier_job_unbegun = 0;
/* the copy job out: when it was handed over, what it should copy, the
staging's hash (sndedramcheck), and whether it has run past its bound */
static SceUInt64 copy_at = 0, copy_begun_at = 0; /* begun: the first poll that saw the ME on it */
static unsigned int copy_bytes = 0, copy_hash = 0;
static int copy_late = 0, copy_checked = 0;
static const char *tier_closed_why = NULL;
static unsigned long tier_dmas = 0, tier_dma_us = 0, tier_dma_worst_us = 0, tier_dma_failed = 0, tier_check_bad = 0,
	tier_given_up = 0, tier_polls = 0;
unsigned long psp_sound_tier_drained = 0; /* psp_sound_tier_drain's, xbox_sound_cache.c */
long psp_sound_tier_rescue(unsigned long low, unsigned long high,
	void (*moved)(unsigned long from, unsigned long to, unsigned long bytes, const unsigned char *loaded),
	int (*in_play)(unsigned long from, unsigned long bytes), unsigned long *counts);
/* "sndtierback" (B55): at the tier's loss to an ME stop, the sounds playing or
loading in it read again into the main tier (tier_rescue). -1 not read yet */
static int tier_back = -1;
/* read again, their bytes, left for want of room, read again by their packets
alone */
static unsigned long tier_back_counts[4], tier_back_missed = 0, tier_back_gap_blocks = 0;
/* sounds the rescue left for want of room in the main tier, tried again each
poll as the main tier's own sounds end (tier_back_retry), for at most
TIER_BACK_RETRY_US after the stop; the tries made */
#define TIER_BACK_RETRY_US 30000000
static unsigned long tier_back_no_room = 0, tier_back_retries = 0;
static SceUInt64 tier_back_stop_at = 0;
/* the new blocks whose reads haven't landed: voices in them play muted until
then (the audio thread reads the table; the game thread fills and clears it) */
#define TIER_BACK_MOST 64
#define TIER_BACK_WAIT_US 5000000
static struct
{
	const unsigned char *volatile lo, *hi;
	const unsigned char *loaded;
	SceUInt64 since;
} tier_back_pending[TIER_BACK_MOST];
static volatile int tier_back_pending_count = 0;
extern int psp_lruv_fence_closed; /* lruv_cache.c: the fence locks the tier's pages */
int psp_keep_number(const char *prefix, int fallback);
int psp_sound_cache_drop(long block_index);
long psp_sound_tier_drain(unsigned long low, unsigned long high);

int psp_sound_tier_mode(void)
{
	if (tier_mode < 0)
	{
		tier_mode = 0;
		if (!psp_keep("sndedram"))
			return 0;
		if (psp_keep("soundedram"))
			platform_log("sound tier: refused: soundedram puts the whole cache in the eDRAM");
		else if (psp_keep("edramcheck"))
			platform_log("sound tier: refused: edramcheck patterns the same stretch");
		else if (!psp_me_start())
			platform_log("sound tier: refused: the Media Engine is off (nomeaudio?)");
		else
		{
			unsigned long room = TIER_END > ME_EDRAM_SNDTIER ? TIER_END - ME_EDRAM_SNDTIER : 0;
			unsigned long kb = (unsigned long)psp_keep_number("sndtier", (int)(room / 1024));

			if (kb > room / 1024)
				kb = room / 1024;
			if (kb >= TIER_MIN_KB)
			{
				tier_bytes = (kb * 1024) & ~0xFFFul;
				tier_mode = psp_me_simulated() ? 2 : 1;
				/* "sndtierram" keeps the emulator's tier in the heap, where an emulator without the map has it */
				tier_mapped = tier_mode == 2 && !psp_keep("sndtierram") && psp_me_sim_edram_holds(ME_EDRAM_SNDTIER, (unsigned int)tier_bytes);
				tier_check = psp_keep("sndedramcheck");
				tier_dma = psp_keep("sndtierdma");
				if (psp_keep("sndpart"))
				{
					const char *refused = tier_dma ? "sndtierdma's DMA reaches the eDRAM only" :
						psp_keep("mehalt") ? "mehalt enters Sony's image in the partition" :
						psp_keep("mewake") && !psp_keep("mewakeoff") ? "mewake's restart enters Sony's image in the partition" :
						psp_keep("partmap") || psp_keep("partmapall") || psp_keep("meddrprobe") ? "a partition probe patterns it" : NULL;

					/* "sndpartnoclaim" (the lead 2026-11-17): a measuring form only.
					User code can't make the claim (sysmem's user call refuses
					partition 4, 800200d6), so until our own kernel module makes
					it the window may go in unclaimed, priced by its pair, never
					the default: nothing allocates partition 4 [the lab's
					me_partition_sleep_read.md] and nothing writes it in play, a
					sleep or a relaunch [partmap], and every piece is hashed as
					copied and as held */
					if (!refused && tier_mode == 1 && psp_me_claim_partition() < 0)
					{
						if (psp_keep("sndpartnoclaim"))
							platform_log("sound tier: sndpart without the claim (sndpartnoclaim: a measuring form, not the shipping one)");
						else
							refused = "sysmem wouldn't give the partition";
					}
					if (refused)
						platform_log("sound tier: sndpart refused: %s", refused);
					else
					{
						part_bytes = SNDPART_MOST;
						tier_check = 1;
					}
				}
			}
			else
				platform_log("sound tier: refused: %lu KB of room, %d KB at least", kb, TIER_MIN_KB);
		}
	}
	return tier_mode;
}

/* the main tier's pages, which is where lruv's fence goes; 0 with no tier */
long psp_sound_tier_fence_page(void)
{
	return psp_sound_tier_mode() ? (long)(tier_main_bytes >> 12) : 0;
}

/* the eDRAM's last page and the window's first (lruv's fence2), 0 without sndpart */
long psp_sound_part_fence_page(void)
{
	return psp_sound_tier_mode() && part_bytes ? (long)((tier_main_bytes + tier_bytes) >> 12) : 0;
}

/* lruv's address for a block (the cache's base plus its first page's
offset), moved into the tier when it lies past the main tier */
void *psp_sound_tier_address(void *address)
{
	unsigned char *at = (unsigned char *)address;

	if (tier_mode > 0 && at >= cache_base + tier_main_bytes)
	{
		unsigned long past = (unsigned long)(at - (cache_base + tier_main_bytes));

		return past < tier_bytes || !part_bytes ? tier_base + past : part_base + (past - tier_bytes);
	}
	return address;
}

int psp_sound_tier_holds(const void *address)
{
	const unsigned char *at = (const unsigned char *)address;

	return tier_mode > 0 && ((at >= tier_base && at < tier_base + tier_bytes) ||
		(part_bytes && at >= part_base && at < part_base + part_bytes));
}

/* sound_dsound_xbox.c's check before it queues a sound: its samples lie
inside the cache, in one tier. Without the tier, the engine's own test */
int psp_sound_cache_holds(const void *address, long size)
{
	const unsigned char *at = (const unsigned char *)address;
	unsigned long main_bytes = tier_mode > 0 ? tier_main_bytes : psp_sound_cache_bytes();

	if (size < 0 || !cache_base)
		return 0;
#if PSP_SNDSTREAM
	{
		/* a streamed sound's stub holds a page, not the samples its size
		says: its packets run past it, and nothing reads them there */
		extern int psp_sndstream_holds(const void *address);

		if (psp_sndstream_holds(address))
			return 1;
	}
#endif
	if (at >= cache_base && (unsigned long)size <= main_bytes && (unsigned long)(at - cache_base) <= main_bytes - size)
		return 1;
	if (tier_mode > 0 && part_bytes && at >= part_base && (unsigned long)size <= part_bytes &&
		(unsigned long)(at - part_base) <= part_bytes - size)
		return 1;
	return tier_mode > 0 && at >= tier_base && (unsigned long)size <= tier_bytes &&
		(unsigned long)(at - tier_base) <= tier_bytes - size;
}

static void tier_close(const char *why)
{
	if (tier_mode <= 0 || tier_closed_why)
		return;
	tier_closed_why = why;
	psp_lruv_fence_closed = 1;
	tier_left = -1;
	platform_log("sound tier: closed (%s): no new sound goes in the eDRAM; the ones there are given up as they stop playing, and the main tier's %lu KB plays on",
		why, tier_main_bytes / 1024);
}

static void tier_fill_failed(void)
{
	if (++tier_fails >= TIER_FAILS_MAX)
		tier_close(tier_dma ? "DMAs failed" : "copy jobs failed");
}

/* "sndtierdma": a piece into the tier by DMACplus; 1 when it's there */
static int tier_copy(const unsigned char *from, unsigned char *to, unsigned long bytes)
{
	static unsigned char check[4096] __attribute__((aligned(64)));
	unsigned int us = 0;
	int ok = 1;

	if (tier_mode == 2)
	{
		/* the emulator: a copy at the DMA's place */
		memcpy(to, from, bytes);
		if (tier_check && memcmp(to, from, bytes))
			tier_check_bad++;
	}
	else
	{
		int result = psp_me_dma_edram(1, (void *)from, (unsigned int)(to - (unsigned char *)ME_EDRAM), (unsigned int)bytes,
			TIER_DMA_TIMEOUT_US, &us);

		ok = result == 0;
		if (!ok)
		{
			tier_dma_failed++;
			platform_log("sound tier: a DMA of %lu bytes to eDRAM %08x failed (%d after %u us)", bytes,
				(unsigned int)(to - (unsigned char *)ME_EDRAM), result, us);
		}
		else if (tier_check)
		{
			unsigned long at, part;

			/* read back through the same controller, a 4 KB part at a time */
			for (at = 0; at < bytes && ok; at += part)
			{
				part = bytes - at < sizeof(check) ? bytes - at : sizeof(check);
				sceKernelDcacheWritebackInvalidateRange(check, sizeof(check));
				if (psp_me_dma_edram(0, check, (unsigned int)(to + at - (unsigned char *)ME_EDRAM), (unsigned int)part,
					TIER_DMA_TIMEOUT_US, NULL))
					ok = 0;
				else
				{
					sceKernelDcacheInvalidateRange(check, sizeof(check));
					if (memcmp(check, from + at, part))
						ok = 0;
				}
			}
			if (!ok)
				tier_check_bad++;
		}
	}
	tier_dmas++;
	tier_dma_us += us;
	if (us > tier_dma_worst_us)
		tier_dma_worst_us = us;
	if (!ok)
		tier_fill_failed();
	return ok;
}

int psp_sound_edram_mode(void)
{
	if (edram_mode < 0)
	{
		edram_mode = 0;
		if (!psp_keep("soundedram"))
			return 0;
		if (psp_keep("edramcheck"))
			platform_log("sound eDRAM: refused: edramcheck patterns the same stretch; the cache stays in main RAM");
		else if (!psp_me_start())
			platform_log("sound eDRAM: refused: the Media Engine is off (nomeaudio?); the cache stays in main RAM");
		else
			edram_mode = psp_me_simulated() ? 2 : 1;
	}
	return edram_mode;
}

/* the cache's room when it isn't in main RAM, 0 when it is */
unsigned long psp_sound_edram_bytes(void)
{
	return psp_sound_edram_mode() ? ME_EDRAM_SOUND_END - ME_EDRAM_SOUND : 0;
}

/* what physical_memory_allocate takes from the heap at start-up for sound
(psp_fixups.py FIXES_SOUNDEDRAM): the cache itself (mode 0, as before), the
staging (mode 1), both (mode 2). With "soundgeo" at least the default
cache's 1,792 KB, the part sound doesn't use being the geometry cache's */
unsigned long psp_sound_block_bytes(void)
{
	unsigned long cache = psp_sound_cache_bytes(), stage_bytes = STAGE_SLOTS * STAGE_SLOT_BYTES, want;
	int geo = psp_keep("soundgeo");

	if (!psp_sound_edram_mode() && psp_sound_tier_mode())
	{
		/* the tier never takes more than a quarter of a megabyte's main tier */
		/* the window takes what the main tier can spare above its 256 KB */
		if (part_bytes && tier_bytes + part_bytes + 256 * 1024 > cache)
			part_bytes = cache > tier_bytes + 256 * 1024 + SNDPART_LEAST ? (cache - tier_bytes - 256 * 1024) & ~0xFFFul : 0;
		if (tier_bytes + 256 * 1024 > cache)
			tier_bytes = cache > 512 * 1024 ? (cache - 256 * 1024) & ~0xFFFul : 0;
		tier_main_bytes = cache - tier_bytes - part_bytes;
		want = stage_bytes + tier_main_bytes + (tier_mode == 2 && !tier_mapped ? tier_bytes : 0) +
			(tier_mode == 2 ? part_bytes : 0);
	}
	else switch (psp_sound_edram_mode())
	{
	case 1:
		want = geo && SOUND_BLOCK_DEFAULT_BYTES > stage_bytes ? SOUND_BLOCK_DEFAULT_BYTES : stage_bytes;
		break;
	case 2:
		want = cache + stage_bytes;
		break;
	default:
		want = geo && cache < SOUND_BLOCK_DEFAULT_BYTES ? SOUND_BLOCK_DEFAULT_BYTES : cache;
		break;
	}
#if PSP_MUSIC
	{
		/* the music path's RAM, which psp_sound_cache_bytes took off the
		cache (64 bytes over, to align it) */
		extern unsigned long psp_music_block_bytes(void);
		unsigned long music = psp_music_block_bytes();

		if (music)
			want += music + 64;
	}
#endif
	return want;
}

/* the block taken (psp_sound_block_bytes' size): where the cache goes, which
the engine keeps as the cache's base */
void *psp_sound_cache_place(void *block)
{
	unsigned long cache = psp_sound_cache_bytes(), stage_bytes = STAGE_SLOTS * STAGE_SLOT_BYTES, used;
	unsigned char *base;
	int k;

	sound_block = (unsigned char *)block;
	block_bytes = psp_sound_block_bytes();
	if (!psp_sound_edram_mode() && psp_sound_tier_mode() && tier_bytes)
	{
		base = sound_block + stage_bytes;
		used = stage_bytes + tier_main_bytes + (tier_mode == 2 && !tier_mapped ? tier_bytes : 0) +
			(tier_mode == 2 ? part_bytes : 0);
		tier_base = tier_mode == 1 || tier_mapped ? (unsigned char *)(ME_EDRAM + ME_EDRAM_SNDTIER) : base + tier_main_bytes;
		/* the emulator's window stands in on the heap, after the main tier
		(and after the tier, when the emulator doesn't map the eDRAM): the
		heap then takes what the PSP's does with the tier and window off it */
		part_base = !part_bytes ? NULL : tier_mode == 1 ? (unsigned char *)SNDPART_ME_BASE :
			base + tier_main_bytes + (tier_mapped ? 0 : tier_bytes);
		for (k = 0; k < STAGE_SLOTS; k++)
			stage[k].buffer = sound_block + k * STAGE_SLOT_BYTES;
		/* one span over the tier and the window, whichever is lower: no
		block of the main tier lies inside it (on the PSP the eDRAM, then the
		partition; in the emulator the window right after the main tier), so
		the cache's blocks are told apart by it. Anything else (a voice's
		packet, which may be a stream's buffer) asks psp_sound_tier_holds */
		me_lo = part_bytes && part_base < tier_base ? part_base : tier_base;
		me_hi = part_bytes && part_base + part_bytes > tier_base + tier_bytes ? part_base + part_bytes : tier_base + tier_bytes;
		edram_lo = me_lo;
		edram_hi = me_hi;
		if (part_bytes)
			platform_log("sound tier: sndpart: %lu KB more of the tier in the ME partition at %08x (%s), the main tier %lu KB smaller; every piece hashed as copied and as held",
				part_bytes / 1024, (unsigned int)part_base,
				tier_mode == 2 ? "SIMULATED: the emulator's window is main RAM" : "from 4 KB, the ME's cached view", part_bytes / 1024);
		stops_at_start = psp_me_stops;
		platform_log("sound tier: on (%s): the cache's %lu KB as %lu KB of main RAM at %08x and %lu KB at %08x (eDRAM %lu-%lu KB), %lu KB staging; %lu KB taken from the heap%s",
			tier_mode == 2 ? (tier_mapped ? "SIMULATED: the emulator's tier is its mapped eDRAM" : "SIMULATED: the emulator's tier is main RAM") :
				tier_dma ? "the ME's eDRAM, filled by DMACplus (sndtierdma)" : "the ME's eDRAM, filled by the ME's copy job",
			cache / 1024, tier_main_bytes / 1024, (unsigned int)base, tier_bytes / 1024, (unsigned int)tier_base,
			(unsigned long)ME_EDRAM_SNDTIER / 1024, (unsigned long)(ME_EDRAM_SNDTIER + tier_bytes) / 1024, stage_bytes / 1024,
			block_bytes / 1024, tier_check ? "; sndedramcheck: every piece read back" : "");
	}
	else switch (psp_sound_edram_mode())
	{
	case 1:
		base = (unsigned char *)(ME_EDRAM + ME_EDRAM_SOUND);
		used = stage_bytes;
		break;
	case 2:
		base = sound_block + stage_bytes;
		used = stage_bytes + cache;
		break;
	default:
		base = sound_block;
		used = cache;
		break;
	}
	cache_base = base;
	if (edram_mode)
	{
		for (k = 0; k < STAGE_SLOTS; k++)
			stage[k].buffer = sound_block + k * STAGE_SLOT_BYTES;
		edram_lo = base;
		edram_hi = base + cache;
		stops_at_start = psp_me_stops;
	}
#if PSP_MUSIC
	{
		extern unsigned long psp_music_block_bytes(void);
		extern void psp_music_block_place(void *at);
		unsigned long music = psp_music_block_bytes();

		if (music && sound_block)
		{
			psp_music_block_place((void *)(((unsigned long)(sound_block + used) + 63) & ~63ul));
			platform_log("sound block: %lu KB for the music path (musicstream)", music / 1024);
			used += music + 64;
		}
	}
#endif
	if (psp_keep("soundgeo") && block_bytes >= used + 64 * 1024)
	{
		spare_start = sound_block + used;
		spare_bytes = (unsigned long)(block_bytes - used) & ~63ul;
	}
	platform_log("sound eDRAM: %s; cache %lu KB at %08x, %lu KB taken from the heap at %08x (%lu KB staging), %lu KB spare%s",
		edram_mode == 1 ? "the cache in the ME's eDRAM" : edram_mode == 2 ? "SIMULATED (the emulator: the cache stays in main RAM)" :
		"off", cache / 1024, (unsigned int)base, block_bytes / 1024, (unsigned int)sound_block,
		edram_mode ? stage_bytes / 1024 : 0, spare_bytes / 1024, spare_bytes ? " for the geometry cache (soundgeo)" : "");
	return base;
}

/* the start-up block's part sound doesn't use, for the geometry cache
("soundgeo"); NULL if none */
void *psp_sound_spare(unsigned long *bytes)
{
	*bytes = spare_bytes;
	return spare_bytes ? spare_start : NULL;
}

/* the block to give back (physical_memory_free) */
void *psp_sound_block(void)
{
	return sound_block;
}

/* instead of the stick reader's read into the cache (xbox_sound_cache.c):
the sound's pieces go through the staging. block_index, the sound's block,
is given up if a tier load fails (-1: R2's, kept) */
void psp_sound_upload_begin(long tag_index, long file_offset, long size, void *dest, unsigned char *loaded, void *datum,
	short identifier, void *sound, long block_index)
{
	struct upload *u;

	if (edram_lost || upload_count == UPLOADS_MAX)
	{
		/* failed, as a read that failed: the voices that play it are mixed
		silent after a loss (mute_cache_voices) */
		if (edram_lost)
			up_after_loss++;
		else
			up_full++;
		*loaded = READ_FAILED;
		return;
	}
	u = &uploads[(upload_head + upload_count++) % UPLOADS_MAX];
	memset(u, 0, sizeof(*u));
	u->dest = (unsigned char *)dest;
	u->loaded = loaded;
	u->datum = datum;
	u->sound = sound;
	u->identifier = identifier;
	u->tag_index = tag_index;
	u->file_offset = file_offset;
	u->size = size;
	u->block_index = block_index;
	u->start_us = sceKernelGetSystemTimeWide();
	*loaded = 0;
}

void psp_sound_upload_start(long tag_index, long file_offset, long size, void *dest, unsigned char *loaded, void *datum,
	short identifier, void *sound)
{
	psp_sound_upload_begin(tag_index, file_offset, size, dest, loaded, datum, identifier, sound, -1);
}

/* the map is going (sound_cache_close): no waiting load is the engine's any more */
void psp_sound_upload_forget(void)
{
	unsigned int k;

	for (k = 0; k < upload_count; k++)
		uploads[(upload_head + k) % UPLOADS_MAX].dead = 1;
}

static void piece_done(struct stage_slot *slot, int copied)
{
	struct upload *u = &uploads[slot->upload];

	u->outstanding--;
	if (copied)
		u->copied += slot->bytes;
	else
		u->failed = 1;
	slot->state = SLOT_FREE;
}

/* a moved sound: the game's own view of its queued packets moved here, and
the mixer's packets by a command, which the audio thread applies in order
after any packet queued before it (the old place is only ever mixed silent
meanwhile, which reads no samples) */
static void tier_back_moved(unsigned long from, unsigned long to, unsigned long bytes, const unsigned char *loaded)
{
	struct psp_stream *stream;
	unsigned long k;

	for (stream = streams; stream; stream = GAME_OF(stream)->next)
	{
		struct stream_game *game = GAME_OF(stream);

		for (k = 0; k <= stream->packet_mask; k++)
		{
			unsigned long at = (unsigned long)game->entry[k].packet.pvBuffer;

			if (at >= from && at - from < bytes)
				game->entry[k].packet.pvBuffer = (LPVOID)(to + (at - from));
		}
		if ((unsigned long)game->queued_end >= from && (unsigned long)game->queued_end - from <= bytes)
			game->queued_end = (const unsigned char *)(to + ((unsigned long)game->queued_end - from));
	}
	if (tier_back_pending_count < TIER_BACK_MOST)
	{
		int k = tier_back_pending_count;

		tier_back_pending[k].hi = (const unsigned char *)(to + bytes);
		tier_back_pending[k].loaded = loaded;
		tier_back_pending[k].since = sceKernelGetSystemTimeWide();
		tier_back_pending[k].lo = (const unsigned char *)to;
		tier_back_pending_count = k + 1;
	}
	move_to = (const unsigned char *)to;
	mix_command(NULL, CMD_MOVE, (const unsigned char *)from, bytes, 0);
}

/* the game thread, each poll: a block whose read has landed (or failed, or
waited past TIER_BACK_WAIT_US) leaves the table, so its voices are heard */
static void tier_back_pending_poll(void)
{
	int k;

	for (k = 0; k < tier_back_pending_count; k++)
	{
		unsigned char state = *(volatile const unsigned char *)tier_back_pending[k].loaded;

		if (tier_back_pending[k].lo && (state || sceKernelGetSystemTimeWide() - tier_back_pending[k].since > TIER_BACK_WAIT_US))
			tier_back_pending[k].lo = NULL;
	}
	for (k = 0; k < tier_back_pending_count && !tier_back_pending[k].lo; k++)
		;
	if (k == tier_back_pending_count)
	{
		platform_log("sound tier: every sound read again after the ME's stop has landed; voices played muted meanwhile in %lu blocks, and still from the tier in %lu (sndtierback)",
			tier_back_gap_blocks, muted_blocks);
		tier_back_pending_count = 0;
	}
}

/* the audio thread: whether a packet lies in a block still being read again */
static int tier_back_waiting(const unsigned char *data)
{
	int k;

	for (k = 0; k < tier_back_pending_count; k++)
	{
		const unsigned char *lo = tier_back_pending[k].lo;

		if (lo && data >= lo && data < tier_back_pending[k].hi)
			return 1;
	}
	return 0;
}

/* whether a voice has a packet queued inside a sound's bytes: the game's view
of each queue, which holds what the command ring hasn't handed the mixer yet */
static int tier_back_in_play(unsigned long from, unsigned long bytes)
{
	struct psp_stream *stream;
	unsigned long k;

	for (stream = streams; stream; stream = GAME_OF(stream)->next)
	{
		struct stream_game *game = GAME_OF(stream);

		for (k = 0; k < game->count && k <= stream->packet_mask; k++)
		{
			unsigned long at = (unsigned long)game->entry[(game->head + k) & stream->packet_mask].packet.pvBuffer;

			if (at >= from && at - from < bytes)
				return 1;
		}
	}
	return 0;
}

/* at the ME's stop, before the tier is closed: nothing here reads or writes
the eDRAM (its sounds come again from the disc), so it holds however the ME
stopped */
static void tier_rescue(void)
{
	if (tier_back < 0)
		tier_back = psp_keep("sndtierback") != 0;
	if (!tier_back || tier_mode <= 0 || edram_lost)
		return;
	/* the fence closed first, so the new blocks come from the main tier */
	psp_lruv_fence_closed = 1;
	game_lock();
	tier_left = psp_sound_tier_rescue((unsigned long)me_lo, (unsigned long)me_hi,
		tier_back_moved, tier_back_in_play, tier_back_counts);
	pthread_mutex_unlock(&mixer_lock);
	tier_back_no_room = tier_back_counts[2];
	tier_back_stop_at = sceKernelGetSystemTimeWide();
	platform_log("sound tier: the ME stopped: %lu sounds playing or loading (%lu KB; %lu of them by their queued packets alone) read again from the disc into the main tier, %lu left for want of room; %ld left in the tier (sndtierback)",
		tier_back_counts[0], tier_back_counts[1] / 1024, tier_back_counts[3], tier_back_counts[2], tier_left);
}

/* a poll after the rescue that left sounds for want of room: the rescue again,
over what's still in the tier (what found room before is in the main tier
now and isn't met again); the main tier's blocks free as its sounds end. Like
the rescue, nothing here touches the eDRAM. Its voices meanwhile mix silent
from the tier (muted_blocks counts them) */
static void tier_back_retry(void)
{
	unsigned long counts[4] = { 0, 0, 0, 0 };

	if (sceKernelGetSystemTimeWide() - tier_back_stop_at > TIER_BACK_RETRY_US)
	{
		platform_log("sound tier: %lu sounds found no room in the main tier within %u s of the ME's stop, after %lu tries (sndtierback)",
			tier_back_no_room, TIER_BACK_RETRY_US / 1000000, tier_back_retries);
		tier_back_no_room = 0;
		return;
	}
	tier_back_retries++;
	game_lock();
	tier_left = psp_sound_tier_rescue((unsigned long)me_lo, (unsigned long)me_hi,
		tier_back_moved, tier_back_in_play, counts);
	pthread_mutex_unlock(&mixer_lock);
	tier_back_counts[0] += counts[0];
	tier_back_counts[1] += counts[1];
	tier_back_counts[3] += counts[3];
	if (counts[0])
		platform_log("sound tier: %lu sounds (%lu KB) found room in the main tier on try %lu, %lu ms after the ME's stop, and are read again from the disc; %lu still without (sndtierback)",
			counts[0], counts[1] / 1024, tier_back_retries,
			(unsigned long)((sceKernelGetSystemTimeWide() - tier_back_stop_at) / 1000), counts[2]);
	/* what's left without room now, playing still; a sound that stopped
	meanwhile was given up by the rescue's drain */
	tier_back_no_room = counts[2];
}

static void edram_loss(const char *why)
{
	if (edram_lost)
		return;
	edram_lost = 1;
	if (tier_mode > 0)
		tier_close(why);
	else
		platform_log("sound eDRAM: %s: the sound cache in its eDRAM is taken as lost; sounds from it are silent from here on",
			why);
}

/* the game thread, every frame (sound_cache_idle): collect the copy job,
start reads into free staging, hand what was read to the ME */
/* SNDNOYIELD and the counts behind it (psp_fixups.py, FIXES_SOUNDEDRAM's
request fixup): a request for a sound not yet loaded yields the thread only
when its caller waits for it ("soundyieldold": every such request yields, as
before); the counts say how often one yielded, and for how long */
unsigned long psp_sound_request_yields = 0, psp_sound_request_yield_us = 0, psp_sound_request_unyielded = 0;

int psp_sound_noyield(void)
{
	static int on = -1;

	if (on < 0)
		on = !psp_keep("soundyieldold");
	return on;
}

void psp_sound_request_report(void)
{
	platform_log("sound requests: %lu yields of the thread for a sound not loaded yet, %lu us in them (%lu us each); %lu not yielded (SNDNOYIELD%s)",
		psp_sound_request_yields, psp_sound_request_yield_us,
		psp_sound_request_yields ? psp_sound_request_yield_us / psp_sound_request_yields : 0,
		psp_sound_request_unyielded, psp_sound_noyield() ? "" : " off: soundyieldold");
}

#if PSP_SNDSTREAM
static void sndstream_poll(void);
#endif
void psp_sound_upload_poll(void)
{
	unsigned int k;

#if PSP_SNDSTREAM
	sndstream_poll();
#endif
	if (edram_mode <= 0 && tier_mode <= 0)
		return;
	tier_polls++;
	/* collected once done, never waited for: the ME may be on a long job */
	if (copy_out && (psp_me_submitted_done(ME_SLOT_PROBE) || psp_me_stops != stops_at_start))
	{
		unsigned int results[ME_JOB_RESULTS] = { 0 };
		int ok = psp_me_finish(ME_SLOT_PROBE, results) && results[0] != 0xFFFFFFFFu;

		copy_out = 0;
		if (tier_mode > 0)
		{
			unsigned long us = (unsigned long)(sceKernelGetSystemTimeWide() - copy_at);

			tier_job_us += us;
			if (us > tier_job_worst_us)
				tier_job_worst_us = us;
			/* the ME's own time on it, against the collected figure's poll gap */
			if (ok)
			{
				unsigned long me_us = psp_me_last_job_us(ME_SLOT_PROBE);

				tier_job_me_us += me_us;
				if (me_us > tier_job_me_worst_us)
					tier_job_me_worst_us = me_us;
			}
			if (ok && results[0] != copy_bytes)
				ok = 0;
			if (ok && copy_checked && (results[1] != copy_hash || results[2] != copy_hash))
			{
				ok = 0;
				tier_check_bad++;
			}
			if (!ok && psp_me_stops == stops_at_start)
			{
				tier_job_failed++;
				platform_log("sound tier: a copy job of %u bytes failed (copied %08x, hashes %08x %08x against %08x%s)",
					copy_bytes, results[0], results[1], results[2], copy_hash, copy_checked ? "" : ", unchecked");
				tier_fill_failed();
			}
			/* past its bound: counted as a failure then, given up now */
			if (copy_late)
				ok = 0;
		}
		for (k = 0; k < STAGE_SLOTS; k++)
			if (stage[k].state == SLOT_COPYING)
				piece_done(&stage[k], ok);
		if (!ok && tier_mode <= 0)
			edram_loss("the copy job failed");
	}
	else if (copy_out && tier_mode > 0 && !copy_late)
	{
		SceUInt64 now = sceKernelGetSystemTimeWide();

		if (!copy_begun_at && psp_me_submitted_begun(ME_SLOT_PROBE))
		{
			copy_begun_at = now;
			if (now - copy_at > tier_job_begun_worst_us)
				tier_job_begun_worst_us = (unsigned long)(now - copy_at);
		}
		if (copy_begun_at ? now - copy_begun_at > TIER_JOB_LATE_US : now - copy_at > TIER_JOB_WAIT_US)
		{
			copy_late = 1;
			tier_job_late++;
			tier_job_unbegun += !copy_begun_at;
			if (copy_begun_at)
				platform_log("sound tier: a copy job not done %u ms after the ME started it: its sounds are read again",
					TIER_JOB_LATE_US / 1000);
			else
				platform_log("sound tier: a copy job not started %u ms after it was handed over: its sounds are read again",
					TIER_JOB_WAIT_US / 1000);
			tier_fill_failed();
		}
	}
	if (tier_back_pending_count)
		tier_back_pending_poll();
	if (psp_me_stops != stops_at_start)
	{
		tier_rescue();
		edram_loss("the Media Engine stopped");
	}
	else if (tier_closed_why && !edram_lost)
		edram_loss(tier_closed_why);
	if (muted_blocks && !muted_said)
	{
		muted_said = 1;
		platform_log("sound eDRAM: voices from the cache mixed silent: the main CPU mixes and can't read the eDRAM");
	}
	/* a closed tier's sounds given up as they stop playing (a pass over the
	cache's sounds every 16th poll), until none is left */
	if (tier_mode > 0 && edram_lost && tier_left != 0 && tier_back_no_room)
		tier_back_retry();
	else if (tier_mode > 0 && edram_lost && tier_left != 0 && tier_polls % 16 == 0)
	{
		long before = tier_left;

		tier_left = psp_sound_tier_drain((unsigned long)me_lo, (unsigned long)me_hi);
		if (!tier_left && before)
			platform_log("sound tier: every sound in the eDRAM given up (%lu)", psp_sound_tier_drained);
	}
	/* pieces read for a load that's gone, or that failed */
	for (k = 0; k < STAGE_SLOTS; k++)
	{
		struct stage_slot *slot = &stage[k];

		if (slot->state != SLOT_READING || !slot->done)
			continue;
		if (slot->done == READ_FAILED)
			up_failed++;
		if (slot->done == READ_FAILED || edram_lost || uploads[slot->upload].dead)
			piece_done(slot, 0);
	}
	/* the tier: what's read, into the eDRAM in the order it was asked for, so
	a load finished by it is told below and its slot read into again */
	if (tier_mode > 0 && tier_dma && !edram_lost)
	{
		for (;;)
		{
			struct stage_slot *slot = NULL;
			struct upload *u;
			unsigned long bytes;

			for (k = 0; k < STAGE_SLOTS; k++)
				if (stage[k].state == SLOT_READING && stage[k].done == 1 &&
					(!slot || stage[k].sequence < slot->sequence))
					slot = &stage[k];
			if (!slot)
				break;
			u = &uploads[slot->upload];
			if (u->dead || !psp_sound_cache_alive(u->datum, u->identifier, u->sound, (unsigned long)u->dest))
			{
				u->dead = 1;
				piece_done(slot, 0);
				continue;
			}
			bytes = (slot->bytes + 15) & ~15ul;
			sceKernelDcacheWritebackInvalidateRange(slot->buffer, (bytes + 63) & ~63ul);
			piece_done(slot, tier_copy(slot->buffer, u->dest + slot->offset, bytes));
		}
	}
	/* loads with nothing left to do: the engine told */
	for (k = 0; k < upload_count; k++)
	{
		struct upload *u = &uploads[(upload_head + k) % UPLOADS_MAX];

		if (u->finished || u->outstanding)
			continue;
		if (edram_lost && !u->dead)
			u->failed = 1;
		if (!u->dead && !u->failed && u->copied < u->size)
			continue;
		u->finished = 1;
		if (u->dead || !psp_sound_cache_alive(u->datum, u->identifier, u->sound, (unsigned long)u->dest))
		{
			up_dropped++;
			continue;
		}
		*u->loaded = u->failed ? READ_FAILED : 1;
		/* a tier sound that didn't load: its block given up, so the sound is
		asked for and read again (into the main tier once the tier is
		closed), rather than played from what the block held */
		if (u->failed && u->block_index >= 0 && psp_sound_cache_drop(u->block_index))
			tier_given_up++;
		if (!u->failed)
		{
			unsigned long us = (unsigned long)(sceKernelGetSystemTimeWide() - u->start_us);

			up_sounds++;
			up_bytes += u->size;
			up_wait_us += us;
			if (us > up_worst_us)
				up_worst_us = us;
		}
	}
	while (upload_count && uploads[upload_head].finished)
	{
		upload_head = (upload_head + 1) % UPLOADS_MAX;
		upload_count--;
	}
	if (edram_lost)
		return;
	/* new reads, in the loads' order */
	for (k = 0; k < STAGE_SLOTS; k++)
	{
		struct stage_slot *slot = &stage[k];
		unsigned int n;
		struct upload *u = NULL;

		if (slot->state != SLOT_FREE)
			continue;
		for (n = 0; n < upload_count && !u; n++)
		{
			struct upload *candidate = &uploads[(upload_head + n) % UPLOADS_MAX];

			if (!candidate->finished && !candidate->dead && !candidate->failed && candidate->issued < candidate->size)
				u = candidate;
		}
		if (!u)
			break;
		slot->upload = (int)(u - uploads);
		slot->offset = u->issued;
		slot->bytes = u->size - u->issued < STAGE_PIECE_BYTES ? u->size - u->issued : STAGE_PIECE_BYTES;
		slot->sequence = next_sequence++;
		slot->state = SLOT_READING;
		slot->done = 0;
		u->issued += slot->bytes;
		u->outstanding++;
		up_pieces++;
		cache_file_read(u->tag_index, u->file_offset + slot->offset, slot->bytes, slot->buffer, (unsigned char *)&slot->done, 0);
	}
	/* what's read, to the ME in the order it was asked for: one job */
	if ((tier_mode <= 0 || !tier_dma) && !copy_out)
	{
		unsigned int args[ME_JOB_ARGS] = { 0 }, n = 0, order[STAGE_SLOTS], m = 0, j, hash = PSP_ME_HASH_START, bytes_out = 0;

		for (k = 0; k < STAGE_SLOTS; k++)
			if (stage[k].state == SLOT_READING && stage[k].done == 1)
			{
				for (j = m++; j > 0 && stage[order[j - 1]].sequence > stage[k].sequence; j--)
					order[j] = order[j - 1];
				order[j] = k;
			}
		for (j = 0; j < m && 3 * n + 2 < ME_JOB_ARGS; j++)
		{
			struct stage_slot *slot = &stage[order[j]];
			struct upload *u = &uploads[slot->upload];
			unsigned long bytes = (slot->bytes + 15) & ~15ul;

			/* a sound deleted while it loaded: its block may be another's now */
			if (u->dead || !psp_sound_cache_alive(u->datum, u->identifier, u->sound, (unsigned long)u->dest))
			{
				u->dead = 1;
				piece_done(slot, 0);
				continue;
			}
			/* the check's hash from what the ME will read: RAM, once written back */
			if (tier_mode > 0 && tier_check)
			{
				const unsigned int *word = (const unsigned int *)slot->buffer;
				unsigned long w;

				sceKernelDcacheWritebackInvalidateRange(slot->buffer, (bytes + 63) & ~63ul);
				for (w = 0; w < bytes / 4; w++)
					hash = psp_me_hash(hash, word[w]);
			}
			sceKernelDcacheWritebackInvalidateRange(slot->buffer, (bytes + 63) & ~63ul);
			args[3 * n] = (unsigned int)slot->buffer;
			args[3 * n + 1] = (unsigned int)(u->dest + slot->offset);
			args[3 * n + 2] = (unsigned int)bytes | (tier_mode > 0 && tier_check ? 1u : 0u);
			bytes_out += (unsigned int)bytes;
			slot->state = SLOT_COPYING;
			n++;
		}
		if (n)
		{
			copy_at = sceKernelGetSystemTimeWide();
			copy_bytes = bytes_out;
			copy_hash = hash;
			copy_checked = tier_mode > 0 && tier_check;
			copy_late = 0;
			copy_begun_at = 0;
			if (psp_me_submit(ME_SLOT_PROBE, ME_JOB_SOUNDCOPY, args, ME_JOB_ARGS))
			{
				copy_out = 1;
				up_jobs++;
				tier_jobs += tier_mode > 0;
				tier_job_pieces += tier_mode > 0 ? n : 0;
			}
			else
			{
				for (k = 0; k < STAGE_SLOTS; k++)
					if (stage[k].state == SLOT_COPYING)
						stage[k].state = SLOT_READING;
				if (!psp_me_running())
					edram_loss("the Media Engine isn't running");
			}
		}
	}
}

/* psp_sound_cache_report's line (every 5 s) */
void psp_sound_upload_report(void)
{
	if (tier_mode > 0 && tier_dma)
		platform_log("sound tier: %lu sounds in (%lu KB, %lu pieces), each %lu ms from asked to loaded on average (worst %lu); %lu DMAs, %lu us in them (worst %lu), %lu failed, %lu read back wrong%s; %lu reads failed, %lu deleted while loading, %lu refused (queue full), %lu given up; %lu blocks with voices mixed silent; %s%s",
			up_sounds, up_bytes / 1024, up_pieces, up_sounds ? up_wait_us / up_sounds / 1000 : 0, up_worst_us / 1000,
			tier_dmas, tier_dma_us, tier_dma_worst_us, tier_dma_failed, tier_check_bad, tier_check ? "" : " (unchecked)",
			up_failed, up_dropped, up_full, tier_given_up + psp_sound_tier_drained, muted_blocks,
			tier_closed_why ? "CLOSED: " : "open", tier_closed_why ? tier_closed_why : "");
	else if (tier_mode > 0)
		platform_log("sound tier: %lu sounds in (%lu KB, %lu pieces), each %lu ms from asked to loaded on average (worst %lu); %lu copy jobs (%lu pieces), %lu us from handed over to collected on average (worst %lu), %lu failed, %lu late, %lu hashed wrong%s; %lu reads failed, %lu deleted while loading, %lu refused (queue full), %lu given up; %lu blocks with voices mixed silent; %s%s",
			up_sounds, up_bytes / 1024, up_pieces, up_sounds ? up_wait_us / up_sounds / 1000 : 0, up_worst_us / 1000,
			tier_jobs, tier_job_pieces, tier_jobs ? tier_job_us / tier_jobs : 0, tier_job_worst_us, tier_job_failed,
			tier_job_late, tier_check_bad, tier_check ? "" : " (unchecked)",
			up_failed, up_dropped, up_full, tier_given_up + psp_sound_tier_drained, muted_blocks,
			tier_closed_why ? "CLOSED: " : "open", tier_closed_why ? tier_closed_why : "");
	if (tier_mode > 0)
		platform_log("sound tier: copy jobs on the ME %lu us each on average (worst %lu); seen started at worst %lu ms after the hand-over; %lu late of them not started",
			tier_jobs ? tier_job_me_us / tier_jobs : 0, tier_job_me_worst_us, tier_job_begun_worst_us / 1000,
			tier_job_unbegun);
	if (edram_mode <= 0)
		return;
	platform_log("sound eDRAM: %lu sounds in (%lu KB, %lu pieces, %lu copy jobs), each %lu ms from asked to loaded on average (worst %lu); %lu reads failed, %lu deleted while loading, %lu refused (queue full), %lu after the loss; %lu blocks with voices mixed silent%s",
		up_sounds, up_bytes / 1024, up_pieces, up_jobs, up_sounds ? up_wait_us / up_sounds / 1000 : 0, up_worst_us / 1000,
		up_failed, up_dropped, up_full, up_after_loss, muted_blocks, edram_lost ? "; LOST" : "");
}

/* voices playing from the cache, mixed silent where it can't be read: by the
main CPU, or by an ME that has stopped since (its eDRAM taken as lost). The
silent path of mix_voice only moves the position on */
static int edram_unreachable(int on_me)
{
	return edram_hi && (!on_me || psp_me_stops != stops_at_start);
}

static void mute_cache_voices(struct psp_stream *const *list, unsigned long count)
{
	unsigned long k, position;
	int any = 0;

	for (k = 0; k < count; k++)
	{
		struct psp_stream *stream = list[k];

		for (position = 0; position < stream->packet_count && position <= stream->packet_mask; position++)
		{
			const struct voice_packet *packet = &stream->packets[(stream->packet_head + position) & stream->packet_mask];

			if (!packet->finished && tier_back_pending_count && tier_back_waiting(packet->data))
			{
				stream->target_left = stream->target_right = 0;
				stream->current_left = stream->current_right = 0;
				tier_back_gap_blocks++;
				break;
			}
			if (!packet->finished && (tier_mode > 0 && part_bytes ? psp_sound_tier_holds(packet->data) :
				packet->data >= edram_lo && packet->data < edram_hi))
			{
				/* the rescue's check: a voice it should have moved */
				if (tier_back_counts[0] && !tier_back_missed++)
					platform_log("sound tier: after the rescue a voice still plays from the tier: its packet %u of %lu at %u KB into the tier, %lu frames, %s, %lu channels, %lu Hz",
						(unsigned int)position, stream->packet_count, (unsigned int)((packet->data - edram_lo) / 1024), packet->frames,
						stream->adpcm ? "ADPCM" : "PCM", stream->channels, (unsigned long)stream->sample_rate);
				stream->target_left = stream->target_right = 0;
				stream->current_left = stream->current_right = 0;
				any = 1;
				break;
			}
		}
	}
	muted_blocks += any;
}

#define VOICE_LIST_MAX 256
int psp_meaudio_start(void);
void psp_me_handover(int which, unsigned long us);
int psp_meaudio_running(void);
int psp_meaudio_run(void *const *list, unsigned long count, short *output, unsigned long frames, unsigned long *voices,
	unsigned long *clipped, unsigned long *waited_us);
/* build 86: the mixer wanted on the ME (the debug menu's "Audio mixer"; on
from the start with the launch option meaudio) */
static int me_wanted = 1;
static unsigned long me_blocks = 0, me_wait_us = 0, me_wait_worst_us = 0, me_failures = 0, me_overflows = 0;
/* the hand-over by ranges: -1 not decided yet; blocks handed over that way */
static int ranges_on = -1;
static unsigned long ranges_blocks = 0;

/* the debug menu (psp_debug.c): 0 the main CPU mixes, 1 the Media Engine;
it reads 0 when the ME isn't running */
int psp_audio_setting(int set, int value)
{
	if (set)
	{
		me_wanted = value != 0;
		if (me_wanted && !psp_meaudio_running())
			platform_log("audio: the Media Engine isn't running (launch option meaudio; never under the emulator's own ME)");
	}
	return me_wanted && psp_meaudio_running();
}

#if PSP_MUSIC
/* the music voices of a block (psp_music.c): who, which chain, the frames
at 44.1 kHz and the gains */
#define MUSIC_MARKS 4
int psp_music_prepare(void);
void psp_music_queue(const void *const *owners, const int *chains, const unsigned long *from, const unsigned long *to,
	const int *left, const int *right, int count);
void psp_music_after_block(int marked);
void psp_me_audio_music(int on);
static const void *music_owner[MUSIC_MARKS];
static int music_chain[MUSIC_MARKS], music_left[MUSIC_MARKS], music_right[MUSIC_MARKS];
static unsigned long music_from[MUSIC_MARKS], music_scale_of[MUSIC_MARKS], music_marks;
/* written by the job (the ME's, often): a line of its own */
static unsigned long music_to[64 / sizeof(unsigned long)] __attribute__((aligned(64)));

/* the block's music, after its mix, in the mixer's job (either CPU): where
each marked voice's frames end, from where the mix left it (on to the next
packet of the same chain, or as far as the step went), into the block's
set for the music job (psp_music.c), which decodes it in the ME's idle time */
void psp_dsound_music_job(unsigned long frames)
{
	unsigned long k;

	for (k = 0; k < music_marks; k++)
	{
		struct psp_stream *voice = (struct psp_stream *)music_owner[k];
		struct voice_packet *head = current_packet(voice);

		music_to[k] = head && head->music - 1 == music_chain[k] ? head->music_start + voice->frame * head->music_scale :
			music_from[k] + ((voice->step * frames) >> 16) * music_scale_of[k];
	}
	psp_music_queue(music_owner, music_chain, music_from, music_to, music_left, music_right, (int)music_marks);
}
#endif

/* "memixcheck": each block mixed by mix_voice's old loop and by MEMIX from
the same voices, the outputs and the voices after compared; the block plays
MEMIX's. On the main CPU only (the check keeps the block off the ME), and
only for blocks of up to CHECK_VOICES voices */
#define CHECK_VOICES 64
static int memix_check = 0;
/* the voices as they were before the block, so the second mix starts from
the same states (CHECK_VOICES * STREAM_BYTES of heap). The states after the
first mix are kept only as digests: a second set of copies cost c20 another
110 KB of heap, and with both sets the level copies' arena found no room and
the run ended at a fatal error short of its stretch */
static unsigned char *check_copies;
static unsigned long long check_after[CHECK_VOICES];
static short check_output[MIX_FRAMES * OUTPUT_CHANNELS];
static unsigned long check_blocks = 0, check_skipped = 0, check_output_differed = 0, check_state_differed = 0,
	check_first_differed = 0;
/* which of MEMIX's paths the checked voice blocks took: a check that never
reached a path says nothing about it */
static unsigned long check_mono = 0, check_stereo = 0, check_pcm = 0, check_filtered = 0, check_stepped = 0,
	check_stepped_adpcm = 0;
/* under "mixkinds", the voice blocks that start in each of H1's kinds (plain,
plain filtered, pitched, pitched filtered): the same reason */
static unsigned long check_kind[4];

/* "audiocheck": each block mixed without the approximate paths and with
them, from the same voices; the block plays the approximate one, and the
next block's two mixes both start from its voices. So each block's error is
measured alone, from one run, sample-aligned: no capture to line up, and no
timing between runs to tell apart. Per report: how far below the mix the
difference lies (all, and in three bands: under ~300 Hz, ~300 Hz-4 kHz, over
~4 kHz, by one-pole filters on both signals), its peak, the worst block, and
how often the voices' states after differed. With no approximate path on,
every block must come out identical: that checks the check. On the main CPU
only, and only blocks of up to CHECK_VOICES voices. The 44.1 kHz work (the
music, the half-band filter) isn't in it */
static int audio_check = 0;
static short check_alt[MIX_FRAMES * OUTPUT_CHANNELS];
struct check_band { long long ref, diff; };
static struct check_band check_bands[4];
static long long check_worst_ref = 1, check_worst_diff = 0;
static unsigned long check_peak = 0, check_identical = 0, check_report_blocks = 0, check_approx_states = 0,
	check_approx_skipped = 0;
/* the filters' states, per channel, for the reference and the difference:
two one-pole stages a corner, 12 dB an octave, so a tone lands in its band */
static int check_low[2][OUTPUT_CHANNELS][2], check_mid[2][OUTPUT_CHANNELS][2];

static void check_energy(const short *ref, const short *alt, unsigned long frames)
{
	/* a = 1 - exp(-2 pi f / 22,050), Q16: 300 Hz and 4 kHz a stage */
	enum { LOW_A = 5370, MID_A = 44560 };
	long long block_ref = 0, block_diff = 0;
	unsigned long i, c;

	for (i = 0; i < frames; i++)
		for (c = 0; c < OUTPUT_CHANNELS; c++)
		{
			int x[2], which;

			x[0] = ref[i * OUTPUT_CHANNELS + c];
			x[1] = alt[i * OUTPUT_CHANNELS + c] - x[0];
			for (which = 0; which < 2; which++)
			{
				int part[4], b;

				int *low = check_low[which][c], *mid = check_mid[which][c];

				low[0] += (int)(((long long)(x[which] - low[0]) * LOW_A) >> 16);
				low[1] += (int)(((long long)(low[0] - low[1]) * LOW_A) >> 16);
				mid[0] += (int)(((long long)(x[which] - mid[0]) * MID_A) >> 16);
				mid[1] += (int)(((long long)(mid[0] - mid[1]) * MID_A) >> 16);
				part[0] = x[which];
				part[1] = low[1];
				part[2] = mid[1] - low[1];
				part[3] = x[which] - mid[1];
				for (b = 0; b < 4; b++)
				{
					if (which)
						check_bands[b].diff += (long long)part[b] * part[b];
					else
						check_bands[b].ref += (long long)part[b] * part[b];
				}
			}
			block_ref += (long long)x[0] * x[0];
			block_diff += (long long)x[1] * x[1];
			if ((unsigned long)abs(x[1]) > check_peak)
				check_peak = (unsigned long)abs(x[1]);
		}
	check_identical += block_diff == 0;
	/* the worst block: the largest difference against its own mix */
	if (block_diff * check_worst_ref > check_worst_diff * (block_ref ? block_ref : 1))
	{
		check_worst_diff = block_diff;
		check_worst_ref = block_ref ? block_ref : 1;
	}
	check_report_blocks++;
}

/* a voice's state as two independent 32-bit sums of its words (FNV-1a, and
a rotating sum weighted by position), so a difference the checks would have
seen in a byte compare is missed only if both collide. game_offset is whole
64-byte lines and the voice is line-aligned, so it reads whole words */
static unsigned long long check_digest(const struct psp_stream *voice)
{
	const unsigned int *word = (const unsigned int *)(const void *)voice;
	unsigned long n = voice->game_offset / sizeof(unsigned int), i;
	unsigned int a = 2166136261u, b = 0;

	for (i = 0; i < n; i++)
	{
		a = (a ^ word[i]) * 16777619u;
		b = ((b << 5) | (b >> 27)) + word[i] + (unsigned int)i;
	}
	return ((unsigned long long)a << 32) | b;
}

static unsigned long mix_approx_checked(struct psp_stream **list, unsigned long count, int *sums, short *output,
	unsigned long frames, unsigned long *clipped)
{
	unsigned char *before = check_copies;
	unsigned long k, voices, clipped_ref, states = 0;
	unsigned int approx[CHECK_VOICES];

	if (count > CHECK_VOICES)
	{
		check_approx_skipped++;
		return psp_dsound_mix_list((void *const *)list, count, sums, output, frames, clipped);
	}
	for (k = 0; k < count; k++)
	{
		memcpy(before + k * STREAM_BYTES, list[k], list[k]->game_offset);
		approx[k] = list[k]->approx;
		list[k]->approx = 0;
	}
	psp_dsound_mix_list((void *const *)list, count, sums, output, frames, &clipped_ref);
	for (k = 0; k < count; k++)
	{
		/* the second mix runs with the approximate paths back on: the field
		they set isn't a difference the mixes made */
		list[k]->approx = approx[k];
		check_after[k] = check_digest(list[k]);
		memcpy(list[k], before + k * STREAM_BYTES, list[k]->game_offset);
	}
	voices = psp_dsound_mix_list((void *const *)list, count, sums, check_alt, frames, clipped);
	if (audio_approx & AUDIO_APPROX_CAL)
	{
		static unsigned long phase = 0;
		unsigned long i, c;

		for (i = 0; i < frames; i++, phase++)
		{
			int tone = (int)(33.0f * sinf(6.2831853f * 1000.0f * (float)(phase % OUTPUT_RATE) / (float)OUTPUT_RATE));

			for (c = 0; c < OUTPUT_CHANNELS; c++)
			{
				int v = check_alt[i * OUTPUT_CHANNELS + c] + tone;

				check_alt[i * OUTPUT_CHANNELS + c] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
			}
		}
	}
	for (k = 0; k < count; k++)
		if (check_after[k] != check_digest(list[k]))
			states++;
	check_approx_states += states != 0;
	check_energy(output, check_alt, frames);
	memcpy(output, check_alt, frames * OUTPUT_CHANNELS * sizeof(short));
	return voices;
}

static float check_db(long long signal, long long noise)
{
	if (!noise)
		return 999.0f;
	return 10.0f * log10f((float)(signal ? signal : 1) / (float)noise);
}

static void audio_check_report(void)
{
	static const char *const names[4] = { "all", "low", "mid", "high" };
	char bands[160];
	unsigned long b, used = 0;
	long long full;

	if (!audio_check || !check_report_blocks)
		return;
	for (b = 1; b < 4; b++)
		used += (unsigned long)snprintf(bands + used, sizeof(bands) - used, "%s%s %.1f", b > 1 ? ", " : "", names[b],
			(double)check_db(check_bands[b].ref, check_bands[b].diff));
	/* against the mix, and against full scale: in a quiet stretch a tiny
	error is close to the mix and still far under hearing */
	full = (long long)check_report_blocks * MIX_FRAMES * OUTPUT_CHANNELS * 32767LL * 32767LL;
	platform_log("audio: audiocheck (approximate paths 0x%x): %lu blocks, %lu identical, %lu over %d voices not checked; the difference %.1f dB below the mix (bands: %s; 999: none) and %.1f dB below full scale (the mix %.1f), its peak %lu of 32,767, the worst block %.1f dB below its own mix; the voices' states after differed in %lu",
		audio_approx, check_report_blocks, check_identical, check_approx_skipped, CHECK_VOICES,
		(double)check_db(check_bands[0].ref, check_bands[0].diff), bands,
		(double)check_db(full, check_bands[0].diff), (double)check_db(full, check_bands[0].ref), check_peak,
		(double)check_db(check_worst_ref, check_worst_diff), check_approx_states);
	memset(check_bands, 0, sizeof(check_bands));
	check_worst_ref = 1;
	check_worst_diff = 0;
	check_peak = check_identical = check_report_blocks = check_approx_states = check_approx_skipped = 0;
}

static unsigned long mix_checked(struct psp_stream **list, unsigned long count, int *sums, short *output,
	unsigned long frames, unsigned long *clipped)
{
	unsigned char *before = check_copies;
	unsigned long k, voices, voices_old, clipped_old, states = 0, differed;

	if (count > CHECK_VOICES)
	{
		check_skipped++;
		return psp_dsound_mix_list((void *const *)list, count, sums, output, frames, clipped);
	}
	for (k = 0; k < count; k++)
	{
		const struct psp_stream *voice = list[k];

		memcpy(before + k * STREAM_BYTES, voice, voice->game_offset);
		list[k]->memix_old = 1;
		if (voice->channels == 1 && voice->lowpass_left == voice->lowpass_right)
			check_mono++;
		else
			check_stereo++;
		check_pcm += !voice->adpcm;
		check_filtered += voice->lowpass_coefficient < GAIN_ONE;
		/* a pitch other than the output's interpolates; in ADPCM it reads
		across each 64-frame block's end into the other buffer */
		check_stepped += voice->step != 0x10000;
		check_stepped_adpcm += voice->step != 0x10000 && voice->adpcm;
		if (voice->mix_kinds && voice->channels == 1 && voice->lowpass_left == voice->lowpass_right && voice->adpcm &&
			voice->step)
			check_kind[(voice->step == 0x10000 && !voice->fraction ? 0 : 2) + (voice->lowpass_coefficient < GAIN_ONE)]++;
	}
	voices_old = psp_dsound_mix_list((void *const *)list, count, sums, check_output, frames, &clipped_old);
	for (k = 0; k < count; k++)
	{
		/* the old loop's own switch isn't a difference the mixes made */
		list[k]->memix_old = 0;
		check_after[k] = check_digest(list[k]);
		memcpy(list[k], before + k * STREAM_BYTES, list[k]->game_offset);
		list[k]->memix_old = 0;
	}
	voices = psp_dsound_mix_list((void *const *)list, count, sums, output, frames, clipped);
	for (k = 0; k < count; k++)
		if (check_after[k] != check_digest(list[k]))
			states++;
	check_blocks++;
	check_state_differed += states != 0;
	differed = voices != voices_old || *clipped != clipped_old ||
		memcmp(output, check_output, frames * OUTPUT_CHANNELS * sizeof(short)) != 0;
	check_output_differed += differed;
	if ((states || differed) && !check_first_differed)
		check_first_differed = check_blocks;
	return voices;
}

/* MUSADPCM ("musadpcm", on by default; "musadpcmoff" halves the music
again): a stereo 44.1 kHz ADPCM voice (the disc's music, in a map baked with
--music-adpcm) mixed at 44,100 Hz into a
block of its own, added to the rest of the mix once that is taken to 44.1 kHz
(audio44's filter), instead of being read at every other frame into the
22,050 Hz mix with nothing to take out what lies above 11 kHz. Half the step
over twice the frames moves the voice exactly as far: the step must be even,
and there's no muffling filter (its coefficient is set for 22,050 Hz; music
has none). On the main CPU for now, after the ME's block; where it runs is
for a PSP pair to settle. Bakes that halve all sound have no such voices */
/* "audio44" (opt-in, for a listening A/B on the PSP): the mix taken from
22,050 to 44,100 Hz here and played on an ordinary 44.1 kHz channel, instead
of the hardware's sample-rate converter, whose filter isn't documented. The
fizzy, shimmering treble heard on the PSP is what the images of a converter that
holds or interpolates linearly sound like [U]: a 7 kHz tone mixed at 22,050
leaves an image at 15 kHz unless a filter takes it out. Here a half-band
filter (12 taps, a Blackman-windowed sinc) does: flat to 6.6 kHz, -2.6 dB at
9.9 kHz (where the bake's own half-rate filter already cuts), images 20 dB
down at 13 kHz and 54 dB at 15 kHz. The even outputs are the mix's own
samples, delayed UP_TAPS; the odd ones the filter's half-way points */
#define UP_TAPS 6
static const int up_taps[UP_TAPS] = { 20276, -5378, 2003, -659, 154, -12 }; /* Q15; 2 x sum = 1 */
static short up_history[2 * UP_TAPS * OUTPUT_CHANNELS] __attribute__((aligned(64)));
static short up_input[(2 * UP_TAPS + MIX_FRAMES) * OUTPUT_CHANNELS] __attribute__((aligned(64)));
static short output44[2][2 * MIX_FRAMES * OUTPUT_CHANNELS] __attribute__((aligned(64)));
/* MUSADPCM's full-rate block, added to the upsampled mix */
static short output_full[2][2 * MIX_FRAMES * OUTPUT_CHANNELS] __attribute__((aligned(64)));

static void add_full(short *out, const short *full)
{
	int k;

	for (k = 0; k < 2 * MIX_FRAMES * OUTPUT_CHANNELS; k++)
	{
		int value = out[k] + full[k];

		out[k] = (short)(value > 32767 ? 32767 : value < -32768 ? -32768 : value);
	}
}

static void upsample(const short *in, short *out)
{
	int j, c, k;

	memcpy(up_input, up_history, sizeof(up_history));
	memcpy(up_input + 2 * UP_TAPS * OUTPUT_CHANNELS, in, MIX_FRAMES * OUTPUT_CHANNELS * sizeof(short));
	for (j = 0; j < MIX_FRAMES; j++)
		for (c = 0; c < OUTPUT_CHANNELS; c++)
		{
			const short *e = up_input + (j + UP_TAPS) * OUTPUT_CHANNELS + c;
			int sum = 0, value;

			for (k = 0; k < UP_TAPS; k++)
				sum += up_taps[k] * (e[-k * OUTPUT_CHANNELS] + e[(k + 1) * OUTPUT_CHANNELS]);
			value = (sum + 16384) >> 15;
			out[2 * j * OUTPUT_CHANNELS + c] = e[0];
			out[(2 * j + 1) * OUTPUT_CHANNELS + c] = (short)(value > 32767 ? 32767 : value < -32768 ? -32768 : value);
		}
	memcpy(up_history, up_input + MIX_FRAMES * OUTPUT_CHANNELS, sizeof(up_history));
}

/* UPFUSE: the full-rate block added in the filter's own loop, with the same
two clamps in the same order (the filtered sample's, then the sum's), so the
same samples as upsample then add_full, in one pass over the 44.1 kHz block
instead of two. "upfuseold" keeps the two passes */
static void upsample_full(const short *in, short *out, const short *full)
{
	int j, c, k;

	if (!full || *(volatile int *)((unsigned int)&up_fuse_old | 0x40000000u))
	{
		upsample(in, out);
		if (full)
			add_full(out, full);
		return;
	}
	memcpy(up_input, up_history, sizeof(up_history));
	memcpy(up_input + 2 * UP_TAPS * OUTPUT_CHANNELS, in, MIX_FRAMES * OUTPUT_CHANNELS * sizeof(short));
	for (j = 0; j < MIX_FRAMES; j++)
		for (c = 0; c < OUTPUT_CHANNELS; c++)
		{
			const short *e = up_input + (j + UP_TAPS) * OUTPUT_CHANNELS + c;
			int even = 2 * j * OUTPUT_CHANNELS + c, odd = (2 * j + 1) * OUTPUT_CHANNELS + c;
			int sum = 0, value;

			for (k = 0; k < UP_TAPS; k++)
				sum += up_taps[k] * (e[-k * OUTPUT_CHANNELS] + e[(k + 1) * OUTPUT_CHANNELS]);
			value = (sum + 16384) >> 15;
			value = value > 32767 ? 32767 : value < -32768 ? -32768 : value;
			value += full[odd];
			out[odd] = (short)(value > 32767 ? 32767 : value < -32768 ? -32768 : value);
			value = e[0] + full[even];
			out[even] = (short)(value > 32767 ? 32767 : value < -32768 ? -32768 : value);
		}
	memcpy(up_history, up_input + MIX_FRAMES * OUTPUT_CHANNELS, sizeof(up_history));
}

static unsigned long full_blocks = 0, full_voice_blocks = 0, full_heard = 0, full_on_me_blocks = 0;

static int full_rate_voice(const struct psp_stream *stream)
{
	return stream->adpcm && stream->channels == 2 && stream->sample_rate == 44100 && !(stream->step & 1) &&
		stream->lowpass_coefficient >= GAIN_ONE;
}

static unsigned long mix_full_rate(struct psp_stream **list, unsigned long count, short *full, unsigned long frames,
	unsigned long *clipped)
{
	static int sums[2 * MIX_FRAMES * OUTPUT_CHANNELS] __attribute__((aligned(64)));
	static unsigned long mixed[VOICE_LIST_MAX], dry[VOICE_LIST_MAX];
	unsigned long k, over = 0, heard;

	if (!count)
	{
		for (k = 0; k < 2 * frames * OUTPUT_CHANNELS; k++)
			full[k] = 0;
		return 0;
	}
	for (k = 0; k < count; k++)
	{
		mixed[k] = list[k]->mixed_frames;
		dry[k] = list[k]->dry_frames;
		list[k]->step >>= 1;
	}
	heard = psp_dsound_mix_list((void *const *)list, count, sums, full, 2 * frames, &over);
	/* the step back, and the voice's counts kept in 22,050 Hz frames, as
	the reports read them */
	for (k = 0; k < count; k++)
	{
		list[k]->step <<= 1;
		list[k]->mixed_frames = mixed[k] + (list[k]->mixed_frames - mixed[k]) / 2;
		list[k]->dry_frames = dry[k] + (list[k]->dry_frames - dry[k]) / 2;
	}
	*clipped |= over;
	return heard;
}

/* the 44.1 kHz block whole (keep "audio44"; with "musadpcm" the full-rate
voices too): on the ME's audio job after its mix (psp_me.c), or on this CPU
when the ME doesn't take it ("audio44main", or the ME stopped). full NULL:
the filter alone */
void psp_dsound_full_job(void *const *list, unsigned long count, unsigned long frames, short *output22, short *out44,
	short *full, unsigned int *heard, unsigned int *clipped)
{
	unsigned long over = 0, h = 0;

	if (full)
		h = mix_full_rate((struct psp_stream **)list, count, full, frames, &over);
	upsample_full(output22, out44, full);
	*heard = (unsigned int)h;
	*clipped = (unsigned int)over;
}

/* full (NULL unless "musadpcm"): the full-rate voices' block, 2 x frames;
out44 (NULL unless the output is 44.1 kHz): the block to play. 1 when the
ME's job made out44 whole (the filter and the full-rate voices); 0 leaves
them to the caller */
static int full_on_me = -1; /* "audio44main" keeps the 44.1 kHz work on this CPU */

void psp_me_audio_full(void *const *list, unsigned long count, short *out44, short *full);
int psp_me_audio_full_done(unsigned long *heard);

#ifdef PSP_RECON
/* "voicecensus" (PSP_RECON): what the lean sound levers would cut (audio_lean
L1-L4), counted on the main CPU from each block's prepared list before it is
mixed. It only reads the records, so play and sound don't change. A heard
voice is counted at the block's frames, its decode at the source frames its
step covers; its gain is the larger of where the ramp starts and ends, on the
mixer's own scale (GAIN_ONE full scale, after distance, cone, the direct path
and the master volume). Distance is the game's offset to the listener, before
DirectSound's distance factor. Every line is a running total */
#define CENSUS_GAINS 5
#define CENSUS_BANDS 7
#define CENSUS_HIST 41
static const int census_gain_at[CENSUS_GAINS] = { 16, 32, 64, 128, 256 };
static const float census_band_at[CENSUS_BANDS - 2] = { 1.0f, 2.0f, 5.0f, 15.0f, 40.0f };
static const unsigned long census_cap_at[3] = { 12, 16, 20 };
static int census_on = -1;
static unsigned long census_blocks, census_listed, census_silent, census_music, census_heard, census_vf, census_dec;
static unsigned long census_stereo, census_pcm;
static unsigned long census_gain_n[CENSUS_GAINS], census_gain_vf[CENSUS_GAINS], census_gain_dec[CENSUS_GAINS];
static unsigned long census_band_vf[CENSUS_BANDS], census_band_dec[CENSUS_BANDS], census_band_quiet[CENSUS_BANDS];
static unsigned long census_kind_vf[5], census_kind_dec[5];
static unsigned long census_hist[CENSUS_HIST];
static unsigned long census_cap_blocks[3], census_cap_vf[3], census_cap_loud[3];

static void voice_census(struct psp_stream *const *list, unsigned long count, unsigned long frames)
{
	static int gains[VOICE_LIST_MAX];
	unsigned long k, heard = 0;

	if (census_on < 0)
	{
		extern int psp_keep(const char *word);

		census_on = psp_keep("voicecensus") != 0;
		if (census_on)
			platform_log("voicecensus: on; gains on the scale %d = full; thresholds 16 32 64 128 256; bands <1 <2 <5 <15 <40 >=40 2d (game units); caps 12 16 20",
				GAIN_ONE);
	}
	if (!census_on)
		return;
	census_blocks++;
	census_listed += count;
	for (k = 0; k < count; k++)
	{
		const struct psp_stream *stream = list[k];
		const struct stream_game *game = GAME_OF(stream);
		int from_left = stream->gains_valid ? stream->current_left : stream->target_left;
		int from_right = stream->gains_valid ? stream->current_right : stream->target_right;
		int gain = stream->target_left;
		unsigned long dec = 0, g, band, kind;

		if (MUSIC_BLOCK(stream))
		{
			census_music++;
			continue;
		}
		if (stream->target_right > gain)
			gain = stream->target_right;
		if (from_left > gain)
			gain = from_left;
		if (from_right > gain)
			gain = from_right;
		if (!gain)
		{
			census_silent++;
			continue;
		}
		gains[heard++] = gain;
		if (stream->adpcm)
			dec = (unsigned long)(((unsigned long long)stream->step * frames + stream->fraction) >> 16);
		census_heard++;
		census_vf += frames;
		census_dec += dec;
		census_stereo += stream->channels != 1;
		census_pcm += !stream->adpcm;
		for (g = 0; g < CENSUS_GAINS; g++)
			if (gain < census_gain_at[g])
			{
				census_gain_n[g]++;
				census_gain_vf[g] += frames;
				census_gain_dec[g] += dec;
			}
		band = CENSUS_BANDS - 1;
		if (game->has_3d && game->mode != DS3DMODE_DISABLE)
		{
			float offset[3], distance;
			int axis;

			for (axis = 0; axis < 3; axis++)
				offset[axis] = game->mode == DS3DMODE_HEADRELATIVE ? game->position[axis] :
					game->position[axis] - listener.position[axis];
			distance = sqrtf(dot3(offset, offset));
			for (band = 0; band < CENSUS_BANDS - 2 && distance >= census_band_at[band]; band++)
				;
		}
		census_band_vf[band] += frames;
		census_band_dec[band] += dec;
		census_band_quiet[band] += gain < 64;
		if (stream->mix_kinds && stream->channels == 1 && stream->lowpass_left == stream->lowpass_right && stream->adpcm &&
			stream->step)
			kind = (stream->step == 0x10000 && !stream->fraction ? 0 : 2) + (stream->lowpass_coefficient < GAIN_ONE);
		else
			kind = 4;
		census_kind_vf[kind] += frames;
		census_kind_dec[kind] += dec;
	}
	census_hist[heard < CENSUS_HIST ? heard : CENSUS_HIST - 1]++;
	/* the loudest first, for the caps */
	for (k = 1; k < heard; k++)
	{
		int value = gains[k];
		unsigned long j = k;

		for (; j > 0 && gains[j - 1] < value; j--)
			gains[j] = gains[j - 1];
		gains[j] = value;
	}
	for (k = 0; k < 3; k++)
		if (heard > census_cap_at[k])
		{
			unsigned long j;

			census_cap_blocks[k]++;
			census_cap_vf[k] += (heard - census_cap_at[k]) * frames;
			for (j = census_cap_at[k]; j < heard; j++)
				census_cap_loud[k] += gains[j] >= 256;
		}
	if (!(census_blocks & 255))
	{
		char hist[CENSUS_HIST * 12], *at = hist;

		platform_log("voicecensus: blocks %lu frames %lu listed %lu music %lu silent %lu heard %lu vf %lu dec %lu stereo %lu pcm %lu",
			census_blocks, frames, census_listed, census_music, census_silent, census_heard, census_vf, census_dec, census_stereo,
			census_pcm);
		platform_log("voicecensus gain: n/vf/dec <16 %lu %lu %lu <32 %lu %lu %lu <64 %lu %lu %lu <128 %lu %lu %lu <256 %lu %lu %lu",
			census_gain_n[0], census_gain_vf[0], census_gain_dec[0], census_gain_n[1], census_gain_vf[1], census_gain_dec[1],
			census_gain_n[2], census_gain_vf[2], census_gain_dec[2], census_gain_n[3], census_gain_vf[3], census_gain_dec[3],
			census_gain_n[4], census_gain_vf[4], census_gain_dec[4]);
		platform_log("voicecensus band: vf/dec/quiet64 <1 %lu %lu %lu <2 %lu %lu %lu <5 %lu %lu %lu <15 %lu %lu %lu <40 %lu %lu %lu >=40 %lu %lu %lu 2d %lu %lu %lu factor %.3f",
			census_band_vf[0], census_band_dec[0], census_band_quiet[0], census_band_vf[1], census_band_dec[1], census_band_quiet[1],
			census_band_vf[2], census_band_dec[2], census_band_quiet[2], census_band_vf[3], census_band_dec[3], census_band_quiet[3],
			census_band_vf[4], census_band_dec[4], census_band_quiet[4], census_band_vf[5], census_band_dec[5], census_band_quiet[5],
			census_band_vf[6], census_band_dec[6], census_band_quiet[6], (double)listener.distance_factor);
		platform_log("voicecensus kind: vf/dec plain %lu %lu plainf %lu %lu pitched %lu %lu pitchedf %lu %lu general %lu %lu",
			census_kind_vf[0], census_kind_dec[0], census_kind_vf[1], census_kind_dec[1], census_kind_vf[2], census_kind_dec[2],
			census_kind_vf[3], census_kind_dec[3], census_kind_vf[4], census_kind_dec[4]);
		platform_log("voicecensus cap: blocks/vf/loud64 12 %lu %lu %lu 16 %lu %lu %lu 20 %lu %lu %lu",
			census_cap_blocks[0], census_cap_vf[0], census_cap_loud[0], census_cap_blocks[1], census_cap_vf[1], census_cap_loud[1],
			census_cap_blocks[2], census_cap_vf[2], census_cap_loud[2]);
		*at = 0;
		for (k = 0; k < CENSUS_HIST; k++)
			if (census_hist[k])
				at += sprintf(at, " %lu:%lu", k, census_hist[k]);
		platform_log("voicecensus hist:%s", hist);
	}
}
#endif

static int mix(short *output, short *full, short *out44, unsigned long frames)
{
	static int sums[MIX_FRAMES * OUTPUT_CHANNELS] __attribute__((aligned(64)));
	static struct psp_stream *list[VOICE_LIST_MAX], *full_list[VOICE_LIST_MAX];
	struct psp_stream *stream;
	unsigned long voices = 0, count = 0, full_count = 0, clipped = 0, me_us = 0, sc_us;
	int on_me = me_wanted && psp_meaudio_running() && !memix_check && !audio_check, mixed = 0, full_done = 0;
	SceUInt64 start = sceKernelGetSystemTimeWide();
#if PSP_MUSIC
	/* while the music job makes the codec setup on the ME (no yield inside
	Tremor's), a block given to the ME would wait past the audio job's 40 ms
	and stop the ME: this CPU mixes those blocks */
	extern int psp_music_holds_me(void);

	if (on_me && psp_music_holds_me())
		on_me = 0;
#endif
#if PSP_MUSIC
	/* the music path runs this map (psp_music.c): off, no voice is marked */
	extern int psp_music_active(void);
	int music_on = psp_music_active(), music_job = 0;
	unsigned long m;
#endif
	/* the music path's stubs carry no samples to mix at any rate */
	int full_on = full != NULL;
#if PSP_MUSIC
	if (music_on)
		full_on = 0;
#endif

	blocks_begun++;
	if (mixlock_old)
	{
		pthread_mutex_lock(&mixer_lock);
		{
			unsigned long waited = (unsigned long)(sceKernelGetSystemTimeWide() - start);

			if (waited > lock_worst_us)
				lock_worst_us = waited;
		}
	}
	drain_commands();
	for (stream = streams; stream; stream = GAME_OF(stream)->next)
	{
		if (!prepare_voice(stream))
			continue;
		if (count == VOICE_LIST_MAX)
		{
			/* more voices than the list holds (never seen: the game plays
			about 16 at most): the rest stay silent this block */
			me_overflows++;
			break;
		}
		if (full_on && full_count < VOICE_LIST_MAX && full_rate_voice(stream))
		{
			full_list[full_count++] = stream;
			continue;
		}
		list[count++] = stream;
	}
#if PSP_MUSIC
	music_marks = 0;
	for (m = 0; music_on && m < count; m++)
	{
		struct voice_packet *head = current_packet(list[m]);

		list[m]->music_block = head && head->music;
		if (list[m]->music_block && music_marks < MUSIC_MARKS)
		{
			music_owner[music_marks] = list[m];
			music_chain[music_marks] = head->music - 1;
			music_from[music_marks] = head->music_start + list[m]->frame * head->music_scale;
			music_scale_of[music_marks] = head->music_scale;
			music_left[music_marks] = list[m]->target_left;
			music_right[music_marks] = list[m]->target_right;
			music_marks++;
		}
	}
	/* the marks ride the mixer's job: the ME then takes its whole cache
	hand-over (the sets are past the ranges' reach) */
	music_job = music_marks && psp_music_prepare();
#endif
#ifdef PSP_RECON
	voice_census(list, count, frames);
#endif
	if (full_on_me < 0)
	{
		extern int psp_keep(const char *word);

		full_on_me = !psp_keep("audio44main");
	}
	if (on_me && edram_unreachable(1))
	{
		mute_cache_voices(list, count);
		mute_cache_voices(full_list, full_count);
	}
	if (on_me)
	{
		unsigned long k, bytes = frames * OUTPUT_CHANNELS * sizeof(short);
		int ranges, with_full = out44 && full_on_me;

		if (ranges_on < 0)
		{
			extern int psp_keep(const char *word);

			ranges_on = psp_keep("meaudioranges");
			/* its ranges drop main RAM's lines only, and a DMA lands the
			tier's bytes behind the ME's cache: the whole cache it is */
			if (ranges_on && psp_sound_tier_mode())
			{
				ranges_on = 0;
				platform_log("sound tier: meaudioranges refused: the ME drops its whole cache before each block");
			}
		}
		/* everything the ME reads, from this CPU's cache to RAM (the lines
		the mix touches, or the whole cache); no copy of the output block
		left here to be written over the ME's */
		for (k = 0; ranges_on && k < count && bytes <= HANDOVER_WHOLE_BYTES; k++)
			bytes += voice_lines(list[k], frames, LINES_COUNT);
		ranges = ranges_on && bytes <= HANDOVER_WHOLE_BYTES;
#if PSP_MUSIC
		ranges = ranges && !music_job;
#endif
		/* the 44.1 kHz work reads and writes past the voices' ranges: the ME
		takes its whole cache hand-over */
		ranges = ranges && !with_full;
		if (ranges)
		{
			for (k = 0; k < count; k++)
				voice_lines(list[k], frames, LINES_WRITEBACK);
			__asm__ __volatile__("sync" : : : "memory");
			ranges_blocks++;
		}
		else
			sceKernelDcacheWritebackAll();
		sceKernelDcacheWritebackInvalidateRange(output, frames * OUTPUT_CHANNELS * sizeof(short));
		if (with_full)
		{
			/* what the ME writes and the filter's state, no copy left here */
			sceKernelDcacheWritebackInvalidateRange(out44, 2 * frames * OUTPUT_CHANNELS * sizeof(short));
			if (full)
				sceKernelDcacheWritebackInvalidateRange(full, 2 * frames * OUTPUT_CHANNELS * sizeof(short));
			sceKernelDcacheWritebackInvalidateRange(up_history, sizeof(up_history));
			sceKernelDcacheWritebackInvalidateRange(up_input, sizeof(up_input));
		}
		psp_me_audio_full(with_full ? (void *const *)full_list : NULL, with_full ? full_count : 0, with_full ? out44 : NULL,
			with_full ? full : NULL);
		job_running = 1;
#if PSP_MUSIC
		psp_me_audio_music(music_job);
#endif
		mixed = psp_meaudio_run((void *const *)list, count, output, frames, &voices, &clipped, &me_us);
#if PSP_MUSIC
		psp_me_audio_music(0);
#endif
		job_running = 0;
		/* what the ME changed, dropped from this cache (whole lines: see
		STREAM_BYTES; nothing here wrote to them (the lock is held, or the game's side is on lines of its own): the
		fields and entries the mix writes, or each voice whole */
		for (k = 0; k < count; k++)
		{
			if (ranges)
			{
				cache_lines(list[k], STREAM_HEAD_BYTES, LINES_DROP);
				entry_lines(list[k], LINES_DROP);
			}
			else
				sceKernelDcacheInvalidateRange(list[k], list[k]->game_offset);
		}
		if (with_full)
		{
			for (k = 0; k < full_count; k++)
				sceKernelDcacheInvalidateRange(full_list[k], full_list[k]->game_offset);
			sceKernelDcacheInvalidateRange(out44, 2 * frames * OUTPUT_CHANNELS * sizeof(short));
			if (full)
				sceKernelDcacheInvalidateRange(full, 2 * frames * OUTPUT_CHANNELS * sizeof(short));
			sceKernelDcacheInvalidateRange(up_history, sizeof(up_history));
			sceKernelDcacheInvalidateRange(up_input, sizeof(up_input));
		}
		__asm__ __volatile__("sync" : : : "memory");
		sceKernelDcacheInvalidateRange(output, frames * OUTPUT_CHANNELS * sizeof(short));
		if (mixed && with_full)
		{
			unsigned long heard = 0;

			full_done = psp_me_audio_full_done(&heard);
			full_on_me_blocks += full_done;
			if (full_done && full)
			{
				voices += heard;
				full_heard += heard;
				full_blocks++;
				full_voice_blocks += full_count;
			}
		}
		if (mixed)
		{
			me_blocks++;
			me_wait_us += me_us;
			if (me_us > me_wait_worst_us)
				me_wait_worst_us = me_us;
		}
		else
			me_failures++;
	}
	if (!mixed)
	{
		if (edram_unreachable(0))
			mute_cache_voices(list, count);
		if (memix_check)
			voices = mix_checked(list, count, sums, output, frames, &clipped);
		else if (audio_check)
			voices = mix_approx_checked(list, count, sums, output, frames, &clipped);
		else
			voices = psp_dsound_mix_list((void *const *)list, count, sums, output, frames, &clipped);
#if PSP_MUSIC
		if (music_job)
			psp_dsound_music_job(frames);
#endif
	}
#if PSP_MUSIC
	for (m = 0; m < music_marks; m++)
		((struct psp_stream *)music_owner[m])->music_block = 0;
#endif
	if (full && !full_done)
	{
		unsigned long heard;

		if (edram_unreachable(0))
			mute_cache_voices(full_list, full_count);
		heard = mix_full_rate(full_list, full_count, full, frames, &clipped);
		voices += heard;
		full_heard += heard;
		full_blocks++;
		full_voice_blocks += full_count;
	}
	{
		unsigned long k;

		for (k = 0; k < count; k++)
			publish_finished(list[k]);
		for (k = 0; k < full_count; k++)
			publish_finished(full_list[k]);
#if PSP_SNDSTREAM
		for (k = 0; k < count; k++)
			sndstream_collect(list[k]);
		for (k = 0; k < full_count; k++)
			sndstream_collect(full_list[k]);
#endif
	}
	blocks_ended++;
	if (mixlock_old)
		pthread_mutex_unlock(&mixer_lock);
#if PSP_MUSIC
	psp_music_after_block(music_job);
#endif
	clipped_blocks += clipped != 0;
	mix_blocks++;
	mix_voice_blocks += voices;
	if (voices > mix_peak_voices)
		mix_peak_voices = voices;
	{
		/* the main CPU's own time on the block: the ME's share, waited for
		asleep, left out */
		unsigned long took = (unsigned long)(sceKernelGetSystemTimeWide() - start);

		sc_us = took > me_us ? took - me_us : 0;
		mix_microseconds += sc_us;
		if (mixed)
			psp_me_handover(0, sc_us);
		if (sc_us > mix_worst_us)
			mix_worst_us = sc_us;
	}
	return full_done;
}

/* ---------- output */

static short output_buffers[2][MIX_FRAMES * OUTPUT_CHANNELS] __attribute__((aligned(64)));
static int audio_started = 0;

static int audio_thread(SceSize arguments, void *argument)
{
	int channel = -1, which = 0, up = 0, full_rate = 0, src44 = 0;
	/* the blocks by slot: two, mixed and played in turn; "audiohold" four, the
	output a block behind the mix, so one block is held mixed beside the
	one queued and a mix may take two blocks before the speaker runs dry
	(psp_me.c's ME_FEATURE_AUDIO_HOLD) */
	short *slot_out[4] = { output_buffers[0], output_buffers[1], NULL, NULL };
	short *slot_44[4] = { output44[0], output44[1], NULL, NULL };
	short *slot_full[4] = { output_full[0], output_full[1], NULL, NULL };
	int slots = 2, mix_at = 0, primed = 1;
	SceUID dump = -1;
	unsigned long dumped = 0;

	/* build 89: the FPU's traps masked, as on every other thread (a gain
	worked out as infinity or NaN would have stopped the PSP here; review) */
	{
		extern void psp_fpu_mask_traps(void);

		psp_fpu_mask_traps();
	}
	(void)arguments;
	(void)argument;
	{
		extern int psp_keep(const char *word);

		up = psp_keep("audio44");
		full_rate = psp_keep("musadpcm");
		up |= full_rate;
		/* the 44.1 kHz block on the sample-rate converter's own channel set
		to 44,100 Hz (nothing to convert), not an ordinary channel: on the
		ordinary one audio44 cost b30's frame +7.8% (sierra-hw
		41211384661) and this one took back 6.3% of it (41213051173).
		"audio44ordinary" keeps the ordinary channel */
		src44 = up && !psp_keep("audio44ordinary");
	}
	channel = src44 ? sceAudioSRCChReserve(2 * MIX_FRAMES, 44100, 2) :
		up ? sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 2 * MIX_FRAMES, PSP_AUDIO_FORMAT_STEREO) :
		sceAudioSRCChReserve(MIX_FRAMES, OUTPUT_RATE, 2);
	{
		extern int psp_keep(const char *word);

		if (channel < 0 && psp_keep("audiohold"))
			psp_me_audio_hold_off(); /* nothing plays, so nothing to hold for */
		else if (psp_keep("audiohold"))
		{
			int k, ok = 1;
			unsigned int bytes = 0;

			/* the 44.1 kHz blocks only where they're mixed and played */
			for (k = 2; k < 4; k++)
			{
				slot_out[k] = (short *)memalign(64, sizeof(output_buffers[0]));
				bytes += sizeof(output_buffers[0]);
				if (!slot_out[k])
					ok = 0;
				if (up)
				{
					slot_44[k] = (short *)memalign(64, sizeof(output44[0]));
					bytes += sizeof(output44[0]);
					if (!slot_44[k])
						ok = 0;
				}
				if (full_rate)
				{
					slot_full[k] = (short *)memalign(64, sizeof(output_full[0]));
					bytes += sizeof(output_full[0]);
					if (!slot_full[k])
						ok = 0;
				}
			}
			if (ok)
			{
				slots = 4;
				primed = 0;
				platform_log("audio: audiohold, one block more ahead of the speaker (%d ms more delay; %u bytes of heap)",
					MIX_FRAMES * 1000 / OUTPUT_RATE, bytes);
			}
			else
			{
				/* as before, and the ME told: it mustn't hold a block with no
				extra slot to cover the wait */
				for (k = 2; k < 4; k++)
				{
					free(slot_out[k]);
					free(slot_44[k]);
					free(slot_full[k]);
					slot_out[k] = slot_44[k] = slot_full[k] = NULL;
				}
				psp_me_audio_hold_off();
				platform_log("audio: audiohold: no room for its two blocks (%u bytes); as before, and the ME doesn't hold", bytes);
			}
		}
	}
	if (channel < 0)
		platform_log("audio: no output channel (%08x); sound runs silent", channel);
	else
		platform_log("audio: %d Hz stereo, %d samples a block (%d ms)%s", OUTPUT_RATE, MIX_FRAMES,
			MIX_FRAMES * 1000 / OUTPUT_RATE, up ? (src44 ? "; the 44.1 kHz block on the converter's channel at 44,100 Hz" : "; the 44.1 kHz block on an ordinary channel (audio44ordinary)") : "");
	if (channel >= 0 && up)
		platform_log("audio: %s", full_rate ? "to 44,100 Hz by the half-band filter, stereo 44.1 kHz voices mixed at 44,100 Hz (musadpcm)" :
			"to 44,100 Hz by the half-band filter (audio44)");
	{
		extern int psp_keep(const char *word);

		/* keep.txt's "audiodump": the first 20 s of the mix into ms0:/halo/audio.raw
		(22,050 Hz stereo 16-bit), to check the mixer off the PSP; with a
		44.1 kHz output, what it plays into audio44.raw (44,100 Hz) */
		if (psp_keep("audiodump"))
			dump = sceIoOpen(up ? PSP_DATA_ROOT "/audio44.raw" : PSP_DATA_ROOT "/audio.raw",
				PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
	}
	for (;;)
	{
		/* the ME's job may have made the 44.1 kHz block whole */
		if (!mix(slot_out[mix_at], full_rate ? slot_full[mix_at] : NULL, up ? slot_44[mix_at] : NULL, MIX_FRAMES) &&
			up)
		{
			upsample_full(slot_out[mix_at], slot_44[mix_at], full_rate ? slot_full[mix_at] : NULL);
		}
		if (dump >= 0 && up)
		{
			sceIoWrite(dump, slot_44[mix_at], sizeof(output44[0]));
			dumped += MIX_FRAMES;
			if (dumped >= 20 * OUTPUT_RATE)
			{
				sceIoClose(dump);
				dump = -1;
			}
		}
		else if (dump >= 0)
		{
			sceIoWrite(dump, slot_out[mix_at], sizeof(output_buffers[0]));
			dumped += MIX_FRAMES;
			if (dumped >= 20 * OUTPUT_RATE)
			{
				sceIoClose(dump);
				dump = -1;
			}
		}
		mix_at = (mix_at + 1) % slots;
		if (!primed)
		{
			/* audiohold's first block: mixed, held; the next pass plays it */
			primed = 1;
			continue;
		}
		if (channel < 0)
			sceKernelDelayThread(MIX_FRAMES * 1000000 / OUTPUT_RATE);
		else
		{
			/* the output takes a block once the one before has started
			playing: returns further apart than a block plus a few ms mean
			the hardware ran out between them */
			static SceUInt64 last_return = 0;
			SceUInt64 now;

			if (up)
			{
				sceKernelDcacheWritebackRange(slot_44[which], sizeof(output44[0]));
				if (src44)
					sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX, slot_44[which]);
				else
					sceAudioOutputPannedBlocking(channel, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, slot_44[which]);
			}
			else
				sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX, slot_out[which]);
			now = sceKernelGetSystemTimeWide();
			if (last_return)
			{
				unsigned long gap = (unsigned long)(now - last_return), block = MIX_FRAMES * 1000000 / OUTPUT_RATE;

				if (gap > block + 3000)
					late_blocks++;
				if (gap > gap_worst_us)
					gap_worst_us = gap;
			}
			last_return = now;
		}
		which = (which + 1) % slots;
	}
	return 0;
}

static void audio_start(void)
{
	SceUID thread;

	if (audio_started)
		return;
	audio_started = 1;
	master_volume = (float)config_real("audio.volume");
	/* its table is written back before the Media Engine can decode */
	ima_fast_build();
	/* build 86: the launch option meaudio: the Media Engine mixes */
	psp_meaudio_start();
	{
		extern int psp_keep(const char *word);

		/* nomeaudio with the ME started for other jobs (meslant): the main
		CPU still mixes, so an A/B of those jobs leaves audio where it was */
		if (psp_keep("nomeaudio"))
			me_wanted = 0;
	}
	{
		extern int psp_keep(const char *word);

		/* before the audio thread or any stream exists: nothing reads it earlier */
		mixlock_old = psp_keep("mixlockold") != 0;
		if (psp_keep("memixcheck"))
		{
			check_copies = memalign(64, CHECK_VOICES * STREAM_BYTES);
			memix_check = check_copies != NULL;
		}
		else if (psp_keep("audiocheck"))
		{
			check_copies = memalign(64, CHECK_VOICES * STREAM_BYTES);
			audio_check = check_copies != NULL;
			if (psp_keep("audiocheckcal"))
				audio_approx |= AUDIO_APPROX_CAL;
			platform_log("audio: audiocheck: %s", audio_check ? "each block mixed with and without the approximate paths on the main CPU, the difference measured" : "no room for its copies, off");
		}
		platform_log("audio: mixkinds: %s", psp_keep("mixkinds") ? "a mono ADPCM voice mixed by its kind's loop, a run a decoded block (H1)" : "off, every heard voice by the one loop");
		platform_log("audio: heard voices mixed %s%s", psp_keep("memixold") ? "by the per-sample loop, as before (memixold)" : "with the position in registers and mono voices once (MEMIX)",
			memix_check ? "; memixcheck: each block mixed both ways on the main CPU" : psp_keep("memixcheck") ? "; memixcheck: no room for its copies, off" : "");
		platform_log("audio: the mixer's lock %s", mixlock_old ? "held through the Media Engine's job, as before (mixlockold)" : "not held across the job: the game's side is on cache lines of its own, its changes reach the mix by a ring (MIXLOCK)");
	}
	/* above the game's thread, which it interrupts for a moment every 23 ms */
	thread = sceKernelCreateThread("halo audio", audio_thread, 0x12, 0x4000, 0, NULL);
	if (thread >= 0)
		sceKernelStartThread(thread, 0, NULL);
}

/* the mixer's cost, for the heartbeat log */
#if PSP_SNDSTREAM
static void sndstream_report(void);
#endif
void psp_audio_report(void)
{
#if PSP_SNDSTREAM
	sndstream_report();
#endif
#if PSP_MUSIC
	{
		extern void psp_music_report(void);

		psp_music_report();
	}
#endif
	if (!mix_blocks)
		return;
	unsigned long block_us = MIX_FRAMES * 1000000 / OUTPUT_RATE, average = mix_microseconds / mix_blocks;
	unsigned long permille = average * 1000 / block_us;

	platform_log("audio: mixing %lu us a block of %lu us (%lu.%lu%% of the CPU), %lu.%lu voices on average, %lu at most; worst block %lu us, lock wait %lu us; %lu of %lu blocks late (longest gap %lu us), %lu at the 16-bit limit; sounds stopped by the game before their end %lu (some normally: a trigger let go)",
		average, block_us, permille / 10, permille % 10,
		mix_voice_blocks / mix_blocks, mix_voice_blocks * 10 / mix_blocks % 10, mix_peak_voices,
		mix_worst_us, lock_worst_us, late_blocks, mix_blocks, gap_worst_us, clipped_blocks, cut_sounds);
	if (full_blocks)
		platform_log("audio: musadpcm: %lu blocks mixed stereo 44.1 kHz voices at 44,100 Hz, %lu.%lu of them a block on average, %lu voice-blocks heard",
			full_blocks, full_voice_blocks / full_blocks, full_voice_blocks * 10 / full_blocks % 10, full_heard);
	if (full_on_me_blocks || full_on_me == 0)
		platform_log("audio: the 44.1 kHz work (the filter%s) on the Media Engine in %lu of %lu blocks%s",
			full_blocks ? " and the full-rate voices" : "", full_on_me_blocks, mix_blocks,
			full_on_me == 0 ? " (audio44main: on the main CPU)" : "");
	if (me_blocks || me_failures || psp_meaudio_running())
		platform_log("audio: Media Engine mixed %lu of the blocks (menu: %s), waited for %lu us a block on average, %lu at most (main CPU asleep meanwhile); %lu failed; %lu over the voice list; %lu handed over by ranges (the rest by the whole cache)",
			me_blocks, me_wanted ? "ME" : "main CPU", me_blocks ? me_wait_us / me_blocks : 0, me_wait_worst_us, me_failures,
			me_overflows, ranges_blocks);
	audio_check_report();
	if (memix_check)
	{
		platform_log("audio: memixcheck: %lu blocks mixed both ways (%lu with more than %d voices not checked): outputs differed in %lu, the voices after in %lu (first at block %lu)",
			check_blocks, check_skipped, CHECK_VOICES, check_output_differed, check_state_differed, check_first_differed);
		platform_log("audio: memixcheck: voice blocks by path: mono %lu, stereo or unequal filters %lu, PCM %lu, filtered %lu, interpolated %lu (ADPCM, across block ends: %lu)",
			check_mono, check_stereo, check_pcm, check_filtered, check_stepped, check_stepped_adpcm);
		platform_log("audio: memixcheck: mixkinds %s; voice blocks by the kind they started in: plain %lu, plain filtered %lu, pitched %lu, pitched filtered %lu",
			psp_keep("mixkinds") ? "on" : "off", check_kind[0], check_kind[1], check_kind[2], check_kind[3]);
	}
	platform_log("audio: of %lu voice blocks, muffled %lu, pitch moved by Doppler %lu, quieter outside a cone %lu%s",
		fx_prepared, fx_muffled, fx_shifted, fx_coned, audio_old > 0 ? " (audioold: all off)" : "");
	platform_log("audio: the game waited for the mixer's lock %lu times, %lu us in all, %lu at most; voices that ran dry and were then fed more (a gap heard) %lu, %lu ms of silence in all, the longest %lu ms",
		game_lock_waits, game_lock_wait_us, game_lock_worst_us, dry_gaps, dry_gap_frames * 1000 / OUTPUT_RATE,
		dry_gap_worst * 1000 / OUTPUT_RATE);
	platform_log("audio: mixlock%s: %lu of the game's %lu calls into the mixer came while the Media Engine mixed (each would have waited for the lock until the job ended); %lu commands queued, the ring %lu of %d at most, %lu waits for room in it",
		mixlock_old ? " (mixlockold: the lock held through the job)" : "", would_waits, game_calls, commands_queued, ring_fullest, MIX_COMMANDS,
		ring_full_waits);
	if (cut_sounds)
		platform_log("audio: cut by the game, by reason: an older like sound retired %lu, the new one retired %lu, its source gone %lu, the players dead %lu, faded out %lu, its voice taken %lu, no voice free %lu, not cached in time %lu, all stopped %lu, other %lu",
			cut_by_reason[PSP_STOP_INSTANCE_OLDER], cut_by_reason[PSP_STOP_INSTANCE_NEWER], cut_by_reason[PSP_STOP_SOURCE_LOST],
			cut_by_reason[PSP_STOP_PLAYERS_DEAD], cut_by_reason[PSP_STOP_FADED], cut_by_reason[PSP_STOP_PREEMPTED],
			cut_by_reason[PSP_STOP_NO_CHANNEL], cut_by_reason[PSP_STOP_CACHE_MISS], cut_by_reason[PSP_STOP_ALL], cut_by_reason[PSP_STOP_OTHER]);
	{
		unsigned long k, shown = gap_record_count < GAP_RECORDS ? gap_record_count : GAP_RECORDS;

		for (k = 0; k < shown; k++)
			platform_log("audio: gap %lu of %lu: channel %ld, sound class %ld, %lu ms of silence, after a frame of %lu ms",
				k + 1, gap_record_count, gap_records[k].context, gap_records[k].sound_class, gap_records[k].dry_ms,
				gap_records[k].frame_ms);
	}
	{
		unsigned long k, shown = jump_record_count < GAP_RECORDS ? jump_record_count : GAP_RECORDS;

		if (stream_links)
			platform_log("audio: %lu permutations queued to a channel with nothing left to send (%s)", stream_links,
				stream_link_off > 0 ? "streamnolink: filed as queued and dropped" : "linked on");
		stream_links = 0;
		if (jump_record_count || flushed_unplayed)
			platform_log("audio: %lu packets didn't continue their sound where the last ended; %lu unplayed packets flushed",
				jump_record_count, flushed_unplayed);
		for (k = 0; k < shown; k++)
			platform_log("audio: jump %lu of %lu: channel %ld, sound class %ld, %ld bytes from the last packet's end, %lu queued",
				k + 1, jump_record_count, jump_records[k].context, jump_records[k].sound_class, jump_records[k].bytes,
				jump_records[k].queued);
	}
	jump_record_count = flushed_unplayed = 0;
	gap_record_count = 0;
	memset(cut_by_reason, 0, sizeof(cut_by_reason));
	dry_gaps = dry_gap_frames = dry_gap_worst = 0;
	game_lock_waits = game_lock_wait_us = game_lock_worst_us = 0;
	would_waits = game_calls = commands_queued = ring_fullest = ring_full_waits = 0;
	me_blocks = me_wait_us = me_wait_worst_us = me_failures = me_overflows = ranges_blocks = 0;
	mix_blocks = mix_microseconds = mix_voice_blocks = mix_peak_voices = 0;
	full_blocks = full_voice_blocks = full_heard = full_on_me_blocks = 0; /* per report, as the mix line's */
	mix_worst_us = lock_worst_us = late_blocks = clipped_blocks = gap_worst_us = cut_sounds = 0;
}

/* How many packets the game keeps queued on each stream: the Xbox kept 4
(about 743 ms), and its packet callbacks topped them up at interrupt time; the
port tops them up only once a frame (DirectSoundDoWork from sound_render), so
a frame longer than the queue (a checkpoint save, a log close) left long sounds
silent. A packet is ~186 ms (2,304 bytes of ADPCM at 22 kHz), so 8 cover ~1.5 s.
A deeper queue isn't free: each packet queued holds its sound in the sound
cache, and at 16 the pieces queued ahead (a cutscene's music, the next loop)
held so much that the cache refused other sounds for room, Cortana's lines
among them (B33). "streampacketsN" sets N (2-32); "streampackets4" is the
Xbox's depth */
#define STREAM_PACKETS_DEFAULT 8
int psp_stream_packets(void)
{
	extern int psp_keep_number(const char *prefix, int fallback);
	static int packets = 0;

	if (!packets)
	{
		packets = psp_keep_number("streampackets", STREAM_PACKETS_DEFAULT);
		if (packets < 2)
			packets = 2;
		if (packets > 32)
			packets = 32;
		platform_log("audio: %d packets queued on each stream (about %d ms)%s", packets, packets * 186,
			packets == 4 ? ": the Xbox's depth" : "");
	}
	return packets;
}

/* sound_dsound_xbox.c's queue_sound: a permutation queued to a channel that
plays on with nothing left to send becomes its playing permutation (psp_fixups.py
FIXES_SOUND #link); "streamnolink" files it as queued, as before, where the
fill never sent it. Counted for the log: each would have been a permutation
dropped */
int psp_stream_link(void)
{
	extern int psp_keep(const char *name);

	if (stream_link_off < 0)
		stream_link_off = psp_keep("streamnolink") != 0;
	stream_links++;
	return !stream_link_off;
}

/* ---------- completion */

/* completes the head packet; called with the lock held (mixlockold), which the
game's callback runs without */
static void stream_complete_head(struct psp_stream *stream, DWORD status, DWORD completed_size)
{
	struct stream_game *game = GAME_OF(stream);
	XMEDIAPACKET packet = game->entry[game->head].packet;

	game->head = (game->head + 1) & stream->packet_mask;
	game->count--;
	game->popped++;
	mix_command(stream, CMD_POP, NULL, 0, 0);
	if (packet.pdwCompletedSize)
		*packet.pdwCompletedSize = completed_size;
	if (packet.pdwStatus)
		*packet.pdwStatus = status;
	if (stream->callback)
	{
		if (mixlock_old)
			pthread_mutex_unlock(&mixer_lock);
		stream->callback(stream->context, packet.pContext, status);
		if (mixlock_old)
			game_lock();
	}
	else if (packet.hCompletionEvent)
	{
		SetEvent(packet.hCompletionEvent);
	}
}

static void streams_complete_finished(void)
{
	struct psp_stream *stream;

	queue_enter();
	for (stream = streams; stream; stream = GAME_OF(stream)->next)
	{
		struct stream_game *game = GAME_OF(stream);

		while (game->count && packet_finished(stream, 0))
			stream_complete_head(stream, XMEDIAPACKET_STATUS_SUCCESS, game->entry[game->head].packet.dwMaxSize);
	}
	queue_leave();
	reap_streams();
}

/* ---------- stream interface */

static struct psp_stream *stream_from_interface(void *stream)
{
	return (struct psp_stream *)stream;
}

static ULONG STDMETHODCALLTYPE stream_add_reference(IDirectSoundStream *object)
{
	struct psp_stream *stream = stream_from_interface(object);
	unsigned int guard;
	ULONG count;

	guard = set_enter();
	count = ++GAME_OF(stream)->reference_count;
	set_leave(guard);
	return count;
}

static HRESULT STDMETHODCALLTYPE stream_flush(IDirectSoundStream *object);

/* out of the mixer's list: under the lock (mixlockold), or by single stores that
the audio thread's walk can't see torn. The record stays until zombie_stamp passes */
static void stream_unlink(struct psp_stream *stream)
{
	struct psp_stream *volatile *link;

	queue_enter();
	for (link = &streams; *link; link = &GAME_OF(*link)->next)
	{
		if (*link == stream)
		{
			*link = GAME_OF(stream)->next;
			break;
		}
	}
	GAME_OF(stream)->zombie_stamp = blocks_begun + 1;
	queue_leave();
}

static ULONG STDMETHODCALLTYPE stream_release(IDirectSoundStream *object)
{
	struct psp_stream *stream = stream_from_interface(object);
	unsigned int guard;
	ULONG count;

	guard = set_enter();
	count = --GAME_OF(stream)->reference_count;
	set_leave(guard);
	if (count)
		return count;

	stream_flush(object);
	stream_unlink(stream);
	retire_stream(stream);
	return 0;
}

static HRESULT STDMETHODCALLTYPE stream_get_info(IDirectSoundStream *object, LPXMEDIAINFO information)
{
	struct psp_stream *stream = stream_from_interface(object);

	memset(information, 0, sizeof(*information));
	information->dwFlags = XMO_STREAMF_FIXED_SAMPLE_SIZE | XMO_STREAMF_INPUT_ASYNC;
	information->dwInputSize = stream->adpcm ? XBOX_ADPCM_BLOCK_BYTES * stream->channels : 2 * stream->channels;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_get_status(IDirectSoundStream *object, LPDWORD status)
{
	struct psp_stream *stream = stream_from_interface(object);

	queue_enter();
	*status = GAME_OF(stream)->count <= stream->packet_mask ? XMO_STATUSF_ACCEPT_INPUT_DATA : 0;
	queue_leave();
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_process(IDirectSoundStream *object, LPCXMEDIAPACKET input, LPCXMEDIAPACKET output)
{
	struct psp_stream *stream = stream_from_interface(object);
	struct stream_game *game = GAME_OF(stream);
	const unsigned char *data;
	unsigned long frames;

	(void)output;
	if (!input)
		return E_INVALIDARG;
	/* whole frames only: whole ADPCM blocks, whole PCM frames */
	frames = stream->adpcm ?
		input->dwMaxSize / (XBOX_ADPCM_BLOCK_BYTES * stream->channels) * XBOX_ADPCM_BLOCK_SAMPLES :
		input->dwMaxSize / (2 * stream->channels);
	queue_enter();
	if (game->count > stream->packet_mask)
	{
		queue_leave();
		return E_OUTOFMEMORY;
	}
	game->entry[(game->head + game->count) & stream->packet_mask].packet = *input;
	data = (const unsigned char *)input->pvBuffer;
	if (input->pdwStatus)
		*input->pdwStatus = XMEDIAPACKET_STATUS_PENDING;
	if (input->pdwCompletedSize)
		*input->pdwCompletedSize = 0;
	if (game->queued_context && input->pContext == game->queued_context && data &&
		data != game->queued_end)
	{
		if (jump_record_count < GAP_RECORDS)
		{
			jump_records[jump_record_count].context = (long)stream->context;
			jump_records[jump_record_count].sound_class = game->sound_class;
			jump_records[jump_record_count].bytes = (long)(data - game->queued_end);
			jump_records[jump_record_count].queued = game->count;
		}
		jump_record_count++;
	}
	game->queued_end = data ? data + input->dwMaxSize : NULL;
	game->queued_context = input->pContext;
	if (psp_sound_queueing_class >= 0)
		game->sound_class = psp_sound_queueing_class;
#if PSP_MUSIC || PSP_SNDSTREAM
	{
		/* whether it is a music stub's or a streamed sound's is the game's
		side's to say: the stubs are made and freed on the game's thread */
		extern int psp_music_packet(const void *permutation, const unsigned char *data, unsigned long channels,
			unsigned long *start, unsigned long *scale);
		struct mix_command command;

		memset(&command, 0, sizeof(command));
		command.stream = stream;
		command.type = CMD_PUSH;
		command.data = data;
		command.frames = data ? frames : 0;
		command.sound_class = game->sound_class;
#if PSP_MUSIC
		command.music = psp_music_packet(input->pContext, data, stream->channels, &command.music_start,
			&command.music_scale);
#endif
#if PSP_SNDSTREAM
		command.snd_check = 0;
		command.snd_id = stream->adpcm ? sndstream_packet(input->pContext, data, &command.snd_offset,
			&command.snd_check) : 0;
		game->entry[(game->head + game->count) & stream->packet_mask].snd_id = command.snd_id;
		game->entry[(game->head + game->count) & stream->packet_mask].snd_offset = command.snd_offset;
#endif
		mix_queue(&command);
	}
#else
	mix_command(stream, CMD_PUSH, data, data ? frames : 0, game->sound_class);
#endif
	game->count++;
	queue_leave();
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_discontinuity(IDirectSoundStream *object)
{
	(void)object;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_flush(IDirectSoundStream *object)
{
	struct psp_stream *stream = stream_from_interface(object);
	struct stream_game *game = GAME_OF(stream);

	queue_enter();
	/* build 43: a sound stopped by the game before it finished playing (a
	voice taken for a newer sound, or a sound the game gave up on): the
	heard cut-offs, if the output itself never runs late */
	if (game->count && !packet_finished(stream, 0))
	{
		cut_sounds++;
		cut_by_reason[psp_sound_stop_reason]++;
	}
	{
		unsigned long position;

		for (position = 0; position < game->count; position++)
			if (!packet_finished(stream, position))
				flushed_unplayed++;
	}
	game->queued_context = NULL;
	while (game->count)
	{
		int done = packet_finished(stream, 0);

		stream_complete_head(stream, done ? XMEDIAPACKET_STATUS_SUCCESS : XMEDIAPACKET_STATUS_FLUSHED,
			done ? game->entry[game->head].packet.dwMaxSize : 0);
	}
	mix_command(stream, CMD_RESET, NULL, 0, 0);
	queue_leave();
	return S_OK;
}

static IDirectSoundStreamVtbl stream_vtable =
{
	stream_add_reference,
	stream_release,
	stream_get_info,
	stream_get_status,
	stream_process,
	stream_discontinuity,
	stream_flush,
};

/* ---------- the DirectSound object */

struct psp_direct_sound
{
	ULONG reference_count;
};

static struct psp_direct_sound direct_sound = { 0 };

HRESULT WINAPI DirectSoundCreate(LPGUID device_id, LPDIRECTSOUND *result, LPUNKNOWN outer)
{
	(void)device_id;
	(void)outer;
	audio_start();
	direct_sound.reference_count++;
	*result = (LPDIRECTSOUND)&direct_sound;
	return DS_OK;
}

ULONG WINAPI IDirectSound_Release(LPDIRECTSOUND sound)
{
	(void)sound;
	return direct_sound.reference_count ? --direct_sound.reference_count : 0;
}

VOID WINAPI DirectSoundDoWork(void)
{
	streams_complete_finished();
}

VOID WINAPI DirectSoundUseFullHRTF(void)
{
}

HRESULT WINAPI IDirectSound_GetCaps(LPDIRECTSOUND sound, LPDSCAPS caps)
{
	(void)sound;
	memset(caps, 0, sizeof(*caps));
	caps->dwFree2DBuffers = 64;
	caps->dwFree3DBuffers = 64;
	caps->dwFreeBufferSGEs = 2047;
	caps->dwMemoryAllocated = 0;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_GetSpeakerConfig(LPDIRECTSOUND sound, LPDWORD speaker_config)
{
	(void)sound;
	*speaker_config = DSSPEAKER_STEREO;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_DownloadEffectsImage(LPDIRECTSOUND sound, LPCVOID image, DWORD image_size,
	LPCDSEFFECTIMAGELOC image_location, LPDSEFFECTIMAGEDESC *image_description)
{
	(void)sound;
	(void)image;
	(void)image_size;
	(void)image_location;
	if (image_description)
		*image_description = NULL;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CommitDeferredSettings(LPDIRECTSOUND sound) { (void)sound; return DS_OK; }
HRESULT WINAPI IDirectSound_SetMixBinHeadroom(LPDIRECTSOUND sound, DWORD mix_bin_mask, DWORD headroom) { (void)sound; (void)mix_bin_mask; (void)headroom; return DS_OK; }
HRESULT WINAPI IDirectSound_SetI3DL2Listener(LPDIRECTSOUND sound, LPCDSI3DL2LISTENER listener_properties, DWORD apply) { (void)sound; (void)listener_properties; (void)apply; return DS_OK; }

HRESULT WINAPI IDirectSound_SetDistanceFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	unsigned int guard = set_enter();
	listener.distance_factor = factor > 0.0f ? factor : 1.0f;
	set_leave(guard);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetRolloffFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	unsigned int guard = set_enter();
	listener.rolloff_factor = factor >= 0.0f ? factor : 1.0f;
	set_leave(guard);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetPosition(LPDIRECTSOUND sound, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)sound;
	(void)apply;
	unsigned int guard = set_enter();
	listener.position[0] = x;
	listener.position[1] = y;
	listener.position[2] = z;
	set_leave(guard);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetVelocity(LPDIRECTSOUND sound, FLOAT x, FLOAT y, FLOAT z, DWORD apply) { (void)sound; (void)x; (void)y; (void)z; (void)apply; return DS_OK; }

static void normalize3(float *vector)
{
	float length = sqrtf(dot3(vector, vector));

	if (length > 1.0e-6f)
	{
		vector[0] /= length;
		vector[1] /= length;
		vector[2] /= length;
	}
}

HRESULT WINAPI IDirectSound_SetOrientation(LPDIRECTSOUND sound, FLOAT x_front, FLOAT y_front, FLOAT z_front,
	FLOAT x_top, FLOAT y_top, FLOAT z_top, DWORD apply)
{
	(void)sound;
	(void)apply;
	unsigned int guard = set_enter();
	listener.front[0] = x_front;
	listener.front[1] = y_front;
	listener.front[2] = z_front;
	listener.top[0] = x_top;
	listener.top[1] = y_top;
	listener.top[2] = z_top;
	normalize3(listener.front);
	normalize3(listener.top);
	set_leave(guard);
	return DS_OK;
}

/* a stream as DirectSound starts one, in STREAM_BYTES + GAME_BYTES of 64-byte
aligned memory (the mixer's record, then the game's), and in the mixer's list */
static void stream_start(struct psp_stream *stream, LPCDSSTREAMDESC description, unsigned long packets)
{
	const WAVEFORMATEX *format = description->lpwfxFormat;
	struct stream_game *game;

	memset(stream, 0, stream_bytes(packets) + game_bytes(packets));
	stream->packet_mask = packets - 1;
	stream->game_offset = stream_bytes(packets);
	game = GAME_OF(stream);
	stream->object.lpVtbl = &stream_vtable;
	game->reference_count = 1;
	stream->callback = description->lpfnCallback;
	stream->context = description->lpvContext;
	game->sound_class = -1;
#if PSP_SNDSTREAM
	stream->snd_hint = -1;
#endif
	stream->adpcm = format && format->wFormatTag == WAVE_FORMAT_XBOX_ADPCM;
	stream->channels = format && format->nChannels == 2 ? 2 : 1;
	stream->sample_rate = format ? format->nSamplesPerSec : 0;
	game->sample_rate = stream->sample_rate;
	game->frequency = stream->sample_rate;
	game->volume = 1.0f;
	/* DirectSound's default mix bins: a mono voice to both fronts, a
	stereo voice's channels to the front left and right */
	game->mix_left = 1.0f;
	game->mix_right = 1.0f;
	game->has_3d = (description->dwFlags & DSSTREAMCAPS_CTRL3D) != 0;
	game->mode = DS3DMODE_NORMAL;
	game->minimum_distance = DS3D_DEFAULTMINDISTANCE;
	game->maximum_distance = DS3D_DEFAULTMAXDISTANCE;
	game->i3dl2_gain = 1.0f;
	stream->lowpass_coefficient = GAIN_ONE;
	game->cone_inside = game->cone_outside = 360;
	game->cone_forward[2] = 1.0f;
	stream->decoded_block[0] = stream->decoded_block[1] = -1;
	queue_enter();
	game->next = streams;
	__asm__ __volatile__("" : : : "memory");
	streams = stream;
	queue_leave();
}

/* SNDSTREAMREC: the game's streams made with room for 16 queued packets
rather than MAXIMUM_STREAM_PACKETS ("streamrecN", 8, 32 or 64, for another
size; streamrec64 is the old record). The game keeps at most
four attached (sound_dsound_xbox.c, dwMaxAttachedPackets) and the mixer's
ring held at most 16 on any route (sndrec1); its 81 streams at 64 take ~249
KB of heap, at 32 ~140. A full ring refuses the packet (E_OUTOFMEMORY), which
the game logs as "packet refused". The loading chant's loop voices keep 64:
they loop by queueing the sound in every entry */
static unsigned long stream_record_packets(void)
{
	static long packets = -1;

	if (packets < 0)
	{
		extern int psp_keep_number(const char *prefix, int fallback);
		long n = psp_keep_number("streamrec", 16);

		packets = n == 8 || n == 16 || n == 32 ? n : MAXIMUM_STREAM_PACKETS;
		if (packets != MAXIMUM_STREAM_PACKETS)
			platform_log("audio: the game's stream records hold %ld packets (streamrec%ld), %u bytes each (%u at %d)",
				packets, packets, (unsigned)(stream_bytes(packets) + game_bytes(packets)),
				(unsigned)(stream_bytes(MAXIMUM_STREAM_PACKETS) + game_bytes(MAXIMUM_STREAM_PACKETS)), MAXIMUM_STREAM_PACKETS);
	}
	return (unsigned long)packets;
}

HRESULT WINAPI IDirectSound_CreateSoundStream(LPDIRECTSOUND sound, LPCDSSTREAMDESC description,
	LPDIRECTSOUNDSTREAM *result, LPUNKNOWN outer)
{
	unsigned long packets = stream_record_packets();
	struct psp_stream *stream = memalign(64, stream_bytes(packets) + game_bytes(packets));

	(void)sound;
	(void)outer;
	if (!stream)
		return E_OUTOFMEMORY;
	stream_start(stream, description, packets);
	*result = &stream->object;
	return DS_OK;
}

/* January-era DirectSound exports the game declares itself
(sound_dsound_xbox.c); the XDK 3911 headers no longer carry them */

void __stdcall DirectSoundStopStream(LPDIRECTSOUNDSTREAM stream)
{
	stream_flush(stream);
}

unsigned long __stdcall DirectSoundGetStreamVoiceStatus(LPDIRECTSOUNDSTREAM stream)
{
	struct psp_stream *record = stream_from_interface(stream);
	unsigned long active;

	queue_enter();
	active = GAME_OF(record)->count != 0;
	queue_leave();
	return active;
}

#define STREAM_SETTER(body) \
	struct stream_game *record = GAME_OF(stream_from_interface(stream)); \
	unsigned int guard = set_enter(); \
	body; \
	set_leave(guard); \
	return DS_OK;

HRESULT WINAPI IDirectSoundStream_SetFrequency(LPDIRECTSOUNDSTREAM stream, DWORD frequency)
{
	STREAM_SETTER(record->frequency = frequency ? frequency : record->sample_rate)
}

HRESULT WINAPI IDirectSoundStream_SetVolume(LPDIRECTSOUNDSTREAM stream, LONG volume)
{
	float gain = gain_from_millibels(volume);

	STREAM_SETTER(record->volume = gain)
}

HRESULT WINAPI IDirectSoundStream_SetMixBins(LPDIRECTSOUNDSTREAM stream, DWORD mix_bin_mask)
{
	STREAM_SETTER(
		record->mix_left = (mix_bin_mask & DSMIXBIN_FRONT_LEFT) ? 1.0f : 0.0f;
		record->mix_right = (mix_bin_mask & DSMIXBIN_FRONT_RIGHT) ? 1.0f : 0.0f)
}

/* volumes come in the order of the set bits of the mask */
HRESULT WINAPI IDirectSoundStream_SetMixBinVolumes(LPDIRECTSOUNDSTREAM stream, DWORD mix_bin_mask, const LONG *volumes)
{
	struct stream_game *record = GAME_OF(stream_from_interface(stream));
	unsigned long bit, index = 0;
	float left = 0.0f, right = 0.0f;
	int has_left = 0, has_right = 0;
	unsigned int guard;

	for (bit = 0; bit < 32; bit++)
	{
		if (!(mix_bin_mask & (1UL << bit)))
			continue;
		if ((1UL << bit) == DSMIXBIN_FRONT_LEFT)
		{
			left = gain_from_millibels(volumes[index]);
			has_left = 1;
		}
		else if ((1UL << bit) == DSMIXBIN_FRONT_RIGHT)
		{
			right = gain_from_millibels(volumes[index]);
			has_right = 1;
		}
		index++;
	}
	guard = set_enter();
	if (has_left)
		record->mix_left = left;
	if (has_right)
		record->mix_right = right;
	set_leave(guard);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundStream_SetMode(LPDIRECTSOUNDSTREAM stream, DWORD mode, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->mode = mode)
}

HRESULT WINAPI IDirectSoundStream_SetPosition(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->position[0] = x; record->position[1] = y; record->position[2] = z)
}

HRESULT WINAPI IDirectSoundStream_SetMinDistance(LPDIRECTSOUNDSTREAM stream, FLOAT distance, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->minimum_distance = distance)
}

HRESULT WINAPI IDirectSoundStream_SetMaxDistance(LPDIRECTSOUNDSTREAM stream, FLOAT distance, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->maximum_distance = distance)
}

HRESULT WINAPI IDirectSoundStream_SetI3DL2Source(LPDIRECTSOUNDSTREAM stream, LPCDSI3DL2BUFFER source, DWORD apply)
{
	LONG direct;

	(void)apply;
	/* the low frequency part of the direct path */
	direct = source->lDirect +
		(LONG)(source->Obstruction.lHFLevel * source->Obstruction.flLFRatio) +
		(LONG)(source->Occlusion.lHFLevel * source->Occlusion.flLFRatio);
	if (direct > 0)
		direct = 0;
	{
		/* the high frequencies' loss past the low ones' (the I3DL2 model's
		high-frequency path): what the low-pass takes away */
		LONG high = source->lDirectHF +
			(LONG)(source->Obstruction.lHFLevel * (1.0f - source->Obstruction.flLFRatio)) +
			(LONG)(source->Occlusion.lHFLevel * (1.0f - source->Occlusion.flLFRatio));

		if (high > 0)
			high = 0;
		float direct_gain = gain_from_millibels(direct);

		STREAM_SETTER(record->i3dl2_gain = direct_gain; record->hf_millibels = high)
	}
}

HRESULT WINAPI IDirectSoundStream_Pause(LPDIRECTSOUNDSTREAM stream, DWORD pause)
{
	STREAM_SETTER(record->paused = pause == DSSTREAMPAUSE_PAUSE)
}

HRESULT WINAPI IDirectSoundStream_SetVelocity(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->velocity[0] = x; record->velocity[1] = y; record->velocity[2] = z)
}

HRESULT WINAPI IDirectSoundStream_SetConeAngles(LPDIRECTSOUNDSTREAM stream, DWORD inside, DWORD outside, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->cone_inside = inside; record->cone_outside = outside)
}

HRESULT WINAPI IDirectSoundStream_SetConeOrientation(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->cone_forward[0] = x; record->cone_forward[1] = y; record->cone_forward[2] = z)
}

HRESULT WINAPI IDirectSoundStream_SetConeOutsideVolume(LPDIRECTSOUNDSTREAM stream, LONG volume, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->cone_outside_millibels = volume)
}

/* ---------- buffers

The game's only buffer is a silent looping one that keeps the voice
processor busy; it needs no mixing. */

struct null_buffer
{
	ULONG reference_count;
	LPVOID data;
	DWORD size;
	BOOL playing;
};

HRESULT WINAPI DirectSoundCreateBuffer(LPCDSBUFFERDESC description, LPDIRECTSOUNDBUFFER *result)
{
	struct null_buffer *buffer = calloc(1, sizeof(*buffer));

	(void)description;
	if (!buffer)
		return E_OUTOFMEMORY;
	buffer->reference_count = 1;
	*result = (LPDIRECTSOUNDBUFFER)buffer;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CreateSoundBuffer(LPDIRECTSOUND sound, LPCDSBUFFERDESC description,
	LPDIRECTSOUNDBUFFER *result, LPUNKNOWN outer)
{
	(void)sound;
	(void)outer;
	return DirectSoundCreateBuffer(description, result);
}

ULONG WINAPI IDirectSoundBuffer_Release(LPDIRECTSOUNDBUFFER buffer)
{
	struct null_buffer *record = (struct null_buffer *)buffer;
	ULONG count = --record->reference_count;

	if (!count)
		free(record);
	return count;
}

HRESULT WINAPI IDirectSoundBuffer_SetBufferData(LPDIRECTSOUNDBUFFER buffer, LPVOID data, DWORD size)
{
	struct null_buffer *record = (struct null_buffer *)buffer;

	record->data = data;
	record->size = size;
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Play(LPDIRECTSOUNDBUFFER buffer, DWORD reserved1, DWORD reserved2, DWORD flags)
{
	(void)reserved1;
	(void)reserved2;
	(void)flags;
	((struct null_buffer *)buffer)->playing = TRUE;
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Stop(LPDIRECTSOUNDBUFFER buffer)
{
	((struct null_buffer *)buffer)->playing = FALSE;
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetCurrentPosition(LPDIRECTSOUNDBUFFER buffer, DWORD play_cursor) { (void)buffer; (void)play_cursor; return DS_OK; }
HRESULT WINAPI IDirectSoundBuffer_SetLoopRegion(LPDIRECTSOUNDBUFFER buffer, DWORD loop_start, DWORD loop_length) { (void)buffer; (void)loop_start; (void)loop_length; return DS_OK; }
HRESULT WINAPI IDirectSoundBuffer_SetPitch(LPDIRECTSOUNDBUFFER buffer, LONG pitch) { (void)buffer; (void)pitch; return DS_OK; }
HRESULT WINAPI IDirectSoundBuffer_SetVolume(LPDIRECTSOUNDBUFFER buffer, LONG volume) { (void)buffer; (void)volume; return DS_OK; }

/* ---------- a looping voice from memory (the loading screen's chant)

The Xbox played the chant through looping buffers, which this file leaves as
null objects (above). psp_loading.c plays it on a stream instead: the whole
sound queued as every packet a stream holds, all pointing at the same bytes,
so it loops for as long as any load takes (64 passes, over a minute) without
completions, which nothing runs during a blocking load.

The voices live here rather than on the heap: each load starts them before
the old map is let go and stops them after the new one is in, so four
blocks taken from the heap's holes for the length of a load split the room
the level's large blocks were to have, and a level's stencil bake (one
block of ~160 KB) could then find no hole large enough.
"chantheap": from the heap at each load, as before. */

#define LOOP_VOICES 4
/* whole cache lines each (STREAM_BYTES), as the Media Engine's hand-over
of a voice needs */
static union
{
	struct psp_stream stream;
	unsigned char bytes[STREAM_BYTES + GAME_BYTES];
} loop_voices[LOOP_VOICES] __attribute__((aligned(64)));
static unsigned char loop_voice_used[LOOP_VOICES];

/* an unlinked slot is the Media Engine's until a block that began after the unlink has ended */
static int loop_voice_busy(int k)
{
	unsigned long stamp = GAME_OF(&loop_voices[k].stream)->zombie_stamp;

	return !mixlock_old && stamp && (long)(blocks_ended - stamp) < 0;
}
static int loop_heap = -1;

static int loop_voice_index(const void *voice)
{
	int k;

	for (k = 0; k < LOOP_VOICES; k++)
		if (voice == &loop_voices[k].stream)
			return k;
	return -1;
}

void *psp_dsound_loop_start(const void *adpcm, unsigned long bytes, unsigned long sample_rate,
	unsigned long frequency, long millibels)
{
	WAVEFORMATEX format;
	DSSTREAMDESC description;
	XMEDIAPACKET packet;
	LPDIRECTSOUNDSTREAM stream = NULL;
	int k;

	memset(&format, 0, sizeof(format));
	format.wFormatTag = WAVE_FORMAT_XBOX_ADPCM;
	format.nChannels = 1;
	format.nSamplesPerSec = sample_rate;
	memset(&description, 0, sizeof(description));
	description.lpwfxFormat = &format;
	if (loop_heap < 0)
	{
		extern int psp_keep(const char *word);

		loop_heap = psp_keep("chantheap");
	}
	if (loop_heap)
	{
		if (IDirectSound_CreateSoundStream(NULL, &description, &stream, NULL) != DS_OK)
			return NULL;
	}
	else
	{
		for (k = 0; k < LOOP_VOICES && (loop_voice_used[k] || loop_voice_busy(k)); k++)
			;
		if (k == LOOP_VOICES)
			return NULL;
		loop_voice_used[k] = 1;
		stream_start(&loop_voices[k].stream, &description, MAXIMUM_STREAM_PACKETS);
		stream = (LPDIRECTSOUNDSTREAM)&loop_voices[k].stream.object;
	}
	IDirectSoundStream_SetFrequency(stream, frequency);
	IDirectSoundStream_SetVolume(stream, millibels);
	memset(&packet, 0, sizeof(packet));
	packet.pvBuffer = (LPVOID)adpcm;
	packet.dwMaxSize = bytes;
	for (k = 0; k < MAXIMUM_STREAM_PACKETS; k++)
		stream_process((IDirectSoundStream *)stream, &packet, NULL);
	return stream;
}

void psp_dsound_loop_volume(void *voice, long millibels)
{
	if (voice)
		IDirectSoundStream_SetVolume((LPDIRECTSOUNDSTREAM)voice, millibels);
}

void psp_dsound_loop_stop(void *voice)
{
	int k;

	if (!voice)
		return;
	{
		struct psp_stream *stream = stream_from_interface((IDirectSoundStream *)voice);

		platform_log("audio: a loop voice stopped after %lu ms mixed in %lu blocks prepared (%s, frequency %lu, gain %d at the end, %d at its loudest, of %d)",
			stream->mixed_frames * 1000 / OUTPUT_RATE, stream->prepared_blocks, loop_voice_index(voice) < 0 ? "heap" : "static slot",
			(unsigned long)GAME_OF(stream)->frequency, stream->target_left, stream->peak_gain, GAIN_ONE);
	}
	DirectSoundStopStream((LPDIRECTSOUNDSTREAM)voice);
	k = loop_voice_index(voice);
	if (k < 0)
	{
		stream_release((IDirectSoundStream *)voice);
		return;
	}
	stream_unlink(&loop_voices[k].stream);
	loop_voice_used[k] = 0;
}

/* SNDPERMAP (keep word "sndpermap", opt-in): the sound cache sized per map.
The block taken at start-up stays the boot size (the largest any map needs);
each map's open sizes the engine's cache to its own pages, and the main-RAM
pages it doesn't use, the main tier's end, are spare for that map: the
sound-wait census (soundsweep3, sndwait) found no sound held back at these
sizes (a10 held a computer ambience 3 s at 1,536 on the ladder, sndpermap1, so it keeps
the full cache). The eDRAM tier keeps its pages, so a smaller cache gives up main RAM
only. A taker of the spare uses it for the map alone and lets it go before the
next map's open, which may grow the cache back over it */
static long map_pages = 0, boot_pages = 0;
static unsigned long tier_main_boot = 0;

static long sndpermap_pages(const char *map)
{
	static const struct { const char *map; long kb; } sizes[] = {
		{ "d20", 1792 }, { "a10", 1792 },
		{ "a50", 1536 }, { "b30", 1536 }, { "c10", 1536 },
		{ "a30", 1280 }, { "b40", 1280 }, { "c20", 1280 }, { "c40", 1280 }, { "d40", 1280 },
	};
	int k;

	for (k = 0; k < (int)(sizeof(sizes) / sizeof(sizes[0])); k++)
		if (map && strcmp(map, sizes[k].map) == 0)
			return sizes[k].kb >> 2;
	return boot_pages;
}

#if PSP_SNDSTREAM
static long sndstream_map_open(const char *map);
static void sndstream_place(unsigned char *pool, long pages);
static long sndstream_permap_kb(const char *map);
static long sndstream_pool_pages = 0;
static int sndstream_mode;
#endif

/* the engine's page count for the map now opening; sets the main tier's
share, and logs the spare */
long psp_sound_map_pages(const char *map)
{
	static int on = -1;
	long pages;
#if PSP_SNDSTREAM
	long pool;
#endif

	if (!boot_pages)
	{
		boot_pages = (long)(psp_sound_cache_bytes() >> 12);
		tier_main_boot = tier_main_bytes;
	}
	if (on < 0)
		on = psp_keep("sndpermap");
	pages = on ? sndpermap_pages(map) : boot_pages;
	if (pages > boot_pages)
		pages = boot_pages;
#if PSP_SNDSTREAM
	/* the streamed sounds' pool: the top of the cache's main RAM (the main
	tier's, with the tier), the same place for every map, so the cache stops
	short of it */
	pool = sndstream_map_open(map);
	if (on && sndstream_mode == 1)
	{
		long kb = sndstream_permap_kb(map);

		pages = kb ? (kb >> 2) - pool : boot_pages;
		if (pages > boot_pages)
			pages = boot_pages;
	}
	if (pool && pages > boot_pages - pool)
		pages = boot_pages - pool;
#endif
	if (tier_mode > 0 && tier_main_boot)
	{
		unsigned long cut = (unsigned long)(boot_pages - pages) << 12;

		if (cut + 256 * 1024 > tier_main_boot)
		{
			cut = tier_main_boot > 256 * 1024 ? tier_main_boot - 256 * 1024 : 0;
			pages = boot_pages - (long)(cut >> 12);
		}
		tier_main_bytes = tier_main_boot - cut;
		{
			/* the cache's fence between its tiers moves with the main tier's
			end: set at start-up (sound_cache_new) and never since, it let a
			block straddle the main tier's new end, its tail past it in main
			RAM the map doesn't give the cache (lruv_cache.c's fence) */
			extern struct lruv_cache *psp_lruv_fence_cache;
			extern long psp_lruv_fence_page;

			if (psp_lruv_fence_cache)
				psp_lruv_fence_page = (long)(tier_main_bytes >> 12);
		}
	}
#if PSP_SNDSTREAM
	if (pool)
	{
		unsigned long top = tier_mode > 0 ? tier_main_boot : (unsigned long)boot_pages << 12;
		unsigned long used = tier_mode > 0 ? tier_main_bytes : (unsigned long)pages << 12;

		if (top - used >= (unsigned long)pool << 12)
			sndstream_place(cache_base + top - ((unsigned long)pool << 12), pool);
		else
			platform_log("sndstream: refused for %s: the main tier keeps %lu KB (the least it may), with no room for the pool",
				map ? map : "?", used >> 10);
	}
#endif
	if (on && pages != map_pages)
		platform_log("sndpermap: %s's sound cache %ld KB of %ld; %lu KB of main RAM spare at %08x for this map",
			map ? map : "?", pages << 2, boot_pages << 2, (unsigned long)(boot_pages - pages) << 12 >> 10,
			(unsigned int)(cache_base + (tier_mode > 0 ? tier_main_bytes : (unsigned long)pages << 12)));
	map_pages = pages;
	return pages;
}

long psp_sound_pages_now(void)
{
	return map_pages ? map_pages : (long)(psp_sound_cache_bytes() >> 12);
}

/* the map's spare main RAM in the sound block (none without sndpermap) */
void *psp_sound_map_spare(unsigned long *bytes)
{
	unsigned long spare = map_pages && boot_pages > map_pages ? (unsigned long)(boot_pages - map_pages) << 12 : 0;

#if PSP_SNDSTREAM
	/* the streamed sounds' pool is the block's top */
	spare = spare > (unsigned long)sndstream_pool_pages << 12 ? spare - ((unsigned long)sndstream_pool_pages << 12) : 0;
#endif
	if (edram_mode > 0) /* the cache whole in the eDRAM: no main RAM to spare */
		spare = 0;
	if (bytes)
		*bytes = spare;
	if (!spare || !cache_base)
		return NULL;
	return cache_base + (tier_mode > 0 ? tier_main_bytes : (unsigned long)map_pages << 12);
}

#if PSP_SNDSTREAM
/* ---------- SNDSTREAMLONG: the reader (docs/plan_sndstream.md; keep words
"sndstream", and "sndstreamcheck" for the proof)

Which sounds: permutations of the ambient classes (33-35) of 32 KB or more,
in Xbox ADPCM. Such a sound takes a one-page stub in the cache instead of its
samples ("sndstream"); with "sndstreamcheck" it loads whole as before and is
streamed beside, its pieces compared with the cache's copy as they play.

Pieces: a pool at the top of the sound block, the same place for every map,
which the cache stops short of (psp_sound_map_pages). A piece is 8,136 bytes
(whole blocks, mono or stereo) of one sound, keyed by the sound's id and its
number in the samples, so voices playing the same sound share it.

The reader runs once a frame on the game's thread (psp_sound_upload_poll,
from sound_cache_idle, after the frame's packets are queued), through the
stick reader the cache's own loads use (cache_file_read), a few reads in
flight. What each voice needs is read from the game's side of its queue (the
packets not yet played out, up to two pieces ahead a channel), nearest
first. A piece no voice needs (behind every voice playing its sound, or of a
sound gone) is reused, a sound's first piece last; one a read is filling
never is. A stub is loaded (its flag set, so the game plays it) when its
first piece is in. A block whose piece isn't in plays silent and is counted */
#define SNDSTREAM_SOUNDS 128
#define SNDSTREAM_STUB_BYTES 4096
#define SNDSTREAM_MIN_BYTES (32 * 1024)
#define SNDSTREAM_FLIGHT 4
#define SNDSTREAM_NEEDS 96
#define SNDSTREAM_FIRST_KEEP_US 3000000ull

struct sndstream_sound
{
	const void *permutation;
	void *datum;
	unsigned char *loaded; /* a stub's: the engine's flag, set when its first piece is in */
	unsigned long base;    /* its block in the cache */
	unsigned long id;      /* 0: a free entry */
	long tag_index, file_offset, size;
	SceUInt64 made_us;
	short identifier;
	unsigned char stub, first_in;
};
static struct sndstream_sound sndstream_sounds[SNDSTREAM_SOUNDS];
/* 0 off, 1 "sndstream" (stubs), 2 "sndstreamcheck" (the cache keeps the samples) */
static int sndstream_mode = -1;
static unsigned char *sndstream_pool = NULL;
static const void *sndstream_chosen = NULL;
static unsigned long sndstream_next_id = 1, sndstream_polls = 0;
static int snd_big_said = 0;
static int sndstream_flight = 0, sndstream_peak_pieces = 0;
static unsigned long snd_registered = 0, snd_stubs = 0, snd_refused = 0, snd_reads = 0, snd_read_bytes = 0,
	snd_read_failed = 0, snd_read_us = 0, snd_read_worst_us = 0, snd_reads_done = 0, snd_short_polls = 0,
	snd_first_us = 0, snd_first_worst_us = 0, snd_firsts = 0, snd_stub_peak_bytes = 0, snd_maps = 0, snd_stolen = 0,
	snd_unread_firsts = 0, snd_differ_alive = 0, snd_differ_gone = 0, snd_differ_said = 0, snd_need_peak = 0,
	snd_differ_tier = 0, snd_landed_equal = 0, snd_landed_differ = 0, snd_landed_differ_tier = 0, snd_landed_said = 0;
/* the mixer's counts, summed by the audio thread after each block */
static volatile unsigned long snd_blocks = 0, snd_late = 0, snd_checked = 0, snd_mismatched = 0, snd_unaligned = 0;

static int sndstream_on(void)
{
	if (sndstream_mode < 0)
	{
		sndstream_mode = psp_keep("sndstreamcheck") ? 2 : psp_keep("sndstream") ? 1 : 0;
		if (sndstream_mode && psp_sound_edram_mode())
		{
			platform_log("sndstream: refused: the whole sound cache is in the ME's eDRAM (soundedram)");
			sndstream_mode = 0;
		}
		if (sndstream_mode)
		{
			/* sndstreampiecesN sets one pool for every map; without it,
			each map's own (sndstream_map_pieces) */
			int pieces = psp_keep_number("sndstreampieces", -1);

			if (pieces >= 0 && pieces < 8)
				pieces = 8;
			if (pieces > SNDSTREAM_PIECES_MAX)
				pieces = SNDSTREAM_PIECES_MAX;
			sndstream_pieces_kept = pieces;
			sndstream_dialog = psp_keep("sndstreamdialog");
			sndstream_devices = psp_keep("sndstreamdevices");
		}
	}
	return sndstream_mode;
}

/* The pool each map needs: the most pieces wanted at once on its
hands-off run to 300 s, with room over it (sndstream5a, sndstream6: a10 at 8 and
a30 at 24 played late), d20's swamp the
busiest (38 at once with every just-placed sound's first piece held; 24
played late there, 32 didn't). b40, c20 and c40 have no ambient loop long
enough to stream, so they keep the room. A map not listed gets 32. */
static int sndstream_map_pieces(const char *map)
{
	/* devices: the pieces "sndstreamdevices" adds, where its sounds came to
	more than the pool (sndclose4a, at most needed at once: a10 31 of 28, which with 36 still went 46 polls short in sndclose5, so 44; b40
	19 of 12, c20 15 of 12) */
	static const struct { const char *map; int pieces, dialog, devices; } pools[] = {
		{ "d20", 40, 1, 0 }, { "a30", 32, 1, 0 }, { "a10", 16, 1, 16 }, { "a50", 16, 1, 0 }, { "d40", 16, 1, 0 },
		{ "c10", 12, 1, 0 }, { "b30", 8, 1, 0 }, { "b40", 0, 1, 12 }, { "c20", 0, 1, 8 }, { "c40", 0, 1, 0 },
	};
	int k;

	if (sndstream_pieces_kept >= 0)
		return sndstream_pieces_kept;
	for (k = 0; k < (int)(sizeof(pools) / sizeof(pools[0])); k++)
		if (map && strcmp(map, pools[k].map) == 0)
			/* a few lines of dialog at once, each a piece playing and two
			ahead: 8 left c40 short of a piece 47 times (sndstream8) */
			return pools[k].pieces + (sndstream_dialog && pools[k].dialog ? 12 : 0) +
				(sndstream_devices ? pools[k].devices : 0);
	return 32;
}

/* SNDPERMAP's sizes with the sounds streamed: the cache and the pool
together. With the streamed block at 1,536 KB (psp_platform.c), sndcut1b
found these seven maps clean at 1,280 (no refusal, nothing held: b30's
busiest second used 972 KB of its 1,120), while a10 (147 refusals, a computer
ambience never loaded), c10 (138, a sound held 8.9 s) and d20 (no room for its
pool) keep the whole block */
static long sndstream_permap_kb(const char *map)
{
	static const char *const small[] = { "a30", "a50", "b30", "b40", "c20", "c40", "d40" };
	int k;

	for (k = 0; k < (int)(sizeof(small) / sizeof(small[0])); k++)
		if (map && strcmp(map, small[k]) == 0)
			return 1280;
	return 0;
}

int psp_sndstream_wanted(void)
{
	return sndstream_on() == 1;
}

/* a map opens (psp_sound_map_pages, the cache empty): every piece and sound
forgotten; the pool's pages, which the cache gives up (placed once the
cache's own size is settled) */
static long sndstream_map_open(const char *map)
{
	sndstream_pool = NULL;
	sndstream_pool_pages = 0;
	if (!sndstream_on() || !cache_base)
		return 0;
	sndstream_piece_count = sndstream_map_pieces(map);
	if (!sndstream_piece_count)
	{
		platform_log("sndstream: %s streams nothing; no pool", map ? map : "?");
		return 0;
	}
	memset(sndstream_sounds, 0, sizeof(sndstream_sounds));
	sndstream_flight = 0;
	sndstream_chosen = NULL;
	return (long)(((unsigned long)sndstream_piece_count * SNDSTREAM_PIECE_ROOM + 4095) >> 12);
}

static void sndstream_place(unsigned char *pool, long pages)
{
	int k;

	sndstream_pool = pool;
	sndstream_pool_pages = pages;
	for (k = 0; k < sndstream_piece_count; k++)
	{
		struct sndstream_piece *piece = &sndstream_pieces[k];

		piece->state = PIECE_FREE;
		piece->id = 0;
		piece->index = -1;
		piece->buffer = sndstream_pool + (unsigned long)k * SNDSTREAM_PIECE_ROOM;
		piece->done = piece->first = 0;
		piece->flag = &piece->done;
		piece->stamp = 0;
	}
	snd_maps++;
	platform_log("sndstream: %s; a pool of %d pieces (%ld KB) at %08x, the top of the cache's main RAM",
		sndstream_mode == 2 ? "sndstreamcheck (the cache keeps the samples; pieces compared as they play)" :
		"on (stubs)", sndstream_piece_count, pages << 2, (unsigned int)sndstream_pool);
}

static int sndstream_alive(const struct sndstream_sound *sound)
{
	return sound->id && psp_sound_cache_alive(sound->datum, sound->identifier, (void *)sound->permutation, sound->base);
}

/* a free entry, a gone sound's freed for it; -1 when every one is live */
static int sndstream_slot(void)
{
	int k;

	for (k = 0; k < SNDSTREAM_SOUNDS; k++)
		if (!sndstream_sounds[k].id)
			return k;
	for (k = 0; k < SNDSTREAM_SOUNDS; k++)
		if (!sndstream_alive(&sndstream_sounds[k]))
		{
			sndstream_sounds[k].id = 0;
			return k;
		}
	return -1;
}

static struct sndstream_sound *sndstream_find(unsigned long id)
{
	int k;

	for (k = 0; k < SNDSTREAM_SOUNDS; k++)
		if (sndstream_sounds[k].id == id)
			return &sndstream_sounds[k];
	return NULL;
}

/* a piece filled from the stick: marked free first, so no mix takes it while
it changes; its lines dropped, so neither CPU's cache holds old bytes over
what the read puts in RAM */
static void sndstream_read(struct sndstream_piece *piece, const struct sndstream_sound *sound, long index, int first,
	unsigned char *flag)
{
	long bytes = sound->size - index * (long)SNDSTREAM_PIECE_BYTES;

	if (bytes > (long)SNDSTREAM_PIECE_BYTES)
		bytes = (long)SNDSTREAM_PIECE_BYTES;
	piece->state = PIECE_FREE;
	__asm__ __volatile__("" : : : "memory");
	piece->id = sound->id;
	piece->index = index;
	piece->bytes = bytes;
	piece->done = 0;
	if (!flag)
		flag = (unsigned char *)&piece->done;
	*flag = 0;
	piece->flag = flag;
	piece->first = (unsigned char)first;
	piece->stamp = sndstream_polls;
	piece->issued_us = sceKernelGetSystemTimeWide();
	sceKernelDcacheWritebackInvalidateRange((void *)piece->buffer, SNDSTREAM_PIECE_ROOM);
	__asm__ __volatile__("" : : : "memory");
	piece->state = PIECE_READING;
	sndstream_flight++;
	snd_reads++;
	snd_read_bytes += (unsigned long)bytes;
	{
		/* ahead of the level's reads (FIXES_SNDSTREAM #sndurgent) */
		extern int psp_cache_read_urgent_next;

		psp_cache_read_urgent_next = 1;
		cache_file_read(sound->tag_index, sound->file_offset + index * (long)SNDSTREAM_PIECE_BYTES, bytes,
			(void *)piece->buffer, flag, 0);
	}
}

/* the piece to fill: a free one, else the one no voice needs that was needed
longest ago, a sound's first piece last (it is what a sound starting again
plays first); NULL when every piece is needed or being read */
static struct sndstream_piece *sndstream_victim(void)
{
	struct sndstream_piece *best = NULL;
	int k;

	for (k = 0; k < sndstream_piece_count; k++)
	{
		struct sndstream_piece *piece = &sndstream_pieces[k];

		if (piece->state == PIECE_FREE)
			return piece;
		if (piece->state != PIECE_READY || piece->stamp == sndstream_polls)
			continue;
		if (!best || (best->index == 0 && piece->index != 0) ||
			((best->index == 0) == (piece->index == 0) && piece->stamp < best->stamp))
			best = piece;
	}
	return best;
}

/* the classes streamed: the ambient loops (33-35); with "sndstreamdialog" the
dialog (unit 19, scripted 44, 46, 47): long, played once, and the sounds the
cache held back longest (sndstream5a: c40's monitor 32 s); with
"sndstreamdevices" the devices' (26-30): a10's computer ambiences (class 30,
97-189 KB) and c10's c10_captpilot (346 KB, tagged class 26), the large sounds
that still loaded whole (sndclose3) */
static int sndstream_class(int sound_class)
{
	if (sound_class >= 33 && sound_class <= 35)
		return 1;
	if (sndstream_dialog && (sound_class == 19 || sound_class == 44 || sound_class == 46 || sound_class == 47))
		return 1;
	return sndstream_devices && sound_class >= 26 && sound_class <= 30;
}

/* xbox_sound_cache.c, before a sound's block is made: the stub's size for a
sound streamed ("sndstream"), else 0 (it loads whole) */
long psp_sndstream_choose(const void *permutation, int sound_class, int compression, long size)
{
	sndstream_chosen = NULL;
	if (sndstream_pool && size >= 96 * 1024 && snd_big_said < 16 &&
		(compression != 1 || !sndstream_class(sound_class)))
	{
		/* the large sounds that load whole: what a smaller cache must still hold */
		snd_big_said++;
		platform_log("sndstream: %ld KB loads whole (class %d, compression %d)", size >> 10, sound_class, compression);
	}
	if (!sndstream_pool || compression != 1 || size < SNDSTREAM_MIN_BYTES)
		return 0;
	if (!sndstream_class(sound_class))
		return 0;
	if (sndstream_slot() < 0)
	{
		snd_refused++;
		return 0;
	}
	sndstream_chosen = permutation;
	return sndstream_mode == 1 ? SNDSTREAM_STUB_BYTES : 0;
}

/* ... and once it is placed: 1 for a stub (no read of its samples: its first
piece is asked for, and loaded is set when that is in), 0 to read it as before */
int psp_sndstream_placed(const void *permutation, void *address, unsigned char *loaded, void *datum, short identifier,
	long tag_index, long file_offset, long size)
{
	struct sndstream_sound *sound;
	struct sndstream_piece *piece;
	int slot;

	if (!sndstream_chosen || sndstream_chosen != permutation)
		return 0;
	sndstream_chosen = NULL;
	slot = sndstream_slot();
	if (slot < 0)
	{
		/* (choose found it room on this thread just before) */
		if (sndstream_mode != 1)
			return 0;
		*loaded = 2; /* cache_files.h's _cache_file_read_failed: its block holds a page */
		return 1;
	}
	sound = &sndstream_sounds[slot];
	sound->permutation = permutation;
	sound->datum = datum;
	sound->identifier = identifier;
	sound->loaded = loaded;
	sound->base = (unsigned long)address;
	sound->tag_index = tag_index;
	sound->file_offset = file_offset;
	sound->size = size;
	sound->stub = sndstream_mode == 1;
	sound->first_in = 0;
	sound->made_us = sceKernelGetSystemTimeWide();
	sound->id = sndstream_next_id++;
	if (!sndstream_next_id)
		sndstream_next_id = 1;
	snd_registered++;
	if (sound->stub)
		snd_stubs++;
	/* its first piece at once, past the reads-in-flight limit: the game may
	wait for it. With every piece needed, the oldest ready one is taken (a
	voice may play a late block); with none ready, a stub is called loaded at
	once and starts late, rather than the game waiting on a read never made */
	piece = sndstream_victim();
	if (!piece)
	{
		int k;

		for (k = 0; k < sndstream_piece_count; k++)
			if (sndstream_pieces[k].state == PIECE_READY && (!piece || sndstream_pieces[k].stamp < piece->stamp))
				piece = &sndstream_pieces[k];
		if (piece)
			snd_stolen++;
	}
	if (piece)
		sndstream_read(piece, sound, 0, 1, sound->stub ? loaded : NULL);
	else if (sound->stub)
	{
		snd_unread_firsts++;
		sound->first_in = 1;
		*loaded = 1;
	}
	return sound->stub;
}

/* the queueing range check (psp_sound_cache_holds): a stub's samples run past its page */
int psp_sndstream_holds(const void *address)
{
	int k;

	if (!sndstream_pool)
		return 0;
	for (k = 0; k < SNDSTREAM_SOUNDS; k++)
		if (sndstream_sounds[k].id && sndstream_sounds[k].stub && sndstream_sounds[k].base == (unsigned long)address)
			return 1;
	return 0;
}

/* stream_process, as the game queues a packet: its sound's id if streamed,
where it starts in the samples, and whether the cache holds them too */
static unsigned long sndstream_packet(const void *permutation, const unsigned char *data, unsigned long *offset,
	int *check)
{
	int k;

	if (!sndstream_pool || !data)
		return 0;
	for (k = 0; k < SNDSTREAM_SOUNDS; k++)
	{
		const struct sndstream_sound *sound = &sndstream_sounds[k];

		if (sound->id && sound->permutation == permutation && (unsigned long)data >= sound->base &&
			(unsigned long)data - sound->base < (unsigned long)sound->size)
		{
			*offset = (unsigned long)data - sound->base;
			*check = !sound->stub;
			return sound->id;
		}
	}
	return 0;
}

/* the audio thread, after a block: the voice's counts into the totals */
static void sndstream_collect(struct psp_stream *stream)
{
	if (!(stream->snd_blocks | stream->snd_late | stream->snd_checked | stream->snd_mismatched | stream->snd_unaligned))
		return;
	if (stream->snd_mismatched)
	{
		/* whose copy changed: the cache's, when its block no longer holds
		that sound (a channel lets go of its sound while the mixer still has
		packets of it queued, and the cache may then reuse the block); the
		piece's, otherwise */
		const struct sndstream_sound *sound = sndstream_find(stream->snd_differ_id);
		int alive = sound && sndstream_alive(sound);

		if (alive)
		{
			snd_differ_alive += stream->snd_mismatched;
			if (psp_sound_tier_holds((const void *)sound->base))
				snd_differ_tier += stream->snd_mismatched;
		}
		else
			snd_differ_gone += stream->snd_mismatched;
		if (snd_differ_said < 8)
		{
			snd_differ_said++;
			platform_log("sndstream: %lu blocks differed in a block's mix, the first of sound %lu at byte %lu: %s",
				stream->snd_mismatched, stream->snd_differ_id, stream->snd_differ_at,
				alive ? (psp_sound_tier_holds((const void *)sound->base) ? "its cache copy still that sound, in the eDRAM tier" :
				"its cache copy still that sound, in the main tier") :
				sound ? "its cache block no longer that sound's" : "its sound gone from the table");
		}
	}
	snd_blocks += stream->snd_blocks;
	snd_late += stream->snd_late;
	snd_checked += stream->snd_checked;
	snd_mismatched += stream->snd_mismatched;
	snd_unaligned += stream->snd_unaligned;
	stream->snd_blocks = stream->snd_late = stream->snd_checked = stream->snd_mismatched = stream->snd_unaligned = 0;
}

/* sndstreamcheck: a piece as it lands, against the cache's copy, where this
CPU can read it (the main tier; the tier too in the emulator, whose eDRAM is
mapped), once that copy is loaded: whether the read itself brought the right
bytes, apart from anything that happens to either copy later */
static void sndstream_landed(const struct sndstream_piece *piece)
{
	const struct sndstream_sound *sound = sndstream_find(piece->id);
	const unsigned char *copy;
	int tier;

	if (!sound || !sndstream_alive(sound) || *sound->loaded != 1)
		return;
	copy = (const unsigned char *)sound->base + piece->index * (long)SNDSTREAM_PIECE_BYTES;
	tier = psp_sound_tier_holds(copy);
	if (tier && tier_mode != 2)
		return;
	if (memcmp(copy, piece->buffer, (size_t)piece->bytes) == 0)
	{
		snd_landed_equal++;
		return;
	}
	snd_landed_differ++;
	snd_landed_differ_tier += tier;
	if (snd_landed_said < 8)
	{
		long k = 0;

		while (k < piece->bytes && copy[k] == piece->buffer[k])
			k++;
		snd_landed_said++;
		platform_log("sndstream: piece %ld of sound %lu (%ld bytes, file offset %ld + %ld) differs as it lands from its cache copy (%s) from byte %ld",
			piece->index, piece->id, piece->bytes, sound->file_offset, piece->index * (long)SNDSTREAM_PIECE_BYTES,
			tier ? "in the eDRAM tier" : "in the main tier", k);
	}
}

struct sndstream_need
{
	unsigned long id;
	long index, ahead;
};

static void sndstream_poll(void)
{
	static struct sndstream_need needs[SNDSTREAM_NEEDS];
	SceUInt64 now;
	struct psp_stream *stream;
	int k, count = 0, in_use = 0;
	unsigned long stub_bytes = 0;

	if (!sndstream_pool)
		return;
	sndstream_polls++;
	now = sceKernelGetSystemTimeWide();
	/* reads done: a piece is ready once its bytes are in RAM */
	for (k = 0; k < sndstream_piece_count; k++)
	{
		struct sndstream_piece *piece = &sndstream_pieces[k];
		struct sndstream_sound *sound;
		unsigned long us;

		unsigned char done;

		if (piece->state != PIECE_READING || !*piece->flag)
			continue;
		done = *piece->flag;
		sndstream_flight--;
		us = (unsigned long)(now - piece->issued_us);
		snd_read_us += us;
		if (us > snd_read_worst_us)
			snd_read_worst_us = us;
		snd_reads_done++;
		if (done == 1)
		{
			sceKernelDcacheWritebackInvalidateRange((void *)piece->buffer, SNDSTREAM_PIECE_ROOM);
			__asm__ __volatile__("sync" : : : "memory");
			piece->state = PIECE_READY;
			piece->flag = &piece->done;
			if (sndstream_mode == 2)
				sndstream_landed(piece);
		}
		else
		{
			snd_read_failed++;
			piece->id = 0;
			piece->state = PIECE_FREE;
		}
		if (!piece->first)
			continue;
		piece->first = 0;
		sound = sndstream_find(piece->id);
		if (sound && !sound->first_in)
		{
			sound->first_in = 1;
			us = (unsigned long)(now - sound->made_us);
			snd_firsts++;
			snd_first_us += us;
			if (us > snd_first_worst_us)
				snd_first_worst_us = us;
		}
	}
	/* what the voices need: each queued packet not yet played out, nearest first */
	for (stream = streams; stream; stream = GAME_OF(stream)->next)
	{
		struct stream_game *game = GAME_OF(stream);
		unsigned long budget = 2 * SNDSTREAM_PIECE_BYTES * (stream->channels ? stream->channels : 1), ahead = 0, j;
		unsigned long finished_to = game->finished_to;

		/* the mixer's last piece kept only while this voice still has a
		streamed packet to play: a voice keeps its hint after its sound ends,
		and the game keeps its voices, so stamping every voice's hint pinned
		one piece per voice that had ever streamed, and once as many voices
		had streamed as the pool has pieces no piece could be taken (B86:
		c20's 12 of 12 in use with at most 5 needed, reads stopped, silence) */
		for (j = 0; j < game->count; j++)
			if (game->entry[(game->head + j) & stream->packet_mask].snd_id && game->popped + j >= finished_to)
				break;
		if (j < game->count && stream->snd_hint >= 0 && stream->snd_hint < sndstream_piece_count)
			sndstream_pieces[stream->snd_hint].stamp = sndstream_polls;
		for (j = 0; j < game->count && ahead < budget; j++)
		{
			unsigned long slot = (game->head + j) & stream->packet_mask, id = game->entry[slot].snd_id;
			unsigned long size = game->entry[slot].packet.dwMaxSize;

			if (game->popped + j < finished_to)
				continue;
			if (id)
			{
				unsigned long from = game->entry[slot].snd_offset, to = from + size, at;

				for (at = from; at < to && ahead + (at - from) < budget;
					at = (at / SNDSTREAM_PIECE_BYTES + 1) * SNDSTREAM_PIECE_BYTES)
				{
					long index = (long)(at / SNDSTREAM_PIECE_BYTES);
					int n, have = 0;

					for (n = 0; n < count; n++)
						if (needs[n].id == id && needs[n].index == index)
						{
							have = 1;
							if (needs[n].ahead > (long)(ahead + (at - from)))
								needs[n].ahead = (long)(ahead + (at - from));
							break;
						}
					if (!have && count < SNDSTREAM_NEEDS)
					{
						needs[count].id = id;
						needs[count].index = index;
						needs[count].ahead = (long)(ahead + (at - from));
						count++;
					}
				}
			}
			ahead += size;
		}
	}
	/* a sound just placed keeps its first piece a while: the game starts it
	within a frame or two of asking */
	for (k = 0; k < SNDSTREAM_SOUNDS; k++)
	{
		struct sndstream_sound *sound = &sndstream_sounds[k];

		if (!sound->id)
			continue;
		if (!sndstream_alive(sound))
		{
			sound->id = 0;
			continue;
		}
		if (sound->stub)
			stub_bytes += (unsigned long)sound->size;
		if ((!sound->first_in || now - sound->made_us < SNDSTREAM_FIRST_KEEP_US) && count < SNDSTREAM_NEEDS)
		{
			needs[count].id = sound->id;
			needs[count].index = 0;
			needs[count].ahead = 0;
			count++;
		}
	}
	if (stub_bytes > snd_stub_peak_bytes)
		snd_stub_peak_bytes = stub_bytes;
	if ((unsigned long)count > snd_need_peak)
		snd_need_peak = (unsigned long)count;
	/* the pieces there, kept; the rest read, nearest first */
	for (k = 0; k < count; k++)
	{
		int n;

		for (n = 0; n < sndstream_piece_count; n++)
			if (sndstream_pieces[n].state != PIECE_FREE && sndstream_pieces[n].id == needs[k].id &&
				sndstream_pieces[n].index == needs[k].index)
			{
				sndstream_pieces[n].stamp = sndstream_polls;
				needs[k].id = 0;
				break;
			}
	}
	while (sndstream_flight < SNDSTREAM_FLIGHT)
	{
		struct sndstream_piece *piece;
		struct sndstream_sound *sound;
		int best = -1;

		for (k = 0; k < count; k++)
			if (needs[k].id && (best < 0 || needs[k].ahead < needs[best].ahead))
				best = k;
		if (best < 0)
			break;
		sound = sndstream_find(needs[best].id);
		if (!sound || needs[best].index * (long)SNDSTREAM_PIECE_BYTES >= sound->size)
		{
			needs[best].id = 0;
			continue;
		}
		piece = sndstream_victim();
		if (!piece)
		{
			snd_short_polls++;
			break;
		}
		{
			int first = needs[best].index == 0 && !sound->first_in;

			/* a stub's first piece read again (its first read had no piece)
			completes into the engine's flag, as the first one does */
			sndstream_read(piece, sound, needs[best].index, first, first && sound->stub ? sound->loaded : NULL);
		}
		needs[best].id = 0;
	}
	for (k = 0; k < sndstream_piece_count; k++)
		in_use += sndstream_pieces[k].state != PIECE_FREE;
	if (in_use > sndstream_peak_pieces)
		sndstream_peak_pieces = in_use;
}

static void sndstream_report(void)
{
	int k, sounds = 0, stubs = 0, in_use = 0;
	unsigned long stub_bytes = 0;

	if (!sndstream_mode || sndstream_mode < 0)
		return;
	for (k = 0; k < SNDSTREAM_SOUNDS; k++)
		if (sndstream_sounds[k].id)
		{
			sounds++;
			if (sndstream_sounds[k].stub)
			{
				stubs++;
				stub_bytes += (unsigned long)sndstream_sounds[k].size;
			}
		}
	for (k = 0; k < sndstream_piece_count; k++)
		in_use += sndstream_pieces[k].state != PIECE_FREE;
	/* two lines: the log cuts a line at about 1 KB */
	platform_log("sndstream: %s; late blocks %lu, blocks from pieces %lu, checked %lu equal, %lu differed "
		"(%lu with the cache's copy that sound's, %lu of them in the tier; %lu not), %lu unaligned; "
		"pieces as they landed against the cache's copy: %lu equal, %lu differed (%lu in the tier)",
		sndstream_mode == 2 ? "sndstreamcheck" : "on", snd_late, snd_blocks, snd_checked, snd_mismatched,
		snd_differ_alive, snd_differ_tier, snd_differ_gone, snd_unaligned,
		snd_landed_equal, snd_landed_differ, snd_landed_differ_tier);
	platform_log("sndstream: %d sounds streamed now, %d of them stubs standing for %lu KB (peak %lu KB); "
		"%lu registered, %lu stubs made, %lu refused (table full); pieces %d of %d in use (peak %d, at most %lu needed at once), "
		"%lu polls short of a piece; reads %lu (%lu KB, %lu failed, %lu in flight, mean %lu us, worst %lu us); "
		"first pieces %lu (mean %lu us, worst %lu us; %lu taken from a voice, %lu not read)",
		sounds, stubs, stub_bytes / 1024, snd_stub_peak_bytes / 1024,
		snd_registered, snd_stubs, snd_refused, in_use, sndstream_piece_count, sndstream_peak_pieces, snd_need_peak,
		snd_short_polls, snd_reads, snd_read_bytes / 1024, snd_read_failed, (unsigned long)sndstream_flight,
		snd_reads_done ? snd_read_us / snd_reads_done : 0, snd_read_worst_us,
		snd_firsts, snd_firsts ? snd_first_us / snd_firsts : 0, snd_first_worst_us, snd_stolen, snd_unread_firsts);
}
#endif
