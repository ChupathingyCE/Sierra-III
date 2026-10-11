# Build 114: b113's tested switches on, the bodies grounded, the drawing trimmed (2026-10-11)

Built overnight on b113 after its three PSP tests (`hardware_b113.md`), while Sheepdog01 slept, with the lab now running up to ten emulators at once (below). Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator (PPSSPP with the local patches, PSP-1000 memory), **[D]** read in the code, **[U]** inference.

Zip `halo_psp_b114.zip`, EBOOT `FINAL_EBOOT`, commit `FINAL_COMMIT`. Heap 5,888 KB (the program sits about 14 KB under the next 64 KB step).

## What changed

Every change has a keep word, typed into `keep.txt` or toggled in the launch menu, except the few that give the same results by construction (marked). The launch menu now has **presets** (below): each test is one press.

### On by default

| Change | Keep word for b113's behaviour | Why |
|---|---|---|
| The display list 768 KB | `list512` | b113 Test 2 [M]: no draw skipped with the list full (Test 1: 13,518 by game time 1,200, the trees blinking) |
| The sound cache 1,792 KB | `sound1536` (the numbered word) | b113 Test 2 [M], with the placement: no sound dropped by game time 4,000, against 108 |
| Big sounds placed at the cache's top | `nosoundplace` | as above |
| The see-through table holds 320 draws a frame | `seethrough160` | b113 dropped 494 (Test 1) and 712 (Test 3, tracers) [M]: smoke, sparks and shots not drawn. 128 bytes a draw, so the table comes from the heap (40 KB) |
| The sleep's checksum multiplies by 33 (a shift and an add) | `fnvsum` (b113's FNV-1a), `weaksum` (b112's) | FNV-1a cost about 1 ms a tick [M]. The flip fix holds (below) |
| A body sleeps only while the level holds it up | `corpseanyground` | The bodies Sheepdog01 saw in midair (below). Restores the game's behaviour, so no A/B (Sheepdog01, 2026-10-11) |
| The tracers' corners the GE can't take are folded away (with `contrails`) | none (`contrails` is off by default) | b113 Test 3's streaks across the screen |

### Exact, with nothing to switch

- **Keep words asked once at their call site** (`PSP_KEEP_ONCE`, `psp_post.h`): seven asks the engine makes per object or per draw (shadows, transparency, particles, decals, the HUD, every bitmap bound, the detail level). `psp_keep` remembers answers already, but each ask still hashed and compared the word: 61,000 instructions a frame of drawing in b112's cache model [E]. b114's cache model no longer lists it among drawing's misses.
- **The model path's tan(fov / 2)** from the remembered value instead of `tanf` per model (14,000 instructions a frame [E]; gone from b114's misses too).
- **The launch menu redraws** whenever an option changes (options past the 32nd wrapped onto the first in its redraw check, so a toggle could leave the screen unchanged).

### Off by default (Sheepdog01's A/B)

