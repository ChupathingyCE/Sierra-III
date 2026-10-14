Part of the Halo CE PSP performance review checklist research

# Old PSP project sources: performance techniques (FBA4PSP, snes9xTYL, PSP Quake lineage)

Method: `git clone --depth 1` into the session scratchpad (`.../scratchpad/srcs/{fba,tyl,dqp,pspsdk}`), read with grep/sed only, nothing built or run. Claims marked **(inferred)** are my reading, not something the code states.

## 1. Sources and status

| Project | Repo / commit | Found? | License |
|---|---|---|---|
| FBA4PSP | https://github.com/PSP-Archive/FBA4PSP @ `df81472` (2013-01-16) | Yes | FB Alpha licence: non-commercial, see `readme.txt` / `burn.h` ("Refer to license.txt") |
| snes9xTYL (TYLme/TYLcm "Mod") | https://github.com/esmjanus/snes9xTYL @ `6ce09a0` (2018-04-04, branch `mecm`) | Yes. This is the only GitHub mirror I found. 14 `PSP-Archive/*` and user name guesses returned "not found" | Snes9x licence: **non-commercial, personal use** (`copyright.h`). ME stub is "from the melib package, (c) mrbrown" |
| snes9x-euphoria | none found | **No.** Not on GitHub under any name I tried (`PSP-Archive/Snes9x_Euphoria` and others). GitHub search API is blocked in this session | n/a |
| Peter Mackay PSP Quake / Kurok | none found directly | **No direct mirror.** I used the descendant **https://github.com/nzp-team/dquakeplus @ `b1a2823`** (2026-10-12) instead. This is the NZ:P "Vril" engine, lineage Mackay PSP Quake → Kurok/Crow_bar dQuake → dQuakePlus. Several files still carry "Copyright (C) 2007 Peter Mackay and Chris Swindle", including `clipping.cpp`, `gu_mesh.cpp` and the `vram` allocator | GPL v2+ |
| pspsdk (reference only) | https://github.com/pspdev/pspsdk @ `244f889` | Yes | BSD |

Caveat for dquakeplus: the repo is under active development in 2026, so some techniques below are modern NZ:P work, not 2007 Quake. I note where provenance is clear.

## 2. Findings by topic

### 2.1 Media Engine (snes9xTYL only; FBA4PSP and dquakeplus do not use the ME)

- **Boot.** A kernel PRX (`psp/mediaengineprx/main.c`, `PSP_MODULE_INFO("MediaEngine", 0x1006, ...)`) memcpy's a stub to `0xbfc00040`, then writes the entry function and its argument to `0xbfc00600`/`0xbfc00604`. It then calls `sceKernelDcacheWritebackAll()` and `sceSysregMeResetEnable → MeBusClockEnable → MeResetDisable`, with firmware-371-specific NIDs for the sysreg calls.
- **Stub** (`psp/mediaengineprx/me_stub.S`):
  - writes 7 to `0xbc100050`
  - zeroes TagLo/TagHi, then runs `cache 0x01` (I-cache index store tag) and `cache 0x11` (D-cache index store tag) over 8192 bytes in 64-byte steps
  - writes `-1` to `0xbc000000..0xbc00002c` **(inferred: opens the memory-protection registers)**
  - sets `STATUS=0x20000000` and `sp=0x80200000` (top of the 2 MB ME eDRAM) and jumps to the loader's `me_loop`
- **Job model** (`me_struct`, `mediaengineprx/me.c`): a single mailbox with `start`, `done`, `func`, `param`, `result`, `precache_len/addr`, `postcache_len/addr`. Both sides access it through an **uncached pointer** (`malloc_64(...) | 0x40000000`, `psp.cpp:2652-2659`).
  - `BeginME` is async. `CallME` and `WaitME` busy-wait on `done`.
  - The ME loop busy-waits on `start`: `while (mei->start == 0);` (`mediaengineprx/main.c` `me_loop`).
