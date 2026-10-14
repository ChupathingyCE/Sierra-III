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
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);

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
	XMEDIAPACKET packet;
	const unsigned char *data;
	unsigned long frames;
	BOOL finished;            /* played out by the mixer, not yet completed */
};

struct psp_stream
{
	/* must be first: in C an IDirectSoundStream is just { lpVtbl } */
	IDirectSoundStream object;
	struct psp_stream *next;
	ULONG reference_count;
	LPFNXMEDIAOBJECTCALLBACK callback;
	LPVOID context;

	/* format */
	BOOL adpcm;
	unsigned long channels;
	DWORD sample_rate;
	DWORD frequency;

	BOOL paused;

	/* 2D gains */
	float volume;             /* SetVolume */
	float mix_left, mix_right;

	/* 3D */
	BOOL has_3d;
	DWORD mode;
	float position[3];
	float minimum_distance, maximum_distance;
	float i3dl2_gain;

	struct voice_packet packets[MAXIMUM_STREAM_PACKETS];
	unsigned long packet_head;
	unsigned long packet_count;
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
	the game then queues more (Sheepdog01's b86 run 2: regular breaks heard with
	either mixer, the output never late) */
	unsigned long dry_frames;
};
/* build 86: a voice takes whole 64-byte cache lines (memalign, the size
rounded up): the Media Engine writes voices back a cache line at a time, and
a line shared with other data would carry stale copies of its bytes */
#define STREAM_BYTES ((sizeof(struct psp_stream) + 63) & ~(size_t)63)
void *memalign(size_t alignment, size_t size);

static pthread_mutex_t mixer_lock = PTHREAD_MUTEX_INITIALIZER;
static struct psp_stream *streams;

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
/* build 40 (Sheepdog01: sound cutting out in the heaviest fighting): which of
three it is. The worst block's mixing time and the longest wait for the
mixer's lock (the game holds it to start and change sounds); blocks that
reached the output late (a gap heard); and blocks that hit the 16-bit limit
(distortion, not a gap) */
/* build 87: voices that ran dry and were then fed more (a gap heard) */
static unsigned long dry_gaps = 0, dry_gap_frames = 0, dry_gap_worst = 0;
static unsigned long cut_sounds = 0, mix_worst_us = 0, lock_worst_us = 0, late_blocks = 0, clipped_blocks = 0, gap_worst_us = 0;

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

static inline int ima_expand(int nibble, int *predictor, int *index)
{
	int step = ima_step_table[*index];
	int difference = step >> 3;

	if (nibble & 1) difference += step >> 2;
	if (nibble & 2) difference += step >> 1;
	if (nibble & 4) difference += step;
	if (nibble & 8) difference = -difference;
	*predictor += difference;
	if (*predictor > 32767) *predictor = 32767;
	if (*predictor < -32768) *predictor = -32768;
	*index += ima_index_table[nibble];
	if (*index < 0) *index = 0;
	if (*index > 88) *index = 88;
	return *predictor;
}

