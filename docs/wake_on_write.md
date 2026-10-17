# Sleepers woken by writes (lever WT): design (2026-10-16)

**Status (2026-10-17):** built and measured on branch `wake-on-write` (`e2f4c79`), keep words `wakeonwrite` and `wakecheck`; to ship on by default in the next build. Play exact against the checksum (a50, c10, d20) and no missed writer left (b30, its route, c40) [E]; on the PSP-1000, b30's b113 route, the frame −5.4 to −8.7 ms (−3.7 to −4.8%) and the tick −2.3 to −2.8 ms [M]; on the PSP-3000, a50, the tick −1.6 ms (−6%) [M]. The writers it took beyond §2's list, the results and why the gain is the low end of §4's estimate: the lab's handoff of 2026-10-17 (recon) and its findings tables. Sized by recon 134 (`recon_134.md`: `sleepcensus`, and the object access trace `objtrace134`).

Labels: **[M]** measured on a PSP · **[E]** emulator · **[D]** read in the code · **[U]** inference.

## 1. What it replaces

Today a sleeping object is checked every tick (`psp_sleepy_update`, `psp_fixups.py`). Its whole datum, up to 2 KB, is checksummed with the words the rules leave out (`psp_checksum_rules_for`: word 2, the object marker; a unit's timer; a weapon's last-owned time; a corpse's idle ticks, aim and function values). The checksum is compared with the one stored when it fell asleep. If they match, its update is skipped; if not, it wakes.

- **Cost:** 3.8–6.8 ms a tick on b30 [M, PSP-3000], almost all of the scenery's share of the tick. On a50 that is 266–540 sleepers a tick [E].
- **Waste:** 0.00–0.26% of the checks find a change [E, `sleepcensus`]. Nearly every checksum confirms what was already known.

## 2. What the trace says about writers [E]

`objtrace134`, eight maps, hands off. These are stores into an object asleep at the tick's start, made outside its own update, that changed its bytes, leaving out:
- the object marker (`object_mark_function`: word 2, which the checksum skips);
- memory moves (`memmove`, `memset`, `memcpy`, `memory_pool_compact`: the pool compacting or a freed slot reused).

| Map | Changing stores a tick | Writers |
|---|---:|---|
| a10 | 0.65 | node matrices made outside an update (`object_new` placing a vehicle) |
| a30 | 0.003 | the render state cache |
| a50 | 0.03 | the render state cache, `unit_update_driver_and_gunner` |
| b30 | 0.12 | the render state cache, `object_cause_damage`, `object_set_garbage`, `unit_control`, `object_set_visibility` |
| b40 | 0.002 | `object_cause_damage` |
| c10 | 0.03 | the render state cache |
| c20 | 0.004 | the render state cache, deletes, map disconnects |
| d20 | 0.62 | connecting and disconnecting from the map on the BSP switch |

The render state cache (`object_get_cached_render_state`: the datum's `cached_render_state_index`) is the renderer's own memo. It doesn't feed the update. The other writers are events: damage, garbage, visibility, control, creation, map changes.

**What the hands-off runs can't show:** the player, scripts (teleports, `object_set_*` from the scenario's scripts), blasts on scenery, pushes, attaching and detaching. The trace gives the floor of the writer list, not its whole.

## 3. The design

1. **A dirty bit an object** (`psp_sleep[]` gains a bit). It is set by every function that changes an object's data from outside its own update, and by its children's and its device groups' changes (the checksum's family). A sleeper with the bit clear skips its update with no checksum. One with it set is checksummed as today: woken if changed, asleep again if not, so a false alarm costs what every tick costs today.
2. **The writers, found by reading the code, not by the trace alone.** That means every function reachable from scripts, damage, physics, units, AI and the network that writes an `object_datum` field of an object other than the one updating. The trace's list is where the reading starts; `objtrace` is how it is checked.
3. **The check that keeps it honest:** keep word `wakecheck`. Every tick it also checksums each sleeper whose bit is clear, and logs any that changed (with the object's definition and the tick): a writer the list misses. Run on the emulator ladder with routes (the player's input), statecrc against the checksum version. A miss found is a writer added.
4. **A safety net in play** (to be decided after the check, [U]): every Nth tick, a full checksum of one Nth of the sleepers. A missed writer would then delay a wake by at most N ticks instead of forever. If the check finds no misses over the ladder, it may not be needed.

## 4. What it's worth [U]

The checks' 3.8–6.8 ms a tick on b30 drop to the cost of testing one bit a sleeper, plus the rare checksum of a dirtied one: about −7 to −13 ms a frame on b30 (two ticks), 3–5% of the ride's frame, more on scenery-heavy maps (a50, c10). With frames in flight, a millisecond off the tick is a millisecond off the frame.

## 5. Order of work

1. Read the code for the writers (the engine's object, unit, damage, script and network code), starting from §2's list. Write the list into this doc.
2. Build the dirty bit, its setters and `wakecheck`, behind a keep word (`wakeonwrite`). Off: nothing changes.
3. Emulator: `wakecheck` on the ten-level ladder with routes; statecrc pairs. A clean ladder decides whether the safety net is needed.
4. PSP: an A/B in one build (`keepsets="- | wakeonwrite"`), b30's beach and a50.

Multiplayer: the sleep system is the campaign's and the server's simulation is ChupathingyCE's own, so a sleep change must keep the multiplayer build's play in step with the server. That's checked by the lab's `mpcheck` and a netplay run before it ships.
