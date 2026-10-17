# Plan: the front end (start-up, main menu, loading screen)

Sheepdog01, 2026-10-16: restore the main menu and the start-up sequence, and bring in the loading screen between levels. A flag must skip the main menu and boot straight to the launch (debug) menu, for testing.

Labels: [M] PSP, [E] emulator, [D] code, [U] inference.

## 1. Where it stands

**On the Xbox** [D]: the intro movie → the main menu (`ui.map`: the ring flythrough, the music) → after idling, the attract movies → Campaign → difficulty and level → the loading screen with the chant → the level … The Maw → the credits movie → the main menu.

**On the PSP today:** the launch menu (`psp_menu.c`, text on the debug screen) → "Loading …" → straight into the chosen level.
- The main menu already runs when `init.txt` names no map: the Xbox's picture, 30 fps at ~3 ms a frame (`overnight_b108.md` §8, `chupa_rebase.md`) [E].
- The multiplayer build already boots to it (`mp135`: the launch menu only with `ms0:/halomp/menu.txt`).
- Bink is absent, so the intro, attract and credits movies are skipped (FIXES_108 `bink_playback.c#psp`).
- The loading screen never starts: the Xbox ran it only while copying a map to its hard drive (`plan_loading_screen.md` §1).

## 2. The work, in order

| # | Area | Size | What it takes |
|---|---|---|---|
| 1 | **The front door** | small | Boot to the main menu by default; the launch menu with L held at start-up or `ms0:/halo/menu.txt`; scripted runs unchanged (§3) |
| 2 | **The main menu's flow** | testing, then fixes | Menu input; Campaign → difficulty → level start; resuming a saved game; settings; pause → quit to the main menu; The Maw → credits → the main menu. The menus' button prompts become the PSP's (§4) |
| 3 | **Level to level** | testing | Map changes work (the ladder chains ten), but a level's natural end with the next map on the stick has never run on the PSP: every b30 run to its end lacked b40 (`hardware_b111.md`) [D] |
| 4 | **The loading screen** | medium to large | `plan_loading_screen.md`: progress from a chunked map read (a screen frame drawn between chunks); the clouds rebuilt on the GE (its noise and mask textures are made in code, its background is the last frame: no tags needed [D]); the chant's four stems from the disc at bake time; the hold and settle; the frame lock reset; a multiplayer variant |
| 5 | **The menu's look** | renderer session | The near-plane cuts, the black below them, the white band (`overnight_b108.md` §8); LK19 (the ring) and LK20 (the menu at full quality). The menu uses ~3 of its 33 ms |
| 6 | **The movies** | out for now (Sheepdog01) | Intro, attract, credits stay skipped, as today. For later: converted from the player's disc at bake time, plus a player: the PSP's video decoding runs on the Media Engine through Sony's firmware, which may clash with our own ME core [U]; a software decoder on the main CPU, idle in menus, is the other route [U] |

**Why this order:** every hand test needs to reach the menu, so the front door comes first. The loading screen wraps level changes, so those are proven before it.

## 3. The front door: how the PSP chooses its first screen

| Condition at start-up | First screen | `init.txt` |
|---|---|---|
| `input.txt` or `nomenu.txt` in `ms0:/halo` (the rigs, the emulator) | as the file says | run as it is (unchanged) |
| `menu.txt` in `ms0:/halo`, or L held as the game starts | the launch menu | written by the launch menu, then run |
| otherwise | **the main menu** | run without its `map_name` lines |

- `menu.txt` is the multiplayer build's switch already (`ms0:/halomp/menu.txt`), so both builds share one name.
- Only the `map_name` lines are skipped, so other commands in `init.txt` still run, and the launch menu still remembers the last map it chose.
- The choice is logged at start-up (`front door: …`), so a log says which route ran.

## 4. Decisions

- **The movies:** left out for now (Sheepdog01, 2026-10-16); they stay skipped.
- **The launch menu:** behind L held at start-up, and `menu.txt` for a stick that should always open it (Sheepdog01, 2026-10-16).
- **Button prompts: the PSP's, no A/B** (Sheepdog01, 2026-10-16). The glyphs are built: the `controls` branch (on `w134`, the renderer session's groundwork) carries Sheepdog01's own PSP glyph sheet in the EBOOT (`psp/glyphs/`, `tools/psp_glyphs.py`), drawn over the game's button icons in play and in the menus, `xboxglyphs` to turn it off (`review_controls.md` §6). The front end takes them by folding that branch in, not by new work.

## 5. Who touches what

**Already done elsewhere** (2026-10-17), so not repeated here:
- **The menu's look** (step 5): the renderer session's `glowmaps` branch (on `w135`, `docs/main_menu_restoration.md`): glow maps, the menu as the hardware's showcase (`ui_showcase.ptex`, `noshowcase` off).
- **The main menu into the campaign** (part of step 2): checked there in the emulator (lab 39000246258): profile, difficulty, a10 plays. Open from it: a red engine line on the profile screen ("event handler new game if no plyr profiles failed") and a silent memory check at a10's load.
- **The glyphs:** the `controls` branch (§4).


- The menus are shared with the multiplayer build: front-end changes compile into it (`PSP_MULTIPLAYER=1`) and are announced to the multiplayer session before they land.
- Drawing (`psp_ge.c`, the loading screen's picture, the menu's look) is the renderer session's.
