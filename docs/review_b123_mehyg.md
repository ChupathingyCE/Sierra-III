# Review: the Media Engine's cache hand-overs and audio first (`mehyg123`)

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). Same shape as `review_b121_me.md`: **what**, **key lines**, **why**, **proven**, **default**. Labels: [M] measured on a PSP (the PSP-3000 rig), [E] emulator, [D] read in code, [U] inference.

Branch `mehyg123`, from `w122` (`85ebea3`). Your rule (2026-10-14): audio comes first on the ME; everything else waits on sound.

| # | Change | Kind | Default | On word |
|---|---|---|---|---|
| 1 | Audio mixed before each ring job and inside long ones | audio priority (exact) | **off** | `meaudiofirst` |
| 2 | One cache hand-over a tick's rays, not one a job | exact speed | **off** | `meraysbatch` |
| 3 | The audio job's cache hand-over by ranges | exact speed | **off** | `meaudioranges` |
| 4 | The push writes back and drops the job's output itself | safety (a latent hole) | **off** | `mepushdrop` |
| 5 | The ME's debug stash, dumped when a job is late | instrument | **off** | `mestash` |
| 6 | Small ray batches cast on the main CPU | exact speed | **off** | `meraysminN` (e.g. `meraysmin4`) |

**All six are opt-in for b123**: nothing on this branch has shown a gain on the rig yet, and c40's cinematic ran slower with 1–5 on for a reason the logs don't name (below). With no word given, b123 runs w122's ME code paths, so the emulator's exactness proof against w122 covers the defaults.

The emulator runs no ME code: it proves exactness and the line rules; only the rig can say whether these are faster and whether the audio waits shrink. Every change is exact by construction, so the rig runs ask only "how long".

**The rig's first answer** (all five words on against all off, 2 runs a side, PSP-3000; read in "What the rig showed" below): c40's opening cinematic was slower with them on (tick 20.4 against 17.7 ms, frame by the clock 82.1 against 76.5) [M]. All six are now off by default until an A/B names the cause.

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

