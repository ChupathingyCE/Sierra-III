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

Diagnosis so far; no code change yet. Read against the PC build's menu at the same view (the hull from below, about 8 s into the menu on the emulator).

- **H15, the flicker [E]:** none in the emulator. Over four frames in a row every 4 s, frame 1 against frame 3 always differs more than 1 against 2 (base: 3.1/3.1/4.1 mean levels, for example). That's the ring turning, not A-B-A flicker. The same holds for `nomodeldetail`, `glowclamp`, `noshowcase` and `showcasebias4`. `noshowcase` moves least (0.5-1.3), because the half-size pack has less fine detail to shimmer. If Sheepdog01 still sees flicker on the PSP, that's a hardware question, for a `keepsets` run on the menu.
- **H2, the hull's colour [E, D]:** a box on the hull's underside averages RGB 11,17,10 on the emulator against 35,36,38 on the PC. It is three times darker and green where the PC's is neutral grey. None of the menu's switches moves it (`nomodeldetail` 10,14,10; `noshowcase` 8,14,8; `glowclamp` 11,16,10), so it isn't the pack's size or the mip level. The outer ring's shader (`scenery\halo\shaders\halo outer ring`, soso) has a base map, a multipurpose map, the detail map and a reflection cube map (`cubemap halo`). The PSP draws all of them except the reflection: the bake drops cube maps (review_ringdetail.md, "Still open"). In space the hull gets almost no light, so the PC's grey panel sheen comes from that reflection, masked by the multipurpose map's red. **Fix to design:** a reflection pass on ui.map only. One option is to add the reflection's average colour (from the cube map, at the shader's reflection brightness), masked per texel by the multipurpose red. The bake would make that one 2-D map, drawn added after the base. That restores the sheen's colour and panel pattern without the view dependence. It's a restoration, so on by default behind a keep word.
- **H16, the starfield [D]:** the sky behind the ring is drawn by `sky\sky_x10\shaders\space`. It is a see-through shader with several maps (the bitmaps `galaxy` 512x128, `space` 128x128, `stars` 256x256 and `star mask` 64x64 are in the pack). The GE draws one map of a see-through shader: the one with the smallest scale (`psp_transparent_map`, map 2 here; the bake kept it, "0 drawn from another map than the scale rule's"). The PC combines the maps: stars under their mask over the nebula. Drawn alone and stretched across the sky, the low-resolution map shows as blocky purple patches with no stars. The mip level isn't the cause, since `noshowcase` looks the same. **Fix to design:** on ui.map only (the showcase rule), draw the sky shader's other maps as extra passes, following its combiner. Stars added over the drawn map is the likely minimum. The map list, scales and combiner come from the tag listing below.

Open: the tag listing for the sky shader (maps, scales, combiner stages) is queued in the lab. With it, the passes can be written and checked against the PC build's menu.

## Results

Emulator batch front1 (the branch's code at its previous head, on the b139a bake; the later commits change only docs and comments) and the multiplayer compile.

- **Multiplayer build:** `PSP_MULTIPLAYER=1` compiles and links (program 5.93 MB, heap 5168 KB, 5 symbols stubbed), build job ok. Nothing was run, as the rules say.
- **The all-levels profile:** the menu's profile list shows "All Levels" (Controls Default, Difficulty Legendary, Current Level The Maw) beside the two empty slots; selecting it opens the Load Level list, whose first page (The Pillar of Autumn, Halo, The Truth and Reconciliation) shows every level unlocked with its difficulty marks. The walk's later steps (the rights through the other seven levels) did not run in front1: the batch's `exitatN` counts game seconds at 30 ticks, the input script's seconds are 60 ticks, so the walk ended at script second 11. The other seven are read from front2's walk, below.
- **Clean runs [E]:** the four menu runs, the three pause-menu runs, the profile walk and the strict menu walk (largest size) all ended normally: no fault, no crash.txt, strict: 0 distinct sites.
- **Cost [E]:** none measurable. The menu runs at 100, 115/125, 115/150 and 108/125 report the same frame CPU time (5.14 ms average at game time 504); the three pause-menu runs the same (9.57 ms average while paused). The zoom changes where the menu's quads land, not how many are drawn. The GE's own time is not in the emulator's figures: the menus are small and no part of play touches them.

### The shots for H11's size

In `/tmp/claude-0/overnight/morning/h11/` (local screenshots, 480 x 272, not committed). All from front1; the menu ones are the profile list at game time 8.5 s, the pause ones are the pause menu at game time 60.3 s on b40.

| File | Shows |
|---|---|
| `front1_menu-100_profilelist.png` | Profile list at the default size (no keep word): small body text, footer glyphs at their old size. |
| `front1_menu-115-125_profilelist.png` | `menuzoom115 menuglyph125`: everything 15% larger; the title sits about 7 px from the top, the footer glyphs larger. |
| `front1_menu-115-150_profilelist.png` | `menuzoom115 menuglyph150`: same page, the footer glyphs at their biggest (150); the footer ends about 4 px above the bottom edge. |
| `front1_menu-108-125_profilelist.png` | `menuzoom108 menuglyph125`: a smaller step, more margin all round. |
| `front1_pause-100_pausemenu.png` | Pause menu at the default size. |
| `front1_pause-115-125_pausemenu.png` | Pause menu at `menuzoom115 menuglyph125`. |
| `front1_pause-115-150_pausemenu.png` | Pause menu at `menuzoom115 menuglyph150`. |
| `front1_profile-walk_loadlevel.png` | The Load Level page with the all-levels profile (levels unlocked, difficulty marks). |

To choose: look at the four profile-list shots for the page, the pause shots for the box with a few lines, and pick the pair; set it in `keep.txt`.

### front2: the keyboard at each size

Emulator batch front2 (the branch's code at `ae223d7`, b139a bake): the four menu runs to script second 12, one strict menu run (115/150) to second 16, and the all-levels walk. All six ended normally: no fault, no crash.txt, strict: 0 distinct sites. A screenshot batch stops at its 24th shot (`shotevery1` is a shot every half script second, so second 12): the keyboard opens at second 12, the last shot of each plain run, and the strict run (`shotevery2`) shows it at seconds 12 to 16.

| File | Shows |
|---|---|
| `front2_menu-100_keyboard.png` | "Enter name" keyboard at the default size: the footer legend ("=BACK =ENTER") has small glyphs and sits at the bottom of the dark band. |
| `front2_menu-108-125_keyboard.png` | `menuzoom108 menuglyph125`: a little larger; the legend glyphs larger; the title and prompt clear the top edge. |
| `front2_menu-115-125_keyboard.png` | `menuzoom115 menuglyph125`: the grid and name box 15% larger; the legend's glyphs larger; the whole page still inside the screen. |
| `front2_menu-115-150_keyboard.png` | `menuzoom115 menuglyph150`: as the last, the legend's glyphs at their biggest; the legend stays inside the screen. |
| `front2_menu-strict-115-150_keyboard_t16.png` | The strict run's keyboard at script second 16 (115/150): the same page after four seconds, nothing moved or cut off. |

Cost [E]: none; the four runs end at the same game time (988) as each other.

### front2: all ten levels open

The walk (two crosses, then nine rights, one shot a script second) steps the Load Level list through every level of the all-levels profile: The Pillar of Autumn, Halo, The Truth and Reconciliation, The Silent Cartographer, Assault on the Control Room, 343 Guilty Spark, The Library, Two Betrayals, Keyes, The Maw. Each has its picture and blurb and three difficulty medals under it; none is greyed or locked. Both timings of the walk end normally, strict 0 sites, game time 1458. (An earlier try pressed a third cross, which chose the first level and opened Choose Difficulty: a script mistake, not the menu.)

| File | Shows |
|---|---|
| `front2_profile-walk_levellist_mid.png` | The list at script second 14, the middle pages: the highlight moves one level at a time and the page scrolls. |
| `front2_profile-walk_levellist_last-page.png` | The last page: Two Betrayals, Keyes, The Maw, the highlight on The Maw, three medals each. |
