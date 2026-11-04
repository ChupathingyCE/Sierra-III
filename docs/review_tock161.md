# Review: tock161

The tock after tock160 (`0ebfd3b3`), cut as the next release candidate: it takes fixes only from here. Three merges, one lever turned on by default, the defaults' room raised, a fix to the recon builds, then the tock's own profile and layouts. Every change keeps a word that turns it off.

| Change | Default | Off word | Its own review |
|---|---|---|---|
| P2NC (tick pass P2): a non-collideable object's partition moves made after the object walk | **on** | `p2passoff` | `docs/review_p2pass.md` |
| recon/pcother: the AI job's drains timed by job; `aimevisleft`; `pcsampleother` | off (instruments) | (their words: `aimevisleft`, `pcsampleother`) | below |
| The defaults' room 384 -> 640 bytes, and a default that finds no room is logged | always | none (it's a size) | below |
| Recon builds compile again with drawpart (`FIXES_RECONLATE`) and with step labels | recon builds only | none (a normal build is unchanged) | below |

## The merges

- `recon/p2-pass` (`34ec8875`), merge `66fcb26a`: P2NC with its flip on by default (PSP-3000 pair, c20 frame -2.0 to -2.2%).
- `recon/pcother` (`fc803274`), merge `903b171e`: carries `recon/drainlook` (`0440fda7`). Instruments only: `psp_me.c` times the AI job's ring drains by job type (the margin check's `drains` line), `aimevisleft` leaves a visibility job to the ME's loop (rejected as built, off), and `pcsampleother` bins the other phases' PC samples (`psp/profile/main.c`).
- `docs/cost-map` (`dc0797db`), merge `7e9a4628`: docs only (`cpu_cost_map.md`, `redesign_recon.md`).
- `recon/p1-armpass` `c1e1ccf`, cherry-picked as `e30e3e9e`: one `docs/shelf.md` row (ARMPASS, tick pass P1, shelved: not exact; it stays on its branch).

## The defaults' room (`psp/src/psp_platform.c`, `68a2f134`)

```
#define KEEP_DEFAULTS_ROOM 640
	if (used + length + 2 > sizeof(keep_words))
	{
		... strcat(keep_defaults_dropped, word);
		return;
	}
```

With p2pass the defaults need 358 bytes; the release build's `nooverlay` adds ~10. At 384 the next flip would have been lost without a word, and a lever silently off reads as on. Now there is room, and a default that still finds none is named in the log ("NO ROOM for: ..."), once, after the defaults pass.

## The recon builds (`psp_fixups.py`, `43306f1f` and `686142b7`)

A `PSP_RECON=1` build stopped at the fixups once drawpart was in. `FIXES_RECON` is applied before `FIXES_DRAWPART`, and three of its pairs changed text DRAWPART anchors on: modelcensus's reset after the walk's `render_model`, and the view's object loop, where objcost times `render_object` (and objsteps sits inside objcost's text). Those three pairs move to a new group, `FIXES_RECONLATE`, applied right after DRAWPART in recon builds only:

```
list(FIXES_DRAWPART.items()) + (list(FIXES_RECONLATE.items()) if os.environ.get("PSP_RECON", "") == "1" else []) + list(FIXES_P2PASS.items())
```

