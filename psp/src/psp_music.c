/* The music path (docs/plan_music_stream.md, keep word "musicstream", off by
default): a map's music permutations come from its Vorbis pack (<map>.pmus,
bake --music-stream) instead of the sound cache, and play on a 44.1 kHz
channel of their own.

The game never sees the difference. A music permutation the pack has gets a
one-page block in the cache (its "stub") instead of its samples, and its
stream stays a voice of the 22,050 Hz mixer, which advances it, completes its
packets and ramps its gains exactly as before but mixes none of its samples
(psp_dsound.c marks it). Each block the mixer says where each music voice
was at the block's start and end, and with what gains; this side decodes
those frames of the voice's chain and plays them, at twice the positions
(the bake halved music to 22,050 Hz for the map, and the pack keeps it at
44.1 kHz). So starts, stops, loops, alternates, crossfades and volume are
the game's own.

Here the decode runs on the main CPU, in the audio thread after the mix (the
Media Engine's audio job is the next step). Tremor's allocations come from
this file's pool (psp_build.py renames them at link), so the codec never
enters the game's allocator and the pool can later move to the ME as a
whole. The stick is read by a thread of its own, 16 KB at a time, one read
in flight. A block whose packets aren't in RAM in time plays as silence and
is counted, and the voice comes back with a short fade */

#include <pspkernel.h>
#include <pspaudio.h>
#include <pspiofilemgr.h>
#include <pspthreadman.h>
#include <malloc.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <tremor/ivorbiscodec.h>
#include "psp_paths.h"
#include "psp_me.h"

void platform_log(const char *format, ...);
void psp_log_flush(void);
int psp_keep(const char *word);
void psp_me_sim_call(unsigned int index, void *top, void (*fn)(unsigned int));
int psp_me_running(void);

#define PMUS_MAGIC 0x53554D50u /* "PMUS" */
#define PMUS_VERSION 1
#define OUT_FRAMES 1024        /* one 22,050 Hz mixer block (512 frames) at 44.1 kHz */
#define VOICES 3               /* two music channels crossfading, and one starting */
#define STUBS 160
#define STUB_BYTES 4096
#define PIECE_BYTES 16384
#define INDEX_PACKETS 512      /* a 4 KB window of a chain's packet table */
#define PCM_FRAMES 4096        /* a voice's decoded frames not yet played */
/* the codec's setup and three decoders peaked at 199 KB (the menu, three
voices), 157 on b30 and a10; a packet wants 48 KB free above that */
#define POOL_BYTES (256 * 1024)
/* a started stub's first index window and piece, read ahead of its voice */
#define PREFETCH 3
/* a map's entries and chains, and its codec setup while it is read: a10's
pack needs ~25 KB of tables (940 entries, 70 chains) */
#define TABLE_BYTES (48 * 1024)
#define FADE_FRAMES 220        /* 5 ms */
/* the mixer's blocks queue for the music job: their marks and their output.
The output plays MUSIC_LEAD silent blocks first (again after it ran dry),
so the music trails the mix by MUSIC_LEAD blocks of 1,024 frames at 44.1 kHz
(46 ms), the room the job has to wait for the ME's idle time */
#define MUSIC_SETS 4
#define MUSIC_LEAD 2
#define MUSIC_MARKS_MAX 4
/* the music job's stack when it runs on the main CPU (the emulator, or no
ME): Tremor's codebook set-up sorts on its stack (alloca, by the book's
entries), past the 16 KB of the audio thread that hands the job over */
#define MUSIC_STACK_BYTES (64 * 1024)
/* the pool on the real ME: its eDRAM, apart from main RAM's bus (the room
psp_me.h names); in the emulator, or with the sound cache in the eDRAM, the
block's part of main RAM */
typedef char music_pool_fits_its_edram[(ME_EDRAM_MUSIC + POOL_BYTES <= ME_EDRAM_MUSIC_END) ? 1 : -1];

struct pmus_header
{
	unsigned int magic, version, setups, entries, chains, entries_at, chains_at, keys, rate;
};
struct pmus_entry
{
	unsigned int hash;
	unsigned short pitch, permutation;
	unsigned int chain, start, frames, first;
};
struct pmus_chain
{
	unsigned int setup, packets, frames, index_at, packets_at, body;
};

/* a stub: one music permutation the cache holds as a page */
struct stub
{
	const void *permutation;
	const unsigned char *base;
	long size;
	int entry;
	unsigned char *loaded;
	unsigned int first_offset; /* the entry's first packet, bytes into the chain's packets */
	int state;                 /* 0 free, 1 waiting for the reader, 2 ready */
};

/* one music voice's decoder and its window onto the stick */
struct voice
{
	const void *owner;          /* the mixer's stream */
	int chain;                  /* the chain the decoder is in step with; -1: start it again */
	int data_chain;             /* the chain whose bytes the index window and the pieces hold (the reader's) */
	unsigned int position;      /* the next frame the PCM queue gives, in the chain at 44.1 kHz */
	int primed;                 /* decoding from a restart: the first packet gives nothing */
	unsigned int next_packet;   /* the next packet to decode */
	unsigned int next_offset;   /* its bytes into the chain's packets */
	unsigned int index_wanted;  /* the index window the decode wants */
	unsigned int piece_wanted;  /* the first of the two pieces it wants */
	unsigned int pcm_frames;
	int fade;                   /* frames into the fade-in after a dropout or a start */
	int left, right;            /* the gains the last block ended on */
	int used;                   /* blocks since it last played (frees the voice) */
	vorbis_dsp_state dsp;
	vorbis_block block;
	int dsp_ready;
	short pcm[PCM_FRAMES * 2];
	/* the reader's part, on lines of its own: the decode (on the ME, often)
	only reads it, so the main CPU never drops it after a job, and the
	reader's writes never share a line with the decode's */
	/* the index window: packets [index_first, index_first + index_count) */
	unsigned int index_first __attribute__((aligned(64))), index_count;
	/* two pieces of packet bytes, each 16 KB from piece_at[k] in the chain's packets */
	unsigned int piece_at[2];
	int piece_ok[2];
	unsigned int index[INDEX_PACKETS][2];
	unsigned char piece[2][PIECE_BYTES] __attribute__((aligned(64)));
};
/* a stub's start, read by the reader when the game asks for the stub, so a
voice's first block finds its packets: the decode only reads it */
struct prefetch
{
	int ok, chain;
	unsigned int index_first, index_count, piece_at;
	unsigned int index[INDEX_PACKETS][2];
	unsigned char piece[PIECE_BYTES] __attribute__((aligned(64)));
};
#define PREFETCH_BYTES ((sizeof(struct prefetch) + 63) & ~(size_t)63)

/* what the decode writes in a voice */
#define VOICE_DECODE_BYTES offsetof(struct voice, index_first)

static struct
{
	int on;                     /* the pack is open and matches: the path runs this map */
	SceUID file;
	struct pmus_header header;
	struct pmus_entry *entries;
	struct pmus_chain *chains;
	/* each entry's first packet's bytes into its chain, once walked (the reader's) */
	unsigned int *first_offsets;
	const unsigned char *setup;   /* the map's codec setup bytes, in the tables */
	unsigned int setup_bytes;
	struct stub stubs[STUBS];
	struct voice *voices[VOICES];
	struct prefetch *prefetch[PREFETCH];
	int prefetch_next;
	/* "musicnodecode" (a measurement): the path runs but Tremor doesn't,
	and the music is silent, so a pair prices the decode apart from the
	rest (the reader, the stubs, the smaller cache) */
	int nodecode;
	/* "musicmestop" (a test): the ME stopped once the first music job is
	collected, so a run takes the fallback (the decoders and setup made
	again on this CPU, on the job's own stack) as after an ME failure */
	int mestop_test;
	SceUID reader_wake, out_ready, out_free, io_lock, state_lock;
	int out_channel;
	unsigned char *pool_ram;     /* the pool's room in main RAM */
	unsigned char *stack;        /* the job's stack on the main CPU (MUSIC_STACK_BYTES) */
	unsigned int stops_seen;     /* psp_me_stops at the last job */
	int first_collected, slow_said;
	/* the path's RAM: its part of the sound cache's start-up block (psp_dsound.c) */
	unsigned char *block;
	unsigned char *tables;
	/* counts, for the heartbeat */
	unsigned long prefetched, walks, index_reads;
	unsigned long sets_full, out_waited, jobs, jobs_here;
	unsigned long out_late, pieces, piece_worst_us, stubs_made, not_in_pack, decode_us, decode_worst_us;
} music;