- **Waiting is pure spinning on both sides.** No `halt`, no `0xBC100044`, no `sceSysregInterruptToOther` anywhere. The main-side wait was once `sceKernelDelayThread(1000)`, but that line is commented out in `psp/me/me.c:me_wait`.
- **ME-side cache handling** (`mediaengineprx/main.c`), using `__builtin_allegrex_cache`:
  - before the job: `0x19` (hit-invalidate D) over a range, or an all-lines index invalidate (`0x13` + store-tag `0x11`) when `precache_len == -1`
  - after the job: `0x1b` (hit writeback-invalidate D) over a range, or `0x14` (index writeback-invalidate D) over 0..8191 when `postcache_len == -1`
  - The older `psp/me/me.c` version unrolls `cache 20, N($0)` for N = 0..8128 step 64, **issuing each line twice**. **(inferred: 16 KB / 2-way = 128 sets × 64 B, so an 8 KB index sweep hit twice covers both ways.)**
- **What gets offloaded:** the whole SNES APU (SPC700 core plus DSP mixing) runs on the ME as a free-running loop (`static int me_MixSound(me_sound_t *p)`, `psp.cpp:~978`).
  - Shared emulator state is copied to uncached aliases before start: `memcpy(UNCACHE_PTR(IAPU.RAM), ..., 0x10000)`, `psp.cpp:1094-1097`.
  - Cross-core counters are read and written only through uncached macros: `#define apu_cycles_left (((SAPUEVENTS*)UNCACHE_PTR(&stAPUEvents))->apu_cycles_left)`, `apu.h:257-266`.
  - Sound buffers are handed to the ME as `SoundBuffer[i] | 0x40000000`.
  - The main thread uses `sceAudioOutputPannedBlocking` and signals the ME with an uncached `mixsample_flag`.
  - A dead-code `me_csc` (YUV→RGB colour conversion) splits a frame in half between the ME and the main CPU (`psp/me/me_csc.c`). That is the "split the same job across both cores" pattern.
- **Coherency ritual before every start:** main CPU `sceKernelDcacheWritebackInvalidateAll()`, then `CallME(me_Dummy, prelen=-1)` purely to make the ME invalidate its own D-cache ("all we want is the ME to inv its dcache", `psp.cpp:1104`). Only after that does it run `BeginME(me_MixSound, ...)`.
- **Suspend/firmware handling.** The PRX unregisters the `SceMeRpc` sysevent handler, which would otherwise hang on suspend. `ResetME` pokes per-firmware offsets inside `sceMeCodecWrapper` (3.52 to 6.60 tables). This is fragile and firmware-specific.

### 2.2 Cache management

- **TYL, software render path** (`gfx.cpp:2390-2440`):
  - flips the framebuffer pointer between cached and uncached with `CACHE_ON`/`CACHE_OFF` macros
  - renders cached, copies rows into uncached VRAM (`0x44000000 + ...`), then does one `sceKernelDcacheWritebackAll()` per frame
  - The GE path writes the CLUT through `NO_CPU_CACHE(&clut256[0])`, so no flush is needed (`gfx_psp.cpp:257`).
- **TYL whole-cache flushes** are used liberally: 6× `WritebackInvalidateAll` in `psp.cpp`, all at ME start/stop and state changes, **not per job**. No `*Range` calls in TYL.
- **dquakeplus is range-only and narrowly scoped.** It has 6 `sceKernelDcacheWritebackRange` calls in total, all in `gu_psp.cpp` and `gu_hyena.cpp`, and all at publish points:
  - after texture upload (`gu_hyena.cpp:839`)
  - for the palette/CLUT (`:828`, `:1156`, `gu_psp.cpp:130`)
  - for static vertex buffers (`Hyena_UpdateBuffer :924`) and on buffer unmap (`:1141`, comment: "GU reads RAM asynchronously; publish writes before submitting the buffer")
  - There are **zero** `WritebackAll`/`InvalidateAll` calls.
