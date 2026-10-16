# Review: the controls branch (for the next build)

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). The `controls` branch is `w134` plus the renderer session's controls and glyph groundwork, kept apart so the main session can fold or rework it (Sheepdog01, 2026-10-16). The design and the scheme tables are in `docs/game_feature_survey.md` §4 (on the `renderer` branch). Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

## Where it stands (2026-10-16)

- **Branch:** `controls` at d7232b9, on `w134`. The campaign and multiplayer builds compile (lab 38937812305, 38937818323; the multiplayer build of the controls alone: 38937050710).
- **Emulator checks:** `controls1` (crouch, reload and action on d20 under a scripted pad; strict; southpaw) and `glyphs1` (the glyphs on b30 and a10, against `xboxglyphs`). Their results are under "The checks".
- **Not built yet:**
  - `nublearn`: a save during play costs a Memory Stick write, so it waits for a background save like the texture totals'.
  - The prompt wording at bake.
  - The string survey (§4.7 of the survey).
- **Nothing here touches the simulation's rules.** The engine's own port paths (the Linux keyboard's) carry the crouch and the reload/action split. The multiplayer build's server sees the same control bits a desktop player's keyboard sends.

| Change | What | Kind | Default | Words | Review |
|---|---|---|---|---|---|
| crouch toggle | Select + ↓ toggles crouch, which holds at any speed | control change (Sheepdog01's decision) | on | `crouchhold` (off) | §1 |
| reload tap, action hold | D-pad ← tapped reloads; held 250 ms it acts and never reloads | control change (Sheepdog01's decision) | on | `actiontap` (off), `actionholdN` (ms) | §2 |
| nub calibration | The nub's rest, dead radius and reach mapped onto the engine's dead zone and full scale; the launch menu's NUB page measures them | control change, opt-in | off (no `nub.txt`) | `ms0:/halo/nub.txt`, `nubdeadN`, `nubreachN` | §3 |
| profile by keep words | The base game's button preset, stick preset, look sensitivity and invert, until its menu loads | control option | off | `buttonpresetN`, `stickpresetN`, `looksensN`, `lookinvert` | §4 |
| counts | The heartbeat's pad line carries the nub's range; a line counts toggles, taps, holds and what the engine took | instrument | always (log lines) | — | §5 |
| PSP button glyphs | The game's button icons drawn as the PSP's buttons, in play and in the menus | look change (Sheepdog01's direction; his A/B decides the default) | on | `xboxglyphs` (off) | §6 |

## The checks

(Filled in from `controls1` and `glyphs1`.)

## §1 Crouch, toggled (`psp/src/psp_sdl.c`: `controls_update`, `psp_input_port_crouch`; `psp_fixups.py`: `FIXES_CONTROLS[...input_abstraction.c#controls]`)

- **Why:** `player_control.c` lets a gamepad's crouch through only while the move stick is under 98% of its travel. That's the Xbox's own rule, so it isn't an upstream bug. The PSP's face buttons always move at full travel, so crouch only worked standing still.
- **How:** Select + ↓ flips `crouch_toggled`. `input_abstraction_port_crouch` (the keyboard's path, which Halo PC crouches through at any speed) now returns it for player 0. The left-stick-click bit reads the toggle too, so anything else that reads it agrees.
- **Off:** `crouchhold` gives back the held crouch under the engine's rule.

## §2 Reload on a tap, action on a hold (`psp_sdl.c`: `controls_update`, `psp_input_port_reload`, `psp_input_port_action_only`; the same fixup)

- **Why (Sheepdog01):** "enough that a reflexive reload doesn't grab a stray weapon off the ground or throw you out of a vehicle". The engine acts on X's tap (a vehicle exit is a tap) and reloads only when there's nothing to act on.
- **How:**
  - **Released before `action_hold_us`** (250 ms): `reload_pending` is set. The engine's reload control takes it once.
  - **Held past it:** `action_acting` comes on. The X bit the engine reads is that, not the raw button, so it sees a press starting 250 ms late. The action-only control (`UNIT_CONTROL_PORT_ACTION_ONLY_BIT`) stops the reload fallback.
  - **Picking up a weapon** still takes the engine's own swap hold (`minimum_weapon_swap_ticks`) on top, so ~0.25 s plus that.
- **Off:** `actiontap` gives back X as the engine reads it. `actionholdN` sets the threshold in ms, until it's settled by hand.

## §3 The nub, calibrated (`psp_sdl.c`: `nub_calibrate_load`, `nub_calibrated_axes`)

- **Why:** Halo has its own response curve, dead zone (9,000 of 32,767 an axis, `input_xbox.c`) and acceleration when pegged. Every PSP's nub rests and reaches differently, so a fixed mapping can leave the top of the range short of pegging, or the bottom inside the engine's dead zone.
- **How:**
  - A round dead radius around the measured rest point.
  - Then each direction's reach (an ellipse) is mapped so that leaving the dead radius lands just past 9,000 and the reach lands on 32,767.
  - The engine's per-axis rescale then sees a straight line from 0 to full.
- **Where it comes from:** `ms0:/halo/nub.txt` (`rest X Y`, `dead D`, `reach L R U D`, in the PSP's 0–255 units), or `nubdeadN` / `nubreachN` in percent.
- **The launch menu's NUB page** (`psp_menu.c`: `nub_page`, `nub_work_out`, `nub_draw`; R past PRESETS) writes the file:
  1. Let go and press ✕ after a second: the rest samples.
  2. Roll the nub round its edge, then ✕: the furthest each way. It refuses until both axes have travelled 120 of 255.
  3. Let go again, then ✕: where the nub comes back to.
  4. It shows the result; ✕ saves, ○ leaves the file as it was. ○ on the first screen removes `nub.txt`.
- **The values:**
  - The rest point is the mean of both let-go steps.
  - The dead radius is the furthest any let-go sample sat from it, plus 4, held between 8 and 40. So neither jitter nor where a worn nub settles moves the player.
  - Each reach is the furthest seen less 2, so full scale is reached without pressing as hard as the measuring did.
- **Testing it:** the menu isn't shown in scripted runs (the emulator, the rig), so the page needs Sheepdog01's hands: one pass on each PSP, then the `nub.txt` it wrote and a few minutes of play.
- **Off:** with neither, the mapping is the old one, so the lab's sticks and input scripts play exactly as before.
- **To choose values:** the heartbeat's pad line now carries the furthest the nub has gone each way (§5).

## §4 The profile from keep words (`psp_sdl.c`: `psp_input_profile_words`, `psp_input_profile_overrides`; `FIXES_CONTROLS[...player_ui.c#controls]`)

- **Why:** the base game's profile settings (five button presets, four stick presets, sensitivity 1–10, invert) are the schemes Sheepdog01 wants. Halo's front end, where they're set, doesn't load yet (survey §2).
- **How:**
  - At `player_ui_initialize`, if any of the words is present, `set_local_player_controls_from_player_profile(0)` runs as the menu would.
  - The words override the default profile's fields first.
- **Off:** without the words nothing runs, and the controls are the engine's defaults as before.

## §5 Counts (`psp/src/psp_platform.c`)

- The keep-word line names the controls' words.
- The heartbeat's pad line adds the nub's range seen: left, right, up, down.
- A "controls (since launch)" line counts:
  - crouch toggles;
  - reload taps, and the reloads the engine took;
  - action holds, and the actions the engine took.

  A tap counted but not taken, or the other way round, would show a wiring fault.

## §6 PSP button glyphs (`tools/psp_glyphs.py`; `psp/src/psp_glyphs.h`; `psp_sdl.c`: `psp_button_glyph`, `psp_button_glyph_extra`; `psp_ge.c`: `psp_ge_glyph_next`, `glyph_quad`; `FIXES_CONTROLS[...hud_messaging.c#glyphs]`, `[...ui_widget.c#glyphs]`)

- **Why (Sheepdog01):** "so we can have the UI reflect and ground the fact that this isn't an xbox".
- **The sheet:**
  - `tools/psp_glyphs.py` draws 18 glyphs: △ ○ ✕ □, the D-pad (each arm and whole), L, R, the nub, the face-button diamond (moving), START, SELECT, and the chords SELECT + ↑ / ↓ / L (each button at its own size, never scaled, with a plus between: Sheepdog01).
  - Each glyph has the shape of the PSP-1000's own button (Sheepdog01, 2026-10-16):
    - the face buttons are round buttons with their outlined symbol;
    - each D-pad glyph is that one leaf of the pad (after a PSP-3000 photo: a slightly bowed outer edge with rounded corners, long straight sides, then a short taper to a rounded point at the pad's middle), with its arrow: the face buttons' triangle at about 60% of its size, outlined with their stroke, centred on the leaf; the whole pad spaces its four leaves as the PSP does, and the face-button cluster takes the same spacing;
    - L and R are the PSP's shoulder tab, after a trace of a PSP-3000 photo and shaped with Sheepdog01: flat top and bottom; both sides straight slants at 55°, the outer one curving broadly into the top and the inner one into the bottom; the two remaining points softened a little. Every corner is rounded with a curve whose bend rises and falls smoothly (a "continuous" corner), so none kinks where it meets a straight edge. They take two cells (R is L mirrored);
    - the nub is a disc with a dense cross-hatch, as dense as still shows at 32 px, named "NUB" above it as the Xbox's stick icons are named ("RIGHT");
    - START and SELECT are the PSP-1000's flat-topped bowl keys (Sheepdog01's sketch), each with its name.
  - All of it is drawn by the script; only the letters are a font (Overpass, OFL).
  - **Glass, in the UI's colour:**
    - Each texel is a coverage and a shade (4 bits each, one byte: 32 KB, the GE's 8-bit indexed format).
    - At draw, `glyph_palette` turns the shade into colour from the colour the game hands the icon, which is its text colour. Shade 0 is that colour at 15%, 0.7 the colour itself, and above it the colour lightens to white.
    - So the body is a dome of the colour, lighter in the middle and darker toward the edges, half see-through, with a bright rim; the symbol is near white and solid. Sheepdog01's picks from the tool's options (`--finish`, `--spread`, `--rim`, `--arrow`): the dome finish, a rim 1.75 times the first one's width, and the D-pad arrows' stroke at three quarters of the face buttons'.
  - `psp/glyphs/psp_glyphs.png` is a preview, in the HUD's blue and in white.
  - **Resolution:** the shapes are geometry, drawn at 8× and reduced. Rims, edges and rounded corners come from each pixel's straight-line distance to the shape's edge (scipy's distance transform), so they're the same width at every angle; square grow and shrink filters left the slants blotchy (Sheepdog01).
    - The game carries 32-px cells; the HUD draws glyphs about 18 px high [U: from the mock; `glyphs2`'s shots will say].
    - A larger sheet would be worse, not better: the GE would shrink it more than twice over with no mipmaps, which shimmers, and it would take 4× the memory (128 KB at 64-px cells).
    - `psp/glyphs/psp_glyphs_master.png` is a 128-px-cell copy at full precision (`--master`), for looking at and for other uses.
- **The map:**
  - `glyphs_in_play` and `glyphs_in_menus` take the engine's 18 icons (A … right stick) to glyphs by survey table A. In play, A is D-pad ↑; in the menus, ✕.
  - The engine has already turned each prompt's control into an Xbox button through the profile's preset, so the glyphs follow the presets with no more work.
- **The draw:**
  - Both icon sites know the icon's index but their `render_state_bitmap` doesn't, so the caller marks it (`psp_icon_drawing`).
  - Just before `hud_draw_bitmap_direct`, `render_state_bitmap` tells `psp_ge.c` the glyph (`psp_ge_glyph_next`) and clears it after.
  - `psp_ge_screen_quad` swaps the next quad's key for a glyph key (0x100 + glyph, below any bitmap's address). The key then passes through the defer queue untouched.
  - `screen_quad` sends that key to `glyph_quad`, which:
    - draws the cell at its share of the quad's height (80%, Sheepdog01; the nub, named like the Xbox's stick icons, the whole height), on the quad's bottom, in the glyph's own shape;
    - centres it when it's narrower, or starts at the quad's left edge;
    - uses the quad's place and the HUD's zoom;
    - takes its palette from the quad's colour (1 KB of the display list a glyph, written back by `list_take`);
    - blends as glass, over what's behind, whatever the icon's own blend: added, as most HUD icons are, the dark body would vanish;
    - binds the sheet with the ordinary `sceGu` calls, which clear `psp_tex`'s bind memo.
  - **The colour:** both icon sites hand a glyph the text's colour, not the Xbox button's own (A green, B red …).
  - A wide glyph moves the text's cursor on by the extra width (`psp_button_glyph_extra`, the same rule in game units).
- **Cost:** one 2-vertex sprite per icon, as before, plus a 256-entry palette worked out (a few µs [U]) and a CLUT and texture bind (~10 words). The sheet is the EBOOT's own data, never written, so there's no cache writeback.
- **Off:** `xboxglyphs` draws the Xbox's icons from the map, as before.
- **Open:**
  - Prompts whose meaning changed with §2 ("Press X to enter") need their wording changed at bake.
  - **The crouch and zoom icons are crossed in the original's own data** [D]:
    - `button_mappings` in both `hud_messaging.c` and `ui_widget.c` takes the `crouch` token to game control 11 and `zoom` to 10.
    - `input_abstraction.c` binds 10 to the left-stick click (crouch) and 11 to the right-stick click (zoom).
    - The decomp matches the binary, and the token names list `crouch` before `zoom`, so on the Xbox too a `crouch` prompt would show the right-stick click.
    - It matters only if a map's strings use those two tokens [U]; the string survey will say. The glyphs draw what the engine picks, so they inherit it. Correcting it would be a departure from the original's data, so it's Sheepdog01's call.
  - The multiplayer session's ONLINE GAMES prompts should use this sheet and map, so there's one set for both builds.

## Credits

- Overpass (Red Hat, OFL): the letters on the L, R, START and SELECT glyphs, from ChupathingyCE's assets. It's in the README's Credits.
- The glyphs' look was inspired by ChupathingyCE's button glyphs (Kenney's Input Prompts). No art of theirs is used since the redesign.
