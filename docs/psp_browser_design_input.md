# The PSP's server browser: what upstream's code gives us (2026-10-14)

Design input for a code-drawn server browser in the multiplayer build (Sheepdog01's choice: option 2, for fluidity and responsiveness). Read from ChupathingyCE at `v0.7.5a` (`5c1a363c`). Labels: [D] read in code, [U] inference.

**On hold** (Sheepdog01, 2026-10-14, CLAUDE.md "Campaign first"): no multiplayer-specific work until the campaign holds a steady 30 ticks a second. This is kept as the input for when that work resumes.

## 0. The finding that reshapes the plan

**Every listed game is joined by its invite, through internet play (`p2p.c`).** Neither list source carries an address: `browser_join(invite)` hands the invite to the tunnel, as an invite link would. The PSP build leaves internet play out today (`multiplayer_plan.md` §1: "LAN only"), and every multiplayer test so far has been system link on the X3D's hotspot.

So **a browser alone doesn't put a PSP into online games.** Joining PC servers over the internet (phase 1's parity target) needs one of:
1. **Port internet play** (§3): portable C, no TLS, ~7,200 lines.
2. **A direct-address path:** dedicated servers that opt in publish an address and open ports, and the PSP joins by system link over the internet. That needs a new list field upstream and each operator's cooperation.

**Decided (Sheepdog01, 2026-10-14): port internet play.** "LAN only" leaves the multiplayer plan.

Why the tunnel exists, in two parts:
- **System link assumes a LAN** (broadcast discovery, Xbox-style addresses). The tunnel fakes one with virtual addresses, so the game keeps speaking system link above it.
- **The internet needs NAT traversal and encryption** whatever the game's design.

## 1. Upstream's pieces [D]