- **pspsdk detail that matters:** `sceGuStart` ORs the list pointer with `0x40000000` (`pspsdk/src/gu/sceGuStart.c`: `local_list = list | 0x40000000`). So **all `sceGuGetMemory` writes, including per-frame vertices, are uncached stores** and need no flush. dquakeplus still aligns the list to 64 bytes "so it doesn't share a cache line with anything" (`gu_psp.cpp:65-67`, 512 KB list).
- **VFPU memcpy with an uncached destination** (`zone.c:51-260`, `memcpy_vfpu`):
  - writes through `dst | 0x40000000` using `sv.q ..., wb` (VFPU write buffer)
  - does `cache 0x1B` (hit writeback-invalidate) on each destination line first, so no stale cached copy survives; `sync` precedes the stores; `vflush` comes at the end
  - Per its comment, the reason is to "free cpu cache for src only". Used for loads and level-data copies (`gu_model.cpp:490,679,719,773`).

### 2.3 GE usage

- **dquakeplus (closest Halo analog)**
  - Display list:
    - one `GU_DIRECT` list per frame
    - `GL_EndRendering` = `sceGuFinish(); sceGuSync(0,0);`, then an optional vblank, `sceGuSwapBuffers()`, and an immediate `sceGuStart` (`gu_psp.cpp:280-296`)
    - **The CPU waits for the GE every frame**, so there is no CPU/GE frame overlap.
    - No `sceGuCallList` and no prebuilt world lists.
  - Framebuffer: `GU_PSM_5650`, stride 512, 16-bit depth, all three buffers in VRAM via a simple 1 KB-block first-fit allocator (`vram.cpp`, Mackay).
  - **World polys:** static `glvert_t` (UV float + XYZ float, 20 B) built at load.
    - Unclipped polys are drawn **straight from main-RAM `poly->verts`**.
    - Only clipped output is copied into `sceGuGetMemory` (`gu_surface.cpp:377-410, 683-712`).
    - **(Inferred risk:** I found no explicit writeback of `poly->verts` after load.)
    - Surfaces are batched by texture chains and lightmap chains (`r_world.c:645-846`). Binds are deduplicated with `if (currenttexture == texture_index) return;` (`gu_hyena.cpp:620`).
  - **Alias models:**
    - **GE hardware morphing.** `GU_VERTICES(2)` + `sceGuMorphWeight(0, 1-blend)` / `(1, blend)`.
    - **Compressed vertices:** `GU_TEXTURE_16BIT | GU_VERTEX_8BIT` (8 B/vertex).
    - Static poses are drawn directly from prebuilt packed buffers with no CPU vertex work (`r_mdl.c:74-115`, `gu_hyena.cpp:973-1030`).
  - **Textures** (`gu_hyena.cpp:750-845`):
    - **VRAM first, falling back to RAM** if `vramalloc` fails (`texture.vram ? texture.vram : texture.ram`)
    - **swizzled** (`Hyena_TilePixels(..., 16, 8)`) for all non-DXT formats, `swizzle = format < GU_PSM_DXT1`
    - formats: `T4` (per-texture 16-entry CLUT), `T8`, 4444, 8888, DXT1/3/5
    - lightmaps as `T8` through a luminance CLUT (`d_8to24tableLM`)
    - mip chains with manual `sceGuTexSlope`/`sceGuTexLevelMode`
    - textures on the 32 MB "phat" model are capped at 128 px for RGBA
  - **Large-poly clipping (Mackay/Swindle 2007, refined by "shpuld", `clipping.cpp`):** the GE culls any vertex beyond the 4096² virtual coordinate space, so polys near the edge must be CPU-clipped.
    - Trick: test against a **wide frustum** (`is_clipping_required` uses `vhtfm4.q` against wide planes in VFPU regs M500, early exit with `bvt`), but **clip against the exact frustum** (`clip_to_plane`, Sutherland–Hodgman in VFPU asm, two 32-vertex ping-pong buffers).
    - Only left/right/top/bottom planes are clipped (`CLIP_NEAR 0`, `CLIP_FAR 0`). The hardware handles near/far with `GU_CLIP_PLANES`.
    - The comment says the number of polys actually clipped "ends up being extremely low".
