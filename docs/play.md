# Playing Sierra-III: from a stock PSP to a playable game

This guide takes you from a PSP and your own Halo: Combat Evolved disc for the original Xbox to the game running on the PSP. Follow the steps in order. Each step says what you need, what to type, and how to tell it worked (the **Check:** lines).

Sierra-III contains nothing from the game. You bake the maps yourself, from your own disc, on your own PC. Never download game files, and never share the ones you bake.

**Commands** are plain `sh` (bash, zsh or dash all work). Two names stand for paths on your machine:
- `$STICK`: where your PSP's Memory Stick is mounted (step 1, "Finding `$STICK`", shows how to find it).
- `~/Downloads`: where you saved the release's files.

<!-- TODO(master): the release these steps were checked against (tag, e.g. b139), once 1.3 names it -->

## 1. What you need

1. **A PSP.** The port is built for the PSP-1000 and tested on it; the PSP-2000 and PSP-3000 work too.
2. **Custom firmware:** official firmware 6.61 with ARK-4 (step 2). Sierra-III is built and tested on that.
3. **A Memory Stick** (or an adapter) with about 3.5 GB free. <!-- TODO(master): confirm the free space against b139's full bake (the build zip, the bake, and room for the logs) -->
4. **Your Halo disc** for the original Xbox, **NTSC, version 01.10.12.2276**, as a disc image (step 3).
5. **A PC to bake on:** x86-64 Linux with glibc 2.39 or later (Ubuntu 24.04, Fedora 40, a current Arch or CachyOS), and Python 3.9 to 3.14. On Windows, use WSL2 with Ubuntu 24.04. <!-- TODO(master): has the bake been run end to end under WSL2? If not, say "should work" until it has -->
6. **About 3 GB free on the PC** while the bake runs. <!-- TODO(master): the free disk the b139 bake needs, and its result's size (bake/README's 3 GB and 1.6 GB are stale) -->
7. **A USB cable** for the PSP, or a card reader for the Memory Stick.

**Finding `$STICK`.** On the PSP, open **Settings → USB Connection** and plug the cable in. Most Linux desktops mount the stick by themselves. Then:

```sh
lsblk -o NAME,SIZE,LABEL,MOUNTPOINT      # find the stick by its size; its MOUNTPOINT is the path
STICK=/run/media/$USER/<label>           # or whatever MOUNTPOINT showed; set it again in each new terminal
ls "$STICK"                              # a stick in use shows PSP/ at least
```

Under WSL2, a stick Windows shows as drive `E:` can be mounted with `sudo mkdir -p /mnt/e && sudo mount -t drvfs E: /mnt/e`, then `STICK=/mnt/e`.

**When you're done with the stick**, every time: `sync`, then `umount "$STICK"` (or eject it from the desktop), then leave USB mode on the PSP.

**Check:** `ls "$STICK"` lists `PSP`.

## 2. Custom firmware (ARK-4)

The PSP runs homebrew only on custom firmware. We don't write a CFW guide: follow ARK-4's own.

