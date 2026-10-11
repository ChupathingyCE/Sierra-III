# Build 115: the instruments that size the VRAM swap and the streaming (2026-10-11)

Built the evening after b114's nine PSP tests and a day of recon with Sheepdog01 (`big_levers.md` §0, §4, §8, §9; `streaming.md`). Its job is measurement: the three numbers the VRAM split needs, who waits for the Memory Stick and behind whom, and whether the ME's eDRAM is really free. It also carries the ocean's A/B. The two engine fixes planned for it are out: one never ran, and the other wasn't exact in the emulator (below). Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator (PPSSPP with the local patches, PSP-1000 memory), **[D]** read in the code, **[U]** inference.

Zip `halo_psp_b115.zip` (`5f4d44164b67fff1`, the same bytes from any machine), EBOOT `d22eb2eddda5139a` (the lab built the same), commit `88f09ee565`. Heap 5,888 KB, as in b114, but **the program now sits 0.7 KB under the next 64 KB step** (b114: about 14 KB): b116 has to trim code before it adds any, or the heap drops to 5,824 KB.

## What changed

### On by default

Nothing that changes play. The ME's job slots go from two to three, so the eDRAM probe below never shares a slot with audio or rendering.

### Off by default: instruments

| Keep word | What it reports | Where it means something |
|---|---|---|
| `iostats` | Every stick report (about 30 s): for each stream (textures, geometry, animations, the map file, log writes, other), the calls, KB read and written, seconds in the stick (worst call), seconds waiting for the lock (times, worst) and **which stream held the lock meanwhile**. For the map file (sound, mostly), how many reads fetched a block read before | PSP (the emulator's stick has no timing) |
| `gereads` | Every 300 frames: what the draws made the GE read, KB a frame, by **where it lives** (VRAM, this frame's data in the display list, the level copies, the geometry cache, the rest) and by kind of draw; the 8 buffers that give most back per KB of VRAM; and the **fit curve**, the share of the GE's reads from main RAM that VRAM would take holding the best buffers, 200 to 1,000 KB of them | Both: the bytes are the same in the emulator |
| `gebusy` (b113's word, now **by kind**) | The GE's own time a frame, now split by kind of draw (level, models, see-through, decals, sprites, shadows, lights, HUD, …): a change of kind ends the chunk, so each chunk is one kind's. A line `gebusy by kind` beside the `frameend:` line | PSP only (the emulator doesn't time the GE) |
| `vramtexN` | The VRAM texture cache cut to N KB; textures past it come from the RAM cache. The swap's first step, and the A/B for whether texels from main RAM cost the GE anything on b30. The caches hold less in all, so more textures stream: `iostats` shows that apart | PSP |
| `edramcheck` | The ME fills its eDRAM from 256 KB to 1.5 MB with a pattern at the first heartbeat, then checks 256 KB of it each heartbeat. Any changed word is logged; every 20 checks, a tally | PSP, with the ME running (not with `nomeaudio`) |

### Off by default: Sheepdog01's A/B

| Keep word | What it does |
|---|---|
| `overlayfreefar` | The ocean (and every free see-through surface) pushed back by its far end instead of its near one, so it stops showing through the seabed in the flight (`hardware_b114.md`, "The ocean") |
| `vfpuskin` (b106's, untested on the PSP) | Bending corners near the eye on the VFPU (E24). The cinematic, with the marines beside the camera, is its best case |

## Not in b115, and why

| Item | Why not |
|---|---|
| The water's tint (the ocean white from afar) | It needs a colour carried through the see-through queue to the flush, and it changes the look: its own build and A/B (b116) |
| `vrammodels` (the shared models in VRAM) | Sized by `gereads`' fit curve, timed by `gebusy` by kind: after this build's results |
| L1 step 4, the overlap | Needs a day with the PSP at hand |
| S1, one input stage for the stick | Design first; `iostats` says how much it buys |
| The script nodes sized to the map | Already done for the tag window in b106 (`invader-build -Y`). The start-up's stand-in table in the heap is let go of before the first map [E]: b115's planned fix for it never ran, so it was dropped (e510ddf) |
| The animations' fractions in single precision (E16) | Not exact in the emulator: see "What the emulator showed". Held back; the text pairs are in 2de694d |

## What the emulator showed [E]

**The release commit's checks** (b115-final, run 38183181528): the bench preset (every instrument on) over the opening bench's route, past its end to game time 20,203, no fault; the strict interpreter with the instruments on, to game time 1,328, with one finding: loads from 0x083ffefc in the GU library's signal handler (`callbackSig`, `sceGuInit.c`). That address is PPSSPP's own interrupt stack, where it runs `gebusy`'s SIGNAL callbacks, so this is the emulator's artifact, not ours. Every preset ran clean over the same route in b115-first (run 38180937131).

### What the GE reads (`gereads`)

The opening bench, KB a frame from main RAM [E]. The bytes don't depend on the platform, so they transfer to the PSP; they assume a fetch per index, so they're an upper bound [U].

| Stretch | From main RAM | Models' share | VRAM holding the best 200 KB | 400 KB | 600 KB | 800 KB |
|---|---:|---:|---:|---:|---:|---:|
| flight | 3,821–7,011 | 83–88% | 64–72% | 78–83% | 85–92% | 90–97% |
| landing | 6,807 | 87% | 76% | 88% | 96% | 100% |
| beach | 928–5,956 | 74–88% | 66–92% | 86–99% | 92–100% | 96–100% |
| bunker | 503–3,572 | 69–84% | 48–93% | 69–99% | 82–100% | 89–100% |

- **The models are the GE's main RAM traffic:** 69–88% of it, from the geometry cache. The level's copies are 0.1–0.6 MB a frame; this frame's data in the list 30–200 KB, which VRAM can't hold.
- **A few small buffers carry most of it.** In the flight, an 11 KB model buffer is read 586 KB a frame and an 18 KB one 613 KB: each about 50 times.
- **So VRAM needs ~600 KB for models, not the whole 1.2 MB.** 600 KB takes 82% of the reads in the worst window and 92% or more in most. 400 KB takes 69–99%.
- **The level ranks last per KB:** each of its vertices is read about once a frame. With textures at ~400 KB and models at ~600, the level gets what's left (~200 KB) [U].
- The fit is over the 93–95% of those bytes the buffer table follows. b115-first's table overflowed (139d81e fixed it); these figures are the release commit's.
- Caveat: the emulator's route meets fewer enemies than Sheepdog01's play, and c40 will differ (the lab's next run).

### The stick (`iostats`)

The emulator's stick has no timing, so only the counts mean something here [E]:
- **Most of what's read from the map file was read before.** To the bunker, 81 MB came from the map file (sound, mostly) and 83% of it re-read blocks read earlier. Standing still after the route, it still reads ~3.5 MB every 27 s, all of it re-reads: looping sounds streamed again and again. The emulator's audio follows its own clock, so the PSP's figure decides (test 1); b114's sound reader spent 19.5 s in the stick [M].
- **"Other" is the checkpoints:** 1.3 MB at the first (330 of 853 pages of game state), up to 3.2 MB later.
- **`vramtex400` streams 20% more textures** on the cinematic: 364 loaded against 316 (4,599 KB against 3,848), 210 evicted against 111. On the PSP that's ~0.15 s more stick time over the cinematic [U]. Whether the GE slows with texels in main RAM is test 3's question.

### The animations' fraction change: not exact

The check (b115-exact, run 38182244656): `fixedticks fixedseed statecrc`, no route, 300 s.

| Pair | Result |
|---|---|
| b115 against b115 | the same to the end (tick 10,770) |
| b115 without the change against b114 | the same to tick 1,110; parted at 1,140 (objects, a prop), where builds part anyway (#25) |
| b115 with the change against without | the decal vertex cache differed from tick 0; actors and objects from tick 90 |

- The values agree for every ordinary float [D], so something else differs: a NaN, a denormal, or `fast_ftol` given a NaN are the candidates [U].
- The gain was small [U], so it's held back until the cause is found.

## On the PSP

Six presets. Pick the map (b30) on the first page; then R twice to the presets page, cross on the test, START. Deathless, as before.

**Before the first test: install**
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b115.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b115 is on the stick"
ls $psp/halo/presets.txt        # the presets came with it
rm -f $psp/halo/shots/*
umount $psp
```
The log's first line should say `bring-up build 115`.

**After each test: pull the log.** Change the name for each test:
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b115_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
rm -f $psp/halo/shots/*
umount $psp
```

Run them in this order: the three short cinematic runs back to back, then the ocean, then the long bench.

| # | Preset | Play | Stop | Time | What it answers |
|---|---|---|---|---|---|
| 2 | cinematic, base | Start, and leave the controls alone through the opening cinematic | When the cinematic ends and you have control in the Pelican | ~3 min with loading | The GE's time by kind on the fair stretch: the baseline for 3 and 4 |
| 3 | cinematic, textures from RAM | as 2 | as 2 | ~3 min | Against 2: does the GE take longer with most textures in main RAM? (`vramtex400`) |
| 4 | cinematic, VFPU skinning | as 2 | as 2 | ~3 min | Against 2: is the drawing faster with `vfpuskin`? |
| 5 | flight, the ocean pushed back by its far end | Through the flight; look at the ocean below, where b114 showed it through the seabed | When the Pelican lands | ~5 min | By eye: is the ocean right now? (`overlayfreefar`) |
| 1 | bench, b115 instruments | The opening bench: from the start into the first bunker | In the bunker | ~13 min | The GE's time by kind and what it reads, per stretch; who waits for the stick and behind whom; the eDRAM probe |
| 6 (optional) | play, no instruments | Play as you like | Whenever | any | How it feels with no measuring at all |

`tools/benchreport.py` compares 2, 3 and 4 over the cinematic (its first stretch).
