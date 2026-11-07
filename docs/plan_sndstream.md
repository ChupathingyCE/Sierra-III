# Long sounds streamed (SNDSTREAMLONG): the plan

A build plan (master, 2026-11-06 late; Sheepdog01 19:00 "sorting sounds by type", 23:19 "build it overnight, off by default"). The lab's `levers.tsv` row is SNDSTREAMLONG. Labels: [M] measured on the PSP, [E] emulator or estimate, [D] read in code or data, [U] inference.

## Why

- **Ambient loops hold the sound cache where it's tight** (lab soundclass2 [E], mono bake): at d20's busiest moment 1,038 of the 1,235 KB playing are 19 ambient loops; elsewhere peaks are 507-963 KB with ambient 31-430 and weapons never over 67 KB.
- **The cache must hold every playing loop whole** today, so d20 needs 1,792 KB and still holds sounds back (soundsweep3). Streamed, a loop needs only the few KB around its play position.
- **The stick has room** [M, halo_bench10]: 16 MB/s for large reads; a 22 kHz mono Xbox ADPCM loop is ~11 KB/s; every map's reads today are under ~2% of the stick (soundclass2's iostats).
- **Xbox ADPCM blocks stand alone** [D, decode_block]: each 36-byte block (per channel) carries its own predictor and step, so any block can be decoded from wherever it lies in RAM.

## What it does

1. **Which sounds:** permutations of the ambient classes (33-35, soundclass's `ambient` group) at or over 32 KB. Slice 2 may add the long device and vehicle loops (classes 26-30, 22-23) if the census shows they pay. Dialogue stays cached (one-shot, and first claim on the cache: a later slice).
2. **The cache holds a stub:** as the music path does (`docs/plan_music_stream.md` §3), `sound_cache_start_loading_sound` gives a streamed permutation a one-page block instead of its samples and asks the stream reader for its first piece; `loaded` is set when that piece is in RAM. The game's datum, references, LRU, requests and packets are unchanged, so play stays exact (as the music path's control does, §4).
3. **Pieces:** a shared pool of pieces in the sound block (the cache gives up the same bytes), each a whole number of ADPCM blocks (8 KB rounded down to 36 x channels bytes). A streamed voice holds up to three: the one playing, the next, and the one after when the reader is ahead. A looping permutation's reads wrap to its start.
4. **The reader:** one thread at the stick's I/O priority, one read in flight (`cache_file_read`'s path: the sound's tag and file offset, as an upload today). It refills a voice's next piece once its play position passes half the current one, so each read is issued well ahead of need.
5. **The mixer:** `decode_block` finds a streamed packet's block in the voice's pieces (block index to piece and offset). A block not yet read plays silent and is counted (`sndstream: late blocks`); the next block decodes normally, since blocks stand alone.
6. **The ME:** pieces are read by DMA into main RAM; the mixer's rules for cache coherence apply as for the cache's own uploads (the reader writes back and invalidates before handing a piece over; the ME never calls the allocator).

## Proof and price

- **Off by default** behind `sndstream`; nothing changes without it.
- **Correct:** play and seeds identical with it on (statecrc); no late blocks on d20's swamp, b30's beach and the hands-off maps; the audio check (`docs/audio_ab.md`) equal to off where nothing is late.
- **Memory:** the pool's size and the cache's room per map, with sndwait's holds, at today's sizes and at SNDPERMAP's; then the per-map sizes are cut again.
- **Priced on the PSP:** one pair on d20's swamp (`- | sndstream`), since the ME's mix and the stick's contention are what the emulator can't time.

## Order of the work

1. The stub and the reader with one streamed voice, logged, mixer still reading the cache's copy (both read, compared): proves the reader and the piece mapping.
2. The mixer reads pieces; the cache holds stubs; late blocks counted.
3. The pool from the sound block; the cache's size cut by it; d20 and b30 proofs.
4. The census of what else pays (devices, vehicles); the PSP pair.

## Built so far (slices 1-2)

