# Review section for build 122: posing

For `docs/review_b122.md`. Same shape as `review_b121_machines.md`: **what**, **key lines**, **why**, **proven**, **default**. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

Branch `posing122` (from `w121`, `49d0938`).

| Change | Kind | Default |
|---|---|---|
| 1a. The root's second validity check dropped | exact speed, small | on (`posingold` off) |
| 1b. The root node's frame remembered per object | exact, no gain (misses) | off (opt-in `posingmemo`; `posingcheck` checks) |
| 2. An animation's repeated data ask answered inline | exact speed | on (`animlockcall` off) |

---

## Where posing's time goes

Posing is everything from an object's animation to its node matrices: decoding the keyframes, the quaternion work, the overlays and aiming, and composing each node's matrix with its parent's. The tick's instruction fetches in the cache models, functions in each map's top 40 only (so these are floors), hands off to game time 60 s (c20, a50) or b30's beach on the b113 route to 100 s; lab 38543568066 (`c17bc51`, w121's sphere walk) [E]:

| Part (functions) | c20 | a50 | b30 |
|---|---:|---:|---:|
| Node matrices (`object_compute_node_matrices`, `matrix4x3_*`, `vfpu_multiply`, `valid_real_matrix4x3`) | 6.0% | 2.6% | 3.4% |
| Keyframe decode (`animation_keyframe_search`, `animation_get_keyframe_*`, `quaternion_decompress_6byte`, `psp_anim_lock`, `animation_get_node_orientations`) | 7.9% | 0.5% | 2.8% |
| Quaternion math (`quaternions_interpolate_and_normalize`, `quaternion_normalize`, `quaternions_multiply`) | 2.9% | 1.8% | 2.6% |
| Overlays and aim (`aiming_screen_apply`, `overlay_animation_*`) | 0.7% | 0.9% | 1.2% |
| **Posing** | **17.5%** | **5.8%** | **10.0%** |

The largest single functions on c20: `object_compute_node_matrices` 2.9%, `animation_keyframe_search` 2.0%, `animation_get_keyframe_rotation` 2.0%, `matrix4x3_rotation_from_quaternion` 1.7%, `quaternion_decompress_6byte` 1.5%, `vfpu_multiply` 1.4%, `quaternions_interpolate_and_normalize` 1.4%, `psp_anim_lock` 1.3%.

- **No one hot spot.** Each node of each animated object pays a little in many places: per channel (rotation, translation, scale) a data ask, the header's bounds checks, a binary search for the keyframe, a decompress, an interpolation and a normalize; then a quaternion to matrix and a multiply.
- **c20 is the decode-heavy map**: the Flood's animations are compressed and streamed from the geometry pack (`psp_anim.c`), so every keyframe read asks the animation cache for its bytes: 734k asks in 60 s, 96% of them for the range handed out last [E, the log's `animations:` line].
- **What repeats exactly from tick to tick** is the work before the animation: an object's place in the world. A machine turns its gears by animation and stays where it is; the root's frame (`translation x rotation`, with the centre of mass and origin offset) is the same product every tick. The animation itself moves on a frame every tick, so its decode and the nodes' products don't repeat for the objects that are posed (`machinesleep` already skips the machines that don't move).
- **Already out of double**: `fast_ftol` rounds in float (FIXES_CHUPA), and `halo_math.h` narrows `sqrt`, `floor`, `fmod` on float arguments (build 19). What's left in double in posing is the animations' explicit `fmod((double)x, 1.0)` (aim and overlays), which E16 tried in build 115 and held back (`docs/build_b115.md`).

## 1. The root node's frame, remembered (`8d04c19`, opt-in since `0aa3f58`)

**What.** For an object with no parent that isn't world-relative, the root node's matrix is `(T x R') x N`: `T` the object's position, `R'` its rotation from forward and up (left negated if mirrored) times the centre-of-mass and origin-offset translations, `N` the root's own orientation from the animation. `T x R'` depends only on the position, forward, up, the mirrored bit and the definition's tag data. It's now kept in one of 256 slots (by object index) with all of those inputs; when they all match bit for bit, the root is `kept x N` and the rest of `T x R'` (two translations, a rotation from vectors, two or three multiplies) is skipped.

Also, the root's matrix was tested with `valid_real_matrix4x3` twice in a row with nothing between: the second test now runs only if the first failed.

**Key lines.** `psp_fixups.py`, `FIXES_122["source/objects/objects.c#rootframe"]`:
- `psp_pose_matches`: the object index, the definition index and pointer, the mirrored bit, and the nine floats compared as bytes (`csmemcmp`), so -0 and +0 differ and nothing that differs in a bit matches;
- the fast path, before `matrix4x3_translation(&object_translation_matrix, …)`: only when `!object_node_matrix` (no parent: a parent's matrix moves with it, and its scale and mirror change the product);
- `psp_pose_keep` after `matrix4x3_multiply(&object_translation_matrix, &object_rotation_matrix, …)`: the product as the engine made it;
- `psp_root_placed:` and `psp_root_valid`: the first validity test's answer, reused by the second;
- `objects_initialize_for_new_map`: `psp_pose_on = -1`, so the slots are emptied before the new map's first pose (a definition index can name another tag).

**Why it's exact** [D]. `T x R'` is a pure function of the inputs in the key (tag data doesn't change within a map, and the slots are emptied between maps), so the kept product has the bits a fresh one would. `matrix4x3_multiply` reads both inputs before it writes (the FPU path through `product[12]`; the VFPU path is only used when its start-up probe found aliasing safe), so `kept x N` into `object_nodes[0]` gives the bits that `object_nodes[0] x N` did. `valid_real_matrix4x3` only reads.

