# Review: fix-listres (a10's decals and floor stencils vanish)

Branch `fix-listres`, off w139. Evidence labels as in CLAUDE.md.

## The cause: the light pass fills the display list first [M], [D]

In a10's lit corridors in a firefight, the hardware log's by-kind report reads: lights 160.8 KB of the display list a frame (459.7 KB in the fullest frame), the fullest frame 708 of 768 KB, and the draws skipped for a full list go from 1,823 to 25,408 in the 300 frames around the vanishing decals: 11.9 decal draws and 60.5 particle draws skipped a frame, 5.3 light draws. The kinds' order in that frame is `qMLsldBpQTHo`: the light pass (`l`) runs before the decals (`d`) and the particles (`p`), so when it takes most of the list, the blood, scorch marks and floor stencils, and the particles, are what's left out [M]. The list's existing reserves keep room for the level (before its first draw), for the see-through draws and for the HUD; nothing kept room for the decals and particles against the light pass [D].

## The change (on by default; keep word `nolightreserve`)

`psp_ge.c`, `kind_limit`: light draws (`KIND_LIGHT`) stop `LIGHT_RESERVE` (64 KB) earlier than every other world draw, so the decals and particles drawn after them keep that much room. In the hand log's worst stretch decals and particles together took 25.3 KB a frame on average; their fullest frames elsewhere on a10 reach 95.6 KB of decals and 29.3 KB of particles, so 64 KB covers the average with room and most of a heavy frame [M]. What the light pass gives up is some dynamic light on the surfaces it reaches last, and only in frames whose list is already full; until the list reaches the light's limit nothing changes, and a frame whose list never fills draws exactly as before.

The by-kind report's last line now also says whether the reserve is on and how many light draws it held back (`light draws held back N`).

Restoration: decals and particles are part of the original's frame, and the original never drops them for room. On by default; `nolightreserve`: as before.

## Cost

One more comparison in `kind_limit` a draw (a kind test and a flag); no work added a frame otherwise.

**Emulator A/B [E]** (a10, the hand test's path to the decals' spot, then its camera held; on vs `nolightreserve`, at the 768 KB list and with `list512`; same game time each pair): frame 25.29 vs 25.29 ms and tick 2.979 vs 2.978 ms over the run (94 heartbeats), 25.18 vs 25.13 ms at the spot; with `list512` 24.70 vs 24.70 ms, tick 2.936 vs 2.937. No cost.

**The repro is incomplete [E].** The emulator's replay reaches the spot without the hand test's firefight: at 768 KB the fullest frame is 698 KB, nothing is skipped there, the light pass takes 128 KB a frame (173 fullest), and the two sides draw the same. With `list512` the list fills (17,677 draws skipped in the run): the reserve held back 88 light draws in one 300-frame window (80 more skipped than without it), but that window drew no decals, so neither side skipped any; particles were skipped 0.1-1.3 a frame on both sides alike. So the emulator shows the reserve engages and costs nothing; that it keeps the decals in a lit firefight rests on the hardware log's by-kind figures above [M], and the next hardware run's by-kind line (`light draws held back N`, decals' skips) is its check.
