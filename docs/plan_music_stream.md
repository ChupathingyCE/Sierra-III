# The music path: Vorbis from the stick, decoded on the ME, on a 44.1 kHz output

Step 3 of `audio_fidelity.md` (E5 with its fidelity half). The decision it follows: the plan is worth its cost; the memory comes from music leaving the sound cache; the codec is Vorbis (Tremor), the cheaper of the two on the ME. Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code or data, [U] inference.

## What it starts from

- **Music in the cache** [E] (300 s each): music holds up to 942 KB (b30), 767 (a10), 415 (c10) of the 1,792 KB sound cache, 194 / 345 / 64 KB on average; it is 23% / 53% / 18% of the cache's stick reads (22.7 / 28.9 / 3.6 KB/s), and those reads are re-reads of music the cache evicted (216 / 273 / 34 loads).
- **The codec on the PSP** [M] (the audiotest pair): Tremor took 10% of the ME at real time for a stereo 44.1 kHz stream at ~96 kbps (Opus 33%); a plain 44.1 kHz channel beside the game's 22,050 Hz mix had 0 late blocks of 6,864, and the mix 0 of 435.
- **How the game plays a long sound** [D]: a music sound tag (class 32, `_sound_class_music`) has pitch ranges of permutations; a long piece is a chain of permutations played back to back (`next permutation index`), each loaded into the sound cache on its own (`sound_cache_start_loading_sound`, one stick read of its samples). A channel feeds its stream packets that point into the cache (`dsound_channel_queue_packet`, `pContext` = the permutation), and the mixer completes them on its own clock; the game moves to the queued permutation when the stream has taken the last packet. Tracks, loops, alternates, crossfades and volume are the looping-sound code's, on top of this.
- **The bake** [D]: `soundpack.py` (S9) decodes each chain as one signal, halves music to 22,050 Hz and re-encodes it as Xbox ADPCM, the chunks keeping their share of the chain in whole 64-sample blocks.

## The design

### 1. Which sounds: music, by the tag's own class

A permutation is on the music path when its sound tag's class is `_sound_class_music` and the map's music pack has an entry for it. Nothing else changes path: ambience at 44.1 kHz on the disc stays in the main mix (S9's 22 kHz), as do effects and dialogue (the 10-14 decision). A music permutation the pack doesn't have plays the old way, from the cache, so a stale or partial pack costs only fidelity.

### 2. The bake: one Vorbis stream per chain, beside the map (bake flag `--music-stream`)

- Where the S9 code has each music chain as one signal at 44.1 kHz (before `half_rate`), it also encodes that signal as Vorbis (stereo, 44.1 kHz, VBR quality 3, ~112 kbps; a mono chain is encoded as stereo: identical channels cost Vorbis almost nothing and every stream then shares one codec setup). The map is built exactly as without the flag (S9's ADPCM music stays in it), so the default build plays the same map, and the flag's only output is `maps/<map>.pmus`. **The pack is game data**: it is written to the bake's output on the machines that bake, and never committed, uploaded or logged (the bake log gets counts and bytes only, as the sound bake's summary does).
- The encoder is ours, small (`bake/bundle/musicenc.c`, raw 16-bit PCM in, packets and their end positions out), linked against the libvorbis and libogg the bundle already builds from pinned sources for Invader. No Ogg pages are kept: the pack stores bare packets.
- `<map>.pmus` (little-endian): a header (magic, version, counts); the codec setup's three header packets, once; a sorted table of entries, one per music permutation, keyed by (a hash of the tag's path, pitch range, permutation), each naming its chain, its first frame in the chain at the map's rate, its frame count and the first packet to decode for it (one packet before its start, Vorbis's overlap); per chain, its packet table (each packet's bytes and the 44.1 kHz frame it ends on) and its packets, contiguous, so a chain reads from the stick front to back.
- Positions map exactly: S9 halves the rate, so a frame f of the map's 22,050 Hz chain is frame 2f at 44.1 kHz; the chunk joins are whole 64-sample blocks of the 22 kHz chain, i.e. 128-frame boundaries in the stream. A bake without S9 (the disc's sounds) records a ratio of 1.

### 3. The cache: a one-page stub for each music permutation (frees the room)

