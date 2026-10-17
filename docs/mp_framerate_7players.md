# The multiplayer build's frame rate with 7 players (diagnosis, 2026-10-17)

**The case [M, PSP-1000, lab 39046598878 run 2]:** the client-only build (`mpcut` 68b27ff) joined `[C] Slayer` on Prisoner with 7 players. It started at 7.7 fps and fell to 1–3 fps as the tick grew from 7 to 20–28 ms. In a late window, `objects_update` took 16.6 ms a tick (50% of the time):
- bipeds 11.5–12.3 ms a tick for 6.3–7.5 updated a tick (~1.8 ms each): `biped_update` 5.6–6.0 ms, node matrices ~2.1, damage 0.2; one pill sweep each a tick (moving 455 µs, still 269 µs);
- weapons 2.7–3.3 ms for ~7.9 updated a tick, of which 6.2 are **lit weapons asleep on the ground** ("lit items per tick: 7.85 updated, 6.21 offered to sleep, 6.17 asleep");
- a client simulates all elapsed time (`game_time.c`), so a slow tick means more ticks a frame (up to 22) and fewer frames: the spiral.

**What a client computes for a remote player [D]** (the audit below; ChupathingyCE d98982b, line numbers before the fixups): the host's last relayed action, held until the next, drives the remote biped through the full `object_update` → `unit_update` → `biped_update` → physics (`collision_move_pill`), its node matrices, light re-links and held weapons. The host's unit states then pull it back (`distributed_handle_unit_state`, `network_objects.c:726-781`: nothing within 0.05 wu, half way beyond, exact past 1 wu). The host sends position, velocity, orientation, seat, inventory, shields, powerups and camouflage; the client alone makes animation, aim, airborne/landing/crouch flags, footsteps and firing effects. ChupathingyCE has no "remote unit: skip physics" mode.

## Levers, ranked

| # | Lever | Guard | Expected per tick | Risk | Proof |
|---|---|---|---|---|---|
| 1 | **LD1, already in b138**: lit sleepers' and held lit weapons' lights re-linked once a frame | none | most of the weapons' 2.7–3.3 ms [U] | none for sync | a `[C]` join on b138, `keepsets="- \| nolightsframe"` |
| 2 | Node matrices for remote bipeds (and their held weapons) once a frame or when read, from inputs snapshotted at the normal point | remote players on a client | ~1.5–1.8 ms + the held weapons' share [U] | client only; look only | the campaign's lever #2 (lazy node matrices, b139) with a remote-biped rule; netplay check mode |
| 3a | A still remote biped reuses its last pill sweep | remote, still, grounded | up to ~0.27 ms a still biped | near-exact | a check word on netplay |
| 3b | Remote pill sweeps skipped on catch-up ticks, swept on the frame's last | remote, not the frame's last tick | ~1.3–2.2 ms at 16–22 ticks a frame | not exact: Sheepdog01's call after an A/B | netplay side-by-sides counting the host's corrections |
| 4 | Wake-on-write while networked, with hooks on the network's direct writes | networked games | ~1 ms [U] | exact if the hooks are complete | `wakecheck` on netplay |
| 5 | Corpse sleep on clients (the `!game_engine_running()` guard relaxed) | distributed client | unknown: how many of the 6.3–7.5 bipeds are bodies | low | check mode on netplay |
| 6 | Remote bipeds follow the host's states with no physics | — | all remote sweeps | high: the host sends no airborne/landing flags; sending them changes the wire protocol | not recommended |

`unit_update` and the rest of `biped_update` can't be skipped for remote bipeds without changing how they look. Capping ticks per frame would change how local play feels: Sheepdog01's call.

## Use map

# Remote bipeds and ground items on a distributed client: what is computed, what the host overwrites, and where to do less

Read-only audit, 2026-10-17. Engine: ChupathingyCE `d98982b` (`the engine checkout`). Port: Sierra-III `mpdocs` `c6e936e` (`psp_fixups.py` line numbers are from that checkout). Labels: [D] read in code, [U] inference, [M]/[E] quoted from runs and docs.