Built only with `PSP_SNDSTREAM=1` (`FIXES_SNDSTREAM`, the SNDSTREAMLONG sections of `psp_dsound.c`); a default build is the old code. Keep words: `sndstream` (stubs; the mixer plays the pieces), `sndstreamcheck` (the proof: the cache loads each such sound whole, and each block's piece is compared with the cache's copy as it plays, and each piece with that copy as it lands), `sndstreampiecesN` (the pool, default 32).

- **Which sounds:** ambient classes 33-35, Xbox ADPCM, 32 KB or more (`psp_sndstream_choose`, before the block is made); a stub is one page (4 KB).
- **Pieces:** 8,136 bytes (113 stereo or 226 mono blocks, so no block straddles two), each in an 8 KB slot, keyed by the sound's id (one per placing) and the piece's number, so voices playing one sound share them. The pool is the top of the cache's main RAM (the main tier's, beside `sndedram`, which is on by default), the same place for every map; the cache stops short of it (`psp_sound_map_pages`). The heap is unchanged.
- **The reader** runs once a frame on the game's thread (`psp_sound_upload_poll`, from `sound_cache_idle`, after the frame's packets are queued) and reads through `cache_file_read`, as the cache's loads do, four reads in flight. What to read is each stream's game-side queue (the packets not yet played out, two pieces ahead per channel), nearest first; a piece no voice needs is reused, a sound's first piece last; a piece being read never is. [D] A plain thread was not used: `cache_file_read`'s request slots are taken without a lock, so only one thread may queue reads; the stick's own thread does the reading.
- **A stub's first piece** completes into the engine's `loaded` flag itself: a blocking request spins on that flag on the game's thread, where the reader runs. The mixer takes a piece being read once its flag says done (its bytes are whole in RAM).
- **The mixer** (`stream_block`, on whichever CPU mixes, the ME included): a streamed packet carries the sound's id and where it starts in the samples, so a block's bytes are found by their place in the sound, not by the packet's address (which runs past the stub). A block whose piece isn't in plays silent and is counted (`sndstream: late blocks`). The ME reads only the piece table and the pieces, which only the main CPU writes; a streamed voice takes the whole-cache hand-over under `meaudioranges`.
- **The tiers' fence** (not behind the switch): the cache's fence between its main tier and the eDRAM tier now follows each map's main tier. Set once at start-up, it let a block straddle a cut main tier's end, its tail in main RAM the map no longer gives the cache: with the stream that was the pool (whole pieces overwritten, "outside the valid range" 6-11 times a run on d20); with `sndpermap` alone the same refusals (its ladder 12, off 0).

Proofs [E] (emulator, mono bake w162mono, the lab's sierra-batch 42346261956, one `PSP_SNDSTREAM=1` build, statecrc, nomouth):
- **Slice 1:** d20's swamp to 300 s: 818,308 blocks checked equal, 0 differed; 2,982 pieces equal to the cache's copy as they landed; b30's route to 200 s: 20,147 and 85, 0 differed.
- **Slice 2:** play and both seeds identical to off at every tick (d20 9,000, b30 6,000); late blocks 0 (d20 824,817 blocks from pieces, b30 19,635, the front door into a10 99,525); d20: 0 sounds held back (off: 2, Cortana's line among them), 0 refusals for room (off 11), with the cache at 1,536 KB (the pool's 256 KB off it) and stubs standing for up to 1,076 KB.
- **The pool's size:** d20 needed at most 38 pieces at once (each just-placed sound's first piece held 3 s counts); at 24, 1,980 late blocks; at 48 (384 KB off the main tier, 1,136 -> 752 KB) Cortana's 162 KB line was held back 2.2 s for want of a run long enough; at 32 neither.

Next (slices 3-4): the pool sized per map (b30 needed 4 pieces) and the cache cut by what the stubs free; the census of what else pays (devices, vehicles); the PSP pair on d20's swamp (`- | sndstream`). Stick reads are ~80 KB/s on d20 (23.7 MB in 300 s, 8 KB each); the emulator's read times (mean 82 ms) are not the stick's.
