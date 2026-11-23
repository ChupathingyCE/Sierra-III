# Review: the world store's slice 1 (`PSP_TICK2=root`)

What slice 1 changes, why, and where to look. The store's design is `docs/box_world_store.md` §14; the lab's `tick_store_spec_v1.md` §17-28 holds the rulings.

## Step 1b: the root leaves the datum

- **`psp_fixups.py` `tick2_root_cut`:** the eight root fields (position, velocities, forward, up, location, bounding sphere) leave `_object_datum` for one pointer to a record in the store. 1,097 touches are rewritten to go through the pointer. The 16 datum sizes in `object_types.c` and the engine's layout checks past the root run (28 at the pin, those written over several lines included) move by 0x50.
- **The store's records** (spec §26-27): each map's count comes from its peak of live objects plus a margin, carved from the empty object pool's tail at map start. Past them, overflow chunks of 64 records are pool blocks, re-pointed after each compaction. The store refuses an object only where the old pool would.
- **The collector** counts the store's free records as free pool. Its `gccount` line (with `PSP_GCCOUNT=1` on the old side) gives deletions, the pool's size, peak and free low, live objects, and the store's records, peak and overflow.
- **Biped motion's window** (`bipedmovestore`) can't go live under root: its map is the old biped datum's.

## Literal offsets and sizes into datums (spec §28)

A field found by a number is invisible to the compiler and to a layout pass. d20 parted because the sleep's checksum skipped words at old-layout offsets. Every one found in `psp_fixups.py`, `psp_post.h`, `psp/` and the engine's own text is listed here, with what replaced it:

| Where | Was | Now |
|---|---|---|
| The sleep's checksum rules (objects.c): a unit's round-robin timer skipped | `0x1BC / 4` | `psp_datum_unit_timer / 4` (units.c: `offsetof(struct unit_datum, unit.timer)`) |
| The checksum rules, the sleep checks (itemsleepcheck, weapontrack, the debug compares): a weapon's last-owned stamp | `0x1B4 / 4` | `psp_datum_item_owned / 4` (units.c: `offsetof(struct item_datum, item.last_owned_time)`) |
| The biped's kept tail (bspr: object_types.c, collision_bsp.c, bipeds.c, objects.c): the biped datum's size, the tail's start and the datum size set at load | `0x480` | `psp_datum_biped_bytes` (units.c: `sizeof(struct biped_datum)`) |
| The sleep's sums and checks: the datum's visit stamp passed over | word `2` | `PSP_DATUM_STAMP_WORD` (psp_post.h: `offsetof(struct object_datum, object.magic_number) / 4`) |
| The engine's door test (device_machines.c, ChupathingyCE's own text): a dead unit can't open a door | `*((byte *)unit + 0xb6)`, bit `_object_function2_active_bit` | `unit->object.damage_flags`, `_object_dead_bit` (`datum_raw_reads`). This one parted d20 (the lab's tick2root7d: a door opened at tick 1,131 under root, from a byte of another field) |

Also found and left, with why:
- **The 2,048- and 2,560-byte caps** on the sleep's copies and sums: buffer sizes, not offsets.
- **Biped motion's v0 window** (`psp/include/psp_bipedmove_map.h`, `box_ws_move_map_c[1152]`; `psp/src/box_world_store.c`'s runs; `BOX_WS_DATUM_BYTES`): byte maps generated from the old biped datum. They're live only under the keep word `bipedmovestore`, which `PSP_TICK2=root` forces off. Slice 1's BIPED section retires them.

The check: `grep -nE '0x[0-9A-Fa-f]{2,4} */ *4|(byte|char) \*\)[^;]{0,60}\+ *(0x[0-9A-Fa-f]{2,4}|[1-9][0-9]{2,3})\b' psp_fixups.py psp/src/*.[ch] psp/include/*.h` finds none into a datum.

## Proof

- [E] tick2root6 (the lab's sierra-batch 45119859571): exact on a10, d40, b30 and c20; d20 overflowed 38 records with the same deletions.
- [E] d20 parted at tick 1,135 in device groups (tick2root6r, 45185417617), from the sleep checksum's literal offsets above; fixed at be0dcdd9.
- [E] tick2root7 (45187897160): exact on four; d20 still parted: the sleep's literals were real bugs but not its cause.
- [E] tick2root7d (45193358949): the device groups' trace: a door's group set open at tick 1,131 under root only, from the engine's raw read above. Fixed at cb636b18.
- [E] tick2root8 (45196779341): the old side identical to tick2root7's (the door's fix is exact for it); root parted on b30 and d20 from tick 0, because the census' counts (1,296) overran the free stack's 1,280. Fixed at 6d32bda0 (room 1,536, counts clamped).
- [E] **tick2root9 (45201040108): exact on all five** (a10, d40, b30, c20, d20): play and both seeds at every tick. The collector deleted the same on both sides (11 on d20, none elsewhere), with no overflow on the census' table. The pool's free low, old against new: d20 149 against 127 KB, b30 364 against 330, c20 424 against 396, a10 551 against 543, d40 621 against 609.
