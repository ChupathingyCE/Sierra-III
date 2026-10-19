# Playing Sierra-III: from a stock PSP to a playable game

This guide takes you from a PSP and your own Halo: Combat Evolved disc for the original Xbox to the game running on the PSP. Follow the steps in order. Each step says what you need, what to type, and how to tell it worked (the **Check:** lines).

Sierra-III contains nothing from the game. You bake the maps yourself, from your own disc, on your own PC. Never download game files, and never share the ones you bake.

These steps are for **release b139**, the first public release. It is the campaign only: multiplayer isn't part of it (step 7).

**Commands** are for a terminal on Linux or macOS (bash, zsh or dash all work). On Windows, each step says what to do in File Explorer or the Command Prompt instead. Three names stand for paths on your machine:
- `$STICK`: where your PSP's Memory Stick is mounted (step 1, "Finding `$STICK`", shows how to find it).
- `~/Downloads`: where you saved the release's files.
- `$BAKE`: the bake bundle's folder, once unpacked (step 4): `~/Downloads/halo-psp-bake-linux-b139`, `-macos-b139` or `-windows-b139`.

## 1. What you need

1. **A PSP.** The port is built for the PSP-1000 and tested on it; the PSP-2000 and PSP-3000 work too.
2. **Custom firmware:** official firmware 6.61 with ARK-4 (step 2). Sierra-III is built and tested on that.
3. **A Memory Stick** (or an adapter) with about 3.5 GB free: the bake of all 24 maps is about 2.9 GB, and the rest is room for the game and its logs.
4. **Your Halo disc** for the original Xbox, **NTSC, version 01.10.12.2276**, as a disc image (step 3).
5. **A PC to bake on**, any one of:
   - Windows 10 or 11 (x86-64);
   - macOS 11 or later (Apple silicon or Intel);
   - Linux (x86-64) with glibc 2.28 or later: any distribution from 2018 on.

   It needs **Python 3.9 to 3.14**; `bake/README.md` ("What you need") says how to install it on each system.
6. **About 8 GB free on the PC** while the bake runs. The result is about 2.9 GB; the rest is the bake's working files, which it deletes as it goes. <!-- TODO(master): the 8 GB is an estimate (the disc's maps, up to four maps' working files, the result); measure the peak on b139's full bake -->
7. **A USB cable** for the PSP, or a card reader for the Memory Stick.

**Finding `$STICK`.** On the PSP, open **Settings → USB Connection** and plug the cable in. Most Linux desktops mount the stick by themselves. Then:

```sh
lsblk -o NAME,SIZE,LABEL,MOUNTPOINT      # find the stick by its size; its MOUNTPOINT is the path
STICK=/run/media/$USER/<label>           # or whatever MOUNTPOINT showed; set it again in each new terminal
ls "$STICK"                              # a stick in use shows PSP/ at least
```

On **macOS** the stick shows in Finder and on the desktop; in Terminal it is `/Volumes/<label>`, so `STICK=/Volumes/<label>`. On **Windows** it shows in File Explorer as a drive of its own (`E:`, say).

**When you're done with the stick**, every time: on Linux, `sync`, then `umount "$STICK"` (or eject it from the desktop); on macOS, eject it in Finder; on Windows, "Safely Remove Hardware" or eject it in File Explorer. Then leave USB mode on the PSP.

**Check:** `ls "$STICK"` lists `PSP` (on Windows, the drive holds a `PSP` folder).

## 2. Custom firmware (ARK-4)

The PSP runs homebrew only on custom firmware. We don't write a CFW guide: follow ARK-4's own.

