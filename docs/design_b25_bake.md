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

## What the bake adds (tools/texpack.py)

1. **Averages: a `TAV1` table** after the existing tables (`'TAV1', u32 count, count x { u32 key, u32 average (ABGR) }`, sorted by key), every texture's `runtime_average` (texpack already works it out exactly as `psp_tex_average` does, for the exact-CLUT4 check). The runtime fills its averages table from it at pack open; `psp_tex_average` reads the table and never samples a texture.
2. **Masks: two CLUTs inside each multipurpose map's block.** After the levels, 256 x u32 each, 64-byte aligned: the change mask (the palette's blue expanded to 8 bits, grey, alpha 0xFF) and the glow mask (green). Exactly what `multipurpose_pass` computes today. A directory flag bit (8: masks follow the levels) marks them. The replay binds the map and points the CLUT load at `resident address + mask offset`: an address, no read. The GE loads the CLUT from wherever the texture lives.
3. **Meters: a directory flag bit (16: the CPU reads this texture's palette; keep it in RAM).** On the meter maps (texpack's `meter_maps`), and on any other texture whose palette a CPU reads. The loader puts flagged textures in the RAM cache only (`load_into(index, 1)`, which exists). `meter_palette` is unchanged.
4. **A version gate.** A build that needs these refuses a pack without a `TAV1` table, on screen ("this pack is from an older bake: bake again with this release's tools"), instead of drawing wrong. The bake's id is already logged at start.

## What the runtime loses

- The 40-slot mask cache (`multipurpose_cache`, 40 KB of static RAM) and its working-out loop: the masks are data.
- `psp_tex_average`'s sampling (kept only for a texture missing from `TAV1`, which the gate makes impossible in a release).

## Costs, to be measured before it's built

- **Pack size:** +2 KB per multipurpose map (two 8888 CLUTs), in the texture caches, not the heap. Estimate 50-200 KB per map [U]: the sizing run counts them.
- **Meter maps in RAM:** the GE reads them from RAM (swizzled: ~20% slower than VRAM [M], few and small).
- **A rebake** for the release. The bake tools ship with it already (b143's Windows and Linux bundles).

## Proof

- Exactness in the emulator: frame hashes with `readswait` against the shipped behaviour, two old runs for the noise (as b25vram2 40439142945), on a10 and b30; the masks in the GE's words equal today's (`changetrace` from b25-vramguard's history).
- The PSP-3000: a10 heroic's pod exit, Chief green and the crew right (as 40439058413); a timed pair on a10 and b30.
- Sheepdog01's PSP-1000 hand test: Chief, the crew, a10's HUD meters.

## Not in this change

PRECONVERT, the stencil records (B26), other disc versions, and measured texture levels (the VRAM lever) belong to the tock's bake, proven there. The first fix (the ME refusing VRAM reads, textures moved to RAM at run time) is on branch `b25-vramguard` for reference.
