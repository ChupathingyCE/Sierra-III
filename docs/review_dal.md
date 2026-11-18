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

Earlier version (0f3b381, w140): see the lab's levers.tsv DAL row and measurements.tsv `dalproof*`. This version's proofs are below.