**The measured problem [M, PSP-1000, lab 39046598878, mpcut `68b27ff`]:** Prisoner, 7 players. `objects_update` costs ~16.6 ms a tick. Bipeds take 11.5–12.3 ms for 6.3–7.5 updated a tick. Their type update is 5.6–6.0 ms, node matrices ~2.1 ms, and there are ~6 pill sweeps at 270–455 µs each. Ground weapons: ~8 a tick for 2.7–3.3 ms.

**Note:** `68b27ff` does **not** contain LD1 (`a7a9860`). [D: `git merge-base --is-ancestor`] HEAD/b138 does.

---

## 1. A remote player's biped on a client, per tick

### The model [D]

`port/linux/game/network_distributed.c:8-30` sets the model:

- The host relays every player's input "as its tick ran it", and clients drive those players with it.
- Every tick the host sends every player's unit state: position, velocity, forward and up, seat, shields and powerups. A client "binds, kills, seats and places its copies to match".
- Players far away or out of sight are sent less often:
  - every 2nd, 3rd or 4th tick beyond 25, 60 or 120 wu;
  - every 6th tick when hidden or dead (`:270-288`).
- **ChupathingyCE has no "remote unit: skip physics, interpolate only" mode.**
  - The nearest thing is for objects, not units: a co-op vehicle that no player drives is kept `_object_at_rest_bit` (no physics) and moved exactly to the host's state (`network_objects.c:658-711`, `distributed_vehicle_unsteered` / `distributed_object_follows_host`). It is co-op only.
  - Co-op AI units on clients use the same "feed control, correct drift" model as players (`network_actors.c:1-30`, `:533-545`).

### The path, in order

1. **Messages arrive outside `game_tick`** [D].
   - Data messages are handled from the client message handler (`source/networking/network_client_message_handler.c:715-720` → `network_distributed_handle_message`).
   - Ticks run in `game_time.c:472-481`: `game_tick()`, then `network_distributed_tick()` after it.
   - An older message of a kind is dropped once a newer one has arrived (`network_distributed.c:44-46`).
   - [U] When a frame runs 16–22 ticks, at most one round of host states and relayed actions lands before the whole batch. The catch-up ticks between are pure prediction on stale input.
2. **Input** [D]. `update_client_dequeue_distributed`, `source/game/player_queues_new.c:692-737`:
   - A remote player's action is the last relayed one, held until the next arrives (`:722-726`).
   - Pending buttons are OR'd in once.
   - The action goes through `players_update_before_game` → `unit_control` like a local player's: throttle, facing, triggers, weapon and grenade choice, zoom.
3. **`objects_update`** (`source/objects/objects.c:4441`), loop at `:4513-4532`, calls **`object_update`** (`:3828-3897`) for every active top-level object. In the port this goes through `psp_object_update` (`psp_fixups.py:3452`, `:2763`). The steps:
   - `object_type_update` (`:3853`). For a biped this runs the object, unit and biped updates:
     - **`unit_update`** (`source/units/units.c:5362`): control, aiming servo, weapon readying, zoom, camouflage.
     - **`biped_update`** (`source/units/bipeds.c:4201-4413`):
       - `biped_snap_facing`;
       - `biped_update_turning`;
       - **`biped_update_moving`** (`:4309` → `:3510-4143`). It fills `struct biped_physics` from the biped's velocity and pill and calls **`biped_update_physics`** (`:3974` → `:2356`), whose sweep is **`collision_move_pill`** at `:2618`, tested against the BSP and objects with the living-biped flags. Players collide outside co-op (`network_coop_player_collisions`, `network_coop.c:1977`). It then writes the position (`object_translate`), the velocity, `_biped_airborne_bit`, `_biped_slipping_bit`, ground plane, landing, the jump and `_object_at_rest_bit` (`:4013-4136`);
       - airborne, landing and slipping states;
       - melee;
       - footsteps (marker reads);
       - `unit_update_animation` (`:4396`).
   - `object_damage_update` (`:3856`). Shields are not recomputed on a client: `objects_update_shields()` is `!network_game_distributed_client()` (`source/objects/damage.c:160`, used `:2051-2054`).
   - `object_compute_node_matrices` (`:3862`).
   - Function values and change colours.
   - `object_connect_lights` (`:3869-3879`).
   - **Children:** the held weapon and backpack weapon, each a full `object_update` with `weapon_update`, `item_update` and its own node matrices (`:3881-3891`).
   - `object_postprocess_node_matrices` (`:3893`).
