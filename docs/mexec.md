# MEXEC: the Media Engine's hot code run from its eDRAM

R's slice 10 (`redesign_r.md` §3). The lab's `projects/sierra/research/opportunities/me_unnamed.md`, lever 5, holds the measurements and the order; this page is the design. Labels: **[M]** PSP, **[E]** emulator, **[D]** code or docs, **[U]** inference.

## Why

- On R's milestone build the ME's code misses are **29-32% of its job on b30's flight** (14-21 ms a frame at ~75 cycles a miss) [M, the lab's `me_unnamed_test1_*`]. They're spread over every step of the part chain.
- They fall over **~100 KB of code**: the top 10 functions hold 45% of them in 48 KB, the top 40 hold 75% in 104 KB [E, shares; `me_imiss_spread_r`]. No hot/cold split gets that under the ME's 16 KB I-cache.
- **A code line from eDRAM costs the ME 25 cycles, from main RAM 54 alone and 70 under bus load; eDRAM doesn't slow under load** [M, PSP-1000; `mexec_code_fetch_psp1000`, `_loaded`].
- **Ceiling [U on M + E]:** the ~104 KB moved, ~75% of the code misses cost ~25 cycles instead of ~72-76: about half the code-miss time, **~7-10 ms a frame of the ME on the flight.** It also takes those fetches off the shared bus (the lab's `bus_toll.md` §6).

## The shape

**A second copy of the hot functions, made by the ME in its eDRAM, entered only by the ME.** The originals stay where they are and stay correct: the main CPU runs the same functions (the record side of `psp_ge_draw_model`, gemesim, every path with gemeon off), so nothing is moved, only copied.

1. **At build time** (`tools/mexec.py`, after the link): from `halo.elf` (linked with `-q`, so every relocation is kept) and a list of function names (`psp/layout/mexec_<name>.txt`, from drawhot2's ME-side table), it writes `mexec.tbl`:
   - each function's offset and size in `.text`, and its place in the copy (packed, 64-byte aligned, in the order given);
   - **a placed function's cache set** (a list line `name @<set>`): the copy is laid end to end, so which lines share a set of the ME's instruction cache (16 KB, two ways of 128 lines: `hardware_facts.md`) follows from every size before them, and two builds differ in it. A placed function is padded up to its set from the sizes as linked, its set and pad are printed, a pad over the list's limit (`# @pad <bytes>`, 512) stops the build, and a placed function the copy doesn't hold is named on the build's summary line (`mexec: CUT`, `mexec: UNPLACED`). The sets themselves are derived data, as the text layouts are: the lab derives them from the cache model's ME graph and line counts, and derives them again after a recount, since a function's own hot lines move inside it;
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
- **List rbtswmax** (`psp/layout/mexec_rbtswmax.txt`, a ceiling test, not a default): the room raised to all of eDRAM 256-800 KB (`# @room 544`) and filled hottest first: rbtsw384's names in order, then each ME job's whole reach by jal (the render job's first). Its `# @fill` line has `tools/mexec.py` cut the list at the longest head that fits the room and the table (said in a line) instead of refusing it; its `# @table 8192` line sizes the table for it (rbtsw384 used 2,828 of 4,096 words for 263 KB; at 6,144 the table, not the room, cut the fill at 398 KB; every word is heap). It answers how much the copy's room is worth, against rbtsw at 256 KB, on the PSP; the emulator runs the ME's originals, so it proves only that play and both seeds don't move.

## On the rebase (builder, 2026-11-11; for the recount's priced pair)

- **The machinery came across alone** (Sierra-III `builder/meplace`, off master/rebase090 1fbd2bd): its seven commits cherry-picked; the one conflict, psp_build.py, kept MEXEC's lines and left out R's `PSP_RETAINED` (R's milestone isn't on the rebase, and MEXEC doesn't need it: the GE replay job it enters is the tock's).
- **List rb** (`psp/layout/mexec_rb.txt`): r2 couldn't stand, as 17 of its 55 functions aren't in the rebased program (R's own, and clones under other names). Re-derived from the lab's drawhotrb (sierra-batch 43126079655: the ME side of the cache map on b30's flight and d20's route): the entry, then the GE replay's family by ME code misses over both routes while 192 KB holds them [E counts]. **95 functions, 158.4 KB of code; 332 calls re-aimed, 3 jump tables** (sierra-build 43130385257). The ME's other jobs on this build (the AI's simulation, the sound mixer, visibility) are left out: the replay never enters them.
- **The table sized to the lists** (1,536 words, 6 KB; rb fills 1,040): at 4,096 words it held 16 KB of .data the heap gave up. **MEXEC on costs the heap 8 KB** (5,632 KB against 5,640 without PSP_MEXEC) [D].
- **eDRAM** [D, from the build's figures and psp_me.h's map]: the copy at 256 KB to ~415 KB, so ~385 KB stays free up to the sound tier's start at 800 KB (SNDEDRAM2, on by default under `sndedram`, 656 KB); the AI job's stack at 1,456-2,048 KB. MEXEC refuses only beside `soundedram` (the old tier at 256 KB), `edramcheck`, `mebusedram` and `vmetest`. To confirm on a PSP: the install's own log line (`ME: mexec ...`).
- **Its exactness is a PSP's to prove**, as r1's and r2's were: the emulator doesn't map the ME's low eDRAM for code.

**The profile's counts** (2026-11-11): a `PSP_PGO=generate` build with `PSP_MEXEC` compiles psp_me.c as MEXEC builds do, so the counts cover its `#if PSP_MEXEC` functions (strict builds refused four without them: meLibOnProcess, psp_me_report, psp_me_start, run_job), and leaves the table empty: the counting build's code outgrows the list's room (311,480 bytes over 196,608, the lab's sierra-batch 43141807668), and the table's contents don't touch the counts. A counting run never copies: the empty table refuses it.

## List rbj: the think and the mixer (builder's worker, for the lever MEXEC2)

- **Why they were left out:** rb's header: the replay never enters the ME's other jobs (the AI's think, the mixer, visibility), and the copy was entered only at the replay's entry, so a copied think would never have run. rb fills 144 KB of its 192 KB on the tock (sierra-build 44435038180: 94 functions, 301 calls re-aimed).
- **What's different:** the list's `# @jobs` line builds psp_me.c with `PSP_MEXEC_JOBS`: the install finds run_job's copy in the table, and me_serve enters every slot job through it, so the think (`psp_aime_me_job` and below), the mixer (`psp_dsound_mix_split`) and the replay all start in eDRAM, and their calls among copied functions stay there. Its `# @room 256` line widens the copy to eDRAM 256-512 KB (tools/mexec.py and psp_me.c read the same number); 512-800 KB stays free, the sound tier starts at 800 KB. The table grows to 4,096 words (16 KB of `.data`, ~10 KB of heap more than rb's). A list without those lines builds as before, so `PSP_MEXEC=rb` is unchanged.
- **How it was picked [E counts, D reach]:** the ME's main-RAM fetches by function in the cache model (the lab's tgme2g, sierra-batch 44352925716: b30_leg2's flight and beach, c20_leg2, d20_play1, held, the ME's audio and mexec on), kept to what the think or the mixer reaches by direct calls in the linked code (yields and fault paths cut), ranked by misses a byte. There the AI job is 6.6 / 3.8 / 7.6 / 2.2 M I-misses a window, capacity-bound (fully associative nearly the same), so cheaper misses, not fewer, are what the copy gives; the mixer's own are 0.07-0.12 M (the PSP's audio job fell 10% with TGME's layout alone, so the PSP may differ).
- **Order:** rb's names first, in rb's order, so the replay's copy keeps its places and its sets; then run_job, the mixer, the think. The think and the replay don't run at once (yields aside), so their sets may overlap.
- **Built** [D] (sierra-build 44471035344): 221 functions (3 names left out: one not in the program, two shared by several functions), 246,268 bytes of code, 12 trampolines, 246,756 bytes in all of 262,144; 973 calls re-aimed, 16 branches through trampolines, 8 jump tables copied (97 of 146 words); the table 2,826 words of 4,096; heap 5,836 KB against rb's 5,844. The rb build at the same commit (44471069381) is byte-identical to the tock d91cb8aa's own (44472063744): EBOOT adf61f8ab8bc6c11.
- **Proof [E]:** the emulator runs the originals, so it proves the build plays the same: play and both seeds identical every tick, rb against rbj, held b30_leg2 seed1 to 130 s with mexec on (3,900 of 3,900 ticks), strict 0 on both (the lab's sierra-batch 44472106548, mexecjid). The copy's own exactness and its price are a PSP pair's (`PSP_MEXEC=rb | rbj`, held b30, statecrc on).

## rbt switched into the tock's recipe (the lead, 2026-11-19 15:0xZ)

- **The split** (Sierra-III builder/mexecjobs fcd54ea4): rbj cut in two, rb's 95 names first in both. `PSP_MEXEC=rbm` is rb + the mixer, `PSP_MEXEC=rbt` is rb + the think (121 of its functions). Each was priced against rb on the PSP, held, no statecrc, because rbj had made b30's flight faster but cost the beach and the ME's other jobs.
- **rbt, held b30_leg2, PSP-3000** (sierra-hw 44542428646; the lab's `mexecsplit_rbt_b30`) [M]: faster on every stretch. The tick is -0.3 / -0.4 / -1.8 ms on the cinematic, flight and beach (beach -11.4%), and the beach's frame -5.7 ms (-10.8%), all real. The ME's audio job is -9% and the replay -4%.
- **rbt, held c20_leg2, PSP-1000** (sierra-hw 44547529112; the lab's `mexecsplit_rbt_c20`) [M]: no real loss. The first stretch's tick is -0.5 ms (-1.8%), real; the play stretch is in the noise (rb's own two runs spread 19.8-21.7), and the ME's audio and replay jobs are flat.
- **So the tock's recipe's `PSP_MEXEC` moves from rb to rbt** (the lead pre-decided on c20's read at 15:0xZ). rbm, the mixer's part, is closed: it didn't pay beside the replay, and it may return once its hot set is cut to fit beside the replay's. The builds behind the read were counted with campaign-rc2, as built; the next recount covers rbt's psp_me.c.