/* what the decode writes outside the voices and the pool: the ME's audio job
runs it, so it sits on lines of its own (the main CPU drops the head and the
output after each job, and never writes any of it while one is out) */
static struct __attribute__((aligned(64)))
{
	unsigned char *pool;        /* where the pool is: the ME's eDRAM, or main RAM */
	unsigned char *pool_end;
	int pool_in_edram;
	/* the codec setup, made in the pool by the job's CPU (the main CPU can't
	reach the ME's eDRAM): 0 not yet, 1 made, -1 refused, -2 lost (the ME
	stopped with it in its eDRAM) */
	int setup_state;
	vorbis_info info;
	vorbis_comment comment;
	unsigned long stops;
	int yielding;               /* the job runs on the ME: it yields between packets */
	unsigned char *pool_next;   /* the top of the pool's blocks */
	unsigned char *pool_last;   /* the block just below the top, NULL when none */
	unsigned char *pool_peak;
	unsigned long pool_failed;
	int wake;                   /* the reader is wanted: signalled after the block */
	unsigned long blocks, late_blocks, restarts, playing_worst, prefetch_hits;
	short out[MUSIC_SETS][OUT_FRAMES * 2] __attribute__((aligned(64)));
	/* the decode's scratch: only the CPU that decodes reads it */
	int sums[OUT_FRAMES * 2] __attribute__((aligned(64)));
	unsigned char joined[PIECE_BYTES];
} dec;
/* the part the main CPU reads after a job */
#define DEC_READ_BYTES offsetof(__typeof__(dec), sums)

/* ---------- the pool (Tremor's malloc family, renamed at link) */


/* ---------- the path's RAM

The pool, the voices and the tables come out of the sound cache's start-up
block, and the cache is made smaller by as much (psp_platform.c), so the heap
pays nothing for them: the room was music's in the cache, and with the path
on, music no longer goes there. The block is made once, before any map, so
the keep word alone decides it. */
#define VOICE_BYTES ((sizeof(struct voice) + 63) & ~(size_t)63)

unsigned long psp_music_block_bytes(void)
{
	static int wanted = -1;

	if (wanted < 0)
		wanted = psp_keep("musicstream") != 0;
	return wanted ? POOL_BYTES + VOICES * VOICE_BYTES + PREFETCH * PREFETCH_BYTES + TABLE_BYTES + MUSIC_STACK_BYTES : 0;
}

/* the block's part for the path (64-byte aligned), or NULL if it got none */
void psp_music_block_place(void *at)
{
	music.block = (unsigned char *)at;
}

/* Tremor frees: each packet's block store is grown by realloc and its
overflow links are freed (_vorbis_block_ripcord), so the pool takes frees
back, or it runs out over a map and Tremor writes through the NULL it gets.
Blocks lie end to end below the top, each with a 16-byte header (its size
and the size of the one before), first fit, joined with free neighbours */
struct pool_block
{
	unsigned int size;          /* bytes after the header, a multiple of 16 */
	unsigned int prev;          /* the size of the block before, 0 for the first */
	unsigned int free;
	unsigned int pad;
};
#define BLOCK_AT(p) ((struct pool_block *)(p))
#define BLOCK_NEXT(p) ((p) + 16 + BLOCK_AT(p)->size)

void *mu_malloc(unsigned int bytes)
{
	unsigned char *p, *top = dec.pool_next;

	if (!top)
		return NULL;
	bytes = bytes ? (bytes + 15) & ~15u : 16;
	for (p = dec.pool; p < top; p = BLOCK_NEXT(p))
	{
		struct pool_block *b = BLOCK_AT(p);

		if (!b->free || b->size < bytes)
			continue;
		if (b->size - bytes >= 64)
		{
			unsigned char *rest = p + 16 + bytes;

			BLOCK_AT(rest)->size = b->size - bytes - 16;
			BLOCK_AT(rest)->prev = bytes;
			BLOCK_AT(rest)->free = 1;
			if (BLOCK_NEXT(rest) < top)
				BLOCK_AT(BLOCK_NEXT(rest))->prev = BLOCK_AT(rest)->size;
			else
				dec.pool_last = rest;
			b->size = bytes;
		}
		b->free = 0;
		return p + 16;
	}
	if (top + 16 + bytes > dec.pool_end)
	{
		dec.pool_failed++;
		return NULL;
	}
	BLOCK_AT(top)->size = bytes;
	BLOCK_AT(top)->prev = dec.pool_last ? BLOCK_AT(dec.pool_last)->size : 0;
	BLOCK_AT(top)->free = 0;
	dec.pool_last = top;
	dec.pool_next = top + 16 + bytes;
	if (dec.pool_next > dec.pool_peak)
		dec.pool_peak = dec.pool_next;
	return top + 16;
}

void mu_free(void *pointer)
{
	unsigned char *p, *next;

	if (!pointer)
		return;
	p = (unsigned char *)pointer - 16;
	BLOCK_AT(p)->free = 1;
	next = BLOCK_NEXT(p);
	if (next < dec.pool_next && BLOCK_AT(next)->free)
	{
		BLOCK_AT(p)->size += 16 + BLOCK_AT(next)->size;
		if (dec.pool_last == next)
			dec.pool_last = p;
		next = BLOCK_NEXT(p);
		if (next < dec.pool_next)
			BLOCK_AT(next)->prev = BLOCK_AT(p)->size;
	}
	if (p > dec.pool && BLOCK_AT(p - 16 - BLOCK_AT(p)->prev)->free)
	{
		unsigned char *before = p - 16 - BLOCK_AT(p)->prev;

		BLOCK_AT(before)->size += 16 + BLOCK_AT(p)->size;
		if (dec.pool_last == p)
			dec.pool_last = before;
		p = before;
		next = BLOCK_NEXT(p);
		if (next < dec.pool_next)
			BLOCK_AT(next)->prev = BLOCK_AT(p)->size;
	}
	if (BLOCK_NEXT(p) == dec.pool_next)
	{
		/* the top block: the pool shrinks back to it (the one below is in use) */
		dec.pool_next = p;
		dec.pool_last = p > dec.pool ? p - 16 - BLOCK_AT(p)->prev : NULL;
	}
}

void *mu_calloc(unsigned int count, unsigned int size)
{
	void *p = mu_malloc(count * size);

	if (p)
		memset(p, 0, count * size);
	return p;
}

void *mu_realloc(void *old, unsigned int bytes)
{
	unsigned int had;
	void *p;

	if (!old)
		return mu_malloc(bytes);
	had = BLOCK_AT((unsigned char *)old - 16)->size;
	if (bytes <= had)
		return old;
	p = mu_malloc(bytes);
	if (p)
	{
		memcpy(p, old, had);
		mu_free(old);
	}
	return p;
}

/* the most a packet's decode asks of the pool: its PCM (the long block,
both channels) and the store's growth, with room to spare. Tremor doesn't
check what its allocator gives, so a packet is decoded only with this much
free at the top, and is otherwise late (counted as the pool full) */
#define PACKET_POOL_BYTES (48 * 1024)

static int pool_has_room(void)
{
	return dec.pool_next && dec.pool_next + PACKET_POOL_BYTES <= dec.pool_end;
}

/* ---------- the pack */

