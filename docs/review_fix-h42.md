# Review: fix-h42, the lighter wedge beside b30's bunker door frame

The fault: on b30 (second structure BSP, the bunker doorway), the wall right
of the slanted door frame shows a lighter triangle with a hard diagonal edge.
The original draws that wall as a smooth olive.

## Cause [D/E]

- The bake doesn't move the lightmap coordinates [D]: the desktop build reads
  the same raw lightmap coordinates for the doorway's triangles as the port's
  geometry pack holds. The edge is a real chart seam.
- The source page is continuous across that seam [D]: the shared corners read
  the same on both charts (0.353, 0.451). The left chart covers about 3 x 2
  texels of a 256 page and rises to 0.71-1.0 inside it.
- The asset track (the play preset) halves lightmaps [E]. The 3 x 2 chart
  becomes 1.5 x 1 texels of one flat colour, so the seam steps from 0.64 to
  0.45: the wedge. The main pack (full-size lightmaps) draws a smooth
  gradient and no wedge.

## The change: chart-aware halving (bake side only)

Ported onto w139 from the h42 branch (its bake-side commits; the runtime
whole-page lookup and the diagnostics stay there).

- `tools/lightmap_masks.py`: `page_charts` splits each lightmap page into
  charts (triangles joined through shared lightmap corners); `chart_halve`
  finds seam spots whose step the plain halving widens by more than
  `SEAM_STEP` (32), blames the side that moved, and refits that chart's
  half-size texels by least squares against the GE's bilinear reads of the
  full page (corners and edges weighted 4; a light pull, 0.1, toward the
  plain halving). Only charts at most `CHART_MAX` (8) texels across are
  refitted: larger charts keep their gradient through halving, and refitting
  them only changes their blur.
- `tools/texpack.py`: keeps the plain halving's palette and dithering and
  re-picks only the refitted texels' palette entries. Every other texel is
  the same byte as before, and every page the same size. The bake log gives
  "lightmap charts" lines: charts, texels and each page's worst seam step
  before and after.
- `bake/bake.py`: `--lm-chart-halve` is on by default (a restoration);
  `--no-lm-chart-halve` bakes plain halving as before. `--chart-max N` and
  `--seam-step N` tune it; `--lightmap-seam-whole` (pages kept whole) stays
  available and off.

Off switch: the bake option `--no-lm-chart-halve`. It is not a keep word: the
refit is in the packed texels, so a pack baked without it draws as before
with any build. The runtime is unchanged (no EBOOT change).

## Evidence (emulator, b30, a test bake of b30 and ui)

- Bake [D]: 939 charts refitted on 42 of b30's 55 lightmap pages, 9,587
  texels (the doorway's page: 122 charts, 1,130 texels). Seam-breaking charts
  by size: 524 at 4 texels or fewer, 939 at 8, 1,370 at 16, 1,699 in all.
  The track's texture pack is the same size as with plain halving.
- The wedge [E]: at the doorway (lightmap-only pass mode), the wedge's edge
  steps 49-69 grey levels across 3 pixels with plain halving and 5-8 with the
  refit; the wall now matches the main pack's (full-size) lighting within a
  couple of levels on average, and the desktop build shows no wedge.
- Elsewhere [E]: b30's opening (flight and beach), 19 shots: 0-37 pixels a
  shot differ, at most 10 levels (small lit spots on the dropship and far
  structures). At the doorway the wall right of the frame changes over about
  40,000 pixels by up to 25 levels: the wedge region and its chart's
  neighbours, now as the full-size lighting draws them.
- Play [E]: the state checksums on every tick match plain halving's (625
  ticks at the doorway, 1,000 over the opening). Object lighting samples the
  lightmap, but these refits leave every sampled value play depends on alone.

## Cost

None at run time: the same pages, the same sizes, the same draws. The bake
takes a few seconds more per map (the chart fit).

## Not yet done

- An all-maps bake with the change, which must succeed before it is called
  done; its "lightmap charts" lines give the counts per map.
