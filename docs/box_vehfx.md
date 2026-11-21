# Box: the vehicle effects (slice 1, the decals' grouped view test)

> **v0 (2026-11-21).** The lab's REBUILD §7 lists the box (`[box:vehfx]`, the lead 2026-11-21 02:5x). Its first step, the exact count (`vehfx_decal_count`), is done. This page covers the second step: a cheaper visibility test, made by groups and not mark by mark, and its exact proof. The build switch is `PSP_VEHFX=1`, off by default; keep words `vehfxgroup` and `vehfxgroupcheck`. Nothing here is priced on a PSP yet.

## 1. The ground

The vehicle effects on b40's warthog drive (b40_leg1, game ticks 16,500-19,500, PSP-3000, one run; the lab's `vehfx_drive_psp` [M]; which names belong to the warthog is [U]):
- **making:** ~0.7 ms a tick (decal_new_from_collision, effect_update, particle_new, decal_clip_to_surface, contrails);
- **drawing:** ~3.3 ms a frame. Decals take ~2.5 of it: `psp_ge_decal_visible` 3.91% of the drawing (~1.6 ms), `_rasterizer_decals_draw` 1.24% (~0.5), `psp_ge_decal` 0.96% (~0.4). Particles take ~0.8 ms.

The drive runs below real time on the tock (15.3 ticks/s, 7.7 fps).

**What the test does** (the lab's `vehfx_decal_count` [E, exact counts], sierra-batch 44888776590): per frame the walk tests 810-1,200 decals, and tyre marks are 730-1,100 of them. Only 23-33% pass. The rest fail, and they cost a datum, a definition lookup, a vertex-block lookup, a lock and a kept sphere each. Each passing decal is one `psp_ge_decal` call. 870-1,310 decals are live. Because DFADE keeps a mark for 30 s, the pool holds ~1,100 marks while the warthog drives, and ~3.2 marks are laid a tick (`vehicles_b40_census` [E]).

**Budget** (REBUILD §7): making ≤0.3 ms a tick; the decals' test and draw ≤1 ms a frame on the drive.

**What's been tried on this ground** (the lab's `levercheck.py decal`, `--ground effects`, `--ground decals`, 2026-11-21):
- **DECALTEST** (in, on): one kept sphere per decal against the main CPU's copy of the camera, so a failing decal takes no record. b40's drive frame went 133.4 → 113.0 ms (−15.3%) [M]. This is today's test, and this slice groups it.
- **DC** (stopped, "exact but slower"): a call-level test that walks every corner twice. Its `revive_if` asks for "a call test cheaper than the quad loop it skips". The grouped test reads no corners at all.
- **DECALRING** (in): the vertex cache's room is taken in turn. It bounded the cost of making a decal, not the test each frame.
- **DECALJOIN** (in): decals of one state are joined in the record. That cuts the ME's replay, not the walk.
- **DB, DM, RR3**: batching the draws. The ceiling is ~0 (0 of 459,025 pairs could be joined).
- **DCW, SMALLME, FLAMEDECAL, TREADRIBBON**: other grounds (the cache's search, placing work on the ME, stamps, the ribbon's look). None of them is a grouped view test.

Nothing groups the test itself. A group test matches REBUILD's "by the ring's cluster or batch, not mark by mark".

## 2. What the walk is, read in the engine [D]

- `render.c` draws the decals four times a frame (layers light, alpha-tested, primary, secondary). Each time it calls `rasterizer_decals_draw(cluster)` for every rendered cluster.
- `_rasterizer_decals_draw` walks one list: `decal_get_first_decal_index(cluster, layer)`, then `decal->next_decal_index`.
- Every decal is made through `decal_insert(…, next_decal_index = NONE, …)` (its only caller, `decals.c:2294`). That calls `decal_reinsert`, which puts the decal at the list's head. So **a list is in the order its decals were laid, newest first**, and a tyre trail's marks are consecutive in its cluster's list.
- The decal's slot is its vertex block's index. `datum_new` takes the lowest free slot, so slot order is not the order the decals were laid: groups by slot would mix old and new marks. **The groups are runs of the walk.**
- The PSP branch of the loop (FIXES_23) reads, per decal:
  - the datum;
  - `decal_definition_get`;
  - `lruv_block_get_address`, which only computes an address: no last-used stamp, no side effect (`lruv_cache.c:400`);
  - the colour;
  - the texture key, a pure memo of the tag (`psp_tex_bitmap_key` → `psp_rs_store`);
  - the vertex buffer's lock.
  
  It then calls `psp_ge_decal_visible`, and after that `psp_ge_decal` (pass) or `psp_ge_decal_passed` (fail). `psp_ge_decal_passed` does nothing unless `decaltesttouch` is set. So passing over a decal that fails changes nothing the picture or the game reads.
