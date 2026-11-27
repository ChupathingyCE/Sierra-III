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
| `owed` | new | store_pose_make_skipped (reached from PSP_NMS_READ, store_pose.c 1130-1136, only when the object's compute is owed) | the accessors' settles, one aggregate wherever they fall (inside or outside the pass); the site is the caller's return address, already passed |
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

## Ask A, from the same hooks

Ask A (emulator, nmreaders census on new-only, both seeds) needs owed settles by site, and nmreaders counts first reads by class and site, not whether a read found work owed. Under `ktstamp` the `owed` hook also counts its calls by caller site (12 slots, as psp_nmr_site), so A runs on the stamped B build with keep words `nmreaders ktstamp`: one build serves both asks. Open, to check at the fixed-up form before A is asked: whether new-only's eager compute still reaches psp_nmr_computed (psp_fixups.py 23622-23631, 31223-31232 are objects.c's compute) so nmreaders' classes are filled on new-only, and the owed makes (store_pose_make_skipped) are not counted as computes there.
