# HOTG: the program's hot variables packed by heat

`PSP_DATA_LAYOUT=hotg` (psp_build.py) places the inputs listed in `psp/layout/data_hotg.txt`, hottest per byte first, in one 64-byte-aligned run right after the ME's `.data` block. `.bss` and small files' COMMON blocks are placed in the `.data` output too (zeros in the file), so the whole hot set is one run of lines. The ME's own objects are never matched, so their blocks and pspgu's `.sbss` line stay where they are. Code and `.rodata` don't move: in the link maps every `.text` and `.rodata` input has the same address and size with the layout on or off [D].

## Why

The tick's program-data misses are mostly one or two warm words to a 64-byte line, sitting among cold neighbours: small flags, counters, keep-word caches and pointers, in each file's input order. Each such line has to be held in the 16 KB data cache, against the object stream, for the sake of a few bytes [E].

## How the list was made

1. A spread build (`PSP_DATA_LAYOUT=hotgspread PSP_DATA_SPREAD=1`) put every input that overlapped b30's and c20's 200 hottest program data lines (457 inputs: up to 2 KB each, small COMMON blocks up to 1 KB) on lines of its own. Then the cache map's per-line counts (`tools/cachemap.py`, "program data lines") give each input's own misses on b30's route, c20 and c10 [E].
   - Spreading them made the tick's data misses go up: +10.0% on b30, +19.4% on c20, +15.4% on c10. Density is what costs [E].
2. 189 inputs, 3,086 B, hold 96% of those misses. They are the ones with at least 1,000 misses a byte, at least 20,000 in all, and at most 1 KB each. 193 of the 457 held none [E].
3. The packed run is 3,200 B (50 lines). It lands on cache sets 0x6D-0x7F and 0x00-0x1F, clear of the main thread's hot stack top (sets 0x30-0x6B) [D].

## What is left out

- The merged float pool (`.rodata.cst4`, 2,584 B) is 3.6% of b30's tick data misses, spread over 31 of its 41 lines (80% of them in 15 lines). The pool is merged across units, so a link script can't reorder it. That is CSTPOOL's question [E].
- Large hot arrays and structs: `structure_globals` (2 KB), `psp_surfaces_seen`, `psp_marker_memo`, `psp_perc_units`, and ai_profile.o's and collision_usage.o's COMMON (5.9 KB, 11.9 KB). Their hot words would need initializer fixups (each one becomes `.bss.<name>`) or a split of their own [E].

## Proof [E]

- Play and both seeds are the same every tick on b30, c20, d20, a30 and c10 (statecrc). The whole state differs only where it follows addresses.
- Strict mode finds 0 problems on all five maps.
- The ME write trace (b30): no line in the packed run is written by both CPUs.
- In the cache model, the tick's data misses fall 7.2% on b30, 11.5% on c20 and 13.1% on c10; over all phases they fall 4.1%, 6.5% and 5.8%.

The price comes from a PSP pair.

## On by default (2026-11-01)

The tock builds with `hotg` unless `PSP_DATA_LAYOUT=none`. On the PSP-3000 against the same commit built without it (w142 4c909478, b30, two runs a side, lab sierra-hw 41308669112 off and 41308798525 on) [M]: the tick -1.5% cinematic (noise), -1.8% flight, -1.7% landing, -1.9% beach (real); the frame -0.9% flight and -1.4% beach (real), the landing's drawing +3.4% (real) with its frame unchanged. The file still fits the code: 185 of its 189 placements matched, a 3,392-byte block (sierra-build 41308152700).