unsigned int psp_music_hash(const char *path)
{
	unsigned int h = 0x811C9DC5u;

	for (; *path; path++)
	{
		unsigned int c = (unsigned char)*path;

		if (c >= 'A' && c <= 'Z')
			c += 'a' - 'A';
		if (c == '/')
			c = '\\';
		h = (h ^ c) * 0x01000193u;
	}
	return h;
}

static int read_at(unsigned int offset, void *buffer, unsigned int bytes)
{
	int got;

	sceKernelWaitSema(music.io_lock, 1, NULL);
	sceIoLseek32(music.file, (int)offset, PSP_SEEK_SET);
	got = sceIoRead(music.file, buffer, bytes);
	sceKernelSignalSema(music.io_lock, 1);
	return got == (int)bytes;
}

static int entry_find(unsigned int hash, int pitch, int permutation)
{
	int low = 0, high = (int)music.header.entries - 1;

	while (low <= high)
	{
		int middle = (low + high) / 2;
		const struct pmus_entry *e = &music.entries[middle];
		long d = e->hash != hash ? (e->hash < hash ? -1 : 1) : e->pitch != pitch ? e->pitch - pitch : e->permutation - permutation;

		if (!d)
			return middle;
		if (d < 0)
			low = middle + 1;
		else
			high = middle - 1;
	}
	return -1;
}

static void pack_close(const char *why)
{
	if (why)
		platform_log("music: the pack %s; music plays from the cache this map", why);
	music.on = 0;
	if (music.file >= 0)
		sceIoClose(music.file);
	music.file = -1;
}

static int reader_thread(SceSize arguments, void *argument);
static int output_thread(SceSize arguments, void *argument);

static void start_once(void)
{
	static int started = 0;
	unsigned char *at = music.block;
	int k;

	if (started || !at)
		return;
	started = 1;
	music.pool_ram = at;
	at += POOL_BYTES;
	/* the voices' windows and queues apart from the codec's pool */
	for (k = 0; k < VOICES; k++)
	{
		music.voices[k] = (struct voice *)at;
		memset(music.voices[k], 0, sizeof(struct voice));
		music.voices[k]->chain = music.voices[k]->data_chain = -1;
		sceKernelDcacheWritebackRange(music.voices[k], sizeof(struct voice));
		at += VOICE_BYTES;
	}
	for (k = 0; k < PREFETCH; k++)
	{
		music.prefetch[k] = (struct prefetch *)at;
		memset(music.prefetch[k], 0, sizeof(struct prefetch));
		sceKernelDcacheWritebackRange(music.prefetch[k], sizeof(struct prefetch));
		at += PREFETCH_BYTES;
	}
	music.tables = at;
	at += TABLE_BYTES;
	music.stack = at;
	music.nodecode = psp_keep("musicnodecode") != 0;
	music.mestop_test = psp_keep("musicmestop") != 0;
	music.state_lock = sceKernelCreateSema("music state", 0, 1, 1, NULL);
	music.reader_wake = sceKernelCreateSema("music reader", 0, 0, 64, NULL);
	music.io_lock = sceKernelCreateSema("music io", 0, 1, 1, NULL);
	music.out_ready = sceKernelCreateSema("music out", 0, 0, MUSIC_SETS, NULL);
	music.out_free = sceKernelCreateSema("music free", 0, MUSIC_SETS, MUSIC_SETS, NULL);
	music.out_channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, OUT_FRAMES, PSP_AUDIO_FORMAT_STEREO);
	/* below the mixer, which feeds it; the reader above the game, just below
	the cache file thread (psp_platform.c's PSP_READER_PRIORITY): below the
	game it waited seconds for a 16 KB read while the game ran on */
	k = sceKernelCreateThread("music out", output_thread, 0x13, 0x2000, 0, NULL);
	if (k >= 0)
		sceKernelStartThread(k, 0, NULL);
	k = sceKernelCreateThread("music reader", reader_thread, 0x1d, 0x2000, 0, NULL);
	if (k >= 0)
		sceKernelStartThread(k, 0, NULL);
	platform_log("music: on: pool %u KB, 44.1 kHz channel %d", POOL_BYTES / 1024, music.out_channel);
}

/* the map's tags are in: its pack, if the keep word wants one (cache_files.c) */
static void psp_music_map_open(const char *name, int wanted);

void psp_music_map_opened(const char *name)
{
	static int wanted = -1;
	int locked;

	if (wanted < 0)
		wanted = psp_keep("musicstream") != 0;
	/* not while a job decodes: it reads the pack's tables and the setup
	(before the first map there is no lock, and no job) */
	locked = music.state_lock > 0;
	if (locked)
	{
		/* bounded: a music job out with the ME holds it until it's collected */
		SceUInt wait = 500000;

		if (sceKernelWaitSema(music.state_lock, 1, &wait) < 0)
		{
			pack_close("wasn't opened: the music job kept the state for 500 ms");
			return;
		}
	}
	psp_music_map_open(name, wanted);
	if (locked)
		sceKernelSignalSema(music.state_lock, 1);
}

static void psp_music_map_open(const char *name, int wanted)
{
	char path[128];
	unsigned char *setup = NULL;
	unsigned int bytes, k;

	pack_close(NULL);
	memset(music.stubs, 0, sizeof(music.stubs));
	/* chain numbers are the pack's own: nothing a voice or a slot holds
	carries over to another map's */
	for (k = 0; k < VOICES; k++)
		if (music.voices[k])
		{
			music.voices[k]->owner = NULL;
			music.voices[k]->chain = music.voices[k]->data_chain = -1;
			music.voices[k]->index_count = 0;
			music.voices[k]->piece_ok[0] = music.voices[k]->piece_ok[1] = 0;
		}
	for (k = 0; k < PREFETCH; k++)
		if (music.prefetch[k])
			music.prefetch[k]->ok = 0;
	music.entries = NULL;
	music.chains = NULL;
	if (!wanted)
		return;
	if (!music.block)
	{
		platform_log("music: the sound block had no room for the path; music plays from the cache");
		return;
	}
	/* the pack first: a map without one starts nothing */
	snprintf(path, sizeof(path), PSP_MAPS_ROOT "/%s.pmus", name);
	music.file = sceIoOpen(path, PSP_O_RDONLY, 0);
	if (music.file < 0)
	{
		platform_log("music: no %s.pmus; music plays from the cache this map", name);
		return;
	}
	start_once();
	if (!music.pool_ram || music.out_channel < 0)
	{
		pack_close("has no channel to play on");
		return;
	}
	music.on = 1;
	if (!read_at(0, &music.header, sizeof(music.header)) || music.header.magic != PMUS_MAGIC ||
		music.header.version != PMUS_VERSION || music.header.rate != 44100 || music.header.setups != 1)
	{
		pack_close("isn't one this build reads (one setup, version 1, 44.1 kHz)");
		return;
	}
	bytes = music.header.entries_at - 64;
	{
		/* the tables stay for the map; the setup after them only while it is read */
		unsigned int entries = (music.header.entries * sizeof(struct pmus_entry) + 63) & ~63u;
		unsigned int chains = (music.header.chains * sizeof(struct pmus_chain) + 63) & ~63u;
		unsigned int offsets = (music.header.entries * sizeof(unsigned int) + 63) & ~63u;

		if (music.header.entries_at < 64 || entries + chains + offsets + bytes > TABLE_BYTES)
		{
			platform_log("music: %s.pmus needs %u KB of tables and setup, the path has %u", name,
				(entries + chains + offsets + bytes) / 1024, TABLE_BYTES / 1024);
			pack_close("is larger than this build's tables");
			return;
		}
		music.entries = (struct pmus_entry *)music.tables;
		music.chains = (struct pmus_chain *)(music.tables + entries);
		music.first_offsets = (unsigned int *)(music.tables + entries + chains);
		memset(music.first_offsets, 0xFF, offsets);
		setup = music.tables + entries + chains + offsets;
	}
	if (!read_at(music.header.entries_at, music.entries, music.header.entries * sizeof(struct pmus_entry)) ||
		!read_at(music.header.chains_at, music.chains, music.header.chains * sizeof(struct pmus_chain)) ||
		!read_at(64, setup, bytes))
	{
		pack_close("couldn't be read");
		return;
	}
	{
		/* the pack's own keys against its header: a pack cut short or written
		by another bake is refused, not half trusted */
		unsigned int keys = 0x811C9DC5u, e;

		for (e = 0; e < music.header.entries; e++)
		{
			unsigned char key[8];

			memcpy(key, &music.entries[e].hash, 4);
			memcpy(key + 4, &music.entries[e].pitch, 2);
			memcpy(key + 6, &music.entries[e].permutation, 2);
			for (k = 0; k < 8; k++)
				keys = (keys ^ key[k]) * 0x01000193u;
		}
		if (keys != music.header.keys)
		{
			pack_close("doesn't match its own keys");
			return;
		}
	}
	/* the codec setup is made by the music job, on its own CPU, from these
	bytes (every pack has the same one: stereo, 44.1 kHz, one quality) */
	music.setup = setup;
	music.setup_bytes = bytes;
	platform_log("music: %s.pmus: %u permutations in %u chains", name, music.header.entries, music.header.chains);
	/* a hang's trail: the stick has this line before any job runs */
	psp_log_flush();
	sceIoSync("ms0:", 0);
}

