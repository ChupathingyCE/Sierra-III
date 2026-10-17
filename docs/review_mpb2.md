# Review: mpb2, internet play made to explain itself (branch `mpbrowse`)

What changed since mpb1 (`b39d693`), in the order to read it.

## Why

- **mpb1 on the PSP-1000 (Sheepdog01, 2026-10-16, 00:19) [M]:**
  - ONLINE GAMES opened, the PSP joined the hotspot, and internet play picked its two brokers.
  - Then nothing until "0 public games", every 10 s.
  - The stack's pool showed sockets open (62 KB at most in use), so it was trying.
- **The lobby at that hour [D]:** the lab's new `lobby-check` read 51 public games on `opence.milenko.org` (lab run 38858917393).
  - Network versions ran 11–30; the PSP joins 11–30.
  - Several were on Blood Gulch, the one stock map baked so far.
  - So there were games to show, and the fault is on the PSP's side.

## 1. What internet play is doing, in the log (`FIXES_INTERNET` #report, #clock; `psp_online.c`)

Every 10 s while ONLINE GAMES is up, by hand or hands-free:
- `online games: brokers:` each broker's state and failures so far: idle, connecting, awaiting MQTT's answer, or ready.
- `online games: lobby:` the listings heard and shown, and why any were turned away: unreadable, another version, wrong key, off the clock, bad signature.
- `online games: this PSP's clock:` in UTC.
- The games, as before.

`psp_net.c` also logs each broker's looked-up address (as a tag, as `log_address` does) and any connection that fails at once, with its errno.

## 2. The PSP's clock set from the hosts' (`FIXES_INTERNET` #clock)

**The problem:** a listing carries its host's Unix time, and is dropped when it's more than 10 min (kept by a broker) or an hour (live) from this machine's clock. A PSP's clock is whatever its owner set. mpb1's log files carry 1985 dates, though whether that's the PSP's clock is unproven [U].

**The fix:**
1. The first live listing whose signature holds sets the PSP's offset from the hosts' clocks.
2. The offset is logged.
3. The windows then apply to the corrected clock.

**The risk:** a host with a wrong clock could set the offset wrong. The desktop's own windows take that risk for any host far off [U].

## 3. The last socket error kept per thread (`psp_net.c`)

**The race:** one shared variable for the game's thread and internet play's.
1. The p2p thread's non-blocking broker `connect` sets WSAEWOULDBLOCK.
2. The game's per-frame `select` can clear it in between.
3. `p2p_signal.c` then reads 0 and closes the connection as failed, without a word in the log.

**The fix:** each thread now gets its own slot by thread ID, claimed under a lock (the Allegrex has no compare-and-swap). Whether this race is what stopped mpb1 is unproven; it is real either way [U].

## 4. `onlinedoneN` (`psp_online.c`, `psp_platform.c`'s `psp_exit_game`)

- **What:** ends the game N s after the screen opens, so an emulator run ends at its question (the lab's 2026-10-15 rule). The menu's game time stands still, so `exitatN` never comes.
- **Never on a PSP:** a game that quits by itself leaves it hung.

## Reverted along the way

**`2a296dc` → `9042ac8`:** I first read the emulator's EINVAL as an address-layout bug (BSD against Winsock). The disassembly showed internet play already passes Winsock's layout, so I reverted it.

The emulator's EINVAL is the lab's PPSSPP patch binding every connecting socket to the instance's loopback address. The lab now has a `halo-net` variant for online runs only.

## The campaign build

Code byte-identical to w131's but for mp5's crash-screen string, as in mpb1.

## Open

- What mpb2's log says on the PSP-1000.
- In the emulator, joining can't be proven even with `halo-net`: the tunnel's UDP sockets are bound to 127.0.0.N, so STUN and hole punching stay on the machine.
