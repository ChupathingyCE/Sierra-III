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
- **The log (`frontdoor_test1.tgz`) agrees with Sheepdog01's t53** [M]: the check starts at game time 0; it finds the saved game `sheepPSP` at tick ~1,095 (36 s) and reads the profile ("checksum failed on persistent storage") at tick ~1,683 (56 s), after which the pad's presses land. The log's lines carry no clock, so `frontdoor3` times the check's parts (`front door: saved-game check: …` lines: the free-space query, the saved games' scan, the profiles' reads, the total).
- **The menu into the campaign worked on the PSP** [M]: a10 loaded from the menu and played. On the way, the same lines the renderer session's emulator run saw: "memory check failed at scenario_load" (32 KB), the profile's "checksum failed on persistent storage", and "XCreateSaveGame() failed … (maybe the name is already in use?)" at the profile write. For step 2.
- The menu's look is `w135`'s: the renderer session's fixes are on `glowmaps`, not on this branch.

## 3. The menu's dead first minute: `playlist_profile.c#variants`, and the timers

frontdoor3's timers on the PSP-1000 [M]: the free-space query 1,423 ms, the saved games' scan 743 ms, **the profiles' reads 54,195 ms**, 56,362 in all (Sheepdog01: control at t53). So not the free-space query (my first suspect).

**Why [D]:** the profiles' first call writes the 26 default multiplayer game variants to disk on every start-up (`playlist_profile_create_default_profiles_on_disk`; its `first_time` flag starts TRUE each boot). For each it empties `z:\saved\playlists\default_playlist\NN`, then creates, writes and closes `blam.lst`. On the Xbox, `z:` is the hard drive's scratch partition; on the PSP it's `ms0:/halo/save/z/` on the Memory Stick, so that's 26 folder resets and file writes every boot.

**The change:**
```c
if (!psp_keep("variantswrite") && file_exists(&file) && file_open(&file, read) &&
	file_read(&file, sizeof(existing), existing) &&
	!csmemcmp(existing, block, PLAYLIST_PROFILE_CHECKSUM_DATA_SIZE + sizeof(XCALCSIG_SIGNATURE)))
	file_written = TRUE;   /* the stick already holds it: nothing written */
```
- The block is built and signed first, then compared with the file on the stick. Only a mismatch (or no file) empties the folder and writes, as before.
- **Why only the first 124 bytes are compared:** they're the variant and its signature, the only bytes the game reads back. The rest of the 512-byte block is whatever the stack held, different every boot, so comparing it would never match.
- An exact speed-up: the files the game reads are the same bytes either way. `variantswrite` writes them every boot, as before.
- A line logs the count and the time (`front door: the default game variants: N already on the stick, M written`). A fresh stick writes all 26 once.

## Builds

Campaign and multiplayer (`PSP_MULTIPLAYER=1`) both build and link (local pspdev 15.2.0, `tools/build_release.sh`).

## For the multiplayer session

`mp135`'s `#ifdef PSP_MULTIPLAYER` check for `menu.txt` in `psp_menu()` becomes redundant on rebase: this does the same for both builds. Drop it when merging.

## Tests (Sheepdog01, the PSP-1000)

1. Start the game: the main menu (HALO title, the ring, music).
2. Quit, start again holding L until the text screen appears: the launch menu, open on its maps page (not a page back, on presets).
3. Put an empty `menu.txt` in `ms0:/halo`: the launch menu without holding L. Pick a map, START: that level loads.
