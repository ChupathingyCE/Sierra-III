# review_canopy: the Pelican canopy (glass tint)

Branch `fix-canopy` off w139. One draw change (`psp/src/psp_ge.c`) and an instrument (`tools/tagdeps.py`).
No bake change: it works on the release bake as it is.

## What was wrong

**Which build drew it last as it was [D]/[E].** Up to build 138, and build 139 on its first bake (no cube
patches), glass without a diffuse map wasn't drawn at all: the Pelican's canopy was clear glass over its lit
cockpit (front panes 10 13 10 at b30's opening, the cockpit's seats and frame showing). That read as dark glass
and looked right. On the release bake (cube patches in the pack) the same code draws the glass's reflection,
so the canopy turned a milky violet-grey wash over the still visible cockpit: 31 32 37. The desktop reference
at the same tick: 22 19 28, an even smoked violet with nothing of the cockpit showing, the side panes near white.

**The change that did it [D].** The fix-lights branch's "model and glass reflections" (H9: a10's floor glass a
black pit): glass with no diffuse map draws its cube map's patch, added and tinted by the view angle
(`psp_fixups.py`, `glasslit`; `psp_ge.c`, `psp_ge_seethrough_shader`). The patch only exists in a pack baked with
the cube patches, so the canopy changed with the bake, not the build. The reflection is about right; what's
missing is the pass before it.

**Why [D].** The Xbox draws glass in up to three passes (`rasterizer_xbox_transparent_geometry.c`, the glass
case): a **tint** pass that multiplies the frame behind by the tint map times the tint colour (source zero,
destination times source colour), then the reflection, added, then the diffuse map. The port never drew the
tint pass. The Pelican's glass (`tools/tagdeps.py`, b30): tint 0.227 0.220 0.314, no tint map, no diffuse
map, one-sided; the lifepod's: tint 0.447 0.471 0.714, two-sided. So the original darkens the cockpit to about
a quarter, violet, and adds the reflection over that.

## The change

`glass_tint_behind` (`psp_ge.c`, called from `transparent_flush_body` for an added, angle-tinted part):
glass drawn by its reflection alone is one added pass on the GE, so the tint rides in that same pass as the
blend's destination factor: `sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xFFFFFF, tint)`, the frame times the
tint plus the reflection, which is what the Xbox's two passes give. Only model glass (the level's see-through
draws aren't angle-tinted, so level glass keeps its look), only without a
tint map (a map can't ride a fixed factor), and only when the tint isn't black (the Xbox skips the pass then).

- Keep word **`glassnotint`**: the frame left as it is, as before. On by default: a restoration.
- Log: "glass draws tinting the frame behind" in the models line every 300 frames.
- Cost: nothing extra drawn (the same pass, another blend factor); one call per angle-tinted added part at
  the flush, which reads the shader's type first.
- Multiplayer: the same code compiles in; the Pelican and other vehicles' glass look the same there.

**The reflection left as it is [D].** The Xbox's reflection is not the cube's average: its combiners square
the cube texel three times (texel to the eighth power) and mix that toward the texel by the tint, which is
the two view colours mixed by the angle's cosine squared, and the mixed brightness scales it. `tagdeps.py`
now prints that average for each glass shader beside what the PSP adds from the patch: for the Pelican, face
on 21 20 31 (Xbox) against 10 8 17 (PSP), edge on 102 98 119 against 81 77 97. The PSP is a little dimmer, but
the canopy's front panes come out where the desktop is once the tint is drawn (below), so the patch and its
angle tint stay. The elite armour shares the cube map ("cubemap dark gray"): its patch is untouched.

## Evidence

[E] numbers and shots in the lab's findings (canopy rows). Shot pairs against the desktop frames at the same
tick: b30's opening flight (shot000, t300) and a30's lifepod from inside.

**b30's opening, shot000 against the desktop at t300 [E]** (front panes by the desktop frame's own pane
masks; one EBOOT, the release bake, the two sides by keep word):

| | upper front pane | lower front pane | side pane |
|---|---|---|---|
| desktop | 34 28 47 | 16 12 23 | 203 202 207 |
| tint (new default) | 33 33 42 | 24 26 33 | 22 26 25 |
| `glassnotint` (release bake as shipped) | 36 36 44 | 35 36 41 | 24 27 26 |
| no cube patches (b138, b139's first bake) | 10 13 11 | 16 17 14 | 16 19 17 |

The cockpit no longer shows through; the panes take the desktop's smoked violet. The upper pane is within a
few levels, the lower 8-14 brighter (its reflection there is the PSP's patch, the desktop's a darker part of the
cube). The pixels that change lie on the canopy alone (x 136-199, y 88-141); the later b30 shot is unchanged.

**a30's lifepod from inside [E]** (two-sided glass, tint 0.447 0.471 0.714): the window's sky 110 108 150 with
the tint, 180 173 186 without; the desktop's inside the pod 93 89 132 and 125 121 146 (its timeline runs later,
so a like view, not the same tick). Without the tint the pod's windows were washed near white.

**Play exact [E]:** both sides' player positions and game time identical on both maps (19 player lines each).
**Cost [E]:** none drawn (same pass); the frame times sit inside the runs' own spread.

## What's left (not done)

- The side panes: the desktop's go near white edge on, the PSP's stay dim. The PSP mixes the two view colours
  by the cosine where the Xbox uses its square, and adds the patch where the Xbox adds the cube's bright
  directions (`tagdeps.py`'s edge-on figures: 102 98 119 against the PSP's 81 77 97, before the per-pixel
  highlight). A per-vertex mix by the square would cost a branch in the see-through vertex loop; left for an A/B.
- Glass with a diffuse map (b30's floor glass, the powerups) still draws no tint: its diffuse pass is alpha
  blended, and the tint would need a second pass.
