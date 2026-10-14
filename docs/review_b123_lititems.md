# Review section for build 123: resting lit items sleep

For `docs/review_b123.md`. Same shape as `review_b120.md`: **what**, **key lines**, **why**, **proven**, **default**. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

Branch `lititems123` (from `w122`, `85ebea3`).

| Change | Kind | Default |
|---|---|---|
| Resting weapons and equipment with attached lights may sleep, their lights reconnected as their update would | exact speed; **never reached**: another test keeps every lit ground weapon awake (§4), so no gain | on (`litemsawake` off) |
| The lit items counted on a line of their own, with the sleep test each one fails | report | always |
| `pillreach`: how far a biped's pill sweep reaches (for `plan_biped_collision_me.md`) | recon, `PSP_DEBUG_TOOLS=1` only | off (opt-in) |

**In short.** The change is exact and changes nothing else, but it never gets to act. Lit weapons do lie on the ground (the assault rifle, pistol, needler, plasma pistol and plasma rifle: 2–20 a tick), and every one that comes to rest is kept awake by the sleep rule's **tracking test**. That test fires because `tracked_object_index` is never initialised: it is 0, not NONE, and 0 names whatever object sits in slot 0 (§4). The lights exclusion was never the binding one. An exact lever follows from it (§4); not built.

---

## 1. Lit items sleep (`4b45c0f`)

**What.** b120's item sleep (`review_b120.md` §3) left out items with attached lights. Their update reconnects their lights, which puts each first in its clusters' lists, and a sleeper's lights would have stayed where they were. b121's `machinesleep` fix (`f6d2617`) solved the same thing for machines: asleep, the machine still reconnects its lights, at its own turn in the tick. Items now do the same, and may sleep.

**Key lines.** `psp_fixups.py`, `FIXES_123L["source/objects/objects.c#litems"]`:
- `psp_item_may_sleep`: the lights test moved last, so it counts only the lit items nothing else keeps awake. They're offered to the rule unless `litemsawake`.
- `psp_sleepy_update`'s asleep branch: `object_connect_lights` for a sleeping machine **or a sleeping item with attached lights**, under the same flags `object_update` tests (dynamic lighting, and not invisible unless it has no model).

**Why it's exact** [D].
- The byte-identical rule already covers everything else the update reads (b120's table).
- `object_connect_lights` reads the light's marker on the item (the item's node matrices, unchanged while it sleeps) and the item's location. It writes only the light and the light lists. Called at the item's own turn, the lights land in the lists in the order the update would put them.
- Nothing in an item's update reads its lights back.

**Counted** [E]. `lit items per tick` is the lit items that nothing else keeps awake:

| Run | Lit items kept awake a tick (old rule) |
|---|---|
| d20, hands off, 18,468 ticks (lab 38614501309) | 0 (91 reports) |
| c20, hands off, 23,731 ticks (same) | 0 (105 reports) |
| c20, `itemsleepcheck`, 16,256 ticks (lab 38614681696) | 0 (52 reports) |
| b30, the b113 route (same batch as d20) | 0 in the 16 reports the log kept |
| a50 | not counted: the object report's line was cut at the log's 512 bytes |
| Blood Gulch, multiplayer build (lab 38614681696) | no items at all (no game running in the harness) |

b30's beach and a50 lost the counts to that cut, so `c2c041f` gives the lit items a line of their own.

**The census** (`c2c041f`, lab 38623624370, hands off to a 420 s wall, b30 by the b113 route) [E]. Lit weapons updated a tick, mean and most, and those offered to the sleep rule:

| Map | Lit items updated a tick | Offered | Asleep |
|---|---|---:|---:|
| a30 | 3.0, 3.0 | 0 | 0 |
| a50 | 1.8, 2.0 | 0 | 0 |
| b30 (route) | 7.4, 18.9 | 0 | 0 |
| b40 | 3.0, 3.0 | 0 | 0 |
| c10 | 8.0, 8.1 | 0 | 0 |

Every lit definition logged is a weapon: `weapons\assault rifle`, `pistol`, `needler`, `plasma pistol`, `plasma rifle`. None was ever offered, so a test before the lights one keeps them awake: §4 finds which.