4. **Host correction** [D]. `distributed_handle_unit_state` (`network_distributed.c:2152-2271`):
   - It sets seat, damage and network state, powerups and camouflage.
   - If the unit is placed and not riding, it calls `distributed_apply_state(unit, state, pos, REMOTE_CORRECTION_TOLERANCE 0.05 wu, REMOTE_BLEND_DISTANCE 1.0 wu)` (`:2264-2270`; constants in `network_distributed.h:129-134`).
   - Within 0.05 wu, nothing happens (`:1611-1614`).
   - Beyond that, `network_objects_reconcile` (`network_objects.c:726-781`) runs:
     - **≤ 1 wu:** the copy moves **half way** to the host's position, its forward and up become the host's, and its velocity becomes the host's. A render glide is added.
     - **> 1 wu:** the copy is put exactly at the host's state.

### What is overwritten, what isn't [D]

| Client computes | Host's word | Overwritten? |
|---|---|---|
| Position (pill sweep) | Position | Half of any error over 5 cm, each time a state arrives. All of it beyond 1 wu |
| Velocity | Velocity | Replaced on each correction (`network_objects.c:771-772`) |
| Forward / up | Forward / up | Replaced on each correction (`object_set_position`) |
| Shields / health | Unit state's vitality | Client doesn't compute them (`damage.c:160`) |
| Seat, weapons, grenades, powerups, camo, death | Unit states, inventories | Host's, wholesale |
| Animation state, aim servo, airborne / landing / crouch, footsteps, firing effects | — (only the damage animations it picks, `network_objects.c:838+`) | **Not sent: the client's own** |

**Is full physics run only to be corrected?** [D/U]

- **Partly.**
  - The sweep's position counts only for the ticks between states, and only to within 5 cm before correction pulls it half way.
  - Near players (under 25 wu) get a state every tick. That is the remote players that matter on Prisoner [U].
- **The physics still does two jobs:**
  - **(a)** It dead-reckons between states. This is all of the movement during a catch-up batch of ticks.
  - **(b)** It produces the flags the animation needs: airborne, landing, slipping, crouch height, on ground. The host doesn't send these.
- **Hit reports don't need precision.** The host accepts a client's hit if the target is within 2–3 wu of where the host had it, a second of history back (`network_damage.c:18-30`, `:216-226`). A remote biped's position on a client therefore has to be plausible and within about 2 wu, not exact.

## 2. Node matrices (~0.3 ms a biped)

**Who reads a remote biped's node matrices on a client** [D/U]:

- **Drawing:** once a frame. At 16–22 ticks a frame, that is 1 read for every 16–22 computes.
- **Its own children, at once:**
  - the held weapon's compute (parent marker);
  - footstep, melee and firing markers (`object_get_marker_by_name` in `biped_make_footstep` and the weapon fire path);
  - speech at the "head" marker.
- **Collision against its model:** `collision_model_instance_new` from projectile and line tests, which is how the local player's shots find what to report. Also items resting on objects (`items.c:808-822`), though only scenery and devices.
- **Nothing that goes to the host reads them** [D]. Hit reports carry points, which the host checks against its own history.

**Could they be lazy?** [U]

- Yes, for remote bipeds and their held weapons. Compute when read (draw, collision, marker), or once a frame on the tick before drawing, the way LD1 handles lights.
- The port already has the machinery:
  - FIXES_134 `nmreaders` (`psp_fixups.py:19754`), a recon that classes each compute by its first reader;
  - FIXES_136 `nwbatch` (`:20116`), whose deferred compute settles on any read through `object_get_node_matrices` / `object_get_node_matrix`. It is shelved: `SHELVED_TAGS`, `:21577`.
  - `levers_next_b138.md` row 2 already names "node matrices computed only when read" as lever #2.
