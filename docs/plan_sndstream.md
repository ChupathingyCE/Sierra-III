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
