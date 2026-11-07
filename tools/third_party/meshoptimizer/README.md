# meshoptimizer (vendored)

From https://github.com/zeux/meshoptimizer, release v1.3 (commit 9e1f07b159d3cb777f1c67ed31fc11fd117986f4), by Arseny Kapoulkine, under the MIT licence (`LICENSE.md`, kept with the files as it requires). Only the files the bake's simplifier needs, unchanged: the header, `simplifier.cpp`, `indexgenerator.cpp` (`meshopt_generatePositionRemap`, which keeps UV seams closed) and `allocator.cpp`.

Used by `tools/sspr.py` (SSPR at bake, `docs/sspr_bake.md`), which compiles them into a small host library at bake time. Nothing here runs on the PSP.