- **The exactness trap** [D, from the nwbatch notes `:20097-20115`]:
  - A read that comes before this tick's eager point gets last tick's matrices.
  - An attachment's update changes the biped's animation state after its compute.
  - So a lazy compute must use the inputs the object had at its eager point: snapshot them, as nwbatch does. Otherwise a check mode will show differences.
  - These differences are client-local and look-only, but the rule is to prove it.
- **The campaign's `posehalf`** (FIXES_122 `farrate`, `psp_fixups.py:16736-16751`; viewer test `psp_far_viewer` ~`:16780`, requiring `_game_connection_local`):
  - It is refused in networked games because "a client simulates in step with a server that updates everything every tick".
  - That reason holds for lockstep. It does not hold for this netcode's remote units, whose matrices feed nothing the host sees [D/U].
  - It also only covers objects at rest, which living players rarely are.

## 3. Weapons and items on the ground

**What the counts mean** [D]:

- **The "asleep" line** (`psp/src/psp_platform.c:740-746`):
  - `psp_objects_asleep` is every type that slept. "of 26.4" is `psp_objects_sleep_checked`: objects offered to the rule and summed.
  - **"24.3 items"** is `psp_items_asleep`: weapons **and equipment** that slept (`psp_fixups.py:3115`).
  - Only top-level items count. A held weapon is a child, updated inside its owner's `object_update` (`objects.c:3881-3891`), so it never reaches `psp_object_update` and is never offered. It is also excluded by `parent_object_index != NONE` in `psp_item_may_sleep` (`:2737-2745`).
- **"weapons ~8 a tick, 2.7–3.3 ms"** is the per-type account from `objects_update`'s loop hook (`psp_fixups.py:3452`, `psp_object_account` in `psp_platform.c:677-684`).
  - It counts **every top-level weapon visited, sleepers included.**
  - So the ~8 are very likely among the 24.3 asleep. The rest of the 24.3 is equipment (grenades and powerups), timed under the equipment account [U].
  - **The ~0.35–0.4 ms each is what a weapon costs while asleep, not a sign that it is awake** [U]. The run's own lines settle it: "weapons on the ground per tick: N updated, M asleep" and "lit items per tick: … kept awake: held / not at rest / on an object / tracking / breakable" (`psp_platform.c:747-756`).
- **What a sleeping ground weapon still costs** [D, `psp_sleepy_update` and FIXES_123L]:
  - the family checksum of its datum, every tick. Wake-on-write is off in networked games: FIXES_WAKE, `psp_fixups.py:21583-21587`, `:21703`;
  - its function values and change colours (FIXES_123L `weapontrack`, `:17256-17283`);
  - **for a lit weapon (plasma glow, needler), a full light re-link every tick**: `object_connect_lights`, disconnect and reconnect (FIXES_123L `:17235-17249`).
  - The b137 audit found the same on Damnation: "weapons on the ground 4.5 ms for 10, all lit and asleep" [M, `docs/mp_build_audit.md`].
  - **LD1** (`psp_fixups.py:23081-23180`, on by default from b138, `nolightsframe` turns it off) defers that re-link to the last tick before each frame. That covers sleepers, through the anchor on the sleeper's relink at `:23150-23156`. It has no networked guard.
