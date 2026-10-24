# Build 141: the b140 polish (2026-10-24)

`review_b141.md` lists every change and why. Build 141 is build 140 with the fixes from your b140 and b140p1 hand tests: the real-time look back on, the meter load off, models no longer drawn white for long stretches, the heap's failures gone on a10 and a30, and the release's instrument timers off (b30 ~3% faster). No new levers: BSPR v2 and the rest wait for build 142.

`halo_psp_b141rel.zip`, EBOOT `867cc24547f73d14`: built from Sierra-III `0915fb4` (w140) with `PSP_PGO_PROFILE=campaign-b141 PSP_PGO_STRICT=1 PSP_METRICS=0` (lab sierra-build 40082747122). It needs the **b140z** bake, as build 140 did. The measuring twin (metrics on) is EBOOT `6ad8950f7b47f1b6` (40082744222).

## On the PSP

**Install** (the b140z maps stay):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b141rel.zip
ls -l $zip
unzip -o $zip -d $psp
for f in init.txt keep.txt input.txt; rm -f $psp/halo/$f; end
umount $psp
```
Clearing `init.txt`, `keep.txt` and `input.txt` takes the rig's last job off the stick, so the game boots to its main menu with the build's own defaults. The log's first line should say `bring-up build 141`.

**Maps** (only if the stick doesn't have b140z yet), on the X3D:
```fish
sudo rsync -rt --modify-window=2 --delete /home/runner/psp-bake/b140z/halo/maps/ $psp/halo/maps/
```

**Every level:** the zip carries the "All Levels" profile (`halo/save/u/UDATA/1483099185B9/`).

## What to try

- **The look:** the real-time turn is back on; it should feel like b140p1's run 3. `lookrealoff` is the capped turn.
- **White models:** Chief, Grunts and Jackals should draw textured within a frame or two of appearing. `txptceil85` is the old limit.
- **a10 and a30:** the overlay's red "HEAP FULL" line should not appear. `copyreserve0` is the old behaviour.
- **Speed:** the release's timers are off; `timerson` turns them back on, for comparison.
- **Keyes at a10 t236:** `nearcineoff` is the old near plane. This one is unproven: tell me whether the face still clips.

## Known

- **The HUD bars** on a10 show but pulse at empty and never fill. b139 draws them the same; the fault is upstream of the meter's palette. Did the bars ever fill on an older build?
- The tutorial's lights on a10 may stay yellow (likely a legacy stick preset's path: which preset is your profile on?).
- Animations sometimes look cut short: not yet looked at.

## Proof

`review_b141.md`, "Proof of this build".
