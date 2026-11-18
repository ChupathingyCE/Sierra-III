# Review: DAL, attached lights linked on need (branch `builder/dal2`, on the tock 3989e308)

The lights box's first on-need slice (`docs/box_lights.md` §9). Built in by `PSP_DAL=1`, off by default; in such a build the keep word `dal` turns it on. A default build is the tock's: its mirrored engine is byte-identical (only `objects.c` and `object_lights.c` change, and only under the switch).

**Credits.** The idea is after halo-ce-vita's dark attached-light reconnect skip (BirchWoodGod, his L1, `docs/research/research_vita_insider16.md`). The same-tick form is after Milenko's finding (ChupathingyCE): on the Xbox, an attached light that is off isn't re-placed in the map until it turns on, and is placed on the tick it turns on. No code was taken from either. Both are credited in the README.

## What it does

`object_update` re-links each updated object's attached lights (`object_connect_lights`, on the last tick before a frame under FIXES_LD1). Only drawing reads those links. `lights_preprocess_scene` draws nothing of a light whose colour comes out black: it gets no rasterizer light, and `lights_port_cluster` passes over a light without one, so no object picks it either. On a full relink (disconnect and reconnect), DAL first works out the colour the frame will make (`psp_light_dark`):
- if it's black, the light is disconnected (if it's linked) and isn't re-linked;
- if it's lit and was left off, it's linked now, on the tick it lights.

## Key lines

- `object_lights.c`, `psp_light_dark`: first calls `psp_fvp_settle(light->object_index)`, then the frame's own calls (`object_get_function_value`, `rgb_colors_interpolate_and_scale` on the object's `outgoing_change_colors`). Glow lights (`_point_light_port_glow_bit`), lights that don't connect to the map, and child lights are never called dark.
- `objects.c`, `object_connect_lights_with`: DAL's block sits ahead of LSAME's `psp_light_relink`. A light DAL left off is never passed to `light_disconnect_from_map`, because it has nothing to take out. A disconnect-only or reconnect-only call (`object_set_visibility`) on such a light reconnects or does nothing.
- `psp_dal_may_skip`: a dead biped's lights are all linked, as in 0f3b381.
- `psp_dal_frame_check` (keep word `dalcheck`, with `dal`): before each frame's lights, every light DAL left off is tested again, and one that would be drawn is logged (`dalcheck:`). This is the direct proof that a light is linked on the tick it lights; pictures can't give it where they differ run to run. A hidden object's lights, which the old path leaves off too, are passed over.

## Why this is the same-tick form [D]