- **What would keep a ground item awake on a client** [D]:
  - The host's object states. `network_objects_handle_states` (`network_objects.c:2471-2578`) sets `_object_at_rest_bit` to the host's word on every state.
    - A moving state clears it and the client simulates the item (`items.c:576-777`).
    - A resting state more than 0.05 wu or about 11° off (`REMOTE_OBJECT_TOLERANCE` and `REMOTE_OBJECT_ANGLE_TOLERANCE`, `:203-205`) moves it, which changes its bytes and wakes it for 2 ticks.
    - At rest, the host sends 4 resting objects a tick round robin, plus 3 repeats as each comes to rest (`network_objects.c:107-115`, `:1379-1428`). That is cheap: one snap, then still.
  - Resting on a scenery or device object (`_item_on_object_bit`), which realigns it to that object's node 0 every tick (`items.c:808-822`). Items only rest on structure, scenery or devices (`items.c:650-656`).
  - A lit item whose `psp_lit_item_reason` is non-zero.
  - Garbage timers don't apply: `object_set_garbage` on landing only when `!game_engine_running()` (`items.c:666-670`).
  - `last_owned_time` is stamped only while attached (`items.c:927-930`) and is excluded from the checksum anyway (`psp_fixups.py` ~`:2389`).

## 4. Levers, ranked

Expected savings are per tick on a Prisoner-like client at the collapse (16–22 ticks a frame) [U unless marked]. "Remote" means a biped whose `unit.player_index` is a player that isn't local on a `network_game_distributed_client()`.

| # | Lever | Change and guard | Expected | Risk | Proof |
|---|---|---|---|---|---|
| 1 | **LD1 in the multiplayer build** (already in b138) | Nothing new. Sleeping lit ground weapons and lit held weapons re-link lights once a frame | Most of the weapons' 2.7–3.3 ms if they are lit sleepers (Damnation's 10 cost 4.5 ms); scales with ticks a frame | None for sync: links are read only by drawing, and upstream's server skips the cluster join (FIXES_LD1's note) | First read the existing run's "lit items" and "weapons on the ground" lines. Then a `[C]` join on b138 with `keepsets="- \| nolightsframe"`, comparing weapons ms and those lines. Exactness is already shown in the campaign by statecrc |
| 2 | **Remote bipeds' node matrices once a frame or on read** | Defer compute and postprocess for remote bipeds and their children. Settle on any read (nwbatch's accessor settling) or on the tick before a frame. Snapshot inputs at the eager point | ~0.3 ms × 5–6 remote ≈ 1.5–1.8 ms, plus the children's computes (part of the ~3.5 ms not in the three timed parts [U]) | Client-local. Look-only if wrong. Never reaches the host | A check mode (like `poseaheadcheck`) computing eagerly and comparing bit for bit at each read, on emulator `sierra-netplay` against a PC server. Then a PSP A/B in one join. Run `nmreaders` on netplay first to see who reads biped matrices within the tick |
| 3a | **Still remote biped: reuse its last sweep** | Remote, alive, grounded, not airborne or slipping. Host velocity 0, no throttle in its relayed action, not crouch-moving, same position, support surface and flags as the last sweep's result. Re-sweep when any of those change or every N ticks | Up to ~0.27 ms per still remote biped | Near-exact. Misses only an object moving into a still pill between re-sweeps, which the host's state corrects | Emulator netplay with a check word that sweeps anyway and compares the clipped position and out flags |
| 3b | **No pill sweep on catch-up ticks for remote bipeds** | On a client tick that isn't the last before a frame, advance a remote living biped by its velocity and keep its flags. Sweep on the frame's last tick, or every Nth, so it never stays in a wall | (1 − 1/ticks a frame) × 0.27–0.46 ms × remote ≈ 1.3–2.2 ms at 16–22 ticks a frame | Not exact. Intermediate ticks aren't drawn, but local shots hit-test those positions; the host allows 2–3 wu. Long batches can push a pill into geometry before the final sweep. A **behaviour change for Sheepdog01's A/B** unless side-by-sides show it plays and looks the same | `sierra-netplay` emulator side-by-sides (both halves on one server), counting `distributed_statistics.corrections` and the remote error before correction. Then a PSP join A/B |
| 4 | **Wake-on-write while networked** (b137 audit item 5) | Hook the network's direct writes: `handle_states`' `_object_at_rest_bit` write (`network_objects.c:2554-2555`), `reconcile`'s velocity writes (`:771-774`), `damage_set_network_state`, inventory moves and `network_objects_set_seat`. Then `wakeonwrite` on | ~1 ms at the campaign's rates [U, audit]. Removes the per-tick checksum of every sleeper, including 24 items | Exact if the hooks are complete | `wakecheck` on emulator netplay (it already sums and logs misses), then on |
| 5 | **Corpses sleep on clients** | Relax `!game_engine_running()` (`psp_fixups.py:2904`) to allow `network_game_distributed_client()`. Host rest states still wake them. Dead, at-rest bodies already skip the moving physics (`bipeds.c:3521-3526`) | ~0.5–1 ms per body per tick (pose, animation, unit update) × bodies. **Unknown: check whether the 6.3–7.5 bipeds include bodies** | Low: client-local, corrected by the host. The ground test applies | `itemsleepcheck`-style check on netplay. The body count comes from the run's log |
| 6 | Remote unit kinematics (full) | Replace the remote biped's physics with "follow host + extrapolate", as `distributed_object_follows_host` does for co-op vehicles | All sweeps of remote bipeds | High for look: no airborne or landing flags from the host. Would need the flags sent (a wire change against network version 29: not allowed unilaterally) | — |

