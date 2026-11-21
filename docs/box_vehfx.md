# Box: the vehicle effects (slices 1 and 2, the decals' grouped view test and the packed walk)

> **v0 (2026-11-21).** The lab's REBUILD §7 lists the box (`[box:vehfx]`, the lead 2026-11-21 02:5x). Its first step, the exact count (`vehfx_decal_count`), is done. This page covers the second step: a cheaper visibility test, made by groups and not mark by mark, and its exact proof. The build switch is `PSP_VEHFX=1`, off by default; keep words `vehfxgroup` and `vehfxgroupcheck`. Nothing here is priced on a PSP yet. **Slice 2 (2026-11-21, §6):** a group in the view walked from the box's own copy of its members, keep words `vehfxpack` and `vehfxpackcheck`.

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
- The budget needs p ≈ 0.75, or a cheaper walked decal (slice 2, §6, built).

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

### 5.1 The emulator proof (2026-11-21) [E; the lab's findings `vehfx_group_identity`]

- **Exact:** on b40_leg1 to 660 s, `vehfxgroupcheck` walked 1,360,661 members of groups that tested out; today's test drew none of them and no walk left a group's path.
- **Play and seeds:** identical at all 19,800 ticks, the word off against on, seeds 1 and 2.
- **The whole-state hash** differs on `objects` and sometimes `particle`, from ticks 726-1,217. That comes from the run's timing, not the grouping: the check run draws exactly as the word off does, yet parts from it at the same tick and checkpoints. The decal allocations are identical at all 660 checkpoints in every pair, and `objects` and `particle` are the set known to move with the emulator's speed (review_prs.md, review_b123_flags.md) [D].
- **The pass-over share on the drive:** 417-731 decals passed over a frame, against ~810-1,200 tested (`vehfx_decal_count`), so p ≈ 0.55 [E]. That's under the ~0.75 §4 needs for the 1 ms budget, so slice 2 is likely needed; the PSP pair prices it.

## 6. Slice 2: the walked decals cheaper (`vehfxpack`)

The lead (2026-11-21 10:0x): with p ≈ 0.55 [E] against the 0.75 the 1 ms needs, make each walked decal cheaper. What's been tried here is §1's list; this slice adds nothing to DECALTEST's test (it is the same test) and doesn't batch draws (DB, DM, RR3: 0 of 459,025 pairs joinable exactly). It attacks `cpu_cost_map.md` §4's **Data density and layout** line (D misses; its ceiling there is sized for the objects, not the decals): a walked decal's cost is mostly the scattered lines it touches.

### 6.1 What a walked decal spends today [D for what's read; U for the cost]

After slice 1, ~460 decals a frame are still walked on the drive (~1,020 tested less ~556 passed over), ~265 of them pass and ~200 fail. Each one, in `_rasterizer_decals_draw`'s PSP branch (FIXES_23, with FIXES_VEHFX):

| step | what it reads | lines (64 B) | cost [U] |
|---|---|---|---|
| `DECAL_GET` (datum_get) | the 56-byte datum: identifier, flags, bitmap, colour, intensity, quad count, definition, next (offsets 0x00-0x37) | 1, or 2 when the record crosses a line (6 of the 8 alignments a 56-byte stride takes): ~1.75, scattered (slots aren't in walk order, §2) | ~1.75 misses |
| `decal_definition_get` (tag_get) | the definition's shader: blend, map | the drive has a few definitions (snow and ice treads): hits | a call |
| `lruv_block_get_address` | the 32-byte vertex block's first page | 1, scattered | 1 miss + a call |
| texture key memo | the last key | hit; `psp_tex_bitmap_key` when the bitmap differs from the last decal's (a tread's sprite is random, so often) | a call when it changes |
| Lock, `psp_ge_active`, Unlock | — | — | three calls |
| `psp_ge_decal_visible` | the 20-byte kept sphere | 1, or 2 in a quarter of the alignments: ~1.25, scattered | ~1.25 misses + the sums: 2 `sqrtf` (31 cycles each [M], `hardware_facts.md`), a divide, ~40 flops: ~0.3 µs |
| `psp_vehfx_seen` | `member[slot]` (4 KB) | mostly hits | ~0.1 µs |