With the keep word on, `sound_cache_start_loading_sound` (a fixup) gives a music permutation the pack has a one-page block (4 KB) instead of its samples, and asks the music reader for its first piece instead of reading its samples; `loaded` is set when that piece is in RAM. Everything the game keeps about it stays as it is: the block's datum, its software and hardware references, the LRU, the request and wait in `_sound_cache_sound_request`, the permutation's `samples.size`. The queueing range check in `dsound_channel_queue_packet` lets a stub's packets through (they run past the stub; nothing reads them). So the channel's packets, their sizes and the order the game queues and completes them are the same numbers as today.

### 4. The game's control, kept exactly: the music voice stays in the 22 kHz mixer, silent

The game's stream for a music channel stays a voice of the 22,050 Hz mixer: positions, packet completions, pitch, gains, pauses and stops run through the same code, on the same clock, as now. It is marked as a music voice (from the packet's permutation) and the mixer skips its samples: it adds nothing to the 22 kHz sum and reads nothing from the cache. Per block, the mixer notes each music voice's position at the block's start and end and its two gains (`voice_gains`, the game's volume and pan as for any 2D voice). The 44.1 kHz side plays those frames at twice the positions, with those gains, ramped across the block as the mixer ramps. So start, stop, loops, alternates, crossfades (two channels, two voices) and volume are the game's own, and the game's state never sees the new path: play stays exact with `nomouth` [U, to prove].

### 5. Decoding: inside the audio job, in step with the mix

A 22,050 Hz block of 512 frames is 23.2 ms, the same as 1,024 frames at 44.1 kHz, about one long Vorbis packet. So the music decode rides the mixer's own job (`psp_dsound_mix_list`, on the ME as `ME_JOB_AUDIO`, on the main CPU when the ME doesn't answer), after the 22 kHz block is mixed: for each music voice, Tremor decodes packets into the voice's small PCM queue until it covers the block's frames, then the voices are summed with their gains into the 44.1 kHz block. Why not a separate low-priority ring job with a large PCM ring, as the 10-14 plan had it:
- A ring job is pushed from the game thread once a frame; at 20 fps the audiotest's paced jobs fell a second behind about 50 times in 5 minutes [M]. The mixer's job runs every 23 ms whatever the frame rate.
- In step, the PCM queue is one packet ahead per voice (~12 KB), not a second of PCM (176 KB): RAM is the scarce side.
- The game's changes (a new permutation, a gain, a stop) reach the music in the same block as the effects, with nothing decoded ahead to throw away.
The cost: the decode runs at audio priority, inside long render jobs when an audio block is mixed there (`psp_me_yield`). It is ~2.3 ms of ME a block per stereo voice [M: 10% of real time], two voices during a crossfade, against ~45-50% of the ME free on average and ~37-45% in the worst windows under the full speed set [M]. Tremor's state and queues live in one pool; whichever CPU mixes a block owns them, handed over by the mixer's rules (the ME writes them back at the job's end; the main CPU drops its copies only before it mixes a block itself, which happens when the ME doesn't answer). Tremor's `malloc` family is renamed to that pool at link, as in the audiotest: the ME never enters the game's allocator.

### 6. Output: a plain 44.1 kHz channel and its thread

The audiotest's output: `sceAudioChReserve` at 1,024 frames, a thread at the mixer's priority that waits for block k's music (a semaphore the audio thread posts after the mix) and plays it; the SRC channel keeps the 22 kHz mix. A block that isn't ready plays as silence and is counted. Both outputs run at the same block length, so the music stays within a block of the effects.

### 7. The stick: a reader thread, 16 KB pieces, one read at a time

- A low-priority thread reads the pack in pieces of 16 KB (about 1.2 s at 112 kbps): a stub's first piece when the game asks for the permutation (so it is there when the channel starts, as the samples are today), then each voice's next piece as soon as the piece before it is half played, so every read is issued about a second before it is needed. A chain's data is contiguous, so a voice reads straight on across its chunk joins; the next chunk's own request finds its piece already read.
- Pieces: 2 per voice for 2 voices, and 2 for first pieces asked for ahead (alternates, the next track): 6 x 16 KB = 96 KB. One read in flight at a time, so a music read delays a texture or sound load by at most one 16 KB read [U ~10 ms].
- Rate: ~14 KB/s per playing voice, against today's 22.7 KB/s (b30) and 28.9 (a10) of music re-reads [E], and no music in the cache's own reads.

### 8. A dropout