**Not a lever:** `unit_update` and the rest of `biped_update` for remote bipeds. They make the animation, aim, weapon readying, firing effects and footsteps, none of which the host sends. Skipping them changes the look [D].

**Frame-level, outside the per-object question but the root of the collapse** [U]:

- The client simulates all elapsed time (`game_time.c:460-481`: on a client, `maximum_possible_server_time = final_local_time`).
- Each tick it falls behind costs a full tick, which is the spiral (7 → 20–28 ms ticks, 16–22 ticks a frame [M]).
- Every lever above saves more as ticks per frame grow. Levers 1, 2 and 3b are once-a-frame by design, so they directly attack the spiral.
- Capping ticks a frame and letting the clock jump changes local play feel. That is Sheepdog01's call, not proposed here.

## 5. What the campaign's sleep levers can share

- **LD1** applies unchanged: no networked guard. Remote bipeds' and ground weapons' lit attachments benefit at once (lever 1).
- **The sleep rule** (`psp_sleepy_update`, `psp_fixups.py:3028`) and its item, lit-item and tracking extensions (FIXES_123L) already run in multiplayer. Ground items do sleep there (24.3 asleep). What remains per sleeper:
  - the checksum, which FIXES_WAKE would remove (lever 4);
  - the light re-link, which LD1 covers;
  - its function values.
- **Corpse sleep** (FIXES_105/112/114: the ground test and idle count) carries to clients with the guard relaxed (lever 5).
- **Light fixtures sleep** (`fixturesleep`, LF; play-exact, b139's Damnation A/B) applies to multiplayer maps as is: Damnation's 27 fixtures cost 3.7 ms [M, audit]. Prisoner's share is not measured here.
- **`posehalf` and `machinesfar`** are refused when networked for a lockstep reason that doesn't hold for remote units. A remote-only variant of the pose part is lever 2's simpler cousin, but it needs objects at rest.
- **The nmreaders recon** (FIXES_134) and **nwbatch's** settle-on-read (FIXES_136, shelved) are lever 2's foundation. It is the same work as the campaign's lever #2 ("node matrices computed only when read", `levers_next_b138.md`), so one implementation serves both, with a remote-biped policy in multiplayer.

## Open questions the existing run can answer (no new run)

1. Read these lines from 39046598878's run-2 log:
   - "weapons on the ground per tick: N updated, M asleep";
   - "lit items per tick … kept awake …";
   - the per-type equipment account.

   They confirm whether the ~8 weapons are lit sleepers, so whether LD1 is the whole fix for them.
2. Count the bipeds that are bodies: the biped account against the living players. That sizes lever 5.
3. Split the biped's ~1.8 ms: type update + damage + node matrices ≈ 8 ms of 11.5–12.3. The other ~3.5–4 ms is children, functions and postprocess [U]. `nmreaders`' by-type table on a netplay emulator run would say how much belongs to held weapons.
