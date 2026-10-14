# Review section for b121: see-through tints from texels, and the drawn map chosen by the bake

For `review_b121.md` (CLAUDE.md, "How AI is used here"): what, key lines, why, proven, default. Labels: [M] measured on a PSP, [E] emulator, [D] read in code or data, [U] inference.

Branch `tints121` from `w121` (`274f08a`): `cdc5641` (the bake), `1ecfb43` (the runtime). Fixes LK16 (a10's yellow window splotches) and LK18 (c40's dim golden column, the holograms drawn as flat bands) from `hardware_b120.md`.

| Part | Kind | Default | Off switch |
|---|---|---|---|
| Tints worked out over the drawn map's texels | restoration (bake) | on | bake with `--tints-from-means` |
| Three drawing models: modulate, add, flat | restoration (bake + GE) | on | the same bake option |
| The drawn map chosen by the bake | restoration (bake + runtime) | on | keep word `drawnmapscale` |

---

## 1. Tints from the drawn map's texels (`cdc5641`)

**What.** The GE draws one map of a see-through shader and multiplies it by a constant, the tint. Since b116 the bake worked the tint out with every map at its **mean** colour. It now runs the shader's combiners over up to 4,096 of the drawn map's texels instead.
- A map that is the **same bitmap at the same scale** as the drawn one takes the same texel (layered).
- Every other map, and a reflection cube the PSP skips, stays at its mean.

**Key lines.** `bake/lib/seethrough.py`:
- `generic_result()` and `chicago_result()` work on n × RGBA arrays (the same register code as before, one row per texel);
- `shader_tint()`'s `evaluate(group)`: the maps in `group` give texels, the rest their means;
- `bitmap_texels()`: the even sample of a map's top level.

**Why.** Means can't stand for a map layered on itself. c40's teleport column (`cinematics\effects\teleportation\shaders\teleport ring`) multiplies two copies of one gold streak map:
- its means give 0.29 × 0.29;
- where the streaks line up, the real product stays near the streak's own brightness.

So b120 drew the column at about a quarter of its strength [D].

**Proven.** [D]
- `--tints-from-means` gives exactly the old module's tints on 2,744 random synthetic shaders (same word, map and doubling).
- With every texel set to its map's mean, the texel path gives the means' tint (2,599 of 2,744; the rest are shaders brighter than 2x their map, where the add model fits better than a saturated modulate).
- The tint reports below show the named shaders before and after.

## 2. Three drawing models (`cdc5641`, `1ecfb43`)

**What.** Three ways for the GE to draw the result are fitted to the texels:

| Model | GE | What it draws |
|---|---|---|
| modulate | `TFX_MODULATE` (`GU_FRAGMENT_2X` when doubled) | the map times the tint (as before) |
| add | `TFX_ADD` | the map plus the tint |
| flat | `TFX_BLEND`, environment colour = vertex colour | the tint alone; the map gives only its alpha |

The alpha is the map's times the tint's in all three. Modulate is kept unless all of these hold:
- another model's error is under half of modulate's;
- modulate is off by more than 2/255 a channel;
- the shader has no fade (the fade rides on the vertex colour, and only modulate multiplies the whole draw by it);
- the shader isn't pinned at white.

The pack keeps the model in the two low bits of a tint's address: 0 modulate, 1 doubled, 2 add, 3 flat. Tag addresses are multiples of 4, and bit 0 was already "doubled".

**Key lines.**
- `seethrough.py`: `_fit()`.
- `psp_tex.c`: `psp_tex_shader_tint()` returns the two bits as `flags`.
- `psp_ge.c`:
  - `seethrough_tint_set()` turns them into `doubled` and `tint_model`;
  - the transparent flush sets `GU_TFX_ADD`, or `GU_TFX_BLEND` with `sceGuTexEnvColor(draw->color)`, and puts both back after the draw.

**Why.** The brief's case 2, a reflection added in, turned out to be two cases [D] (the combiner stages, from `tagdeps.py`):
- **a10's `cryo glass`** (tdump3: the bridge windows' pattern). Its colour is `cube × C1 × (1 − map × C1.a) + cube.b × C0.a × map`, and its alpha is `map.a × C1.a`. The map's share of the output is about 0: the window is the reflection, faint and nearly clear. A multiplier of the map can't draw that. b120's `20469494` doubled scaled every texel by ×1.16 red and green and ×0.55 blue, so the map's near-white texels came out as the yellow splotches. Add can't draw it either, because the map would show at full strength. Flat draws the reflection's mean (`20070a09`: about 0.03-0.04 grey, added through blend 7) and the map's alpha.
- **a10's `monitor glass`** (and the Longsword's `fb_monitor`): `cube × C1.a + map × C0.a`, which is the add model exactly.

**Default.** On (a restoration).

## 3. The drawn map chosen by the bake (`cdc5641`, `1ecfb43`)