Re-anchoring alone would have compiled but measured the wrong thing, since with drawpart a model is drawn after the walk (or in a later object's walk, when the 64-record buffer fills). So each instrument now follows the draw:

- **modelcensus:** `psp_drawpart_draw` names the drawn object (`psp_census_definition`, which psp_ge.c's draw census reads) around each record's `render_model`, and clears it after.
- **objcost:** each record remembers the view object whose walk made it (`objcost_root`, a field recon builds alone have); its drawing time is charged to that object with no draw counted, and a walk that draws a full buffer takes that time out of its own (`psp_drawpart_drawn_us`). So an object's drawing cost is its walk plus its own draws, as it is with drawpart off.
- **objsteps** (`PSP_TICK_STEPS` too): the recording object is pushed around its draws.

A second break showed once the first was fixed: with `PSP_TICK_STEPS=1` the step labeller made `PSP_AIME_LOOK_AT` and `PSP_AIME_MARK` (`do { } while (0)` macros in actors.c) into comma expressions. They join the statement macros it already skips (`step_excluded`).

**A normal build is unchanged:** the engine tree after the fixups is byte-identical with and without both changes (`PSP_RECON` unset), so the profile and the layouts still match [D].

## The profile and the layouts

- **`pgo/campaign-w161a.tgz`**: the tock's own profile, 13 counting runs at `68a2f134`, P2NC and its flip in. The strict build found no function without counts, at the profile's commit and at the layouts'.
- **`psp/layout/w161a_tgc90.txt`**: codeorder's placement by cache set from the tock's own temporal graphs (b30's route, c10, c20; the ME's graph at weight 0): 234 hot functions, 90.1% of the fetches. Its alignments come from a strict build of the same EBOOT.
- **`psp/layout/data_hotg161a.txt`**: HOTG's hot variables from the spread build's misses: 208 placements, holding 98.0% of the candidates' misses; 4 new and 6 gone against hotg160a (in: `build_sprite_globals`, `psp_pose_ahead_listed`, `psp_pose_ahead_me`, `psp_machine_sleep_on`; out: `psp_machine_seen`, `psp_pose_ahead_made`, `in_order`, `psp_checksum_kind`, `psp_item_sleep_check`, `psp_vp_mode`).
- **The recipe:** `PSP_LTO=1 PSP_PGO=use PSP_PGO_PROFILE=campaign-w161a PSP_PGO_STRICT=1 PSP_PGO_EXTRA=-fno-unroll-loops,-fno-peel-loops,-fno-tracer,-fno-ipa-cp-clone PSP_TEXT_LAYOUT=w161a_tgc90 PSP_DATA_LAYOUT=hotg161a`. The bake is **w157m**, as tock160's.

## Proofs

| What | Lab run | Result |
|---|---|---|
| Exactness: tock160 vs tock161 with `p2passoff` vs tock161's defaults | t161x1 41896491380, t161x2 41896505534 | play and both seeds identical at every tick on b30's route (4,500), c20, d20, c10 (3,600); the defaults 358 of 640 bytes, p2pass added; the front door clean [E] |
| The profile | pgo-gen161a 41896258535, pgo-gen161b 41896273137, pgo-merge 41897311076 | campaign-w161a, 13 runs; strict 0 functions without counts at `47c1c58b` (41897532654) and at the layouts' `3a104119` (41904627308: heap 5,772 KB, EBOOT `0a00f831`) |
| The layouts | ltotg161a 41897503433, ltodata161a 41897522483 | the ltotg build's EBOOT is the strict build's (`ce006442`); 234 hot functions (90.1%); 208 data placements (98.0%) [E] |
| The recon builds | 41900145202 (`PSP_RECON=1`), 41900534966 (with `PSP_TICK_STEPS=1`); recon161 41900901656 | both compile and link; the tick-steps one plays b30's route and c10 to 45 s strict-clean, default and `drawpartoff` alike; modelcensus and drawcensus lines identical on the two sides, as are the player's positions [E] |
| The release check | rc161a 41904641396 | 14/14 played, strict 0 sites, no crash; the `PSP_METRICS=0` build (EBOOT `29620d20`) links and plays the route and the front door; sound loads failed for room in 60 s: d20 117 (rc160a 91), others 0, as rc160a; heap 5,768 KB taken (rc160a 5,775) [E] |

The emulator's heap fills in every map, in rc160a as here (the caches take what is free), so its failed requests are no verdict: they rose on c40 (1 to 1,966) and b30's route (67 to 671), fell on a10 (2,623 to 1,514) and b40, and match on the rest. The PSP pair below had none.

## The PSP pair: P2NC on the merged tock

**PSP-1000 [M]** (sierra-hw 41905207966; c20 hands off to exitat120, `fixedseed nomouth hwprofile gebusy`; `- | p2passoff`, two runs a side, one build in the tock's own recipe). The default against `p2passoff`:

| Stretch | Frame | Tick | Drawing |
|---|---:|---:|---:|
| 150-570 | 47.8 -> 46.8 ms (**-2.1%**, real) | 17.7 -> 17.5 (noise) | 17.1 -> 17.2 (noise) |
| 300-2000 | 46.2 -> 44.7 (**-3.2%**, real) | 18.4 -> 17.8 (-3.3%, real) | 15.3 -> 15.5 (noise) |
| 2000-2700 | 61.4 -> 57.1 (**-7.0%**, real) | 19.2 -> 18.6 (-3.1%, real) | 20.5 -> 20.3 (noise) |
| 2700-end | 61.4 -> 56.9 (**-7.4%**, real) | 19.2 -> 18.6 (-3.2%, real) | 20.4 -> 20.2 (-1.3%, real) |

P2NC pays more on the 1000 in the tock's own layouts than its own pair on the 3000 said (-2.0 to -2.2% there, on tock160's layouts), and the drawing cost that pair saw (+0.2-1.4 ms, the queue's lines) is gone [M]; that the recount and the new layouts placed the queue's code and data well is the likely reason [U]. The frame gains more than the tick and drawing add up to in the later stretches [U: fewer frames held for a second tick]. No crash, no ME stop, no default dropped; heap in use 5,215 KB (5,212 off), free inside 229-236 KB, 0 failed; the margin check's worst stretch 4.8 ms, the word answered at once. p2pass made 20,031 partition moves after their place a run (829 queues at the walk's end, 4 full).

## For the tick

Nothing in tock161 waits on a call: its one new default has its PSP price, and the recon fix changes no normal build. The cleanup list for the tick cut (releases.tsv, the build notes, the `PSP_METRICS=0` hand build, `aicad3`'s standing) is the tock160 handoff's.