/* one Xbox ADPCM block of a packet into the stream's decoded block: per
channel a 4-byte header (predictor, step index), then 4-byte groups of
eight nibbles, low nibble first, alternating between channels; 64 samples
per channel. The decoded block is always two channels (a mono one twice) */
static void decode_block(struct psp_stream *stream, const struct voice_packet *packet, long block)
{
	unsigned long channels = stream->channels;
	const unsigned char *data = packet->data + block * XBOX_ADPCM_BLOCK_BYTES * channels;
	unsigned long channel, group, byte;

	for (channel = 0; channel < channels; channel++)
	{
		const unsigned char *header = data + channel * 4;
		int predictor = (short)(header[0] | (header[1] << 8));
		int index = header[2] > 88 ? 88 : header[2];
		short *output = stream->decoded[block & 1] + channel;

		for (group = 0; group < 8; group++)
		{
			const unsigned char *nibbles = data + 4 * channels + (group * channels + channel) * 4;

			for (byte = 0; byte < 4; byte++)
			{
				unsigned long sample = group * 8 + byte * 2;

				output[2 * sample] = (short)ima_expand(nibbles[byte] & 0xf, &predictor, &index);
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

static void spatialize(const struct psp_stream *stream, float *left, float *right)
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
		*left = cosf(angle);
		*right = sinf(angle);
	}
	*left *= attenuation * stream->i3dl2_gain;
	*right *= attenuation * stream->i3dl2_gain;
}

static int gain_to_fixed(float gain)
{
	int value = (int)(gain * GAIN_ONE);

	return value < 0 ? 0 : value > 2 * GAIN_ONE ? 2 * GAIN_ONE : value;
}

static void voice_gains(const struct psp_stream *stream, int *left, int *right)
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
			struct voice_packet *candidate = &stream->packets[(stream->packet_head + position) % MAXIMUM_STREAM_PACKETS];

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

/* build 86: a voice's step and gains for the next block, on the main CPU
(floats and its maths library); 0 when it won't play (paused, nothing queued) */
static int prepare_voice(struct psp_stream *stream)
{
	if (stream->paused || !stream->packet_count || !stream->sample_rate)
		return 0;
	stream->step = (unsigned long)(((unsigned long long)(stream->frequency ? stream->frequency : stream->sample_rate) << 16) /
		OUTPUT_RATE);
	voice_gains(stream, &stream->target_left, &stream->target_right);
	return 1;
}

/* mixes one prepared voice into output (frames of stereo 32-bit sums);
returns whether it played. Integers only, no calls out of this file: the
Media Engine runs it (build 86) */
static int mix_voice(struct psp_stream *stream, int *output, unsigned long frames)
{
	unsigned long step = stream->step, done = 0;
	int target_left = stream->target_left, target_right = stream->target_right, left, right, ramp_left, ramp_right;

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
	if (!target_left && !target_right && !left && !right)
	{
		/* silent: just advance. Build 89: not past the end of what's queued
		(a silent voice that had run dry kept counting, so the start of what
		the game queued next was skipped) */
		unsigned long long advance;

		if (!current_packet(stream))
			return 0;
		advance = (unsigned long long)step * frames + stream->fraction;

		stream->frame += (unsigned long)(advance >> 16);
		stream->fraction = (unsigned long)(advance & 0xFFFF);
		current_packet(stream);
		return 0;
	}
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
	if (done < frames)
		stream->dry_frames += frames - done;
	return 1;
}

/* build 86: one block of the listed (prepared) voices into output, through
sums (frames of stereo 32-bit sums); returns the voices heard, and whether
any sample hit the 16-bit limit. The same code on either CPU: the main
CPU's mix below, and the Media Engine's job (psp_meaudio.c). No library
calls (the zeroing is a loop, not memset) */
__attribute__((optimize("no-tree-loop-distribute-patterns")))
unsigned long psp_dsound_mix_list(void *const *list, unsigned long count, int *sums, short *output,
	unsigned long frames, unsigned long *clipped)
{
	unsigned long sample, k, voices = 0;
	int over = 0;

	for (sample = 0; sample < frames * OUTPUT_CHANNELS; sample++)
		sums[sample] = 0;
	for (k = 0; k < count; k++)
		voices += mix_voice((struct psp_stream *)list[k], sums, frames);
	for (sample = 0; sample < frames * OUTPUT_CHANNELS; sample++)
	{
		int value = sums[sample];

		if (value > 32767 || value < -32768)
			over = 1;
		output[sample] = (short)(value > 32767 ? 32767 : value < -32768 ? -32768 : value);
	}
	*clipped = over;
	return voices;
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

static void mix(short *output, unsigned long frames)
{
	static int sums[MIX_FRAMES * OUTPUT_CHANNELS] __attribute__((aligned(64)));
	static struct psp_stream *list[VOICE_LIST_MAX];
	struct psp_stream *stream;
	unsigned long voices = 0, count = 0, clipped = 0, me_us = 0, sc_us;
	int on_me = me_wanted && psp_meaudio_running(), mixed = 0;
	SceUInt64 start = sceKernelGetSystemTimeWide();

	pthread_mutex_lock(&mixer_lock);
	{
		unsigned long waited = (unsigned long)(sceKernelGetSystemTimeWide() - start);

		if (waited > lock_worst_us)
			lock_worst_us = waited;
	}
	for (stream = streams; stream; stream = stream->next)
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
		list[count++] = stream;
	}
	if (on_me)
	{
		unsigned long k;

		/* everything the ME reads, from this CPU's cache to RAM; no copy of
		the output block left here to be written over the ME's */
		sceKernelDcacheWritebackAll();
		sceKernelDcacheWritebackInvalidateRange(output, frames * OUTPUT_CHANNELS * sizeof(short));
		mixed = psp_meaudio_run((void *const *)list, count, output, frames, &voices, &clipped, &me_us);
		/* what the ME changed, dropped from this cache (whole lines: see
		STREAM_BYTES; nothing here wrote to them, the lock is held) */
		for (k = 0; k < count; k++)
			sceKernelDcacheInvalidateRange(list[k], STREAM_BYTES);
		sceKernelDcacheInvalidateRange(output, frames * OUTPUT_CHANNELS * sizeof(short));
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
		voices = psp_dsound_mix_list((void *const *)list, count, sums, output, frames, &clipped);
	pthread_mutex_unlock(&mixer_lock);
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
}

/* ---------- output */

static short output_buffers[2][MIX_FRAMES * OUTPUT_CHANNELS] __attribute__((aligned(64)));
static int audio_started = 0;

static int audio_thread(SceSize arguments, void *argument)
{
	int channel = sceAudioSRCChReserve(MIX_FRAMES, OUTPUT_RATE, 2), which = 0;
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
	if (channel < 0)
		platform_log("audio: no output channel (%08x); sound runs silent", channel);
	else
		platform_log("audio: %d Hz stereo, %d samples a block (%d ms)", OUTPUT_RATE, MIX_FRAMES,
			MIX_FRAMES * 1000 / OUTPUT_RATE);
	{
		extern int psp_keep(const char *word);

		/* keep.txt's "audiodump": the first 20 s of the mix into ms0:/halo/audio.raw
		(22,050 Hz stereo 16-bit), to check the mixer off the PSP */
		if (psp_keep("audiodump"))
			dump = sceIoOpen("ms0:/halo/audio.raw", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
	}
	for (;;)
	{
		mix(output_buffers[which], MIX_FRAMES);
		if (dump >= 0)
		{
			sceIoWrite(dump, output_buffers[which], sizeof(output_buffers[which]));
			dumped += MIX_FRAMES;
			if (dumped >= 20 * OUTPUT_RATE)
			{
				sceIoClose(dump);
				dump = -1;
			}
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

			sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX, output_buffers[which]);
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
		which ^= 1;
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
	/* build 86: the launch option meaudio: the Media Engine mixes */
	psp_meaudio_start();
	{
		extern int psp_keep(const char *word);

		/* nomeaudio with the ME started for other jobs (meslant): the main
		CPU still mixes, so an A/B of those jobs leaves audio where it was */
		if (psp_keep("nomeaudio"))
			me_wanted = 0;
	}
	/* above the game's thread, which it interrupts for a moment every 23 ms */
	thread = sceKernelCreateThread("halo audio", audio_thread, 0x12, 0x4000, 0, NULL);
	if (thread >= 0)
		sceKernelStartThread(thread, 0, NULL);
}

/* the mixer's cost, for the heartbeat log */
void psp_audio_report(void)
{
	if (!mix_blocks)
		return;
	unsigned long block_us = MIX_FRAMES * 1000000 / OUTPUT_RATE, average = mix_microseconds / mix_blocks;
	unsigned long permille = average * 1000 / block_us;

	platform_log("audio: mixing %lu us a block of %lu us (%lu.%lu%% of the CPU), %lu.%lu voices on average, %lu at most; worst block %lu us, lock wait %lu us; %lu of %lu blocks late (longest gap %lu us), %lu at the 16-bit limit; sounds stopped by the game before their end %lu (some normally: a trigger let go)",
		average, block_us, permille / 10, permille % 10,
		mix_voice_blocks / mix_blocks, mix_voice_blocks * 10 / mix_blocks % 10, mix_peak_voices,
		mix_worst_us, lock_worst_us, late_blocks, mix_blocks, gap_worst_us, clipped_blocks, cut_sounds);
	if (me_blocks || me_failures || psp_meaudio_running())
		platform_log("audio: Media Engine mixed %lu of the blocks (menu: %s), waited for %lu us a block on average, %lu at most (main CPU asleep meanwhile); %lu failed; %lu over the voice list",
			me_blocks, me_wanted ? "ME" : "main CPU", me_blocks ? me_wait_us / me_blocks : 0, me_wait_worst_us, me_failures,
			me_overflows);
	platform_log("audio: the game waited for the mixer's lock %lu times, %lu us in all, %lu at most; voices that ran dry and were then fed more (a gap heard) %lu, %lu ms of silence in all, the longest %lu ms",
		game_lock_waits, game_lock_wait_us, game_lock_worst_us, dry_gaps, dry_gap_frames * 1000 / OUTPUT_RATE,
		dry_gap_worst * 1000 / OUTPUT_RATE);
	dry_gaps = dry_gap_frames = dry_gap_worst = 0;
	game_lock_waits = game_lock_wait_us = game_lock_worst_us = 0;
	me_blocks = me_wait_us = me_wait_worst_us = me_failures = me_overflows = 0;
	mix_blocks = mix_microseconds = mix_voice_blocks = mix_peak_voices = 0;
	mix_worst_us = lock_worst_us = late_blocks = clipped_blocks = gap_worst_us = cut_sounds = 0;
}

/* ---------- completion */

/* completes the head packet; called with the lock held, which the game's
callback runs without */
static void stream_complete_head(struct psp_stream *stream, DWORD status, DWORD completed_size)
{
	struct voice_packet *entry = &stream->packets[stream->packet_head];
	XMEDIAPACKET packet = entry->packet;

	if (stream->decoded_packet[0] == entry)
		stream->decoded_packet[0] = NULL;
	if (stream->decoded_packet[1] == entry)
		stream->decoded_packet[1] = NULL;
	entry->data = NULL;
	entry->finished = FALSE;
	stream->packet_head = (stream->packet_head + 1) % MAXIMUM_STREAM_PACKETS;
	stream->packet_count--;
	if (packet.pdwCompletedSize)
		*packet.pdwCompletedSize = completed_size;
	if (packet.pdwStatus)
		*packet.pdwStatus = status;
	if (stream->callback)
	{
		pthread_mutex_unlock(&mixer_lock);
		stream->callback(stream->context, packet.pContext, status);
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

	game_lock();
	for (stream = streams; stream; stream = stream->next)
	{
		while (stream->packet_count && stream->packets[stream->packet_head].finished)
			stream_complete_head(stream, XMEDIAPACKET_STATUS_SUCCESS, stream->packets[stream->packet_head].packet.dwMaxSize);
	}
	pthread_mutex_unlock(&mixer_lock);
}

/* ---------- stream interface */

static struct psp_stream *stream_from_interface(void *stream)
{
	return (struct psp_stream *)stream;
}

static ULONG STDMETHODCALLTYPE stream_add_reference(IDirectSoundStream *object)
{
	struct psp_stream *stream = stream_from_interface(object);
	ULONG count;

	game_lock();
	count = ++stream->reference_count;
	pthread_mutex_unlock(&mixer_lock);
	return count;
}

static HRESULT STDMETHODCALLTYPE stream_flush(IDirectSoundStream *object);

static ULONG STDMETHODCALLTYPE stream_release(IDirectSoundStream *object)
{
	struct psp_stream *stream = stream_from_interface(object);
	struct psp_stream **link;
	ULONG count;

	game_lock();
	count = --stream->reference_count;
	pthread_mutex_unlock(&mixer_lock);
	if (count)
		return count;

	stream_flush(object);
	game_lock();
	for (link = &streams; *link; link = &(*link)->next)
	{
		if (*link == stream)
		{
			*link = stream->next;
			break;
		}
	}
	pthread_mutex_unlock(&mixer_lock);
	free(stream);
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

	game_lock();
	*status = stream->packet_count < MAXIMUM_STREAM_PACKETS ? XMO_STATUSF_ACCEPT_INPUT_DATA : 0;
	pthread_mutex_unlock(&mixer_lock);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_process(IDirectSoundStream *object, LPCXMEDIAPACKET input, LPCXMEDIAPACKET output)
{
	struct psp_stream *stream = stream_from_interface(object);
	struct voice_packet *entry;
	unsigned long frames;

	(void)output;
	if (!input)
		return E_INVALIDARG;
	/* whole frames only: whole ADPCM blocks, whole PCM frames */
	frames = stream->adpcm ?
		input->dwMaxSize / (XBOX_ADPCM_BLOCK_BYTES * stream->channels) * XBOX_ADPCM_BLOCK_SAMPLES :
		input->dwMaxSize / (2 * stream->channels);
	game_lock();
	if (stream->packet_count == MAXIMUM_STREAM_PACKETS)
	{
		pthread_mutex_unlock(&mixer_lock);
		return E_OUTOFMEMORY;
	}
	entry = &stream->packets[(stream->packet_head + stream->packet_count) % MAXIMUM_STREAM_PACKETS];
	entry->packet = *input;
	entry->data = (const unsigned char *)input->pvBuffer;
	entry->frames = entry->data ? frames : 0;
	entry->finished = FALSE;
	if (input->pdwStatus)
		*input->pdwStatus = XMEDIAPACKET_STATUS_PENDING;
	if (input->pdwCompletedSize)
		*input->pdwCompletedSize = 0;
	if (stream->packet_count && stream->dry_frames)
	{
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
	pthread_mutex_unlock(&mixer_lock);
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

	game_lock();
	/* build 43: a sound stopped by the game before it finished playing (a
	voice taken for a newer sound, or a sound the game gave up on): the
	heard cut-offs, if the output itself never runs late */
	if (stream->packet_count && !stream->packets[stream->packet_head].finished)
		cut_sounds++;
	while (stream->packet_count)
	{
		struct voice_packet *head = &stream->packets[stream->packet_head];

		stream_complete_head(stream, head->finished ? XMEDIAPACKET_STATUS_SUCCESS : XMEDIAPACKET_STATUS_FLUSHED,
			head->finished ? head->packet.dwMaxSize : 0);
	}
	stream->frame = 0;
	stream->fraction = 0;
	stream->dry_frames = 0;
	stream->decoded_packet[0] = stream->decoded_packet[1] = NULL;
	pthread_mutex_unlock(&mixer_lock);
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
	game_lock();
	listener.distance_factor = factor > 0.0f ? factor : 1.0f;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetRolloffFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	game_lock();
	listener.rolloff_factor = factor >= 0.0f ? factor : 1.0f;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetPosition(LPDIRECTSOUND sound, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)sound;
	(void)apply;
	game_lock();
	listener.position[0] = x;
	listener.position[1] = y;
	listener.position[2] = z;
	pthread_mutex_unlock(&mixer_lock);
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
	game_lock();
	listener.front[0] = x_front;
	listener.front[1] = y_front;
	listener.front[2] = z_front;
	listener.top[0] = x_top;
	listener.top[1] = y_top;
	listener.top[2] = z_top;
	normalize3(listener.front);
	normalize3(listener.top);
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CreateSoundStream(LPDIRECTSOUND sound, LPCDSSTREAMDESC description,
	LPDIRECTSOUNDSTREAM *result, LPUNKNOWN outer)
{
	struct psp_stream *stream = memalign(64, STREAM_BYTES);
	const WAVEFORMATEX *format = description->lpwfxFormat;

	(void)sound;
	(void)outer;
	if (!stream)
		return E_OUTOFMEMORY;
	memset(stream, 0, STREAM_BYTES);
	stream->object.lpVtbl = &stream_vtable;
	stream->reference_count = 1;
	stream->callback = description->lpfnCallback;
	stream->context = description->lpvContext;
	stream->adpcm = format && format->wFormatTag == WAVE_FORMAT_XBOX_ADPCM;
	stream->channels = format && format->nChannels == 2 ? 2 : 1;
	stream->sample_rate = format ? format->nSamplesPerSec : 0;
	stream->frequency = stream->sample_rate;
	stream->volume = 1.0f;
	/* DirectSound's default mix bins: a mono voice to both fronts, a
	stereo voice's channels to the front left and right */
	stream->mix_left = 1.0f;
	stream->mix_right = 1.0f;
	stream->has_3d = (description->dwFlags & DSSTREAMCAPS_CTRL3D) != 0;
	stream->mode = DS3DMODE_NORMAL;
	stream->minimum_distance = DS3D_DEFAULTMINDISTANCE;
	stream->maximum_distance = DS3D_DEFAULTMAXDISTANCE;
	stream->i3dl2_gain = 1.0f;
	stream->decoded_block[0] = stream->decoded_block[1] = -1;
	game_lock();
	stream->next = streams;
	streams = stream;
	pthread_mutex_unlock(&mixer_lock);
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

	game_lock();
	active = record->packet_count != 0;
	pthread_mutex_unlock(&mixer_lock);
	return active;
}

#define STREAM_SETTER(body) \
	struct psp_stream *record = stream_from_interface(stream); \
	game_lock(); \
	body; \
	pthread_mutex_unlock(&mixer_lock); \
	return DS_OK;

HRESULT WINAPI IDirectSoundStream_SetFrequency(LPDIRECTSOUNDSTREAM stream, DWORD frequency)
{
	STREAM_SETTER(record->frequency = frequency ? frequency : record->sample_rate)
}

HRESULT WINAPI IDirectSoundStream_SetVolume(LPDIRECTSOUNDSTREAM stream, LONG volume)
{
	STREAM_SETTER(record->volume = gain_from_millibels(volume))
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
	struct psp_stream *record = stream_from_interface(stream);
	unsigned long bit, index = 0;

	game_lock();
	for (bit = 0; bit < 32; bit++)
	{
		if (!(mix_bin_mask & (1UL << bit)))
			continue;
		if ((1UL << bit) == DSMIXBIN_FRONT_LEFT)
			record->mix_left = gain_from_millibels(volumes[index]);
		else if ((1UL << bit) == DSMIXBIN_FRONT_RIGHT)
			record->mix_right = gain_from_millibels(volumes[index]);
		index++;
	}
	pthread_mutex_unlock(&mixer_lock);
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
		STREAM_SETTER(record->i3dl2_gain = gain_from_millibels(direct))
	}
}

HRESULT WINAPI IDirectSoundStream_Pause(LPDIRECTSOUNDSTREAM stream, DWORD pause)
{
	STREAM_SETTER(record->paused = pause == DSSTREAMPAUSE_PAUSE)
}

HRESULT WINAPI IDirectSoundStream_SetVelocity(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply) { (void)stream; (void)x; (void)y; (void)z; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSoundStream_SetConeAngles(LPDIRECTSOUNDSTREAM stream, DWORD inside, DWORD outside, DWORD apply) { (void)stream; (void)inside; (void)outside; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSoundStream_SetConeOrientation(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply) { (void)stream; (void)x; (void)y; (void)z; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSoundStream_SetConeOutsideVolume(LPDIRECTSOUNDSTREAM stream, LONG volume, DWORD apply) { (void)stream; (void)volume; (void)apply; return DS_OK; }

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
