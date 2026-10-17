# Build 137: faster start-up, the loading chant, multiplayer on Milenko's servers (2026-10-17)

`review_b137.md` lists every change and why. **No new bake:** b137 runs on the b136 bake.

`halo_psp_b137.zip` (SHA-256 `c3769e1a7183da0a5c74d6fb97cb127b21b4fff3d61c26863dc53bb3daa25959`), EBOOT `b22b5349a53b78b4593564d49720959f5b5c3f3f937b035df7ec6abd9605ab88`: built from commit `b7524d5` on `w137` (lab sierra-build 39036434369). Heap 5,600 KB (b136: 5,584). **Its own profile** (`campaign-b137`, 13 runs at `290fdaf` on the b136 bake), checked against the code (`PSP_PGO_STRICT=1`), and b132's code order. Release check (lab 39034419300): play and both seeds identical to b136 at every tick on d20, a50 and c20 (12,000 each) [E]; shots, the menu, a10's prompts and the menu into a10 right; Blood Gulch plays; strict clean. For the archive: `releases.tsv` has the line; `publish-releases.sh tags` and `archive` on your machine.

**What's new for you:**
- **The menu answers sooner.** The saved-game check on the PSP-1000 took 26 s in b136; 1.8 s now (3.7 s on the build's first launch) [M]. The start-up's log also waits in a batch, so the stick isn't held at the first frame.
- **The loading chant**, as on the Xbox: four voices of the monk tone, swelling with the load. Between levels the last view zooms out and fades (the Xbox's trail); from the menu the screen is already black. The `loading.tga` picture needs the bake to carry the disc's file: b138.
- **The settle:** after a load from the menu or the level before, the level's first view is drawn standing still until its textures are in (up to 3 s), then play starts.
- **Multiplayer build:** joins and plays Milenko's `[C]` servers on both PSPs (the errno race fixed; KCP has a 64 KB reserve); 16 KB more heap (the glyph sheet moved to VRAM).
- **The menu's glow clamped as the Xbox does** (the ring's clouds as ChupathingyCE draws them).
- **The level's geometry shares the models' VRAM room** (exact): c20 and d20 frames 5–8% faster on the PSP-3000 [M].
- **One fix the reviewer found:** a model's VRAM copy could be placed over one the GE was still reading (b136 had it with `vrammodels` on).

SPEED_LINE

**A call for you:** the chant's 18 KB are disc audio, typed into upstream's decompiled source, so every EBOOT has carried them. b137 plays them and adds nothing. Keep them as upstream ships them, or bake them from your disc (b138)? Decide before any build is shared.

## On the PSP

**Install the build** (the b136 maps stay):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b137.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b137 is on the stick"
for f in init.txt keep.txt input.txt; rm -f $psp/halo/$f; end
find $psp/halo/shots -type f -delete
umount $psp
```
The log's first line should say `bring-up build 137`. Clearing `init.txt`, `keep.txt` and `input.txt` takes the rig's last job off the stick, so the game boots to its main menu.

**After each test, pull the log** (change the number each time):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b137_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
find $psp/halo/shots -type f -delete
umount $psp
```

**The tests:**

| # | How | Play | Stop | What it answers |
|---|---|---|---|---|
| 1 | Boot normally from the XMB | Time from the XMB to the menu answering; then Campaign into The Pillar of Autumn (a10); listen through the load | The cryo prompt shows | The start-up (and how long the boot log shows), the chant, the settle |
| 2 | a10, preset **1** | Play to the end of a10 so it loads a30 | a30's first view | The trail between levels, the chant, the settle there |
| 3 | The multiplayer build: `halo_psp_b137mp.zip` (SHA-256 `601d0d75…`, EBOOT `b0c8b601…`, lab sierra-build 39036478850, repacked into `PSP/GAME/halomp` as `multiplayer_setup.md` §1 does), unzipped over the stick as in §6; the mp134 maps | Join a `[C]` server of Milenko's, play a game on Prisoner if one runs | 5 min, or a drop | The heap on Prisoner, with the glyph sheet's 16 KB back |

b136's open tests (wake-on-write's hits on b30, the controls on a30, the sleep test with `nomeaudio`) move to b137.

**Turning things off** (launch menu keep words): `chantoff`, `settleoff` (or `settleN`, N seconds at most), `loadingoff` (no chant, no picture, no settle), `loadingplain` (black frames), `nolevelvram`, `glownoclamp`, `bootvariants`, `logeachline`, `pathscan`, `loadwhole`.
