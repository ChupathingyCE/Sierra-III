# Dead code and oversized data: the inventory (2026-10-08, b107 dev tree)

It measures which of the program's code ran and which data only unrun code uses, groups the rest by what it's for, and says what cutting each group would take.

## Status at b125 (2026-10-14, `trim125`)

**Done:**
- Group 1 (multiplayer) and three data items, in b107 (below).
- **Group 4, the PC graphics shims: −111 KB of program, +108 KB of heap** (`review_trim125.md`). The fix wasn't leaving files out: the GL code was reachable through branches the compiler couldn't prove dead, so `psp_fixups.py`'s `gl_never` makes them provably dead.
- **Group 5 (zlib's inflate):** already gone; nothing of it is in b124's link map [D].
- **The retired engine base:** its fixup halves, build switch, pin, patches and the first link harness are out of the repo (exact: identical EBOOT).

**Still linked, with b124's sizes** (link map, no PGO) [D]. The coverage is b107's, so each needs a fresh read before it's cut:

| What | KB | What cutting takes | Risk |
|---|---:|---|---|
| **Group 2, debug and development tools:** `ai_debug.o` 61, `profile.o` 29, `render_debug.o` 18, `collision_debug.o` 7.5, `rasterizer_xbox_profile.o` 6.3, `editor_flying_camera.o` 5, `rasterizer_debug.o` 4.8, `debug_memory.o` 4.6, `ai_profile.o` 3.6, others 3.7 | **~144** | Most of it hangs off debug flags tested at run time: make them compile-time constants outside `PSP_DEBUG_TOOLS`, as `gl_never` does. Keep the console and `cheats.o`: the bench runner needs console commands | low–medium |
| **libc's scanf engine** (`vfscanf`, `svfscanf`, `svfiscanf`) | ~30 | Replace the one `sscanf` in `psp_sdl.c` and the `fscanf`s in `psp_tick_account` and the director's camera file with small parsers | low |
| `tiff_file.o`, `s3tc.o` | 3 | Leave the units out | low |
| **Group 6**, the Xbox rasterizer's replaced draw paths | ~150 (b107) | Function by function | medium |

**Data sized for the Xbox, still as it was** (b124 symbols) [D]. Each runs, so each needs its peak measured or a check mode before shrinking:

| What | KB | Campaign need |
|---|---:|---|
| Player update queues (`update_client_globals`, `update_server_globals`; `player_queues_new.o` 91 in all) | 83 | One player, a queue 1–2 deep [U] |
| `features` (ragdoll scratch) | 44 | Could share another scratch buffer [U] |
| Lens flares: `local_lens_flare_parameters`, `…_occlusion_test_results` | 70 | Peaks not measured |
| `compressed_monk_tone` (in the EBOOT's data too) | 18 | Never plays [E, b107]: only the Xbox's disc-to-hard-drive copy starts the loading screen. **Keep the sound, move it** (Sheepdog01, 2026-10-14): it becomes the rebuilt loading screen's, baked from the player's disc (`plan_loading_screen.md`) |

**Suggested next:** group 2 (the biggest, and the same technique as group 4), then the scanf engine. The data trims need a lab batch each.

**Since done in b107 (dev tree; b107_candidates.md items 8–9):**
- Group 1 is out: Sheepdog01 chose a campaign-only engine, with co-op and multiplayer as their own branch.
- Cut to the PSP's needs: the lens-flare marker table, the query slots and the vertex shader copies.
- **The program: 5.16 → 4.45 MB; the heap: 5,376 → 6,080 KB.**
- The other groups were left as report only (see the b125 status above).

**Headline: about 0.8 MB of the 5.16 MB program can come out of the campaign build, and 0.2–0.3 MB more of data is sized for the Xbox's limits.**
- The biggest part is the multiplayer and networking code and its buffers: about 0.55 MB. It runs in the campaign, but only to do nothing.
- The program sits below the heap, the game state and the tag window. Every byte it gives up can go to the window instead. The three maps that don't fit (a50 0.20 MiB over, b40 0.53, a30 0.57: campaign_window_fit.md) need less than what's here.
- None of this changes the tick (the networking code is under 0.02% of b30's time [E]). It's memory, not speed.

Labels: **[E]** measured in the emulator, **[D]** read in the code, **[U]** inference. Sizes come from the link map and are exact for b107f.

## How it was measured

- **Coverage:** the emulator records every block of code it compiles, which is every block that ran at least once. That's a local PPSSPP patch, `PSP_COVERAGE=<file>` (in `tools/ppsspp_local_patches.diff`).
- **Matching:** `tools/coverage.py` matches those blocks to functions through the link map. psp_build.py now writes `OUT/halo.map` on every link.
- **Data:** it reads each object file's relocations to see which functions use which variables. A variable counts as used when a function that ran refers to it, or a used variable does (pointer tables).
- **Runs (b107f):**

| run | what it covers |
|---|---|
| b30, Sheepdog01-style play input, deathless, 17,321 ticks | the Pelican drop, beach fight, inland |
| The Library, grenade input, 7,232 ticks | Flood, machines, lightning |
| ui.map with the menu input | little: the main menu's widgets never loaded (group 3) |
| The Library with `savetest` | checkpoint save and revert |
| Blood Gulch, fire input (the multiplayer kind) | an MP map, loaded the way we test them |

- **To reproduce:**
  1. Set `PSP_COVERAGE=/tmp/cov_x.txt` in the environment of `tools/emu.sh`.
  2. Run `python3 tools/coverage.py --map halo.map --elf halo.elf campaign:cov_b30.txt ... mp:cov_bg.txt`. `--who NAME` lists who refers to a function or variable.

**What "never ran" means:** not in these runs. Code for a map, vehicle, death or error path the runs didn't reach also shows as never run. So the groups below are by what the code is *for*, read in the code. Coverage alone isn't proof.

## The program, 5.16 MB

| | KB | ran | never ran / unused |
|---|---|---|---|
| code | 2,893 | 1,821 | 1,072 |
| rodata (strings ≈ 350 KB of it) | 474 | | |
| data | 177 | | |
| bss | 1,735 | | |
| data + rodata + bss | 2,386 | 2,059 used | 271 unused, 54 in libraries (unknown) |

## The groups

| group | code KB | data KB | what runs of it now | what cutting takes | risk |
|---|---|---|---|---|---|
| **1. Multiplayer and networking** (source/networking, source/bungie_net, port/linux/game/network_*, xnet.o, game_engine_ctf/race/oddball/king/slayer/stub/multiplayer_sounds) | 210 | 354 | 24 KB: per-tick calls that do next to nothing (under 0.02% of the time), listed below | a campaign flavour of the build that leaves these files out and answers the ~25 entry points with "no network game" | low [D] |
| **2. Debug and development tools** (ai_debug, render_debug, collision_debug, rasterizer debug, the game's profiler, editor and tool code, the lens-flare *builder*) | 110 | 63 | 25 KB: hooks that check a debug flag; the profiler's frame marks | `PSP_DEBUG_TOOLS` off already exists: extend it to these | low [D] |
| **3. The game's own menus** (ui_widget*, virtual keyboard, dialogs) | 91 | 53 | 7 KB: the widget update with nothing open | **mostly not dead.** Start opens the pause menu, a widget (psp_sdl.c maps Start for it), and no run pressed Start. Only the multiplayer screens (the pre-game lobby, player lists in ui_widget_game_data_input_functions.c) go, with group 1 | medium [D] |
| **4. The PC graphics shims the PSP replaced** (GL loader, nv2a shader translators, the GL texture cache, TIFF and S3TC) | 31 | 30 | 1 KB: three texture-size helpers that stay | leave the files out; keep the helpers | low [D] |
| **5. Compressed maps** (zlib's inflate) | 15 | 6 | none: the bake writes maps uncompressed | refuse a compressed map with a message | low [D] |
| **6. The Xbox rasterizer's draw paths** (functions in rasterizer/xbox that never ran: the environment, fog, water and transparent-geometry draws psp_ge replaced) | 135 | 18 | the rest of those files does run | function by function | medium: needs each one read |
| everything else that never ran | 521 | 112 | | **keep**: game features these runs didn't reach (below) | |

**Groups 1, 2, 4 and 5 come to about 0.8 MB.** Group 6 is another 0.15 MB of slower work. Group 3 stays, apart from its multiplayer screens.

### Group 1's seams: what the campaign calls into multiplayer [E]

Each tick or each game:
- `network_distributed_tick`, `network_distributed_new_game`, `distributed_batch_flush`
- `network_test_update`
- `network_damage_deals`, `network_damage_aftermath`, `network_damage_player_effect`, `network_damage_new_game`, `network_damage_replaying_kill` (from damage.c)
- `network_objects_new_game`, `network_objects_new_object_index`, `network_objects_may_delete`, `network_objects_placed`, `network_objects_creating_host_object` (from objects.c)
- `network_game_is_active`, `network_game_distributed_client`, `global_network_game_client_get`

At startup:
- `WSAStartup`, `XNetStartup` and the other XNet queries
- `telnet_console_initialize`, `telnet_console_process`
- `transport_initialize`, `create_mutex`, `initialize_network_game_packets`

Loading an MP map the way we test them (`map_name levels\test\...`) runs no game engine and no networking at all [E]. So the campaign build still loads MP maps for testing.

The data group 1 takes with it (bss, all only cleared at the start of a game in the campaign) [E]:

| variable | KB | file |
|---|---|---|
| damage_trails, damage_rate_cache, damage_history, damage_explosions, damage_reports, damage_events | 175 | network_damage.o |
| objects_host_inventories, objects_client_dead, objects_host_told, objects_client_has | 72 | network_objects.o |
| the rest (message buffers, client and server managers, strings) | ~107 | |

## Data sized for the Xbox (used, but far bigger than any map needs)

These run, so each needs a peak measured or a guard before shrinking. Every one would get a check mode like b107's.

| what | KB now | what it holds | what the maps need | KB after |
|---|---|---|---|---|
| lens flare results per BSP marker (`rasterizer_lights.c`) | 64 | one byte per possible marker index (65,536) | **most markers in any BSP: 4,616 (d40)**; a10 2,596, c20 733; read from all ten maps [D] | 8 |
| lens flares submitted per frame | 40 | 1,024 a frame | peak not measured | ~5 [U] |
| lens flare results per light | 30 | every light in the map × 8 flares | peak not measured | ? |
| the D3D shim's occlusion query slots (`d3d8_gl.c`, `device`) | 48 | 4,096 GL queries | **never used on the PSP** (its flares use `psp_ge_widget_occlusion`) [E] | ~0 |
| the player update queues (`player_queues_new.c`) | 82 | 128 client updates × 16 players' actions | campaign: 1 player, a queue 1–2 deep [U] | ~6 |
| `render.environment_surface_indices` | 64 | 16,384 surfaces a frame | peak not measured; whether psp_ge reads it to be checked | ? |
| vertex shader copies (`D3DDevice_CreateVertexShader`) | 34 heap | a malloc'd copy of every Xbox vertex shader's code | read only by the GL translator, which never runs [D] | 0 |
| `compressed_monk_tone` (progress bar sound) | 18 (.data: in the EBOOT too) | never plays [E] | | 0 |
| `features` (limp-noodle ragdoll scratch) | 44 | one function's scratch | could share another scratch buffer [U] | ? |

Lens flares and the query slots alone are about 140 KB, and both are cheap.

## What the runs didn't reach (keep, and test these paths before trusting a cut near them)

These all never ran in these runs but are campaign features:
- The dead camera (no player death: deathless), the following and orbiting cameras.
- The Flood's infection swarm (`infection_swarm_control`), `action_obey`, path avoidance.
- Weather, breakable surfaces (glass), glow and flag widgets.
- The cutscene's recorded-animation playback (`recorded_animation_playback_v1`).
- Player-profile saves (`saved_game_files.c`).

A wider coverage set would shrink this list: a10's opening, a vehicle route, a death and revert, and b40's snow.

## Smaller finds

- **libc's scanf engine (12 KB)** is linked for one `sscanf` in `psp_sdl.c`'s controller set-up, plus `fscanf` in `psp_tick_account` and the director's camera file. The printf integer variants (12 KB) come from inside the library.
- **The merged strings are ≈350 KB.** Script function names and help (hs.c 34 KB, hs_globals_external.c 11 KB) and assertion texts are the large ones. Trimming them is a separate project.

## Suggested order (for Sheepdog01 to pick)

1. **The campaign flavour without multiplayer** (group 1). It's the biggest piece, and the seam list above is short.
2. **The cheap data trims:** lens flares, the occlusion slots and the vertex shader copies, each with a check mode.
3. **Groups 2, 4 and 5.**
4. **Use the space for the window.** Lower `GAME_STATE_BASE` and the bake's `-X` by what the program lost, and re-measure a50, b40 and a30. The other way is to keep it as heap (b30's heap peaks near 4.5 of 5.25 MB [E]).
5. **Group 6** after a function-by-function read, and group 3's multiplayer screens with group 1.

Something to look at separately: loading ui.map never loaded the main menu's widgets (`ui_widget_load_by_name_or_tag` ran in no campaign run). The PSP's launch menu goes straight to a map, so nothing needs it today.
