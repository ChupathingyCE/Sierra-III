# Review: tock159

The tock after tock158 (`b93cf17`): two merges and one lever turned on by default, then the tock's own profile and layouts. Every change keeps a word that turns it off.

| Change | Default | Off word | Its own review |
|---|---|---|---|
| MUSROOM: the sound cache 256 KB larger, only with the full-rate music and the eDRAM tier | on (with `musadpcm` and `sndedram`) | `musroomoff` | (below) |
| MUSADPCM: the disc's 44.1 kHz music mixed at its own rate | **on** | `musadpcmoff` | `docs/optimization_ranking.md`; `docs/review_tock158.md` (its gate) |
| LIGHTRINGLOOK (B50): the ME serves the ring inside the flashlight's light pass | **on** | `lightringlookoff` | `docs/review_lightringlook.md` |
| AIMERINGLOOK: the AI job serves the ring at its looks inside an actor | **off** | `aimeringlook` turns it on | `docs/review_aimeringlook.md` |

## MUSROOM (`psp/src/psp_platform.c`, `psp_sound_cache_bytes`)

The full-rate music takes twice the sound cache's room of the halved music. With SNDEDRAM2's tier, the cache's last 656 KB sit in the ME's eDRAM and leave the heap, so 256 KB of them can go back to the cache's main part and the heap still keeps ~270 KB of the tier's gain. The growth applies only when the size is the default and both `musadpcm` and `sndedram` are on (and not `soundedram`, which puts the whole cache in the eDRAM). Every other build keeps its heap as it was.

```
if (kb == SOUND_CACHE_DEFAULT_KB && psp_keep("musadpcm") && psp_keep("sndedram") &&
	!psp_keep("soundedram") && !psp_keep("musroomoff"))
	kb += 256, grown = 1;
```

Why 256 KB: over 300 s of d20's swamp route, full-rate music refused 420 loads for room at the old size, against the halved music's 220; at 2048 KB in all it refuses 99, at 2304 KB 13. a10 refuses none at any size (lab musroom1). The log line names the growth (`the default + 256 KB for musadpcm's music (musroom)`).

## MUSADPCM on by default (`psp/src/psp_platform.c`, `keep_levers`)

`musadpcm` joins tock158's four in `keep_levers` (with `lightringlook`, below), read before M6 #2's table, so `m6off` leaves it on. The defaults now need ~317 of `KEEP_DEFAULTS_ROOM`'s 384 bytes; the run logs list every one of them, `inflightkb224` last.

- **Why on:** Sheepdog01's listening verdict on the PSP-1000 (2026-11-03 22:38): keep the 44.1 kHz music. Its frame cost on b30 was +0.8% with the ME's 44.1 kHz job and the converter's channel (lab 41214741040).
- **`src44` isn't a word any more.** The 44.1 kHz block has played on the converter's channel at 44,100 Hz by default since `e0a7dc05` (`audio44ordinary` keeps the ordinary channel). So only `musadpcm` goes in the table.
- **It needs a bake made with `--music-adpcm`** (the tock's music bake is w157m). The mixer picks the full-rate voices by their format (stereo 44.1 kHz ADPCM, `full_rate_voice`). On a bake without them the music is the halved voice, as before, played through the 44.1 kHz path.

## LIGHTRINGLOOK (`psp/src/psp_ge.c`, `psp/src/psp_me.c`; merged from its own branch)

B50: with the flashlight on, a record's light pass can hold the ME for more than 10 ms, and a ring job queued meanwhile (the mixer's points, say) is picked up past its deadline. The light pass's triangle loop now calls `psp_me_ring_look` every 8 triangles, on the ME only. It drains the ring once ~1 ms of ME clock has passed since the record began or since its last drain, and only with stack room above the AI job's guard. The time it spends goes to the ME's nested share (MEBUSY), so the busy figure isn't counted twice. Off the ME it is one flag test a light draw. On the PSP-3000 in d20's flashlight stretch, the late ring jobs went from 9 and 8 a run to 0 and 0, with no ME stop and the tick a tie. Its walk-through is `docs/review_lightringlook.md`. It brings round 1's `aimeringlook` with it, off by default. The only conflict was `keep_levers`: both lines are kept, and the comment above the loop says why each lever is on.

