# b105 candidates (worked out 2026-10-07, while b104 waits for hardware)

Nothing here is built. Each item says what the evidence is and what the change would be. Labels: **[M]** measured, **[D]** read in the decomp, **[U]** inference.

## 1. Settled corpses: stop updating them

**The cost [M, b102 run 3].** In the Library's first Flood fight, biped updates went from 30 to 116 a tick at about 0.74 ms each, while the AI had at most 35 actors. Tick time went from 30 to 118 ms (2.4 fps).

**What Halo does with a dead biped [D].**
- `biped_update` runs in full for the dead (`units/bipeds.c`): moving, physics, the unit's animation, footsteps, falling danger, the discard check. Only the turning step is skipped. `biped_update_dead` itself is cheap once the limp-body relaxation has finished.
- The game already knows when a corpse has settled. `biped_update` ends with: dead **and** `_object_at_rest_bit` → `++object.idle_ticks`, else 0. The at-rest bit is set by the biped's own physics: on the ground, not flying, no volatile collision, speed² < 0.0001.
- A truly dead unit is flagged garbage at death (`units.c`, `object_set_garbage(unit, TRUE)` when not feigned). A Flood combat form that **feigns** death (and later gets up: `_unit_state_resurrect_front/back`) is not garbage until it dies for real.
- The garbage collector (`objects.c`) only starts at 50 active garbage objects (target 30, for up to 16 players), and **never collects an object any player can see**. In a one-room fight every body is in view, so none is collected. That explains run 3's 80-odd corpses.

**The change:** let a biped sleep under the existing byte-identical rule (b19's scenery sleep, `psp_sleepy_update`) when it is:
- dead,
- garbage (truly dead, not feigning),
- at rest with `idle_ticks` past about 60 (2 s),
- carrying nothing.

`idle_ticks` is left out of the checksum and advanced by hand while the biped sleeps, so anything that reads it (the multiplayer body clean-up in `game_engine.c`) sees the same value. Any outside change (a grenade's push, damage, a script) alters its data and wakes it, as for scenery. Keep word `corpsesawake` turns it off.

**Expected gain [U]:** a sleeping corpse costs a checksum (about 20 µs) instead of a full update. With 80 settled bodies that could be tens of ms a tick in the fight. The 0.74 ms average includes the living, so the real figure needs measuring.

**Prototype [dev tree, not shipped]:** written as above (`psp_fixups.py`'s `psp_object_update`, with `corpses` added to the objects_update line's asleep counts). Builds clean; the Library runs. It hasn't been tested on real corpses yet. In the emulator, `ai_place` then `ai_kill` of enc1, enc1b and enc2 at tick 300 dropped biped updates from 30 to 10 a tick, because bodies outside the player's active area stop updating anyway. So the test needs a fight near the player: on hardware, or the emulator with the player teleported to an encounter.

**Multiplayer:** sleeping changes nothing the simulation computes, so lockstep is safe as long as the skipped update really is a no-op. The byte-identical test is that guarantee, but it should still be campaign-only until multiplayer lockstep is tested.

## 2. Checkpoint hitch

**The cost [M, b102 run 6]:** one checkpoint froze the game for **1.76 s** (388 of 837 pages of 4 KB, 1.55 MB: about 0.9 MB/s). The first, at load, took 173 ms for 213 pages.

**How it's written now** (`psp_fixups.py`, `psp_state_write`): a 2 KB header with a page bitmap, then every non-empty page, packed one after another. So every checkpoint rewrites everything.

**Options:**
- **Write only the pages that changed since the last checkpoint.** That needs a fixed position per page in the file instead of packing, plus a 4-byte hash per page kept in RAM (3.3 KB). Hashing 3.3 MB costs some CPU [U: tens of ms]. The win depends on how many pages change between checkpoints, which is unmeasured. An emulator measurement (two snapshots 10 s apart, pages that differ) decides it.
- **Measured in the emulator [M, dev build with a page-hash diagnostic, The Library on Sheepdog01's run-3 route]:** first checkpoint 213 pages; the second 232 pages, of which **135 changed** (58%). So writing only the changed pages would save about 40% early in the level, and probably less in a fight. That's not the fix on its own, and scattered 4 KB writes may cost more per byte than one long write.
- **Find where the 1.76 s goes first.** The diagnostic now in the dev build logs the scan's time apart from the writes'. 1.55 MB in 1.76 s is 0.9 MB/s, far under the stick's 16 MB/s reads (halo_bench v10). The write runs under the stick lock alongside sound streaming, so the log's stick report around a checkpoint would show whether it waited on the lock or on the card.

## 3. The bus-priority probe

Read-only, through the ME library's `kcall` (already loaded with the ME): log once at start-up the registers psdevwiki lists but doesn't explain:
- DDR controller: `0xBD000000`–`0x44`
- GE: `0xBD400004`, `0x104`, `0x200`, `0x300`
- EDRAM: `0xBD500000`–`0xA0`
- System controller: `0xBC100040`–`0x5C`

Details in psp_hardware_reference.md §9.1.

## 4. Smaller

- **Handle-table noise:** "opens failed" includes ordinary file-not-found probes. Split the count by error code, so only the interesting failures stand out.
- **stderr/stdout:** two handles on the same `stderr.txt` (two `freopen` calls). One would do.

## 5. The b103 load hang: emulator, strict interpreter [M]

The Library with Sheepdog01's exact keep words (assettrack, hwprofile, handletest, geo2), b103, strict checks: it loads past the stencil bake and plays 20 s with **0 bad accesses**. The hang doesn't reproduce in the emulator, so it depends on hardware timing, as a lock problem would. b104's on-screen `LOCK:` lines are the instrument for it.

## Decisions (Sheepdog01, 2026-10-07 evening)

- **The campaign build carries no multiplayer.** Code that is dead weight for the campaign goes. Multiplayer becomes its own build, parallel to the campaign one, sharing what it can. The multiplayer maps must still load in the campaign build for testing, as they have so far (`map_name` with no game engine running).
- **Dead code (lever 5) gets a thorough investigation with full coverage** before anything is cut; Sheepdog01 may raise the reasoning effort for that pass.
- **Sound facts that shape the bake:** the port's mixer runs at 22,050 Hz (`psp_dsound.c` `OUTPUT_RATE`) and the PSP's sample-rate converter takes it to the 44.1 kHz output. So 44.1 kHz music is already heard at 22 kHz today; baking it at 22 kHz removes data that is read from the stick and then thrown away.
