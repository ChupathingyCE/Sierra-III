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

By object kind on the same stretch (objcost, PSP-1000, sierra-hw 42141971439) [M]: object updates 34.9 ms a tick as instrumented, bipeds 24.3 of it, the moving bipeds 23.6 (~24 a tick at ~970 µs). **CONFLICT (2026-11-16):** the lab's `tick_buyback.md` gives the same stretch and PSP 23.0 ms at 768 µs a biped (29.9 bipeds) by another instrument; neither reconciles the two. By band (S4's census, PSP-1000, sierra-hw 42672087890) [M]: far and out of the fight 30% of biped time, in the fight at 20-40 units 36%, at 10-20 units 19%.

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

**Multiplayer boundary (corrected 2026-11-09):** collision never goes on the wire. Halo's netcode isn't lockstep: the PSP is only ever a client, and collision reaches the host only through the client's movement and hit reports, both checked against loose bounds (3.5 units for its own biped, 3-6 for hits) rather than bit for bit (`hardware_utilization.md` §6, api-job 40731381973 [D]). So collision may differ in the last bits if it plays the same; what must hold is play staying well inside those tolerances, and objects taking the same datum indices at map load. `redesign_s.md` §4's list ("collision's answers" among the state a server consumes) is stricter than the netcode.

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
- **Plays the same:** Sheepdog01 16:07 allows it where multiplayer stays compatible, and for collision it does (§1.1): every collision lever stopped only for not being bit-exact reopens, proven by side-by-sides and the outcome census instead of the state hash.

### 1.5 Does the algorithm suit this machine? [D/U]

- **Every query starts at the BSP's root.** The level half is cheap on the PSP (two S2 pairs read ~0; the top nodes are in the scratchpad), so the cost is elsewhere: the object half (walking each cluster's collideable list, ~6,000 objects a tick on the beach [E]), the vector tests and the reconnects.
- **The broadphase is per cluster:** an object's reconnect rebuilds its cluster references, and every query walks the lists of the clusters it touches, filtering by type and flags object by object. RT1 (types in the lists) and SC1 (still objects indexed apart) attack that filtering and were never built.
- **The same position walked twice** [D, engine `physics.c:837` then `:843` → `:684`]: each mass point's physics update does a point walk for its location, then a feature query from the root at the same position, so the sphere walk retraces the point's descent until the sphere straddles a plane. Vehicles and props only, not bipeds; worth unsized.
- **Misses:** collision's walks are pointer chases over BSP nodes, surfaces and cluster lists, with one miss in flight; PACKFEAT (surfaces with their edges and vertices together) and CSPK (packed cluster lists) are the layout answers, unbuilt.

## 2. Node matrices and animation (posing)

### 2.1 What it computes, and who reads it [D]

