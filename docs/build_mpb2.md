# mpb2: internet play made to explain itself (2026-10-16)

mpb1 plus a log that says what internet play is doing, the PSP's clock set from the hosts', and per-thread socket errors (`review_mpb2.md`). Same screen, same install, same rating.

## The build

| | |
|---|---|
| Name | `mpb2` |
| Source | Sierra-III `mpbrowse` `1780aeb` (the code at `9042ac8` is the same); `PSP_MULTIPLAYER=1`, no PGO |
| EBOOT | sha256 `79a0623bee1c8e8d…` (`tools/build_release.sh`, pinned me-core) |
| Zip | `halo_psp_mpb2_halomp.zip`: `PSP/GAME/halomp/` and `halomp/presets.txt` |
| Heap | 3,724 KB |

## What mpb1 showed

| Where | What | Result |
|---|---|---|
| PSP-1000, Sheepdog01 | `onlinetest`, Blood Gulch on the stick | 0 public games; nothing logged between choosing the brokers and the count [M] |
| Lab `lobby-check` 38858917393 | the lobby read directly, 06:02 UTC | 51 games on `opence.milenko.org`, versions 11–30, several on Blood Gulch [D] |
| Emulator, `sierra-netplay` 38857365803 | mpb2's log | brokers looked up, every connection EINVAL: the lab's PPSSPP binds them to 127.0.0.0 (emulator-only; `halo-net` variant added) [E] |

## Test 1 again (PSP-1000)

**Install (fish):**

```fish
pspmount
unzip -o ~/Downloads/halo_psp_mpb2_halomp.zip -d $psp
echo "nearauto nomeaudio onlinetest" > $psp/halomp/keep.txt
umount $psp
```

**Run it:**
1. Launch Halo MP and leave it on the main menu for about 2 minutes. ONLINE GAMES opens by itself.
2. Pull the log:

   ```fish
   pspmount
   tar czf $HOME/psp/logs/mpb2_test1.tgz -C $psp/halomp log
   umount $psp
   ```

**What the log answers** (`online games:` lines, every 10 s):
- `brokers:` "ready" means connected. "idle, N failed" with `network: a connection to … failed at once (errno N)` means the PSP's connection itself fails.
- `lobby:` "heard N" means listings arrived. "turned away" says why any were dropped.
- `clock:` the PSP's clock in UTC.