| Keep word | What it does | Exact? | What the emulator can say |
|---|---|---|---|
| `listcachedall` | The GE's command words written through the cache, and written back before each hand-over to the GE (X18) | yes, by design | Pictures identical [E]; whether it's faster, and whether a write-back is ever missed, only the PSP shows (the emulator has no caches) |
| `statememo` | Texture binds and model lighting the GE already has left out of the list (`memotex`, `memolight`: one half each) | yes, by design | See "the state memo" below |
| `lean` | The play set-up with the measuring off: `notimers` and `nooverlay` in one word (E2) | yes (the game plays the same; the log's per-step and per-object timings read 0) | −8 to −12% of the frame in the bench's flight, landing and beach [E] |
| `drawdistN` | The far clip at N world units outside cutscenes, the fog closing at it (B9) | no: you see less far | Works; small on b30: −5% of drawing at 60, nothing at 100 [E] |

### Test tools

- **Presets** in the launch menu: `ms0:/halo/presets.txt` (the zip carries b114's), one a line, `name: keep words`. R from the options page; cross applies one (exactly its words); START plays.
- **The last 8 launches' logs are kept** as `log/NNN_log.txt` (with `NNN_profile.txt`, and `NNN_crash.txt` when the Memory Stick lets `crash.txt` be moved). The pull below leaves them out of the archive; a missed pull can be recovered.
- **`statewordsN`, `statewordsall`, `statefullN`** (tests, with `stateobjectsN`): an object's play words, or its whole datum, every tick of the window (#25, below).
- **`tools/benchreport.py`**: pulled logs to the opening bench's tables in one command (the flight, the landing, the beach, the bunker), several runs side by side.
- **`tools/build_release.sh`**: a release from the checkout on a clean machine, the same bytes anywhere (`docs/toolchain.md`); the lab builds through it now.

## The flip with the multiply by 33

FNV-1a fixed the flip (`build_b113.md`) because a single change always shows, and the flip's two neighbouring words go to different lanes. Both hold for any odd multiplier, so b114 multiplies by 33, which the compiler does as a shift and an add (the loop has no multiply instruction [D, the disassembly]). One blind spot is shared with FNV-1a: the top bit of two words four apart (one lane) changed together cancels.

- The flip fixture on Blood Gulch [E]: the Warthog rights itself in 26 ticks (up.k −0.96 → 0.91), as with FNV-1a and with every vehicle kept awake.
- The fast sum against its reference (`checksumcheck`) on b30's route [E]: 4.4 million sums, none different.

## The bodies in midair

**What Sheepdog01 saw** (b113 Test 3, `shot000_t436`): an Elite's body hanging above the beach.

**Why** [D]: a sleeping body skips its whole update, and its own bytes don't change when what it rests on goes away. On the Xbox the body's physics runs every tick and works its at-rest bit out afresh (`bipeds.c`), so it falls the tick its support goes. The shot is where the player had just picked up the Elite's grenades: it may have come to rest on something that then moved away, the player perhaps [U].

**The fix:** as a body settles, the corpse rule asks whether the level is under it, with the biped's own support test (a sphere at the foot of its physics pill, its radius plus 0.05, against the level, broken surfaces left out). A body held up by anything else stays awake and falls as on the Xbox. The answer is kept and asked again only after the body moves.

**Emulator, b30's route** [E]: about one body at a time is found resting on something other than the level and kept awake; 0 to 3 ground tests per report window. Each body kept awake costs its full update (about 0.7 ms a tick on the PSP [M, b102]). The corpses line reports both counts.

## The state memo

b113 Test 3 put "model: bind + state + lighting" at **8–12 ms a frame** for 127–220 parts, about 55 µs a part [M]. Uncached command words alone don't explain that (halo_bench v9: an uncached store about 37 ns, so a part's ~48 words come to 2–4 µs), and the cache model shows drawing's instruction misses spread thin over many small functions [E]: less code on the hot path is what helps.

- **Texture binds:** a bind of the texture the GE already has (same texture, same address, same frame) sends nothing (about 28 words). Every other texture-state call clears the memo: the five pspgu functions are linked with `--wrap`. In the emulator 8–17% of binds were left out (20 of 238 up to 132 of 784 a frame) [E].
- **Model lighting:** only what differs from the GE's lighting as last set (ambient, two lights). Each part still turns lighting on and off around its later passes. In the emulator 143 of 275 lighting sets a frame were left out (52%) [E].
- **Exactness** [E]: screenshots every 2 s of the flight, pixel for pixel. Two runs with the memo off: identical (the pictures are deterministic). `listcachedall`: identical except the clock stamp. `statememo`: identical except the clock stamp.
  - One run of the build before (memo-check2, 38129327298) had one pixel at the screen's edge different (green 52 against 48) in the flight's last screenshot, before play parted there. On the final code, each half alone (`memotex`, `memolight`) and both gave all 24 screenshots identical to the memo off (memo-halves, 38129955293), play parting only at tick 1,750, after every shot. So that pixel looks like something outside the play hash following the emulator's speed (an effect, say), not the memo [U]. The PSP test is by eye as well.

## What the emulator showed [E]

**The opening bench, b30 Test 1's route** (run 38125250298; frames by the clock in ms; the emulator runs about 3.4 times the PSP's speed, so its shares transfer, not its milliseconds):

