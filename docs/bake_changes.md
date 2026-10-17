# The new bake (go_live 3.3): numpy, Pillow and shapely only

From build 139 the bake needs three Python libraries, and any Python from 3.9 to 3.14. Two it used to need are gone: scikit-learn (the palettes) and Triangle (the wall decals' cuts). Triangle has no package for Python 3.14 and its source no longer builds there, which shut out current Arch and CachyOS systems; scikit-learn was a large dependency for one function. Labels: [M] PSP, [E] emulator or measured on the bake, [D] code, [U] inference.

**Re-bake needed:** yes. The packs (`.ptex`) and the decal files (`.pstn`) come out different byte for byte, though they draw the same (below). A build from 139 on works with a bake from the new bundle; a bake from an older bundle still loads, but the release names the bundle it was checked with (go_live 3.4 adds the check).

## What changed

### Palettes: a numpy k-means (`bake/lib/bitmap_bake.py`, `kmeans()`)

Each 8-bit and 4-bit texture's palette is the centres of a k-means over its colours (RGBA where the alpha holds data, RGB otherwise); the texture quality step's (`--quality 2`, off by default) is in Oklab.

- **Before:** scikit-learn's `KMeans(n_init=1, random_state=0)`, 25 rounds for the plain palettes, 60 for the quality step.
- **Now:** `kmeans()`: the same method, written in numpy. It starts as scikit-learn does (k-means++, each next centre the best of 2 + log(k) picks weighted by squared distance), then runs Lloyd's rounds until nothing moves, at most 60. A centre left with no colours takes the farthest one, so every entry stays in use. The nearest centres are found in 32-bit chunks. Seeded, so a bake is the same on every machine.
- **Why the palettes differ at all:** two k-means runs differ by where they start, and scikit-learn's random sequence isn't numpy's. That alone moves a single texture's error by a few percent either way: scikit-learn against itself with another seed does the same.

### Wall decals: faces, then ear clipping (`tools/stencilbake.py`, `pslg_triangulate()`)

A wall triangle under a decal is cut along the decal's outline, so the part under the decal is its own triangles (`stencilbake.py`'s header says why).

- **Before:** Triangle's constrained Delaunay triangulation of the outlines and the triangle's edges.
- **Now:** the outlines are noded (shapely's `unary_union`), vertices lying a hair off a segment are snapped onto it (as Triangle's exact arithmetic split them), and `polygonize` finds the faces. Each face is cut into triangles by ear clipping, its holes joined in by cuts to the nearest outside vertex that sees them.
- Every vertex on a face's edge is a corner of its triangles, so neighbouring pieces meet without T-junctions, as Triangle's did. The triangles themselves differ (ear clipping makes thinner ones than Delaunay), but each face, and so each decal layer, covers exactly the same area.

### The launcher and the README

`halo-psp-bake` installs numpy, Pillow and shapely, and takes Python 3.14. `bake/README.md` no longer sends 3.14 users to uv.

## The proof

`--compare-old` (the lab's `sierra-bake compare=yes`) makes each palette and each decal cut both ways in the same bake, with scikit-learn and Triangle installed for that alone, and logs:

- `kmeans compare summary`: the mean Oklab error old and new over every palette, and how many came out worse by more than 1% and 3%; `kmeans compare worse`: the worst eight by name. Outliers go to side-by-sides.
- The decals' `compare:` counts: how many source triangles were compared, how many layers covered a different area (more than a millionth of the triangle), the largest difference, and where Triangle failed and the faces didn't.

Before the disc [E, synthetic]:
- 48 test palettes (16 and 256 colours): the mean error 0.4% lower than scikit-learn's; single textures up to ~4% either way.
- 1,200 random decal layouts: every layer's area the same to a millionth of the triangle wherever Triangle succeeded, and three it failed on ("topological inconsistency") cut correctly.

On the disc: the proof bake `newbake138` (a10, b30, c20, d20, ui), then emulator shots of b30's and c20's decals against b136's bake. Results to follow here.