If a voice's next packet isn't in RAM in time (the stick late) or its decoder lost its state (the ME stopped mid-job), that voice is silent for the block (counted: `music: late blocks`); the next block decodes from the packet before its position again (Vorbis's overlap) and fades in over 5 ms. The game's packets complete on the mixer's clock all the same, so the game never waits and the music comes back in step. A pack that is missing or doesn't match the map (its header carries the map's music tags' hash) turns the path off for that map, said once in the log.

## Memory: what it frees, what it costs, where the room goes

| | b30 | a10 | c10 |
|---|---|---|---|
| Music held in the cache, peak / mean [E] | 942 / 194 KB | 767 / 345 KB | 415 / 64 KB |
| Held by its stubs instead (4 KB each, evictable) [U] | ~120 / 25 KB | ~100 / 45 KB | ~55 / 8 KB |

The path's own RAM [U, measured at the first build]: Tremor's code ~80 KB (off the heap at link); one shared codec setup (unpacked codebooks) ~100-200 KB; per voice ~30 KB of decoder state and ~12 KB of PCM queue; 96 KB of pieces; 8 KB of output blocks: **~330-430 KB in all with two voices.** That is more than music's average hold on b30 and c10, so the path does not pay in RAM on every map: it pays at the peaks (where the cache is full and music pushes effects out) and in stick reads. The proposal:
- The heap pays for nothing: the path's state, pieces and output come out of the sound cache's block (the cache is made smaller by exactly that), and its code is paid by the same amount off the cache's default (`soundN`). So the heap is unchanged.
- The cache's room for everything but music then changes by (music's hold, now gone) minus (the path's RAM): at the peaks +500 to +800 KB on b30, +300 to +650 on a10; on average -100 to -250 KB on b30 and c10, about even on a10.
- Whether that is a net gain is the count's question, not the table's: **effects' stick re-reads by class** (the music1 count's `loads:` line, without music) on b30, a10, c10, with the path on and off at the same total RAM. If c10 or b30 re-read more effects, the next lever is Tremor's state (its low-memory branch from source, which unpacks codebooks as it reads them) before any cache size moves.

## What it costs in time

- **ME:** +10% of real time per playing music voice [M], less the 22 kHz mix of music's ADPCM (two channels of decode and resample a voice, now skipped) [U, small].
- **Main CPU:** the output thread's wake a block, the reader's one read a second, the stub path in the cache (cheaper than a sample load); the music voice's sample work leaves the 22 kHz mixer. Expected about even [U]; measured in the emulator's cache model on the b30 flight (the fair stretch) and on the PSP (`hwprofile`, `gebusy`).
- **Stick:** ~14 KB/s per playing voice, in 16 KB reads, instead of the music re-reads.

## Restoration or change

- **Restoration:** music at 44.1 kHz with its full bandwidth, the disc's own rate (S9 halved it to 22,050 Hz and everything above 11 kHz was lost). The re-encode is lossy (a second generation of the disc's ADPCM, as S9's was), at a rate chosen to be transparent.
- **Changes** (Sheepdog01's listening, speakers and headphones): music no longer passes the 22 kHz mix's 16-bit limit, so a loud moment of music and effects together sums in the PSP's output hardware instead; the music can sit up to one block (23 ms) off the effects; the joins between chains are decoder restarts (the joins inside a chain are continuous).

## Keep words, switches, checks

- `musicstream` (off by default): the path. Off, nothing of it runs: the map is the same, the stub path and the voice marks are skipped, no thread, no pool. The bake flag `--music-stream` writes only the pack.
- Build: Tremor and libogg link with the path's units in every build (the keep word decides at launch); multiplayer builds compile it the same; `PSP_METRICS=0` keeps the counts' lines out.
- Proof before it's called done: an all-maps bake with `--music-stream` succeeds; emulator: play and both seeds identical with `nomouth` on b30 and a10 (on against off), every music permutation of b30 and a10 found in the pack, no late music blocks, the cache's music hold gone (the count's line), effects' re-reads by class, the main CPU's cost on the b30 flight; PSP: ME share and late blocks on both outputs, one run a side.
- Log lines: `music:` every heartbeat: voices playing, late blocks, decode us a block (ME), pieces read and their worst wait, stubs, permutations not in the pack.

## Order of the work

1. The encoder, the bake flag and the pack (bake side); an all-maps bake.
2. The reader, the stubs and the silent voices with the decode on the main CPU only (the emulator proves exactness and positions).
3. The decode in the ME's audio job, the 44.1 kHz output, the counts; the PSP run.
4. The cache's split (the path's RAM out of the cache's block) and the effects' re-read count.
