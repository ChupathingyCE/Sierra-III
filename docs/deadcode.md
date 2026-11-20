# The dead-code pass: code no level needs, out of the link (2026-11-15)

Every KB of program is a KB of heap: the heap is sized from the program's end, and memory is short on the heavy maps. This page says what each class of never-run code is, what takes it out, why it can't run in a campaign build, and what it gave.

Labels: [E] emulator, [D] read in the code or the link map, [U] inference.

## Where it started

- **The census:** the lab's coverage1 batch (`coverage1_code_never_run`; the tock d6849f3 built plain): 740 of 2,963 KB of code ran in none of b30_leg1 and a30_play1 (900 s each) and a ui.map menu walk [E]. By name, four classes no level needs: debug/describe/dump/test 113 KB, the rasterizer's Xbox draw paths 102, the C library 56, the PC port layer 42.
- **"Never ran" is not "dead":** two levels' first 15 minutes, on a fresh stick. Each cut below is read in the code as well.
- **Method:** the link already has `-ffunction-sections` and `--gc-sections`, so a never-run function is still linked only because something that runs refers to it. A cut is made at that reference: a call made provably dead, so the compiler drops it and the linker drops the rest. To find the references, the link map and the object files' relocations (`tools/coverage.py`'s reader) were made into a call graph, with every function that ran as a root; a never-run function's dominator subtree is what one cut there frees.
- **Earlier tries on the ground** (the lab's `projects/sierra/tools/levercheck.py --ground code-size`): COLDSINKS (cold sinks, parked: the toolchain can't link split functions), PSPRELEASE (the port's instruments out of hot code, parked: 4 KB), FRAMEPTR, RH5, DBLSOFT, STRICTALIAS, TUNE, ENUM32 (code generation, not removal). None takes unreachable code out. The b107/b125 pass (`dead_code_inventory.md`) took out multiplayer (group 1) and the GL shims (group 4) and left group 2, the debug tools, as "the same technique as group 4".

## Class 1: the game's developer tools (`PSP_DEADDEBUG`, lever DEADDEBUG)

**What:** ChupathingyCE's own console switch `HALO_CONSOLE_NO_DEBUG_TOOLS` (it's for a console port's build without developer tools), given to every unit of the game. It leaves out:
- the AI's debug drawing and records (`ai_debug.c`: `ai_debug_render_actor` alone was 29 KB);
- the render-debug cache and its shapes (`render_debug.c`), the collision's debug drawing (`collision_debug.c`), the texture and sound caches' displays;
- the profiler (`profile.c`) with its frame marks and every section's enter and exit, and its graphs (`interface.c`);
- the collision log's counting and timing (`collision_usage.c`; the calls become empty macros).

**Why it can't change the game:** all of it only draws a debug display or keeps figures for one. Nothing the game computes reads it. Two things ran in play and go:
- the profiler's frame marks and section enters;
- the debug renders' flag tests (`render_debug`, `ai_debug_render`, `collision_debug_render`).

They ran only to find their flags off. The hs commands that reach the tools stay in the script table, so the scripts' function indices don't move. They answer "not in this build" through `console_debug_tools_unavailable`, which psp_platform.c defines (the console's platform layer would). `psp_collision_report`, FIXES_128's heartbeat report, is defined empty there too: the log it reads is gone.

**When it's off:** `PSP_DEADDEBUG=0`, or any `PSP_DEBUG_TOOLS=1` build (its collision log by user needs the log).

**Proof** [E]:
- The lab's sierra-batch deadcode1 compared the tock e6a9641 with feaab31 under the shipped switches (LTO, profile campaign-rb152p, layouts).
- Play and both seeds were identical at every tick: b30_leg1 4,500 ticks, c20_leg1 9,000. The ui.map menu walk was ok on both builds.
- The whole-state hash differs from tick 0, in allocations that hold heap addresses (the decal vertex cache, objects, particles, looping sounds, the HUD's unit interface). Every address above the program moved by 216 KB [U].
- `PSP_DEADDEBUG=0` builds the tock's EBOOT byte for byte (plain local build).
- `PSP_DEBUG_TOOLS=1`, `PSP_RECON=1` and `PSP_METRICS=0` compile and link with no new stubbed names.

**What it gave** (the shipped recipe's build logs) [D]:
- The program went from 5.31 to 5.10 MB: `.text` -117.7 KB, `.data` -67.2 KB (the profiler's per-section records), `.bss` -29.2 KB.
- **The heap went from 5,568 to 5,784 KB (+216 KB).**
- Its low point rose [E]: b30_leg1 87 -> 296 KB free (largest block 54 -> 250), c20_leg1 33 -> 244 KB.

**Not measured:** the tick. The profiler's section enters and the collision log's counters sat in hot code. Leaving them out can only shorten the tick, but a change that runs through the caches is priced on the PSP (lab CLAUDE.md). That price is a PSP pair someone may ask for. It isn't claimed here.

**Left in class 1** (~25 KB, each needing its own read): the object types' `render_debug` functions, kept by the type tables' pointers (`object_render_debug` 3.2 KB, `unit_` 0.7, `device_` 0.6, `biped_` 0.4); the editor's flying camera (3 KB, `director_choose_camera`); `rasterizer_debug_model_vertices` (1.9); the console's tab completion (1.1); the frame statistics. The port's own tests behind keep words (`psp_me_batch_test`, `ge_precision_test`, `psp_objects_play_dump`) are the lab's instruments and stay. Error paths that dump to a file (`lruv_debug_to_file`, `scenario_debug_to_file`) stay: they run when a request fails.

## Class 2: the Xbox rasterizer's draw paths (`PSP_DEADXBOX`, lever DEADXBOX)

**Slice 1, the level's passes:**
- skipped_passes (psp_fixups.py) returns at the top of eight passes once the GE is active: the level's diffuse texture, specular lightmap, reflection mask, reflection, fog and fog-screen passes, the detail objects' pass and `lights_render_specular`.
- The GE becomes active at the first window's start (psp_ge.c, `window_begin_main`). These passes are only called inside a window, so the test can't fail.
- Under the switch it is a constant, and the Xbox code behind those returns leaves the link.
- The transparent draw's gate is made constant as well. Its keep word `transparent` ran the Xbox path into the null device, which draws nothing (gl_never). Under the switch the word does nothing.
- Nothing that ran is removed: the returns stay.
- Plain local build: `.text` -33 KB over class 1, heap 5,796 -> 5,832 KB. `PSP_DEADXBOX=0` builds class 1's EBOOT byte for byte.
- **Proof** [E]: lab sierra-batch deadcode2 compared the tock e6a9641 with f9344e0 (class 1 + this slice) on the same pads. Play and both seeds were identical at every tick (b30 4,500, c20 9,000), and the menu walk was ok.
- **What it gave** (the shipped recipe) [D]: the program is 5.07 MB (`.text` -32.6 KB over class 1) and **the heap 5,816 KB: +248 KB against the tock for the two together**. The low point went 87 -> 318 KB free on b30 and 33 -> 268 KB on c20 [E].

**Left in class 2:**
- **`rasterizer_transparent_geometry_group_draw` (31 KB)** stays reachable. Water drawn before the fog (`_rasterizer_environment_transparent_geometry_submit`) and models' no-queue groups (`_rasterizer_model_transparent_geometry_submit`) draw a group at once. Whether any map's tags take those branches on the PSP is the next read.
- Also left: the lightmap and shadow draws (4 and 3 KB), the pseudo-dynamic screen quads, the motion sensor's blip end, the screen flash. Each has a caller that runs and a gate to read.
- `psp_hud_multiply` (3 KB, the port's own) never ran either.

## Class 3: the PC port layer (not cut)

The census's 42 KB here is mostly not dead:
- **The TOML parser (17 KB) and the config writer** read and write `config.toml` on a real stick (audio, network, interpolation: `chupa_rebase.md`). Every emulator run starts on a fresh stick with no config.toml, so they never run there. This is a blind spot of the coverage, not dead code.
- The Xbox save-game calls (`XCreateSaveGame` and the rest) are player-profile saves.
- The file calls (`FindFirstFileA`, `WriteFileEx`) are error and save paths.
- The render interpolation's blends run at frame rates the two pads didn't reach.
- What looks dead (`nv2a_vertex_shader_lighting` 1.7 KB, `render_targets_evict` 1.0) is ~3 KB. That isn't worth a switch.
- `halo_linux_vfprintf` (13.9 KB, FILE printf) belongs to the C library class (miniscanf's).