- **snes9xTYL:** SNES tiles are drawn as `GU_SPRITES` with `GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D` from `T8` CLUT textures. Vertices are batched per priority/bitshift into one `sceGuDrawArray` each, the depth test resolves priority, and the alpha test handles transparency (`gfx_psp.cpp:245-275, 441, 616`). Texture sources live in VRAM (`0x44000000 + offset`). `GU_PSM_5551` is used throughout.
- **FBA4PSP:** the CPU renders directly into **uncached VRAM** (`pBurnDraw = frame | 0x44000000`, `main.cpp:41,151`). The GE does one `GU_SPRITES` blit with a 5650 texture straight from VRAM (`gui.cpp:136-175`). It has no 3D content.

### 2.4 VFPU usage

- All of it is inline asm in dquakeplus, using `lv.q`/`sv.q`, `vdot.q`, `vrsq.s`, `vhtfm4.q`, `vcmp` + `bvt`, and `vcmovt`:
  - frustum extraction (`calculate_frustum`)
  - clipping
  - `BoxOnPlaneSide` (`mathlib.c:250+`)
  - `rsqrt`
  - sin/cos via `vfpu_sinf`/`vfpu_cosf`/`vfpu_atan2f` from libpspmath
  - `memcpy_vfpu`
  - `-lpspgum_vfpu`
- **Persistent VFPU registers across calls:** C500 (wide frustum), C600 (zero constant) and C700 (exact frustum) are loaded once per frame or brush model and "reserved for clipping use" (`clipping.cpp begin_frame`). This only works if nothing else touches those registers. Threads are created with `PSP_THREAD_ATTR_VFPU` (`module.cpp:28`, `thread.c:60`).
- Collision (`world.c` `SV_HullPointContents`, `SV_RecursiveHullCheck`) is stock scalar Quake with the axial-plane fast path (`plane->type < 3`). The VFPU include is commented out.
- TYL uses `__builtin_allegrex_sqrt_s`, `round_w_s` and `floor_w_s` (`psp/math.c:68-94`), with no VFPU.

### 2.5 Memory tricks

- **Scratchpad (`0x00010000`): not used by any of the three.**
- TYL has `malloc_64` (round the size up to 64, `memalign(64)`), so that cross-core objects never share a line (`psp/me/mem64.c`).
- dquakeplus:
  - `PSP_HEAP_SIZE_KB(-1024)`, then sizes its zone at 13 MB on the phat model and 30 MB on the slim (`main.cpp:66-67`)
  - aligns clip buffers and frustum data with `__attribute__((aligned(16)))` for `lv.q`
  - copies big lumps with `memcpy_vfpu`
- FBA4PSP uses generated, specialised tile/sprite renderers (`src/generated/*_func_table.h`). That is the burn core's general technique, not PSP-specific.

### 2.6 Compiler flags

- **FBA4PSP** (`makefile.psp:198`): `-O2 -G0`. Aggressive flags and `-march` are commented out. `PSP_FW_VERSION=200`.
- **snes9xTYL** (`makefile_multi:11-15`):
  - `-march=allegrex -O2 -G0 -pipe -fsingle-precision-constant -fpredictive-commoning -ffunction-sections -frename-registers -fno-gcse -finline-functions -finline-limit=20000`
  - **`-falign-functions=64 -falign-loops=64`** (I-cache line), `-fno-align-jumps -fno-align-labels`
  - `LDFLAGS = -fwhole-program`
- **dquakeplus** (`Makefile.psp:151`): `-ffast-math -O3 -Ofast -G0 -Wno-strict-aliasing -DPSP_VFPU`. `BUILD_PRX=1`, `PSP_LARGE_MEMORY=1`, optional `-pg` gprof build (`GPROF_FLAGS`).
- All three use `-G0`. None uses `-fno-strict-aliasing` (dquakeplus only silences the warning).