1. Install ARK-4 by **[its own instructions](https://github.com/PSP-Archive/ARK-4)**. They say which official firmware it needs and how to install it on your model; Sierra-III is tested on 6.61.
2. **Settings that matter for Sierra-III: none.** The game sets the PSP's standard clock (333 MHz) by itself; it never needs an overclock. Leave ARK-4's settings as its guide sets them.
3. From now on, the stick holds ARK-4's own files. **Never delete them, and never format the stick** (step 5 has the rules).

**Check:** the PSP boots to its home menu (the XMB) with ARK-4 running, as ARK-4's guide describes. The real test comes in step 6, when the game shows up in the game list.

## 3. Imaging your disc

The bake reads your disc's files out of an image you made. It takes any of these:
- **A bare game-partition image** ("XISO"): what extract-xiso and most Xbox dumping tools make.
- **A full Redump-style image** of the disc.
- **A folder** with the disc's files. Point the bake at the folder that holds `default.xbe` and `maps/`, so it finds both (with only the maps, the loading screen is silent: step 4).

1. **Make the image from your own disc.** An ordinary PC DVD drive can't read an Xbox disc; Redump's guides describe ways to dump one. Don't download an image.
2. **Check the image can be read.** The bake bundle (step 4) carries a small reader, `xiso.py`. Once you have the bundle unpacked:

   ```sh
   python3 -I "$BAKE/xiso.py" ~/Halo.iso | grep 'maps/'
   ```

   On Windows, in a Command Prompt: `py -I %USERPROFILE%\Downloads\halo-psp-bake-windows-b139\xiso.py C:\Users\you\Halo.iso | findstr maps/`. It lists the image's files, here only the maps, with their sizes.
3. **Check the revision.** The bake checks every map's SHA-256 against the retail NTSC 01.10.12.2276 maps (`hashes.json` in the bundle). The quickest test is a bake of the menu map alone, into a scratch folder:

   ```sh
   "$BAKE/halo-psp-bake" ~/Halo.iso ~/halo-psp-test --maps ui
   ```

   On Windows: `halo-psp-bake.cmd C:\Users\you\Halo.iso C:\halo-psp-test --maps ui`, in the bundle's folder. This also sets the bake up (its first run, step 4), so it is not wasted time. Delete the test folder afterwards.

**Check:**
- step 2 lists the maps, `maps/ui.map` among them (an image the reader can't open says `not an Xbox disc image`);
- step 3 ends with `baked 1 of 1 maps`. If it says `ui: not the retail map this build expects`, the image is another version, region or a bad dump (step 4, "Hash mismatch").

## 4. Baking the maps

*Placeholder: this step is being written for b139's bake bundle. Until it is, `bake/README.md` describes the bake.*

<!-- TODO(easybake): this step (go_live 2.4) is being written with the bundle's new double-click flow; nothing here until it lands. It covers:
- download: the bundle for your system from the release (halo-psp-bake-<linux|macos|windows>-b139, .tar.gz or .zip, each with its .sha256), check it, unpack it;
- the first run (the Python environment, the internet once), macOS's quarantine and Windows' SmartScreen (bake/README.md);
- how long: about an hour for all 24 maps on an 8-core laptop;
- disk: about 2.9 GB of result (step 1 has the free space);
- the last lines when it worked (`baked 24 of 24 maps`, the chant read from default.xbe), "Hash mismatch", other failures and the per-map logs, a silent chant;
- that a release says when its bundle changed and maps must be baked again (b139 does: bake_changes.md);
- that the output is game data made from your disc: keep it on your own machines.
Step 5 expects the output folder at ~/halo-psp-out (C:\halo-psp-out on Windows; its halo/ folder holds chant.adp and maps/). Steps 3, 6 and 8 point to this step's "Hash mismatch" and "The chant" by those names. -->

## 5. Installing on the Memory Stick

You copy two things: the **build zip** (the game, from the release page) and the baked **`halo/` folder** (the maps and the chant).

1. Mount the stick (step 1) and set `STICK`.
2. **Unzip the build** at the stick's root. The build zip, `halo_psp_b139.zip`, is on the same release page as the bake bundle.

   ```sh
   unzip -o ~/Downloads/halo_psp_b139.zip -d "$STICK"
   ```

3. **Copy the bake** to the stick's root (only `halo/`, not the whole output folder):

   ```sh
   cp -R ~/halo-psp-out/halo "$STICK/"
   ```

   It's large, so it takes a while. **When you bake again for a new release,** delete the old maps first, so no stale pack is left behind: `rm -rf "$STICK/halo/maps"`, then copy.
4. Unmount the stick (step 1, "When you're done").

**On Windows,** in File Explorer: open the zip, choose "Extract all" into a folder, and drag what it holds (the `PSP` folder, and a `halo` folder when the build has one) onto the stick's drive; then drag the bake's `halo` folder (in `C:\halo-psp-out`) onto the stick too. Explorer merges them into the folders already there; answer "Replace the files" if it asks. **On macOS,** use the Terminal commands above, not Finder: Finder can replace the stick's whole `PSP` folder, ARK-4's files with it.

**The stick, once installed:**

| Path | What | From |
|---|---|---|
| `PSP/GAME/halo/` | the game: `EBOOT.PBP`, `halo_crash.prx`, `halo_profile.prx`, `LICENSE-mecore.md` | the build zip |
| `halo/presets.txt` | the launch menu's presets (when the build has one) | the build zip |
| `halo/maps/` | each map's `.map` and its packs (`.ptex`, `_track.ptex`, `.pgeo`, `.pstn`) | your bake |
| `halo/chant.adp` | the loading screen's chant | your bake |
| `halo/log/`, `halo/shots/`, `halo/save/`, `halo/crash.txt`, `halo/stderr.txt` | what the game writes | the game |
| `halo/keep.txt`, `halo/init.txt` | the launch menu's choices (step 6) | the launch menu |

**The stick rules:**
- Delete only inside `halo/` and `PSP/GAME/halo/`.
- **Leave ARK-4's files alone**, wherever they are (`SEPLUGINS/` and the files ARK-4's guide put on the stick), and `overconfig.txt` if your stick has one.
- **Never format or wipe the stick.**

**Check:** before you unmount,

```sh
unzip -p ~/Downloads/halo_psp_b139.zip PSP/GAME/halo/EBOOT.PBP | cmp - "$STICK/PSP/GAME/halo/EBOOT.PBP" && echo "the build is on the stick"
ls "$STICK/halo/chant.adp" "$STICK/halo/maps/ui.map" "$STICK/halo/maps/a10.ptex"
```

prints `the build is on the stick` and lists the three files. On Windows, check that the stick holds `PSP\GAME\halo\EBOOT.PBP`, `halo\chant.adp` and `halo\maps\ui.map`.

## 6. First launch

1. On the PSP, open **Game → Memory Stick** and start **Sierra-III: Halo CE port** (the game's name in the list, beside the Sierra-III tile). <!-- TODO(master): if the release sets PSP_TITLE, put its name here -->
2. **The start-up.** The game shows a loading screen with a thin progress bar while it starts, then its menus. For the first 3 seconds the screen also says `Hold L as the game starts for the development menu` (the launch menu, below). <!-- TODO(master): the time from the XMB to the main menu on b139 (its hand test) --> The first launch of a new build takes a few seconds longer (the saved-game check).
3. **The main menu** is Halo's own, with the ring behind it. It answers once its textures are in. In Halo's menus the buttons are the pad's: cross is A, circle B, square X, triangle Y; the d-pad and the nub move.
4. **Starting a level.** Pick **Campaign** and a level. While it loads you hear the **chant**, as on the Xbox: four voices of the monk tone, swelling with the load. The loading screen stays at least 6 seconds, so the chant can swell as it does on the Xbox. Between levels the last view zooms out and fades. A level's first view stands still for up to 3 s while its textures arrive; then play starts.
5. **In play**, the PSP is one Xbox pad:

   | PSP | Halo |
   |---|---|
   | nub | look |
   | triangle / cross | move forward / back |
   | square / circle | strafe left / right |
   | R / L | fire / grenade |
   | d-pad up / down | jump / melee |
   | d-pad left | tap: reload; hold: action (vehicles, switches, weapons) |
   | d-pad right | switch weapon |
   | select (tap) | zoom |
   | select + up | flashlight |
   | select + down | crouch (toggles) |
   | select + L | switch grenades |
   | select + right | screenshot (to `halo/shots/`) |
   | start | pause menu |

   Halo's own control settings (presets, look sensitivity, invert) apply on top. A small performance readout is on by default; the launch menu's `nooverlay` turns it off.
6. **How fast:** README's "Where it stands" says what each level runs at. The heaviest stretches run slowly; this is the port's open work.
7. **To quit:** HOME → **ARK-4 Kommander** → **Exit**.

**Quit before you put the PSP to sleep** (the power switch). Sleep during play is still being tested; if the game doesn't come back from a sleep, hold POWER until the PSP turns off. <!-- TODO(master): drop or soften this once a hand test sleeps and wakes b139 cleanly (the open test since b131: build_b131.md test 6) -->

**The launch menu** (optional; the game calls it the development menu). **Hold L as the game starts**, or put an empty file `halo/menu.txt` on the stick, and the game opens a plain text menu before it starts:
- **MAPS:** every map on the stick, and whether its texture pack is beside it (`NO TEXTURE PACK` means the bake of that map is incomplete). The chosen map starts straight away.
- **OPTIONS:** the everyday keep words (switches) as toggles.
- **PRESETS:** named sets of keep words, from `halo/presets.txt`.
- **Controls:** up/down move; left/right or cross toggle; L/R switch pages; START saves and starts the game.

It writes your choices to `halo/keep.txt` (keep words, separated by spaces) and `halo/init.txt` (the map). The next plain launch still goes to the main menu, with the keep words you chose. To go back to the defaults, delete both files. None is needed to play.

**Check:** the main menu answers the buttons, a level loads with the chant, and `halo/log/log.txt` on the stick starts with `Halo PSP bring-up build N`, the release's build number. If the load is silent, the log says `loading: the chant is silent: chant.adp isn't in the bake` (step 4, "The chant").

## 7. Multiplayer

**Multiplayer isn't part of this release.** The build here is the campaign. A multiplayer build is in development (README, "Multiplayer"); a later release will say when it's ready.

## 8. When it goes wrong

**The log.** The game rewrites `halo/log/log.txt` at every launch and keeps the last 8 launches' as `halo/log/NNN_log.txt`, so a missed copy can still be recovered. After a crash it also writes `halo/crash.txt`.

**A crash** shows a text screen that ends: `Saved to ms0:/halo/crash.txt (if the Memory Stick still answers). Take a photo of this screen, then hold POWER.` Take the photo, then hold POWER until the PSP turns off.

1. **Copy the log off** right after the run that went wrong, before you launch again:

   ```sh
   cd "$STICK/halo" && tar czf ~/sierra-log.tgz log $(ls crash.txt stderr.txt 2>/dev/null); cd ~
   ```

   On Windows, copy the stick's `halo\log` folder (and `halo\crash.txt` and `halo\stderr.txt`, if they're there) into a new folder, then right-click it and choose "Compress to ZIP file".
2. **Open an issue** on **[Sierra-III-Release's issues page](https://github.com/Sheepdog-01/Sierra-III-Release/issues)** with:
   - the build number (the log's first line) and which zip you installed;
   - your PSP model (1000, 2000 or 3000) and your ARK-4 version;
   - your PC's system (Windows, macOS or Linux), whether you baked with that release's bundle, and the bake's last lines;
   - what you did, what you saw, and what you expected;
   - the log archive from step 1, the crash screen's photo if there was one, and a screenshot from `halo/shots/` if it shows the problem.
3. **Never attach game files:** nothing from `halo/maps/`, no `chant.adp`, nothing from your disc image or the bake's `work/` folder except its `.log` files.

**Common problems:**

| What you see | Cause | Fix |
|---|---|---|
| The game isn't in Game → Memory Stick | The zip isn't at the stick's root, or ARK-4 isn't running | Step 5's check; step 2 |
| The launch menu's MAPS page says `no maps in ms0:/halo/maps` | The bake isn't on the stick, or it's one folder too deep (`halo/halo/maps`) | Step 5.3 |
| A map says `NO TEXTURE PACK`, or a level never loads | That map's bake failed or was skipped | The bake's log (step 4); bake that map again with `--maps <map>,ui` and copy `halo/` again |
| The loading screen is silent | No `chant.adp` on the stick | Step 4, "The chant" |
| The game goes straight into a level, holding L shows no launch menu, or it starts in an odd state | Old `halo/init.txt`, `keep.txt`, `input.txt` or `nomenu.txt` left on the stick (from testing) | Delete those four files |
