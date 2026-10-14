# Review: the Media Engine's cache hand-overs and audio first (`mehyg123`)

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). Same shape as `review_b121_me.md`: **what**, **key lines**, **why**, **proven**, **default**. Labels: [M] measured on a PSP (the PSP-3000 rig), [E] emulator, [D] read in code, [U] inference.

Branch `mehyg123`, from `w122` (`85ebea3`). Your rule (2026-10-14): audio comes first on the ME; everything else waits on sound.

| # | Change | Kind | Default | Off word |
|---|---|---|---|---|
| 1 | Audio mixed before each ring job and inside long ones | audio priority (exact) | on | `meaudiofirstoff` |
| 2 | One cache hand-over a tick's rays, not one a job | exact speed | on | `meraysbatchoff` |
| 3 | The audio job's cache hand-over by ranges | exact speed | on | `meaudiorangesoff` |
| 4 | The push writes back and drops the job's output itself | safety (a latent hole) | on | `mepushdropoff` |
| 5 | The ME's debug stash, dumped when a job is late | instrument | on | `mestashoff` |
| 6 | Small ray batches cast on the main CPU (`meraysminN`) | exact speed, for an A/B | **off** | on: `meraysmin4` |

The emulator runs no ME code: it proves exactness and the line rules; only the rig can say whether these are faster and whether the audio waits shrink. Every change is exact by construction, so the rig runs below ask only "how long".

---

## 1. Audio first

**What.** The ME used to serve the audio slot, then drain every ring job pushed, then look at audio again: an audio block could wait behind 8 ring jobs (a ray job is ~430 us, a slant job up to ~250 us). Now:
- `me_ring_drain()` mixes a waiting audio block before each ring job;
- `psp_me_ring_step()` (new, `psp_me.c`) is called by the rays' job between rays and by the slant job every 64 triangles; on the ME it mixes a waiting block, anywhere else it does nothing.

