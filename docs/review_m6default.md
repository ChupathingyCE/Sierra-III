# Review: M6 #2 on by default in the campaign (branch `merge-w140-m6default`)

Sheepdog01's decision 2 = A (2026-10-21 09:16): the M6 #2 set goes on by default once TXPT clears b30's untextured frames and heaproom256 is in. Both are now true, so this branch merges `txpt3` (texpretouch) and `mepoll` into w140 and turns the set on. Labels: [M] PSP, [E] emulator, [D] code or data, [U] inference.

## What is on now (campaign build, no keep words)

`gedefer geinflight gemeon mevram texpretouch mepoll inflightkb224`. heaproom256 follows on its own wherever the in-flight buffer is on and `geo2` or `geo15` is set (every preset has geo2), as `psp_heaproom_bytes` already did.

## The change

### `psp/src/psp_platform.c`: one place, not every call site

- `keep_defaults[]` pairs each default word with its off word. `keep_defaults_add()` runs in `keep_load()` right after keep.txt is read, and appends each word as if keep.txt had named it. Every `psp_keep("gemeon")` and its kin then answers as it did with the words typed, so no feature's code changes and a run with the defaults is the same run as one with the words.
- Skipped whole by `m6off`, and by the instruments that measure or split the old pipeline: `mebench`, `gemeoverlap`, `geserial`. With those, the words mean what they did; to combine one with M6 #2, type the words.
- Each word is skipped by its off word: `gedeferoff geinflightoff gemeonoff mevramoff notexpretouch mepolloff`.
- `texpretouch` is added only if `gemeon` is on, because it reads in the textures that the ME's replay binds.
- `inflightkb224` is added only if `geinflight` is on and keep.txt gives no `inflightkbN`. `inflightkb128` restores the old size.
- `keep_words` has `KEEP_DEFAULTS_ROOM` (96 bytes) past the file's 509, so a full keep.txt still gets the defaults.
- The `null path keeps:` line shows the words in effect, defaults included, so `hwreport.py`'s keep list is the truth. A new `keep words, the campaign's defaults` line lists what was added and the off words.
- Compiled out with `PSP_MULTIPLAYER`.

### `psp/src/psp_menu.c`

In the campaign build, the launch menu's `gedefer` and `gemeon` rows become one `m6off` row. The multiplayer build keeps its two rows.

## Multiplayer: unchanged (off)

None of the set has been played against a ChupathingyCE server. heaproom256 is already compiled out of the MP build. The MP build still compiles every feature and takes the words when they're typed.

## The measured basis

- **Play:** sv7 39607467321 [E]: with `nomouth`, plain, the set, and the set + inflightkb224 + heaproom256 play identically on a30 (9,000 ticks) and b30 (6,000 ticks), both seeds, every tick.
- **Speed:** geinflight + mevram on the PSP-1000 c20 (39501638143) [M]: frame 106.2 to 80.5 ms (-24%). Tick costs and the gebus ladder are in lab `coord/handoff/gebus.md`.
- **Look (TXPT):** txpt2 39616931750 [E]: b30 untextured frames, plain 23, the set 175, the set + texpretouch 30. a10: 111 to 14 (plain 16). Play matches at every tick. Cost: about +1.5 ms of drawing a frame on b30's flight (cache model).
- **Heap (heaproom256):** sv2 39575417500, PSP 39577431764: a30's failed allocations 410 to 0, headroom +200 KB, no speed change. docs/review_sv.md.
- **MEPOLL:** meprog1 39610034003 [E] is exact under nomouth. PSP-1000 pair 39614432308 [M], b30 route: frame -5 to -11% in all five stretches, beyond the repeats' spread, with 0 late audio blocks. This was measured only on b30 and on one PSP.

## Risks

- Lab A/Bs that used no words as their "plain" side now get M6 #2 on that side. Plain is now `m6off`.
- The geometry cache is 256 KB smaller with heaproom256, which costs a few % more misses (review_sv.md).
- `listcachedall` is off whenever `gemeon` is on, as before. This now means always, unless `m6off` is given.

## blipdepth on by default (with the exact CLUT4 set's merge)

`keep_defaults_add` (`psp/src/psp_platform.c`) also adds `blipdepth` in the campaign build: the motion sensor's 64 x 64 target drawn in the depth buffer after the 3D pass, its 16 KB of VRAM given to the model copies (600 -> 616 KB; `docs/review_vrbroom.md`). It is added before the M6 #2 gate, so `m6off` and the instruments leave it on; `blipdepthoff` turns it off, `blipdepthcheck` still checks it.

Why default: exact. Play identical every tick on b30 and a10 (lab vrbroom4, vrb2m1 under the M6 #2 defaults); its check found 0 differing frames in the emulator (0 of 2,059 and 2,394) and on the PSP-3000 (0 of 1,198 plain, 0 of 918 under M6 #2; lab sierra-hw 39626413267). Its own speed gain is small (0.07-0.29 ms a tick [U]); the 16 KB is the point.