1. Install ARK-4 by **[its own instructions](https://github.com/PSP-Archive/ARK-4)**. They say which official firmware it needs and how to install it on your model; Sierra-III is tested on 6.61.
2. **Settings that matter for Sierra-III:**
   - **None for the campaign.** The game sets the PSP's standard clock (333 MHz) by itself; it never needs an overclock.
   - **For multiplayer only:** ARK-4's WPA2 patch, on for homebrew. That is the line `always, wpa2, on` in ARK-4's `SETTINGS.TXT` ([ARK-4's settings page](https://github.com/PSP-Archive/ARK-4/wiki/settings)). `multiplayer_setup.md` §3 has the rest.
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
   python3 -I ~/Downloads/halo-psp-bake/xiso.py ~/Halo.iso | grep 'maps/'
   ```

   It lists the image's files, here only the maps, with their sizes.
3. **Check the revision.** The bake checks every map's SHA-256 against the retail NTSC 01.10.12.2276 maps (`hashes.json` in the bundle). The quickest test is a bake of the menu map alone, into a scratch folder:

   ```sh
   ~/Downloads/halo-psp-bake/halo-psp-bake ~/Halo.iso ~/halo-psp-test --maps ui
   ```

   This also sets the bake up (step 4.3), so it is not wasted time. Delete `~/halo-psp-test` afterwards.

**Check:**
- step 2 lists the maps, `maps/ui.map` among them (an image the reader can't open says `not an Xbox disc image`);
- step 3 ends with `baked 1 of 1 maps`. If it says `ui: not the retail map this build expects`, the image is another version, region or a bad dump (step 4, "Hash mismatch").

## 4. Baking the maps

The bake turns your disc's maps into the files the PSP reads: each map rebuilt for the PSP's memory, plus its texture, geometry and wall-decal packs. It also reads the loading screen's chant out of the disc's `default.xbe` into `halo/chant.adp`.

**Use the bake bundle from the same release as the build zip.** A new release's notes say whether you need to bake again (from build 139 you do: `bake_changes.md`).

1. **Download** the bake bundle from the release page, **[Sierra-III-Release's releases](https://github.com/Sheepdog-01/Sierra-III-Release/releases)**, into `~/Downloads`. <!-- TODO(master): the bundle's file name (make_bundle.sh writes halo-psp-bake.tar.gz) and the release tag; confirm the release page once the mirror is live -->
2. **Check it and unpack it:**

   ```sh
   cd ~/Downloads
   sha256sum halo-psp-bake.tar.gz        # must equal the bundle's SHA-256 in the release notes
   tar xzf halo-psp-bake.tar.gz          # makes ~/Downloads/halo-psp-bake/
   ```

3. **Run it:**

   ```sh
   ~/Downloads/halo-psp-bake/halo-psp-bake ~/Halo.iso ~/halo-psp-out 2>&1 | tee ~/halo-psp-bake.log
   ```

   - **The first run** sets up a Python environment beside the script (`halo-psp-bake/.venv`) and needs the internet once. If it says it needs Python 3.9 to 3.14, or `python3 -m venv failed`, `bake/README.md` ("What you need") has the fix.
   - **If it says the bundled Invader doesn't run on this system**, your system's glibc is older than 2.39: use a newer system, or build Invader from the source in the bundle (`INVADER.md` in the bundle).
   - **How long:** 3 to 10 minutes a map, so an hour or two for all 24. <!-- TODO(master): confirm the time on b139's bundle -->
   - Each line is timed. A map ends with `<map>: done in N s`.
4. **Optional:**
   - `--maps c20,bloodgulch,ui`: only these maps. Always include `ui`, the menus.
   - `--detail fp,vehicles`: also bake full-size textures for those classes (`fp`, `weapons`, `vehicles`, `characters`). The launch menu's `assetdetail` option uses them. More disk and time.
   - `--eboot ~/Downloads/<build>/PSP/GAME/halo/EBOOT.PBP`: a copy of the game with a clip of your disc's music as the XMB's sound for the game (`bake/README.md`, "The menu's sound").

**Check:** the last lines of the bake say

```
the chant: read from default.xbe into /home/<you>/halo-psp-out/halo/chant.adp
baked 24 of 24 maps into /home/<you>/halo-psp-out/halo/maps
copy /home/<you>/halo-psp-out/halo to the root of the Memory Stick (it holds maps/)
```

and `ls ~/halo-psp-out/halo` shows `chant.adp` and `maps`.

**Hash mismatch.** A line like

```
b30: not the retail map this build expects (sha256 0123abcd...); skipped (--force bakes it anyway)
```

means that map in your image isn't the retail NTSC 01.10.12.2276 one: another version or region, a modified map, or a damaged dump. The bake skips it, and the last line counts fewer maps (`baked 23 of 24`). Make the image again from your disc. `--force` bakes it anyway, but the port is made and tested only for the retail maps, so expect it to break.

**Other failures.** A map that fails says `<map>: FAILED:` with the end of its own log. Every map's full log stays in `~/halo-psp-out/work/<map>.log`. The bake then ends with `failed: <maps>`.

**The chant.** `the chant: no default.xbe in …` (a folder of maps alone) or `… holds no chant this build knows` means no `chant.adp`: the game plays, but its loading screen is silent. Bake from the whole image, or from the folder that holds `default.xbe`.

<!-- TODO(master): when go_live 3.4 lands, say what the game shows for a bake from another bundle ("re-bake with bundle N") -->

The output is game data made from your disc. Keep it on your own machines.

## 5. Installing on the Memory Stick

You copy two things: the **build zip** (the game, from the release page) and the baked **`halo/` folder** (the maps and the chant).

1. Mount the stick (step 1) and set `STICK`.
2. **Unzip the build** at the stick's root: <!-- TODO(master): the zip's name for the release (halo_psp_bNNN.zip) -->

   ```sh
   unzip -o ~/Downloads/halo_psp_bNNN.zip -d "$STICK"
   ```

3. **Copy the bake** to the stick's root (only `halo/`, not the whole output folder):

   ```sh
   cp -R ~/halo-psp-out/halo "$STICK/"
   ```

   It's large, so it takes a while. **When you bake again for a new release,** delete the old maps first, so no stale pack is left behind: `rm -rf "$STICK/halo/maps"`, then copy.
4. `sync`, then `umount "$STICK"`.

**The stick, once installed:**

| Path | What | From |
|---|---|---|
| `PSP/GAME/halo/` | the game: `EBOOT.PBP`, `halo_crash.prx`, `halo_profile.prx`, `LICENSE-mecore.md` | the build zip |
| `halo/presets.txt` | the launch menu's presets (when the build has one) | the build zip |
| `halo/maps/` | each map's `.map` and its packs (`.ptex`, `_track.ptex`, `.pgeo`, `.pstn`) | your bake |
| `halo/chant.adp` | the loading screen's chant | your bake |
| `halo/log/`, `halo/shots/`, `halo/save/`, `halo/crash.txt`, `halo/stderr.txt` | what the game writes | the game |
| `halo/keep.txt`, `halo/init.txt` | the launch menu's choices (step 6) | the launch menu |
| `PSP/GAME/halomp/`, `halomp/` | the multiplayer build and its files (step 7) | the multiplayer zip |

**The stick rules:**
- Delete only inside `halo/`, `halomp/`, `PSP/GAME/halo/` and `PSP/GAME/halomp/`.
- **Leave ARK-4's files alone**, wherever they are (`SEPLUGINS/` and the files ARK-4's guide put on the stick), and `overconfig.txt` if your stick has one.
- **Never format or wipe the stick.**

**Check:** before you unmount,

```sh
unzip -p ~/Downloads/halo_psp_bNNN.zip PSP/GAME/halo/EBOOT.PBP | cmp - "$STICK/PSP/GAME/halo/EBOOT.PBP" && echo "the build is on the stick"
ls "$STICK/halo/chant.adp" "$STICK/halo/maps/ui.map" "$STICK/halo/maps/a10.ptex"
```

prints `the build is on the stick` and lists the three files.

## 6. First launch

1. On the PSP, open **Game → Memory Stick** and start the game. <!-- TODO(master): the XMB title for the release: psp_build.py's default is "Halo PSP bring-up" unless PSP_TITLE is set; multiplayer_setup.md calls it "Halo PSP" -->
2. **The start-up.** The game starts up, then loads its menus. <!-- TODO(master): what the start-up looks like in the release: b138 shows the log scrolling on the screen; the bootscreen branch (b00b9cd, not yet run) draws a loading screen with a bar and the hint "Hold L as the game starts for the development menu" for 3 s; and the time from the XMB to the menu (b138 test 1) --> The first launch of a new build takes a few seconds longer (the saved-game check).
3. **The main menu** is Halo's own, with the ring behind it. It answers once its textures are in. In Halo's menus the buttons are the pad's: cross is A, circle B, square X, triangle Y; the d-pad and the nub move.
4. **Starting a level.** Pick **Campaign** and a level. While it loads you hear the **chant**, as on the Xbox: four voices of the monk tone, swelling with the load. Between levels the last view zooms out and fades. A level's first view stands still for up to 3 s while its textures arrive; then play starts.
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

<!-- TODO(master): is the PSP's sleep (the power switch) safe during play on the release? b135/b136 tested a sleep hang. If not, say "quit before you put the PSP to sleep" -->

**The launch menu** (optional; the game calls it the development menu). **Hold L as the game starts**, or put an empty file `halo/menu.txt` on the stick, and the game opens a plain text menu before it starts:
- **MAPS:** every map on the stick, and whether its texture pack is beside it (`NO TEXTURE PACK` means the bake of that map is incomplete). The chosen map starts straight away.
- **OPTIONS:** the everyday keep words (switches) as toggles.
- **PRESETS:** named sets of keep words, from `halo/presets.txt`.
- **Controls:** up/down move; left/right or cross toggle; L/R switch pages; START saves and starts the game.

It writes your choices to `halo/keep.txt` (keep words, separated by spaces) and `halo/init.txt` (the map). The next plain launch still goes to the main menu, with the keep words you chose. To go back to the defaults, delete both files. None is needed to play.

**Check:** the main menu answers the buttons, a level loads with the chant, and `halo/log/log.txt` on the stick starts with `Halo PSP bring-up build N`, the release's build number. If the load is silent, the log says `loading: the chant is silent: chant.adp isn't in the bake` (step 4, "The chant").

## 7. Multiplayer (briefly)

The multiplayer build joins Halo PC's public ChupathingyCE games over the internet from its **ONLINE GAMES** screen. For now it only joins: no hosting, no PSP-to-PSP play.

1. Unzip the release's multiplayer zip at the stick's root: `unzip -o ~/Downloads/halo_psp_bNNNmp.zip -d "$STICK"` (it makes `PSP/GAME/halomp/`). It reads the same baked maps in `halo/maps/`; a full bake has all thirteen multiplayer maps.
2. The multiplayer build keeps its own files in `halomp/`. For its loading chant, copy the chant there too:

   ```sh
   mkdir -p "$STICK/halomp" && cp ~/halo-psp-out/halo/chant.adp "$STICK/halomp/"
   ```

   <!-- TODO(MP): the multiplayer build reads chant.adp from ms0:/halomp (PSP_DATA_ROOT, psp_loading.c), but the bake writes only halo/chant.adp; either keep this step (and add it to multiplayer_setup.md §2) or have the build read it from ms0:/halo -->
3. Set up the Wi-Fi: ARK-4's `always, wpa2, on`, a WPA2-AES or open 2.4 GHz network saved as **connection 1**, WLAN power save off.
4. Start the multiplayer build from the XMB, then **Multiplayer → ONLINE GAMES**, and pick a game.

**`multiplayer_setup.md`** has every step, the Wi-Fi a PSP can join, what each message means, and playing over your own PC's hotspot and server.

**Check:** the ONLINE GAMES list fills within a few seconds.

## 8. When it goes wrong

**The log.** The game rewrites `halo/log/log.txt` at every launch and keeps the last 8 launches' as `halo/log/NNN_log.txt`, so a missed copy can still be recovered. After a crash it also writes `halo/crash.txt`. The multiplayer build writes the same files in `halomp/`.

**A crash** shows a text screen that ends: `Saved to ms0:/halo/crash.txt (if the Memory Stick still answers). Take a photo of this screen, then hold POWER.` Take the photo, then hold POWER until the PSP turns off.

1. **Copy the log off** right after the run that went wrong, before you launch again:

   ```sh
   cd "$STICK/halo" && tar czf ~/sierra-log.tgz log $(ls crash.txt stderr.txt 2>/dev/null); cd ~
   ```

   For the multiplayer build, use `"$STICK/halomp"` and name the file `~/sierra-mp-log.tgz`.
2. **Open an issue** <!-- TODO(master): where players file issues (Sierra-III-Release's issues?) --> with:
   - the build number (the log's first line) and which zip you installed;
   - your PSP model (1000, 2000 or 3000) and your ARK-4 version;
   - whether you baked with that release's bundle, and the bake's last lines (from `~/halo-psp-bake.log`);
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