- Where a list changes [D, the mirror]:
  - `decal_delete` (the previous decal's link, the datum);
  - `decal_reinsert` (the new decal's link);
  - `decal_insert`'s branch for a given next (dead code today, hooked anyway);
  - `decals_disconnect_from_structure_bsp` / `decals_reconnect_to_structure_bsp` (every list);
  - `decals_initialize_for_new_map` / `decals_dispose_from_old_map`;
  - a game state read back (checkpoint, core), which brings whole lists.
  
  No port fixup writes a decal link (`psp_fixups.py`, grep).

## 3. The design

**A group** is a run of consecutive decals of one list's walk (up to `vehfxrunN` of them, 16 by default, 2-32). Each member has a kept sphere that is current, meaning the one today's test just used for it.

The group record is 32 bytes (`psp/src/box_vehfx.c`, `struct vehfx_group`):
- `first`: the full datum index of the first member;
- `after`: the decal the walk reached past the last member when the group was made, or NONE;
- `count`;
- one sphere that holds every member's sphere;
- a turn number.

There are 255 groups, and each decal slot names its group and the group's turn in 16 bits (`member[2048]`). The box's data comes to ~12.6 KB in all (8.0 KB of groups, 4 KB of slots, the open run), off the heap only in a `PSP_VEHFX=1` build.

**When groups are made:** during the walk, from what today's test has just done. `box_vehfx_seen` adds each decal with a current sphere that isn't in a live group to the open run. A run closes:
- at `vehfxrunN` members;
- at the list's end;
- at a decal that can't join (a decal with no sphere, a failed lock, a member of a live group).

When a run closes against the first member of a live group that has room, it grows that group backwards. Without that, the newest marks would make a small group every frame. Lone decals make no group.

The sphere is the box around the members' spheres, centred, with radius max(|cᵢ − C| + rᵢ) × 1.001 + 0.01.

**When groups are used:** `psp_vehfx_at` runs at the top of each decal in the walk, before the datum is read. If the decal is the `first` of a live group, the group's sphere is tested once with today's test, in its own float order (`sphere_out`):
- **out:** the walk goes on at `after`. No member's datum, definition, vertex block, lock or sphere is read.
- **in:** the members are walked and tested one by one as today.

**When groups are let go:** whenever the engine writes a decal's link to the next or deletes a decal, `psp_vehfx_touch(decal)` lets that decal's group go first (`FIXES_VEHFX`, `effects/decals.c`). `decal_delete` touches the decal and its previous decal, and `decal_reinsert` touches the decal it links. Every whole-list change (§2) forgets every group (`psp_vehfx_forget`).

**Why it is exact.** A decal is drawn iff today's test passes it, in the same order:
1. *The jump is the walk's own path.* A group's members were consecutive steps of the walk, and the step after the last one was `after`. A step changes only if a member's link is written or a member is deleted, and either lets the group go first. If `after` itself is deleted, today's walk still goes there, because the last member's link wasn't written. Only the head of a list can change without a write to a member, and a group is tested only when the walk reaches its `first`.
2. *Every member fails today's test.* Today's verdict for a member is its kept sphere against the frustum's five planes. Each plane's test is linear in the sphere's centre, and its normal has length `side` (1 for the near plane). So for a member sphere (cᵢ, rᵢ) inside the group's (C, R) with |cᵢ − C| + rᵢ ≤ R: if f(C) > R·s, then f(cᵢ) ≥ f(C) − s|cᵢ − C| > s·rᵢ. The margin (0.1% + 0.01 units) is far above the rounding of the test's sums: about 1e-4 units at 1,000 units from the eye [U]. A member's kept sphere can't change while it is a member, because the kept sphere is keyed by slot, identifier and quad count, and a new decal in the slot means the member was deleted. This is the same assumption as today's (`decaltestcheck`'s stale-sphere count).
3. *Nothing else is skipped:*
   - The texture key's memo is pure.
   - `lruv_block_get_address` stamps nothing.
   - A failed decal's `psp_ge_decal_passed` acts only under `decaltesttouch`.
   - The box stands aside whenever today's test wouldn't use the kept sphere: under `decaltestold`, `decalnocull`, `decaltestcheck`, `decaltesttouch`, before the first view, and when the GE isn't drawing.
   - The counters `decals_tested_out` and the recon `decalcount` count fewer tests. They are logs only.

**The contract.**
- **Reads:** the main view's camera copy (`main_view_*`, `psp_ge.c`), today's kept spheres (`decal_bounds`), and the decal walk's indices and links.
- **Writes:** its own tables only. It writes nothing of the game state, so statecrc's play and whole-state words are untouched.
- **Who reads it:** the decal walk, when it reaches a group's first decal.
- **Where it runs:** the main CPU, inside the drawing walk. It never runs on the ME: no VFPU, no system calls, no double. The group table is 64-byte aligned, at two records a line. No unaligned loads.

**Multiplayer.** Drawing only; nothing goes on the wire. co-op: the same in every role (a client walks the same lists).

## 4. The price, per decal

| | today [U on M and E] | with groups [U] |
|---|---|---|
| a decal that fails | test ~1.6 µs (1.6 ms ÷ ~1,000 tests) + the walk's own step ~0.5 µs (0.5 ms ÷ ~1,000): **~2.1 µs** | **0** inside a group that tests out (its share of one group test: one record line, about 25 flops, ≤0.5 µs a group); +~0.1-0.2 µs when it is walked (its slot's group looked up, the run's note) |
| a decal that passes | ~2.1 µs + `psp_ge_decal` ~1.5 µs (0.4 ms ÷ ~265) | unchanged, +~0.1-0.2 µs |
| a list walked | — | the view copied once (~0.3 µs), 100-200 lists a frame: ~0.03 ms |

**On the drive.** Let p be the share of tested decals passed over inside out groups.
- At p = 0.6: ~600 × 2.1 µs = 1.26 ms saved, against ~0.1 ms of group tests and notes. The decals' test and walk go ~2.1 → ~0.95 ms, and with the draws (~0.4) to **~1.35 ms a frame**, against the budget's 1 ms.
- The budget needs p ≈ 0.75, or a cheaper walked decal (slice 2, §6).

p is an exact count: the emulator batch below gives it (`vehfxgroup:` line, "decals passed over" against "decalcount"'s tested). The harness's synthetic drive passes over 44% at a run of 16 (§5), but its camera and clusters are invented. Only the PSP pair prices it.

## 5. The proof

- **Harness** (host, outside the repo): the box's own `box_vehfx.c` against today's test, on synthetic lists that change as the engine's do. The marks follow four trails, two ticks a frame. There are head inserts, deletes by age, room and at random, and forgets.
  - Over 20,000 frames: 0 frames whose drawn decals or their order differ, at runs of 8, 16 and 32 and at 300 and 3,000 units from the origin.
  - Check mode: 17.8 M members walked, 0 drawn, 0 broken.
  - With `decal_delete`'s touch of the previous decal removed on purpose, the walk parts from today's (1,489 frames of 3,000 differ, and walks loop). The check mode catches it as 18,227 broken, so the check proves that the hooks are complete as well as that the bounds hold.
- **The emulator** [E, exact] (the builder's batch; no run here):
  1. b40_leg1 held (padhold, seed 1) to exitat660, with the drive window gt 16,500-19,500. Build: `PSP_VEHFX=1` (+ `PSP_RECON=1` for the side with `decalcount`). Keep words `vehfxgroupcheck`. Pass: every `vehfxgroupcheck:` line reads 0 drawn and 0 off the path, and members > 0.
  2. The same build, `- | vehfxgroup`, statecrc on, seeds 1 and 2. Pass: play and both seeds identical at every tick. The game state isn't touched, so the whole-state word should also be identical. If it isn't, find out why before the PSP pair.
  3. Shots on the drive, `- | vehfxgroup`, pixel-identical (DECALTEST's decaltest6 recipe). Expected identical because the GE stream is the same.
  4. Strict (`-i`) on the `vehfxgroup` side.
- **The PSP** [M]: one pair on the drive stretch, `- | vehfxgroup`, repeat 2 (the builder's Ready line). It reads the drawing's decal share and the frame. The counts come from the emulator.

## 6. Next

- Slice 2, the walked decals: the per-decal sphere test's sums made once a view (`side_x`, `side_y` and the half-angles are worked out each call today), and the kept spheres packed by slot. This is exact. It is the cost left once p is known.
- Making (≤0.3 ms a tick): `decal_new_from_collision`, `effect_update`, `particle_new` on the drive. Not started.
- Particles' draw (~0.8 ms a frame): not started.
- If the run cap matters: a second level (groups of groups) for lists over ~250 decals.