**What.** For each shader, the bake measures each map's **share** of the output: the variance of what the frame gets when only that map (with its layered copies) varies.
- The map with the largest share is drawn, if its share is more than 1.5× the share of the map the runtime's own rule picks (the smallest scale).
- The pack lists those shaders in a new table after the tints (`TND1`). Each entry holds the map's index and the tint the bake worked out for the rule's map.
- `psp_transparent_map` draws the pack's map when the pack names one, and the smallest scale otherwise (packs before b121, and `drawnmapscale`).

**Key lines.**
- `seethrough.py`: `shader_tint()`'s `shares` loop.
- `texpack.py`: the `TND1` writer.
- `psp_tex.c`: the reader, `psp_tex_shader_map()` and `drawn_map_find()`.
- `psp_fixups.py`: `FIXES_121["source/rasterizer/rasterizer.c#drawnmap"]`.

**Why.** "Smallest scale" assumes the spanning map is the pattern. On the holograms the spanning map is a scrolling overlay [D]:
- `holo halo solar system` has `solar system` and the band twice, all at scale 1. The tie went to the later map, so the band was drawn.
- The ring holograms' content (`halo inner ring` 4×1, `halo outer ring` 60×1) lost to the band's 2×1.

Shares on c40 [D]: `solar system` 0.00203 against the band's 0.00069; `inner ring` 0.00785 against 0.00095; `outer ring` 0.00209 against 0.00024. The same rule also restores other shaders whose smallest-scale map is an overlay, or a map the shader never reads:
- a10's command display (`indicators`, `trajectory a`-`e`, `poa peremeter grid`) drew the static band at 0.1 scale, not the display;
- `poa engine exhaust` and `cortana vol` drew the band at 0.1-0.2 scale, not their own maps;
- b40's `energyrope` and `tubeenergy` drew a map their stages never read (its share is 0).

**Default.** On. The keep word `drawnmapscale` puts every shader back on the scale rule, with the tint the bake worked out for that map, so `drawnmapscale` shows b120's choice of map with b121's tints.

## Before and after, from the tint reports [D]

Lab `sierra-tags`, campaign120's maps, Sierra-III `tints121` as committed. Each report prints the new tint beside the means' one (`from means`), each model's error, each map's share, and a changed generic shader's stages: `38512498627` (a10), `38512507411` (c40), `38512515791` (b30), `38512524389` (b40), `38512533453` (d20).

Earlier rounds, kept for the record:
- `38510716909` and `38510726734`: modulate and add only. They showed cryo glass staying at `20479797` doubled.
- `38511462860`-`38511502053`: before the white-pinned guard. They gave the needler's ammo glow a flat white.

