# Review: texture pre-touch at record time (branch `txpt`, keep word `texpretouch`)

The Media Engine split (`gedefer gemeon geinflight mevram`) draws many more frames with a texture missing than plain does: on b30's route, about 190 frames instead of about 25. This change makes each deferred draw's textures resident on the main CPU as the draw's record is written, so the ME's replay finds them already loaded. Labels: [M] PSP, [E] emulator, [D] code or data, [U] inference.

## Why the frames went untextured [D]

- Plain: `psp_tex_bind` finds the texture missing and reads it from the Memory Stick right there, within the frame's 96 KB budget, so the draw is textured in the same frame.
- Under `gemeon` the bind runs on the ME, which can't read the stick. It only calls `want()`, and the main CPU reads what was wanted at `psp_tex_frame_end`, after the replay. Each texture's first sight therefore costs at least one untextured frame, and more with `geinflight`, which draws a frame behind.
- VRAM is not the cause: the texture cache is 616 KB on every side, and mevram's model copies take no texture room.

## The change

### `psp/src/psp_tex.c`

- `psp_tex_pretouch(key)`: main CPU only (it returns at once under `PSP_ME_WORK()`). It finds the entry and loads it if it's missing, within the frame's budget and room. Either way it marks the texture as this frame's (`last_frame`, the frame set), the same as a bind does.
  - Because the mark protects the texture from the rest of the frame's loads (`protected_at`), a later record's load can't evict a texture an earlier record still needs before the replay binds it.
  - A load that's refused (budget spent, or no room) is left to the replay's bind, which wants it as before. So the change can only add textured draws, never take one away.
- The load's accounting matches a bind's: `misses++` together with the load, so the log's "waited for the next frame" count (misses minus loads) stays honest.
- It remembers the last texture it marked this frame. A model's parts and a level's runs often repeat one key, and then the call does no `find()`.
- A `textures: texpretouch:` log line every 300 frames gives the number of touches, the loads made then, and the loads left to the replay.

### `psp/src/psp_ge.c`

- `GE_PRETOUCH(key)` runs before the `GE_DEFER` of every record that carries a texture key: level (base and lightmap), level detail (both), model base, model detail, multipurpose and glow, sprites, contrails, widget texture, see-through model and level queues, screen quads, zoom mask, light draws, decals, blips and the blips' end (sweep and mask). It fires only while recording (`DEFER_RECORDING() && !defer_paused`), so a draw carried out at once binds as it always did.
- Before it touches the texture cache, `pretouch()` does what the record's own `defer_take` would do first: it parks during a sleep and carries out (or waits for) the frame `geinflight` still has out. The ME is never in a replay while the main CPU changes the texture cache's residency, and the texture is marked in the frame whose replay will bind it.
- It's off with `gemeoverlap`. There the ME replays one half while this CPU writes the other, and both would write the residency words through caches of their own.
- The word is read once in `defer_frame_begin`. It takes effect only with ME semantics (`gemesim`, `gemeon`, `geinflight`, `mebench`).

### Cache write-back

A pre-touch load is the same `load()` that a bind or the frame's end uses: DMA from a staging buffer that's written back first, into the uncached caches. Each hand-over to the ME (`defer_on_me`, `inflight_hand_over`) writes the whole data cache back, `resident_at` and `last_frame` included, before the ME reads anything. So the replay sees the new residency. [D]

## Cost

On the main CPU, per keyed record: one `find()` (a hashed recent slot, then a binary search) and a residency check, skipped for a repeat of the last key. Reads that the ME's replay would have wanted at the frame's end now happen during the record walk instead, so the read time moves; it doesn't grow. Measured in txpt1's cache-model pair (below).

## Proof

- Compiles: campaign and `PSP_MULTIPLAYER=1`.
- txpt1 (emulator): see the findings rows (`untextured_frames_texpretouch`, `texpretouch_cost`). Expected: untextured frames on words + texpretouch near plain's; statecrc the same as words alone (it changes when a texture loads, not what plays).

## Risk

- Hot code: the record writers of every textured draw. With the word off it costs one load and one branch per keyed record.
- The texture-cache residency is touched from the record walk, in the middle of the engine's render walk, only on the main CPU and never while a replay is out.