**Key lines.** `me_audio_now()` (the old `psp_me_serve_audio`'s body, now returning whether it mixed); `if (audio_first && me_audio_now())` in the drain; `psp_me_ring_step(k)` at the top of `psp_perception_me_rays`'s loop (`psp_fixups.py` FIXES_122 `#merays`) and in `psp_ge_me_slant`.

**Why exact.** The audio job reads and writes only the voices, its output block and its own sums. A ring job never touches those, and the audio job never touches what a ring job reads or writes [D]. Mixing in the middle changes when, not what.

**Rig figure to watch.** `audio: Media Engine mixed … waited for N us a block` and the `ME: running … audio: … waiting` line: b122 showed 1.1–3.1 ms average, worst 7.2 ms (lab 38599518536) [M]. The ME's own time a block is most of that (0.9–3.0 ms), so the wait falls by the queueing part only; change 3 is what can shorten the mix itself.

## 2. One hand-over a tick's rays

**What.** `psp_me_push_perc_rays()` wrote the main CPU's whole cache back for every job of 16 rays, and the ME dropped its whole cache for every job. Now the first job of a tick's hand-over does both; the rest carry `args[ME_RING_ARG_CONTINUES] = 1` and `ring_run()` skips their input drop.

**Why exact.** `psp_merays_hand_over()` pushes the tick's jobs back to back: nothing the rays read is written between the pushes (only the ring's own records and counters). Until every job is collected, nothing a ray reads changes: the five functions that count `psp_object_epoch` wait for the jobs first (b122's fence). So what job 1's drop made fresh on the ME is still what main RAM holds when job 2 runs [D]. An audio block mixed in between drops only audio's lines (change 3), or the whole cache, which is only slower.

## 3. Audio by ranges

**What.** Each block (~43 a second) the main CPU wrote back its whole cache, and the ME dropped its whole cache before and after the mix, so a ray job the block was mixed beside started over from main RAM. Now each side covers only the lines the mix touches (`psp_dsound.c`, `voice_lines`, `psp_dsound_me_lines`), and the whole cache past 16 KB as before (YAPSPD's rule).

**What the mix touches** [D] (`mix_voice`, `current_packet`, `decode_block`, `packet_frame`):
- each listed voice's fields above `packets[]` (`packets[]` moved to the end of `struct psp_stream`, so these are ~11 whole lines), read and written;
- the queued packets' entries, `packet_head` on, `packet_count` of them (one or two stretches of the ring), read, and the `finished` flag written;
- of each queued packet's samples, the stretch the block can reach: `reach + 2` frames from the current one, `reach = (fraction + frames × step) >> 16`, carried across packets as `current_packet` moves (`sample_lines`; an ADPCM frame counts its whole block). Read only;
- the output block (written), and its sums (the ME's own memory).

The game's side changes a voice only under the mixer's lock, which the audio thread holds through the job. On the ME the fields are dropped first and only then read to work out the entries and samples, so the ranges come from the main CPU's values, not a stale copy. Each side may fall back to the whole cache on its own: the main CPU's ranges cover every line the ME can write back, and after the job it drops just those, so none of its own writes elsewhere in a voice is lost.

**Log.** `audio: … N handed over by ranges` (the main CPU's side) and `ME: since start: … M of all its audio blocks handed over by ranges` (the ME's).

## 4. The push drops the output

**What.** `psp_me_wait()` drops the output with cache op 0x1B (write back and invalidate). If a caller had left a dirty line in the output, that write-back would land on top of the ME's answer. `psp_me_push()` now writes back and drops the output lines itself before the request goes out; past 16 KB, the whole cache. No caller does this today [D]; it closes the hole for the next one.

## 5. The debug stash

**What.** 128 bytes (two lines, uncached, written only by the ME): where it is (between jobs, a slot job's cache work / code / write-back, the same for a ring job, halted, stopped), the ring job's ticket and type, the step inside it (the ray or triangle), its cycle counter at the note, and counts (ring and slot jobs begun and ended, audio blocks mixed before or inside ring jobs, audio blocks by ranges). `me_stash_dump()` logs it when the main CPU gives up on a ring job (`ring_fail`, too late), a slot job or a handed-over job; each heartbeat logs the counts.

**Cost.** One or two uncached stores a job and one a ray [D]; `mestashoff` removes them.

## 6. The b122 cutscene creep, and `meraysmin`

**Diagnosis** (rig ladders b121 38551389789 against b122 38599518536, per opening):

| Opening | tick b121 → b122 (ms) | ME ray jobs, rays a job (b122; from the counts logged every 1800 passes, so the boundaries are approximate) |
|---|---|---|
| a10 | 6.26 → 6.62 (+0.36) | 0 |
| a30 | 10.85 → 11.41 (+0.56) | 0 |
| a50 | 25.56 → 24.90 (−0.66) | 843, 8.6 |
| b30 | 56.82 → 57.45 (+0.63) | 5,071, 11.0 |
| b40 | 13.78 → 14.00 (+0.22) | 193, 1.4 |
| c10 | 26.76 → 27.45 (+0.69) | 1,225, 1.5 |
| c20 | 25.17 → 25.87 (+0.70) | 602, 1.4 |
| c40 | 15.11 → 15.60 (+0.49) | 2,111, 1.2 |

Per opening, b121 → b122, the mixer's lock waits a heartbeat (a10 5.4 → 5.9, a30 4.2 → 4.2, c10 6.5 → 5.9, c20 5.0 → 4.0, c40 9.7 → 8.8), the ring's wait a job (37 → 40, 19 → 22, 35 → 32, 18 → 20, 30 → 20 us), the audio wait a block and the ME's busy share are all flat [M]. What moved is `objects_update`: +0.7 (a10), +1.0 (a30, c10), +1.4 (c20), +1.8 (c40) ms a frame, while `ai_update` fell (c10 −1.7) [M].

**The c40 A/B on one build** (lab 38611163488, 2 runs a side): in the opening cinematic, rays on / `meraysoff` / everything of b122's off gave ticks of 19.5 / 19.0 / 19.4 ms, inside the repeats' spread; in play, turning the rays off costs +0.4 ms a tick in the bunker [M]. So **the creep is not the ME's rays or posing**:
- a10 and a30 handed over no rays at all and crept anyway;
- with both builds' code the same in the cinematic, the gap went away.

What's left is the difference between the two builds as built: different profiles (`campaign-b121` against `campaign-b122`) and so a different code layout, or another b122 change that isn't behind the A/B's words (the cut-piece counter, `hudsnap` on by default, the look words' tests). `objects_update` taking it all points at layout over any one change [U]. The test that would name it: the b121 and b122 commits built with one profile, A/B'd on a10.

**What.** No cinematic gate. `meraysminN` (off by default; `meraysmin0` is the default) casts a tick's rays on the main CPU when fewer than N would go to the ME. Many ticks hand over only 1–2 rays (c10–c40: 1.2–1.5 a job), each costing a whole-cache hand-over. It's here for a rig A/B only.

**Why exact.** The same function casts the same rays either way (`psp_perc_cast`); only which CPU [D].

---

## Proven (emulator)

Lab batch `mehyg123a` (run ID in the final report): the state CRC pairs hands off on d20, a50, c20 against `w122`; `perccheck`, `meslantcheck`, `meringtest` with 0 mismatches; a50 and b30 in the strict interpreter with `gemesim mewrites` showing no line written by both sides; the multiplayer build compiled and started.

## For the rig

The runs to ask for are in the final report: an audio-wait and tick A/B with `keepsets` on b30 and c40 (`- | meaudiofirstoff meraysbatchoff meaudiorangesoff mepushdropoff mestashoff`), and c10 with `meraysmin4` against the default.
