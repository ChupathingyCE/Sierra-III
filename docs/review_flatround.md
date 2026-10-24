# Review: flatround (finding 14, the olive bridge glass)

**What it restores:** a10's cryo glass (the bridge window, the cryo pods' covers) draws a light neutral grey, as on the Xbox, instead of olive. On by default; `flatnoround` turns it off.

**Why it was olive** [E] (glass6 39296134720): the glass is a see-through draw with a flat tint (the bake's `SEETHROUGH_FLAT`: the map gives only its alpha) added over the frame (blend 7). Its tint is about (9,10,7) a pane. The GE rounds a blend's result down to the frame's 5-6-5 steps (red and blue 8, green 4) when it doesn't dither, so each pane adds (8,8,0): the blue is lost, and two panes make (16,16,0), olive. The Xbox adds about (18,20,14).

**The change** (`psp/src/psp_ge.c`, the see-through flush, `SEETHROUGH_FLAT`):

```c
if (flat_round && !draw->sprites && !draw->colored && !draw->doubled && !draw->sky &&
    (draw->blend == 3 || draw->blend == 7) && dither_all <= 0 && flat != 0xFFFFFFFF)
{
    flat = flat_rounded(flat);   /* + half a step: R+4, G+2, B+4, clamped */
    flat_color = flat;
    ...
}
```

The same rounded colour goes to the environment colour and the vertex colour (`sceGuColor`), which the flat model makes equal. Because the frame already sits on a step, frame + tint + half a step, rounded down, is frame + the tint rounded to the nearest step. So each pane adds about (8,8–12,8), and two make a grey.

**Why not the dither:** `addeddither` (the earlier try) fixed the colour for every added draw by dithering them, but its 4×4 pattern showed on the water, the haze and the glass (Sheepdog01, 2026-10-19 13:43: today's look is better). A flat tint is one colour, so it can be rounded once with no pattern. Textured adds (glows, plasma, water) keep today's banding.

**Where it applies:** flat-tinted see-through draws with an added blend (3, 7), not sprites, not coloured per corner, not doubled, not the sky, and not under `ditherall` (where the GE already dithers). The `GE: build 116 restorations` log line counts them (`rounded N`).

**Cost:** one compare chain and a few integer ops per flat-tinted see-through draw (a handful a frame on a10's bridge); nothing elsewhere.

**Proof:** the lab's flatround1 batch: a10's exterior and bridge both ways, b30 both ways (unchanged but for the count), a10 strict, Blood Gulch on the multiplayer build.