**Gain** [E]. None: no lit item slept in any run.
- Cache models, `litemsawake` against on, to game time 40 s: b30's route 47.818 → 47.817 ms est., a50 16.285 → 16.285 ms (labs 38614501309 and 38614681696).
- The two sides' fetches differ by 53 on b30 and 5 on a50 out of millions a tick: the extra flag test, nothing else.

**Proven** [E], on against `litemsawake`, hands off (`nearauto assettrack geo2 nomeaudio fixedticks fixedseed statecrc`), `tools/statecmp.py --ignore="object looping sounds,first person weapons"`, lab 38614501309:
- **Play exact.** The play hash and both random seeds are the same at every common tick: d20 18,468 (across its BSP switch), a50 31,420, c20 23,731.
- **The light lists** (`lights`, `light`, `light cluster reference`, `cluster light reference`) are the same at every checkpoint on all three.
- The whole state differs only in `objects`, the render pass's mark that follows the emulator's speed (`big_levers.md` §5, #25). c20's last checkpoint (23,730) is cut mid-line by the wall and left out, as in the earlier reviews.
- `itemsleepcheck` (every sleeper updated anyway, any change logged; lab 38614501309): 0 changed, by the last count lines, of 60,000 sleeper updates on b30's route and 120,000 on a50.
- **What that proves.** Since no lit item slept, these runs show that the change disturbs nothing else. They don't exercise the new branch. The branch is the machines' (`f6d2617`, proven with lit machines on d20, `review_b121_machines.md`), now taken by items. If the tracking lever (§4) is built, lit weapons will sleep and its statecrc pairs will exercise this branch too.

**Multiplayer.** Items sleep in both builds.
- What a client sends or receives about an item is read from, or written into, the item's own bytes. A network update writes those bytes, which changes the checksum and wakes the item before its next update.
- The lights are drawing state: they're never sent. The desktop servers don't connect lights to the map at all (`object_lights.c`, `HALO_SERVER`).
- So a sleeping lit item is, to the network, the same item as an awake one, and play stays in step with ChupathingyCE's servers (network version 28) [D].
- The multiplayer build compiles and runs (lab 38614681696, build 2, EBOOT `f107d6a937fb0190`: Blood Gulch to game time 4,671, no fault). That run had no items, so it exercises nothing of this.

**Default.** On, in both builds: it can only make a resting lit item cheaper, and it is exact by the same argument as the machines'. Off word `litemsawake`.

**Next.** §4's lever: with it, lit weapons reach this branch.

## 2. The lit items' counts (`4b45c0f`, `c2c041f`, `58e2181`, `9d3ecaa`)

All report only; play reads none of it.
- `psp_platform.c` `object_report`: a line of its own, `lit items per tick: U updated, O offered to sleep, A asleep; kept awake: held, not at rest, on an object, tracking, breakable surface`.
- `psp_fixups.py` `psp_lit_item_reason`: `psp_item_may_sleep`'s tests in its order, read only.
- `psp_lit_item_seen`: counts each lit update by its reason, and logs each lit item's reason when it changes (`sleep: lit item <tag> #N: <reason> (t, speed, spin, item flags, surface)`), with what a tracking weapon's index names (`sleep: lit item #N tracks <index>: <object>; triggers, light`). At most 400 lines a run.

## 3. `pillreach` (`767feb7`)

Recon for `plan_biped_collision_me.md`; it builds only with `PSP_DEBUG_TOOLS=1`, so the play builds carry none of it.
- With the keep word, every pill sweep is counted against four speeds.
- A per-biped cache of three margins (0.25, 0.5, 1 wu) is played alongside: rebuilt when a sweep falls outside it, with the leaves and surfaces the rebuild's walk finds.
- The extra walk fills a result on the stack. It also passes through the sphere memo and the walk's choice of code, which decide speed only. Play reads none of it.
- One `pillreach:` log line every 300 ticks.

Results (lab 38616332979) are in the plan.

## 4. What keeps lit ground weapons awake: the tracking test

**Measured** [E] (`58e2181`, lab 38635666867; `9d3ecaa`, lab 38637594296; hands off to a 480 s wall, b30 by the b113 route). Lit items a tick, by the first sleep test they fail, mean and most:

| Map, run | Updated | Held | Not at rest | On an object | **Tracking an object** | Breakable surface | Offered |
|---|---|---:|---|---:|---|---:|---:|
| b30, 38635666867 | 3.66, 17.75 | 0 | 0.05, 0.97 | 0 | **3.61, 17.61** | 0 | 0 |
| b30, 38637594296 | 4.51, 20.21 | 0 | 0.09, 3.18 | 0 | **4.42, 20.17** | 0 | 0 |
| c10, 38635666867 | 8.00, 8.03 | 0 | 0 | 0 | **8.00, 8.03** | 0 | 0 |
| c10, 38637594296 | 8.00, 8.05 | 0 | 0 | 0 | **8.00, 8.05** | 0 | 0 |

- They do come to rest. A dropped weapon is "not at rest" while it falls and bounces (5–75 ticks), then settles on the level (`on structure 1`, speed 0) and from then on fails only the tracking test. The level's placed ones settle within ~5 ticks of their first update.
- Every "tracking" weapon's index is **`00000000`**, and it resolves to the object in slot 0: `characters\marine_armored\marine_armored` on b30 (178 of 178 logged), `characters\marine\marine unarmed` on c10 (8 of 8). Its triggers are idle (state 0), its integrated light off.

**The path** [D].
- `struct _weapon_datum` (`weapons.h`) has `tracked_object_index`. In this engine nothing sets it but to NONE: `weapon_trigger_finish_tracking`, and `weapon_update`'s check that the object still exists. `weapon_new` doesn't initialise it, so a new weapon keeps the 0 its datum starts with [E: the logged value].
- `weapon_update` (`weapons.c`) clears it only if `object_try_and_get_and_verify_type(tracked, _object_mask_all)` fails.
- An index of 0 has identifier 0. `datum_try_and_get` (`data.c`) treats identifier 0 as "any": `if (!header->identifier || (identifier && header->identifier != identifier))`. So index 0 finds whatever live object holds slot 0, here the level's first marine, and the check never clears it.
- `psp_item_may_sleep` excludes a weapon whose `tracked_object_index != NONE`, because `weapon_update` reads that object's existence (`review_b120.md` §3). So every ground weapon whose index was never cleared stays awake: on these maps, all of them.
- The lights have nothing to do with it [D]: `items.c` sets and clears `_object_at_rest_bit` only for falls, bounces and pushes. The census counted only lit items, but unlit weapons go through the same `weapon_new` [D; not counted].
- The trigger code reads the index only in `_trigger_tracking`: a trigger whose definition has `_weapon_trigger_tracks_projectile_bit` enters it after firing, and leaves it when the trigger is up or the index is NONE (`weapons.c`). Nothing here sets the index to the projectile, so with the 0 such a trigger tracks until released. Whether any of the game's weapons has that flag is not checked [U]. On the ground, triggers are idle, so a resting weapon never reads it.

**An exact lever** (not built). The tracking test exists only because `weapon_update` reads whether the tracked object still exists. Fold that one read into the sleep checksum and the test can go:
- In `psp_family_checksum`, for a weapon whose `tracked_object_index != NONE`, mix in whether `object_try_and_get_and_verify_type(tracked, _object_mask_all)` succeeds, as b102 mixes in a machine's groups. The checksum is taken at the weapon's own turn in the tick, the moment `weapon_update` would ask.
- Then drop the tracking exclusion from `psp_item_may_sleep`.
- If slot 0's object goes (or a real target dies), the checksum changes that same tick, the weapon wakes, and its update clears the index exactly as it would have. While the answer stays the same, the update leaves the weapon's bytes alone (the byte-identical rule shows it, as for any sleeper).
- Proof needed: `itemsleepcheck` (0 changed), statecrc pairs on d20, a50, c20 (play hash, seeds, light lists: this would also exercise §1's branch), and the cache models on b30's route and a50. Ground weapons cost 1.5–4.7 ms a tick on the PSP (`hardware_b114.md`) [M], so this may be the lever b120 meant to pull [U].
- Multiplayer: the same argument as §1. The read is local; a network update writes the weapon's bytes and wakes it.

**Not exact: initialising the field.** Setting `tracked_object_index = NONE` in `weapon_new` would end the wildcard lookup at the source. But it changes every new weapon's bytes and what `weapon_update` reads, so the state hash would differ from the first tick. ChupathingyCE's desktop servers run the same `weapon_new`, so the server and the port would hold different weapon state. It is a behaviour change with an upstream question (does the Xbox's `weapon_new` set it? [U]), for Sheepdog01 and, if it holds, a small upstream fix. Not built.

