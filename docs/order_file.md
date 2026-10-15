# The order file: the game's code laid out by its calls (phase B, T2)

Phase B of the order of work (`living/STATE.md` section 2): fetch less code. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

## Why

The main CPU's tick spends more on instruction-cache misses than on anything else but the instructions themselves: on b30's b113 route, instructions 37%, instruction-miss stalls 39%, data-miss stalls 24% [E, cache model, b131, 71 cycles a miss]. The hot tick code is far larger than the 16 KB instruction cache, so which functions share cache sets decides how often they evict each other.

That placement is luck today. PGO puts the functions it finds hot together (`.text.hot.*`), in the order the objects happen to be linked. b131's profile placed them worse than b130's: the same code misses 42% more instructions on c10 and 48% more on a30, and the tick is 13–14% slower [E, lab `reg131`]; the PSP-1000's ladder measured +17% and +14% [M]. Built without any profile, b131 is only 2.6% slower than b130, and b130's profile was worth ~18% of c10's tick.

What a miss costs also depends on placement: on the PSP-1000 a miss within 128 bytes of the last one costs 55 cycles, one 1 KB or more away 71 [M, `halo_bench11`]. Code that runs in sequence and sits in sequence misses less, and more cheaply.

## How

1. **Count the calls.** The emulator records every call the game's text takes (`PSP_CALL_EDGES`, lab `VARIANTS.md` item 14), on runs of the build's own profile: the batch field `calledges`, with `cache = 1`. `calledges.txt` gives them by caller and callee.
2. **Order the functions.** The lab's `tools/codeorder.py` does Pettis and Hansen's procedure ordering ("Profile guided code positioning", PLDI 1990): the pair of functions that call each other most go side by side first, chains joined so each pair stays as close as it can, the most called chains first, every other function after in its old order.
3. **Score the order before building it.** The order as a `PSP_ICACHE_MAP` layout (the batch field `layout`): a second instruction cache sees every fetch of the same run at the layout's addresses, so `cache.txt`'s `ICACHE:` line gives the misses as built and as laid out. Scored on maps the calls didn't come from too (c10 and a30 for b131).
4. **Build it.** `codeorder.py --link <the build's halo.map> --placements psp/layout/<name>.txt` writes the order as linker placements, one `*<object>(<section>)` a function, each followed by its cold part (the two branch to each other with 16-bit branches, which reach 128 KB). `PSP_TEXT_LAYOUT=<name>` links the build in that order (`psp_build.py`; a bare name is a file of `psp/layout/`). Names, not addresses: a placement file outlives small code changes, and functions it doesn't name go after, as before.
5. **Check it.** The build as linked, in the cache model, against the same build without the order; then one PSP-1000 A/B.

## Exact

The same code at new addresses [D]: nothing computes differently. Each build's release check still compares play and seeds with the build before.

## Where it stands (2026-10-15)

- Tooling built (lab `ffa00c73`, `a0e0c976`; this branch's `psp_build.py`); a local build linked 4,412 placements.
- `edges131` is counting b131's calls on b30, c20, a50 and d20.