| File | Lines | What it does | Layer | For the PSP |
|---|---:|---|---|---|
| `p2p.c` | 3,245 | Internet play: invites, sessions, the UDP tunnel, STUN, hole punching, virtual addresses for the game | Transport | **Port** (§3) |
| `p2p_signal.c` | 1,970 | Signalling over public MQTT brokers (plain TCP 1883) | Transport | **Port** |
| `p2p_lobby.c` | 900 | **Public game listings over the brokers**, Ed25519-signed (the PC menus' Server Browser) | Model + transport | **Port: the PSP's list source** (§2) |
| `p2p_crypto.c` | 706 | SHA-256, HMAC, ChaCha20-Poly1305, X25519, Ed25519 (Monocypher's) | Crypto | **Port as is** (pure C) |
| `p2p_discord.c` | 399 | Discord invites | — | Leave out |
| `browser.c` | 1,860 | The game list from halo.milenko.org (`/v1/games.txt`), carnage reports, profile claims, Link Profile, key links | Model + transport | **Take `parse_game` / `parse_host` / `parse_roster`** (pure text parsing); the rest is desktop features, later or never |
| `posix_browser.c` | 919 | HTTP + **TLS (Mbed TLS)** client, threads | Transport | **Replace or skip:** only needed for `games.txt` |
| `browser_screen.c` | 2,192 | Online Games: drawn by code over the menus; join flow (invite → wait for the host's game → lobby), map-download offer, Link Profile | View | **Replace with the PSP's own view**; reuse the join flow's states |
| `map_families.c` | 748 | Map names across families (`@ce`, `@md`, `custom_maps\`, hashed names) | Model | **Already built into the multiplayer build** |

Platform needs of what we'd port [D]: BSD sockets (the PSP has them: `psp_net.c`), pthreads mutexes and two threads in `p2p.c` (pspdev's pthreads; b106 replaced its polling condition variables, and mutexes are fine), DNS for the brokers' names [U: the PSP's resolver, to check].

## 2. Two list sources

| Source | Transport | Carries | For the badge |
|---|---|---|---|
| **MQTT lobby** (`p2p_lobby.c`) | Plain TCP 1883; listings Ed25519-signed, so no TLS needed | Name, map, game type, engine, players, maximum, flags (open, under way, teams, password, **dedicated**), network version | **Enough:** map, mode, players, version, password |
| halo.milenko.org `games.txt` | HTTPS | All of that plus rosters, host platform, hosting kind, protocol, machines by platform | Richer: "how many PSPs are in there", official servers |

**Plan:** the MQTT lobby is the PSP's first source (no TLS, already signed). `games.txt` becomes an optional extra later, through pspkit-https or a mirror. Dedicated servers list in the lobby when public (`HALO_DEDICATED_PUBLIC`, default on).

## 3. Cost of porting internet play [U unless marked]

- **Code:** ~7,200 lines of C (without Discord), maybe 150–250 KB compiled. On top of the ~1.9 MB the client-only cuts free.
- **Per-packet crypto:** every tunnel packet is sealed with ChaCha20-Poly1305. In software on the Allegrex, guessing ~30 cycles a byte, that's ~10 MB/s; at a full game's ~120–200 KB/s it's **~1–3% of the CPU**. Measure with a micro-benchmark before trusting it.
- **Joining:** one X25519 agreement and an Ed25519 check per join, likely tens to hundreds of ms at 333 MHz. Once per join.
- **NAT:** hole punching with no relay, as on desktop. A PSP behind a home router behaves like any desktop client. UPnP is optional (the desktop asks the router; the PSP could skip it and rely on the host's).
- **Bonus:** Ed25519 and SHA-256 are also what the signed self-updater and Delta's signed table need, so the crypto is shared.

## 4. The model layer's shape (proposal)

Pure code, no drawing and no sockets, unit-testable on the desktop:

```c
/* one listed game, from either source */
struct psp_listed_game {
	unsigned char source;           /* lobby or games.txt */
	char invite[65];                /* how to join it (p2p) */
	unsigned short name[16];        /* UTF-16, as the game's */
	char map[64];                   /* wire name, map_families.c's */
	short engine, players, maximum_players;
	unsigned short version;
	unsigned char flags;            /* open, under way, teams, password, dedicated */
};

/* the badge: a tier and the reason a player is shown */
enum psp_tier { PSP_VERIFIED, PSP_PLAYABLE, PSP_UNSUPPORTED };
enum psp_tier psp_rate_game(const struct psp_listed_game *game, char *reason, int reason_size);
```

`psp_rate_game`'s inputs, in order of the gates (`mp_gamemode_wargame.md` §5, the badge's tiers):
1. Network version inside the PSP's accepted range → else Unsupported.
2. Engine 1–5 → else Unsupported (unknown modes refused, never trusted).
3. The map baked on this stick (by family and name; later by the bake's identity stamp) → else Unsupported, with the map's name.
4. Password → needs a prompt, not a refusal.
5. Maximum players against the PSP's measured comfort number → Verified or Playable.
6. Later: the server's rotation, if the listing ever carries it.

## 5. The view (code-drawn)

- Opened from the Multiplayer menu, like upstream's Online Games; drawn through `psp_ge` with Halo's fonts from `ui.map`, so the look stays Halo's. The launch menu (`psp_menu.c`) is the in-port precedent.
- The list refreshes in the background; the view only reads the model's snapshot.
- Join flow, as upstream's: invite → tunnel → the host's game appears → lobby. A game rated Unsupported shows its reason and doesn't try.

## 6. Risks of the port, in order
1. **`p2p.c`'s two threads on the PSP:** use the port's kernel-semaphore pattern from b106, not stock condition variables, and set their priorities deliberately.
2. **NAT without a relay:** two strict NATs (some phone hotspots, carrier-grade NAT) can't connect, the same limit desktop players have. A PSP on a phone hotspot is the likeliest case.
3. **Per-packet crypto speed** on the Allegrex: a micro-benchmark on hardware.
4. **DNS:** the PSP's resolver for the brokers' host names.
5. **Following upstream:** the signalling has its own versions (4 since network version 26), one more thing to track on rebases.
6. Tunnel memory: buffers per peer (a client has one peer, the host).
