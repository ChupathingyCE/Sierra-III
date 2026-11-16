# Multiplayer: v0.7.5a anchor check, and the memory a client-only build can drop (2026-10-14)

Two desktop checks from the custom-maps planning session. No builds made, nothing committed. Labels as in CLAUDE.md: [D] read in code or data, [U] inference.

**Decisions they rest on (Sheepdog01, 2026-10-14):**
- Phase 1 of multiplayer = parity with vanilla Xbox multiplayer (13 maps, 5 game types, 16 players) plus ChupathingyCE's multiplayer layer. Custom maps are phase 2.
- **The multiplayer build never hosts.** PSP hosting belongs to the co-op build, which is the project's final phase (the campaign and multiplayer clients are proven separately first, then merged).
- A PSP never caps anyone else's game. The PSP-friendliness check is a soft gate in the PSP's own server browser, before joining (Steam Deck Verified-style tiers). If the PSP later speaks Delta Peer, it always sends the opted-in flag.

## 1. Anchor check against ChupathingyCE v0.7.5a [D]

`PSP_FIXUPS_CHECK=1 PSP_MULTIPLAYER=1 python3 psp_fixups.py <tree>` on clean copies of each engine version:

| Port branch | on v0.7.4e (`f8abf216`) | on v0.7.5a (`5c1a363c`) |
|---|---|---|
| `w122` (`c44133e`, pinned to v0.7.4e) | clean | **1 anchor** |
| `mp5` (`20074be`, pinned to `b78e6cf`) | 2 anchors (the known `errors.c` pair, fixed in `w122`) | 3 anchors (the same pair + the new one) |

- **The new break:** `port/linux/src/platform.h`. Upstream `64b77c17` widened `#ifdef HALO_ANDROID` to `#if defined(HALO_ANDROID) && !defined(HALO_64BIT)` for its native 64-bit Android build. A re-anchor, nothing more.
- **All 7 `FIXES_MULTIPLAYER` anchors match on v0.7.5a.**
- **v0.7.5a's map-identity code (network version 29) doesn't reach the PSP:** the whole-file hash in `cache_files_map_version` sits under `HALO_CUSTOM_EDITION`, which the PSP build leaves out, so stock maps keep version 0 as before. The new Delta wire calls in the networking files (`delta_peer_advertised_wire*`) sit under `HALO_GAME_BROWSER`, also left out. So no new link dependencies at the text level. A compile and link (`sierra-build` on the desktop) is the real check.
- **The PSP's network version is its own, not the engine's:** `psp_limits.h` hard-codes `HALO_PORT_NETWORK_VERSION 24` (accepting 11–24) on both branches, while v0.7.5a's `halo_port_limits.h` says 29 (accepting 11–30). A rebase doesn't move it. Moving to 29 is a deliberate edit, made together with the rebase that brings versions 25–29's code.
  - Codebase note: two sources of truth for one number. Worth either deriving the PSP's from the engine's header, or a build-time check that they agree, so the next bump can't be missed.

## 2. Memory the client-only build can drop [D for sizes; U for host-only by name and comment]

From mp1's link map (the lab's results, `sierra-build/38100266481`; program 6.16 MB, bss 2,673,500 B, heap 4,352 KB). **The heap is whatever the program leaves of the user partition, so every KB of bss here is a KB of heap.**

| Group | Size | Confidence |
|---|---:|---|
| **`network_distributed_unban`'s `static char contents[1 MB]`**: the dedicated server's `sv_unban` rewriting its bans file | **1,024 KB** | [D] server-only; never reached by a client |
| Co-op units (`network_coop.o`, `network_actors.o`, `coop_enemies.o`; `host_sent_*`, `host_/client_actors`…) | 276 KB | [D] co-op is the final phase; needs answer stubs as the campaign build has |
| Host's damage records (`network_damage.o` except the client's `damage_reports`; already sized for 16) | 158 KB | [U] by comments ("the host: …") |
| Per-machine send batches (`distributed_batches`, 129 senders; a client sends one batch, to the host) | ~151 KB | [U] needs a code change: the array is indexed by sender |
| Host player queues (`update_server_globals`) | 128 KB | [U] by name |
| Host object records (`objects_host_*`) | 94 KB | [U] by name |
| Late joiners' copies (`late_joiner_players`) | 64 KB | [D] host-only by its comment |
| Host distributed states (`distributed_host_*`) | 18 KB | [U] by name |
| **Total** | **~1.9 MB** | Code (text) on these paths is extra |

- **Against mp5's 4,152 KB heap,** the bans buffer alone is +25%. The whole list is up to ~45%, room the player-count work can spend on objects and bodies.
- **The bans buffer is the cheap, certain one:** an `#ifdef HALO_PSP` fixup sizing it to 0 (or leaving the function out) in `port/linux/game/network_distributed.c`, multiplayer build only. The campaign build doesn't compile that unit.
- **The rest wants a review pass** before cutting: confirm each array is only touched on host paths, then size it to 0 or 1 under `PSP_MULTIPLAYER`. That's the same pattern as the existing `PSP_HOST_MAXIMUM_NETWORK_*` fixups (`psp_limits.h`), which then become 0/1 instead of 16.
- Re-measure on the next multiplayer link map; mp4/mp5 were built in the cloud session, so mp1's map is the newest on the results branch.

## Open unknowns (from the session, for the master list)

Phase 1: the load generator on the desktop (N headless ChupathingyCE clients with `network_test = "join"` and scripted bots); network bytes per tick against N; Delta Peer's cost on the PSP; the rig's user partition at 24 MB (the `hwprofile` partition line); the player-count ladder and Wi-Fi throughput on hardware.

Phase 2: CE map sizes against the 6 MB window (needs maps from Sheepdog01's contact); bake time per stage; stock/custom asset split; DXT passthrough on the GE ever measured?; TLS 1.3 (pspkit-https, wolfSSL, GPL-2.0, emulator-tested only) on hardware.
