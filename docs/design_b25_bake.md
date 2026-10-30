# B25's fix in the bake (design, 2026-10-26)

**The bug.** On the PSP, Chief's armour (and other change-colour parts) draws white or red whenever the Media Engine carries the frame out; the HUD's meters on a10 draw the wrong graphics. The emulator never shows it.

**The cause** [M]+[D]. The Media Engine sees main RAM only: at VRAM's addresses it reads memory of its own (PPSSPP pull request 22690, hrydgard: "the ME doesn't see VRAM but memory of its own"; mcidclan's psp-media-engine-reload: its ping-pong buffers at 0x04000000 on the ME side). The GE replay on the ME reads three things out of textures, wherever the texture lives:

| What the replay derives | From | Changes | Where |
|---|---|---|---|
| The change and glow masks (a grey CLUT for the colour pass) | the multipurpose map's palette (blue: change, green: glow) | never | `multipurpose_pass` |
| The meters' palettes | the meter bitmap's 32-bit palette, and the meter's value | the value every frame | `meter_palette` |
| Average colours (models' reflection folded into their ambient; the projected flashlight cone) | the texture's top level through its palette | never | `ambient_with_reflection`, the light pass |

A texture in VRAM read from the ME gives what isn't there: Chief's change map came back all zeros (sierra-hw 40434355878, frame-locked: the GE's CLUT `ff000000` throughout; with `gemeonoff` the same map was in RAM and right). The mask cache keeps whatever was worked out first, so the fault sticks per texture. The pinned PPSSPP lets the ME read VRAM.

**The rule this design follows.** The replay reads nothing out of a texture in VRAM. Everything it derives from a texture is either made by the bake (it never changes) or read from a texture the bake places in RAM (meters: their palette is fixed, their value isn't).

## What the bake adds (tools/texpack.py, `runtime_reads`)

1. **Averages: a `TAV1` table** after the others (`'TAV1', u32 count, count x u32 average (ABGR)`, one a texture in directory order; 0 for a texture in its own colours), each texture's `runtime_average`, worked out exactly as `psp_tex_average` sampled it. Every table before it (`TNT1` to `TNC1`) is now written, empty or not, so the chain always reaches it.
2. **The change mask after each multipurpose map's levels:** the palette's blue as a grey 8888 CLUT (16 or 256 entries), 64-byte aligned, the last bytes of the texture's data, marked by directory flag 8. Exactly what `multipurpose_pass` worked out. Only the change mask: the glow pass draws from the glow maps the bake makes (`glow_looks`).
3. **Meters: directory flag 16** (the CPU reads this palette; keep it in main RAM) on texpack's `meter_maps`.
4. **Bake format 2** (`bake.py`'s `BAKE_FORMAT`): a build with this refuses older maps at the bake check, on screen, before anything draws.

## What the runtime does with it

- `change_pass` binds the multipurpose map and loads the CLUT from `psp_tex_change_mask`: the resident address plus the offset, no read. The 40-slot mask cache (40 KB of static RAM) and its working-out go.
- `psp_tex_average` takes the table's value where it used to sample (same rules: the texture resident, kept in `averages` once asked, so `psp_tex_average_known` behaves as before). The sampling, `c4xavgoff`, `avgold` and `avgmerealloc` go.
- `load` puts flag-16 textures in the RAM cache only, and the start's VRAM set leaves them out. `meter_palette` is unchanged.
- A part with a glow colour and no glow map isn't lit (`glow_unmapped` counts them; it was the mask alone before). `glowflat` goes.

## Costs (sizing bake, lab sierra-bake 40446101594, texpack's `b25 sizing` line) [M]

| Map | Textures | Multipurpose maps | Both mask CLUTs | Meter maps (to RAM) | `TAV1` |
|---|---|---|---|---|---|
| b30 | 732, 10,657 KB | 82, 1,041 KB | +90 KB (+0.8%) | 3, 16 KB | 5,864 B |
| c20 | 768, 14,684 KB | 39, 497 KB | +35 KB (+0.2%) | 3, 20 KB | 6,152 B |
| ui | 380, 6,633 KB | 5, 66 KB | +6 KB | 0 | 3,048 B |

- **Only the change mask is needed:** the glow mask (green) served a self-illuminated part without a glow map, and the bake makes a glow map for every self-illuminated model shader (54 on b30); every PSP run of 10-26 logged self-illumination 100% through glow maps. Built (sierra-bake 40447629227): b30 82 masks +43 KB, a10 76 +38 KB, c20 39 +17 KB, ui 5 +3 KB, in the texture caches while those maps are resident.
- **Meter maps in RAM:** the GE reads them from RAM (swizzled: ~20% slower than VRAM [M], few and small).
- **A rebake** for the release. The bake tools ship with it already (b143's Windows and Linux bundles).

## Proof

- Exactness in the emulator: frame hashes with `readswait` against the shipped behaviour, two old runs for the noise (as b25vram2 40439142945), on a10 and b30; the masks in the GE's words equal today's (`changetrace` from b25-vramguard's history).
- The PSP-3000: a10 heroic's pod exit, Chief green and the crew right (as 40439058413); a timed pair on a10 and b30.
- Sheepdog01's PSP-1000 hand test: Chief, the crew, a10's HUD meters.

## Not in this change

PRECONVERT, the stencil records (B26), other disc versions, and measured texture levels (the VRAM lever) belong to the tock's bake, proven there. The first fix (the ME refusing VRAM reads, textures moved to RAM at run time) is on branch `b25-vramguard` for reference.