/* ---------- the cache's side (xbox_sound_cache.c, the stub path) */

int psp_music_active(void)
{
	return music.on;
}

/* a music permutation about to be loaded: the stub's size if the pack has it
(its samples' size and its frames agree), else 0 and it loads as before */
long psp_music_stub(const void *permutation, const char *tag_path, int pitch, int index, long size)
{
	int entry, k, free_slot = -1;

	if (!music.on)
		return 0;
	entry = entry_find(psp_music_hash(tag_path), pitch, index);
	if (entry < 0 || size <= 0)
	{
		music.not_in_pack++;
		return 0;
	}
	for (k = 0; k < STUBS; k++)
	{
		if (music.stubs[k].permutation == permutation)
		{
			free_slot = k;
			break;
		}
		if (free_slot < 0 && !music.stubs[k].permutation)
			free_slot = k;
	}
	if (free_slot < 0)
	{
		/* more music asked for than the table holds: the oldest is reused
		(its permutation, if it plays again, asks again) */
		static int next = 0;

		free_slot = next;
		next = (next + 1) % STUBS;
	}
	memset(&music.stubs[free_slot], 0, sizeof(struct stub));
	music.stubs[free_slot].permutation = permutation;
	music.stubs[free_slot].entry = entry;
	music.stubs[free_slot].size = size;
	return STUB_BYTES;
}

/* the range check of a packet's queueing (sound_dsound_xbox.c) */
int psp_music_is_stub(const void *permutation)
{
	int k;

	if (!music.on)
		return 0;
	for (k = 0; k < STUBS; k++)
		if (music.stubs[k].permutation == permutation && music.stubs[k].state == 2)
			return 1;
	return 0;
}

/* the stub's block is placed: the reader finds the entry's first packet and
sets loaded, as a sample read's completion would */
void psp_music_request(const void *permutation, void *block, unsigned char *loaded)
{
	int k;

	for (k = 0; k < STUBS; k++)
		if (music.stubs[k].permutation == permutation)
		{
			music.stubs[k].base = block;
			music.stubs[k].loaded = loaded;
			music.stubs[k].state = 1;
			music.stubs_made++;
			sceKernelSignalSema(music.reader_wake, 1);
			return;
		}
	*loaded = 1;
}

/* the mixer, as the game queues a packet (under its lock): whether it is a
stub's, and where it starts in the chain at 44.1 kHz; scale is 44.1 kHz
frames per 22,050 Hz frame of the stream */
int psp_music_packet(const void *permutation, const unsigned char *data, unsigned long channels,
	unsigned long *start, unsigned long *scale)
{
	int k;

	if (!music.on || !data)
		return 0;
	for (k = 0; k < STUBS; k++)
	{
		const struct stub *s = &music.stubs[k];

		if (s->permutation == permutation && s->state == 2 && data >= s->base && data < s->base + s->size)
		{
			const struct pmus_entry *e = &music.entries[s->entry];
			unsigned long long frames = (unsigned long long)s->size * 64 / (36 * (channels ? channels : 1));

			if (!frames)
				return 0;
			*scale = (unsigned long)(e->frames / frames);
			if (*scale < 1 || *scale > 2)
				return 0;
			*start = e->start + (unsigned long)((unsigned long long)(data - s->base) * e->frames / (unsigned long long)s->size);
			return 1 + e->chain;
		}
	}
	return 0;
}

/* ---------- the reader */

static unsigned int index_offset_walk(int chain, unsigned int from_packet, unsigned int from_offset, unsigned int to_packet)
{
	static unsigned int window[INDEX_PACKETS][2];
	const struct pmus_chain *c = &music.chains[chain];

	while (from_packet < to_packet)
	{
		unsigned int n = to_packet - from_packet, k;

		if (n > INDEX_PACKETS)
			n = INDEX_PACKETS;
		if (!read_at(c->index_at + from_packet * 8, window, n * 8))
			return 0xFFFFFFFFu;
		music.index_reads++;
		for (k = 0; k < n; k++)
			from_offset += window[k][0];
		from_packet += n;
	}
	return from_offset;
}

/* an entry's first packet's bytes into its chain: walked from the nearest
entry of the chain already walked (else the chain's start), and kept, so a
chain's index is read at most once a map. Walking each stub from the chain's
start read ~10 windows of 4 KB a stub (5,016 stick calls in a b30 run for
457 pieces), from a thread above the game's */
static unsigned int entry_first_offset(int entry)
{
	const struct pmus_entry *e = &music.entries[entry];
	unsigned int target = e->first ? e->first - 1 : 0, from_packet = 0, from_offset = 0, k, offset;

	if (music.first_offsets[entry] != 0xFFFFFFFFu)
		return music.first_offsets[entry];
	for (k = 0; k < music.header.entries; k++)
	{
		const struct pmus_entry *x = &music.entries[k];
		unsigned int at = x->first ? x->first - 1 : 0;

		if (x->chain == e->chain && music.first_offsets[k] != 0xFFFFFFFFu && at <= target && at >= from_packet)
		{
			from_packet = at;
			from_offset = music.first_offsets[k];
		}
	}
	offset = index_offset_walk((int)e->chain, from_packet, from_offset, target);
	if (offset != 0xFFFFFFFFu)
		music.first_offsets[entry] = offset;
	music.walks++;
	return offset;
}

/* the reader's turn at the state the music job reads: bounded, since a job
out with the ME holds it until the audio thread collects it (a block or
two); a miss leaves that piece or window for the reader's next round */
static int state_take(void)
{
	SceUInt wait = 100000;

	return sceKernelWaitSema(music.state_lock, 1, &wait) >= 0;
}

/* the entry's first index window and piece into the next slot (the
reader): its flag changes only under the lock, as a voice's pieces' do */
static void prefetch_read(const struct pmus_entry *e, unsigned int first_offset)
{
	struct prefetch *f = music.prefetch[music.prefetch_next];
	const struct pmus_chain *c = &music.chains[e->chain];
	unsigned int first = e->first ? e->first - 1 : 0, n = c->packets - first, at = first_offset & ~(PIECE_BYTES - 1), bytes;

	music.prefetch_next = (music.prefetch_next + 1) % PREFETCH;
	if (!f || first >= c->packets || at >= c->body)
		return;
	if (n > INDEX_PACKETS)
		n = INDEX_PACKETS;
	bytes = c->body - at < PIECE_BYTES ? c->body - at : PIECE_BYTES;
	if (!state_take())
		return;
	f->ok = 0;
	sceKernelSignalSema(music.state_lock, 1);
	if (!read_at(c->index_at + first * 8, f->index, n * 8) || !read_at(c->packets_at + at, f->piece, bytes))
		return;
	f->chain = (int)e->chain;
	f->index_first = first;
	f->index_count = n;
	f->piece_at = at;
	sceKernelDcacheWritebackRange(f, sizeof(struct prefetch));
	if (!state_take())
		return;
	f->ok = 1;
	sceKernelSignalSema(music.state_lock, 1);
	music.prefetched++;
}

