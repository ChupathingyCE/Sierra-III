# The GE's bytes off main RAM (M6 rank 1: L2b and lever #1)

`plan_m6_limits.md` ranks this first: it gates the split (rank 2, the GE layer on the Media Engine, L1/L5), whose overlap run slowed the tick 73% with the GE drawing out of main RAM, and it is a30's GE wall at 30/30 (39 ms a frame mean). Labels: [M] PSP, [E] emulator or cache model, [D] code or data, [U] inference.

## 1. The finding that reorders it: the ME's draws bypass VRAM entirely

`vm_redirect`, the draw gate's hand-off to a VRAM copy, returns at once for the Media Engine's work (`PSP_ME_WORK()`), so with `gemeon` every model and level draw goes to the GE from main RAM, whatever VRAM holds [D]. The logs agree:

- the overlap run on the PSP-3000 (c20, `gedefer gemeon` with and without `geinflight`): `vrammodels: 0 buffers in 0 of 600 KB ... draws from a copy 0 of 0` in every report [M log];
- the frames-in-flight emulator check (F0 and F1 on c20, a30, d20, b30): the same, `0 of 0` [E].

So every split measurement so far (L1's gemeoverlap, L5's F1, the 73% tick rise, `mekinds`' ME/main ratios) ran with the GE reading all its geometry from main RAM: the 1.3-3.0 MB a frame it reads from VRAM in the plain build (memory_census.md §5: a10 1,307, b30 1,893, a30 2,991 KB a frame mean; most 5.8 MB) moved onto the bus the tick misses on, on top of the 0.5-0.75 MB it reads from main RAM anyway [E bytes, U for the move]. That is 3-6x the GE's main-RAM traffic of the plain build, and it lands during the tick under overlap.

The ME can't touch VRAM, but it needn't: the gate only puts the copy's address in the display list; the GE does the reading. The vm table is the main CPU's, written only at the frame's end (after the ME's last run) and written back before each run (`sceKernelDcacheWritebackAll` in `defer_on_me` and `defer_hand_over`), so on the ME it can be read without a race.

**Step 1 (built: keep word `mevram`, off by default):** the ME's side of the gate reads the table and uses a good copy; it releases and moves nothing. Its reads, which the ranking needs, go in a tally only the ME's side writes (one word an entry, the counters on a line of their own), folded into the table at the frame's end once the ME is done. The same bytes, the same pixels: frames must be identical with and without it.

**Test word `mevramdry` (off):** the ME's reads go in the tally and the ranking exactly as with `mevram`, so the frame's end uploads and evicts the same bytes, but the ME's draws keep reading main RAM. Against `mevram` and plain, it tells apart what the upload work does to the emulator's speed (and so to the speed-following state) from what drawing from the copies does to the pictures.

## 2. What the GE reads from main RAM today (the plain build, the main CPU's draws)

Per frame, KB, mean / most, from memcensus1's `gereads` on the hand-test routes [E bytes; they don't depend on timing]:

| Map | VRAM (off the bus) | Main RAM | level copies (most) | geometry cache (most) | display list (most) | heap or program (most; indices inside) |
|---|---:|---:|---:|---:|---:|---:|
| a10 | 1,307 / 2,559 | 750 / 2,211 | 656 | 695 | 514 | 347 |
| a30 | 2,991 / 5,761 | 602 / 1,673 | 489 | 866 | 209 | 328 |
| b30 | 1,893 / 5,788 | 589 / 2,128 | 569 | 1,334 | 161 | 305 |

By kind (a30's reports): the main-RAM rest is model buffers that missed the room (fit curve: VRAM holding the best 400-600 KB of them would take 88-98% of their reads on a30, 76-93% on b30), the level copies the room couldn't take, this frame's data in the list (decals, cut pieces: can't be held), and the skinning plans' index lists in the heap (150-480 KB a frame; GL3: no room).

## 3. The VRAM budget

