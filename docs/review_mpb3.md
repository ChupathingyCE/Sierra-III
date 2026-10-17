# Review: mpb3 (branch `mpbrowse`, `7f0dc04`)

mpb2 (`review_mpb2.md`) plus what the first emulator runs against the live lobby showed. Every change is in `psp/src/psp_online.c`, `psp/src/psp_internet.c` and `psp_fixups.py`'s `FIXES_INTERNET`. The campaign build's code is unchanged.

## 1. One row per game (`psp_online.c`, `fetch_games`)

- **Why:** a host may list one game once for each network version it speaks; the lab's `lobby-check` saw one server under twelve versions. Each listing is real, with its own invite.
- **What:** rows with the same name, map, game type and player counts are folded into the first. The lobby already orders by players, then open games first, so the first is the one to show.

## 2. Co-op games named as such (`psp_join_blocker`)

A game on a campaign level (a10…d40) is a host's network co-op. It now reads "Co-op: not on the PSP yet", not "Custom map".

## 3. The clock offset, by agreement (`FIXES_INTERNET` #clock)

- **mpb2's rule:** take the first live, signed listing outside the window as the PSP's clock error.
- **What went wrong:** in the emulator, whose clock is right, a host three hours off moved it.
- **The rule now:** the offset is taken only while no listing has yet been in time, and only once three live, signed listings agree on it within an hour. A PSP with a right clock never moves; one far off moves after three listings.

## 4. The map check through the firmware (`psp_internet.c`'s `psp_file_exists`)

- **Why:** `fopen` in a game unit goes through the engine's file layer, and in the emulator found no maps with Blood Gulch on the stick.
- **What:** `sceIoGetstat`, from a platform unit that has the SDK's headers.

## 5. The hands-free join (`psp_online_test_update`)

- **Dedicated first:** it picks a joinable dedicated server first, then any joinable game. A server's port is usually open, where a player's home router may block the tunnel.
- **A longer wait:** `CONNECT_TIMEOUT` is 30 s. p2p.c's hole punching tries for a while before it gives up.

## The lab's side (not in this repo)

- **`halo-net` emulator variant,** for `host=online` runs only. Connections to the internet aren't bound to 127.0.0.N, and sockets bind where the game asks. The batches' emulator is untouched.
- **`lobby-check`:** what the brokers hold, as ground truth.
- **`emu.sh`:** the per-frame polls kept out of `netlog.txt`.
- **`sierra-netplay`:** `runner=build`.

## Open

- **The tunnel on a real network:** proven on the PSP-1000 (`build_mpb3.md`, lab run 38918999662): it joined a 12-player public game, then fell behind.
- **Joining in the emulator:** blocked by the runner's NAT [U].
- **Password games:** need a text-entry prompt.
- **The b132 merge:** `mpbrowse-b132` (`746346b`) holds it. It joined a local ChupathingyCE server and played 4,323 ticks at 30 a second, heap top 2,632 of 3,736 KB [E] (lab sierra-netplay 38865114002). It becomes the line after mpb3's test.