That's ~4 scattered misses a decal. At 73 cycles a miss with the bus quiet and ~140 beside the GE reading RAM (`hardware_facts.md`), ~0.9-1.7 µs, plus ~0.3 µs of sums and ~0.2 µs of calls: **~1.4-2.2 µs**, which matches §4's ~2.1 µs (the PSP's time over the emulator's count). A decal that passes then costs `psp_ge_decal`'s ~1.5 µs (its record and the vertices' pointer for the ME).

### 6.2 The candidates

| | saves a walked decal [U] | exact? | cost |
|---|---|---|---|
| **A. the frustum's sides once a view** (the agent before's suggestion) | 2 `sqrtf` and a divide, ~70-100 cycles, ~0.2-0.3 µs a test asked | yes: the same expression on the same float, made once; Allegrex has no fused multiply-add (GCC's ISA for it is MIPS II), so the floats are the ones the test makes today. The check compares them bit for bit each walk | 3 floats |
| **B. the group's members packed in the box's own table, walked from there** | a failing member: ~4 scattered misses and the sums become a fifth of a sequential line and ~30 flops, ~2.1 → ~0.2 µs; a passing member: `psp_ge_decal_visible` (its sphere's line, the sums) skipped, ~0.5 µs, for ~0.15 µs of the box's test | yes: the member's verdict is today's test on today's floats (its kept sphere copied bit for bit when it joined, and it can't change while it's a member, §3 point 2); a failing member skips exactly what an out group's members skip in slice 1 (§3 point 3); the order and every argument of `psp_ge_decal` are the same, so **the same GE words** | 80 KB of `.bss` in `PSP_VEHFX` builds (320 bytes a group, 255 groups), a `memmove` when a group grows backwards, runs capped at 16 |
| C. a cheaper member test inside an in group | flops only (~0.1 µs): the cost is the misses, which B removes; a cheaper conservative test must still fall back to today's for the members it can't decide | yes, with a fallback | folded into B: not worth its own code |
| D. consecutive members' GE words batched | — | no exact form: DB/DM's ceiling is ~0 (0 of 459,025 pairs share the mip slope); its non-exact form is DB's parked look question | not this slice |
| E. a passing group's draw words kept between frames | up to `psp_ge_decal`'s ~1.5 µs a draw | no: a mark's intensity is rewritten from its age every tick while it fades (`decals.c` 2615, DFADE), and each frame's record lands at new list addresses, so the words aren't identical; keying them by intensity leaves a copy as dear as the record | not this slice |

**Recommended and built: B with A**, as one keep word, `vehfxpack`.

### 6.3 The design (`psp/src/box_vehfx.c`, `FIXES_VEHFX`)

- **The pack:** each group keeps its members in walk order, `index[16]` (the full datum index) and `sphere[16][4]` (centre, radius): 320 bytes, five lines, 64-byte aligned. A run made into a group copies its members' kept spheres (`box_vehfx_seen` already has them); a run that grows a group backwards is put in front of its members. `vehfxpack` caps runs at 16 (`BOX_VEHFX_PACK_MAX`).
- **The walk:** when a group tests in, `psp_vehfx_at` walks its members from the pack, from the decal the walk is on. A member whose sphere fails today's test is passed over without its datum, definition, vertex block, lock or kept sphere being read. At the first member that passes, the walk is left on it with `psp_vehfx_known = 1`: the engine's body reads it as today and takes the verdict as known (`FIXES_VEHFX`: `psp_vehfx_walk && psp_vehfx_known ? 1 : psp_ge_decal_visible(...)`). The body's own step to the next decal comes back to `psp_vehfx_at`, which goes on from the next member; past the last member, the walk goes to the group's `after`.
- **The sides:** `main_view_begin` works out `half_x`, `side_x` and `side_y` once a view (in a `PSP_VEHFX` build); under `vehfxpack`, `psp_ge_decal_visible` reads them for the decals it is still asked about, and the box's view takes the same three floats, so both tests use the very same values.
- **Why it is exact:** a group's members, in order, are the walk's own path while it lives (§3 point 1); each member's verdict is today's test on today's floats; a member that fails skips only what slice 1 already skips (§3 point 3); a member that passes reaches `psp_ge_decal` with the same arguments in the same order. So the same decals are drawn in the same order with the same GE words. The box still stands aside wherever slice 1 does.
- **The check (`vehfxpackcheck`):** every member of a group in the view is handed to the body, which asks today's test; the box counts verdicts that differ from the pack's, steps that leave the members' path (a member not where the pack says, a group let go mid-walk, a list ending inside a group), and walks whose stored sides differ from the sums made as the test makes them. All three 0 is the proof. `vehfxpack`'s own log line counts the members passed over and sent on known a frame, and the steps off the path, which should be 0 there too.

### 6.4 The price [U on §4's M and E]

On the drive, at slice 1's p ≈ 0.55: of the ~460 decals walked a frame, most are members of groups in the view (~25-38 such groups, ~12 members each by `vehfx_group_identity`'s counts; the harness gives 94%): take ~85%.
- ~170 failing members × ~1.9 µs = **~0.32 ms**;
- ~225 passing members × ~0.45 µs = **~0.10 ms**;
- ~70 walked decals outside a group × A's ~0.25 µs = ~0.02 ms.

**~0.44 ms a frame saved.** Slice 1 alone at p ≈ 0.55 comes to ~1.45-1.5 ms (walked 460 × ~2.25 µs, group tests ~0.04, draws ~0.40, views ~0.03); **with slice 2, ~1.05 ms against the 1 ms budget** [U]. What's left is mostly the draws: each passing decal's datum, definition and vertex block (~1 µs) and `psp_ge_decal` (~1.5 µs), ~0.65 ms in all. The next cut there: the pack also holding a member's draw arguments that can't change while it's a member (its vertex address, blend, map, quad count, permanence), so only the datum's colour and intensity are read; ~0.3-0.4 µs × ~265 ≈ 0.1 ms [U].

### 6.5 The proof

- **Harness** (host, outside the repo; the box's own `box_vehfx.c` against today's test on §5's synthetic lists):
  - Over 20,000 frames at runs of 8, 16 and 32 (32 capped to 16) and 300 and 3,000 units out: 0 frames whose drawn decals or their order differ, 0 "known" verdicts today's test wouldn't give, 0 steps off the path. At a run of 16 the body walks 1,136 decals a frame without the pack and 356 with it, and today's test is asked 69 times a frame instead of 1,136 (synthetic: its camera and clusters are invented).
  - Check mode: 21.3 M members handed to the body, 0 differ, 0 broken.
  - With `decal_delete`'s touch of the previous decal removed on purpose, the pack's walk parts from today's (23 frames of 300 differ) and counts 25 steps off the path even without the check; the check counts 20,245 verdicts that differ and 22,421 steps off the path over 3,000 frames. So both lines catch missing hooks.
- **The emulator** [E, exact] (the builder's batch; no run here):
  1. b40_leg1 held (padhold, seed 1) to exitat660, `PSP_VEHFX=1`, keep words `vehfxpackcheck vehfxgroupcheck`. Pass: every `vehfxpackcheck:` line reads 0 differ, 0 off the path, 0 sides differing, with members walked > 0; `vehfxgroupcheck:` still 0 and 0.
  2. The same build, `- | vehfxgroup | vehfxpack`, statecrc, seeds 1 and 2. Pass: play and both seeds identical at every tick. The drive's `vehfxpack:` lines give the exact counts this section's price assumes (members passed over, sent on known).
  3. Shots on the drive, `vehfxgroup | vehfxpack`: pixel-identical (the GE stream is the same).
  4. Strict (`-i`) on the `vehfxpack` side.
- **The PSP** [M]: one pair on the drive stretch, `vehfxgroup | vehfxpack`, repeat 2, in one build (an ask through the master). It reads the drawing's decal share and the frame.

## 7. Next

- Slice 3 candidate: a passing member's draw arguments from the pack (§6.4), once the PSP has priced slices 1 and 2.
- Making (≤0.3 ms a tick): `decal_new_from_collision`, `effect_update`, `particle_new` on the drive. Not started.
- Particles' draw (~0.8 ms a frame): not started.
- If the run cap matters: a second level (groups of groups) for lists over ~250 decals.
