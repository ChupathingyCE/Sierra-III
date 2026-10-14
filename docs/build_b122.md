# Build 122: b121 plus perception's rays on the Media Engine and cheaper posing (2026-10-14)

Labels: **[M]** measured on a PSP (the PSP-3000 rig's figures are marked as such until Sheepdog01's PSP-1000 runs this build), **[E]** emulator, **[D]** read in the code, **[U]** inference.

**b122 replaces b121 for testing:** it carries all of b121. Read `docs/review_b121.md` and `docs/review_b122.md` first.

`halo_psp_b122.zip` (SHA-256 `1eb5fae7233f651c12152122fed0ff585bf4b54767df147fd93044ab25ebc410`), EBOOT `d1dba340b62894cc18dd84416b8a72f4d92f1f1c69fb68d351db4af1cb38ca67`: built from commit `17a305f` on `w122` (lab run 38599466275). Heap 5,716 KB (b121: 5,760); 0 symbols stubbed. Its own profile (`campaign-b122`, counted at `832b74b` on `campaign121`, 13 runs). For the archive: `releases.tsv` has the line; `publish-releases.sh tags` and `archive` on your machine.

**Texture packs: `campaign121`**, the same as b121's. If b121's packs are already on the stick, the copy below changes nothing.

## What's in it

| Change | Kind | On? | Off word |
|---|---|---|---|
| Files reopened after a sleep | fix (b121) | yes | `noreopen` |
| The HUD 1:1 on whole pixels, nearer the top (your test 4b) | your A/B (b121) | yes | `hudnosnap hud150 hudnopushup` |
| The preset on the menu header and the screenshot stamp | tool (b121) | yes | n/a |
| See-through tints from texels; the bake picks the drawn map | restoration (b121) | yes | `drawnmapscale` |
| AI perception as a batch | exact speed (b121) | yes | `percold` |
| The level sphere walk | exact speed (b121) | yes | `surfacetestold` |
| Machines sleep | exact speed (b121) | yes | `machinesawake` |
| The ME job ring and the slant split | exact speed (b121) | yes | `meslantoff` |
| **Perception's rays on the Media Engine** | exact speed (new) | yes | `meraysoff` |
| **Cheaper posing** | exact speed (new) | yes | `posingold animlockcall` |

### On the rig (PSP-3000) [M]

| Change | Where | Gap with it on | Lab run |
|---|---|---|---|
| Rays on the ME | a50 | tick **−1.0 to −1.8 ms (−3.8 to −5.7%)**; 0 of 31,940 checked answers mismatched | 38567183309 |
| Rays on the ME | b30 (b113 route) | flight tick **−6.7 ms (−9.8%)**, cinematic −3.3 ms; landing and beach −2.0 to −2.3 ms (within the spread) | 38577715595 |
| Cheaper posing | c20 | tick −0.2 to −0.3 ms (−0.7 to −1%) | 38567141001 |

b121's own A/Bs are in `build_b121.md`. The ladder on this EBOOT (lab 38599518536) and the `aihalf` A/B on b30's beach (38599553360) are queued on the rig.

## On the PSP

**Install** (the EBOOT, the presets and the new packs):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b122.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b122 is on the stick"
for f in $psp/halo/maps/*.ptex      # every texture pack already on the stick, replaced by its b122 copy
    cp -v ~/psp-bake/campaign121/halo/maps/(basename $f) $f
end
find $psp/halo/shots -type f -delete
umount $psp
```
The `find` line empties the shots folder even when it's already empty (fish refuses `rm` with a wildcard that matches nothing).

The log's first line should say `bring-up build 122`, and the launch menu's header `Halo PSP build 122  preset none` until you apply one.

**After each test, pull the log** (change the name):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b122_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
find $psp/halo/shots -type f -delete
umount $psp
```

**The tests, in this order.** The preset number is what to pick on the launch menu's third page.

| # | Map, preset | Play | Stop | What it answers |
|---|---|---|---|---|
| 1 | The Silent Cartographer (b30), **3** (cinematic bench) | Hands off | It quits by itself at game time 70 s | The milestone number: b122 against b120's 158.7 ms |
| 2 | b30, **1** (play) | The landing and the beach fight; once in the fight, **sleep the PSP, wake it, and play on a minute** | When the fight's done | By feel: anything off? Does the game come back whole after a sleep (the b120 bug)? The new HUD in play |
| 3 | Two Betrayals (c40), **1** | Watch the opening cutscene; a screenshot in the control room | After the control room | LK18: the gold column, Guilty Spark's halo, the planet in its sphere. The railings and the bouncing textures are still open |
| 4 | The Pillar of Autumn (a10), **1** | The opening, from inside the bridge and the flyby | After the flyby | LK16: the bridge windows clear, no yellow splotches; LK17: the thruster |
| 5 | a10, **2** (the ladder) | Hands off | It quits by itself after The Maw (d40) | Every map on b122 on your PSP-1000 |
| 6 | b30, **4** (speed-ups off) | as 2, no sleep | as 2 | Optional, by feel: the same fight with the six speed-ups of b121 and b122 off |
| 7 | b30, **13** then **1** | Sit on the Pelican bench and look at the wall behind you; a screenshot on each | When you've looked | The Pelican wall: does `cutroom` bring it back? |
| 8 | c10, **8** then **1** | The swamp's plants, standing and moving | When you've looked | Foliage two-pass: softer plant edges, worth +13% frame time? |
| 9 | b30 beach, **7** then **1** | The beach fight | Fight done | Far AI at half rate: does the far AI feel slower to react? |
| 10 | d20, **9** then **1** | The corridors' ceiling and wall edges | When you've looked | Light merging: the seam, and is this the merge you meant? |

**First sitting:** 1, 2, 3 and 4, then start 5 on the charger and leave it. **Later:** 7–10 are your A/B calls, any order; 6 is optional. Presets 10 (`seethroughvfpu`, c40), 11 (`machinesfar posehalf`, c20) and 12 (`sceneryhalf`) are speed questions the rig answers, so you can skip them.
