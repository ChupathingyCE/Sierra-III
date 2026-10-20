# Review: fix-frontend (b139's front-end findings)

Branch `fix-frontend`, off `w139`. Four changes for b139's hand test: the release's all-levels profile, the menus drawn larger (H11, off by default), the XMB tile's glass frosted (H13), and the main menu's ring (H2, H15, H16). Each section: what changed, its key lines, why, the evidence, and its cost in frame time.

## 1. The all-levels profile in the release

**What:** the campaign build's zip carries a ready-made profile, "All Levels", with every level open at every difficulty, at `halo/save/u/UDATA/1483099185B9/` (`blam.sav`, `SaveMeta.xbx`). The multiplayer build carries none.

**Key lines:**
- `psp/seed/all_levels.c`: the profile as a C initialiser of the engine's own `struct player_profile`: a new profile's values (`player_profile_new`), with each level's flags byte `PSP_SEED_ALL_DIFFICULTIES` (a bit per difficulty), as `game.unlock_all_levels` marks a new profile. Never linked into the game.
- `psp_build.py`, `seed_profile()`: compiles it with `player_profile.c`'s flags, takes the profile's bytes out of the object, and writes the save file the way the engine's save code does: the checksum (SHA-1 over the content signature key and the data), the folder (FNV-1a of the UTF-16 name) and the meta file. Each format fact is read from the engine tree by a pattern, and the build stops if one no longer reads as expected.
- `tools/build_release.sh`: copies `<out>/build/stick/` into the zip.

**Why:** a player shouldn't have to finish the campaign to play any level. The profile has a slot of its own (its folder is named from its name), so unzipping a new build over the stick never touches a player's own profiles; it does replace "All Levels" itself, settings included.

**Built from code, not committed bytes:** a field the engine adds, moves or resizes moves in the seed too, and the checks stop the build when the save format changes under it.

**Evidence:** [D] the bytes checked against the engine's reader (name, colour, flags `0x0F` for each of the ten levels, look sensitivity, checksum). [E] front1's `profile-walk` and `menu-*` runs: see Results.

**Cost:** none in play (a file on the stick).

**Keep word:** none: it's a file. Delete `halo/save/u/UDATA/1483099185B9/` to remove it.

## 2. H11: the menus drawn larger (`menuzoomN`, `menuglyphN`), off by default

**What:** two keep words, both off (100) unless set:
- `menuzoomN` (100 to 115): the whole 640 x 480 menu page drawn N% larger about the screen's centre, while the menus are centred (`ui_centered`). One zoom for the whole page keeps every box where it was against the others, so nothing comes to overlap. 115 is the most that keeps the Xbox's title-safe area (32 of 480 lines from each edge) on the screen: 240 / 208.
- `menuglyphN` (100 to 150): the menus' button glyphs drawn N% larger. A screen key's glyph (its label to its right: the footer keys and the keyboard's legend) grows up and to the left, keeping its right edge by its label. A glyph in a line of text grows up and to the right, no taller than its line's icon box, and the text's cursor moves on by as much (`psp_button_glyph_extra`).

**Key lines:** `psp/src/psp_ge.c`: `keep_percent`, `menu_zoom` in `psp_ge_ui_space` (with `ui_offset_y`, added to `hud_draw`, `glyph_quad` and `text_character`), `psp_ge_menu_glyph_zoom`, `psp_ge_glyph_next_labelled`; `psp/src/psp_sdl.c`: `psp_button_glyph_extra`; `psp_fixups.py` (`FIXES_MENUS`): the footer keys and the keyboard legend call `psp_ge_glyph_next_labelled`.

**Why off:** a change of look, not a restoration: the menus are laid out for a TV, and on the PSP's 4.3 inches the text reads small. The size is a choice to make from the shots.

**The A/B for the pick:** front1, see Results. To choose, set one pair in `keep.txt`, e.g. `menuzoom115 menuglyph125`.

**Cost:** menus only; nothing in play (the zoom applies only while the menus are centred, and the glyph zoom only in the menus).

## 3. H13: the XMB tile's glass frosted

**What:** `psp/xmb/icon0.py`'s `GLASS_BLUR` 0 → 14; both ICON0 tiles redrawn.

**Why:** the tile fakes glass by carrying the patch of the wallpaper (PIC1) under it. Drawn sharp, the patch's planet limb showed on the PSP shifted left of the wallpaper's own: the XMB doesn't put the tile exactly where the photo measured it, and moves it as the entry comes into focus. The EBOOT's ICON0 and PIC1 are the PNGs byte for byte, so the patch was right and only its place was off. Blurred, the patch keeps the scene's colours and no edge to jog.

**Evidence:** [M] the hand test's report; [D] the b139 zip's EBOOT sections compared with the PNGs; [E] the tile rendered over PIC1 at offsets of -8 to +4 px: no jog. `docs/xmb_menu.md` updated.

**Cost:** none (XMB art). No keep word: it's the XMB's art, and the previous tile is in git.

## 4. H2, H15, H16: the main menu's ring

Pending: the ring batch's read against the PC reference.

## Results

Pending: front1 (the menu at four sizes, the pause menu at three, the profile walk, one strict menu walk) and the multiplayer build's compile.
