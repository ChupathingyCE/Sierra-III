# The tick's work, system by system

Sheepdog01 2026-11-09 16:19: "everything needs to be questioned, analysed for gains, and justified for its cost. we need to be sure we map the existing work out, where it's been priced, and where else it might gain." This page is that map, kept current by the bottleneck session. Nothing here is ranked yet: ranking comes once every system has its section (the lab's `coord/handoff/tick30_2026-11-09.md`).

Labels: [M] measured on a PSP (device named), [E] emulator, [D] read in code, [U] inference. **Every price carries its scenario** (device, map, stretch, build): a price holds in one scenario, and a stop on one stretch isn't a stop everywhere.

How each section is built:
- **What it computes, and who reads it**, cited in the engine (ChupathingyCE at the pin in `upstream/pins.env`) or the port.
- **Its PSP cost per heavy stretch** (b30 flight and landing, b40 snowfield and cliff, d20, c20, a30), as shares within one run.
- **Every lever tried on it**, with where each was priced (device × map).
- **Where else it might gain**, and **whether its algorithm suits this machine** (in-order, 16 KB caches, misses at 55-71 cycles, one miss in flight, a second CPU, a VFPU).

Reads are delegated to DeepSeek and checked by hand; the checked reads live in the lab's `research/manifest/tickmap_<system>_*.md`.

## The systems at a glance

b30's flight, the tick by function (PC sampler, PSP-3000, sierra-hw 42114568213, b151's code) [M], in ms of ledger1's 84.7 ms of tick a frame (two ticks):

| System | Share of the tick | ms a frame | Section |
|---|---:|---:|---|
| Collision and BSP | 25.6% | 21.6 | §1 (below) |
| AI on the main CPU (its rays count as collision; the thinking runs on the ME) | 16.5% | 14.0 | to come |
| Node matrices and animation | 11.3% | 9.5 | to come |
| Units, bipeds, vehicles, weapons (their own code) | 9.3% | 7.8 | to come |
| Other object code | 7.7% | 6.5 | to come |
| libm and memcpy | 5.3% | 4.4 | to come |
| Effects and particles | 3.4% | 2.8 | to come |
| Tag lookups | 2.9% | 2.4 | to come |
| Audio on the main CPU | 2.4% | 2.0 | to come |
| Scripts | 1.6% | 1.3 | to come |
| The rest (spread thin) | 14.5% | 12.2 | to come |