**Proven exact** [E, lab 38559154038, `8d04c19`, both parts on against `posingold`, hands off, `fixedticks fixedseed statecrc`, `tools/statecmp.py --ignore="object looping sounds,first person weapons"`]:

| Map | Ticks compared | Play hash | Global seed | Local seed | Allocations that differ (they follow the emulator's speed) |
|---|---:|---|---|---|---|
| c20 | 17,110 | same at every tick | same | same | contrail point, objects, a one-checkpoint lights blip (13,260) |
| a50 | 22,441 | same | same | same | objects, actor; the last checkpoint truncated |
| d20 | 14,341 | same | same | same | objects, contrail point |

`posingcheck` (every remembered frame made again and compared), hands off: c20 0 of 953,596 differed, a50 0 of 947,254, b30's route 0 of 793,278 [E].

How often the frame repeats (the `posing:` lines, hands off): c20 64% of root frames, a50 89%, d20 85%; c20 poses ~53 roots a tick [E].

**Worth** [E, cache models, hands off to game time 60 s, the PSP-1000 estimate: one cycle an instruction plus 72 a miss]:

| | c20 | a50 |
|---|---:|---:|
| Tick instruction fetches | -0.95% | -0.33% |
| Tick data misses | +1.33% | +0.45% |
| Tick instruction misses | +0.47% | -0.28% |
| Estimated tick | +0.23% (+0.05 ms) | -0.12% (-0.02 ms) |

b30's beach pair (-4.2%) isn't a measure: its play parts across any speed change (2,867 against 2,932 ticks reached).

So the memo saves ~380 instructions a reused frame, but its 24 KB of slots (256 x 96 bytes, past the 16 KB data cache) miss about as much as that saves. It's exact and stays in, off.

**Default.** The second check skipped: on (`posingold` restores it). The memo: off; `posingmemo` turns it on, `posingcheck` turns it on and compares; the log's `posing:` line every 300 ticks counts frames reused, made, and remembered ones that differed.

## 2. The animation data's repeated ask, inline (`c981570`)

**What.** Every keyframe read asks `psp_animation_data` for its bytes; for a map whose animations live in the geometry pack (c20's Flood, b30's and a10's cinematics) that calls `psp_anim_lock`, whose first test is "the same range as last time?". 96% of asks pass it. `psp_animation_data` now makes that same test inline, on the same variables, and returns the same pointer, counting the repeat the same way; the rest still call `psp_anim_lock`.

**Key lines.** `psp_fixups.py`, `FIXES_122["source/models/model_animation_definitions.h#inline"]`; `psp/src/psp_anim.c`: the memo, `repeats`, `forget_pending` and `trace` shared under `psp_anim_` names (`#define`s keep the file's own names), and `psp_anim_inline` read from `animlockcall` when the pack opens.

**Why it's exact** [D]. The same condition on the same globals in the same order as `psp_anim_lock`'s first lines, and the same result. No float is touched.

**Proven exact** [E, lab 38560000156, `c981570`, inline against `animlockcall`, hands off, as above]: c20 21,181 ticks, a50 27,271, d20 22,023: the play hash and both seeds the same at every tick; only objects, contrail point and actor differ (and the truncated last checkpoint).

**Worth** [E, same batch, c20 cache models to game time 60 s, the memo on in both]: tick fetches -0.78%, instruction misses -0.44%, data misses +0.02%; estimated tick -0.45% (-0.10 ms of 23.4). `psp_anim_lock` (1.3% of c20's tick fetches) leaves the top 40. Maps whose animations are in the tags (a50) never call it: no change there [D].

**Default.** On. `animlockcall`: every ask calls `psp_anim_lock`, as before.

## Gain

- c20: -0.45% of the tick from the inline ask [E]; the second validity check a little more [U, not separated: posing122c, lab 38564505953, measures the shipped default against both off words if it ran].
- a50: about nothing (no streamed animations; the check skip only) [U].
- The multiplayer build compiles (`8d04c19`, lab 38559154038's build 2). Networked clients pose with these changes too: both are bit-exact, so nothing a client computes differs [D].

## What's left in posing (for later)

- **Hoist the decode's per-node overhead** [U]: each channel of each node re-reads the compressed header, makes up to five bounds checks (`animation_data_contains`) and a binary search; the checks and header are per animation, not per node. Exact (pure checks), maybe a third of the decode.
- **E16 again** [U]: `fmod((double)x, 1.0)` in the aim and overlays (`__ieee754_fmod` is 0.7% of c20's tick, `__pack_d` 1.2%, not all posing). Build 115's check that held it back ran before `statecmp.py`'s play hash; worth a fresh pair.
- **A memo that doesn't miss** [U]: the root frame kept beside the object's own node matrices (already in cache when posed) instead of in a table; needs room in the object's header blocks, so it costs object memory.

## For the rig

Two `sierra-hw` runs of one build each, keep sets (on, off, on, off), read with `tools/abreport.py`:

1. c20, where both changes act most: `ref=posing122 maps=/bake/campaign121/halo/maps map=levels\c20\c20 init="cheat_deathless_player 1" keep="nearauto assettrack geo2 hwprofile" keepsets="- | posingold animlockcall" repeat=2 wall=420`
2. b30's opening (the fair A/B stretch; its animations stream too): the same with `map=levels\b30\b30`.

Expect a small gap (under 0.5% of the tick on c20 [E]): if `abreport.py` calls it noise, the changes still stand as exact and free.
