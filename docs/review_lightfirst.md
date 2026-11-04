# Review: LIGHTFIRST, the objects' lighting before the model walk

The drawing rebuild's stage 3, first slice (`docs/draw_rebuild.md`). Off by default; keep word `lightfirst`.

## What changed

One fixup group, `FIXES_LIGHTFIRST` in `psp_fixups.py`, on `source/render/render_objects.c`:

- `psp_light_first()` walks the view's rendered-object list once before `process_rendered_objects` draws it, and calls `object_get_cached_render_lighting` for each object that will need lighting.
- `process_rendered_objects` calls it first, outside the shadow pass.

## Why

Per object, the model walk runs about 20 KB of hot code lines: the lighting (`object_get_cached_render_lighting`, `lights_distant_lighting_at_point`, the point lights), the level of detail, `render_model` and the rasterizer's per-part chain. The I-cache holds 16 KB, so each object fetches the chain again. b30 draws about 90 objects a frame at 1.9 parts each: batching parts within one object can't help, batching across objects can. With each stage of the chain in a cache of its own, the cache model takes 39% of b30's drawing I-misses away (c10 43%, c20 25%, d20 23%) [E]. The lighting is the stage that needs no deferred GE records, so it goes first.

## Why it's exact

- Only an object that already holds its render state is lit early. No state is made, taken from another object, or woken (FIXES_WAKE) in a different order.
- It runs only while the state cache has a free slot for every object of the view (plus 16 for the first-person weapon and widgets). So the walk itself never takes an older state either, and the victim choice can't change.
- The walk's own call then finds a state refreshed in this scene. The refresh changes nothing on a second call in the same scene (no refresh, no rebuild, `scene_age` 0), so it returns the same lighting.
- Lighting reads the level's vertex copies and the light texels. Nothing in the model walk changes them inside the frame, so the early call sees what the walk's call would have.

## Proofs

- Exact [E]: play and both seeds the same every tick on b30 (4,500 ticks), c10, c20 (3,600), d20 (2,700). The GE's work list by list is the same on b30 (2,280 lists), c20 and d20. On c10, two start-up lists differed. They differ between batches with the lever off too, so they aren't the lever's. Frames are within the off-vs-off control's spread.
- Price: the PSP pair (b30 route, `- | lightfirst` x2). It's cache work, so the cache model's figure neither sizes it nor dismisses it.
