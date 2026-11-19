# VRAMCODE: the main CPU's hot code run from VRAM

A `[lever]` on the cost map's code-miss line (`cpu_cost_map.md` §35: "the objects' update waits more on code than on data on every stretch... the levers are smaller code, stages that fit the cache, and cheaper fills (VRAMCODE)"). The lab's `levers.tsv` row VRAMCODE holds its status. Labels: **[M]** PSP, **[E]** emulator, **[D]** code or docs, **[U]** inference.

## Why

- **The gate passed** (2026-11-17, halo_bench19, lab bench-hw 44027754398, PSP-1000; `hardware_facts.md`, VRAM; the lab's findings `vram_code_runs_cached_1000`, `vram_code_line_fill_1000`) [M]: VRAM's cached view 0x04000000 runs code in user mode through the I-cache. A line filled from it costs 57.9 cycles against 72.3 from main RAM with the GE idle, 148.8 against 215.0 with the GE reading main RAM (66 saved), but **91.3 against 73.7 with the GE reading VRAM (17.6 lost)**. In a frame the GE reads both, so only the game's own mix can say which wins.
- **The tick's code misses are >90% capacity** over ~59-70 KB of hot code against the 16 KB I-cache (`cpu_cost_map.md` §3, the lab's `whole_frame_memory_wait_ms`); layout is spent. A cheaper fill is the untried side.

## The three sides (one build)

`PSP_VRAMCODE=b30` builds them; keep words choose at launch.

| Side | Keep word | What |
|---|---|---|
| 1 | neither | the game as without the unit |
| 2 | `vramroom` | the room taken from the start of the texture cache, nothing put there: the room's cost to the drawing alone |
| 3 | `vramcode` | the room, the copy in it, and the calls re-aimed |

2 against 1 prices the room; 3 against 2 prices the fills. The room is taken from textures, not the model copies' room, whose KB are worth ~5.6 times a texture KB to the tick (`research/vram_budget.md` §3, as psp_tex.c cites it).

## The shape

- **The room:** VRAM 0xcc000 upward (the motion sensor's 16 KB that blipdepth frees, on by default, and on), sized to the copy rounded to 4 KB; the texture cache starts after it. Fixed for the whole run, so the tool writes the copy's own addresses; refused with blipdepth off and under benchrun (they put other things there). Nothing at boot uses it: the on-screen log is at 0x1b0000 and the boot list at 0x1c0000 [D].
- **The list** (`psp/layout/vramcode_b30.txt`): the PSP sampler's top 400 functions in the tick and the drawing over b30's flight and beach (lab sierra-hw 43802496817, ticks 634-1,897 and 2,774-4,041; the source of the findings rows `whole_frame_by_cost_ms` and `b30_leg1_legendary_tick_and_draw_by_box`), ordered by the cache model's main-CPU code misses a byte on the same windows (lab sierra-batch 44352925716): a fill from VRAM saves only on a miss. Over all functions, 64 KB so ordered hold ~38% of b30's code misses and ~39% of c20's [E counts].
- **`tools/vramcode.py`** (after the link, from `-q`'s relocations, as `tools/mexec.py`): packs the functions while 64 KB holds them, 16-byte aligned; makes the copy as MEXEC's (calls between copies re-aimed, branches out through trampolines, jump tables copied with VRAM addresses); and lists **every call in main RAM to a copied function's entry**, from a function outside the ME's set, to re-aim at the copy. jal keeps the PC's top four bits, which VRAM's 0x04xxxxxx and main RAM's 0x08xxxxxx share.
- **The ME can't see VRAM** (`hardware_facts.md`, VRAM). So the originals stay whole, and no call the ME can make is re-aimed: `psp/layout/vramcode_me.txt` names every function the ME was seen to run (the cache model's ME side, meside on, on b30's flight and beach and c20's and d20's fights, plus MEXEC's rb and rbj), and the tool adds everything those reach by a direct call or a branch, and their `.cold` parts. A function only the ME's set calls is left out of the copy (nothing could enter it). Function pointers still name the originals: correct, slower.
- **`psp/src/psp_vramcode.c`:** at the first map's `psp_tex_close` it copies from the loaded originals, applies the patches, writes the D-cache back and drops the I-cache's lines, then re-aims the calls (each one word, so a thread running one meanwhile takes either target, both right). Each later map checks the copy's sum; a changed copy turns the calls back to main RAM for the rest of the run. Around a sleep: the calls back to the originals before it, the copy checked (and put back) and the calls re-aimed after it [untested on a PSP].

## Costs in the build

- The table is in `.data` (4,096 words, 16 KB of heap) in all three sides of a `PSP_VRAMCODE` build; a default build has none of this.
- The PC sampler bins only main RAM's code: on side 3 the copy's samples land in `pc_outside` (its share of the time), not by name. The build log's `vramcode:` lines map each VRAM address to its function.

## Proof

Exact by construction (the same instructions at other addresses). In the emulator: play and both seeds identical on all three sides, held b30_leg2 and c20_leg2, strict 0. The emulator runs the ME's jobs on the main CPU, so a re-aimed call the ME could make wouldn't fault there; that safety rests on the ME's set and the PSP's run (a crash.txt would name it).

## Results

(The emulator's proof and the PSP's three-way land here.)
