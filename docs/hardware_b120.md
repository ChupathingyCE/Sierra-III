# Build 120 on Sheepdog01's PSP-1000 (2026-10-13)

Labels: **[M]** measured on a PSP, **[E]** emulator, **[D]** read in the code, **[U]** inference. The tests are `build_b120.md`'s.

## Test 1: the cinematic bench (b30, preset 3)

`b120_test1.tgz`. `benchreport.py` [M]:

| Stretch | b120 | b119 | Change |
|---|---:|---:|---:|
| cinematic (150-570), frame | **158.7 ms** (6.30 fps) | 162.2 ms (6.17 fps) | −2.2% |
| cinematic, drawing | 49.1 ms | 49.5 ms | −0.8% |
| cinematic, tick | 52.7 ms | | |
| flight (300-2000), frame | 238.1 ms | 242.2 ms | −1.7% |
| flight, drawing | 111.6 ms | 112.3 ms | −0.6% |

The rig saw the same direction from the fresh profile on b30's flight (231.7 against 235.9 ms with b119's profile on the same code) [M, rig].

### LK13: far water flickers on the horizon in b30's intro (Sheepdog01)

Sheepdog01: the water in the far distance of the intro cutscene flickers on the horizon; likely longstanding.

- **Not dropped draws:** "draws skipped with the display list full 0" in every window [M].
- **The lead [U]: depth precision as `nearauto` switches.** In this run the near plane was ×16 on 151 frames and ×1 on 149 [M]. Each halving of the near plane doubles a depth step at every distance (the design note above `near_plane_auto` in `psp_ge.c`), so ×1 frames have 16× coarser depth than ×16 frames, with a 16-bit depth buffer. Close-up shots in the Pelican pin it at ×1 (a model near the eye); wide shots go to ×16. On the coarse frames the far water and what lies just behind it would share depth values and fight, so the horizon flickers as the cuts alternate.
- **The check (Sheepdog01, ~3 min, any time):** preset 3 with `nearauto` replaced by `near4` in `keep.txt`. Steady water confirms it. The Pelican's interior will be cut open near the eye, which is expected with a pinned plane.
- **If confirmed, the fix (b121, a restoration with an off word):** draw the far water with a slight depth bias toward the eye so it wins ties, as the stencil nudges do. Smoothing `nearauto` would only slow the flicker.

## For b121 (Sheepdog01, 2026-10-13)

- **The build number and the active preset on screen:** in the launch menu's header, and as a small corner line in play (off with a keep word), so every screenshot carries them.
- **The log pull's `rm -f $psp/halo/shots/*`** fails in fish when the folder is empty (fish refuses a wildcard that matches nothing). Use `find $psp/halo/shots -type f -delete`.
