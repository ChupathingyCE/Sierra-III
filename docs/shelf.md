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
| The BSP query cache, version 2: a per-object key, the last answers kept in state (BSPR v2) | `bspr2`; `bspsame` (off), `bspsamecheck` (check) | Play and both seeds exact on a30, b30 and c10, 0 answers differing [E]. Takes 0.78% off a30's fight tick and 0.73% off b30's in the cache model [E], under the 1% gate. Not built: rays almost never repeat (8 of 105,290 on b30), so a version 3 would give the 2 ray slots to spheres (6 in all); its D-miss saving (-1.2%) may weigh more on the PSP than the model's flat price, which only a PSP pair can show |
| The sounds' obstruction rays on the Media Engine, the answer taken next frame, riding perception's hand-over (E3; `review_e3.md`) | `e3me`; `meobstruct` (on), `meobstructalone`, `meobstructcheck` (check) | Play and both seeds exact on b30 and a10; the ME's answers match the main CPU's, 0 of 6,772 checked on b30 and c10 (water) [E]. No gain: −0.01 ms a frame on b30's flight in the cache model [E]. Its hand-over pushes the frame before the one the next render asks about (the ticks run between `game_frame`'s count and the render), so b30 took 24 answers of 2,857 [E]; and only the sounds at a spot heard before can be sent (b30 9.8% of tests, ~1 a frame), so fixed it would take ~0.07 ms a frame [U], ~0.1%: under the gate by about ten times |
| The performance readout's two rows built line by line and copied in words, the same pixels (`review_overlaycheap.md` on the branch) | `overlaycheap`; `overlayslow` (the old path) | Same pixels as pspsdk's character-by-character path over 6,000 strings (host test). The readout's text fetches fall 44%, but that is 0.8% of the drawing phase's model cycles on c20, ~0.1 ms a frame [E]: under the gate |

## Taking one off the shelf

1. Build with `PSP_SHELF=1` (or move it back to the plain keep words).
2. Count a profile with it in, at that commit.
3. A/B it inside one build on the PSP-1000, and in the cache model against the build without it.