static int reader_thread(SceSize arguments, void *argument)
{
	(void)arguments;
	(void)argument;
	for (;;)
	{
		int k;

		sceKernelWaitSema(music.reader_wake, 1, NULL);
		if (!music.on)
			continue;
		/* stubs first: the game waits on them */
		for (k = 0; k < STUBS; k++)
		{
			struct stub *s = &music.stubs[k];

			if (s->state == 1)
			{
				const struct pmus_entry *e = &music.entries[s->entry];

				s->first_offset = entry_first_offset(s->entry);
				s->state = 2;
				if (s->loaded)
					*s->loaded = 1;
				prefetch_read(e, s->first_offset);
			}
		}
		for (k = 0; k < VOICES; k++)
		{
			struct voice *v = music.voices[k];
			const struct pmus_chain *c;
			SceUInt64 t;
			int p;

			/* the chain its data is for: the decoder's own may be -1 while it
			waits to start again, and its bytes are still wanted */
			if (!v || !v->owner || v->data_chain < 0 || v->data_chain >= (int)music.header.chains)
				continue;
			c = &music.chains[v->data_chain];
			t = sceKernelGetSystemTimeWide();
			if (v->index_wanted != v->index_first || !v->index_count)
			{
				unsigned int first = v->index_wanted, n = c->packets - first;

				if (n > INDEX_PACKETS)
					n = INDEX_PACKETS;
				static unsigned int spare[INDEX_PACKETS][2];

				if (read_at(c->index_at + first * 8, spare, n * 8) && state_take())
				{
					if (v->index_wanted == first)
					{
						memcpy(v->index, spare, n * 8);
						v->index_first = first;
						v->index_count = n;
					}
					sceKernelSignalSema(music.state_lock, 1);
				}
			}
			for (p = 0; p < 2; p++)
			{
				unsigned int at = v->piece_wanted + (unsigned int)p * PIECE_BYTES, slot = (at / PIECE_BYTES) & 1, n;

				if (at >= c->body || (v->piece_ok[slot] && v->piece_at[slot] == at))
					continue;
				n = c->body - at < PIECE_BYTES ? c->body - at : PIECE_BYTES;
				/* a slot's flags change only under the lock, which the audio
				thread holds while a music job is out: no job sees a slot ready
				while its bytes are being read, and they are in RAM before
				it says so */
				if (!state_take())
					continue;
				v->piece_ok[slot] = 0;
				sceKernelSignalSema(music.state_lock, 1);
				if (read_at(c->packets_at + at, v->piece[slot], n))
				{
					sceKernelDcacheWritebackRange(v->piece[slot], n);
					if (state_take())
					{
						v->piece_at[slot] = at;
						v->piece_ok[slot] = 1;
						sceKernelSignalSema(music.state_lock, 1);
						music.pieces++;
					}
				}
			}
			t = sceKernelGetSystemTimeWide() - t;
			if (t > music.piece_worst_us)
				music.piece_worst_us = (unsigned long)t;
		}
	}
	return 0;
}

/* ---------- decoding */

/* a packet's index entry (its size, and where its frames end): the voice's
window, else a prefetched start of its chain */
static const unsigned int *index_of(struct voice *v, unsigned int packet)
{
	int k;

	if (v->index_count && packet >= v->index_first && packet < v->index_first + v->index_count)
		return v->index[packet - v->index_first];
	for (k = 0; k < PREFETCH; k++)
	{
		const struct prefetch *f = music.prefetch[k];

		if (f && f->ok && f->chain == v->data_chain && packet >= f->index_first && packet < f->index_first + f->index_count)
		{
			dec.prefetch_hits++;
			return f->index[packet - f->index_first];
		}
	}
	return NULL;
}

static const unsigned char *packet_bytes(struct voice *v, unsigned int offset, unsigned int bytes)
{
	unsigned int at = offset & ~(PIECE_BYTES - 1), slot = (at / PIECE_BYTES) & 1;

	if (!v->piece_ok[slot] || v->piece_at[slot] != at)
	{
		int k;

		for (k = 0; k < PREFETCH; k++)
		{
			const struct prefetch *f = music.prefetch[k];

			if (f && f->ok && f->chain == v->data_chain && f->piece_at == at)
				return offset + bytes > at + PIECE_BYTES ? NULL : f->piece + (offset - at);
		}
		return NULL;
	}
	if (offset + bytes > at + PIECE_BYTES)
		return NULL; /* across two pieces: copied below */
	return v->piece[slot] + (offset - at);
}

static short clip16(int value)
{
	return (short)(value > 32767 ? 32767 : value < -32768 ? -32768 : value);
}

/* decode until the queue holds frames up to `until` (in the chain); 0 when
a packet or the index isn't in RAM yet, 2 when the job gives way. No kernel call: it runs in the ME's
audio job (the reader is woken after the block, by dec.wake) */
static int decode_to(struct voice *v, unsigned int until)
{
	unsigned char *joined = dec.joined;

	while (v->position + v->pcm_frames < until)
	{
		ogg_packet op;
		ogg_int32_t **pcm;
		const unsigned char *data;
		unsigned int size, end, k;
		int n;

		const unsigned int *entry = index_of(v, v->next_packet), *after;

		if (!v->index_count || v->next_packet < v->index_first || v->next_packet >= v->index_first + v->index_count)
		{
			/* not in the voice's own window: the reader fetches it, and a
			prefetched start serves meanwhile */
			if (v->index_wanted != v->next_packet || !v->index_count)
				dec.wake = 1;
			v->index_wanted = v->next_packet;
		}
		if (!entry)
			return 0;
		size = entry[0];
		end = entry[1];
		after = index_of(v, v->next_packet + 1);
		if (v->primed && after && after[1] <= v->position)
		{
			/* the packet after this one ends before the position: this one
			isn't the overlap the position needs */
			v->next_offset += size;
			v->next_packet++;
			/* past both pieces: the reader moves to the one this packet is in */
			if (v->next_offset >= v->piece_wanted + 2 * PIECE_BYTES)
			{
				v->piece_wanted = v->next_offset & ~(PIECE_BYTES - 1);
				dec.wake = 1;
			}
			continue;
		}
		data = packet_bytes(v, v->next_offset, size);
		if (!data)
		{
			unsigned int first = PIECE_BYTES - (v->next_offset & (PIECE_BYTES - 1));
			const unsigned char *a = packet_bytes(v, v->next_offset, first < size ? first : size);
			const unsigned char *b = first < size ? packet_bytes(v, v->next_offset + first, size - first) : NULL;

			if (!a || !b || size > PIECE_BYTES)
				return 0;
			memcpy(joined, a, first);
			memcpy(joined + first, b, size - first);
			data = joined;
		}
		if (!pool_has_room())
		{
			dec.pool_failed++;
			return 0;
		}
		memset(&op, 0, sizeof(op));
		op.packet = (unsigned char *)data;
		op.bytes = size;
		op.granulepos = -1;
		op.packetno = v->next_packet + 3;
		if (vorbis_synthesis(&v->block, &op) == 0)
			vorbis_synthesis_blockin(&v->dsp, &v->block);
		while ((n = vorbis_synthesis_pcmout(&v->dsp, &pcm)) > 0)
		{
			/* this packet's frames end at `end`: the first after a restart
			gives none, and those before the voice's position are dropped */
			unsigned int from = end - (unsigned int)n, skip = 0, room;

			if (v->primed)
			{
				v->primed = 0;
				if (v->pcm_frames == 0 && v->position > from)
					skip = v->position - from < (unsigned int)n ? v->position - from : (unsigned int)n;
				else if (v->pcm_frames == 0)
					v->position = from;
			}
			room = PCM_FRAMES - v->pcm_frames;
			for (k = skip; k < (unsigned int)n && room; k++, room--)
			{
				v->pcm[v->pcm_frames * 2] = clip16(pcm[0][k] >> 9);
				v->pcm[v->pcm_frames * 2 + 1] = clip16(pcm[1][k] >> 9);
				v->pcm_frames++;
			}
			vorbis_synthesis_read(&v->dsp, n);
		}
		v->next_offset += size;
		v->next_packet++;
		if (v->next_offset >= v->piece_wanted + PIECE_BYTES + PIECE_BYTES / 2)
		{
			/* half the second piece played: the reader moves on a piece */
			v->piece_wanted += PIECE_BYTES;
			dec.wake = 1;
		}
		if (v->next_packet >= v->index_first + v->index_count * 3 / 4 &&
			v->index_first + v->index_count < music.chains[v->chain].packets)
		{
			v->index_wanted = v->next_packet;
			dec.wake = 1;
		}
		/* between two packets: audio and the ring served, and the job ends
		here if another slot's job waits (it goes on from here next time) */
		if (dec.yielding && psp_me_music_yield())
			return 2;
	}
	return 1;
}