## 3. What to copy / what not to copy

**Copy (as technique, not code; TYL and FBA licences are non-commercial and dquakeplus is GPL):**
1. **Wide-test / exact-clip frustum scheme** for big BSP polys. Clip only L/R/T/B, and let `GU_CLIP_PLANES` handle near/far. This maps directly onto Halo's large BSP surfaces.
2. **Draw static geometry straight from RAM.** Copy into `sceGuGetMemory` only when vertices are generated per frame. Publish static buffers once with `WritebackRange`.
3. **GE morphing plus 8/16-bit vertex formats** for skinned or animated meshes. This cuts vertex bandwidth by 2–3× and removes CPU lerp.
4. **VRAM-first, swizzled, CLUT-heavy (T4/T8) textures**, with RAM fallback and deduplicated binds. Sort by texture and lightmap chains.
5. **Range-scoped writebacks at publish points only.** Never call `WritebackAll` per frame. Rely on the fact that pspsdk lists are already uncached.
6. **ME mailbox design:** a 64-byte-aligned, uncached mailbox; explicit pre/post cache ranges per job; shared counters accessed only through uncached aliases. The offloaded job should be **a self-contained, long-running loop over its own data** (TYL: the whole APU), not fine-grained calls.
7. **`-falign-functions=64 -falign-loops=64`** and `-ffunction-sections` are worth A/B testing for the I-cache.
8. **`memcpy_vfpu` pattern** (uncached-destination `sv.q wb` after `cache 0x1B`) for bulk copies whose destination will not be re-read soon. It avoids evicting the hot working set.

**Do not copy:**
- the per-frame `sceGuFinish → sceGuSync(0,0) → swap` serialisation in dquakeplus. Use double-buffered lists so the CPU builds frame N+1 while the GE draws frame N.
- TYL's `WritebackInvalidateAll` + dummy-ME-job ritual and the per-firmware `sceMeCodecWrapper` offsets in `ResetME`. They are fragile and expensive.
- busy-spin waits on the main CPU (`WaitME`), which burn the main core.
- reserving VFPU registers globally unless the whole codebase is audited for them.
- `-Ofast` across all code without checking float semantics (Halo physics and collision).

## 4. Answers to the open questions

- **(a) Prefetch / hit-under-miss: no evidence in any of the three.** No `pref` instruction, no `__builtin_prefetch`, no prefetch-style `cache` op (grep for `pref`, `cache 0x1e`/`30` returned nothing). The cache ops used are only maintenance ops: 0x01, 0x11, 0x13, 0x14, 0x19, 0x1B, plus 20 = 0x14. Nothing in the code relies on or comments on hit-under-miss. The nearest thing is `memcpy_vfpu`'s comment that address updates "get hidden in sv.q latency", which is about store latency, not misses.
- **(b) ME woken by interrupt: no.** TYL spins on uncached `start`/`done` flags on both cores. `0xBC100044`, `sceSysregInterruptToOther` and `halt` do not appear in any of the three repos. The only `0xbc1000xx` access is the stub's write of 7 to `0xbc100050` during boot **(inferred: clock/bus enable)**.
- **(c) Allegrex min/max/movz/movn from GCC: not observable.** None of the repos ship compiler output, and **no hand-written asm uses `min`/`max`**.
  - FBA's (unused) MIPS 68k core generator `fba_make68k.c` emits `.set arch=allegrex` plus `movz`/`movn` (76 in `a68k.s`), `seb`/`seh` (~1230), and `clz`/`clo`. However, `makefile.psp` builds the C core (`EMU_C68K`, `a68k.o` commented out at line 154).
  - TYL uses `ins` (mips32r2) in `pg.h:180`.
  - Whether GCC emits min/max needs a disassembly of our own build: `psp-objdump -d | grep -E '\b(min|max|movz|movn)\b'`.
