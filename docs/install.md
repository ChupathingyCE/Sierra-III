# Testing, installing and pulling logs

Moved from CLAUDE.md on 2026-11-18 (the rules reconcile, Sheepdog01 2026-11-18 12:30). The stick's safety rules stay in CLAUDE.md; this is the how-to. The pipeline's shape: `testing_pipeline.md`.

## Testing

- **Emulator:** PPSSPP at `tools/ppsspp_BASE_COMMIT.txt` with `tools/ppsspp_local_patches.diff`, built as `PPSSPPHeadless`. It gives you:
  - `PPSSPP_FAT=1`: PSP-1000 memory;
  - `-i`: the strict interpreter (range, unaligned and FPU checks);
  - `PSP_DCACHE` / `PSP_ICACHE`: the cache model, with exact tick counts.

  `tools/emu.sh LABEL` does one run. It is driven by environment variables (map, keep words, input script, route, strict mode, wall time) and summarised by `tools/strictfilter.py`. The Pelican flight into b30 is the fair A/B stretch; beach battles vary ~10% between runs.
- **The lab's runners:** builds, bakes and emulator runs (many at once, as batches) can go to Sheepdog01's desktop through the lab repository's workflows. Its README describes them; maps live on Sheepdog01's machines and, on GitHub, only in the private lab repository, never here.
- **Hardware:** Sheepdog01 runs the build and sends back `halo/log/` (`log.txt`, `profile.txt`, `crash.txt`, shots). `tools/hwreport.py` reads the `hwprofile` lines.

## Installing (Sheepdog01's machine, fish)

`pspmount` mounts the Memory Stick as `$psp`. Unzip the build zip into `$psp`, then `umount $psp`. Rules for anything that touches the stick:
- Delete only within `$psp/halo`, `$psp/PSP/GAME/halo`, and the multiplayer build's `$psp/halomp` and `$psp/PSP/GAME/halomp` (Sheepdog01, 2026-10-10 and 2026-10-11).
- Leave ARK-4's files and `overconfig.txt` alone.
- Never format or wipe the stick.

To quit the game: HOME → ARK-4 Kommander → Exit.

**Logs come off the stick into `$HOME/psp/logs/`** (Sheepdog01's rule, 2026-10-10), one `.tgz` per run, named for the build and the test: `mkdir -p $HOME/psp/logs; tar czf $HOME/psp/logs/b114_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots`. Pull after each run: the last 20 launches' logs stay on the stick as `log/NNN_log.txt` (the `--exclude` leaves them out of the archive), so a missed pull can still be recovered. A network test's tarball also carries the server's `debug.txt` (Sheepdog01's rule, 2026-10-11), and the multiplayer build's log is in its own folder from mp4: `tar czf $HOME/psp/logs/mp4_test1.tgz -C $psp/halomp log -C ~/games/halo-server debug.txt`. Give each test its stop condition and an honest time: a bench that waits for game time runs slower than the clock at low frame rates.