**Checked on the host** (the lab's `psp/sierra/hosttests/audioranges`): the mixer's own code compiled for the PC, 20,000 random voices (ADPCM and PCM, mono and stereo, 1–6 packets, finished ones, pitches from a third to four times), 56,590 blocks: with every sample byte outside the named lines scrambled, the output and the voice's new state came out the same, and no voice byte changed outside the lines named for after the job. Its negative controls catch both kinds of miss (the reach two frames short: 446 blocks read outside; the fields one line short: 17,107 blocks wrote outside).

**Log.** `audio: … N handed over by ranges` (the main CPU's side) and `ME: since start: … M of all its audio blocks handed over by ranges` (the ME's, with `mestash`).

On the rig the ME's mix took the same time either way (c40's cinematic 872 against 874 us a block, b30's 2,072–2,084 against 2,069) [M], so it gave nothing measurable, and it is one of the three changes that were on in c40's cinematic when it ran slower.

## 4. The push drops the output

**What.** `psp_me_wait()` drops the output with cache op 0x1B (write back and invalidate). If a caller had left a dirty line in the output, that write-back would land on top of the ME's answer. `psp_me_push()` now writes back and drops the output lines itself before the request goes out; past 16 KB, the whole cache. No caller does this today [D]; it closes the hole for the next one.

## 5. The debug stash

**What.** 128 bytes (two lines, uncached, written only by the ME): where it is (between jobs, a slot job's cache work / code / write-back, the same for a ring job, halted, stopped), the ring job's ticket and type, the step inside it (the ray or triangle), its cycle counter at the note, and counts (ring and slot jobs begun and ended, audio blocks mixed before or inside ring jobs, audio blocks by ranges). `me_stash_dump()` logs it when the main CPU gives up on a ring job (`ring_fail`, too late), a slot job or a handed-over job; each heartbeat logs the counts.

**Cost.** One or two uncached stores a job and one a ray, and a line in each heartbeat [D]. With it off, the dump on a late job is skipped.

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

Lab batch `mehyg123a`, run 38617640170 (`7571547`, the code as it is here) [E]:
- **Exact.** The state CRC pairs hands off against `w122` (`85ebea3`), both with the ME's rays and slant on: play and both random seeds the same at every tick on d20 (22,552 ticks), a50 (38,554) and c20 (29,045). The allocations that differ (decal vertex cache, particles, actor/prop/objects bookkeeping) are the ones that differed between b122's own accepted pair (lab 38590465463, a50: the same verdict).
- **Every answer checked.** `perccheck`: 24,760 rays checked on a50, 112,845 on b30's b113 route (with `meraysmin4`, 385 ticks cast on the main CPU), 0 mismatched, 0 failed jobs. `meslantcheck`: 414,000 and 279,900 triangles, 0 differing. `meringtest`: 952 jobs, 0 wrong.
- **The line rules.** a50 and b30 in the strict interpreter with `gemesim mewrites`: no system call in the ME's work. The lines written by both sides are 23 on a50, the same 23 main-thread stack lines (0x09fb….) as b122's own a50 trace (lab 38560105583), where the emulator runs the ME's work on the main thread's stack (on the PSP the ME's stack is its own eDRAM); b30's 7 are among those same stack lines. No line of the program's data or heap.
- **Audio by ranges.** On the (simulated) ME: 433 of 433 blocks on a50's opening, 117 of 433 on b30's flight, 0 of 433 on b30's beach (more voices and samples than 16 KB: the whole cache, as before). So change 3 helps the lighter scenes only.
- **Multiplayer.** The build compiles. It stops at start in the emulator on a full heap (the 768 KB display list), exactly as `w122`'s own multiplayer build does (lab 38595336888, the same two lines): not this branch's.

## What the rig showed

Lab 38631087543 (c40) and 38631133512 (b30's b113 route), `7d2e988`, all five words on (`-`) against all five off, 2 runs a side [M]:

| Stretch | on | off | |
|---|---|---|---|
| c40 cinematic, tick (ms) | 20.4 | 17.7 | real |
| c40 cinematic, frame by the clock (ms) | 82.1 | 76.5 | real |
| c40 flight / landing, frame by the clock | | | off 1.4% / 0.7% faster |
| c40 bunker, beach | | | the same |
| b30 cinematic, drawing (ms) | 53.7 | 52.1 | real |
| b30 cinematic and flight, tick | | | the same |

What the logs separate, per change:
- **1, audio first.** In c40's cinematic it never fired: `ME: since start: 0 audio blocks mixed before or inside a ring job` [M]. So it is not c40's cost. On b30 it fired ~500 times a run, and the worst audio wait a block fell from 3.9–5.0 ms to 2.7–3.5 ms (cinematic and flight; the average stays 2.3 ms) [M]. The price is paid by the ring jobs it mixes inside: the ME's time a ring job rose 950 → 1,020 us (cinematic) and 670 → 725 us (flight), and the main CPU's wait a ring job by 30–80 us. b30's tick didn't move. It does what you asked of it on b30; it waits for the A/B below like the rest.
- **2, one hand-over a tick's rays.** In c40's cinematic every job carried one ray (1,158 jobs, 1,158 rays in the first 1,800 passes, the same both sides), so no job ever continued a hand-over. It is not c40's cost.
- **3, audio by ranges.** It ran on every block (c40: ~110 a heartbeat, all of them), with no shorter mix on the ME.
- **4, the push drops its output.** Two lines a ray job; a slant job's output, up to 16 KB, on the renderer. A safety fix of a few cache operations; c40's extra cost is in its ticks, not its drawing.
- **5, the stash.** A few uncached stores on the ME a job and a heartbeat line.

What doesn't explain c40's cinematic: these were the same on both sides there:
- the ring's wait a job (22 against 24 us);
- the ME's time a ring job (110 against 108 us);
- the audio wait a block (1.0 ms against 1.0) and the ME's own mix (872 against 874 us);
- the mixer lock's waits (0.8 ms a heartbeat against 0.7);
- the ME's busy share (4.3% against 4.4);
- the ray counts and the play (perception's lines identical, game time at each heartbeat within a few ticks).

The extra time shows as occasional long ticks: the worst tick of a heartbeat was 93–160 ms with the words on, 48–78 ms off. None of the waits above accounts for them [U]. The profile windows (from 100 s in) show the two sides doing the same work at the same rate afterwards.

**The A/B that names it** (on this head, everything off by default): c40, `wall=300`, `repeat=2`,
`keepsets = "- | meaudiofirst meraysbatch | meaudioranges mestash | mepushdrop"`.
The pair that brings back the long ticks holds the cause; a single-word A/B inside that pair then names it.

## For the rig

Next, in order (the PSP-3000; read with `tools/abreport.py`):
1. c40, `wall=300`, `repeat=2`, `keepsets = "- | meaudiofirst meraysbatch | meaudioranges mestash | mepushdrop"` (above).
2. c10, `wall=300`, `repeat=2`, `keepsets = "- | meraysmin4"`: whether small ray batches are better on the main CPU.
