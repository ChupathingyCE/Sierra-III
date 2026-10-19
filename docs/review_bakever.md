# Review: the bake check (go_live 3.4), branch `bakever`

For the build's review walk-through. Labels: [E] emulator, [D] code.

## What it does

A player's maps come from the bake bundle released beside each build. If a build reads maps from a bundle whose format it doesn't know, it draws them wrong or crashes far from the cause. Now the bake stamps its format and the game checks it before anything reads a map.

## The changes, and why

- **`bake/bake.py`**: `BAKE_FORMAT = 1`, the one number for "what a build needs of the maps". `write_stamp()` writes `halo/bake.txt` (`format`, `bundle`, `commit`, `baked`) after the maps, beside `chant.adp`. `bundle_label()` names the bundle: the release tag or CI ref from `bundle.txt`, else `dev-<commit>` from a checkout. The rule for raising the number sits beside it: raise it whenever a build would read the previous bundle's maps wrongly (a pack's layout, Invader's flags or tag space, anything a release says needs a re-bake). Builds that read the same format share a bake, so the stamp isn't tied to each commit.
- **`bake/bundle/assemble.py`**: writes `bundle.txt` (the bundle folder's name and the commit: `GITHUB_SHA` in CI, whose Linux container may have no git). **`smoke.py`** checks the stamp on every OS (the bake-bundle workflow on this branch: all three green).
- **`psp_build.py`**: reads `BAKE_FORMAT` from `bake/bake.py` and passes it as `-DPSP_BAKE_FORMAT` to the new unit alone (and never reuses its object under `PSP_REUSE`, since the number lives outside the source). The unit is cold (`-Os`, no PGO counts), so a release's strict PGO build isn't disturbed.
- **`psp/src/psp_bakecheck.c`**: `psp_bake_check()`, called from start-up right after `keep_load()` (the keep word must be readable) and before the boot screen. Same format: one log line, on. Otherwise one of three screens, on the debug screen (the GE isn't up): "YOUR MAPS NEED BAKING AGAIN" (older format, no stamp, or an unreadable one), "THESE MAPS ARE NEWER THAN THIS BUILD", or "NO MAPS FOUND" (no stamp and no `ui.map`). Each names the bundle the maps came from and the build's own (`b%d`), says what to do, and quits on START (pressed after a release, so a press held from the XMB doesn't quit unseen). The lines also go to the log. A scripted run (`input.txt`) saves the screen as `shots/bakecheck.bmp` and quits after 3 s, so test runs end. Keep word `bakeany` skips the check. Both builds read the stamp in `ms0:/halo` (`PSP_MAPS_HOME`), as they share the maps.
- **`psp/src/psp_platform.c`**: the one call (unconditional, so `main`'s branches and its PGO counts stay), and `psp_exit_game()` now in both builds (it was multiplayer-only).

## Proof [E]

- Emulator runs of this branch on an older bake (b136's) with stamps written for the test: a matching stamp boots a10; older (format 0), newer (format 2), none, an unreadable stamp and the multiplayer build each show their screen and quit in 3 s; a stampless bake given `bakeany` boots as before.
- A real bake with this branch's scripts (ui and a10) writes `format: 1`, `bundle: bakever`; the build boots on it, and shows the screen when that stamp is edited to format 0.

## Not covered

- "NO MAPS FOUND" is untested in the emulator (the emulator harness won't start a run without map files) [D].
