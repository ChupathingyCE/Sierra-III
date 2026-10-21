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
- A `textures: texpretouch:` log line every 300 frames gives the number of touches, the loads made then, the loads left to the replay, the marks stopped at the ceiling and the keys passed over as not resident.

#### Mark only what the replay binds (`psp_tex_pretouch(key, kind, resident_only)`)

A mark holds room that a later load can't then take, so a mark for a texture the replay never binds costs another draw its texture. The record writers now ask for a key only where the replay's own tests would bind it:

- Level detail maps (bound only near the eye): marked only if already resident (`GE_PRETOUCH_IF_RESIDENT`). That costs no load and evicts nothing.
- The multipurpose map: only when the change colour isn't white, or a glow has no glow map of its own: the tests `psp_ge_model_multipurpose` makes before it binds. The glow map: only with a glow, and only if the pack has it.
- A ceiling: nothing more is marked once this frame's set plus the texture would pass `txptceilN` percent of the caches' room (85 by default). Past it, the replay's binds want what's missing, as they do without the word.
- `txptall` marks every key again (the first version's filter); `txptall txptceil100` is the first version exactly.

#### One memo per record kind

A level draw names its base and its lightmap in turn, so one memo for every kind missed nearly every time. Each of the 18 record kinds now keeps its own last key, and a key already marked this frame returns before the ceiling and the load path.

#### `txptcensus` (built only with `PSP_RECON`)

Per record kind, the textures marked in a frame against those the replay then bound, at each of the first 8 frames that waited for room, and every 300 frames the totals, with the keys a filter passed over that the replay bound anyway. It's how the first version's over-marking was found and how the filters were checked.

### `psp/src/psp_ge.c`

- `GE_PRETOUCH(key)` runs before the `GE_DEFER` of every record that carries a texture key: level (base and lightmap), level detail (both), model base, model detail, multipurpose and glow, sprites, contrails, widget texture, see-through model and level queues, screen quads, zoom mask, light draws, decals, blips and the blips' end (sweep and mask). It fires only while recording (`DEFER_RECORDING() && !defer_paused`), so a draw carried out at once binds as it always did.
- Before it touches the texture cache, `pretouch()` does what the record's own `defer_take` would do first: it parks during a sleep and carries out (or waits for) the frame `geinflight` still has out. The ME is never in a replay while the main CPU changes the texture cache's residency, and the texture is marked in the frame whose replay will bind it.
- It's off with `gemeoverlap`. There the ME replays one half while this CPU writes the other, and both would write the residency words through caches of their own.
- The word is read once in `defer_frame_begin`. It takes effect only with ME semantics (`gemesim`, `gemeon`, `geinflight`, `mebench`).

### Cache write-back

A pre-touch load is the same `load()` that a bind or the frame's end uses: DMA from a staging buffer that's written back first, into the uncached caches. Each hand-over to the ME (`defer_on_me`, `inflight_hand_over`) writes the whole data cache back, `resident_at` and `last_frame` included, before the ME reads anything. So the replay sees the new residency. [D]

## Cost

On the main CPU, per keyed record: one `find()` (a hashed recent slot, then a binary search) and a residency check, skipped for a repeat of the last key. Reads that the replay would have wanted at the frame's end now happen during the record walk.

Measured on b30's flight (the cache model, 1,800 ticks, 899 frames, M6 #2's words with and without the word) [E]:

- The drawing phase grows 5.2% in model cycles (fetches + 72 per data miss + 64 per instruction miss): about 1.5 ms a frame at 333 MHz, down from 7.8% (2.2 ms) before the memo and the filters. The tick is unchanged (-0.02%) and the frame's end a little smaller (-0.6%). The metrics line: draw 36.38 to 36.87 ms a frame.
- On the same flight the word takes untextured frames from 75 to 24.
- What's left is the `find()` per new key, the loads' search for room (`fits()`) now made during the record walk instead of at the frame's end, and instruction-cache knock-on in the model draw path.

## Proof

- Compiles: campaign and `PSP_MULTIPLAYER=1`.
- Untextured frames, plain | words | words + `texpretouch` (emulator, the `textures:` line at each run's end; longest run in brackets) [E]:

  | Stretch | plain | words | + texpretouch | + texpretouch, first version (`txptall txptceil100`) |
  |---|---|---|---|---|
  | b30 route, 6,300 ticks | 23 (12) | 175 (14) | 30 (12) | 96 (30) |
  | a10 route, 9,000 ticks | 16 (10)* | 111 (12) | 14 (10) | 17 (10)* |
  | c20 hands off, 9,000 ticks | 18 (12)* | 35 (12)* | 19 (12) | 18 (11)* |

  *From the first version's batch, the same code paths and routes.
- Loads that waited for room on b30: 0 with the word (0 plain, 0 words), against 198 with the first version. `txptall` with the ceiling alone gives 5 and 70 untextured frames: the ceiling stops the waits, but the marks it keeps still crowd out real binds. The filters are what bring b30 back.
- Largest frame set on b30: 2,416 KB with the word (plain and words 2,411; the first version 2,562, of the caches' 2,632). Loads 377 (plain 375, words 397, first version 572), evicted 199 (203, 216, 377).
- Why the first version ran out of room, now measured (the census, at its first frames that waited for room, b30 frame ~710) [E]: the multipurpose maps were 34 marked, 19 bound, 139 KB unbound; the glow maps 20 marked, 17 bound, 32 KB unbound; every other kind was bound in full. Summed over the run's frames, the multipurpose maps' marks were 295,111 KB unbound of 648,345 KB marked, glow 53,729 KB, level detail 17,524 KB. With the filters: multipurpose 510 KB, glow 1,239 KB, level detail 8,424 KB. Decals (19,542 KB) and culled level runs (40,951 KB) still mark unbound textures, but they're small next to the room.
- What the filters give up [E]: of the keys passed over, those the replay bound anyway are left to its bind, as without the word: 2,855 multipurpose maps, 607 screen quads (at the ceiling) and 430 blip ends over the run. That accounts for b30's 30 against plain's 23.
- Play: with `nomouth` (lip sync kept out of game state), the play hash and both random seeds match words alone at every tick on b30 (6,300) and a10 (9,000), and plain against words matches on b30 too. Without `nomouth`, b30's play split at the looping sounds, as it splits between plain and words.
- Compiles: campaign (`PSP_RECON` too) and `PSP_MULTIPLAYER=1`.

## Risk

- Hot code: the record writers of every textured draw. With the word off it costs one load and one branch per keyed record.
- The texture-cache residency is touched from the record walk, in the middle of the engine's render walk, only on the main CPU and never while a replay is out.
- The filters repeat the replay's own bind tests (`psp_ge_model_multipurpose`). If those tests change, the filters must change with them, or the marks drift back to unbound textures (`txptcensus` shows it).
- Default: on with M6 #2's words, a restoration of plain's look: with them, b30, a10 and c20 draw as many textured frames as plain [E], at about 1.5 ms of drawing a frame [E], and play the same. It takes effect only with ME semantics, so the proposal is that `texpretouch` defaults to on whenever `gemeon` is on, with `notexpretouch` to turn it off. The PSP pair (b30's route, the words with and without it) gives the hardware's untextured frames and cost [M].
