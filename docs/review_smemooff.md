# Review: the sphere memo off by default (SMEMO; keep words `spherememo`, `spherememocheck`, `spherewalk`)

Build 118 added a small table that remembers `collision_bsp_test_sphere`'s answers under all their inputs, compared bit for bit, so a repeated query skips the BSP walk (psp_fixups.py, FIXES_118 `collision_bsp.c#spherememo`). It was on by default. This change turns it **off by default**; `spherememo` turns it back on, `spherememocheck` turns it on with its check, and `spherewalk` (now the default anyway) still means off.

**Why.** On b30 the table answers only ~5% of the queries (4.8% in the stretch where play is equal; 3.3-7% by window), and keying and comparing every query costs more than the walks it saves: +1.56% of b30's tick in the cache model [E] (smemo1, lab 39541976607: the memo's `collision_bsp_test_sphere` +393 M cycles over the run against the walks saved). On the PSP-1000 (lab 39735848497, b30 route, `- | spherewalk` x2) memo off cut the cinematic's tick by 0.8 ms (-1.7%) [M], the emulator's price; the flight was flat, and the later stretches can't be read because that pair's play parted.

**Exact either way.** The memo returns the walk's own answer on a hit (its check, `spherememocheck`, has never counted a difference), so the game plays the same with it on or off; this build's proof is play and both random seeds identical with the defaults against `spherememo` (below).

**What changed.** psp_fixups.py: the switch reads `spherememo` / `spherememocheck` (on) and `spherewalk` (off) instead of `spherewalk` alone, and the comment says why it's off. psp_platform.c: the start-up line that lists build 118's keep words names `spherememo` as a test word.

## Proof [E]

(filled in by the merge request's proof batch)
