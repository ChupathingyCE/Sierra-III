# S, the tick by how often its work changes: the design page (Phase 1 draft)

Part S of `redesign_plan.md` (§3 S). Sheepdog01 gave it its go at Gate 1 (2026-11-05 19:28), with ruling 3: the margin is covered by "plays the same" work on the moving bipeds first, and the compromise freeze lifted only if that's hopeless. Built under the redesign rule (CLAUDE.md, "Redesign work"). The master builds; recon builds the census-driven pieces. A draft for a second-opinion read against the code before slice 1.

Labels: [M] measured on the PSP, [E] emulator or estimate, [D] read in code or data, [U] inference. Code references are tock162.

## 1. The target, and where the tick goes

**Target:** a bare tick of ≤ 25.8 ms on b30's flight, with drawing, other and frame end at ~15 ms a frame; ≤ 22.3 if they end at ~20 (plan §1). Today it's **42.4 ms bare** (b151's ledger1 bench, PSP-3000, 2026-11-05; ledger 3 less the instruments' ~7% had estimated 40.6).

**Where it goes** (b30's flight, PSP-3000 sampler, 42114568213) [M]: collision 25.6%, AI 16.5, node matrices and animation 11.3, units and vehicles 9.3, other objects 7.7, libm and memcpy 5.3, effects 3.4, tags 2.9, audio 2.4, scripts 1.6, the rest 14.5. It waits on memory for 56-68% of its cycles on every stretch, its instruction and data misses close to even in cost (`cpu_cost_map.md` §2).

**What changes tick to tick** (C3) [E for counts, M for times]:
- objects: 69-73% unchanged on b30, 96-97% on b40;
- object updates' time (PSP-1000, 42141971439): 34.9 ms a tick, 6.8 of it (19%) in updates that left the object unchanged; the moving bipeds 22.9 (~970 µs each);
- collision's queries mostly don't repeat (vectors 3-6%, reconnects under 5%); bipeds' feature-in-sphere queries do (25-46% for bipeds; 16-39% overall);
- **b40's snowfield, timed** (PSP-3000, sierra-hw 42152471353, game time 14,100-15,300; row c3_objcost_ms_b40_snowfield) [M]: object updates 30.8 ms a tick, 6.6 (21%) leaving the object unchanged: **vehicles 3.7** (6.4 a tick at ~580 µs: gun turrets and parked vehicles), machines 0.9, weapons 0.7, scenery 0.4, bodies 0.6. Bipeds 20.0 ms (24.9 a tick); **far and out of the fight 28% of it, ~5.5 ms a tick**; in the fight 57%;
- bipeds by distance and fight (objcost2 42146294460, shares of biped time): far and out of the fight 19% on b30's flight (~4.6 ms a tick at the PSP's 24.3), 32% on b40's snowfield, 14% on the cliff, 8% on b30's landing; in the fight at 20-40 units 30-38% everywhere; bodies without an AI 25% on the landing, 86-97% unchanged.

## 2. The pieces

| # | Piece | Exact? | Ceiling on b30's flight, ms a tick | Evidence |
|---|---|---|---|---|
| S1 | Bodies held up by sleeping objects sleep too | exact | ~1-2 (of the 6.8 measured unchanged, the Pelicans' 2.4 are scripted and the scenery's 2.3 animated: what's left is bodies and small work) | objcost 42141971439 [M]; whyawake1-3 [E] |
| S2 | Collision's feature queries: the level half reused while the sphere is unchanged | exact | ~0.75-1.1 | s2census1 [E] |
| S3 | AITICKOLD: the AI's think on the ME from a world one tick older | plays the same (built, Sheepdog01's earlier OK to experiment) | ~5.9 (-14.5%) | PSP-3000 pair 42041663929 [M]; needs ME room R frees |
| S4 | The far-biped tier | plays the same | ~2.3 (half rate) to ~4.6 (on the ME a tick behind) | objcost2 shares × the PSP's 24.3 [U on E × M] |
| S5 | T5 (walks that overlap their misses) and T7 (data laid out for the tick) | exact | ~0.5-1 | `redesign_tick.md` |
| — | The GE's toll, cut by R and B | exact | ~2-4 | lead A [U on E and M] |