/* the voice jumps: to another chain, or not where its queue would go on */
static void voice_seek(struct voice *v, int chain, unsigned int position)
{
	const struct pmus_entry *best = NULL;
	unsigned int e, offset;

	if (v->dsp_ready && v->data_chain == chain && v->chain == chain && position >= v->position &&
		v->next_packet > 0 && index_of(v, v->next_packet - 1))
	{
		/* on in the same chain: the decoder starts again a packet back (the
		overlap the position needs), and the index and pieces it has stay */
		if (!v->primed)
		{
			v->next_packet--;
			v->next_offset -= index_of(v, v->next_packet)[0];
		}
		vorbis_synthesis_restart(&v->dsp);
		v->position = position;
		v->primed = 1;
		v->pcm_frames = 0;
		v->fade = 0;
		dec.restarts++;
		dec.wake = 1;
		return;
	}
	/* the entry of this chain that holds the position: its first packet's
	byte offset is the stub's, walked on through the index if need be */
	for (e = 0; e < music.header.entries; e++)
	{
		const struct pmus_entry *x = &music.entries[e];

		if ((int)x->chain == chain && x->start <= position && (!best || x->start > best->start))
			best = x;
	}
	if (!best)
		return;
	offset = 0xFFFFFFFFu;
	for (e = 0; e < STUBS; e++)
		if (music.stubs[e].state == 2 && &music.entries[music.stubs[e].entry] == best)
			offset = music.stubs[e].first_offset;
	if (offset == 0xFFFFFFFFu)
		return;
	if (!v->dsp_ready)
	{
		vorbis_synthesis_init(&v->dsp, &dec.info);
		vorbis_block_init(&v->dsp, &v->block);
		v->dsp_ready = 1;
	}
	else
		vorbis_synthesis_restart(&v->dsp);
	v->chain = chain;
	v->next_packet = best->first ? best->first - 1 : 0;
	v->next_offset = offset;
	v->position = position;
	v->primed = 1;
	v->pcm_frames = 0;
	v->index_wanted = v->next_packet;
	v->piece_wanted = offset & ~(PIECE_BYTES - 1);
	if (v->data_chain != chain)
	{
		/* another chain's bytes: the reader reads this one's */
		v->index_count = 0;
		v->piece_ok[0] = v->piece_ok[1] = 0;
		v->data_chain = chain;
	}
	v->fade = 0;
	dec.restarts++;
	dec.wake = 1;
}

static struct voice *voice_for(const void *owner)
{
	int k, spare = -1;

	for (k = 0; k < VOICES; k++)
	{
		if (music.voices[k] && music.voices[k]->owner == owner)
			return music.voices[k];
		if (spare < 0 && music.voices[k] && (!music.voices[k]->owner || music.voices[k]->used > 8))
			spare = k;
	}
	if (spare < 0)
		return NULL;
	music.voices[spare]->owner = owner;
	music.voices[spare]->chain = -1;
	music.voices[spare]->left = music.voices[spare]->right = 0;
	return music.voices[spare];
}

/* a line for a hang to leave behind: written to the stick at once and the
stick synced, so a run that never comes back still names its last step */
static void music_trail(const char *format, unsigned long a, unsigned long b, unsigned long c)
{
	platform_log(format, a, b, c);
	psp_log_flush();
	sceIoSync("ms0:", 0);
}

/* ---------- the music job and its feed

A block's marks (psp_dsound.c, in the mixer's job on either CPU): its music
voices, their chains, their frames at 44.1 kHz and their gains. Each set is
the mixer's job's own lines; the music job reads the sets it is handed */
struct mark_set
{
	int count;
	const void *owners[MUSIC_MARKS_MAX];
	int chains[MUSIC_MARKS_MAX], left[MUSIC_MARKS_MAX], right[MUSIC_MARKS_MAX];
	unsigned int from[MUSIC_MARKS_MAX], to[MUSIC_MARKS_MAX];
} __attribute__((aligned(64)));
static struct mark_set marks[MUSIC_SETS];

/* the audio thread's own: the sets marked, handed out and collected, and
the music job out (1 with the ME, 2 run here) */
static struct __attribute__((aligned(64)))
{
	unsigned int head, finished, job_first, job_count, done_here;
	int current, out;
	SceUInt64 submitted_at;
} feed = { 0, 0, 0, 0, 0, -1, 0, 0 };

/* before the mixer's job (the audio thread): a set and its output buffer
are free for this block's marks. 0: no music this block (all four still
queued or playing: counted) */
int psp_music_prepare(void)
{
	if (!music.on || dec.setup_state < 0)
		return 0;
	if (sceKernelPollSema(music.out_free, 1) < 0)
	{
		music.sets_full++;
		return 0;
	}
	feed.current = (int)(feed.head % MUSIC_SETS);
	return 1;
}

/* in the mixer's job, after its mix: the block's marks into its set */
void psp_music_queue(const void *const *owners, const int *chains, const unsigned long *from, const unsigned long *to,
	const int *left, const int *right, int count)
{
	struct mark_set *m;
	int k;

	if (feed.current < 0)
		return;
	m = &marks[feed.current];
	if (count > MUSIC_MARKS_MAX)
		count = MUSIC_MARKS_MAX;
	m->count = count;
	for (k = 0; k < count; k++)
	{
		m->owners[k] = owners[k];
		m->chains[k] = chains[k];
		m->from[k] = (unsigned int)from[k];
		m->to[k] = (unsigned int)to[k];
		m->left[k] = left[k];
		m->right[k] = right[k];
	}
}

/* the main CPU decodes after the ME was handed music jobs: its copies of
what the ME wrote are dropped, and the voices start again from their
positions (a job the ME stopped in may have left them half done) */
void psp_music_own(void)
{
	int k;

	sceKernelDcacheWritebackInvalidateRange(music.pool_ram, POOL_BYTES);
	sceKernelDcacheWritebackInvalidateRange(&dec, sizeof(dec));
	sceKernelDcacheWritebackInvalidateRange(marks, sizeof(marks));
	for (k = 0; k < VOICES; k++)
		if (music.voices[k])
		{
			sceKernelDcacheWritebackInvalidateRange(music.voices[k], sizeof(struct voice));
			music.voices[k]->chain = music.voices[k]->data_chain = -1;
			/* their decoders lived in the pool, which starts again */
			music.voices[k]->dsp_ready = 0;
		}
	/* the pool and the setup made again at the next job, in main RAM if the
	ME's eDRAM (which this CPU can't reach) is gone with the ME */
	dec.setup_state = 0;
	dec.pool_in_edram = 0;
}

