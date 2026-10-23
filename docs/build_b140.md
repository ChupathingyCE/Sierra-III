# Build 140: the milestone (2026-10-23)

`review_b140.md` lists every change and why. Build 140 is the first release with the frame in flight (M6 #2), the crash fixes of 10-22 and 10-23, and the compromises you chose (aicad3, lodpsp). It plays far better than b139: b30's opening 4.5 -> 6.5 fps, b40 16.8 -> 27.8, c10 9.0 -> 12.8, c40 and d40 at the 30 fps cap (PSP-3000 [M]).

`halo_psp_b140rel.zip`, EBOOT `86408aa92133c1dc`: built from Sierra-III `bc40b5a` (w140) with `PSP_PGO_PROFILE=campaign-b140 PSP_PGO_STRICT=1 PSP_METRICS=0` (lab sierra-build 39913659207). It needs the **b140z** bake. The measuring twin (metrics on) is `halo_psp_b140.zip`, EBOOT `fe20986e0425216f` (39913654863).

## On the PSP

**Install** (the b140z maps stay):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b140rel.zip
ls -l $zip
unzip -o $zip -d $psp
for f in init.txt keep.txt input.txt; rm -f $psp/halo/$f; end
umount $psp
```
Clearing `init.txt`, `keep.txt` and `input.txt` takes the rig's last job off the stick (its keep words and its jump straight into b30), so the game boots to its main menu with the build's own defaults. The log's first line should say `bring-up build 140`.

**Maps** (only if the stick doesn't have b140z yet), on the X3D:
```fish
sudo rsync -rt --modify-window=2 --delete /home/runner/psp-bake/b140z/halo/maps/ $psp/halo/maps/
```

**Every level:** the zip carries an "All Levels" profile (`halo/save/u/UDATA/1483099185B9/`): choose it and the Load Level list has every level at every difficulty.

## What to try

- Play as you like; the release's defaults are on. A/B words from the launch menu if something looks off: `aicad3off`, `lodpspoff`, `notexpretouch`, `lookrealoff`, `detailequal` (the menu ring), `geshow3off`.
- **Die once** and let the checkpoint restart (the restart fix).
- **a10's look tutorial** through the inversion step (the freeze fix).
- **The menu ring:** the far band should hold still.

## Known

- The tutorial's lights on a10 may stay yellow (not reproduced: likely a legacy stick preset's path). Which stick preset is your profile on?
- Animations sometimes look cut short: not yet looked at.
- The frame pacing still swings between catching up and falling behind on heavy stretches; the fix is more speed (your call, 10-22).

## Proof

`review_b140.md`, "Proof of this build".
