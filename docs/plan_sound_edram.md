# The sound cache's second tier in the ME's eDRAM (SNDEDRAM2, `sndedram`)

The goal (Sheepdog01, 2026-11-03): keep the sound's fidelity and move memory out of main RAM without a speed penalty. The sound cache is the heap's biggest owner (1,792 KB, one start-up block) and b30's heap is ~130 KB from empty at the bunker approach [M]. R2 (`soundedram`) moved the whole cache into the eDRAM and was rejected: its slow staged loads cost d20 +11-14% a frame, most of it the yields SNDNOYIELD has since removed [M, E]; it also took the eDRAM AIME now uses. This is the smaller step: part of the cache in the eDRAM, filled by DMA.

## What's known

- **DMACplus writes the ME's eDRAM exactly from the main CPU's side** [M, PSP-1000, `bench/halo_bench15`]: 254-256 MB/s at 16-byte transfers, one descriptor of up to 4,095 counts (65,520 bytes). The ME's side address is the eDRAM's offset (its eDRAM at 0). A chained descriptor list hung the PSP, so transfers stay single and bounded. At 99% duty the main CPU's misses got 44% slower; a sound load's duty is under 1%.
- **The main CPU can't read the eDRAM; the ME can.** The mixer runs on the ME and is the only reader of a sound's bytes [D, `psp_dsound.c`; R2's audit, `me_programme.md` section 5]. The engine checks a queued sound's address against the cache's range (`sound_dsound_xbox.c`), and that check is all it does with it.
- **The eDRAM's free room** [D, `psp_me.h` at the tock]: 256-1,456 KB holds nothing by default (the music pool at 1,184-1,456 KB only with `PSP_MUSIC=1`); AIME's data and stack take 1,456-2,048 KB. The layout plan keeps 256-800 KB for MIST's reserve, PDC and MEVGATHER (lab `findings/edram.md`), so the tier takes **800-1,456 KB (656 KB)**.

## The design

**One cache, two tiers.** The engine's sound cache stays one lruv page space of the same size (1,792 KB, `soundN`). Its first pages are main RAM (1,136 KB), its last 164 pages are the eDRAM (656 KB). A sound's block is still one run of pages, and never one that crosses the boundary (a fence in both hole searches). The placement is today's: big sounds (32 KB and up) take the highest place they fit (`soundplace`), so the tier holds mostly dialogue, music and long loops; the rest go by age as before. Same room, same policy: the sounds that play are the ones that play today, less any refusal the fence causes (counted).

**Who fills it.** A main-tier load reads straight into the cache, as now. A tier load goes through R2's staging (two 64 KB slots in main RAM, 60 KB pieces): the stick reader reads a piece, then the game thread, in `sound_cache_idle`, writes the staging back from its cache and hands what's read to R2's copy job (`ME_JOB_SOUNDCOPY` in the probe slot, at most two pieces, collected when done and never waited for); once the last piece is in, the sound is called loaded. The ME writes its own memory, so no DMA crosses the bus while it works.

The first fill was a DMACplus transfer from the main CPU (one descriptor, ~240 µs for 60 KB, bounded at 2 ms). It was exact in the emulator but hung the PSP-3000 hard at b30's opening on both tier runs of its pair [M]; the likeliest cause is a DMACplus transfer into the eDRAM while the ME is busy on the bus (75-83% there; bench15 only ran it against an idle ME) [U]. It stays behind `sndtierdma`, off, until a bench of DMACplus against a busy ME decides it.

**The copy job's bounds.** A job not collected 250 ms after it was handed over counts as a failure: its pieces are given up when it ends (the staging can't be reused before) and its sounds read again. A job that copies the wrong count, or (`sndedramcheck`) whose hashes disagree with the main CPU's hash of the staging, is a failure too. Three failures close the tier, as below. The ME hashes each piece as it reads it and again from the eDRAM once its cache is written back, so the check covers both the staging's hand-over and the eDRAM's contents.