| Stretch (game time) | b113 (Test 2's words) | b114 | b114 `lean` |
|---|---:|---:|---:|
| flight (300–2,000) | 80.3 | 80.2 | 70.4 |
| landing (2,000–2,700) | 84.9 | 87.1 | 75.9 |
| beach (2,700–6,000) | 49.7 | 50.6 | 46.7 |
| bunker (6,000–8,100) | 40.2 | 40.2 | 39.0 |
| See-through draws dropped, the whole run | 635 | 0 | 0 |

- **b114's defaults cost nothing measurable** against b113 with the same switches typed in, and drop no see-through draws.
- **`lean` is the biggest single saving tonight:** −12% of the frame in the flight, −11% in the landing, −8% on the beach [E]. The timers are system calls, which may cost the PSP more [U].
- One oddity: `lean` failed 39 sound loads for room against 0. The emulator's audio follows its own clock and its sound counts were already unreliable (`build_b113.md`); the PSP decides.

**b30's whole route** (b108 run 1's, run 38122860608): b114 played to game time 19,988 with no fault; no draw skipped with the list full; see-through drops 0 (b113: 177); sound failures 25 (b113: 41); heap peak 5,428 of 5,888 KB.

**The strict interpreter** (same route, every default on): no bad access to game time 1,473 (a strict run is slow).

**The draw distance** (run 38127497143): the cap works and the fog closes at it in the level's colour. On b30 it saves little: drawing −5% in the flight at 60 world units, nothing measurable at 100 or 150 [E]. b30's own fog already runs 5 to 90 units at half density [D, the log], so most of what's drawn is near. The PSP's GE time may gain more than the CPU's [U]: preset 6 tests 60.

**The render-state cache at the cliffs** (512 against 256): on the emulator's route it never filled even at 256, as b111 found (the route meets far fewer objects than Sheepdog01's run). Only the PSP's cliffs can judge it.

**The cache model on b30's opening** (b114, run 38125250298): `psp_keep` and `tanf` are gone from drawing's instruction misses; `psp_step` (the timers `lean` turns off) is still 2.5% of them.

## #25: where play parts across builds

Two runs of one build play identically. Two builds, or keep words that change drawing, part ways near tick 1,350 of b30 even with `fixedticks` and `fixedseed`. Tonight's findings (batches x25-words 38123516507 and x25-full 38124588225; b114 against b114 with `gedefer`, which changes drawing alone):
- **Not the VFPU** (the vector unit): with `novfpu` play still parts at the same tick.
- **The first object:** a standing Elite (slot 947), at tick 1,348, by one unit in the last place of its facing.
- **Its whole datum** (`statefull947`, word 2, the render mark, aside): identical through tick 1,319; at tick 1,320, 163 words part at once, starting with `unit.desired_aiming_vector` and `unit.aiming_vector` (offsets from the PSP compiler). Its AI aims at a slightly different point: its target's position or marker differs [U].
- **And the object mark counter** (`magic_number`, stamped by render and collision passes) differs between the runs from tick 1,204 at least: the engine's own render-side work runs a different number of marking passes with `gedefer`. A deferred GE call reports success to the engine at once, so the engine may branch differently [U].
- **Next:** dump the Elite's target (`statefull` on the player and the marines), and audit the GE layer's entry points whose return value the engine uses, deferred and not.

## The lab: up to ten emulators at once

- One job now plays several emulators side by side (`sierra-batch`): each PPSSPP instance needs about one thread, and the emu runner has 10.
- The first try crashed every instance within a second: PPSSPP keeps the PSP's RAM in `/dev/shm`, 64 MB in a container, about one instance's worth. The emulator patch now takes it from `memfd_create` (here too, `tools/ppsspp_local_patches.diff`).
- **Runs side by side measure exactly what a run alone does** [E]: eight copies of one run played identically to each other and to the run alone, every tick's state and timing line the same. Each got about 60% as far in its wall time, so eight at once is about 4.8 times the work. The build runner (6 threads) takes batches too.
- Tonight: 10 batches, about 60 emulator runs. `docs/testing_pipeline.md` describes the shape.

