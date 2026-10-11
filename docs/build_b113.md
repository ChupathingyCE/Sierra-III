# Build 113: the flip fix, the tracers, room for more objects (2026-10-10)

Built on b112 (commit `e2022aff12`, zip `halo_psp_b113.zip`, EBOOT `f0114760…`) after its six PSP runs (`hardware_b112.md`) and Sheepdog01's go to clean up and move on. Sheepdog01 was away for most of the build; he chooses which of the visible changes stay, one at a time, after an A/B. Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator (PPSSPP with the local patches, PSP-1000 memory), **[D]** read in the code, **[U]** inference.

## What changed

Every change has a keep word, typed into `keep.txt` or toggled in the launch menu (b113 lists its own words there).

### On by default (exact, or the Xbox's own behaviour)

| Change | b113_candidates | Keep word for b112's behaviour | Why |
|---|---|---|---|
| **The flip fix:** the sleep's checksum is four-lane FNV-1a | #10 | `weaksum` | b112's checksum cancelled the flip's two writes, so a flipped vehicle stayed asleep and never righted itself (below) |
| **The render-state cache holds 512 objects** | #21 | `renderstates256` | On b30's cliffs it was full for minutes, so most objects worked their lighting out whole every frame |
| **Render jobs get 500 ms; audio is mixed between replay records** (`gemeon` only) | step 4b | none | b112's Test 3: the 40 ms audio limit stopped the ME, and audio waited up to 38 ms behind render jobs |
| **The level copies' wanted list holds 128** (`gemeon` only) | step 4b | none | 14 of 113 didn't fit in b112's Test 3 |

### Off by default (Sheepdog01's A/B)

| Keep word | b113_candidates | What it does | Cost |
|---|---|---|---|
| `contrails` | #1 | The Covenant weapons' tracers: the plasma bolts' and the needler's trails | Up to about 16 KB of display list a frame in heavy fire [E]; the game already built the strips and threw them away |
| `zoommask` | #20 | A zoomed weapon's mask (the pistol's) as a soft edge | One full-screen quad a frame while zoomed |
| `zoommaskfull` | #20 | The same mask at full strength: a clear circle in a black surround | as above |
| `list768` | #19 | The display list 768 KB instead of 512, so fewer objects are pushed out | 256 KB of heap |
| `gebusy` | instruments | The GE's busy time in the `frameend:` line | One interrupt per 16 draws |
| `soundplace` | #24 (Sheepdog01, 17:55: "might as well") | The sound cache places big sounds (32 KB and up) at the top, so playing ones stop splitting the space the small ones need | None: no memory; big sounds may be read again more often |

### Instruments and test tools

- **`objects drawn:`** line, with the objects report, always on: views drawn; the rendered-object list's peak against its 256 and the views where it was full (objects past it aren't drawn); render states worked out whole a view, and how many first pushed out the oldest.
- **The `hardware:` line** now prints the profiler's 19th counter, `local bus` (read but never printed before; PSPTEK reads it as the GE's bus cycles [U]).
- **`fixedseed`** (tests, with `fixedticks statecrc`): the local random seed starts at a constant, and sounds draw from their own (`big_levers.md` §5). The per-tick state line carries both seeds.
- **`shoteveryN`** (tests): a screenshot every N seconds of game time, at most 24.
- **`flipwatch`**, and `flipfixture` in `PSP_DEBUG_TOOLS` builds: the flip prompt's conditions logged as they change, and a Warthog put upside down beside the player.

## The flip fix

**What Sheepdog01 saw** (several builds): the flip prompt shows briefly once, then never again, from any side.

**Why** [E + D]:
- The port puts parked vehicles to sleep, and wakes one when a checksum of its bytes changes.
- b112's checksum turns the running sum 5 bits left, then XORs in each word.
- The flip writes two neighbouring words: the flags word gains 0x10, and the next word, the upending type, becomes 2 (0x200). Turned 5 bits, 0x10 is 0x200, so the two changes cancel and the checksum doesn't move.
- The vehicle stays asleep, so its own update never runs, and that update is what rights it and clears the flag that hides the prompt.
- From the other side the type is 1 (0x100); that doesn't cancel, so the flip worked from that side only [D, by the arithmetic].