/* the first music job is out, making the codec setup (psp_dsound.c's mix
keeps its blocks off the ME meanwhile) */
int psp_music_holds_me(void)
{
	return feed.out == 1 && dec.setup_state == 0;
}

/* the pool and the codec setup, on the job's CPU the first time: the ME's
eDRAM on the real ME, main RAM in the emulator or with the sound cache in
the eDRAM. 1 when the setup is there */
static int setup_here(int on_me, int edram_free)
{
	unsigned int at = 4, k;

	if (dec.setup_state)
		return dec.setup_state > 0;
	dec.pool_in_edram = on_me && edram_free;
	dec.pool = dec.pool_in_edram ? (unsigned char *)(ME_EDRAM + ME_EDRAM_MUSIC) : music.pool_ram;
	dec.pool_end = dec.pool + POOL_BYTES;
	dec.pool_next = dec.pool_peak = dec.pool;
	dec.pool_last = NULL;
	vorbis_info_init(&dec.info);
	vorbis_comment_init(&dec.comment);
	for (k = 0; k < 3 && music.setup; k++)
	{
		ogg_packet op;
		unsigned int size;

		memcpy(&size, music.setup + at, 4);
		if (at + 4 + size > music.setup_bytes)
			break;
		memset(&op, 0, sizeof(op));
		op.packet = (unsigned char *)music.setup + at + 4;
		op.bytes = size;
		op.b_o_s = k == 0;
		op.packetno = k;
		if (vorbis_synthesis_headerin(&dec.info, &dec.comment, &op))
			break;
		at += 4 + size;
	}
	dec.setup_state = k == 3 ? 1 : -1;
	return dec.setup_state > 0;
}

/* a set's first half: each marked voice decoded up to its block's end,
resumable (nothing played yet). ok[k]: 1 ready, 0 late. 2: the job gives way */
static int set_decode(const struct mark_set *m, int *ok)
{
	int k, count = music.nodecode ? 0 : m->count;

	for (k = 0; k < count; k++)
	{
		struct voice *v = voice_for(m->owners[k]);
		int r;

		ok[k] = 0;
		if (!v || m->to[k] <= m->from[k])
			continue;
		/* a gap the queue can hold is decoded through and dropped below, so
		the decoder keeps its step: after a late block, or a skip the game made */
		if (v->chain != m->chains[k] || m->from[k] < v->position || m->to[k] - v->position > PCM_FRAMES / 2)
			voice_seek(v, m->chains[k], m->from[k]);
		if (v->chain != m->chains[k])
			continue;
		r = decode_to(v, m->to[k]);
		if (r == 2)
			return 2;
		ok[k] = r;
	}
	return 1;
}

/* its second half, never interrupted: the ready voices summed into its output */
static void set_mix(const struct mark_set *m, const int *ok, short *out)
{
	int *sums = dec.sums;
	int k, playing = 0, count = music.nodecode ? 0 : m->count;

	memset(sums, 0, sizeof(dec.sums));
	for (k = 0; k < VOICES; k++)
		if (music.voices[k])
			music.voices[k]->used++;
	for (k = 0; k < count; k++)
	{
		struct voice *v = voice_for(m->owners[k]);
		unsigned long frames = m->to[k] - m->from[k], i, step;
		int gain_left, gain_right, ramp_left, ramp_right;

		if (!v || m->to[k] <= m->from[k])
			continue;
		v->used = 0;
		if (!ok[k])
		{
			/* late: silent this block, its place kept, so the bytes the
			reader is fetching are the ones the next block decodes; it fades
			back in */
			dec.late_blocks++;
			v->fade = 0;
			continue;
		}
		playing++;
		/* drop what lies before the block (a skip the game made) */
		if (m->from[k] > v->position)
		{
			unsigned int drop = m->from[k] - v->position;

			memmove(v->pcm, v->pcm + drop * 2, (v->pcm_frames - drop) * 2 * sizeof(short));
			v->pcm_frames -= drop;
			v->position += drop;
		}
		/* the block's frames, stretched to the output's (pitch 1 is one to one) */
		step = (frames << 16) / OUT_FRAMES;
		gain_left = v->left << 8;
		gain_right = v->right << 8;
		ramp_left = ((m->left[k] - v->left) << 8) / OUT_FRAMES;
		ramp_right = ((m->right[k] - v->right) << 8) / OUT_FRAMES;
		for (i = 0; i < OUT_FRAMES; i++)
		{
			unsigned long f = (i * step) >> 16;
			int fade = v->fade < FADE_FRAMES ? v->fade++ : FADE_FRAMES;

			sums[2 * i] += ((v->pcm[2 * f] * (gain_left >> 8)) >> 14) * fade / FADE_FRAMES;
			sums[2 * i + 1] += ((v->pcm[2 * f + 1] * (gain_right >> 8)) >> 14) * fade / FADE_FRAMES;
			gain_left += ramp_left;
			gain_right += ramp_right;
		}
		v->left = m->left[k];
		v->right = m->right[k];
		memmove(v->pcm, v->pcm + frames * 2, (v->pcm_frames - frames) * 2 * sizeof(short));
		v->pcm_frames -= (unsigned int)frames;
		v->position += (unsigned int)frames;
	}
	for (k = 0; k < VOICES; k++)
		if (music.voices[k] && music.voices[k]->used > 8)
			music.voices[k]->owner = NULL;
	for (k = 0; k < OUT_FRAMES * 2; k++)
		out[k] = clip16(sums[k]);
	dec.blocks++;
	if ((unsigned long)playing > dec.playing_worst)
		dec.playing_worst = playing;
}

/* the music job (ME_JOB_MUSIC, psp_me.c; or here when the ME isn't up):
args[0] the first set, args[1] how many, args[2] 1 when the eDRAM's room
is free for the pool (the sound cache isn't there). results[0]: the sets finished, in
order; the rest wait for the next job. No kernel call reachable from here */
static void music_job_body(volatile unsigned int *args, volatile unsigned int *results, int on_me)
{
	unsigned int first = args[0], count = args[1], k;
	int ok[MUSIC_MARKS_MAX];

	dec.yielding = on_me;
	results[0] = 0;
	if (!setup_here(on_me, (int)args[2]))
	{
		/* no codec: the sets play silent */
		for (k = 0; k < count; k++)
			memset(dec.out[(first + k) % MUSIC_SETS], 0, sizeof(dec.out[0]));
		results[0] = count;
		dec.yielding = 0;
		return;
	}
	for (k = 0; k < count; k++)
	{
		const struct mark_set *m = &marks[(first + k) % MUSIC_SETS];

		if (set_decode(m, ok) == 2)
		{
			dec.stops++;
			break;
		}
		set_mix(m, ok, dec.out[(first + k) % MUSIC_SETS]);
		results[0] = k + 1;
	}
	dec.yielding = 0;
}

static struct { volatile unsigned int *args, *results; } here_call;

static void music_job_here(unsigned int unused)
{
	(void)unused;
	music_job_body(here_call.args, here_call.results, 0);
}

void psp_music_job(volatile unsigned int *args, volatile unsigned int *results, int on_me)
{
	if (on_me || !music.stack)
	{
		music_job_body(args, results, on_me);
		return;
	}
	/* on the main CPU: on its own stack, not the caller's */
	here_call.args = args;
	here_call.results = results;
	psp_me_sim_call(0, music.stack + MUSIC_STACK_BYTES, music_job_here);
}