## Not built tonight, and why

| Item | Why not |
|---|---|
| L1 step 4, the ME's overlap (`gemeon` with the main CPU running on) | The largest structural lever, and the riskiest: double record buffers and hand-over between two CPUs, testable only on the PSP. It needs a day with the PSP at hand. Preset 5 tests b113's step-4b fixes meanwhile |
| L2, VRAM for the GE's reads, in the campaign | Needs a VRAM budget design (the worst views want 1.2–2.55 MB against ~1.2 MB free) |
| X9, collision walks | Structural; days, not a night |
| Profile-ordered linking (lever D) | Already measured on b102 [E]: the best order removed 3% of instruction misses (about 1% of a frame), because the hot path (278 KB) is far bigger than the 16 KB cache. `PSP_TEXT_LAYOUT=coldfirst` exists. Less code on the hot path is the lever (the state memo, `lean`) |
| L4, a profile-guided build | Still blocked: the instrumented program doesn't fit the PSP-1000's layout (`big_levers.md` §6) |
| HUD text a string at a time | Measured: 11–15 characters a frame [M, b113 Test 1], about 0.1 ms. Not worth it yet |
| The holograms (meters) | A visual feature needing a bake change and a re-bake; not speed |

## On the PSP

Eight presets, in priority order. Pick the map (b30) on the first page; then R twice to the presets page, cross on the test, START. Deathless, as before. No USB capture.

**Before the first test: install**
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b114.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b114 is on the stick"
ls $psp/halo/presets.txt        # the presets came with it
rm -f $psp/halo/shots/*
umount $psp
```
The log's first line should say `bring-up build 114`.

**After each test: pull the log.** Change the name for each test. The `--exclude` leaves out the older launches' logs b114 now keeps:
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b114_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
rm -f $psp/halo/shots/*
umount $psp
```

| # | Preset | Play | Stop | Time | What it answers |
|---|---|---|---|---|---|
| 1 | bench, b114 as it comes | The opening bench: from the start into the first bunker | In the bunker | ~13 min | b114's defaults against b113 Tests 1 and 2: the frame, the see-through drops (should be none), sound drops, the heap; watch the beach's bodies for any left in the air |
| 2 | flight, GE words cached | From the start through the Pelican flight | When the Pelican lands and the marines jump out | ~5 min | Is `listcachedall` faster in the flight (the fair A/B stretch)? Watch for garbled triangles, flicker or a freeze: those mean a missed write-back. If it freezes, hold POWER, then pull the log |
| 3 | flight, state memo | as 2 | as 2 | ~5 min | Is `statememo` faster? Same things to watch |
| 4 | flight, lean | as 2 (no overlay: expected) | as 2 | ~5 min | `lean`'s saving on the PSP (the emulator says 12%) |
| 5 | flight, GE work on the ME | as 2 | as 2 | ~5 min | b113's step-4b fixes for `gemeon` (render jobs given 500 ms, audio mixed between records) |
| 6 | bench, draw distance 60 | The opening bench | In the bunker | ~13 min | By eye: is the fog at 60 world units acceptable? And the GE's time against test 1 |
| 7 | beach looks, tracers and zoom soft edge | Land and fight on the beach; watch the plasma bolts; zoom the pistol a few times (tap select); screenshots (select + right) of a tracer and of the zoom | After the first beach fight | ~8 min | Are the tracers' streaks gone? Which zoom looks like the Xbox? |
| 8 | beach looks, tracers and zoom full | Zoom the pistol once or twice | After the zoom | ~3 min | The full-strength mask, for comparison |

Tests 2–5 each change one thing against test 1, over the same flight; `tools/benchreport.py` compares them.