- **FIXES_FISSION (FVP, on by default) puts a unit's, item's or projectile's function values and change colours off until after the walk.** These are the objects whose lights DAL skips. `object_get_function_value` makes a waiting object's values early, but only for a valid function index (`function_index == NONE` reads 1.0 with no settle). So a light with no intensity function but with a colour function would have tested last tick's colour. The explicit settle closes that gap: the test always reads this tick's values. This is FVP's own allowed early read, so it's exact.
- **Cluster lists are head-inserted.** The old path moves every relinked light to the head of its lists in walk order. DAL does the same for every lit light, including one that comes up, so each lit light keeps the place the old path gives it.
- **Corpses:** asleep, a corpse makes its functions with no relink, so a dead biped's lights aren't skipped (0f3b381's reason, kept).
- **What differs:** the light pool (a dark light's position and flags) and the light partition. Play and seeds must not change.

## Co-op

The links are a client's drawing state. In ChupathingyCE v0.9.1a, only `source/objects/objects.c`, `source/objects/object_lights.c(.h)`, the port's drawing-side lighting effects (`port/linux/game/*_lighting.c`) and the linker's list name the light partition, `light_reconnect_to_map`, `light_disconnect_from_map` or the connected bit. No networking unit reads them [D]. A host and its clients each link their own.

## Cost

Each full relink of a living object's attached light also computes the light's colour (one function read and one interpolation), and settles that object's FVP values early: some of FVP's cache gain is given back for objects with lights. A dark light saves the marker lookup, the bounding sphere and the cluster join. A `dal:` log line every 30 s of game time counts the relinks and the dark ones.

## Evidence

Earlier version (0f3b381, w140): see the lab's levers.tsv DAL row and measurements.tsv `dalproof*`. It was set aside on 81 systematic a10 frames out of 4,490.

This version, in the emulator [E] (the lab's dalrev1-3: sierra-batch 44374405352, 44374455188, 44380314792; one `PSP_DAL=1` build, sides `-` and `dal`):
- **Play and both seeds identical** every tick on a10 hands off (9,000 of 9,000, six runs, `arrayfullgo`: without it a10 stops at game time 3,965 on B93's full array, both sides alike), a30_leg1 and c10_play1 (9,000), and b30_leg2 seeds 1 and 2 under padhold (3,600, strict).
- **Strict:** 0 distinct sites on both sides.
- **a10 pictures** (framecrc, dalproof3's method at three repeats, 4,485 frames): dal against off differs in 13-24 frames a pair; off against off differs in 15-17, dal against dal in 20-24. One frame is systematic (game time 1,284), and it sits in a window where off differs from itself run to run. That's inside the noise; dalproof5 had 81.
- **dalcheck:** of the lights DAL left off, tested again before each frame, 0 were lit: 0 of 281,892 on a10, 310,309 on a30 and 320,049 on c10. No light is missing from a frame.
- **Dark relinks** (counted on a frame's last tick, FIXES_LD1): a10 34%, a30 57%, c10 59%, b30 54%.

The time is the PSP's to price: a padhold b30_leg2 pair, seeds 1-2, with the PC sampler (the lab's psp_queue `dal-b30pad`).

## The cheap dark test (e8284cb8)

**Why.** The first PSP pair (the lab's findings `dal_psp_b30`, read with the sampler) found the test eating the saving. In b30's tick DAL saved ~300 samples a run: `object_connect_lights_with`, `light_compute_bounding_sphere` and `find_point_lights_for_object_in_cluster`. The test cost ~390: `psp_light_dark` +270-296, FVP's `psp_fvp_settle` +76 and `psp_fvp_make` +18-40. The test runs on every relink, but only ~54% of b30's relinks are dark, so one test cost about 70% of a whole relink.

**What changed.** The relink calls `psp_light_dark_fast(light, object_index, object)`:
- It takes the light's object from the relink instead of looking it up again.
- It reads the intensity (`outgoing_function_values[function_index]`, 1.0 for `NONE`) and the scale (the change colour, or white) directly, as `object_get_function_value` does for an index the object can have.
- It keeps a memo per light: the definition and the exact bits of the intensity and scale it was last tested with, and the answer. While those hold, it doesn't look up the definition (a tag) or interpolate the colour (HSV for some lights).
- It makes FVP's values early only when the object is waiting for the pass and the light reads one of them.
- A child light, another object's light, or an index the engine reads as off takes `psp_light_dark`, the full test.

**Why it's exact.** Within a map, the answer is a pure function of the definition (a tag, which doesn't change), the intensity and the scale: the same calls on the same bits give the same colour. The memo is reset in `lights_initialize_for_new_map`, so another map's tags never meet it; a checkpoint reload needs nothing, since the key is the inputs, not the light. The memo costs ~21 KB of heap in `PSP_DAL=1` builds.

**Proof [E]** (the lab's dalfast1, sierra-batch 44413418298):
- Play and both seeds identical on a10 (9,000, two runs a side), a30_leg1, c10_play1 and b30_leg2 seed 1 (strict).
- Strict: 0 sites on every run. dalcheck: 0 lit.
- `dalmemocheck`, which runs every memo hit the full way too: 0 answers differing.
- The relink and dark totals equal 6ae5a5bf's, so every dark decision is the same.
- Memo hits as a share of tests: b30 75.6%, a30 76.5%, a10 and c10 99%.
- Values made early: b30 32% of tests, a10 21%, a30 21%, c10 12%.

The counts on dalcount's runs (the lab's dalfast2, sierra-batch 44420116538), all exact as above:

| Map | Tests (one a relink) | Dark | Memo hits | Values made early |
|---|---|---|---|---|
| b30 seed 2 | 101,434 | 53.8% | 73.6% | 33.4% |
| c20 | 86,069 | 26.2% | 44.6% | 10.7% |
| d20 | 18,718 | 41.9% | 39.3% | 32.9% |

d20 stops at game time 1,121 on both sides (an effect array full), so its window is short. b30 stays the bench. The PSP prices it: the lab's psp_queue `dal-fast-b30pad`.