/* the job collected (the audio thread): the finished sets to the output,
what the ME wrote dropped from this cache, the reader woken, the state let go */
static void music_collect(void)
{
	unsigned int results[ME_JOB_RESULTS], done, k;
	int finished;

	if (!feed.out)
		return;
	if (feed.out == 1)
	{
		/* waiting for the ME's idle time isn't a failure: the job is given
		up only once the ME has stopped (the audio and render jobs' own
		limits catch a hung ME), and meanwhile nothing here waits */
		if (!psp_me_submitted_done(ME_SLOT_MUSIC))
		{
			if (psp_me_running())
			{
				if (!music.slow_said && sceKernelGetSystemTimeWide() - feed.submitted_at > 500000)
				{
					music.slow_said = 1;
					music_trail("music job: still waiting for the ME's idle time 500 ms after it was handed over (job %lu, sets %lu, the ME's stops %lu)",
						music.jobs, feed.job_count, psp_me_stops);
				}
				return;
			}
		}
		finished = psp_me_finish(ME_SLOT_MUSIC, results);
		sceKernelDcacheInvalidateRange(&dec, DEC_READ_BYTES);
		sceKernelDcacheInvalidateRange(dec.out, sizeof(dec.out));
		for (k = 0; k < VOICES; k++)
			if (music.voices[k])
				sceKernelDcacheInvalidateRange(music.voices[k], (VOICE_DECODE_BYTES + 63) & ~63u);
		if (!finished)
		{
			/* the ME stopped: its eDRAM is taken as lost with the setup in
			it, the decoders start again in main RAM, and the sets it held
			play silent */
			music_trail("music job: the ME failed it (job %lu, sets %lu, the ME's stops %lu); the decode goes on on this CPU",
				music.jobs, feed.job_count, psp_me_stops);
			psp_music_own();
			for (k = 0; k < feed.job_count; k++)
				memset(dec.out[(feed.job_first + k) % MUSIC_SETS], 0, sizeof(dec.out[0]));
			sceKernelDcacheWritebackRange(dec.out, sizeof(dec.out));
			results[0] = feed.job_count;
		}
	}
	else
	{
		results[0] = feed.done_here;
		sceKernelDcacheWritebackRange(dec.out, sizeof(dec.out));
	}
	done = results[0] <= feed.job_count ? results[0] : feed.job_count;
	if (!music.first_collected)
	{
		music.first_collected = 1;
		music_trail("music job: the first collected after %lu us; the codec setup %ld (1 made), the pool in the eDRAM %lu",
			(unsigned long)(sceKernelGetSystemTimeWide() - feed.submitted_at), (unsigned long)dec.setup_state,
			(unsigned long)dec.pool_in_edram);
		if (music.mestop_test)
		{
			extern void psp_me_stop(const char *why);

			psp_me_stop("musicmestop: the music job's fallback tested");
		}
	}
	feed.finished += done;
	for (k = 0; k < done; k++)
		sceKernelSignalSema(music.out_ready, 1);
	feed.out = 0;
	if (dec.wake)
	{
		dec.wake = 0;
		sceKernelSignalSema(music.reader_wake, 1);
	}
	sceKernelSignalSema(music.state_lock, 1);
}

/* the sets marked and not yet finished, handed to a music job: the ME's
when it runs (its idle time; the audio thread doesn't wait), else run here.
Not at all this block if the reader holds the state (next block) */
int psp_sound_edram_mode(void);

static void music_submit(void)
{
	unsigned int args[3];

	if (feed.out || feed.head == feed.finished || sceKernelPollSema(music.state_lock, 1) < 0)
		return;
	/* an ME stopped (a sleep, a failure) since the setup took its eDRAM with it */
	if (dec.setup_state && dec.pool_in_edram && psp_me_stops != music.stops_seen)
		psp_music_own();
	music.stops_seen = psp_me_stops;
	feed.job_first = feed.finished % MUSIC_SETS;
	feed.job_count = feed.head - feed.finished;
	args[0] = feed.job_first;
	args[1] = feed.job_count;
	args[2] = psp_sound_edram_mode() == 0;
	/* what the job reads: the marks, the voices and pieces, the stubs */
	sceKernelDcacheWritebackAll();
	feed.submitted_at = sceKernelGetSystemTimeWide();
	music.jobs++;
	if (music.jobs == 1)
		music_trail("music job: the first handed over (%lu sets; the eDRAM free for the pool %lu, the ME's stops %lu)",
			feed.job_count, args[2], psp_me_stops);
	if (psp_me_submit(ME_SLOT_MUSIC, ME_JOB_MUSIC, args, 3))
		feed.out = 1;
	else
	{
		/* no ME: the job here, on this CPU, never with a pool in the
		eDRAM (which this CPU can't reach): the decoders start again */
		unsigned int results[ME_JOB_RESULTS];

		if (dec.pool_in_edram)
			psp_music_own();
		music.jobs_here++;
		psp_music_job(args, results, 0);
		feed.done_here = results[0];
		feed.out = 2;
	}
}

/* after the mixer's job and its lock (the audio thread), every block:
marked counts this block's set; the job out collected if it's done, and
the queue handed on */
void psp_music_after_block(int marked)
{
	SceUInt64 t0;

	if (!marked && !feed.out && feed.head == feed.finished)
		return;
	t0 = sceKernelGetSystemTimeWide();
	if (marked)
		feed.head++;
	feed.current = -1;
	music_collect();
	music_submit();
	music_collect();
	t0 = sceKernelGetSystemTimeWide() - t0;
	music.decode_us += (unsigned long)t0;
	if (t0 > music.decode_worst_us)
		music.decode_worst_us = (unsigned long)t0;
}

/* the 44.1 kHz channel: the sets in order, MUSIC_LEAD silent blocks first
and again after it ran dry, so the job has that many blocks of room */
static int output_thread(SceSize arguments, void *argument)
{
	static short silence[OUT_FRAMES * 2] __attribute__((aligned(64)));
	unsigned int which = 0;
	int lead = MUSIC_LEAD;

	(void)arguments;
	(void)argument;
	sceKernelDcacheWritebackRange(silence, sizeof(silence));
	for (;;)
	{
		if (sceKernelPollSema(music.out_ready, 1) < 0)
		{
			music.out_waited++;
			sceKernelWaitSema(music.out_ready, 1, NULL);
			lead = MUSIC_LEAD;
		}
		for (; lead > 0; lead--)
			sceAudioOutputBlocking(music.out_channel, PSP_AUDIO_VOLUME_MAX, silence);
		sceAudioOutputBlocking(music.out_channel, PSP_AUDIO_VOLUME_MAX, dec.out[which]);
		which = (which + 1) % MUSIC_SETS;
		sceKernelSignalSema(music.out_free, 1);
	}
	return 0;
}

/* the heartbeat's line */
void psp_music_report(void)
{
#if PSP_METRICS
	if (!music.on)
		return;
	platform_log("music: %lu blocks, late %lu, no set free %lu; voices at most %lu; restarts %lu; pieces %lu (worst %lu us); "
		"stubs %lu (started ahead %lu, used %lu times; %lu walks, %lu index reads), not in the pack %lu; main CPU after the job %lu us a block (worst %lu); pool peak %lu KB%s",
		dec.blocks, dec.late_blocks, music.sets_full, dec.playing_worst, dec.restarts, music.pieces,
		music.piece_worst_us, music.stubs_made, music.prefetched, dec.prefetch_hits, music.walks, music.index_reads, music.not_in_pack, dec.blocks ? music.decode_us / dec.blocks : 0,
		music.decode_worst_us, (unsigned long)(dec.pool_peak - dec.pool) / 1024, dec.pool_failed ? ", full" : "");
	platform_log("music job: %lu jobs (%lu on this CPU), %lu gave way to another slot's job; the output waited %lu times "
		"(each then %d blocks of silence: the music trails the mix by %d ms); the pool in %s; the codec setup %s",
		music.jobs, music.jobs_here, dec.stops, music.out_waited, MUSIC_LEAD, MUSIC_LEAD * OUT_FRAMES * 1000 / 44100,
		dec.pool_in_edram ? "the ME's eDRAM" : "main RAM",
		dec.setup_state > 0 ? "made" : dec.setup_state == -1 ? "refused" : "not yet made");
#endif
}
