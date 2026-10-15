# Review: build 129

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). b129 is b128 (`review_b128.md`) plus the branches below, and its own profile (`campaign-b129`). Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

| Branch | What | Kind | Default | Words | Review |
|---|---|---|---|---|---|
| `flarewalls129` | A flare's ray asks the level first; the objects are tested only when no wall hides the flare | exact speed-up | on | `flaresfull` (off), `flarescheck` (check) | §1 |
| `fraction129` | The animations' and periodic functions' fractions in single precision, the same bits | exact speed-up | on | `fractiondouble` (off), `fractioncheck` (check) | §2 |
| `periods129` | The collision log's per-period clear and copy only in debug-tool builds | exact speed-up | on (compile-time) | — | §3 |

## Release check

Lab 38761997380, against b128 as built: play and both seeds identical at every common tick on d20 (14,881), a50 (24,811) and c20 (18,782) [E]; the whole state differs only where builds always do (the decal vertex cache, the allocations that follow the emulator's speed, the objects' marks). The logs read whole. b30 and c40 shots right; the multiplayer build plays Blood Gulch. Built from `d855672` (lab sierra-build 38762103752).

## §1 `flarewalls129` (`psp_fixups.py` FIXES_129 `rasterizer_xbox_widgets.c#walls`; `psp_ge.c`'s log line)

**Why.** A lens flare shows only if a ray from the eye reaches it. The port casts that ray against the level and the objects in its way (vehicles, scenery, machines, characters), every third frame per flare. On c20 the PSP-1000 spent **6.9 ms a frame** on it (5.5% of the frame; lab 38745678938): 18 rays a frame at ~380 µs, and the objects' part was 86% of each ray (296 µs against the level's 49). Nine flare answers in ten were "hidden".

**What.** The ray asks the level alone first, with the full test's surface flags. If it meets a wall that isn't sky, the flare is hidden, and the objects aren't tested. Otherwise the full test runs as before.

**Why it's exact** [D]. `collision_test_vector` walks the level first and tests objects only in the leaves the ray crossed up to its wall, and an object can only replace the answer with a nearer hit. So when the level's nearest hit is a wall that isn't sky, the full test's answer is "hidden" whatever the objects say: either that wall or an object in front of it.

**Key lines.** In the flare test, `level_hides = collision_test_vector(front | ignore_invisible | structure, …) && collision.material_index != NONE;` then `if (level_hides && !psp_flare_walls_check) psp_flare_walls_hidden++; else { the full test, as before }`.

**Proven.** [E] (lab 38748855416) `flarescheck` compared every answer with the full test on c20, a50, d20 and b30: **0 of 247,108 differ**. On c20 89% of flare rays were answered by the level alone. Play and both seeds identical at every tick against `flaresfull` on c20 and d20; the state differs only in the objects' marks (the ray stamps the objects it tests), contrail points and looping sounds. The emulator's flare step on c20: 1.49 → 0.42 ms a frame.

**On the PSP** [M] (lab 38749083501, the PSP-1000, c20 hands off, on/off/on/off): **the frame −1.9%** (119.6 against 121.8 ms) and drawing −3.5%, both real, once the Flood are about (game time 1,350 on); the opening in the noise. The flare step 1.72 against 5.35 ms a frame.

## §2 `fraction129` (`psp_fixups.py` FIXES_129 `halo_math.h#fraction`, `model_animations.c#fraction`, `periodic_functions.c#fraction`; `psp_platform.c`)

**Why.** Every animated node's frame fraction, the aim and the overlays, and the periodic functions behind objects' and shaders' function values computed `(real)fmod((double)x, 1.0)` (and one of them `(real)floor(fabs(x))`) on a float. The PSP has no double-precision FPU, so each was a software library call: `__ieee754_fmod` and `__pack_d` were ~2% of c20's tick fetches in the cache models [E].

**What.** `psp_fraction_of(x)` in `halo_math.h` gives the same answer in single precision: below 1 a float is its own fraction; from 2^23 up every float is an integer (fraction ±0); in between, `x - (float)(int)x`, which a float holds exactly. A zero fraction takes x's sign, as fmod gives it. Infinities and NaNs still take the double path. `psp_floor_of_abs` does the same for the floor.

**Why it's exact** [D]. The fraction of a normal float is a multiple of its last bit and smaller than it, so it fits a float, and a subtraction whose exact result fits is exact. Build 115 tried the same idea (E16) and held it back on a whole-state check made before `statecmp.py`'s play hash: the differences it saw (the decal vertex cache, objects, actors) are those every pair of builds shows.

**Key lines.** `psp_fraction_single`: `if (exponent < 127) return x; if (exponent >= 127 + 23) return x < 0.0f ? -0.0f : 0.0f; fraction = x - (float)(int)x;`. The eleven call sites in `model_animations.c` and `periodic_functions.c` call `psp_fraction_of` under `HALO_PSP`.

**Proven.** [E] (lab 38750446540) `fractioncheck` made every fraction and floor both ways and compared the bits: **0 of ~4.8 million differ** (c20, a50, d20, b30). Play and both seeds identical at every tick against `fractiondouble` on c20, a50 and d20. Cache models, the same play to game time 120 s: **c20's tick −3.3%** (19.28 against 19.93 ms), a50's −1.7%.

**On the PSP** [M] (lab 38758017491, the PSP-1000, c20 hands off, on/off/on/off): **the tick −3.4%** (24.5 against 25.4 ms) and the frame −1.7% once the Flood are about, the opening's tick −3.8%, all real.

## §3 `periods129` (`psp_fixups.py` FIXES_129 `collision_usage.c#periods`)

**Why.** Bungie's collision log keeps per-period figures for a debug display. Build 106 stopped its per-call clock reads and counts outside `PSP_DEBUG_TOOLS` builds, but each period still cleared its 2,952 bytes of figures as it began and copied them out as it ended, about 73 times a second: 0.2 ms a frame in b127's ladder profile [M].

**What.** Outside `PSP_DEBUG_TOOLS` builds, the clear and the copy are left out; the period's number is still set and cleared, so the log's asserts see what they did. Debug builds (the collision report) are unchanged.

**Why it's exact** [D]. Nothing but the debug display and the debug builds' report reads the figures; the period's number, which the per-call functions would read, is kept.
