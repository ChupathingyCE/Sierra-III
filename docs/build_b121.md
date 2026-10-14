# Build 121: ME phase 1, three tick speed-ups, the see-through restorations (2026-10-14)

Labels: **[M]** measured on a PSP (the PSP-3000 rig's figures are marked as such until Sheepdog01's PSP-1000 runs this build), **[E]** emulator, **[D]** read in the code, **[U]** inference.

**Read `docs/review_b121.md` first:** every change since b120, its key lines and why.

`halo_psp_b121.zip` (SHA-256 `05d693c51cb7fffea23734718b4703cd121925edda0b8380df0ad84869ee73ed`), EBOOT `5f12f78b488f667d486831543a5c27f5dffb7becec37fec6e949df09ef75e70d`: built from commit `49d0938` on `w121` (lab run 38551328565). Heap 5,760 KB (b120: 5,836); 0 symbols stubbed. Built with **its own profile** (`campaign-b121`: counted at `76a59fd` on `campaign121`, 13 runs, 156 units), `PSP_PGO=use PSP_PGO_PROFILE=campaign-b121 PSP_PGO_EXTRA=-fno-unroll-loops,-fno-peel-loops,-fno-tracer,-fno-ipa-cp-clone`. For the archive: `releases.tsv` has the line; `publish-releases.sh tags` and `archive` on your machine.

**New texture packs: `campaign121`.** Only the `.ptex` files differ from `campaign120` (checked file by file: the see-through tints and the drawn maps are in them), so the install replaces just the packs already on the stick. A b121 pack needs a b121 build, and the other way round: b120 would draw the holograms' band with the planet's tint.

## What's in it

| Change | Kind | On? | Off word |
|---|---|---|---|
| Files reopened after a sleep | fix | yes | `noreopen` |
| The HUD 1:1 on whole pixels, nearer the top (your test 4b) | your A/B | yes | `hudnosnap hud150 hudnopushup` |
| The preset on the menu header and the screenshot stamp | tool | yes | n/a |
| See-through tints from texels; the bake picks the drawn map | restoration | yes | `drawnmapscale` |
| AI perception as a batch | exact speed | yes | `percold` |
| The level sphere walk | exact speed | yes | `surfacetestold` |
| Machines sleep | exact speed | yes | `machinesawake` |
| The ME job ring and the slant split (ME phase 1) | exact speed | yes | `meslantoff` |

### On the rig (PSP-3000) [M]

**The A/Bs, each speed-up against its off word in one build** (`tools/abreport.py`; repeat 2; "real" = the gap beats the repeats' own spread):

| Change | Where | Tick with it on | Off | Gap | Lab run |
|---|---|---:|---:|---:|---|
| AI perception batch | b30 beach (b113 route) | 49.2 ms | 52.0 | **−2.8 ms (−5.4%)** | 38524100365 |
| AI perception batch | a50 | 25.9 | 26.4 | −0.6 (−2.2%) | 38524222985 |
| Level sphere walk | a50 | 18.5 to 30.9 by stretch | 19.6 to 32.5 | **−0.8 to −1.6 ms (−3.4 to −6.0%)** | 38548747232 |
| Level sphere walk | c20 | 27.9 | 27.9 | none (by design: c20's walks are small) | 38548708625 |
| Machines sleep | c20 | 27.9 | 28.7 | **−0.7 to −0.8 ms (−2.6 to −3.0%)**, frame −1.1% | 38548708625 |
| ME slant split | b30, c20 | n/a | n/a | the sort −0.7 to −1.4 ms a frame (b30), −0.6 (c20); 0 of 414,000 triangles differing, 0 failed jobs | 38524258259, 38524293692 |

**The ladder on this exact EBOOT and the new packs** (lab run 38551389789; every map's opening, second half of its 90 s, hands off; `tools/ladderreport.py`). The openings are mostly cutscenes and flights, so the AI and biped gains above show little here:

| Level | fps | Tick (ms) | Ticks a second | b120 on the rig: fps / tick (ms) |
|---|---:|---:|---:|---|
| The Pillar of Autumn (a10) | 14.5 | 6.6 | 25.4 | 14.9 / 6.6 |
| Halo (a30) | 6.9 | 10.5 | 13.9 | 7.0 / 10.5 |
| Truth and Reconciliation (a50) | 9.6 | 30.1 | 19.0 | 9.3 / 31.3 |
| The Silent Cartographer (b30) | 4.0 | 56.6 | 8.0 | 4.0 / 56.7 |
| Assault on the Control Room (b40) | 14.9 | 16.3 | 28.0 | 14.9 / 16.1 |
| 343 Guilty Spark (c10) | 9.1 | 22.0 | 18.0 | 9.3 / 21.3 |
| The Library (c20) | 8.0 | 25.6 | 15.6 | 8.0 / 26.0 |
| Two Betrayals (c40) | 13.7 | 15.2 | 26.7 | 13.7 / 15.3 |
| Keyes (d20) | 8.1 | 15.0 | 16.1 | 8.2 / 16.3 |

The tick is down on a50 (−1.2 ms) and d20 (−1.3), level elsewhere, and up on c10 (+0.7). One ladder run is one sample: c10's and a10's small drops are within what one run varies [U]. Your PSP-1000's ladder (test 5) is the comparison that counts.

## On the PSP

**Install** (the EBOOT, the presets and the new packs):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b121.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b121 is on the stick"
for f in $psp/halo/maps/*.ptex      # every texture pack already on the stick, replaced by its b121 copy
    cp -v ~/psp-bake/campaign121/halo/maps/(basename $f) $f
end
find $psp/halo/shots -type f -delete
umount $psp
```
The `find` line empties the shots folder even when it's already empty (fish refuses `rm` with a wildcard that matches nothing).

The log's first line should say `bring-up build 121`, and the launch menu's header `Halo PSP build 121  preset none` until you apply one.

**After each test, pull the log** (change the name):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b121_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
find $psp/halo/shots -type f -delete
umount $psp
```

**The tests, in this order.** The preset number is what to pick on the launch menu's third page.

| # | Map, preset | Play | Stop | What it answers |
|---|---|---|---|---|
| 1 | The Silent Cartographer (b30), **3** (cinematic bench) | Hands off | It quits by itself at game time 70 s | The milestone number: b121 against b120's 158.7 ms |
| 2 | b30, **1** (play) | The landing and the beach fight; once in the fight, **sleep the PSP, wake it, and play on a minute** | When the fight's done | By feel: anything off? Does the game come back whole after a sleep (the b120 bug)? The new HUD in play |
| 3 | Two Betrayals (c40), **1** | Watch the opening cutscene; a screenshot in the control room | After the control room | LK18: the gold column, Guilty Spark's halo, the planet in its sphere. The railings and the bouncing textures are still open |
| 4 | The Pillar of Autumn (a10), **1** | The opening, from inside the bridge and the flyby | After the flyby | LK16: the bridge windows clear, no yellow splotches; LK17: the thruster |
| 5 | a10, **2** (the ladder) | Hands off | It quits by itself after The Maw (d40) | Every map on b121 on your PSP-1000 |
| 6 | b30, **4** (speed-ups off) | as 2, no sleep | as 2 | Optional, by feel: the same fight with b121's four speed-ups off |

**With one evening:** 1, 2, 3 and 4, then start 5 on the charger and leave it.