2,048 KB [D, `psp_tex.c`, `psp_ge.c`]: two frame buffers and the depth buffer (3 x 272 KB, 5650 at stride 512), the motion sensor's target 16 KB, then from 832 KB: the texture cache 616 KB (the glyph sheet inside) and the copies' room 600 KB (models and level streams ranked together by reads a byte: `vrammodels`, `levelvram`). Both shares are 95-100% full on every map in play [M, memory_census.md]. vrammodels784 (textures 432) measured -1.0 to -1.3% on a30, b30 in the noise [M, PSP-1000]: room past 600 KB buys little on the main CPU's path while textures pay for it.

So the next bytes come from using the room on every path (step 1), not from a bigger room.

## 4. Selection, upload and eviction (unchanged by step 1)

- **Selection:** reads a byte over the last frames (halved every 32 frames), the best buffers drawn this frame or the last without a copy; level streams and model buffers in one ranking.
- **Upload:** at the frame's end on the main CPU (`vm_frame_end`), at most 128 KB a frame, by CPU copy through the uncached view.
- **Eviction:** the least read a byte of the copies not drawn these 30 frames, if worth less than the newcomer; its room comes back after two frames' quarantine (a list still drawing may read it). A model copy dies when the geometry cache loads other bytes into its block (generation); a level copy when its stream is rewritten, moved or freed.
- **On the ME (mevram):** a copy is used only while good by those same tests; anything else draws from main RAM, as today. The ME's reads count toward the next frame's ranking as the main CPU's would.

## 5. How to prove and measure it

1. **Exact** [E]: `gedefer gemeon gemesim` with and without `mevram`, `fixedticks fixedseed statecrc framecrc`: every frame hash and the play hash equal (`tools/framecmp.py`, `tools/statecmp.py`), on a30, c20, d20 and with `gemeoverlap` on c20; the strict interpreter once.
   **c20's picture gap is timing** [E]: plain, `mevram` and `mevramdry`, `readswait`, twice each. Each word against itself: the whole state equal at all 3,600 ticks, pictures 2-4 frames apart (game time 492-504, the noise). `mevramdry` uploads and evicts exactly as `mevram` does (the same `vrammodels` reports) and still parts from plain, in 11-13 pictures; it parts from `mevram` in 17-19. Play and both seeds are identical at every tick in all six runs. All three pairs differ in their whole state, but only in looping sounds, `objects` and `lights`, the allocations known to follow the emulator's speed. The pictures follow those allocations group by group. At tick 1,020 plain's `lights` differ from the other two, and the pictures 998-1,034 part the same way. At 1,050 `mevramdry`'s `objects` differ from both, and so do its pictures at 1,038-1,048. At 1,110 `mevram`'s `lights` and `objects` differ from both, and so do its pictures at 1,138-1,160. After the noise window, no picture differs at a frame where the two runs' states agree (`mevramdry` against plain: 237 such frames). So the copies draw the same bytes. Handing the copies over and doing the upload work each shift the emulator's timing on their own (`mevram`'s state parts from plain at tick 578, `mevramdry`'s at 996), and the speed-following allocations, and with them the pictures, follow. Which emulated cost the VRAM address changes is not known [U]. The strict interpreter without `mevram` shows the same shared lines as with it: 61 against 60, 50 at the same addresses, and the rest at the same code sites, moved with the layout or the heap. So none of them is `mevram`'s.
2. **Size** [E bytes]: `gereads` on both sides: how much the ME path reads from main RAM without and with it, against the plain build's.
3. **Speed** [M]: the PSP A/B the split needs, in one build: `keepsets "gedefer gemeon | gedefer gemeon mevram"` and the overlap pair (the c20 stretch of the 73% run), `gebusy mekinds`, repeat 2. What decides: the tick's rise under overlap, and the frame against the plain build.

## 6. After step 1

- If the overlap's tick rise shrinks toward stage 0a's synthetic +4.5-11%, the split (rank 2) is re-measured with `mevram` on, and `mevram` becomes part of every `gemeon` preset.
- The remaining main-RAM reads (0.5-0.75 MB a frame mean) are lever #1's second step on both paths: the level copies the room can't take (b30's contested room), model buffers past the room (fit curve), the skinning index lists (GL3, ~25-35% of b30/a30's main-RAM reads), and decals' per-frame data in the list (only fewer bytes help: no VRAM fix).
