# Kill test 2's stage stamps (`ktstamp`): the design

_The builder, 2026-11-27 03:09, on the lab's ruling decisions.md 2026-11-27 03:0x (the lead): ask B goes by log-only stamps, an instrument commit under its own keep word, placed-in checked; the recon cut is not mended. Branch `builder/ktstamps` from `builder/killtest2` 818beed5 (the kill test's old-side head; new-only's code there is dbc76ba0's). The lab's findings/killtest2_2026-11-27.md section 3 is the question this answers._

## What B must read

New-only's biped pass a tick by stage, and old-only's biped work a tick, on the PSP-1000 over b30_leg2's fight (ticks 1098-1712), as shares of the uninstrumented 35.7 / 33.7 ms (killtest2-b30 [M]). The pass at dbc76ba0 is `store_biped_pass` (psp/src/store_coll.c 384-426 under PSP_TICK2_MOTIONNEW): the unit parts, I (store_intent_run a living biped, 395-401), G (store_coll_gather_intent, 404-413), M (store_biped_update, 414-422), the settle (store_biped_settle, 423), the ends (424-425).

## Fields

| Field | Side | Site | What it holds |
|---|---|---|---|
| `pass` | new | store_biped_pass, whole | the four stages and the ends |
| `I` | new | store_coll.c 393-401 | the intents, with I's ME half handed over at each keep |
| `G` | new | 404-413 | the gathers from the world before any biped moves |
| `M` | new | 414-422 | each staged biped's datum_update and store_motion_update |
| `settle` | new | 423-425 | store_biped_settle and the ends |
| `owed` | both (old-only's is objects.c's psp_nms_settle, the same text) | store_pose_make_skipped (reached from PSP_NMS_READ, store_pose.c 1130-1136, only when the object's compute is owed) | the accessors' settles, one aggregate wherever they fall (inside or outside the pass); the site is the caller's return address, already passed |
| `bupdate` | old | objects.c's biped branch of the object loop (psp_fixups.py 6981-7012's accounts 17-19 place) | type update, damage and export, node matrices: the three as old-only's own split |
| `bphys` | old | biped_update_physics (engine source/units/bipeds.c, declared at 528; its definition's line to be read at the cut) | the physics inside the type update |

A field nested in another is charged to both (inclusive times): `owed` inside `M` stays in `M`, so the stage times are whole and `owed` is read beside them, never subtracted blind. The engine's collision functions inside M and bphys are not stamped per call: a clock read is a system call of ~0.5 µs (psp_platform.c 899-904; psp_drawsplit.c 10), and those calls run in the hundreds a tick. Their share comes from G (new) and from the gathers A's census and bpcount1 counted [E], not from B.

## Clock and cost

- The clock is `sceKernelGetSystemTimeLow()` read directly (psp_microseconds answers 0 under notimers, the campaign's default, and timerson would turn on ~240 other reads a tick: psp_platform.c 1480-1487).
- Sampled ticks only: the field hooks read the clock on the ticks `psp_object_sample()` times (one in 8, psp_platform.c 905-925), and the report scales by 8, as the per-type lines do. Unsampled ticks pay a flag test a hook. On a sampled tick new-only reads ~10 clocks for the stages plus 2 an owed settle; old-only 2 an update for bupdate (22 a tick) and 2 a physics call. At ~0.5 µs a read that is ~0.03-0.05 ms on a sampled tick, ~0.005 ms a tick averaged, against the 1% bar's 0.357 / 0.337 ms [U until the pair: its I-cache and D-cache cost is what the bar reads].
- The bar (decisions.md 03:0x): each side's stamped tick within 1% of 35.7 / 33.7 ms on the same fight, both sides stamped and run as the kill test ran them; over 1%, the fields read as shares only.

## Gating

- Build switch `PSP_KTSTAMP=1` (psp_build.py passes -DPSP_KTSTAMP; with it off no unit sees a hook, and the EBOOTs are the kill test's own: A's at 818beed5 on campaign-olddbc76, B's b739ef687f633bf9). Keep word `ktstamp` turns the counting on at run time; the hooks are a flag test without it.
- The hooks are source edits in psp/src (store_coll.c, store_pose.c) and two psp_fixups.py cuts (objects.c's biped branch, bipeds.c's biped_update_physics), each matched once against the fixed-up text.
- Profiles: campaign-olddbc76 and campaign-mrc6531 were counted without the hooks, so the stamped builds take allowmismatch as flipck and drawcensus did. Placed-in check before the commit (lead 2026-11-26 20:0x): `python3 -I toolbox/placedin.py psp/layout/w_tgme2m1_tgc90.txt` (A) and `w_tgme2m1mp_tgc90.txt` (B) on each file edited, the commit naming which placed functions grow and the layout line their block moves from.

## The report

One line every 300 ticks, `ktstamp: ms a tick (calls a tick): pass x (n); I ...; G ...; M ...; settle ...; owed ... (n); bupdate ... (n); bphys ... (n)`, fields a side doesn't have left out, and a fight-window figure read by toolbox/gtgrep.py over 1098-1712 as the kill test's tick was.

## The code, the review paragraph (the builder, 2026-11-27 03:22)

- **No line added anywhere.** Every hook is an expression appended to a line already there (psp_post.h's `PSP_KT_*`, beside `PSP_MEPROBE_EXPR`): with PSP_KTSTAMP off they are `((void)0)`, a tick's scale passing through, and a mark that is a plain `0ul` the compiler drops, so the off form compiles to the kill test's code. And with it on, no function's first line moves: GCC's profile check reads each function's start line (its line checksum) as well as its CFG, so only the hooked functions lose their counts (allowmismatch), never the functions after them in their units.
- **The clock** (`psp/src/psp_ktstamp.c`): `psp_object_sample` passes its tick's scale through `PSP_KT_TICK`, which sets `psp_kt_on` for the timed tick (keep word on and a sampled tick) and `psp_kt_count` for every tick with the keep word. A field's figure is the mean over the window's timed ticks (`ms a timed tick`), its calls over the same ticks, plus the raw µs sum in brackets so windows can be summed. One `ktstamp:` line a 300-tick window of game time (bpcount's rule, `game_time_get() / 300`); after a jump in game time the window it left is dropped.
- **New-only's stages** (store_coll.c `store_biped_pass`): six edges in `psp_kt_marks[0-5]`, at the units' call, `store_intent_begin`, after `psp_sweepme_close`, after `psp_sweepme_end`, before `store_biped_settle`, and after `store_intent_end`. `psp_kt_pass` makes them into pass, I, G, M and settle. The early return for no bipeds leaves no line.
- **owed** is the base NMSKIP fixup's `psp_nms_settle` (psp_fixups.py's FIXES_134 text, renamed `store_pose_make_skipped` when it is moved): `store_pose.c`'s copy has to equal objects.c's text (step 5's check refused it otherwise), so the hook is in both, on the same line. It wraps the make only (after the aime2a handback), with a local mark because a make can nest. It also stands in old-only's objects.c, so **old-only's owed makes are counted too**. Sites are the caller's return address, 12 slots, the last taking the rest, counted every tick off the ME (`!psp_work_on_me()`; a clock read on the ME would be a system call).
- **Old-only's cut** (psp_fixups.py `ktstamp_cut`, last in the chain; a no-op with PSP_TICK2_MOTIONNEW=1): bupdate's mark after `unsigned long mark = timed ? ...` and its sum after `psp_object_account(timed + 2, ...)`, both tested on `timed == 17` (a biped). bphys wraps the one `biped_update_physics(&physics);` call, after bipedstage3's resume label, so a resumed solve is timed as well. The cut refuses a line-count change.
- **Placed-in** (toolbox/placedin.py on each edited file): **B, w_tgme2m1mp_tgc90**: the hooked functions (`store_biped_pass`, `store_pose_make_skipped`, `psp_object_sample`) are not placed, so B's placed text doesn't move; `psp_ktstamp.c` links last, unplaced. **A, w_tgme2m1_tgc90**: `object_update` (line 97 hot, 98 unlikely) grows with bupdate's two hooks and owed's is in `psp_nms_settle` (unplaced, noinline). `biped_update_moving` (line 160 hot, 161 unlikely) grows with bphys's. So A's placed block moves from layout line 97 on [U until the pair's placediff]. Both have lost their profiles under allowmismatch, which is part of what the 1% bar reads.
- **Checked here**: psp_fixups.py over fresh 2e5eac00 trees, old-only's form with PSP_KTSTAMP=1 (exit 0; each anchor x1; the hooks read back in the fixed-up objects.c 2852, 8253, 8288 and bipeds.c 4786) and slice 1's form with PSP_TICK2_MOTIONNEW=1 and PSP_KTSTAMP=1 (exit 0; step 5's copy check passes with the owed hook). The off forms are the same chain without `ktstamp_cut` (it returns before reading the tree), so their text is what the on runs checked before it. A host `gcc -fsyntax-only -Wall -Wextra` on psp_ktstamp.c is clean. Not checked here: the PSP compile (no psp-gcc on this host); the runner's build is that check.
- **[U] for the read**: on old-only, whether bipedstage3's staged resumes run inside objects.c's biped branch or in its stage run at the walk's end. bupdate's calls a tick answer it (about 22-30 bipeds a tick means the branch holds them), and bphys times the physics wherever it runs.

## Ask A, from the same hooks

Ask A (emulator, nmreaders census on new-only, both seeds) needs owed settles by site, and nmreaders counts first reads by class and site, not whether a read found work owed. Under `ktstamp` the `owed` hook also counts its calls by caller site (12 slots, as psp_nmr_site), so A runs on the stamped B build with keep words `nmreaders ktstamp`: one build serves both asks. Open, to check at the fixed-up form before A is asked: whether new-only's eager compute still reaches psp_nmr_computed (psp_fixups.py 23622-23631, 31223-31232 are objects.c's compute) so nmreaders' classes are filled on new-only, and the owed makes (store_pose_make_skipped) are not counted as computes there.