The lab's `research/manifest/tickmap_posing_code_2026-11-09.md` (line numbers there are the port's tree after fixups).
- **One entry,** `object_compute_node_matrices`: the pose's decode, then its overlays, scale, the type's preprocess (units add action, overlay and ping animations), interpolation from the last pose, the node-stack matrices, and the bounding sphere (copied to the port's sphere table). Units then run inverse kinematics on the hands.
- **Called once per awake top-level object in the walk,** after its type update; children are posed by recursion. Bipeds and units pose with pre- and postprocess, vehicles and weapons with preprocess only, scenery with neither.
- **The port's gates on the call:** `posehalf` (far resting objects every other tick), `machinesfar`, VISLAZY (undrawn scenery-kind objects made on demand), and hoisting. **Its replacements for the decode:** the loop-pose table (PSC), poses decoded ahead on the ME (pose-ahead), and VFPU quaternion maths (VFPUQD, VFPUP).
- **Readers inside the tick, and the nodes each needs:** markers (the marker's node: head, body, hand, seat, trigger, attachments), `collision_model_instance_new` (every node its collision model binds), IK (the hand chain), lights and effects at attachment markers, machine elevators, items' rest node, the held weapon (its parent's node), the AI's perception and aim (head and camera markers), and the draw (every node). AI and effects run before the walk, so they read the previous tick's pose.

### 2.2 What it costs on the PSP

| Stretch | Posing's share | Source |
|---|---|---|
| b30 flight | 11.3% of the tick (by function) | PC sampler, PSP-3000, sierra-hw 42114568213 [M] |
| b30 beach | 10.1% of the tick (by function) | PC sampler, PSP-1000, sierra-hw 42000782600 [M] |
| b30 beach fight (the pad's replay) | animation and node matrices 6.7 of 44.2 ms a tick (15%, by step, instrumented); `object_compute_node_matrices` 6.1 ms | combatsteps, PSP-3000, sierra-hw 42633989710 [M] |
| c20 hands off | 13.2% of the tick (by function) | PC sampler, PSP-1000 [M] |
| b30 flight, landing; b40 cliff (the PSP census) | node matrices 10.6, 8.5 and 10.0 ms a tick, instrumented; bipeds 74%, 69%, 67% of it | the PSP census, PSP-3000 sierra-hw 42730757534, 42730780195 [M]; lab findings `census_nodes_fate_*` |
| d20, a30 | **not measured** | gap |

**By drawn size, on the PSP** (the census's animlod, PSP-3000 [M]), as a share of all posing:

| Stretch | Bipeds undrawn | under 16 px | 16-32 px | over 128 px |
|---|---:|---:|---:|---:|
| b30 flight | 15.5% | **40.2%** | 4.8% | 12.8% |
| b30 landing | 7.5% | **58.5%** | 0.2% | 2.9% |
| b30 beach fight | **63.6%** | 1.5% | 3.5% | 4.4% |
| b40 cliff | **51.3%** | 4.0% | 3.7% | 4.6% |

The emulator's animlod1 [E] had the same shape (under 16 px 44.7% of the flight's and 37.2% of the landing's; undrawn 49.2% of b40's), so the size tiers sized on it in `redesign_tick_target.md` §3.1 and §3.5 stand on the PSP.

**Who reads the matrices** (the census's nmsplit fates): a biped's matrices are read first by its own update (markers, aiming), drawn or not, so the simulation needs them; only the drawing's extra nodes are free to skip. Made and never read, drawn or not: 0.4-0.5 ms a tick on b30, 1.35 on b40's cliff (scenery, weapons and vehicles mostly). About half of every kind's compute time is made over before a draw judges it: at two ticks a frame the first tick's matrices always are, so that is the frame rate, not waste, and goes away at one tick a frame.

### 2.3 Every lever tried on it, and where it was priced

49 levers sit on posing's ground (the lab's `research/manifest/tickmap_posing_levers_2026-11-09.md`, checked with `tools/levercite.py`: 46 listed, all citations right; MC2, NB and UVBUNIT added by hand).
- **On by default:** PV, PA, PQ, MP (posing a frame behind on the ME), VISLAZY, AIMCACHE, VFPUQD, VFPUP, MECTX, PSC, POSEFPU, LINEHEAT. MP and POSEFPU aren't bit-exact and ship because they play the same.
- **Priced on one PSP map only** (14): PQ (c20), WM, NPF, LEAF, VFPUQD, VFPUP, SKELBATCH (b30), MEB, LBM (b30, both PSPs), PREDEC (b30), PSC (c10), NW2 (c20), VH2 (a10). LBM (poses made only on the drawn tick and on a read) lost on b30 because bipeds' poses are read every tick; NW2 (the node walk on the ME, copied back) lost on c20.
- **Priced only in the emulator** (19), which today's rules don't let stop anything on time: NW, PH, NMS, UNCR, PNC, ANIM2, NMR, VISLAZY, NMPRE, AIMCACHE, NMSAME, POSEW, UNSEENPOSE, RENDERTICK, LNM, NW1, MECTX, TAGHOT, ARMPASS, WORLDVIEW. **Several stopped partly for not being exact:** ANIM2 (far bipeds posed only on the drawn tick: "under gate and PSP floors; not exact"), UNSEENPOSE (undrawn units posed at the root), ARMPASS (armed bipeds' matrices after the walk: "plays differently"), LNM ("plays the same rather than exact ... not built"), WORLDVIEW. Under the netcode's boundary (Sheepdog01 17:30; posing never goes on the wire) these reopen, each with its PSP price.
- **Proposed, never built:** RD (cook-time bounding spheres), GL4, MEPOSE.

### 2.4 Where else it might gain

- **The vehicle double compute** [D, new, exact]: a vehicle moved by physics computes its node matrices in `physics_update_new`'s `object_set_position` (engine `source/physics/physics.c:1422`, then `:1523` or `:1587`; the compute at `source/objects/objects.c:3413`), then again in the walk. The first one's job is likely the root and the bounding sphere for the reconnect. `levercheck.py` finds no earlier try. Other paths do the same (seat exits `units.c`, `action_obey.c`: set the position, then recompute). Count in the emulator, price on the PSP.
- **Readers need few nodes:** the tick's readers other than the draw and the collision model read a handful of markers (tinynodes1 [E]: grunt 8 of 17 nodes, elite 11 of 26). Posing only the read chains for tiny bipeds (`redesign_tick_target.md` §3.5) rests on this.
- **The stretches never measured** (landing, b40, d20), where b40's posing sits in undrawn bipeds the AI reads.

### 2.5 Does the algorithm suit this machine? [D/U]

- **It's miss-bound, not arithmetic-bound:** SKELBATCH (the poses batched onto the VFPU) gained nothing on the PSP, and VFPUQD and VFPUP pay small. Pose decode walks animation data per node; the matrices chain parent to child.
- **Every awake object poses every tick, whoever reads it.** The gates take scenery (VISLAZY) and far resting objects (`posehalf`), not moving bipeds, whose markers the AI reads each tick (LBM's lesson). What a moving biped's readers need this tick is a few chains; what the draw needs is every node, once a frame.
- **The ME already decodes poses ahead** (pose-ahead), and the ME is near full at the target on the heavy stretches (`redesign_tick_target.md` §2a): more posing on the ME is a placement only where it has room.

## 3. AI on the main CPU

### 3.1 What runs where [D]

The lab's `research/manifest/tickmap_ai_code_2026-11-09.md`. Under the default keep words (`aime`, `aime1b`, `aime2a`), **the AI's thinking is one Media Engine job a tick**: input, perception, situation, emotion, orders and the decision loop, with its own line-of-sight rays cast inline. `soundedram` would turn it off, but that word is opt-in. The PSP agrees: the ledger measures the AI's ME work at 19.5 ms a frame on b30's flight [M].

**What stays on the main CPU** (the 16.5% of the flight's tick the sampler counts as AI [M]):
- **the control half of each actor:** action update, movement, look, combat, unit control, communication and conversation: the steps that write the unit and the world other systems read this tick;
- **encounters and squads** (no ME hook at all);
- **path searches** started from `actor_destination_update` (those reached from a decision run on the ME);
- **the tick's batch of perception rays:** the first four on the main CPU, the rest as a ring job on the ME;
- **the hand-over and the collection:** write-back, submit at the frame's end; at the next tick's start a bounded wait, invalidate, applying the job's pathfinding memo and its queued world writes (speak, activate).

### 3.2 What it costs on the PSP

| Stretch | AI on the main CPU | Source |
|---|---|---|
| b30 flight | 16.5% of the tick | PC sampler, PSP-3000 [M] |
| b30 beach | 8.6% thinking + 1.4% control (by function) | PC sampler, PSP-1000 [M] |
| b30 beach fight | 5.9 of 44.2 ms a tick (13%, instrumented) | combatsteps, PSP-3000 [M] |
| The ME's AI job | b30 flight 19.5 ms a frame (ledger); b40 snowfield 14.4 ms a job, worst 42.9 | ledger1 and sierra-hw 42152471353 [M] |
| Actor updates alone, by band (the PSP census) | 2.9 ms a tick on b30's flight, 2.7 on the landing and b40's cliff (instrumented); in a fight 79-86%. By band: flight 20-40 units 46%, 10-20 27%; landing 20-40 69%; cliff under 10 units 55%. By drawn size: flight under 32 px 67%; cliff undrawn 78%; the beach fight 2.6 ms a tick, 98% in a fight, undrawn 91% | the PSP census, PSP-3000 [M]; lab findings `census_ai_actor_updates` |

The flight's 16.5% and the beach's ~10% differ by stretch, and the sampler groups by function, so the AI's rays are counted under collision. The census's actor updates (~2.7-2.9 ms a tick) are only the updates themselves: the rest of the 16.5% is the encounters, path searches and hand-over around them.

### 3.3 Every lever tried on it, and where it was priced

51 levers sit on the ai ground (the lab's `research/manifest/tickmap_ai_levers_2026-11-09.md`, checked with `tools/levercite.py`: 49 of 50 citations right, SPHSL added).
- **On by default, paid on the PSP:** AIME (the thinking on the ME: c20 and b30, both PSPs), AICAD (thinking at half rate, shipped as a cadence change), PWSL and SPHSL (reconnects answered by slack: c20 tick -0.6 to -2.8%), OBSRAY (obstacle rays reused: step -10.4%), PRS.
- **Priced on one PSP map only** (most on b30, PSP-3000): AIFAR (far calm actors think 1 tick in 4: frame -1.9% cinematic, -3.4% landing; off, Sheepdog01's A/B), AITICKOLD (tick -14.5% on the beach), MESCHED, MEMEMO, CLUSTERPACK, TGME, TIMERCALLS, AUDIOCHUNK, RBK; the AIME look jobs on d20 or c10.
- **Priced only in the emulator:** L3, RA, RC, AP, AC, MB, LPH, UNC, UNCR, PRS, LODS, UNSEENPOSE, ORPH, AIMEDRAM; never priced: ACTORSPLIT, RT1, RV3, RH, NNAI, ANIM2, MPAIM.
- **Stopped on play, not on the netcode:** RA (far actors sliced: b30's marines lose the beach), AICAD and AIFAR (cadence changes by design), AITICKOLD (fights resolve differently; two fixes built, unproven together), UNSEENPOSE, UNCR (a skipped side effect diverges play). The AI runs on the host only, and the campaign has no host, so the netcode excuses none of these: they stay questions of "plays the same" (Sheepdog01's bar), and AITICKOLD's two fixes are the cheapest of them to prove.

### 3.4 What it means for the menu [U on M]

**"The AI thinks less often" mostly frees the ME, not the tick.** The thinking is on the ME; the main CPU carries the control half for every actor every tick, plus encounters. So the main CPU's AI levers are the control half's cost per actor and how many actors run it each tick, and the ME's are the thinking (where it's over at the target on the flight, `redesign_tick_target.md` §2a).

## 4. Units and vehicles (their own update code)

### 4.1 What it computes, and who reads it [D]

The lab's `research/manifest/tickmap_units_code_2026-11-09.md` (line numbers there are the port's tree after fixups).
- **The object walk** visits each awake object; sleepers and resting scenery are skipped (FIXES_WS, wake-on-write). `object_update` runs the type update, damage, exported function values, the pose (§2), function values and change colours, light connection, then children.
- **A biped's type update is `unit_update` then `biped_update`.** `unit_update`: timers, the driver's and gunner's control copy, camouflage and stun timers, aim integration, `weapon_owner_update`, seats, delayed damage into `ai_handle_damage`, dialogue, lights. `biped_update`: facing, state from animation, turning, then `biped_update_moving` → `biped_update_physics` (velocity model, `collision_move_pill`, the support-plane search, landing) → `object_translate` (disconnect and reconnect to the map), melee, footsteps, `unit_update_animation`.
- **A vehicle's is `vehicle_update`:** steering and speed seeks, then mass-point physics unless at rest (each mass point a point walk and a ground feature query, §1.5), suspension, slipping and crash effects; a parked vehicle runs only the suspension.
- **Who reads it this tick:** later objects' collision reads the moved object's sphere and cluster links; the draw reads poses and positions; sounds and effects start from footsteps, slips and lights; riders update after their parent. The AI runs before the walk, so it reads the previous tick's positions.

### 4.2 What it costs on the PSP

| Stretch | Cost | Source |
|---|---|---|
| b30 flight | units' own code 9.3% of the tick (by function, excluding the collision and posing they call); object updates 34.9 ms a tick as instrumented (inclusive), bipeds 24.3, the moving bipeds 23.6 (~24 a tick at ~970 µs), vehicles 6.6 (the Pelicans on their script) | sampler, PSP-3000; objcost, PSP-1000 sierra-hw 42141971439 [M] |
| b30 beach fight | object, unit and biped updates 21.6 of 44.2 ms a tick (49%, instrumented): `biped_update_physics` 4.1, the step marked `unit_verify_vectors` 3.2, `object_type_update_for_type` 2.8, `psp_object_update` 2.0 | combatsteps, PSP-3000 sierra-hw 42633989710 [M] |
| b40 snowfield | object updates 30.8 ms a tick, bipeds 20.0 (far and out of the fight 28%, ~5.5 ms), vehicles 3.7 (gun turrets) | objcost, PSP-3000 sierra-hw 42152471353 [M] |
| b30 flight, by change state | object updates 38.8 ms a tick (instrumented): moving bipeds 26.6 (68.5%; 23.5 updates a tick, ~1.13 ms each), vehicles that changed 4.9, scenery that changed nothing 2.7 (~53 updates a tick, ~51 µs each), vehicles that changed nothing 2.0 (~0.9 ms each); **updates that changed nothing 6.4 ms a tick, 16.5%** | the PSP census, PSP-3000 sierra-hw 42730757534 (recon/census c368edef) [M]; lab findings `census_objcost_by_state_flight` |
| b30 landing, by change state | object updates 34.5 ms a tick: moving bipeds 22.5 (65.2%; 20 a tick, ~1.13 ms each), vehicles that changed nothing 3.3 (2.8 a tick, ~1.2 ms each), scenery unchanged 2.8, bipeds unchanged 2.4; **updates that changed nothing 9.5 ms a tick, 27.5%** | the same run [M]; `census_objcost_by_state_landing` |
| b30 beach fight, by change state | object updates 29.7 ms a tick: moving bipeds 20.9 (70.3%; 17.7 a tick, ~1.18 ms each), vehicles that changed 3.8, scenery unchanged 1.2; **updates that changed nothing 3.2 ms a tick, 10.9%** | the PSP census, PSP-3000 sierra-hw 42736037265 [M]; `census_beach` |
| b40 cliff, by change state | object updates 32.1 ms a tick: moving bipeds 20.6 (64.3%; 19 a tick, ~1.09 ms each), vehicles that changed 4.1, vehicles unchanged 2.6 (5.4 a tick, ~0.47 ms each; the turrets), projectiles 2.0, machines and scenery unchanged 1.1 each; **updates that changed nothing 5.5 ms a tick, 17.0%** | the PSP census, PSP-3000 sierra-hw 42730780195 [M]; `census_objcost_by_state_cliff` |
| d20, a30 | not measured by kind | gap |

**How to read the step names:** a step runs from its marker to the next, and markers go before calls written as statements; a call inside a conditional expression gets no marker and is booked to the step before it (`psp_fixups.py`, step_call). So "unit_verify_vectors 3.2 ms" is the work after that call up to the next marked call, mostly the vehicles' physics (UVBUNIT's row: "the step is the vehicles' physics, not unit_update"), not the debug check, which is off by default.

### 4.3 Every lever tried on it, and where it was priced

43 levers sit on object-update's ground (the lab's `research/manifest/tickmap_units_levers_2026-11-09.md`; 36 listed by the read, all citations right; DAL, FISSB, FVP, GSCHECK, HUHINT, VH2 and WT added from levers.tsv).
- **On by default, paid on the PSP:** WT (wake-on-write: PSP-1000 b30 frame -3.7 to -4.8%), HUHINT (function values' phase in 32 bits: c20 frame -3-4% on both PSPs), UVB (vehicle physics early-out: a30 fight tick -8%, PSP-3000), CW (collision loops inlined: b30 drive tick -7%), P2NC (non-collideable relinks queued: c20 tick -2.4…-4.7%), LD1 (attached lights once a frame: b30 tick -5…-7%).
- **Priced only in the emulator:** RB, RB1, BW (the whole biped update on the ME), LPH (loop phasing: the cache model said -11.5%, the real batch lost 34%), VISLAZY, MACHLINE, OBJHOT, PHASEB (bipeds stage-major: "no exact cut").
- **Stopped for want of an exact form:** PHASEB and FISSB (stage-major and split passes). Under the netcode's boundary (Sheepdog01 17:30) a plays-the-same form is allowed; `redesign_tick_target.md` §3.9 sized it at ~1-2 ms a tick on the flight.

### 4.4 Where else it might gain

- **Awake sleep candidates that change every tick** [D, a lead]: a sleep candidate pays a checksum before and after its update (`psp_sleepy_update`); wake-on-write skips the first for objects quiet two ticks, but one that changes every tick pays both for a sleep it can't take. How many a tick, and the cost, are unmeasured.
- **The moving bipeds are the ground:** ~24 a tick on the flight at ~970 µs each, inclusive of their physics, collision and posing. The in-fight band at 20-40 units is 36% of biped time on the flight and 58% on the landing (S4's census); S4's tier takes only the far, out-of-the-fight ones.
- **Vehicles moved by physics pose twice a tick** (§2.4).

### 4.5 Does the algorithm suit this machine? [D/U]

- **Each object runs its whole update chain in turn,** so each update re-fetches its ~300-400 lines of code: instruction misses scale with the updates run (`redesign_tick_target.md` §3.9). Fewer updates (tiers, rates) cut both work and misses; reordering them (stage-major) was priced at ~1-2 ms in a plays-the-same form.
- **A biped's movement is a sequential chain:** velocity, pill move (collision), support search, translate (reconnect). Each step reads what the last wrote, so the chain itself doesn't overlap; the gain is in how often it runs and for whom.

## 5. The smaller systems

Each is a few percent of the flight's tick; together, with the rest, about 37%. Sources: the lab's `research/manifest/tickmap_small_code_2026-11-09.md` (what runs, callers, readers) and `tickmap_small_levers_2026-11-09.md` (every lever on effects, lookups, sound and micro-opt, checked: 143 of 143 citations right, CTN and NEARZ added by hand). Shares are the PC sampler's on b30's flight (PSP-3000) [M] unless named.

### 5.1 Effects and particles (3.4%; 1.5% on b30's beach, PSP-1000)

- **What runs:** `effects_update` (tick) walks live effects: location from the parent, scale and colour functions, visibility, then events that make particles, decals, sounds, objects and damage. Particles, contrails and particle systems update on the **frame** path, decals at the rasterizer's frame start: at 30 fps a frame is one tick, so they count against it at the target. d20 is the heavy map (effects_update ~1,060 µs a tick, 175 live effects [E], `redesign_tick_target.md` §3.4).
- **Levers (44):** on by default and PSP-paid include WXDEAD (c10 frame -1.6%) and VFPUTRIG; PADMIT (the budget asked first) and FLAMEDECAL are priced only in the emulator, with DC, MF, DM, DB, DFADE and others; FX15, L15, PRIO are compromises built off. Stopped partly on exactness: L15, PRIO, DFADE, VFPUTRIG (now shipping).
- **Where else:** the decorative level emitters at a lower fixed rate or only on screen; particles under ~2 px faded early (`redesign_tick_target.md` §3.4). PADMIT and FLAMEDECAL need their PSP prices.

### 5.2 Tag lookups (2.9%)

- **What runs:** every `*_definition_get` is a macro over `tag_get`; the port's TAGSNAP caches the window's (instances, count) inline and falls back to the checked routine; `TAG_BLOCK_GET_ELEMENT` is inlined. **The tag window is 32% of the tick's data misses** (`cache_work.md`), so the cost is the misses on tag data, not the lookup.
- **Levers (10):** TYP, HANDLES, ACCLEAF, LIGHTFIRST priced on one PSP map; RL1, LODX, TLM, LIGHTREUSE emulator only. TAGHOT (hot tag fields copied to compact records; ceiling ~4% of the tick's data misses in the hottest 4 KB [E]) and B's packed tag window are proposed.

### 5.3 Audio on the main CPU (2.4%; 1.7% on the beach)

- **What runs:** per frame, `game_sound_update` (looping sounds) and `sound_render` (listener, priorities, channels, the sound cache); obstruction is a PVS test and one ray, cached 4 ticks and capped at 6 tests a frame. The mix runs on the ME.
- **Levers (67):** the largest ground by count, most of them the ME's audio; 16 emulator only (RM1, SC1, SI, E1, NMS, AFX, HALFMUFFLE, VIRTVOICE, ...), 34 on one PSP map. Stopped partly on exactness: NMS, E3N, HALFMUFFLE, VIRTVOICE.

### 5.4 Scripts (1.6%)

- **What runs:** `hs_runtime_update` walks every script thread every tick and runs those due (`hs_runtime.c`: sleeping threads still cost a step), then object-list garbage collection every tick and node collection every 16. Scripts run before `objects_update`, which reads what they set.
- **Levers:** none of its own on the record; the walk over sleeping threads is a candidate only if the count is large (unmeasured).

### 5.5 libm and memcpy (5.3%)

- **What runs:** matrix products take the VFPU on the main CPU; `sqrt` and `atan2` are narrowed to single precision; `memcpy` is the port's. The heaviest matrix site is the pose compute (§2); square roots sit mostly on AI paths (firing positions, path search).
- **Levers (24 on micro-opt):** DBLSOFT, HUHINT, VFPUQD, VFPUP, VFPUTRIG, FPDENORM, STRICTALIAS, GETTICKCACHE each priced on one PSP map; ENUM32 stopped (an identical EBOOT). TUNE (GCC told the Allegrex's costs) and FDIV (where divides and square roots run) are open, priced as their rows say.

### 5.6 Other object code (7.7%)

- **What runs:** weapons (tracking, animation, heat), items (keeping upright), projectiles, machines (platforms; automatic doors test `objects_in_sphere` one tick in four), scenery (an animation step). Equipment has no update of its own, only `item_update` (engine `object_types.c:321-331`).
- **The port's changes:** the sleepers (vehicles, machines, corpses, weapons and equipment, scenery), with sleepers still computing their function values; `posehalf` and `machinesfar`.
- **Where else:** the awake sleep candidates that change every tick and pay both checksums (§4.4).

## 6. What the map says so far

To be written once the census fills the gaps (§1.2, §2.2, §4.2: the landing, b40, d20 and a30 by system, drawn size, band x step). Leads it already holds, each with its kind:
- **Exact:** the vehicle double compute (§2.4); the mass-point double walk (§1.5); awake sleep candidates' checksums (§4.4).
- **Plays the same, reopened by the netcode's boundary** (Sheepdog01 17:30, 17:52): collision's reuse, batching and tick-start views (§1.4); posing's emulator-only stops (ANIM2, UNSEENPOSE, ARMPASS, LNM, §2.3); the stage-major forms (PHASEB, FISSB, §4.3).
- **Plays the same, open on play:** AITICKOLD's two fixes together (the outcome census s3o5 is running); the AI's cadence levers (§3.3).
- **Untapped hardware:** particle point physics on the VME with a per-batch origin; the VFPU's approximation ops (the lab's `research/manifest/vme_vfpu_under_1607_2026-11-09.md`).