| Shader | b120 (means) | b121 | |
|---|---|---|---|
| c40 `teleport ring` | `ff243b49` | `ff6a8196` | 2.3x brighter; the column's gold |
| a10 `cryo glass` | `20469494` doubled | `20070a09` flat | LK16's splotches gone; the reflection's grey |
| a10 `monitor glass` | `ff9cb6ff` doubled | `ff151a1d` added | the map plus its reflection |
| b40/c40 `holo halo solar system` | band (map 2) `00141815` | `solar system` (map 0) `003b3820` | the planet |
| `holo halo inner ring` | band (map 1) `ff3b3736` | `halo inner ring` (map 0) `ff787566` | the ring |
| `holo halo outer ring` | band (map 1) `00151d19` | `halo outer ring` (map 0) `00777458` | the ring |
| `holo halo control` | `00e9e4ff` | `00757288` doubled | the same colour (×2 of half) |
| b30 `holo control` | `0078798d` doubled | `00787985` doubled | close |
| `jackal shield`, `over shield`, `generator shield` | `ff8080ff` d., `ffffffff` d., `ff8080ff` d. | unchanged | |
| `needler ammo glow` | `ff88dd88` doubled | unchanged | pinned at white: modulate kept |
| b30 `water simple` | `fffff8e8` doubled | `ff925046` added | bluer by the fit; looks the same (below) |
| c40 `index_hologram` | map 1, `ffffffff` doubled | map 0, `ff0a0708` added | the Index in Cortana's hand: green, not pink |
| a10 `sky\space` | star mask (map 0), `ff030303` | stars (map 1), `ff565655` | |
| d20 `ring night` | `ff514547` | `ff726667` | brighter (texels clip where the mean didn't) |

Tints that change, of the generic, chicago and water shaders each report covers:

| Map | Changed | Covered |
|---|---|---|
| a10 | 24 | 66 |
| c40 | 19 | 94 |
| b30 | 11 | 67 |
| b40 | 17 | 90 |
| d20 | 17 | 55 |

Most of the rest are within a few steps of 255: texels clip where a mean didn't, or a layered map is counted texel by texel.

The bake's own count, per map: tinted, doubled, added and flat, then the shaders drawn from another map than the scale rule's:

| Map | Tinted | Doubled | Added | Flat | Other map |
|---|---|---|---|---|---|
| a10 | 34 | 9 | 2 | 1 | 13 |
| c40 | 59 | 17 | 2 | 0 | 7 |
| b30 | 31 | 9 | 1 | 0 | 0 |
| b40 | 53 | 15 | 1 | 0 | 5 |
| d20 | 32 | 14 | 2 | 0 | 7 |
| bloodgulch | 21 | 8 | 1 | 0 | 2 |
| ui | 1 | 1 | 0 | 0 | 0 |

## Shots, old packs against new [E]

The bake: lab `sierra-bake` run `38512452151`, bundle `halo-psp-bake-tints121.tar.gz` (lab `bin` branch, beside the current one), into `/bake/tints121`.

The runs, all on one build (`1ecfb43`) with a shot every 5 s:
- lab batch `38513605175` (`tints121old`): campaign120's packs;
- lab batch `38521624178` (`tints121new`): tints121's packs.

The contact sheets are kept off the repos (they show the game):
- `b121_tints_a10.png`, `b121_tints_c40.png`, `b121_tints_b30.png`, `b121_tints_d20.png`;
- the c40 sheet has a third column, `drawnmapscale` on the new packs.

What they show:
- **a10 (LK16).** The bridge windows' green-yellow mottling (shots 50-65 s, 95 s) is gone. The windows are clear, with the stars and the planet behind them. Everything else in the opening is the same pixel for pixel (shots 5-15 s).
- **c40 (LK18).**
  - The column around the Chief (10 s) is a fuller gold.
  - 343 Guilty Spark has a blue halo ring (15 s), as on Sheepdog01's PC reference. The halo comes from the texel tints; it shows with `drawnmapscale` too.
  - The sphere behind Cortana (76 s) shows the planet, where b120 drew a flat pale disc.
  - The arc (96 s) shows the ring's art, where b120 drew a flat band.
  - The Index in Cortana's hand is green, not pink.
  - With `drawnmapscale`, the sphere and arc go back to flat, as b120.
- **b30.** The landing is the same. The far ocean (`water simple`, added now) is a shade bluer at 22 s and otherwise unchanged.
- **d20.** Unchanged by eye; a few hundred pixels differ in some frames.

The log line `textures: see-through tints for N shaders; drawn maps chosen by the bake for M` shows the tables read. On a10 the restorations line counts 1,800 flat draws per 300 frames (the cryo glass).

**Multiplayer.** The `PSP_MULTIPLAYER=1` build compiles (EBOOT `962c660232aaec82`) and reads the new Blood Gulch pack: 21 tints, 2 drawn maps. It then stops on the display list's 768 KB allocation (heap) and a SIGSEGV. That is not from this change: the same failure happens on `w121`'s own `274f08a` (lab batch `38509454132`, `w121a`) and on the other b121 branches since. b121 has to fix it either way.

**Bake time.** The texel fit adds about 10-25 s a map, inside the bake's own noise:

| Map | campaign120 | tints121 |
|---|---|---|
| a10 | 250 s | 257 s |
| c40 | 302 s | 276 s |
| b30 | 248 s | 199 s |
| b40 | 276 s | 275 s |
| d20 | 282 s | 291 s |
| bloodgulch | 120 s | 144 s |
| ui | 70 s | 85 s |

A fresh full bake stays about 68-70 minutes; campaign120's took 67.6 minutes for its 24 maps with s9 sound.

## For Sheepdog01

- **Look at, by eye:**
  - a10's windows: clear, and is the faint film right?
  - c40's control room: the planet in the sphere, the ring arc, Spark's halo, the column's gold.
- **Restorations, on by default.** Every change here moves the PSP's draw toward the shader's own colour as the Xbox's combiners compute it. Two cases are worth a look anyway:
  - **b30's ocean (`water simple`)** is now drawn added rather than doubled. That is 11× closer by the fit, and the shots show only a slightly bluer far ocean.
  - **The drawn map changed on shaders nobody flagged:** a10's command display and `poa engine exhaust` (LK17's thruster: it drew a scrolling band at 0.1 scale, which may be the shimmer [U]), the space sky's stars, d20's grav lift, Blood Gulch's `teleporter_cone` and `hilltop`. Each now draws the map that carries the shader, by its share. `drawnmapscale` shows b120's choice.
- **Not restored yet:**
  - `monitor glass` and the Longsword's `fb_monitor` are opaque on the Xbox: their alpha is the reflection cube's, 1. On the PSP their alpha is still the map's, about 0.18, as in every build. Fixing that needs the alpha drawn without the texel (`GU_TCC_RGB`), a model the two address bits have no room for.
  - The reflection itself is only its mean: the PSP still doesn't draw the cube.
- **Off switches:**
  - **`drawnmapscale`** (keep word): the smallest-scale map, with b121's tint for it.
  - **`--tints-from-means`** (texpack.py; bake.py in the repo, not in the bin bundle's older bake.py): the b120 tints, byte for byte.
  - **`notint`**: untinted, as before.

  A pack baked for b121 in a b120 build: the holograms' band would take the planet's tint. Packs and builds ship together.
