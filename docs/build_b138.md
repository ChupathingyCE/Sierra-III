# Build 138: the dress rehearsal (2026-10-17)

`review_b138.md` lists every change and why. **No new bake:** b138 runs on the b136 bake. It is the dress rehearsal for going public (`go_live.md`): Sheepdog01 plays it by hand on a PSP; b139 brings the new bake.

`halo_psp_b138.zip` (SHA-256 `7407cd615f780b52ebcfb7c284d6ef5439841a16fbffd3ed72ba0fbcd6c07f07`), EBOOT `c31a50bc5467faa62167575659fa14d851f98c3ffcccad11c2b2e8184f6bc2c8`: built from commit `d32c48e` on `w138` (lab sierra-build 39056266381). Heap 5,604 KB. Its own profile (`campaign-b138`, 13 runs at `a9bf5ed`), checked against the code (`PSP_PGO_STRICT=1`: 0 functions without counts), b132's code order. Release check (lab 39052254838, 39052259922): play and both seeds identical to b137 at every tick on d20, a50 and c20 [E]; strict clean; shots, the menu, a10's prompts and the menu into a10 right; Blood Gulch plays in the multiplayer build.

`halo_psp_b138mp.zip` (SHA-256 `74203e45664a60bb76de1f628cdecac499df796ba0b20d05afd3163de800ace8`), EBOOT `fd76f3df1d8cedb1f33ef222d7f3efc72bf8afc7cc592aca70e148b49f242543`: the multiplayer build (client only) from the same commit with the campaign's profile and code order (lab sierra-build 39056270636), repacked into `PSP/GAME/halomp`. Heap 5,220 KB (b137: 3,668).

**What's new for you:**
- **Levels after a level change keep their stencils** (in the emulator; on the PSP c20's still failed once: see Known issues). b137's ladder lost b30's and c20's after a10 → a30 → a50 (the heap split by the chant's streams); the chant now plays from static storage, and the see-through table (50 KB) moved out of the heap when there's room above it. In the emulator the ladder a10 → d40 loads every level's stencils [E].
- **The menu's ring** was meant to stop sparkling with `showcasebias` 20; it drew panels instead (see Known issues).
- **Faster:** the models' VRAM room 784 KB (a30 and b30 frames 1–2.6% faster on the PSP-1000 [M]); lights linked once a frame, the debug call out of the model loop, three hot words in one cache line, the objects' timers on one tick in eight (about 1.5 ms a tick on b30 from the cache counts [U]; the ladder says how much on the PSP).
- **The multiplayer build is a client only** (Sheepdog01, 09:48: hosting and ad hoc later): 1.5 MB more heap, and it now gets the profile and code order. Joined a 7-player Prisoner game on the PSP-1000.

## Known issues (found by Sheepdog01's hand test and the lab after release; fixes in b139)

Set these in `ms0:/halo/keep.txt` for b138: `vrammodels600 showcasebias0 glownoclamp streampackets4`.
- **The menu ring:** `showcasebias` (20 by default) draws its outer face from the coarsest levels ("panels") and dims the sky; `showcasebias0` puts it back as b137, and `glownoclamp` stills the shimmer. b139: both by default.
- **The models' VRAM room (784 KB):** b30 draws untextured for seconds at a time (PSP-1000 [M]: 193 frames, a run of 85, against 21 at 600). `vrammodels600`; b139: 600 by default.
- **The menu music skips forward** at 16 queued packets; `streampackets4` plays it clean (Sheepdog01's ear). b139: 4 by default, with the streams topped up more often.
- **c20's stencils can still fail after six level changes** (b138's PSP-3000 ladder [M]). b139: the stencils' block taken while the heap's top is whole.
- **a10's level geometry and see-through surfaces missing** (the level copies short of heap: the cryo grating, walls). b139: the copies' overflow room.
- **The loading chant is silent and the load from the menu shows black.** b139: the chant and the loading picture from the player's bake, and a visible loading screen.
- **Profiles:** a deleted profile's name can't be reused, and the profile screens slow the game and show red engine errors. Being diagnosed.
- **Flat level walls:** the bake leaves out the bump maps and the PSP draws no specular or reflection passes, and base maps are halved. A look decision for Sheepdog01 (see the go-live notes).

## On the PSP

**Install the build** (the b136 maps stay):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b138.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b138 is on the stick"
for f in init.txt keep.txt input.txt; rm -f $psp/halo/$f; end
find $psp/halo/shots -type f -delete
umount $psp
```
The log's first line should say `bring-up build 138`. Clearing `init.txt`, `keep.txt` and `input.txt` takes the rig's last job off the stick, so the game boots to its main menu.

For test 4, the multiplayer build too:
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b138mp.zip
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halomp/EBOOT.PBP | cmp - $psp/PSP/GAME/halomp/EBOOT.PBP; and echo "b138mp is on the stick"
umount $psp
```

**After each test, pull the log** (change the number each time; the multiplayer build's log is in `halomp`):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b138_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
find $psp/halo/shots -type f -delete
umount $psp
```

**The tests** (the dress rehearsal: play it as a player would):

| # | How | Play | Stop | What it answers |
|---|---|---|---|---|
| 1 | Boot from the XMB | Time from the XMB to the menu answering; look at the ring for sparkle | The menu takes input | The start-up; showcasebias's look |
| 2 | Campaign → The Pillar of Autumn (a10), from the menu | Play a10 through to its end, so it loads a30 | a30's first view | A whole level by hand; the chant, the trail and the settle between levels |
| 3 | Carry on into a30, then pick b30 (The Silent Cartographer) from the menu, or let the ladder do it | Look at the walls' decals (stencils) after the level changes | 5 min of b30 | The heap fix: stencils after level changes |
| 4 | The multiplayer build (XMB → halomp) | Join a `[C]` server of Milenko's, play a game | 5 min, or a drop | The client-only build by hand (the hotspot and the server as `multiplayer_setup.md`) |

Anything that reads wrong to you as a player (a menu, a message, a step you had to guess) is a finding for `go_live.md`'s guide.

**Turning things off** (launch menu keep words): `showcasebias0`, `vrammodels600`, `nolightsframe`, `debugvertexcall`, `objecttimeall`, `bonusold`, `chantheap`, `seethroughheap`, and b137's (`chantoff`, `settleoff`, `loadingoff`, `loadingplain`, `nolevelvram`, `glownoclamp`, `bootvariants`, `logeachline`, `pathscan`, `loadwhole`).

## The lab's tests

- **The ladder** on the PSP-3000 (lab 39056277015), against b137's (39037203738, PSP-1000; the two PSPs agree within ~3% a map).
- **LD1's A/B** on the PSP-3000 (lab 39056281065): b30, `keepsets "- | nolightsframe"`, repeat 2.