**How the mixer reads it.** Unchanged: a packet's data is the sound's eDRAM address (the ME's cached view, `0x80000000` + offset). The audio job drops the ME's whole data cache before it mixes, so the bytes a DMA landed are read fresh, the same ordering main RAM's sounds have (written, then loaded, then mixed). `meaudioranges` (opt-in) drops only main-RAM lines, so it's refused with the tier.

**The room.** eDRAM 800-1,456 KB (`ME_EDRAM_SNDTIER`, two lines in `psp_me.h` that the layout plan may move); `sndtierN` sizes it smaller. With `PSP_MUSIC=1` the tier ends where the music pool starts (384 KB).

**What main RAM frees.** The start-up block is the main tier and the staging: 1,136 + 128 = 1,264 KB, against 1,792: **528 KB back to the heap** in step 1. Step 2 gives it to what's short (the texture caches), once heapmap says which.

**The failure path.** Every wait is bounded. A DMA that runs past its bound is stopped, its sound is dropped (its block deleted, so it's asked for and read again) and counted. Three failures, or the ME stopping (a fault, HOME, a sleep: the eDRAM is then taken as lost), close the tier for the session: the fence then locks the tier's pages, the tier's sounds not playing are deleted, the ones playing are mixed silent until they end, and sound goes on from the main tier alone (1,136 KB: fewer sounds fit, none is silent). A block the main CPU mixes while the ME is up (a late audio job, `memixcheck`, `audio44main`) mixes the tier's voices silent for that block, as R2 did; counted.

**The emulator.** PPSSPP has no eDRAM and no DMACplus. There the tier is a main-RAM block at the eDRAM's size, and the DMA is a copy at the same point in the poll. So the emulator proves the placement, the fence, the staging, the load order, the counts and play (tick for tick: statecrc, both seeds), and the heap's size; it can't prove the DMA, the eDRAM's addresses, the ME's cache ordering or any timing. Those are the PSP's: `sndedramcheck` DMAs each piece back from the eDRAM and compares it, and a PSP pair prices the frame.

**The emulator's mapped eDRAM.** The patched PPSSPP (`tools/ppsspp_local_patches.diff`) maps the tier's part of the eDRAM, 0xC8000-0x16C000 (0x800C8000 as the game addresses it, with its uncached and kernel mirrors), and says so through its `kemulator:` device (command 0x4D45: the mapped span's first and end offsets). When the span holds the tier, the emulator's tier sits at the PSP's address and the sound block takes from the heap only what a PSP's does (1,520 KB on d20, against 2,176 with the tier in the heap). Without it, the heap was 650 KB shorter than the PSP's after load, materials waited for room for their level copies, and the emulator's drawing censuses undercounted on d20 and b30 [E against M]. The keep word `sndtierram` puts the tier back in the heap; an emulator without the map, and every PSP, never ask.

**The AI job's stack, the same way.** The span runs on to the eDRAM's top, 0xC8000-0x200000, and the simulated AI job (psp_me.c's `aime_simulated`) runs on the PSP's own room, 0x178000-0x200000 (544 KB), with its slack in the job's 48 KB data reserve below it, 0x16C000-0x178000, which nothing places anything in on either machine [D]. The heap's copy was 608 KB (the room and 64 KB of slack); 64 KB below the room would reach 16 KB into the tier's end, so the slack is 48 KB in the eDRAM. A reach through the whole slack is logged once (its figure is then a floor). The keep word `aimestackram` puts the stack back in the heap; `aimesimslack` (640 KB of slack, for runs that measure the job's reach) keeps it there too, since it can't fit below the room.

**A per-map cut of the sound cache must move the tiers' fence too** (sndstream3: blocks straddled into RAM the map no longer gave the cache; fixed cfed490), and `sndedram` is on by default: plan a cut against the main tier (1,136 KB), not the whole 1,792 KB.

## Keep words

| Word | Does |
|---|---|
| `sndedram` | the tier on (off by default) |
| `sndtierN` | the tier's size in KB (64 to 656), the main tier the rest of `soundN` |
| `sndedramcheck` | each piece hashed by the copy job as read and as the eDRAM holds it, against the main CPU's hash of the staging (with `sndtierdma`: read back by DMA and compared); mismatches counted as failures |
| `sndtierdma` | the tier filled by DMACplus from the main CPU instead of the ME's copy job (off: it hung the PSP-3000) |
| `sndtierram` | the emulator only: the tier in main RAM inside the sound block, as an emulator without the mapped eDRAM has it |
| `aimestackram` | the emulator only: the simulated AI job's stack in the heap (608 KB), as an emulator without the mapped eDRAM has it |

Refused (logged, the cache as today) with `soundedram` and `edramcheck`, which use the same eDRAM, and when the ME is off.

## Steps

1. The tier, its fence, the fill (the ME's copy job; DMACplus behind `sndtierdma`), the failure path and the census line (`sound tier:` with the sound cache's line every 5 s: loads and bytes per tier, DMAs and their time, failures, muted blocks, state). Off by default. Proof: play + both seeds on vs off (b30, c20), the census showing the tier used; one PSP-3000 pair on b30's flight for the frame, with `sndedramcheck`'s counts.
2. The freed 528 KB to the texture caches (heapmap's census picks the cache), and the tier's size and placement tuned by refusals and reads (a preference for big sounds in the tier, if the census shows small ones crowding it).
3. Reopening the tier after an ME restart (flush, then reuse), so a HOME visit doesn't cost it for the session.

## Results (step 1 with the ME's copy job)

- Emulator [E]: play and both seeds identical at every tick for 300 s on b30, c20 and d20, off against `sndedram` and `sndedram sndedramcheck`; the tier took 884 sounds on b30 (822 copy jobs), none failed, none late, none hashed wrong; refusals small and mixed against the off side's own spread; the front door (menu, then a10) clean.
- PSP-3000 [M], b30's opening to 90 s, off and on twice each: no hang where the DMA fill hung both tier runs; ~200 sounds a run into the tier, no failure, the tier never closed, no refusal; the heap took 448 KB less from the system; frame gaps of +0.5, -0.2 and +0.7 ms (cinematic, flight, landing), each inside its stretch's A/A floor.
- The cost [E, M]: a tier sound is told loaded one game-thread poll after its last piece was read (the job is collected at the next poll), ~110 ms later than the DMA fill told it in the emulator; on the PSP a tier load takes ~335 ms from asked to loaded. The tier holds big sounds (dialogue, music, long loops), which wait for their load as they did.