By object kind on the same stretch (objcost, PSP-1000, sierra-hw 42141971439) [M]: object updates 34.9 ms a tick as instrumented, bipeds 24.3 of it, the moving bipeds 23.6 (~24 a tick at ~970 µs). By band (S4's census, PSP-1000, sierra-hw 42672087890) [M]: far and out of the fight 30% of biped time, in the fight at 20-40 units 36%, at 10-20 units 19%.

**One consequence already** [U on M]: the AI's thinking runs on the ME (AIME), so "the AI thinks less often" frees mostly the ME, which is also over at the target (`redesign_tick_target.md` §2a), not the tick. The tick's ground is the moving bipeds' updates and the collision they call.

## 1. Collision

### 1.1 What it computes, and who reads it [D]

Four families (the lab's `research/manifest/tickmap_collision_code_2026-11-09.md`; line numbers there are the port's tree after fixups):

| Family | Entries | Main tick callers, and what they do with the answer |
|---|---|---|
| **Sphere and feature queries** | `collision_get_features_in_sphere`, `collision_bsp_test_sphere`; the port replaces the level walk with its own (`psp_sphere_walk`, `psp_bsp3d_test_sphere`, `psp_bsp2d_test_sphere`, `psp_fixups.py`), with the BSP's top in the scratchpad (SCRATCHBSP) | The bipeds' movement (~90% of the walks, 36 a tick on b30 [E counts]): `collision_move_pill`, `collision_fix_pill`, the crouch-stand test; mass-point physics' ground plane (`compute_ground_plane`, engine `source/physics/physics.c:684`), whose answer becomes the mass point's ground plane, depth and material |
| **Vector tests** | `collision_test_vector` → `collision_bsp_test_vector` → the recursive walk; a port-only quiet twin for the ME's AI rays | AI line of sight and fire (`ai_test_line_of_sight` gates long rays on cluster visibility first), projectiles, vehicles' swept mass points and antigrav probes, point physics, melee, damage |
| **Point tests** | `bsp3d_test_point` (via `scenario_location_from_point`) | every mass point's location each physics update; `collision_test_point`'s cluster lookup; the camera's leaf |
| **Cluster reconnect and partition** | `cluster_partition_reconnect` → `structure_clusters_in_sphere` | every `object_translate` (`object_reconnect_to_map`), the queued non-collideable pass, object lights; the cluster lists it builds are what the object halves of the queries above walk, and what `objects_in_sphere` uses for biped and vehicle contact |

**Multiplayer boundary:** collision's answers are among the state a server consumes (`redesign_s.md` §4), so collision stays exact, or campaign-only and proven to play the same.

### 1.2 What it costs on the PSP

| Stretch | Collision's share | Source |
|---|---|---|
| b30 flight | 25.6% of the tick | PC sampler, PSP-3000, sierra-hw 42114568213 [M] |
| b30 beach | 25.3% of the tick; 9.9% of drawing (sound obstruction, flares) | PC sampler, PSP-1000, sierra-hw 42000782600 [M] |
| c20 hands off | 21.4% of the tick | PC sampler, PSP-1000 (lab findings, the b40tick pcsnap row) [M] |
| b30 landing, b40 snowfield and cliff, d20, a30 | **not measured by function** | gap |

**By family on the flight** [U on M × E counts]: the sampler's top functions time the families, and which code calls each is a code fact the emulator counts truly (lab findings `collision_tick_share`, api-job 40135729137; collcensus1 sierra-batch 42010881954).
- Sphere walks (`psp_bsp3d_test_sphere` 6.0%, `psp_bsp2d_test_sphere` 2.5, `collision_get_features_in_sphere` 2.5) ≈ 11% of the tick, so the moving bipeds' own collision is ~10%.
- Rays (`collision_test_vector` 2.1% and the recursive walk) ~2-4%.
- Reconnects (`cluster_partition_reconnect` 2.0% and the point walks) ~2-3%.
- Collision holds ~14% of the flight's data misses (`redesign_tick_target.md` §3.9) and 11-27% of the tick's data misses on the slow maps (PACKFEAT's record).

**Still to confirm on the PSP:** that a sphere walk costs the same per call whichever code calls it (the split above assumes it).

### 1.3 Every lever tried on it, and where it was priced

84 levers sit on collision's ground (the lab's `research/manifest/tickmap_collision_levers_2026-11-09.md` has each with its device × map, result and stop reason; checked by hand against `levers.tsv`).

- **On by default, priced on the PSP:** LD1 (attached lights once a frame: b30 tick -5…-7%, PSP-3000), CW (collision loops inlined: b30 drive tick -7%, PSP-3000), UVB (vehicle physics early-out: a30 fight tick -8%, PSP-3000), P2NC, PWSL, SPHSL, RECONLIST (reconnects queued, slack-answered, made in place: each c20 tick -1…-5%, PSP-1000), BSPR (a standing biped's last answer: b30 flight -1.3 ms a frame), SMEMO, SCRATCHBSP, NC, NR, NS, T1, OBSRAY, WXDEAD, AS1.
- **Stopped, priced on one PSP map only** (27 in all; most on b30's cinematic or flight, or c20): among them S2REUSE (the level half reused: b30 flight +0.1 ms a tick, neutral, PSP-3000), BSPDFS2 and BSPLAYOUT (the BSP's order: noise on b30), BSPW (exact faster walks: b30 neutral), MWMASS, CLUSTERPACK, BEYONDHIT, SUPRING. A stop on b30 isn't a stop on the landing, b40 or d20, whose collision share is unmeasured.
- **Priced only in the emulator** (12, which today's rules don't let stop anything on time): X9, DC, MS, BP, RT1, NMS, DAL, ARMPASS, DRAWRAYSME, E3, FLAMEDECAL; and STILLSWEEP was never priced at all.
- **Proposed, never built:** RD (cook-time bounding spheres), RM (4-wide VFPU bounds tests), RQ, RT1 (cluster lists that carry the object's type: ~6,000 objects walked a tick on b30's beach [E]), RT2, RH3, RH4, SC1 (a coarse index for still objects: ceiling 0.70-0.76 ms a tick on b30 [E]), BSPDFS, CSPK, LSAME, SPREP, PACKFEAT, SPX.
- **Declined by Sheepdog01:** WORLDVIEW (11-07: not exact).

### 1.4 Where else it might gain

- **The stretches never priced:** the landing (in-fight bipeds 58% of biped time), b40's snowfield and cliff, d20. Collision's share there is unmeasured, so every collision stop priced on b30 alone is open there.
- **The emulator-only and never-priced levers** above each need a PSP pair before they count either way.
- **Plays the same:** Sheepdog01 16:07 allows it where multiplayer stays compatible. Collision's answers are server state, so it applies only to campaign-only paths proven by side-by-sides and the outcome census.

### 1.5 Does the algorithm suit this machine? [D/U]

- **Every query starts at the BSP's root.** The level half is cheap on the PSP (two S2 pairs read ~0; the top nodes are in the scratchpad), so the cost is elsewhere: the object half (walking each cluster's collideable list, ~6,000 objects a tick on the beach [E]), the vector tests and the reconnects.
- **The broadphase is per cluster:** an object's reconnect rebuilds its cluster references, and every query walks the lists of the clusters it touches, filtering by type and flags object by object. RT1 (types in the lists) and SC1 (still objects indexed apart) attack that filtering and were never built.
- **The same position walked twice** [D, engine `physics.c:837` then `:843` → `:684`]: each mass point's physics update does a point walk for its location, then a feature query from the root at the same position, so the sphere walk retraces the point's descent until the sphere straddles a plane. Vehicles and props only, not bipeds; worth unsized.
- **Misses:** collision's walks are pointer chases over BSP nodes, surfaces and cluster lists, with one miss in flight; PACKFEAT (surfaces with their edges and vertices together) and CSPK (packed cluster lists) are the layout answers, unbuilt.

## 2-11. The other systems

To come, each built the same way: AI on the main CPU, node matrices and animation, units and vehicles, other objects, libm and memcpy, effects and particles, tags, audio, scripts, the rest.
