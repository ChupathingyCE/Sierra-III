# Review: the front door (branch `frontdoor`, on b135)

Step 1 of `plan_front_end.md`: the game starts on its own main menu, as on the Xbox, and the launch menu becomes the way in for tests. Two changes.

## 1. `psp/src/psp_menu.c`: choosing the first screen

```c
last = buttons_held();
if (last & PSP_CTRL_LTRIGGER)              -> the launch menu
else if (menu.txt or menudump.txt)         -> the launch menu
else { front_door_main_menu = 1; return; } -> the main menu
```

- **Scripted runs come first and are unchanged:** `input.txt` or `nomenu.txt` still skip everything and run `init.txt` as it is. Every rig and emulator run writes `input.txt` (`tools/emu.sh`), so the lab sees no difference.
- **`menu.txt`** is Sheepdog01's test flag: a stick that should always open the launch menu. It's the name the multiplayer build already uses (`ms0:/halomp/menu.txt` on `mp135`), so both builds share it. `menudump.txt` (the emulator's picture of the launch menu) opened the menu by default before, so it still does.
- **L held at start-up** opens it on any stick without editing files.
- **Why `last = buttons_held()`:** the menu acts on buttons newly pressed since the last read. Starting from zero, an L held to open the menu would also count as its first press, flipping the menu a page back.
- Each route logs one `front door:` line, so a log says which ran.
- **The options on a main-menu boot** (Sheepdog01's test 1: none passed along): `keep.txt` holds the launch menu's last choices, and the main menu plays with them. With no `keep.txt` (a fresh or cleared stick), it writes the first preset's words into it, the build "as it ships" (`nearauto assettrack geo2` for b135), and logs which. Without that, a main-menu boot on a fresh stick had none of the words the build ships with: those live only in the first preset, which only the launch menu applied.

## 2. `psp_fixups.py` FIXES_FRONT: `console.c#frontdoor`

```c
if (psp_front_door_main_menu() && !strncmp(buffer, "map_name", 8))
	continue;   /* logged: "init: … (skipped: the game starts on its main menu)" */
```

- `init.txt` is where the launch menu saves its map, and the engine runs every line at start-up (`console_startup`). Without this, a main-menu boot would go straight into the last map chosen.
- Only `map_name` lines are skipped, so other commands still run and the launch menu still opens on its last map.

## Found in test 1 (Sheepdog01, PSP-1000, 2026-10-17)

- The main menu shows and plays its music.
- **No menu input for about the first minute, then it works.** Why it waits [D]: the menu takes no input until its saved-game check thread ends (`perform_filesystem_initialization`, `ui_widget.c`: `ui_widgets_inhibit_processing` until the thread exits). On the Xbox that check runs under the intro movie, which can't be skipped while it runs, so nobody waits on a bare menu. The PSP has no movie, so the wait shows. Suspect [U]: the check's free-space query (`GetDiskFreeSpaceEx`, answered by the Memory Stick's free-space `sceIoDevctl`, FIXES_23 `posix_files.c`), which counts the stick's free clusters and is known to be slow on large sticks. The log's timestamps around "begining filesystem checks" settle it.
- The menu's look is `w135`'s: the renderer session's fixes are on `glowmaps`, not on this branch.

## Builds

Campaign and multiplayer (`PSP_MULTIPLAYER=1`) both build and link (local pspdev 15.2.0, `tools/build_release.sh`).

## For the multiplayer session

`mp135`'s `#ifdef PSP_MULTIPLAYER` check for `menu.txt` in `psp_menu()` becomes redundant on rebase: this does the same for both builds. Drop it when merging.

## Tests (Sheepdog01, the PSP-1000)

1. Start the game: the main menu (HALO title, the ring, music).
2. Quit, start again holding L until the text screen appears: the launch menu, open on its maps page (not a page back, on presets).
3. Put an empty `menu.txt` in `ms0:/halo`: the launch menu without holding L. Pick a map, START: that level loads.