**The fix:** FNV-1a. Each word is XORed in, then the sum is multiplied by an odd number. A single change always shows, and for a second change to undo a first it would have to equal what the multiply made of it. Four lanes (word *i* in lane *i* mod 4) let the multiplies overlap. The reference sum (`checksumcheck`) follows the same rule: 700,000 sums compared in the emulator, none different [E].

## Room for more objects

**The render-state cache** (`psp_capacity.h`):
- Each object drawn keeps its lighting in this cache. When the cache is full, every miss scans all of it for the oldest entry and works the lighting out whole.
- On b112 run 5's cliffs, 350 scenery objects updated a tick, and the cache sat at 256 of 256 from game time 17,000 to 20,500 [M].
- 512 entries cost 64 KB of game state, so the game state's base moved 64 KB lower (0x092A0000), and a compile-time check makes sure it still ends below the tag window. The heap gives the 64 KB: **5,952 → 5,888 KB**.

**The heap's budget, if every new word is on** [U, by adding to b112 run 5's peak of 5,154 KB]:

| | KB |
|---|---|
| b112 run 5's peak (the whole level) | 5,154 |
| `list768` | +256 |
| `sound1792` (Sheepdog01's call) | +256 |
| Peak | about 5,670 of 5,888 |

## What the emulator showed [E]

- **Tracers** (`contrails`, `sierra-test` 38088893670, b30 on run 1's route): drawn: the needler's pink trails and the plasma's. Up to 325 segments a frame in heavy fire. As triangle lists that was 47 KB of display list a frame, so they're now strips: 16 KB.
- **The zoom mask** (38088924310, Blood Gulch, the pistol zoomed): the mask is in the texture pack and drew every zoomed frame. At full strength it leaves a clear circle about half the screen's height inside black.
  - By the Xbox's pixel shaders [D], when the zoom also blurs (the pistol's does), the mask decides where the blur goes: sharp in the middle, blurred at the edges. It darkens the frame only when there's no blur.
  - The PSP has no cheap blur, so `zoommask` darkens by a third of the mask as a soft edge in the blur's place, and `zoommaskfull` keeps the full strength for comparison.
- **The strict interpreter** (38088958245, every new word on): one site, a stack read in pspgu's `callbackSig` (the GE's signal interrupt, which `gebusy` uses) at 0x083ffefc. That's PPSSPP's interrupt stack, which sits below user RAM in the emulator. pspgu's own `sceGuSync` relies on the same interrupt path on hardware [D]; the site is the emulator's.
- **Exactness, b113's defaults against `weaksum renderstates256`** (`fixedseed` pairs, 38089896228 and 38089927978): play is identical up to tick 1,356 and parts at 1,357.
  - Builds or keep words that change the emulator's speed part near tick 1,326 even with `fixedseed`: b112 against b113 set back to b112's behaviour also parts at 1,326. So the check is good for b30's first 44 s across builds, and for a whole run within one build and one set of words.
  - The first object to part across builds is a plasma pistol bolt (runs 38092178378 and 38092205732); the suspect is drawing feeding back into play through streaming [U] (`big_levers.md` §5).
