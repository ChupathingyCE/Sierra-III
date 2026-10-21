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

On the main CPU, per keyed record: one `find()` (a hashed recent slot, then a binary search) and a residency check, skipped for a repeat of the last key. Reads that the replay would have wanted at the frame's end now happen during the record walk.

Measured on b30's flight (the cache model, 1,800 ticks, M6 #2's words with and without the word) [E]:

- The drawing phase grows 7.8% in model cycles (fetches + 72 per data miss + 64 per instruction miss): about 2.2 ms a frame at 333 MHz. The tick is unchanged (-0.2%) and the frame's end a little smaller (-0.5%). The metrics line agrees: draw 36.4 to 37.3 ms a frame.
- By function: `psp_tex_pretouch` itself, `find()` (about 600 lookups a frame get past the last-key memo), and `fits()`, the loads' search for room, which now runs during the walk rather than at the frame's end. The rest is instruction-cache knock-on in the model draw path.
- The in-flight sync adds no wait: `geinflight` carries out all 300 frames on the ME, 103 KB a frame, on both sides.

## Proof

- Compiles: campaign and `PSP_MULTIPLAYER=1`.
- Untextured frames, plain | words | words + `texpretouch` (emulator, the textures: line at each run's end) [E]:

  | Stretch | plain | words | + texpretouch |
  |---|---|---|---|
  | a10 route, 9,000 ticks | 16 (run 10) | 111 (12) | 17 (10) |
  | c20 hands off, 9,000 ticks | 18 (12)* | 35 (12) | 18 (11) |
  | b30 route, 6,300 ticks | 24 (12) | 192 (14) | 82 (29) |

  *c20's plain figure is from an earlier batch on the same build line.
- a10 and c20 are back to plain's. b30 matches plain for its first 600 frames (19 and 21, the same as plain). After that, loads start waiting for room: 84, then 150, against 0 on the other two sides. 60 more frames go untextured, one run of them 29 frames long.
- Why b30 runs out of room [D/U]: every pre-touched texture is marked as this frame's, and a mark protects it from eviction. On b30 the marked set grows to 2,559 KB of the caches' 2,632 KB (plain 2,444, words 2,410), and loads go from 412 to 552 (evictions 236 to 368). Some keys are marked for draws the replay never binds: a glow map drawn flat, a detail map skipped at distance, a level run culled after its record. Their marks hold room that real binds then can't get.
- Play: on a10 and c20, the play hash and both random seeds match words alone at every tick. The parts that differ are object looping sounds first, then objects, which is the known sound-timing sensitivity. On b30, play splits at tick 1459, after looping sounds move at 690. It splits the same way between plain and words (tick 1165), so this is the same sensitivity, not a change in what plays [U].

## Risk

- Hot code: the record writers of every textured draw. With the word off it costs one load and one branch per keyed record.
- The texture-cache residency is touched from the record walk, in the middle of the engine's render walk, only on the main CPU and never while a replay is out.
- Default: off. It isn't ready to go on by default. The words with it are back to plain's look on a10 and c20, but b30 is only part-way there (82 untextured frames against plain's 24), and it costs about 2.2 ms a frame of drawing [E]. Two things would fix it: mark only the keys the replay will bind, or leave a load to the replay once the marked set nears the caches' size; and memoise `find()` per record kind.
