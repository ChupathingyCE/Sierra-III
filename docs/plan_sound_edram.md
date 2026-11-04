# The sound cache's second tier in the ME's eDRAM (SNDEDRAM2, `sndedram`)

The goal (Sheepdog01, 2026-11-03): keep the sound's fidelity and move memory out of main RAM without a speed penalty. The sound cache is the heap's biggest owner (1,792 KB, one start-up block) and b30's heap is ~130 KB from empty at the bunker approach [M]. R2 (`soundedram`) moved the whole cache into the eDRAM and was rejected: its slow staged loads cost d20 +11-14% a frame, most of it the yields SNDNOYIELD has since removed [M, E]; it also took the eDRAM AIME now uses. This is the smaller step: part of the cache in the eDRAM, filled by DMA.

## What's known

- **DMACplus writes the ME's eDRAM exactly from the main CPU's side** [M, PSP-1000, `bench/halo_bench15`]: 254-256 MB/s at 16-byte transfers, one descriptor of up to 4,095 counts (65,520 bytes). The ME's side address is the eDRAM's offset (its eDRAM at 0). A chained descriptor list hung the PSP, so transfers stay single and bounded. At 99% duty the main CPU's misses got 44% slower; a sound load's duty is under 1%.
- **The main CPU can't read the eDRAM; the ME can.** The mixer runs on the ME and is the only reader of a sound's bytes [D, `psp_dsound.c`; R2's audit, `me_programme.md` section 5]. The engine checks a queued sound's address against the cache's range (`sound_dsound_xbox.c`), and that check is all it does with it.
- **The eDRAM's free room** [D, `psp_me.h` at the tock]: 256-1,456 KB holds nothing by default (the music pool at 1,184-1,456 KB only with `PSP_MUSIC=1`); AIME's data and stack take 1,456-2,048 KB. The layout plan keeps 256-800 KB for MIST's reserve, PDC and MEVGATHER (lab `findings/edram.md`), so the tier takes **800-1,456 KB (656 KB)**.

## The design

**One cache, two tiers.** The engine's sound cache stays one lruv page space of the same size (1,792 KB, `soundN`). Its first pages are main RAM (1,136 KB), its last 164 pages are the eDRAM (656 KB). A sound's block is still one run of pages, and never one that crosses the boundary (a fence in both hole searches). The placement is today's: big sounds (32 KB and up) take the highest place they fit (`soundplace`), so the tier holds mostly dialogue, music and long loops; the rest go by age as before. Same room, same policy: the sounds that play are the ones that play today, less any refusal the fence causes (counted).

**Who fills it.** A main-tier load reads straight into the cache, as now. A tier load goes through R2's staging (two 64 KB slots in main RAM, 60 KB pieces): the stick reader reads a piece, then the game thread, in `sound_cache_idle`, writes the staging back from its cache and DMAs it into place (one descriptor, ~240 µs for 60 KB), waits for it within a bound (2 ms), and once the last piece is in, calls the sound loaded. No ME job is involved, so the ME's time and its ring are untouched.

**How the mixer reads it.** Unchanged: a packet's data is the sound's eDRAM address (the ME's cached view, `0x80000000` + offset). The audio job drops the ME's whole data cache before it mixes, so the bytes a DMA landed are read fresh, the same ordering main RAM's sounds have (written, then loaded, then mixed). `meaudioranges` (opt-in) drops only main-RAM lines, so it's refused with the tier.

**The room.** eDRAM 800-1,456 KB (`ME_EDRAM_SNDTIER`, two lines in `psp_me.h` that the layout plan may move); `sndtierN` sizes it smaller. With `PSP_MUSIC=1` the tier ends where the music pool starts (384 KB).

**What main RAM frees.** The start-up block is the main tier and the staging: 1,136 + 128 = 1,264 KB, against 1,792: **528 KB back to the heap** in step 1. Step 2 gives it to what's short (the texture caches), once heapmap says which.

**The failure path.** Every wait is bounded. A DMA that runs past its bound is stopped, its sound is dropped (its block deleted, so it's asked for and read again) and counted. Three failures, or the ME stopping (a fault, HOME, a sleep: the eDRAM is then taken as lost), close the tier for the session: the fence then locks the tier's pages, the tier's sounds not playing are deleted, the ones playing are mixed silent until they end, and sound goes on from the main tier alone (1,136 KB: fewer sounds fit, none is silent). A block the main CPU mixes while the ME is up (a late audio job, `memixcheck`, `audio44main`) mixes the tier's voices silent for that block, as R2 did; counted.

**The emulator.** PPSSPP has no eDRAM and no DMACplus. There the tier is a main-RAM block at the eDRAM's size, and the DMA is a copy at the same point in the poll. So the emulator proves the placement, the fence, the staging, the load order, the counts and play (tick for tick: statecrc, both seeds), and the heap's size; it can't prove the DMA, the eDRAM's addresses, the ME's cache ordering or any timing. Those are the PSP's: `sndedramcheck` DMAs each piece back from the eDRAM and compares it, and a PSP pair prices the frame.

## Keep words

| Word | Does |
|---|---|
| `sndedram` | the tier on (off by default) |
| `sndtierN` | the tier's size in KB (64 to 656), the main tier the rest of `soundN` |
| `sndedramcheck` | each piece read back from the eDRAM by DMA and compared (PSP), or the copy compared (emulator); mismatches counted |

Refused (logged, the cache as today) with `soundedram` and `edramcheck`, which use the same eDRAM, and when the ME is off.

## Steps

1. The tier, its fence, the DMA fill, the failure path and the census line (`sound tier:` with the sound cache's line every 5 s: loads and bytes per tier, DMAs and their time, failures, muted blocks, state). Off by default. Proof: play + both seeds on vs off (b30, c20), the census showing the tier used; one PSP-3000 pair on b30's flight for the frame, with `sndedramcheck`'s counts.
2. The freed 528 KB to the texture caches (heapmap's census picks the cache), and the tier's size and placement tuned by refusals and reads (a preference for big sounds in the tier, if the census shows small ones crowding it).
3. Reopening the tier after an ME restart (flush, then reuse), so a HOME visit doesn't cost it for the session.
