# Review: cheap, exact findings (`flags123`)

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). Same shape as the other reviews: **what**, **key lines**, **why**, **proven**, **default**. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

Branch `flags123`, from `w122` (`85ebea3`, build 122 + docs).

| Change | Kind | Default | Words / switches |
|---|---|---|---|
| 1. PGO flag sweep in the cache models | build setting | SWEEP_DEFAULT | `PSP_PGO_HOT_FRACTION`, `PSP_PGO_HOT_PERMILLE`, `PSP_PGO_ALIGN_FUNCTIONS`, `PSP_PGO_ALIGN_LOOPS` (new switches) |
| 2. `ai_profile_update` samples only for a display | exact speed-up | **on** | `aiprofilefull` (off: as before) |
| 3. Activation census | recon | off | `activecensus` |
| 4. Doc fixes | docs | | |

---

## 1. The PGO flag sweep

**What.** Two thirds of the main CPU's misses are instruction fetches, and GCC's PGO "hot" set is ~600 KB, 37 times the 16 KB instruction cache. Build 122's code and profile (`campaign-b122`, the lean flags `-fno-unroll-loops -fno-peel-loops -fno-tracer -fno-ipa-cp-clone`), with one flag group changed at a time, through the cache models: b30's flight and c20's opening, hands off, 60 s of game time [E]. Lab 38614069299 (`flags123a`) and 38614378331 (`flags123b`), commit `0513e6a`.

