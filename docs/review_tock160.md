# Review: tock160

The tock after tock159 (`74ba0f7`), and the line the next tick comes from: four merges, two levers turned on by default, then the tock's own profile and layouts. Every change keeps a word that turns it off.

| Change | Default | Off word | Its own review |
|---|---|---|---|
| AIMEOBSLOOK (B54): the AI job looks at the collection's word between the steps of the obstacle-avoidance search | **on** | `aimeobslookoff` | `docs/review_mestop.md` |
| SNDTIERBACK v3 (B55's safety net): at an ME stop, the tier's sounds read again from the disc into the main tier | **on** | `sndtierbackoff` | `docs/review_mestop.md` |
| LIGHTFIRST: the objects' lighting in a pass of its own before the model walk | **on** | `lightfirstoff` | `docs/review_lightfirst.md` |
| DRAWPART slice 2 + LIGHTREUSE: the model walk records its model calls and makes them after the walk | **on** | `drawpartoff` | `docs/review_drawpart.md` |

## The merges

All four branches sit on tock159, and each carries the one before it, so they came in without a conflict:

- `recon/sndtierback3` (`bd6ae399`) fast-forwards over `master/mestop` (`2643bbd`): aimeobslook already on, sndtierback v2, then v3's retry.
- `perf160/lightfirst` (`4d0cead`), merge `9829f95b`.
- `drawpart2/slice2` (`4168c11`), merge `b007e0df`.

## The two flips (`psp/src/psp_platform.c`, `keep_levers`)

```
	{ "sndtierback", "sndtierbackoff" }, { "drawpart", "drawpartoff" },
```

`keep_levers` is read before M6 #2's table, so `m6off` leaves both on, as it does the rest of the row.

- **sndtierback.** Sheepdog01's call (2026-11-04 10:53): an ME stop is a bug, fixed where it runs (B54's fix is aimeobslook), not a state to build recovery around. v3 stays on as the net as it is. On the PSP-1000 it is safe after a forced stop: no hang, no copy job or DMA after it (lab 41865182489). It isn't a full recovery: the main tier is fragmented at the stop, and 352 of 444 blocks still play silent there. Until an ME stop happens, its cost is one flag test at the tier's loss.
- **drawpart.** It turns `lightreuse` on too. Exact in play, both seeds and every GE list (lab dp2x1 41861226372). On the PSP: c10 frame -6.1% and -7.4% (PSP-3000, 41870028074), b30 flight -4.3%, the route -2.8% (PSP-1000, 41870061338). The b30 landing came out +0.8 to +1.2%, and the drawing's own counters are no higher there; the pair below looks at it again on the merged tock.
- **The defaults' room.** With both, the default words need 351 of `KEEP_DEFAULTS_ROOM`'s 384 bytes, ~361 with the release build's `nooverlay`. The run logs list every word added; a word that doesn't fit is lost silently, so the next default flip should raise the room.
- **Recon's flip.** recon pushed its own sndtierback flip (`a0b4df43`) after this tock's flip was in. Its merge (`acf738e0`) keeps both lever lines and recon's comment, in the same number of lines, so the code and every function's line are as at `badb0db7`, where the exactness runs and the profile's counts were made.

The comment above the loop says why each lever is on. sndtierback's line no longer says it waits on a PSP run.

## The profile and the layouts

- **`pgo/campaign-w160a.tgz`**: the tock's own profile, 13 counting runs at `badb0db7`, every merge and both flips in. The strict build found no function without counts, at the profile's commit and at the layouts'. As before, the counting runs use the preconverted bake, so the full-rate music's mix has no counts.
- **`psp/layout/w160a_tgc90.txt`**: codeorder's placement by cache set from the tock's own temporal graphs (b30's route, c10, c20; the ME's graph at weight 0): 234 hot functions, 90.1% of the fetches. Its alignments come from a strict build of the same EBOOT.
- **`psp/layout/data_hotg160a.txt`**: HOTG's hot variables from the spread build's misses: 210 placements, holding 97.4% of the candidates' misses; 8 new and 3 gone against hotg159a.
- **The recipe:** `PSP_LTO=1 PSP_PGO=use PSP_PGO_PROFILE=campaign-w160a PSP_PGO_STRICT=1 PSP_PGO_EXTRA=-fno-unroll-loops,-fno-peel-loops,-fno-tracer,-fno-ipa-cp-clone PSP_TEXT_LAYOUT=w160a_tgc90 PSP_DATA_LAYOUT=hotg160a`. The bake is **w157m**, as tock159's.

## Proofs

| What | Lab run | Result |
|---|---|---|
| Exactness: tock159 vs tock160 with `aimeobslookoff sndtierbackoff lightfirstoff drawpartoff` vs tock160's defaults | t160x1 41875328154, t160x2 41875345878 | play and both seeds identical at every tick on b30's route (4,500), c20, d20, c10 (3,600) [E] |
| `drawpartoff` draws as before | t160x2 | GE lists identical, default vs `drawpartoff`: b30 2,280 of 2,280, c10 1,832 of 1,832; pictures differ at 193 of 2,246 b30 frames, against 182 for default vs default; play and seeds identical [E] |
| The defaults | t160x2 | every word added, `sndtierback` and `drawpart` among them, none lost: 351 of 384 bytes; the front door (menu, then a10) clean [E] |
| The profile | pgo-merge 41877121790 | campaign-w160a, 13 runs; strict 0 functions without counts (41878627719, 41886001543) |
| The release check | rc160a 41886015675 | 14/14 played, strict 0 sites, the `PSP_METRICS=0` build links and plays; sound loads failed for room in 60 s: d20 91 (tock159 104), others 0; heap 5,776 KB [E] |
| PSP-1000, b30 route to exitat150, two runs a side, against tock159's | 41891744166 vs 41820025872 | cinematic frame -2.2% (real), drawing -7.1% (real); flight frame -2.8% (both runs under tock159's lowest, inside its spread), drawing -5.9% (real); landing 2000-2600 frame -1.5%, drawing +0.4% (both noise); beach +4.8% by battle size (noise); heap peak 5,284 KB both, free low 491 KB, 0 failed, 0 sounds refused, no crash, no ME stop [M] |

**What the PSP pair says.** The round is faster where it draws, as LIGHTFIRST's and DRAWPART's own pairs said: the drawing is 6-7% quicker in the cinematic and the flight, and the frame follows by 2-3%. drawpart's +1% on the landing in its own pair doesn't show here: the merged tock's landing drawing is a tie. The beach moves by its battle's size, as in every pair. The margin check flags a 19.6-20.3 ms stretch at game time 901 in both runs; it spans a ring drain inside the AI job's yield, which tests the word before each job, so it overstates the wait: the word was answered within 1.3 ms at worst.
