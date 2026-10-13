# Review: build 120, every change since build 119

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). One section per change, each in the same shape: **what** it does, the **key lines**, **why**, how it's **proven**, and its **default**. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

Branch `w120`. Start from b119 (`bb8b02d`).

| # | Change | Kind | Default |
|---|---|---|---|
| 1 | Engine pin: ChupathingyCE `v0.7.4e` | upstream | on |
| 2 | `hotgroup`: PGO's hot code laid out as one block | exact speed (link layout) | *decided after the cache model* |
| 3 | Resting weapons and equipment sleep | exact speed | *decided after the exactness runs* |
| 4 | `mehalt`: the ME halts when idle | ME groundwork | off (opt-in) |
| 5 | `mefinishslow`: a spaced wait on ME jobs | ME groundwork | *decided after rig job B* |
| 6 | HUD sharpness: `hudsnap`, `hud177`, HUD sheets at 8 bits | look | words off; bake on |
| 7 | Docs and comments only | none | n/a |

---

## 1. Engine pin: ChupathingyCE v0.7.4e (`a8f5559`)

**What.** The engine moves from `b78e6cf` (0.7.1d) to `f8abf21`, the `v0.7.4e` tag: 33 upstream commits, network version 25 → 28.

**Key lines.** `upstream/pins.env` (`ENGINE_CHUPA_COMMIT`), and in `psp_fixups.py` the `errors.c` pair under `FIXES_105`. Upstream rewrote `write_to_debug_file`, so our replacement now fences off the whole desktop writer (from `debug_file_port_write`'s declaration to the end of its body) and puts `_PSP_DEBUG_FILE` in its place.

**Why.** The ME work will add fixups, and each fixup is another anchor a later rebase can break. Today only 2 broke, so rebasing now is cheapest. Upstream also brings latent-bug fixes, and multiplayer needs network version 28.

**What upstream changed for us.**
- **The debug log:** upstream counts a line written again as it was ("last line repeated N times") and rolls the file at 64 MB. The PSP keeps the counting, so a warning each tick can't flood the log, and skips the roll, since our log starts empty every launch.
- **Cost:** none added per tick (the hot files' diffs were read: one actor loop got one iteration shorter).
- **Behaviour, edge cases only:** a starting weapon added while one is held becomes the one in hand; two reads of uninitialised values in the AI (grenade aim from a vehicle, who speaks a line) now read defined values.

**Proven.** `PSP_FIXUPS_CHECK=1` lists no broken anchors; it builds with 0 stubbed symbols [D]. Heap 5,784 KB, 60 KB less than b119: upstream's code grew.

**Default.** On; it's the engine.

## 2. `hotgroup` (`7fc8a2a`)

**What.** A link layout: every function's cold part first (as `coldfirst`), then every function PGO marked hot (`.text.hot.*`) together, then the rest.

**Key lines.** `psp_build.py`, the `PSP_TEXT_LAYOUT` block: `first.append("*(.text.hot .text.hot.*)")`.

**Why.** The PSP's link script places code in input order, so hot functions sit among cold ones and share 64-byte cache lines with them. With a 16 KB I-cache and ~59 KB of hot tick code, wasted line ends add up. Two thirds of the main CPU's misses are instruction fetches (`research_icache.md`, T1).

**Proven.** Same code at new addresses, so exact by construction. Its worth: the cache model on b120's own profile *(result here)*.

**Default.** *(decided by the cache model)*

## 3. Resting weapons and equipment sleep (`85b5c57`)

**What.** A weapon or piece of equipment lying still on the level skips its update, exactly as idle scenery, parked vehicles and settled corpses already do.

**Key lines.** `psp_fixups.py`:
- `psp_item_may_sleep()`: who may be offered to the sleep rule;
- the branch in `psp_object_update` for weapons and equipment;
- the `itemsleepcheck` block at the start of `psp_sleepy_update`'s asleep branch.

**Why.** Ground weapons cost 1.5–4.7 ms a tick on the PSP and never slept (`hardware_b114.md`) [M].

**How sleeping stays exact.** The shared rule: an object sleeps after two updates in a row that left its bytes unchanged, and wakes the moment anything changes them (a shot, a blast, a pick-up). That's exact only if the update reads nothing from *outside* the item. Reading `item_update` and `weapon_update` found four outside inputs, so those items never sleep:

| Excluded | Why |
|---|---|
| resting on an object | re-aligned to that object every tick |
| resting on a breakable surface | watches it break |
| attached lights | the update re-links them into the level's light lists; sleeping would change their order |
| a weapon tracking an object | checks the object still exists |

**Proven.**
- `itemsleepcheck` (every sleeper updated anyway, any change logged): 0 of 240,000 sleeper updates changed an item, on b30's beach fight [E].
- 15–31 items asleep a tick there [E].
- The whole game state tick for tick against `itemsawake` *(d20, a50, c20: result here)*. b30 can't judge this one: its play parts at tick ~1,326 across any change in frame time, whatever the change. Every subset of sleeping items parted at that same tick.

**Default.** *(on if exact; off word `itemsawake`)*

## 4. `mehalt` (`338ca2f`)

**What.** When it has nothing to do, the ME halts instead of polling, and the main CPU wakes it with an interrupt.

**Key lines.** `psp_me.c`:
- `me_halt_idle()` (the ME): raise `halted`, look at the slots once more, then `meLibHalt()`;
- `meLibOnExternalInterrupt()`: a few lines of assembly counting wakes (the handler saves only 4 registers, so no C);
- `me_wake()` (the main CPU): wakes only a halted ME, and again on each look while a job waits.

**Why.** The polling ME makes ~275,000 uncached bus accesses a second (`code_review_b119_me.md`, 2–3). And ME phase 1 needs a reliable wake.

**The one subtle part.** A wake that lands between the ME's last look and its `halt` is taken before the halt and lost. The main CPU covers that by waking again while it waits, so a lost wake costs one look, never a job.

**Proven [M, rig].**
- It works: halts = wakes sent = wakes taken (216 / 216 / 216), 0 failed jobs. A wake costs 22 µs of main-CPU time (a kernel call).
- No frame gain on b30: the long stretch in the noise; the cinematic stretch's drawing borderline +5% (gap 2.7 ms against a 2.5 ms repeat spread). Lab run 38472249947.

**Default.** Off. Proven groundwork for ME phase 1, not a speed-up.

## 5. `mefinishslow` (`338ca2f`)

**What.** While the main CPU waits on an overlapped ME job, it looks at the done word every ~3 µs instead of every ~0.2 µs, and reads the clock and the stop word on every 16th look.

**Key lines.** `psp_me.c`, `psp_me_finish()`: `FINISH_SPIN_NOPS_SLOW` and `FINISH_SLOW_CLOCK_EVERY`.

**Why.** Each look is an uncached read on the same bus the ME uses to read its job's inputs, and the clock is a system call (review, 1). Matters only with `gemeoverlap` (opt-in), which ME phase 1 builds on.

**Proven.** *(rig job B, 38472264749: result here)*

**Default.** *(decided by job B)*

## 6. HUD sharpness (`9ab0baf`, by the HUD session; `docs/hud_sharpness.md`)

**What.**
- `hudsnap`: each HUD quad's corners land on whole pixels (`hud_pixel()` in `psp_ge.c`).
- `hud177`: the HUD zoom where one HUD texel is one PSP pixel (~18% bigger than 1.5).
- In the bake: HUD sheets stay at 8 bits (`texpack.py`; `--hud-clut4` reverts). The bake's `HUD sheets:` line names any sheet this changed.

**Why.** You see the HUD as soft. Off-pixel corners and a texel covering 0.85 of a pixel make the filter blend every edge.

**Default.** The words are off: a look change is your call, after an A/B (`hudsnap` alone first, then `hudsnap hud177`). The bake change is on (`campaign120`), being a restoration.

## 7. Docs and comments only

- **How AI is used here** (CLAUDE.md) and halo-ce-universal retired as a base.
- `comments-geo`: `psp_geo.c`'s comments rewritten, and `tools/comments_only.py`.
- Comments in this build's own code say why, not when. Proof: `comments_only.py` finds only comments in the C and Python files. For the C comments inside `psp_fixups.py`'s strings, the engine tree was generated with the old and new fixups and compared without comments: no code differs.