**How it's judged.** Per tick: instruction misses, and the estimated tick = instructions + 72 cycles a miss (instruction and data) at 333 MHz. The runs had `statecrc` on, whose hashing runs in the tick; its two functions (`psp_state_hash`, `psp_objects_play_hash`) are taken out by the cache map's per-function counts, and so are the log writes' kernel fetches, which the map can't attribute (they vary with the emulator's I/O timing). c20's runs ended at 1,728–1,800 ticks (the wall); `ws950` against base shows a few ticks' difference moves the per-tick figures by 0.01%.

| Flags (on top of b122's) | `.text.hot` | Program text | Heap | b30 I miss/tick | b30 est. tick | c20 I miss/tick | c20 est. tick |
|---|---|---|---|---|---|---|---|
| **base** (build 122) | 605 KB, 746 fns | 3,380 KB | 5,716 KB | 84,618 | 49.94 ms | 50,567 | 22.54 ms |
| hot fraction 1000 | 291 KB, 383 | −46 KB | +44 KB | +2.4% | **+0.9%** | −1.6% | +0.2% |
| hot fraction 300 | 170 KB, 218 | −65 KB | +64 KB | +4.6% | **+2.2%** | −0.5% | +0.9% |
| hot fraction 100 | 101 KB, 110 | −80 KB | +76 KB | +7.2% | **+5.4%** | +0.1% | +3.5% |
| ws-permille 950 | (same EBOOT as base) | | | 0 | 0 | 0 | 0 |
| T4a: no unswitch, split, version-for-strides, predictive commoning | 589 KB | −34 KB | +32 KB | −2.5% | **−1.7%** | −4.7% | **−1.6%** |
| functions aligned to 64 | 628 KB | +111 KB | −108 KB | −9.9% | **−4.1%** | −10.0% | **−4.0%** |
| functions 64, loops 32 | 630 KB | +119 KB | −116 KB | −9.6% | **−4.1%** | −10.7% | **−5.0%** |
| **functions 64, loops 32, T4a (the new default)** | 615 KB | +84 KB | −84 KB | pending | **pending** | pending | **pending** |

Sizes from each build's `halo.map` (local builds at the same commit and flags; kept sections only).

**What it says.**
- **Shrinking GCC's hot set loses.** A smaller hot set means more of the counted code is "warm" and built for size: more instructions (b30 +3.4% at 1000, +8.9% at 100), and on b30 more misses too. The split was never the problem; how many lines the hot code spans is. (`hot-bb-count-ws-permille` is read only with LTO: the control, the same EBOOT.)
- **Alignment wins most.** `-O2` on allegrex aligns functions to 4 bytes [D: `psp-gcc -Q --help=optimizers`, a test compile]; a function that starts mid-line drags a partly used line in, and its last line is partly used too. Aligned to the 64-byte line, instruction misses fall ~10% on both maps. Loops to 32 adds c20's −1%.
- **T4a helps a little and pays for some of the padding:** the four transforms copy loops (versions of the same loop for each case), more code for the cache to hold.

**Exactness.** Flags can change code generation, floats included, so each is proved on play, not argued. In these cache-model runs: c20 played identically on every variant, play and both seeds at every tick [E]. **b30's flight is no judge across builds in the cache models:** any two builds with different code layout part ways after tick ~1,050–1,460, even `af64` against `af64l32`, which differ only in the padding before loops (no instruction changes); the same build twice (base, `ws950`: one EBOOT) plays identically. Something in that stretch depends on how fast the code runs, not on what it computes [E→U]; worth knowing for future A/Bs (a note in the lab's findings). The proof that counts is the statecrc pair below.

**Proven.** Pending: lab 38620060902 (`flags123d`): the default's flags and the alignment alone, hands off with statecrc on d20, a50, c20, against `flags123c`'s on side (lab 38615620299, build 122's flags, the same commit `b7ca423`), and the default's flags in the cache models. Locally, the default build is byte for byte the build with these flags given by hand (EBOOT `c5fc58e8…`).

**Default.** `psp_build.py`: in a `PSP_PGO=use` build the counted units get `-falign-functions=64 -falign-loops=32 -fno-unswitch-loops -fno-split-loops -fno-version-loops-for-strides -fno-predictive-commoning` unless told otherwise. **The off side** (build 122's flags): `PSP_PGO_ALIGN_FUNCTIONS=0 PSP_PGO_ALIGN_LOOPS=0 PSP_PGO_LOOP_TRANSFORMS=1`. The release's own `PSP_PGO_EXTRA=-fno-unroll-loops,-fno-peel-loops,-fno-tracer,-fno-ipa-cp-clone` stays as it is. The heap gives 84 KB to the padding (5,632 KB in the local build; b30 needs ~5.2 MB).

**Key lines.** `psp_build.py`, after `PGO_EXTRA`: the loop over `PSP_PGO_HOT_FRACTION`, `PSP_PGO_HOT_PERMILLE`, `PSP_PGO_ALIGN_FUNCTIONS` (default 64), `PSP_PGO_ALIGN_LOOPS` (default 32), and `PSP_PGO_LOOP_TRANSFORMS`. These are switches of their own because a lab build switch can't carry an `=` in its value.

---

## 2. `ai_profile_update`: samples only for a display

**What.** Every tick `ai_update` calls `ai_profile_update`, which samples 28 meters for the AI debug display: two walks over every actor (`ai_profile_sample_units`, `ai_profile_sample_swarm_actors`) and, per meter, a history slot and a float division for the average [D]. Only the display reads the samples, values, histories and averages: `ai_profile_render` (behind `ai_show_*`, all off unless a script or the console turns them on) and `ai_profile_display` (behind `profile_display`) [D]. With none of those on, the update now only zeroes the 28 accumulators and returns.

**Key lines.** `psp_fixups.py`, `FIXES_123["source/ai/ai_profile.c#skip"]`:
- `psp_ai_profile_shown()`: `profile_display`, the word `aiprofilefull`, or `ai_profile.show` with any `show_*` flag.
- At the top of `ai_profile_update`: `if (!psp_ai_profile_shown()) { every meter's accumulator = 0; return; }`.

**Why the reset stays.** The port's perception (FIXES_121 `psp_perception_begin`, FIXES_122's merays) reads the line-of-sight and collision accumulators as counters within a tick, saves them and writes them back; they are `short`s, so they must start each tick at 0 as the full update leaves them, or they would wrap [D]. After the skip, every accumulator holds what the full update would have left: 0.

**What it saves.** Two actor walks and 28 history updates a tick: 0.05–0.2 ms of b30's tick by the research's estimate [U]; the cache models' figure is below (SKIP_SAVED).

**Proven.** SKIP_PROVEN

**Default.** On; `aiprofilefull` brings back every tick's samples. Turning a display on mid-game starts its history then.

---

## 3. Activation census (`activecensus`, recon)

**What.** Every 150 ticks (5 s of game time), from `ai_update` while the AI is initialised for the map, a log line:

```
activecensus: game time 1650 | objects active/all: biped 12/30 vehicle 2/5 … | actors 10/24 active, 8 dormant, 1 forced | encounters 3/9 active, 0 forced | squads 2/40 dormancy off
```

Objects come from the object header array (`_object_header_active_bit`, by type); actors from `actor_data` (`meta.active`, `meta.dormant`, `meta.force_active`); encounters from `encounter_data` (`active`, `force_active`); squads with `disable_dormant` (a script's `ai_allow_dormant false`) [D]. It only reads.

**Key lines.** `FIXES_123["source/ai/ai.c#census"]`: `psp_activation_census()` before `ai_update`, and its call at the end of `ai_update`'s `if (update_ai)` block, behind `game_time_get() % 150 == 0 && psp_keep("activecensus")`.

**Why.** Before designing another activation lever we need to know what stays awake and why: which object types stay active, how many actors are forced active or kept from dormancy by the scripts (those levers are the scripts', not ours), and how many are awake on their own.

**Proven.** CENSUS_PROVEN

**First readings.** CENSUS_READINGS

---

## 4. Doc fixes

- `optimization_ranking.md` X8: `object_connect_lights` isn't lighting; it re-links an object's attached point lights to the map's clusters and returns at once for an object without any [D]. By exact counts it is below the top 40 of the tick on all six levels (tickmap118) [E]. The "4.9 ms target" came from a step share. Parked.
- The lab's `levers.tsv`: RJ (loads first) and RL (branch-free selects) no longer wait on a stall-on-miss rig test: halo_bench v7 [M] (`memory_traffic.md`) already showed the Allegrex stalls on every missed load with no hit-under-miss. RH (sort actors by AI state) is a behaviour change, not exact: actors draw from the one global random seed as they update, so another order gives them other numbers.
