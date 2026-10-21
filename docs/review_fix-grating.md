# Review: fix-grating (hand test 3 items 4 and 5: a10's grating, c10's hanging moss)

Two bake-side restorations, on by default, each with a bake option that turns it off. No EBOOT change, no hot code.

## 1. a10's grating drew as rows of blobs (item 4)

**What the player saw** [M]: a10's grating panels (`levels\a10\shaders\alpha test`, `burn alpha test`) as a coarse pattern of round blobs; the PC build draws a fine diamond wire mesh. The emulator draws the same blobs at the same eye [E], with or without mip levels (`nomip`), so the mip chain is not the cause.

**Cause** [D, tagdeps]: the shader is cut out by its bump map's alpha (the Xbox tests the bump map's alpha). The base map is 1024 x 256 and bakes to 256 x 64; the bump map is 1024 x 256 at scale 1, so 4 x 4 bump texels fall on each baked texel. The mesh repeats every 12.8 base texels across, 3.2 texels at 256 x 64: one diamond is three texels wide, and the GE's bilinear alpha test rounds each into a blob. Simulated (tagdeps "as the GE draws it"): 5.5% of the cut-out's texels wrong at 256 x 64, 1.6% at 512 x 128.

**Change** (`tools/texpack.py`, `bake_one`): a level cut-out whose bump map is finer than its baked base map is baked larger, doubling until at most 2 bump texels fall on each baked texel or a side reaches 512. a10's two grating maps go 256 x 64 -> 512 x 128. The bake log's "cut-outs kept fine" line lists every map it enlarged.

**Cost** [D]: per enlarged map 4x its texels (a10: 2 maps, 16 KB -> 64 KB each at 8 bits, plus mips). Drawn only where the grating is on screen; the Memory Stick budget is 96 KB a frame, so a first sight of both may take one frame more to load. Measured cost: the next emulator batch (before/after at the same eye).

**Off:** bake option `--no-cutout-fine`.

## 2. c10's hanging moss drew as flat bright-green strips (item 5)

**What the player saw** [M]: long pale-green bands with no texture; on the PC, thin dark strands of moss lost in the fog.

**Cause** [E, sdump; D, tagdeps]: the strands are `levels\c10\shaders\c10 moss`, alpha tested, whose base map and bump map are the same bitmap, `c10 moss1` (32 x 128 DXT3, a colour map with the strands' shape in its alpha). The bake classed any bitmap named as a bump map as a bump map and dropped it, so the base map was never baked: the GE drew the surface flat in the shader's colour with no alpha test ("map alpha 0").

**Change** (`bake/lib/refs.py`, `classify`): a bitmap a shader names as its base map is baked as a base map even when one also names it as its bump map. The relief pass skips a shader whose bump map is its own base map (a colour map holds no normals). The cut-out then comes from the same map's alpha, as on the Xbox.

**Cost** [D]: one 16 x 64 map more in c10 (about 1 KB with its palette); the strands draw fewer pixels than the flat strips did (the alpha test drops most of each strip).

**Off:** bake option `--base-as-bump`.

## Checks

A test bake of a10, c10 and ui with both on, then the same-eye emulator shots and a cost pair (the same EBOOT on the test bake and on the release bake before it):

- **Bake** [D]: "cut-outs kept fine" lists a10's two grating maps and c10's two b30 grate maps (`alpha grate`, `alpha grate b`), each 256 x 64 -> 512 x 128; no errors. `c10 moss` is listed among c10's cut-outs, now with its own map. a10.ptex +256 KB (+1.7%), c10.ptex +259 KB (+1.8%), the `_track` packs the same; ui unchanged; every .map, .pgeo and .pstn byte-for-byte the same size.
- **Look** [E]: a10's grating draws as a fine diamond mesh at the eye where it drew as blobs, close to the PC frame; c10's moss draws as thin dark textured strands in the fog (the strips are gone), as on the PC. The draw dump shows `c10 moss1` with map alpha 1 (was 0).
- **Cost** [E]: a10 at the grating: 126 textures loaded either way, 2,113 KB vs 2,049 KB; largest frame 1,638 KB vs 1,574 KB; 14 vs 13 frames drew something untextured (longest run 11 vs 10); frame time the same (12.81 ms mean). a10's opening (non-debug): +64 KB loaded, 17 vs 16 untextured frames, frame time the same (14.34 ms). c10 at the vines: 170 vs 167 textures, +3 KB, untextured the same, frame 24.33 vs 24.30 ms; c10's opening 24.68 vs 24.53 ms. Within the runs' spread; the extra untextured frame is the larger grating's first load.
- **Still to do:** the all-maps bake (b30, and any other map using the b30 grates, picks up the finer cut-out too: its "cut-outs kept fine" lines say which).
