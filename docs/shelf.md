# The shelf: experiments kept for later

Sheepdog01, 2026-10-16: the experiments that didn't pay are mothballed, not deleted, pending review once the fundamental changes (`plan_memory_stalls.md`) are under way. A shelved experiment must cost nothing when it's off. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

## Built only with `PSP_SHELF=1`

Their hooks sit in hot code, so being switched off isn't enough: the build leaves them out.

| Experiment | Keep words (with `PSP_SHELF=1`) | Why shelved | Where |
|---|---|---|---|
| Node matrices made together after the objects' loop (lever NB, NW step 1) | `nwbatch`, `nwbatchwide`, `nwbatchnobipeds`, `nwbatchheld`, `nwbatchsort`, `nwbatchcheck` | Play exact [E]. Faster on b30, slower on c10 and c20 [M]. Even switched off, its hooks made each object's update ~2.5 KB bigger: b133 ran c10 7% slower for it (the cache model: the same instructions, 26% more instruction misses) | `psp_fixups.py` FIXES_136 (tags `nwbatch`, `nwbatchwrites`); `review_b133.md` §1; `plan_node_matrices_me.md` |

`psp_fixups.py`'s `SHELVED_TAGS` names the fixup groups. To shelve another, give its groups a tag of their own, add the tag there, and list it here.

## Still built, opt-in, shelved

Off by default and outside the per-object path, so they cost nothing measurable when off [U]. If a cache-model pair ever shows otherwise, they move behind `PSP_SHELF`.

| Experiment | Keep words | Why shelved |
|---|---|---|
| The b123 ME hygiene (W1–W4) | `meaudiofirst`, `meraysbatch`, `meaudioranges`, `mepushdrop`, `mestash` | The A/B wasn't clean, and came out slower [M] |
| The ME restarted after a sleep | `mewake` (preset 9) | Fails on the PSP-1000: the restarted ME took no job, and the exit hung (b132 test 7) [M] |
| The warm ME cache | `mewarmtest` | No gain for a decode-bound job [M] |
| A slower ME render job | `mefinishslow` | No effect [M] |

The GE lists on the ME (`gemeoverlap`, lever L1) are the renderer session's to shelve or keep.

## Kept on a branch, never merged

Exact, but under the lever gate (a micro-lever must be >= ~1% of the binding lane): left on its branch, out of every build.

| Experiment | Branch, keep words | Why shelved |
|---|---|---|
| The perception refresh skips objects that aren't bipeds, vehicles or projectiles (PRS; after halo-ce-vita's perception skip, BirchWoodGod) | `prs`; `percall` (off), `prscheck` (check) | Play and both seeds exact on b30, c20 and a30, 0 differences in 1.2 million objects passed over [E]. But it takes only 0.13 ms off b30's 40.8 ms tick in the cache model (~0.3%; half the count's 0.27), 0.03 on c20 and nothing on a30 [E], in hot code |

## Taking one off the shelf

1. Build with `PSP_SHELF=1` (or move it back to the plain keep words).
2. Count a profile with it in, at that commit.
3. A/B it inside one build on the PSP-1000, and in the cache model against the build without it.
