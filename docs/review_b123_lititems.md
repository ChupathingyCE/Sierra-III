# Review section for build 123: resting lit items sleep

For `docs/review_b123.md`. Same shape as `review_b120.md`: **what**, **key lines**, **why**, **proven**, **default**. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

Branch `lititems123` (from `w122`, `85ebea3`).

| Change | Kind | Default |
|---|---|---|
| Resting weapons and equipment with attached lights may sleep, their lights reconnected as their update would | exact speed; **no lit item found resting yet**, so no gain measured | on (`litemsawake` off) |
| The lit items counted on a line of their own, and each lit definition named | report | always |
| `pillreach`: how far a biped's pill sweep reaches (for `plan_biped_collision_me.md`) | recon, `PSP_DEBUG_TOOLS=1` only | off (opt-in) |

**In short.** The change works and changes nothing else, but on every map measured so far no resting weapon or piece of equipment carries an attached light. b120's exclusion was costing nothing. The census on five more maps (lab `lititems123d`) says whether any map has one; if none does, this is insurance for maps and game types not yet seen, not a speed-up.

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

b30's beach and a50 lost the counts to that cut, so `c2c041f` gives the lit items a line of their own, and names each lit definition the first time one updates (`sleep: lit item ...`, with where it was and what kept it awake). Batch `lititems123d` (lab 38623624370, queued at this writing) runs it on a50, b30's route, a30, b40 and c10.

**Gain** [E]. None found: no lit item slept in any run.
- Cache models, `litemsawake` against on, to game time 40 s: b30's route 47.818 → 47.817 ms est., a50 16.285 → 16.285 ms (labs 38614501309 and 38614681696).
- The two sides' fetches differ by 53 on b30 and 5 on a50 out of millions a tick: the extra flag test, nothing else.

**Proven** [E], on against `litemsawake`, hands off (`nearauto assettrack geo2 nomeaudio fixedticks fixedseed statecrc`), `tools/statecmp.py --ignore="object looping sounds,first person weapons"`, lab 38614501309:
- **Play exact.** The play hash and both random seeds are the same at every common tick: d20 18,468 (across its BSP switch), a50 31,420, c20 23,731.
- **The light lists** (`lights`, `light`, `light cluster reference`, `cluster light reference`) are the same at every checkpoint on all three.
- The whole state differs only in `objects`, the render pass's mark that follows the emulator's speed (`big_levers.md` §5, #25). c20's last checkpoint (23,730) is cut mid-line by the wall and left out, as in the earlier reviews.
- `itemsleepcheck` (every sleeper updated anyway, any change logged; lab 38614501309): 0 changed, by the last count lines, of 60,000 sleeper updates on b30's route and 120,000 on a50.
- **What that proves.** Since no lit item slept, these runs show that the change disturbs nothing else. They don't yet exercise the new branch. The branch is the machines' (`f6d2617`, proven with lit machines on d20, `review_b121_machines.md`), now taken by items. When the census finds a map with a resting lit item, a statecrc pair there is the remaining proof (below).

**Multiplayer.** Items sleep in both builds.
- What a client sends or receives about an item is read from, or written into, the item's own bytes. A network update writes those bytes, which changes the checksum and wakes the item before its next update.
- The lights are drawing state: they're never sent. The desktop servers don't connect lights to the map at all (`object_lights.c`, `HALO_SERVER`).
- So a sleeping lit item is, to the network, the same item as an awake one, and play stays in step with ChupathingyCE's servers (network version 28) [D].
- The multiplayer build compiles and runs (lab 38614681696, build 2, EBOOT `f107d6a937fb0190`: Blood Gulch to game time 4,671, no fault). That run had no items, so it exercises nothing of this.

**Default.** On, in both builds: it can only make a resting lit item cheaper, and it is exact by the same argument as the machines'. Off word `litemsawake`.

**Next.**
- Read `lititems123d`'s `sleep: lit item` lines. If a map has resting lit items, run a statecrc pair there (on against `litemsawake`) and the cache models for the gain.
- If no map has any, nothing is lost: the branch costs one flag test per item update.

## 2. The lit items' counts (`4b45c0f`, `c2c041f`)

`psp_platform.c` `object_report`: a line of its own, `lit items per tick: U updated, O offered to sleep, A asleep`. `psp_fixups.py` `psp_lit_item_seen`: the first update of each lit weapon or equipment definition (up to 16) logs `sleep: lit item <tag> (type T): parent ..., at rest ..., on an object ...`.

## 3. `pillreach` (`767feb7`)

Recon for `plan_biped_collision_me.md`; it builds only with `PSP_DEBUG_TOOLS=1`, so the play builds carry none of it.
- With the keep word, every pill sweep is counted against four speeds.
- A per-biped cache of three margins (0.25, 0.5, 1 wu) is played alongside: rebuilt when a sweep falls outside it, with the leaves and surfaces the rebuild's walk finds.
- The extra walk fills a result on the stack. It also passes through the sphere memo and the walk's choice of code, which decide speed only. Play reads none of it.
- One `pillreach:` log line every 300 ticks.

Results (lab 38616332979) are in the plan.
