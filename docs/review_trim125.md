# Review: the dead-weight pass (branch `trim125`)

Sheepdog01, 2026-10-14: the PC graphics shims "should not be in here"; make a pass for the rest of the dead weight. Branch `trim125` from `w125` (`d18a1d0`), two commits. Both are exact: they remove code that never runs, so there is no keep word (nothing changes at run time); the off side is `w125` itself.

Labels: [M] PSP, [E] emulator, [D] code or link map, [U] inference. Sizes are from builds made with `tools/build_release.sh` (no PGO), `w125` against `trim125`.

## 1. The result

| | `w125` | `trim125` | change |
|---|---:|---:|---:|
| Program | 4.86 MB | 4.75 MB | **−111 KB** |
| text (code and read-only data) | 3,460,320 | 3,384,056 | −76,264 |
| bss | 1,458,696 | 1,421,468 | −37,228 |
| **Heap** | 5,632 KB | 5,740 KB | **+108 KB** |
| Functions linked | | | 85 fewer, none added |

The heap gains what the program loses, because `psp_build.py` sizes the heap from the program's end [D].

## 2. Commit 1: halo-ce-universal retired, and the bring-up harness

halo-ce-universal stopped being a base on 2026-10-13 (`UPSTREAM.md`); what was left of it goes:
- **`psp_fixups.py`:** the 73 `Either(universal=…, chupa=…)` pairs become their ChupathingyCE halves; the 5 that were universal-only go. The `Either` class and the base detection (`BASE`) go with them. Done mechanically from the parsed source, not by hand.
- **`psp_build.py`:** the `CHUPA` switch and `-DPSP_CHUPATHINGYCE` (always on). `mirror()` moves in from `build_var.py`, the first link harness it was borrowed from.
- **`psp_limits.h`, `psp_platform.c`, `psp_campaign.c`:** the `#ifdef PSP_CHUPATHINGYCE` branches keep their ChupathingyCE side.
- **`tools/build_release.sh`, `upstream/pins.env`:** `ENGINE=universal`, its pin and its two patches.
- **Deleted, unreferenced:** `build.py`, `build_var.py`, `link/` (the first link harness); `try_port.py` and `run_interp.sh` (fixed `/tmp` paths, superseded by `psp_build.py` and `tools/emu.sh`); `psp/crash/main_b101.c.txt` (an old copy); `tools/stencil_survey*.py` (one-off surveys).

**Evidence** [D]: the mirrored, fixed-up tree is byte-identical to `w125`'s, and so is `EBOOT.PBP` (`24de43213bea686f` both). The builds made on universal stay reproducible from their own commits, which still carry the pin and patches.

## 3. Commit 2: the PC renderer's GL path out of the link

**Why it was still there** [D]. ChupathingyCE draws by translating the game's Direct3D 8 calls into OpenGL (`d3d8_gl.c`; `gl_functions.c` loads the GL entry points; `nv2a_*.c` translate the Xbox shaders). On the PSP that path never runs: `psp_sdl.c`'s `SDL_CreateWindow` always returns NULL, so `gl_initialize` is never called, `device.gl_ready` stays FALSE, and `psp_ge.c` draws instead. But the calls into the GL path are ordinary branches the compiler can't prove dead, so `--gc-sections` kept everything they reach. b107's inventory listed it as group 4 and left it as report only.

**The change** (`psp_fixups.py`, the `gl_never` pass):
- `d3d8_gl.c`: every test of `device.gl_ready` reads `DEVICE_GL_READY`, which is `FALSE` on the PSP (10 tests). The one `gl_initialize()` call is compiled out (the branch was never taken).
- `sdl_platform.c`: after `SDL_CreateWindow`'s failure test, the PSP returns FALSE. That code was never reached, and it held the only call to `gl_functions_load`.
- The pass checks its anchors and the count of tests, and stops the build if ChupathingyCE changes them.
- `psp_build.py`: `gl_functions`, `nv2a_psh`, `xgpu_text` and `xiso` leave the platform file list. Nothing of them linked any more; the EBOOT is identical with or without them in the list (`1e0594761c689ab8`).
- Four GL headers nothing includes go: `GLES2/gl2.h`, `GLES3/gl3.h`, `gl31.h`, `gl3ext.h`.

**What leaves the program** [D, link map]: `d3d8_gl.o` −56 KB (the translator, the occlusion queries, the render-target code), `xbox_textures.o` −30 KB (the GL texture cache), `nv2a_psh.o` −12 KB, `gl_functions.o` −7 KB, `nv2a_vsh.o` −6 KB, and −13 KB of merged strings (GL names, shader text).

**What stays, and why:** `d3d8_gl.o` keeps 17 KB, the Direct3D bookkeeping the game still calls (device, resources, render state). `nv2a_vsh.o` keeps 1.8 KB and `xbox_textures.o` 1.2 KB: the helpers b107's coverage saw run. The five remaining GL headers stay because `d3d8_gl.c` still compiles against GL's types, through `gl.h`. They put nothing in the program; taking them out would mean replacing `d3d8_gl.c` with a Direct3D layer of our own.

## 4. What to check

1. **The lab's exactness batch** (d20, a50, c20, both seeds) against `w125`: play identical. The removed code never ran, so only the code layout moves [U].
2. **The multiplayer build** compiles and links (`PSP_MULTIPLAYER=1`): done here. It stubs the same 10 symbols as `w125`'s multiplayer build (`delta_legacy_*`, `map_family_*`, `p2p_set_game_listing`, `log_address`, `pc_menus_screen`): already there before this branch, from desktop units the PSP leaves out, and parked with the multiplayer work.
3. **On the PSP-1000:** nothing new to look at. The heap's start-up line should read about 108 KB more than b124's.
