# Game modes across Halo, and what each kind would ask of the PSP (2026-10-14)

A wargame, not a plan: which modes the series has had, how each kind works mechanically, and what it would cost a PSP client. The aim is to know what to leave room for. Labels: [D] read in code, [K] general knowledge of the games (not looked up), [U] inference.

## 1. How Halo CE's engine holds modes [D]

- `enum game_engine_type` (`game_engine.h`): none, **ctf, slayer, oddball, king, race** (usable 1–5), then `terminator` and `stub`, reserved and unused.
- `game_engine_bomb.c`, `game_engine_defend.c` and `game_engine_terminator.c` exist but are empty: Bungie's file names survived, their code didn't ship. "Bomb" reads like what became Halo 2's Assault [U].
- **Each engine is a 136-byte table of functions** (spawn, score, player added, game ending, client messages…). The game variant (104 bytes) names the engine by index, plus universal and per-mode options.
- **All five engines' static state: ~15 KB** (mp1 link map). Modes cost memory through what they cause to exist: objects, players, map scripts.
- Online, **the host runs the rules**; clients receive results.

## 2. The modes, by kind [K]

| Kind | Modes (first appearance) | How it works |
|---|---|---|
| **A. Rules over existing things** | Slayer and its variants (CE); Juggernaut (H2); VIP, Infection (H3, official); Regicide (H4); Breakout (H5, rounds without respawns); Attrition, Last Spartan Standing, Escalation Slayer (Infinite) | Scoring, traits (speed, camo, damage, shields), team swaps, waypoints on players |
| **B. Objective objects and zones** | CTF, Oddball, King of the Hill (CE); Assault, Territories (H2); Headhunter, Stockpile (Reach); Dominion, Ricochet, Extraction (H4); Strongholds (H5); Total Control (Infinite) | Objects to carry or plant, zones with capture progress, objective markers, timers |
| **C. Vehicles and checkpoints** | Race (CE); Rocket Race (later games) | Vehicles, ordered checkpoints, laps |
| **D. AI in the match** | Firefight (ODST, Reach, Infinite); Warzone (H5: players against players and AI bosses, 24 players); Spartan Ops (H4, episodic co-op) | Waves or bosses run by the host; needs AI content in the map |
| **E. Phased and asymmetric** | Invasion, Generator Defense (Reach) | A state machine of phases, classes per side, objectives that unlock |
| **F. Loadouts, abilities, progression** | Armor abilities (Reach); loadouts and ordnance (H4); REQ cards (H5); equipment (Infinite) | New client-side items, UI and assets |
| **G. Meta systems** | Big Team Battle (larger counts); Forge (H3+, live map editing); Theater (H3+, recorded films); custom game browsers | Not modes but systems around them |

**On Halo CE PC specifically**, the community has long run modes as **server-side mods** (Zombies/infection, Juggernaut, gun game and many others through server scripting) [K]. That's route 1 below, and the cheapest for the PSP.

## 3. What each kind costs a PSP client [U]

| Kind | Client code needed | Memory and CPU | Verdict |
|---|---|---|---|
| A | Little, if the HUD messages and waypoints that exist suffice; traits need the client to show them | Same as the base game | **Cheap.** Mostly server-side |
| B | Zone visuals and capture-progress HUD, unless done with existing markers | A few objects; zones are map data | **Cheap if the client primitives exist** |
| C | None (Race is native) | Vehicles: the heaviest objects | **Native** |
| D | AI sync's client half (`network_actors.c`, ~31 KB) | More bodies: the player-count curve again; AI brains stay on the host | **Affordable as a client; needs content** |
| E | A state machine's HUD; classes | Map content heavy | **Expensive in content, modest in memory** |
| F | New items, UI, assets per client | Assets in memory on every client | **Deferred** (same reason as armor customization and voice chat) |
| G | BTB: none (player count). Forge-made maps: none (they're maps). Live Forge editing, Theater: large | BTB: the player ladder; live Forge: object churn and editing UI; Theater: storage and memory | **BTB via the badge; play Forge maps; no live Forge or Theater on the PSP** |

## 4. Three routes a new mode can arrive by

1. **Server-side logic over existing features.** No PSP update, no PSP cost. Best case.
2. **Map scripts** (HaloScript in a custom map). Costs script threads and nodes per map, caught by the custom-map fit check.
3. **New engine code.** Small, but every client needs it, so the PSP has to update before it can join.

## 5. What to leave room for (rules for the design)

1. **An unknown engine index is refused cleanly, never trusted.** The pre-join badge reads the mode from the advertisement and marks a mode the PSP lacks Unsupported.
2. **Generic client primitives beat specific modes.** Waypoints, objective markers, zones with progress, timers, player traits and team swaps let server-side logic express most of kinds A and B. One primitive set could bring many modes at once, and it's worth raising upstream, since it helps every console port too.
3. **Memory headroom pays for kinds B, D and G.** The ~2.7 MB the multiplayer build can win back (`mp_map_survey_prelim.md`, `mp_v075a_and_host_memory.md`) is the budget for more objects and bodies.
4. **When trimming host-only netcode, keep AI sync's client half,** for any mode with AI in it.
5. **Phase order:** A–C belong to phase 1 parity (the five modes are kinds A–C); D comes after co-op's networked-AI work; E and F wait; G is BTB by badge only.