## The profile and the layouts

- **`pgo/campaign-w159b.tgz`**: the tock's own profile, 13 counting runs at the merge with ringlate2 (`e592611`). A first count, `campaign-w159a`, was made before ringlate2 came in. The strict build refused it: 9 functions had no counts (the light pass, the ME's start, report and yield, the AI's late check). So it was counted again and w159a is gone. The counting runs use the preconverted bake, as every profile has: it has no full-rate music, so the 44.1 kHz mix has no counts. With partial training it is still built at -O2, not for size [D, `psp_build.py`].
- **`psp/layout/w159a_tgc90.txt`**: codeorder's placement by cache set from the tock's own temporal graphs (b30's route, c10, c20; the ME's graph at weight 0), 234 hot functions, 90% of the fetches. Its alignments come from a strict build of the same EBOOT.
- **`psp/layout/data_hotg159a.txt`**: HOTG's hot variables from the spread build's misses, 205 placements, 10 lines different from hotg158a.
- **The recipe:** `PSP_LTO=1 PSP_PGO=use PSP_PGO_PROFILE=campaign-w159b PSP_PGO_STRICT=1 PSP_PGO_EXTRA=-fno-unroll-loops,-fno-peel-loops,-fno-tracer,-fno-ipa-cp-clone PSP_TEXT_LAYOUT=w159a_tgc90 PSP_DATA_LAYOUT=hotg159a`. The bake is **w157m** (w157 with `--music-adpcm`).

## Proofs

| What | Lab run | Result |
|---|---|---|
| Exactness: tock158 vs tock159 with `musadpcmoff musroomoff` vs tock159's defaults on the music bake | t159x1 41810278082, t159x2 41809690699 | play and both seeds identical at every tick on b30's route (4,500), c20, d20, c10 (3,600) [E] |
| LIGHTRINGLOOK's exactness (on tock158) | 41806738678 | play and both seeds identical; on the PSP-3000 ring lates 9/8 -> 0/0 (41807114492) |
| Sound refusals over 300 s, tock158 on w157 vs tock159 on w157m | t159x2, t159x3 41809705273 | a10 0 -> 0, d20 220 -> 133, c20 0 -> 0, b30 0 -> 0 [E] |
| The profile | pgo-merge 41812320595 | campaign-w159b, 13 runs; strict 0 functions without counts (41812587205, 41817410701) |
| The release check | rc159a 41817421923 | 14/14 played, strict 0 sites, the `PSP_METRICS=0` build links and plays [E] |
| PSP-1000, b30 route to exitat150, two runs a side | 41820025872 vs 41815173358 | cinematic frame +1.5% (real), flight +2.8%, landing +2.1%, 2700-3600 +4.3%, beach a tie by battle size (all four in the runs' spread); heap peak +256 KB (free low 511 KB), 0 sounds refused, no crash [M] |

**What the PSP pair says.** The round adds the full-rate music and costs a little frame. Only the cinematic's +1.4 ms is clear of the runs' spread. The other windows sit 2-4% higher, but inside it. The full-rate mix was priced before at +0.8% of b30's frame. The rest may be the full-rate music's larger sound loads (16.5 MB -> 23.2 MB on the route) or the run spread [U]. The heap stays healthy with the extra 256 KB of sound cache: 511 KB free at its lowest on b30 and no failed requests.

The merge commit for ringlate2 has the conflict note after its trailer lines (git's `# Conflicts:` block, kept because the merge was committed without the editor). It is pushed, so it stays as it is.
