# Build 112 on the PSP-1000 (2026-10-10)

Sheepdog01's PSP-1000 at 333 MHz, b30, `halo_psp_b112me.zip` (EBOOT `62644272…`). Labels: **[M]** measured on the PSP, **[E]** emulator, **[D]** read in the code or the tags, **[U]** inference. The ceiling bench, the Blood Gulch benchrun and Test 3 (`gemeon`) are in `big_levers.md`; this file has the five comparison runs that followed b111's long run (`hardware_b111.md`).

## The five runs

All with `nearauto assettrack hwprofile geo2`, deathless, no USB capture. Times are averages of the heartbeat's `budget:` lines.

| Run | Added | Stretch | Result |
|---|---|---|---|
| 1 | `placeddeadawake` (b112's item 2 alone) | start to the Warthog drop | The scripted flight (game time 150–2,000): 260.6 ms a frame by the clock against b111's 267.8 (**−2.7%**), ticks 124.6 against 127.9 ms a frame. Small and near the run-to-run spread, in the expected direction: the cold asserts and float `fast_ftol` stay |
| 2 | + `gedefer` | the flight | 260.0 against 260.6: **no change**. The replay moves about 38 ms a frame from drawing to the frame's end; recording adds about 2 ms of the GE layer's CPU. The cache model's predicted 3 ms didn't appear. Neutral on its own; it is the step before the ME |
| 3 | `nosizefloor` (test B) | to just after landing | **Worse.** Whole groups of beach objects vanished and came back (Sheepdog01). With the display list full, whatever was submitted last is dropped: 83 KB of model draws a frame against 46 KB with the floor (game time 570–1,200). The floor stays; the list's room is `b113_candidates.md` #19 |
| 4 | `sceneryhalf` (test C) | to the beach | **No measurable gain.** The flight's tick is 63.2 ms in runs 1 and 4; 58 sleeping scenery objects a tick were left unchecked. Off it stays |
| 5 | none (b112's item 1 on: placed-dead bodies sleep) | the whole level (game time 35,447) | See below |

## Run 5: placed-dead bodies sleeping [M]

- **Where they are:** b30's placed-dead bodies are there from the start: 7 a tick for most of the level (19 at the very start). In b111's long run they updated every tick, never at rest (`corpses: 7.0 dead, 0.0 of them garbage, 0.0 at rest`). In run 5 they are at rest and offered to sleep (`7.0 placed dead, 7.0 at rest, 7.0 idle past 2 s`).
- **What it saves:** on the flight, sleeping corpses went from 2.5 a tick (run 1, `placeddeadawake`) to 5.7, and the bipeds' update from 28.35 to 27.67 ms a tick (**−0.7 ms, −2.4%**). About half the offered bodies pass the byte-identical rule; the rest stay awake [U, why].
- **The security room:** this run's room held no placed-dead bodies (2 garbage corpses a tick, as in b111's run), so b108's 7 awake bodies there didn't recur. Nothing to compare in the room itself.
- The heap: 0 requests failed.

**The bipeds' cost stands out:** on the flight about 28 bipeds update every tick at about 1 ms each, 28 ms of a 63 ms tick [M]. That is the AI and the units' own update (`big_levers.md` L3).

## The worst spots, from Sheepdog01's screenshots [M]

Sheepdog01 took screenshots (`shot016`–`shot025`) at the worst moments; the log names each with its time and camera. The images stayed on the stick (the log pull takes `log/` only).

**Game time ≈19,500, on the cliffs in the Warthog (shots 016–017): 2.4–2.8 fps, 230–290 ms of drawing a frame.**

| In the report covering game time 19,303–19,942 | |
|---|---|
| Model drawing | 112 ms a frame (`rasterizer_model_draw`), 8,552 part draws in 30 s |
| Scenery | 350 objects updated a tick (trees and rocks) |
| Choosing models' detail levels | 19.6 ms a frame (`object_get_level_of_detail_pixels`) |
| Pushed out | **4,844 draws skipped with the display list full**; the level's reserve held back 507 |
| Loaded late | geometry: 284 blocks read, 37 frames with a block not yet read; textures: 46 requests waited a frame, 5 frames drew something untextured |

So here it is both, and the list overflow is the larger part: objects drawn too late because a frame reads at most 192 KB of geometry (`psp_geo.c`, `SYNC_FRAME_BUDGET`) and 96 KB of textures (`psp_tex.c`), and objects never drawn because the 512 KB list was full.

**Game time ≈24,300–24,950, the map room's descent (shots 019–024): 8–10 fps.** No list overflow and few models (14 KB of the list a frame). Geometry: 13 frames with a block not yet read; the level copies' arena let 91 copies go to make room and 6 pieces went to the heap. Here **late** loading is all there is [M]; which surfaces is unknown without the images [U].

## Sheepdog01's question: a scope effect when zooming the pistol

**Yes, there should be one** [D, `sierra-tags` 38082579877]. The pistol's HUD interface (`weapons\pistol\pistol`) has a screen effect whose full-screen mask is `ui\hud\bitmaps\pistol\pistol_scope_mask`, plus the scope crosshairs (`ui\hud\bitmaps\sniper\hud_reticles_scope`). On the PSP the game hands screen effects to `rasterizer_screen_effect`, which nothing intercepts, so the mask is never drawn, nor any other weapon's zoom mask (the sniper rifle's) [D]. `b113_candidates.md` #20.

## Next time

- The log pull with the screenshots: `tar czf $HOME/psp/logs/<name>.tgz -C $psp/halo log shots`.
