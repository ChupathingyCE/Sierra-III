# Build 115.1: b115 with the animation crash fixed (2026-10-11)

b115 crashed on three campaign maps: a10 at game time 2,477, c40 at 1,424 (both on the PSP), a30 at 118 (the emulator's strict interpreter). All three were one bug, in two places. 115.1 is b115 with that fix and nothing else, so its figures compare with b115's directly; b116 carries the restorations (`b116_candidates.md`). Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator, **[D]** read in the code, **[U]** inference.

Zip `halo_psp_b115.1.zip` (`f37df28c7ecf8519`), EBOOT `f8dd9c426d37901c` (the lab built the same), commit `166da9e933`. Heap 5,888 KB, as in b115.

## What changed

| Change | Why |
|---|---|
| **Compressed animations read with unaligned-safe loads** (`psp_fixups.py` FIXES_116 `model_animations.c#unaligned`; `psp_limits.h`) | Bungie's compressed animations can hold their tables 2 bytes past a multiple of 4. The translation reader read a keyframe (three floats) and a node header (a word) there with word loads, which the PSP's CPU faults on and the emulator's JIT forgives (`hardware_b115.md`, "The crash"). The translation and scale readers now load floats and node headers through packed structs, which GCC turns into `lwl`/`lwr` pairs. The same values, read safely; +172 bytes of code |
| **The default translation in `animation_get_node_orientations`** read the same way (the last pair of that fixup) | The first fix (`0a40fd9`) covered the keyframe readers. The strict check then stopped a10, a30 and c40 at the very same game times on one more read: the node-orientation function copies a compressed animation's default translation itself, for a body part with no translation keyframes, from a table 2 past a multiple of 4 (after an odd count of 6-byte default rotations) |
| The log's first line names the hotfix | `bring-up build 115 (ChupathingyCE; hotfix 1, the animation reads)`. The number stays 115, so b115's tools and baselines apply. Screenshots still say `b115` |
| Presets | `edramcheck` left out (b115's probe stops the Media Engine; fixed in b116). Preset 2 is the campaign ladder's |

## What the emulator showed [E]

The strict interpreter (range, unaligned and FPU checks), 1,800 s a map, deathless, no input.

| Batch | Build | a10 | a30 | c40 | The other seven |
|---|---|---|---|---|---|
| `campaign-strict-b115` | b115 | stopped at 2,418 | stopped at 118 | stopped at 1,360 | clean |
| `campaign-strict-b1151` (38198836039) | 115.1 first fix (`0a40fd9`) | stopped at 2,418 | stopped at 118 | stopped at 1,360 | clean |
| `strict-b1151b-b116` (38202483724) | **115.1 (`166da9e`)** | **3,622, clean** | **2,731, clean** | **3,619, clean** | (b116 ran four of them, clean) |

- All three stops in the first check were one load: `pc 0894936c`, in `animation_get_node_orientations` (`addr2line` on 115.1's program; it is linked at 0, so subtract 0x08804000). The second fix covers it.
- "Clean" means no distinct site: no unaligned load, nothing out of range.
- b116 at `d0315d3` (the same fix plus the restorations so far) was clean on a10, a30, c40, b30, c10, c20 and d40 in the same batch.

## On the PSP

The maps are the campaign bake's, installed for the ladder (`build_b115.md`, the campaign ladder). Pick the map on the launch menu's first page, then R twice to the presets, cross on preset 2, START.

**Install**
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b115.1.zip
ls -l $zip                      # must list the file
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b115.1 is on the stick"
rm -f $psp/halo/shots/*
umount $psp
```
The log's first line should end `hotfix 1, the animation reads)`.

| # | Map | Play | Stop | Time | What it answers |
|---|---|---|---|---|---|
| 1 | a10 | Leave the controls alone through the opening | When you have control in the cryo bay | ~8 min | Past game time 2,477, where b115 crashed twice |
| 2 | c40 | Through the opening, then into the first fight | 5 min after you get control | ~12 min | Past 1,424, where b115 crashed |
| 3 (optional) | a30 | The opening | When you have control | ~6 min | a30's first run on the PSP (b115 failed at 118 in the emulator) |

**After each test: pull the log** (change the name each time):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b115.1_a10.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
rm -f $psp/halo/shots/*
umount $psp
```