- **The flip** (Blood Gulch, `flipfixture`, the same press from the same side, approach type 2):
  - b113 (38090374354): the flag is set, the torque starts the same tick, the Warthog rights itself in 26 ticks (up.k −0.96 → 0.91), and the flag clears, exactly as with every vehicle kept awake (b112's `vehiclesawake` run).
  - `weaksum` (38090411833): b112's bug, unchanged: the checksum stays `e0226073` and the vehicle sleeps through it.
- **`gedefer` over the whole run** (38089959962 against 38089896228): play is identical up to tick 1,350. The whole state first differs at tick 532, in "object looping sounds" (a sound-system record that follows the emulator's speed, #25), which is not play.

- **The finished build, every new word on** (38092985617, b30 on run 1's route, `contrails list768 gebusy hwprofile`): played to game time 18,594 with no fault. The list came from the heap at 768 KB. The tracers drew as strips (2,610 segments in 300 frames at the busiest). The rendered-object list peaked at 227 of 256 and was never full on this route. `gebusy`'s line works; the emulator's GE times mean nothing.
- **The zoom's soft edge** (38093016435): the middle clear, the edges darkened by a third.
- **`soundplace`, one run each on run 1's route** (38093942190 without, 38093971174 with; 1,536 KB) [E]:

  | Sounds dropped by game time | without | `soundplace` |
  |---|---|---|
  | 2,600 (the flight) | 51 | 81 |
  | 6,000 (the beach) | 251 | 158 |
  | about 19,000 | 408 | 373 |
  | Loads from the stick | 4,706 | 5,007 (+6%) |

  - Mixed: worse in the flight, better on the beach. The emulator also drops three times what the PSP did (251 against 85 by the beach), because its audio runs on a different clock. So only Test 2 says whether it helps.
  - To tell size from fragmentation, the first 24 drops are now logged: the sound's size, the longest run the cache could clear for it, and what playing and loading sounds held (`sound cache: no room for …`). **First answer** (38095073612, b30's opening) [E]: at the flight's start (game time 465) sounds playing or loading held 1,360 of the 1,536 KB, and the longest run left was 60 KB, so a 63 KB and a 120 KB sound didn't fit. So there it is mostly size, with fragmentation on top: 1,360 KB plus a 120 KB line needs about 1.5 MB in one piece. The same few sounds are asked for again tick after tick, so "failed for room" counts tries, not missing sounds.

## On the PSP

Three tests, in order. Keep words come from the launch menu (b113 lists its own) or `keep.txt`. Deathless, as before. No USB capture.

**Before the first test: install**
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b113.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b113 is on the stick"
rm -f $psp/halo/shots/*
umount $psp
```
The log's first line should say `bring-up build 113`.

**After each test: pull the log** (`log.txt` is wiped at every launch). Change the name for each test:
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b113_test1.tgz -C $psp/halo log shots
rm -f $psp/halo/shots/*
umount $psp
```

**Test 1: b113 as it comes, through the cliffs** (about 30 minutes)
- Keep words: `nearauto assettrack geo2 hwprofile gebusy`.
- Play from the start, through the Warthog drive and the cliffs, down to the map room. **Stop** when you reach the map room's descent: b112 run 5 got there in about 29 minutes.
- Take a screenshot (select + right) at the slowest moments, as in run 5.
- If a Warthog ends up on its roof, try the flip prompt from either side.
- What it answers: whether 512 render states speed up the cliffs (against run 5: 2.4–2.8 fps there), and how busy the GE is (`GE busy` in the `frameend:` lines).

**Test 2: a bigger list, and the sound cache's best set-up** (about 12 minutes)
- Keep words: Test 1's plus `list768 sound1792 soundplace`.
- Play the opening bench, as Test 1 did: from the start into the first bunker. **Stop** in the bunker (about 13 minutes).
- Listen during the Pelican flight: that's where b112 dropped most of its sounds.
- What it answers: whether the far trees and the beach's objects stop blinking (draws skipped with the list full, against run 1); whether the placement takes the drops below `sound1792` alone (against run 6's 16); and, from the new `no room for` lines, how much the playing sounds really hold on the PSP.
- Heap with both: about 5,670 of 5,888 KB.

**Test 3: the looks** (about 10 minutes)
- Keep words: Test 1's plus `contrails zoommask`.
- Land and fight on the beach. Watch the plasma bolts and the needler's needles.
- Zoom the pistol (tap select) a few times.
- Take screenshots of a tracer and of the zoom.
- If there's time, relaunch with `zoommaskfull` in place of `zoommask` and zoom once more.
- What it answers, by eye: do the tracers look right, and which zoom looks like the Xbox: none (b112), the soft edge, or the full mask?
