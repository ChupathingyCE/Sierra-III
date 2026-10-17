# Build 136: wake-on-write, the front door, the menu showcase, the PSP glyphs, multiplayer's fix (2026-10-17)

`review_b136.md` lists every change and why. **New bake:** `b136` (glow maps and the menu showcase need it; below).

`halo_psp_b136.zip` (SHA-256 `305db5ac8d96bdb14f46e1fd2aba26ab5d3bc7d9565aebc22550991bb83f0e48`), EBOOT `23ab22506ce42bfd202e484ec6ddae4a0b3e4f04f949c41b0da980139357232a`: built from commit `e41a305` on `w136` (lab sierra-build 39019698297). Heap 5,584 KB (b135: 5,644). **Its own profile** (`campaign-b136`, 13 runs at `6d4807e` on the b136 bake), checked against the code (`PSP_PGO_STRICT=1`), and b132's code order. Release check (lab 39018086383): play and both seeds identical to b135 at every tick on d20, a50 and c20 (12,000 each) [E]; b30 and c40 shots right; the menu showcase and a10's glyph prompt right; strict clean. For the archive: `releases.tsv` has the line; `publish-releases.sh tags` and `archive` on your machine.

**What's new for you:**
- **The game starts on its own main menu**, with its music, as on the Xbox. Hold **L** as it starts, or put `ms0:/halo/menu.txt` on the stick, for the launch menu. The menu still takes about 27 s before it answers the buttons (the saved-game check; the next start-up fix).
- **The main menu as a showcase:** its ship, planets and ring at full size and colour, with glow maps (the ring's white band is gone, and its clouds no longer clip to white). The menu's text and buttons are still soft; the renderer session has the fix for the release build.
- **The PSP's own button glyphs** on the HUD prompts and menus; **crouch toggles**; **reload on a tap, action on a hold** (`review_controls.md`).
- **Wake-on-write:** things at rest are no longer checksummed every tick; play is identical, b30's frames 5–9 ms faster on the PSP-1000 (b136's ladder below).
- **Multiplayer build:** the drop from public games is fixed (`netpace5`).
- Off, for your look: `dithershift`, the levels' banding fix (needs a `PSP_DITHERSHIFT=1` build; ask for the zip).

SPEED_LINE

## On the PSP

**Install the build and the b136 maps** (the maps are new: glow maps everywhere, and `ui_showcase.ptex` for the menu):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b136.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b136 is on the stick"
set maps ~/psp-bake/b136/halo/maps
cp -v $maps/ui.* $maps/ui_showcase*.ptex $psp/halo/maps/
for m in a10 a30 a50 b30 b40 c10 c20 c40 d20 d40
    cp -v $maps/$m.* $maps/$m"_track.ptex" $psp/halo/maps/
end
for f in init.txt keep.txt input.txt; rm -f $psp/halo/$f; end
find $psp/halo/shots -type f -delete
umount $psp
```
The copy is 55 files. The log's first line should say `bring-up build 136`. Clearing `init.txt`, `keep.txt` and `input.txt` takes the rig's last job off the stick, so the game boots to its main menu.

**After each test, pull the log** (change the number each time):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b136_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
find $psp/halo/shots -type f -delete
umount $psp
```

**The tests:**

| # | How | Play | Stop | What it answers |
|---|---|---|---|---|
| 1 | Boot normally | Watch the menu; when it answers, Campaign into The Pillar of Autumn (a10) and play to the first prompt | The cryo prompt shows | The front door, the showcase and its glow, the PSP glyph on a prompt |
| 2 | The Silent Cartographer (b30), preset **1** (L at start-up for the launch menu) | On the beach, after the first fight: shoot and grenade things at rest (dropped weapons, grenades on the ground, bodies, any vehicle or crate); walk into them | 5 min, or something at rest ignores a hit | Wake-on-write: everything at rest still reacts |
| 3 | Halo (a30), preset **1** | Play from the start into the first fights; try crouch (toggle), reload (tap) and action (hold) | After two checkpoints, or 10 min | The controls, and b135's checkpoint test |
| 4 | b30, preset **15** (`nomeaudio`) | Play a minute, sleep with the power switch for ~10 s, wake, play 30 s, then HOME → exit | The XMB is back, or the PSP hangs (pull the battery) | b135's test 2: with `nomeaudio` the ME never starts. A clean exit points at the ME for the sleep hang |

Do 4 last: it may end in a battery pull. The log reaches the stick before a hang, so pull it once the battery's back.

**If test 2 finds something that ignores a hit:** note what and roughly when, pull the log, and run the same stretch with `wakecheck` added in the launch menu's keep words. Its log names the object and the bytes that changed, which names the missed writer.

**Turning things off** (launch menu keep words): `wakeonwriteoff`, `noshowcase`, `glowflat` (the flat glow), `glowadd` (the glow added, not screened), `crouchhold`, `actiontap`, `xboxglyphs`, `variantswrite`.
