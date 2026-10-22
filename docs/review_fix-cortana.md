# Review: fix-cortana (H3's seam through Cortana)

Branch `fix-cortana`, off the w139 merge of fix-models and fix-lights. Evidence labels as in CLAUDE.md.

## The cause: a10's alert lights drawn without their gel

**Not her model [E].** In a10's back view of the hologram (camera behind her, Keyes on the right), the straight edge runs at screen column 174. The frame before she materialises, from the same camera, has the same edge at the same column: the right side of a wall pillar behind her, bright warm tan against an unlit recess. She is drawn additively (blend 3), so the pillar shows through her, and its edge reads as a seam. Her parts' order, self-occlusion and shaders don't move it (fix-models' A/B).

**The pillar's warmth is the light pass [E].** With `nolightpass` the pillar loses its red, her hair reads as one violet over both sides, and the edge drops from about 50 to 15 levels of red. The light pass's own contribution over that frame (lit minus unlit) is a red flood over both walls of the room: 25,000 pixels, mean (29, 4, 2). ChupathingyCE's PC build at the same moment shows those walls grey, with no red anywhere [E].

**The lights [E], [D].** The `lightdump` instrument names them: `levels\a10\devices\alert_light\alert_light`, three or four of them, colour (1.00, 0.17, 0.19), radius 2, no cone (cosines -1), plus the command display's teal light. The Xbox shapes every dynamic light on the level with its gel: a cube map turned by the light's yaw, roll and pitch functions over game time (`rasterizer_xbox_environment.c`, `_rasterizer_environment_diffuse_light_begin`). The alert lights are rotating beacons: red beams that sweep the walls. The GE's light pass (`psp_ge.c`, dynamic lights) lights by distance, cone and angle only and has never read the gel, so each beacon floods the walls around it with its full red every frame.

## The change (on by default; keep word `nogel`)

`FIXES_MODELS` `#lightgel`: a light with no cone (a spotlight's cone already comes from the GE's projected cone) carries its gel's average colour, read from the pack (`psp_tex_average_known` on the gel's bitmap, definition +0x70). That is the beams' light averaged over a turn, not their sweep: a restoration of the light's level only. `nogel`: the full colour, as before.

**The bake (`tools/texpack.py`) [D].** The pack carries every cube map as an 8 x 8 patch of its average colour (the cube patches, under the bitmap's own key; `--no-cube-patches` leaves them out, and with them the gels). A light's gel is a cube map, so it already had a patch, but of its first face only, and a beacon's beams sit on some faces and miss others. `gel_bitmaps` names the bitmaps only lights use (`ligh` primary or secondary cube map, nothing else), and for those `gel_average` takes the mean over all six faces: when each face is padded to an alignment it reads each face's top level; unpadded, it averages the whole data (top levels weigh three quarters, every face alike), which holds however faces and levels interleave. `cube_patch` takes that colour in place of the first face's. Nothing else in the pack changes: only those gels' one palette entry. No new bake switch.

**The A/B [E]** (bake from this branch's bundle, a10 b30 ui; a10 game 100-121 s, a shot a second; b30's Pelican flight and beach fight to 240 s; default against `nogel`, both with `lightdump`):
- `lightdump` now reads the alert light's gel as `average`, (24, 28, 24) of 255: a mostly dark gel with narrow beams. Drawn colour 0.094 0.019 0.018 against 1.000 0.169 0.188.
- The seam: the pillar's red edge behind her at x ~174 (shots at game ~108 and ~110 s, rows where `nogel` steps by 20 or more) falls from 37 and 46 red levels to 11 and 21: about what turning the whole light pass off gave (15), and the walls read grey as on the PC. The frame's mean red drops 6-12 levels; green and blue move under 1.5. The pillar's edge is still faintly there (the level's own lighting).
- Cost: nil. Work per frame identical to 0.01 ms in all four profile windows on a10; `lights_render_diffuse` the same.
- b30: no dimming. Every light in the fight (assault rifle, plasma pistol and rifle muzzle flashes, frag) has no gel (index -1), so the change never applies; work per frame within 0.4 ms a window either way (timing drift: the runs end 27 ticks apart).
- Shots: nogel left, gel right, kept off the repository.

**Still open.** The seam through her fades to the pillar's faint edge (the level's own lighting, which the PC shows too); the beacons give their average, not their sweep (below).

**A sweeping gel (not built) [U].** The Xbox draws the gel per pixel, turned over game time; the averaged gel is right on average and wrong at any instant (no moving beams). Two ways to have the sweep:
- Per vertex, on the CPU, in the light pass that already lights each vertex (falloff, cone, angle): rotate the light-to-vertex direction by the gel's matrix (one matrix a light a frame), pick the face, sample a small baked gel (six 8 x 8 faces, under a new key bit). About 30-40% more work a lit vertex for gelled lights only; a10's light pass is 3.8-5.5 ms a frame in this stretch, so up to ~1-2 ms where beacons are in view. But level walls have few vertices and the beams are narrow: they would fall between vertices and flicker. Not recommended.
- Per pixel, on the GE: draw each beacon's beams as projected textures (the GE's texture matrix from positions, divided by q), one or two beam projections a beacon, the matrix turned each frame by the gel's functions. GE cost: one more textured pass over the triangles the light already reaches; no CPU work a vertex. This is the faithful one, and a look call for Sheepdog01 after its own A/B.

## Instrument

`lightdump` (keep word): on a screenshot's frame, each light the level's light pass takes: its tag (`psp_light_tag_name`, hud_draw.c), colour, colour drawn, gel index and average, radius, place, facing, cone.
