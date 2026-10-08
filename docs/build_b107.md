# Build 107: campaign-only, b30's crash fixed, room for draws and sounds (2026-10-08)

Built after b106's two hardware runs (hardware_b106.md). Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator (PPSSPP with the local patches, PSP-1000 memory), **[D]** read in the code, **[U]** inference. The item numbers point into b107_candidates.md, which has the detail.

## What changed

**Crashes and bugs fixed**
- **b30's crash at 88 s** ([M] b106): a recorded animation's 2-byte read from an odd address (the Pelicans fly by recorded animations). Read a byte at a time now (item 11).
- **A BSP switch wiped the animation cache's pages** (0xCD fill): b30's switch into the facility would have hit it; b106 needed `animnowindow` (item 11).
- **A disc read request could be taken half-written** by the reader thread (b106 too), and **sounds past the 12th kept a stale muffling** (item 12).
- The game state's CPU part overflowed since item 4 (item 10); smaller fixes from the review (item 12).

**Memory**
- **Campaign-only engine:** no networking or multiplayer game engines; co-op and multiplayer become their own branch (item 8). Program 5.16 → 4.49 MB.
- **RAM outside the heap used** (about 500 KB): the start-up buffers that fit in it go there (item 13).
- Data sized for the PSP (item 9), the AI's path debug records gone (item 1).
- Heap 5,376 → 6,016 KB. [E] Highest heap top of four maps: a10 4,820 KB.

**Draws: the flicker** (item 13)
- Display list 384 → 512 KB.
- Bending parts' bone matrices go into the list once a part, not once a pass.
- Change-colour palettes cached (6 → 40 slots): about 60 a frame were worked out into the list.
- Near the eye, a part's triangles are copied only when some are dropped or cut.
- When the list is still full, the smallest models on the screen go first, the same ones each frame (the size floor, up to 8 px).
- [E] b30 replayed from Sheepdog01's screenshot spots: model draws skipped for room 100-160 → 0 a frame once past the Pelicans, decal draws skipped ~300 → ~16.

**Sounds** (item 13)
- Sound cache 1,152 → 1,536 KB. [E] failed loads for room: d40's opening 5,993 → 144; a10 116 → 0; The Library 0.

**Speed** (items 1-7)
- No 82 KB debug record per path search; cluster unlinks without walking the list; `_stricmp` without `tolower`; objects rejected on a copy of their sphere; a near-eye corner in one transform; the field of view's tangent once.
- Sleeping scenery checked every other tick: keep word `sceneryhalf`, **off by default**.

**Looks**
- The ocean draws (its first map, a reflection cube map, is passed over for the next one) [E].

## Not fixed yet (b108; item 14)

- The Pelicans' thrusters and c40's light shafts still draw white (several maps multiplied; the GE draws one).
- White streaks over the water in the Pelican's flight.
- Next to the Pelicans the list still fills (14-34 model draws a frame left to the size floor) [E]. Repeated state per part is the next saving.
- a50, b40 and a30 still don't fit the tag window.

## Checked [E]

| Run | Keep words (besides `assettrack geo2 nearauto`) | Game time | Heap top (of 6,016 KB) | Sound loads failed | Result |
|---|---|---|---|---|---|
| b30 beach route | `sceneryhalf` | 8,272 | 4,424 KB | 70 | clean |
| The Library | `savetest spherecheck unlinkcheck nearcheck` | 7,833 | 4,564 KB | 0 | clean; checkpoint and revert; 0 mismatches in every check |
| d40 opening | | 9,061 | 3,900 KB | 139 | clean |
| a10 opening | | 6,514 | 4,820 KB | 0 | clean |

No crash, no strict-mode sites, 0 symbols stubbed.

## New keep words

- `soundN`: the sound cache's size in KB (512 to 4,096), for example `sound1920`. Default 1,536.
- `nosizefloor`: models are never left out by their size (draws skip when the list is full, as in b106).
- `bonesdirect`: bone matrices loaded by every pass, as in b106.
- `animnowindow` is no longer needed.

## Hardware test plan

1. **b30, past the old crash.** Keep words `assettrack geo2 nearauto` (no `animnowindow`: the fix is what's tested), deathless. Play the landing, the beach fight, up the beach, and into the facility if you get that far (the BSP switch). Look for:
   - no crash at about 90 s of play;
   - fewer objects flickering; when something is missing it should be small and far, and stay missing rather than flicker;
   - sounds: the log's `sound cache:` line, failed loads for room (b106: 204);
   - the log's `GE: the models' share` and `models left out for room` lines;
   - the ocean.
2. Optional: The Library, the same keep words, for the A/B against b106's run 1.
