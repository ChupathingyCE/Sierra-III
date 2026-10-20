# Review: fix-cortana (H3's seam through Cortana)

Branch `fix-cortana`, off the w139 merge of fix-models and fix-lights. Evidence labels as in CLAUDE.md.

## The cause: a10's alert lights drawn without their gel

**Not her model [E].** In a10's back view of the hologram (camera behind her, Keyes on the right), the straight edge runs at screen column 174. The frame before she materialises, from the same camera, has the same edge at the same column: the right side of a wall pillar behind her, bright warm tan against an unlit recess. She is drawn additively (blend 3), so the pillar shows through her, and its edge reads as a seam. Her parts' order, self-occlusion and shaders don't move it (fix-models' A/B).

**The pillar's warmth is the light pass [E].** With `nolightpass` the pillar loses its red, her hair reads as one violet over both sides, and the edge drops from about 50 to 15 levels of red. The light pass's own contribution over that frame (lit minus unlit) is a red flood over both walls of the room: 25,000 pixels, mean (29, 4, 2). ChupathingyCE's PC build at the same moment shows those walls grey, with no red anywhere [E].

**The lights [E], [D].** The `lightdump` instrument names them: `levels\a10\devices\alert_light\alert_light`, three or four of them, colour (1.00, 0.17, 0.19), radius 2, no cone (cosines -1), plus the command display's teal light. The Xbox shapes every dynamic light on the level with its gel: a cube map turned by the light's yaw, roll and pitch functions over game time (`rasterizer_xbox_environment.c`, `_rasterizer_environment_diffuse_light_begin`). The alert lights are rotating beacons: red beams that sweep the walls. The GE's light pass (`psp_ge.c`, dynamic lights) lights by distance, cone and angle only and has never read the gel, so each beacon floods the walls around it with its full red every frame.

## The change (on by default; keep word `nogel`)

`FIXES_MODELS` `#lightgel`: a light with no cone (a spotlight's cone already comes from the GE's projected cone) carries its gel's average colour, read from the pack (`psp_tex_average_known` on the gel's bitmap, definition +0x70). That is the beams' light averaged over a turn, not their sweep: a restoration of the light's level only. `nogel`: the full colour, as before.

**Not yet effective [E].** The b139a bake doesn't carry the gels: `lightdump` reports the alert light's gel as `unknown` on every frame, so the change falls through and the look is unchanged (cost nil). It needs the gel cube maps (or their averages) in the pack: a bake change. Until then the seam stays.

## Instrument

`lightdump` (keep word): on a screenshot's frame, each light the level's light pass takes: its tag (`psp_light_tag_name`, hud_draw.c), colour, colour drawn, gel index and average, radius, place, facing, cone.
