# MEXEC: the Media Engine's hot code run from its eDRAM

R's slice 10 (`redesign_r.md` §3). The lab's `research/opportunities/me_unnamed.md`, lever 5, holds the measurements and the order; this page is the design. Labels: **[M]** PSP, **[E]** emulator, **[D]** code or docs, **[U]** inference.

## Why

- On R's milestone build the ME's code misses are **29-32% of its job on b30's flight** (14-21 ms a frame at ~75 cycles a miss) [M, the lab's `me_unnamed_test1_*`]. They're spread over every step of the part chain.
- They fall over **~100 KB of code**: the top 10 functions hold 45% of them in 48 KB, the top 40 hold 75% in 104 KB [E, shares; `me_imiss_spread_r`]. No hot/cold split gets that under the ME's 16 KB I-cache.
- **A code line from eDRAM costs the ME 25 cycles, from main RAM 54 alone and 70 under bus load; eDRAM doesn't slow under load** [M, PSP-1000; `mexec_code_fetch_psp1000`, `_loaded`].
- **Ceiling [U on M + E]:** the ~104 KB moved, ~75% of the code misses cost ~25 cycles instead of ~72-76: about half the code-miss time, **~7-10 ms a frame of the ME on the flight.** It also takes those fetches off the shared bus (the lab's `bus_toll.md` §6).

## The shape

**A second copy of the hot functions, made by the ME in its eDRAM, entered only by the ME.** The originals stay where they are and stay correct: the main CPU runs the same functions (the record side of `psp_ge_draw_model`, gemesim, every path with gemeon off), so nothing is moved, only copied.

1. **At build time** (`tools/mexec.py`, after the link): from `halo.elf` (linked with `-q`, so every relocation is kept) and a list of function names (`psp/layout/mexec_<name>.txt`, from drawhot2's ME-side table), it writes `mexec.tbl`:
   - each function's offset and size in `.text`, and its place in the copy (packed, 64-byte aligned, in the order given);
   - **every call between copied functions** (`R_MIPS_26`: `jal` and the tail call's `j`), as the word's offset and the target's place in the copy;
   - **every branch out of a copied function** (`R_MIPS_PC16` whose target is outside it: `-freorder-blocks-and-partition`'s jumps to a function's `.cold` part). Each gets a trampoline in the copy within ±128 KB (`j target; nop`), and the branch is re-aimed at it. The branch's delay slot runs as before, and a branch-likely only reaches the trampoline when taken, so the semantics hold;
   - the table's own check: the build's text hash, so a table never meets another build's code.
   - **every jump table** (a run of `.rodata` words naming the inside of one copied function: a `switch`): copied after the code with the copy's eDRAM addresses, and the `lui`/`%lo` pair that loads its address re-aimed at the copy. The first build found 106 such words (`defer_replay`'s dispatch among them); left in main RAM, every `switch` would leave the copy for good.
   Nothing else is rewritten. Function pointers still name the originals, so an indirect call goes on in main RAM: correct, only slower. A table whose address pair the tool can't find stays in main RAM and is counted.
2. **At start** (`psp_me.c`, the ME's `meLibOnProcess`, before its loop, and again after every restart, since eDRAM doesn't survive a sleep): with keep word `mexec` and a table that matches, the ME copies each function's loaded bytes from main RAM into eDRAM. The loader has already relocated the originals, so a call from a copy to a function not copied is right as it stands: `jal` keeps the PC's top four bits, and the ME runs its code through the cached alias (`0x80000000 | address`, as mcidclan's handler and the port's fault vector enter it), so `0x08xxxxxx` becomes `0x88xxxxxx` on the ME. Then it applies the table's patches, writes the copy's D-cache lines back and drops its I-cache lines.
3. **The entry** (`ME_JOB_GE` in `psp_me.c`): on the ME, with the copy in place, `psp_ge_me_replay` is called through its copy's address. Everything it reaches through `jal` among the copied functions then stays in eDRAM.

**Where in eDRAM:** up to 192 KB at 256 KB (`0x40000-0x70000`; 128 KB until list r2 added d20's light pass), the start of the room `psp_me.h` calls resident and `hardware_facts.md`'s default map calls free (256-800 KB). The sound tier starts at 800 KB, AI's data at 1,456 KB, and the stack is at the top. There's no claim registry for that room: its users refuse each other by keep word (`psp_vme.c`'s `vmetest` refuses with `soundedram` or `edramcheck`). MEXEC does the same: it's refused with `soundedram` (R2's sound cache from 256 KB, off by default), `edramcheck` and `mebusedram` (recon), and `vmetest` (its probe slot), and says so in the log.

## Proof and price

- **Exact by construction:** the same instructions, other addresses. The proof is play and both seeds identical with `mexec` on and off **on the PSP**: statecrc and the frame hashes across one pair. The emulator doesn't map the ME's low eDRAM for code (its ME jobs run simulated on the main CPU), so it can't run the copy.
- **Checks in the build's log:** the table matched, the bytes copied, the patches applied, and a checksum of the copy read back on the ME against the table's.
- **Price:** one PSP pair at R's milestone, `- | mexec`, b30's flight, with `meprofile meparts`: the ME's I-misses' cost (memory-wait cycles) and its job time against off. The miss *count* stays about the same; each miss is cheaper.

## The first price (2026-11-10; the lab's findings `mexec_r1_b30_flight`) [M]

- **List r1 (41 functions, 108 KB) on the PSP-1000, b30's flight, `- | mexec` x2 (sierra-hw 42922573141):** the ME's GE job −3.8 / −5.6 / −4.0 ms a frame over three windows (−9%), its model part draws −2.8 to −4.1. Play and both seeds identical on all 1,800 ticks of all four runs; the copy installed on both mexec runs.
- **About half the ceiling [U]:** the list holds ~75% of the code misses; the instruments, audio and the other jobs run from main RAM. The frame is flat there, since the ME doesn't bind that instrumented flight.
- **List r2** adds d20's light pass (55 functions, 128.7 KB; the room widened to 192 KB). **On d20's flashlight, PSP-1000, `- | mexec` x2 (sierra-hw 42929075271; the lab's `mexec_r2_d20_flashlight`):** the ME's GE job -0.8 to -1.7 ms a frame over four windows (-2 to -5%), its light draws only -0.6 / -0.1 (-1.5%); play and both seeds identical on all 2,700 ticks of the four runs. The light pass isn't bound by code misses (11.6 k I-misses a frame at its heaviest, the lab's `light_t2_parts_d20_flashlight`), so its own share from the copy is small; its cost is the split's work.

## Then

- **The hot/cold split and TGME's layout** on what's left (me_unnamed.md lever 2 + 4): the misses in code not copied, and the copy's own conflicts in the I-cache, its layout chosen for the ME alone.
- **The ME's hot data in eDRAM** (lever 5b): data misses are a fifth to a quarter of the ME's, and eDRAM answers data in 27 cycles against 77 under load [M].
- **Main RAM's layout freed:** code that only the ME runs hot no longer needs a place that suits the main CPU too.

## Risks

- **A function whose own code the ME reads as data** (none known), or one that compares its own address: the tool can't see either; the PSP proof would.
- **Jump tables and function pointers** leave the copy for main RAM (counted above).
- **Room:** if the list outgrows 192 KB, it's cut at the line where the misses held stop paying for the room. The free eDRAM runs to 800 KB (the sound tier's start).