**On b40's snowfield** (bare tick 37.6, -31% to 25.8) [U on M]: S4's far tier ~2.75 (half rate) to ~5.5 (the ME); S1's unchanged vehicles are the covenant gun turrets (`vehicles\c gun turret`; recon, whyawake4 42161237964 and whyawake5 42161670797 [E]), and **they give S1 little:** two thirds of their unchanged updates have a gunner aboard (AI's; the vehicle sleep rightly refuses them), and the empty third's aim servo still moves its aim and look block on a third of updates. The exact piece, empty turrets with a still aim block, is under ~0.4 ms of the 3.7 [U, shares × the PSP figure]: an open S1 note, not a slice. Bodies ~0.6. **AITICKOLD:** actors and AI are 13.9% of the snowfield's tick (b40tick, PSP-1000 [M]), ~4-5 ms a tick [U on M]. **S2's level half:** 61% of a features-in-sphere call on b40 (b30 71%; s2census1 42155463319 [E]); bipeds' repeating queries 32% of theirs (qrepeat2 [E]); b30's ~0.75-1.1 scaled by those shares and the snowfield's collision share, ~0.6-0.9 [U]. **Sum on the snowfield** [U]: S4 2.75-5.5 + AITICKOLD 4-5 + S2 0.6-0.9 + bodies 0.6 + turrets ≤0.4 → **~8.4-12.4 against the -11.8 needed**: reached only at the top, with S4 in its ME form. The snowfield is a second home for the margin, beside b30's flight; AITICKOLD's AI pass timed on the snowfield (one emulator batch, recon offered) is what sharpens it.

**Sum** [U], on b151's 42.4: less 12.65 to 18.8 → **~23.6-29.8 ms a tick** against 25.8 (or 22.3 with drawing at ~20). The floor's tick on the flight needs the upper half of the ranges; b30's landing (35.3, its bodies a quarter of the biped time, slept exactly by S1) and b40's snowfield (37.6, its far bipeds a third) need less.

### S1. The unchanged-object skip

An object whose update leaves it bitwise unchanged doesn't need the update. The work is knowing that *before* the update runs, with a predicate that's never wrong:

- **Already done this way:** the sleepers (FIXES_WS: ~217 of b30's ~274 scenery a tick never enter `psp_object_update`; recon, 18:57) and the resting weapons and equipment (IS). S1 extends the same idea to the kinds that still carry the measured 6.8 ms:
  - **not vehicles, after all** (recon's predicate read, whyawake1 42152687593 [E]): the 2.4 ms of "unchanged" vehicle updates on b30's flight are the Pelicans hovering on a script, not at rest (~1.1 a tick); parked Warthogs already sleep or cost ~11 µs. A scripted vehicle's input is its script, so it stays out of S1;
  - **awake scenery** (2.3 ms, 53 a tick at ~42 µs) **isn't unchanged: it's animated** (recon's read, whyawake2 42153640742 [E]): its node matrices change every tick (b30's leafy trees, c10's plants and lily pads); the play hash leaves node matrices out, which is why objcost counted it unchanged. Skipping it isn't exact. What's left for S1 is its update's non-pose work (emitters' timers, a few function words), not yet split; recon times it next. **An exact angle on the pose itself** [U]: at the floor's two ticks a frame only the drawn tick's pose is seen, so a tree's node matrices could be made only on the drawn tick and on any read (LBM's machinery). LBM lost on bipeds because the AI reads their poses every tick (~1.2 ms a tick of reads, `levers.tsv`); trees may be read rarely. **Counted** (whyawake3 42155122122 [E]; row s_slice2_scenery_pose): node matrices are 36% of awake scenery's update on b30 and 48% on c10 (c10's plants and lily pads, ~3.0 ms a tick of scenery updates in the emulator). On **c10 nothing reads them on the undrawn tick** (0 reads in 150 undrawn ticks), so poses made only on the drawn tick and on a read are exact there and halve that pose time (~0.7 ms a tick [U]). On **b30 bipeds and code outside the object loop read them** (~6,900 reads in 150 undrawn ticks), so LBM's lesson holds and b30 gains nothing. A target-side piece for c10, not a floor piece for the heavy stretches;
  - **bodies** (25% of the biped time on the landing, 86-97% unchanged): the corpse sleep (b105-b114) already offers every settled body; what keeps some awake is its own guard, a body held up by something other than the level (b30 7.7 of ~50 a tick, d20 12.3 of 36; 55-440 µs each in the emulator). **Slice 1 is therefore:** a body held up by objects may sleep while everything holding it is asleep or static, and wakes the tick any of them wakes or goes (recon, building under `skipchange`).
- **The wake rules** are the sleepers': contact, damage, a script, a parent that moved, a force. A wrong predicate shows up as a play difference, so each kind's rule is proven by play and both seeds identical on b30, c10, c20 and d20 (statecrc), with a check build that runs the update anyway and compares (as `rsocheck` and `vislazycheck` do).

### S2. Collision's feature-query reuse: the level half

The repeating queries are bipeds' `collision_get_features_in_sphere` calls. **Census** (recon, s2census1 42155463319 [E]; row s_slice3_census):
- **the level's part is most of a call:** its BSP sphere test and features are 71% of the call's time on b30 (64 + 2 of 93 µs), 61% on b40, 76% on c20, 50% on d20; the objects' part is the rest;
- **the level never changes**, so its half of the answer is exact to reuse whenever the sphere and the query's flags are bitwise the same and no breakable surface has changed (glass that breaks changes the level's answer). That holds on **31-47% of biped feature queries**, more than the 19-39% that also need every object unchanged;
- **the objects' half can't be keyed cheaply:** collidable objects change their node matrices without a reconnect (held weapons, trees, b40's turret, some grunts), so a per-cluster stamp would be inexact. It stays live (recomputed every query) in S2's form;
- **memory is small:** a biped's answer is 2-3 features, ~250-300 bytes on average (at most 4.3 KB on b30): a level-half store of ~1 KB a caller covers nearly all, ~30 KB for b30's bipeds (the heap has ~69 KB free there; a scratchpad candidate only in part).

**Worth** [U]: the sphere test and feature gathering are ~8.5% of b30's flight tick (~3.4 ms of 40.6), the level part ~70% of that, reused on ~31-47%: **~0.75-1.1 ms a tick.**

**Proof:** play and both seeds identical; a check build compares every reused level answer with a fresh walk (as slice 1).

### S3. AITICKOLD on R's ME room

Built and measured: b30's flight frame -7.8%, tick -14.5% on the PSP-3000 [M]; off by default because the ME's window is full on the beach (the AI job fills 84% of it, recon 16:15). R frees ~29-31 ms of the ME's frame on the flight (`redesign_r.md` §2.4), which is room for both passes. So S3 lands after R's milestone. Recon's MECODE (the AI pass's code in eDRAM, ~1-1.6 ms a pass) makes it cheaper still (B).

### S4. The far-biped tier (ruling 3)

For bipeds far from the player and out of the fight (AIFAR's test), two forms, chosen by a count and a side-by-side:
- **half rate:** updated every other tick with twice the time step, as the AI's think already is at half rate (aicad3). Saves half their time (~2.3 ms a tick on b30's flight, more on the snowfield). Risk: physics with a doubled step (a fall, a slope); proven by side-by-sides and the outcome census at the stretches where far bipeds move.
- **on the ME a tick behind** (BW, the AITICKOLD pattern): their whole update moved, so the main CPU saves all of it (~4.6 ms a tick on b30's flight). It needs ME room after S3 and a snapshot of what they read (`redesign_b.md` §3.2), so it comes second.

Collision stays the same code against the same world (the rule: collision exact, one path). The tier is local: in the campaign every biped is the PSP's own; in co-op the host runs the far AI.

**What S4 doesn't reach:** bipeds in the fight at 20-40 units are the largest band (30-38% of biped time). A distance tier leaves them alone, since they're what the player is fighting. If S1-S4 land below their ceilings, they're where the margin sits; the plan's ruling 3 says what comes next (§5).

## 3. The slices

| # | Slice | Proven by |
|---|---|---|
| 1 | S1 for bodies held up by sleeping or static objects, with its check build (recon/skipchange: play and both seeds identical every tick on b30, c10, c20, d20; the check build's remaining words, on d20's bodies, are the unit timer's low half, idle_ticks and a dead biped's aim servo and functions: the words the shipped corpse sleep's checksum deliberately leaves out, so it's exact to that sleep's standard, not statecrc-exact; skip10 confirms) | play and both seeds identical on four maps; the check build's compare clean |
| 2 | S1 for awake scenery (its animation advanced alone) | as 1 |
| 3 | S2, the level half of bipeds' feature queries reused (the objects' half live), with its check build (recon/skipchange 7a55d714: 0 differences in 27,000 reused level halves against fresh walks; reuse 28% of b30's biped queries, 22% of c10's; play and seeds identical; c10's full dump identical in every object datum word, the allocation difference outside the datums) | as 1 |
| — | **Milestone:** S1 and S2 on b30, the old update path bypassed for the kinds they take | one PSP pair, `- \| skipchange` (the keep word), b30's flight |
| 4 | S4's half-rate form | side-by-sides and the outcome census on b30's flight, the snowfield and a30 |
| 5 | S3 on (after R's milestone frees the ME) | AITICKOLD's own proofs; the window's cut passes counted |
| 6 | S5's pieces that fit the new shape (T5's prefetch in the reshaped loops; T7) | as 1 |
| 7 | S4's ME form, if the half-rate form falls short | as 4, plus the snapshot's census |
| — | **Switch-over:** S's pieces on by default | one PSP pair on the heavy stretches |

## 4. Proofs and boundaries

- Exact pieces (S1, S2, S5): play and both seeds identical, statecrc, on b30, c10, c20 and d20; check builds that run the old path beside the new and compare.
- "Plays the same" pieces (S3, S4): side-by-sides and the outcome census (the rule of 2026-10-30), with statecrc's identity no longer the proof; both sides of their A/B carry the new maths.
- The multiplayer boundary: what the server consumes or checks stays exact (the rules, the 30 Hz tick, collision's answers, tag indices, object slots). S1 and S2 are exact; S3 and S4 touch only local AI and far local bipeds.

## 5. Risks and open questions

- **b40's cliff** (bare tick 35.9, -28% to 25.8; re-run 42156503649 [M]) has the smallest far share (14% of biped time), so S4 buys least there; its -10.1 ms leans on S2 and AITICKOLD (the turrets give ~0.4 at most). Its per-kind object costs aren't timed yet: the snowfield's objcost run's twin on the cliff would price it.
- **The predicates (S1)** must be read in the engine: what an at-rest vehicle's update does that isn't visible in its play hash (timers, effects, sounds). The check build is the guard.
- **The half-rate tier's physics:** a doubled step on a slope or a fall can land a biped differently; that's what the side-by-sides look for.
- **b40's snowfield needs S4's ME form** (the top of its range) to reach -11.8 with the other pieces at their upper ends (§2).
- **The in-fight 20-40 band** is the margin's home if the pieces fall short: Sheepdog01's ruling 3 is "plays the same" first, then the compromise freeze only if that's hopeless.
- **The drawing budget:** S's target assumes drawing, other and frame end at ~15 ms a frame. Recon's split puts 'other' alone at ~5 ms (b30's flight), so R's drawing has to reach ~10.
