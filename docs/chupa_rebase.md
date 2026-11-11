# The ChupathingyCE rebase (build 110, 2026-10-09)

The PSP port now builds on either engine base: **halo-ce-universal** (every build up to 109) or **ChupathingyCE**. Sheepdog01 decided to switch (2026-10-09): it is a separate build, so the old one stays as it was while the new one catches up. Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator, **[D]** read in the code, **[U]** inference.

## What ChupathingyCE is

- A community fork of OpenCE, the same line as halo-ce-universal: the Halo CE decompilation ported to modern machines. Ours is at `b78e6cf`, version 0.7.1d, CC0.
- What it adds over halo-ce-universal [D]:
  - **fixes**: a sweep of the decompilation's original bugs and recent asserts (`docs/bug-sweep.md` upstream);
  - **robustness**: a damaged map's tags, scripts and saved games are checked and refused or corrected instead of crashing;
  - **desktop features** the PSP has no use for: Custom Edition maps, online play and its stats, the PC menus, high-resolution HUD and text, touch input.
- What it costs the PSP: memory. Its desktop features add code and tables, and some of its checks want buffers sized for a desktop. Most of this document is about that.

## How the port carries both bases

The engine is never edited by hand: `psp_fixups.py` patches a mirror of it. The rebase teaches the patches the second base.

1. **Which base:** `psp_fixups.py` and `psp_build.py` look for `source/cseries/xbox_address.h` (ChupathingyCE's 64-bit address helpers). No setting to forget.
2. **A change that differs per base:** `Either(universal=(old, new), chupa=(old, new))`. Each half applies only on its own base; `None` means nothing to do there.
   - 32 changes have a ChupathingyCE half; 6 are halo-ce-universal only (ChupathingyCE already has the fix, or does it its own way).
   - `FIXES_CHUPA`: 15 entries only ChupathingyCE needs (below).
3. **Checking a new upstream:** `PSP_FIXUPS_CHECK=1 python3 psp_fixups.py TREE` lists every change whose anchor no longer matches once, without stopping at the first. Both bases check clean.
4. **The build** (`psp_build.py`):
   - drops `-DHALO_CUSTOM_EDITION` and `-DHALO_GAME_BROWSER` from the desktop flags;
   - leaves out the desktop-only units (`CHUPA_DESKTOP_UNITS`) and the co-op units (with the multiplayer ones);
   - adds `-DPSP_CHUPATHINGYCE=1`, which `psp_limits.h` reads for the netcode's layout and version;
   - keeps the `-iquote port/linux/game` path ChupathingyCE's game code includes through.
5. **One home for the PSP's sizes:** `psp_capacity.h` now holds the tag window's and the texture cache's sizes too (they were in the fixups' prelude, which only patched files see), plus the pool sizes ChupathingyCE reads from the capacity header. All are the Xbox's.

### The answer files

What the game calls in a unit the PSP leaves out is answered by a small function that does what the real one does with nothing to do. These replace the linker's stubs (`return 0`), and several would have broken the game:

| Function | A stub's 0 would... | The answer |
|---|---|---|
| `coop_scripts_safe_to_save` | never let a checkpoint save (it *is* the scripts' `game_safe_to_save`) | `game_safe_to_save()` |
| `coop_scripts_player_add_equipment` | skip the player's starting weapons | `player_add_equipment(...)` |
| `network_coop_player_collisions` | turn off player collisions | `TRUE` |
| `network_objects_damage_animation` | swap every damage animation for number 0 | the animation it was given |
| `pc_menus_root_name` | open no main menu | the Xbox's main menu |
| `text_hires_font`, `hud_hires_override_find` | treat "font 0" as real and draw no text | `-1` (none) |

- `psp/src/psp_chupa.c`: the game's calls (PC menus, the tag check, zlib).
- `psp/src/psp_chupa_platform.c`: the platform layer's (build identity, touch, desktop OpenGL extras, high-resolution HUD and text).
- `psp/src/psp_campaign.c`: a ChupathingyCE section for network co-op and its netcode hooks.
- The build links with **0 symbols stubbed**, as halo-ce-universal does.

## What ChupathingyCE needed changed for the PSP

**Crashes and dead ends**
- **Powerup bounds at map load** [E]: b30 crashed as it loaded. ChupathingyCE widens two powerups' render spheres by reading their meshes through the tags' vertex buffers; the PSP's bake keeps meshes in the geometry pack, so the buffers point at nothing. Skipped on the PSP: the spheres stay the tags', as on the Xbox.
- **Saved games**: ChupathingyCE reads a saved game into a second game state (3.3 MB, half the heap), checks it, then takes it. On the PSP that malloc fails, and the menu's saved-game path would then delete the save. The PSP reads in place and checks there. Checkpoint reverts work [E] (14 in one b30 run).
- **The tag schema check**: off on the PSP. It wants a bit per byte of the tag window (1.1 MB) and checks against the Xbox's layouts, which the PSP's bake changes (slim blocks, packed BSP vertices, geometry in the pack): it would refuse our maps.
- **Smaller things**: no `dladdr` on the PSP; the clock reads the PSP's microsecond counter (ChupathingyCE's shared clock needs 64-bit atomics); the log, the address window, the sound cache's page count, sound names the slim layouts dropped.

**Memory: static** (the program's size comes off the heap's end)

| | Program | Heap |
|---|---|---|
| b109c (halo-ce-universal) | 4.48 MB | 6,080 KB |
| halo-ce-universal, build 110 | 4.41 MB | 6,144 KB |
| ChupathingyCE, first link | 4.89 MB | 5,632 KB |
| ChupathingyCE, build 110 | 4.55 MB | 6,016 KB |

(The heap is what's left between the program and the game state, in 64 KB steps.)

What came back, ChupathingyCE's buffers at the PSP's size:
- the script check's 76 KB stack, now only while it walks (it walks only a damaged map's scripts);
- the empty tag data 64 → 8 KB; the multiplayer vehicle homes 1,024 → 80; the cluster lights cache 2,048 → 448 entries (b30 peaks at 294); the saved-game check's list of allocations 256 → 128 (a map makes 81);
- desktop OpenGL's sampler cache and the texture lookups remembered (the null renderer's).

And on both bases:
- two debug displays the PSP never shows, at a smaller size (`FIXES_110`: the profile graph's table, the collision spray);
- render interpolation's buffers for one local player, not four (19 KB);
- the code that runs once a frame or less built for size (`psp_build.py`'s `COLD_PORT_UNITS`: the settings, the controls' polling, the file calls, the launch menu, the overlay, the crash handler's side, the load trace).

**Memory: at run time** [E], b30's beach with the same inputs, 150 emulated seconds:
- ChupathingyCE first took about 900 KB more heap. Two desktop buffers did most of it: the widgets' pool (256 KB for the PC menus; the Xbox's menus need 16 KB) and the high-resolution text's atlas (256 KB). Both are the Xbox's again on the PSP.
- After: heap top 4,988 KB against b109c's 5,004 KB; in use 4,651 against 4,662.

## Speed: the cache model [E]

**The fair comparison is a scripted stretch.** The model now counts the ticks it saw (`tools/ppsspp_local_patches.diff`), so figures per tick are exact, and both builds start from their own state at the same moment of the same map. On the beach, though, the battle goes differently in every run, and the work per tick with it: two runs of the *same* build differed by about 10%. The Pelican flight into b30 is a cinematic, the same work in both builds, so that is the comparison to trust.

b30's Pelican flight, a state at 42 emulated seconds, then 30 seconds in the cache model:

| Per tick | b109c | ChupathingyCE (build 110) | Change |
|---|---:|---:|---:|
| Ticks | 579 | 568 | |
| Instructions | 7.08 M | 7.21 M | +2% |
| Instruction misses | 106 K | 112 K | **+5%** |
| Data misses | 61.1 K | 61.4 K | +0.5% |

| Per frame | b109c | ChupathingyCE | Change |
|---|---:|---:|---:|
| Instructions | 14.7 M | 14.9 M | +1% |
| Instruction misses | 124 K | 128 K | +3% |
| Data misses | 82 K | 79 K | −4% |

- **Close to parity.** The tick runs about 2% more instructions; its instruction misses are 5% more, because ChupathingyCE's fixes and checks make the hot code bigger (the PSP's instruction cache is 16 KB). On the PSP that is roughly 1 ms of a tick that takes tens of ms [U].
- **Trusted maps.** ChupathingyCE checks a map's data each time the tick reads it: each animation keyframe against the animation's data, each node a collision walk enters, each edge of a surface's ring. They guard a damaged or hand-made map; the PSP's maps are its own bake of Sheepdog01's disc. On the PSP those checks answer TRUE and the compiler drops them (`FIXES_CHUPA` `#trusted`, the ring check through `collision_bsp.h`), as halo-ce-universal reads those maps. In the beach runs they had been about 45 K instructions a tick (1-2%), and they are gone from the profile. `PSP_MAP_CHECKS=1` builds them back in.
- **What stays**: ChupathingyCE's other changes on the tick's path (the cluster lists, more `datum_try_and_get`, quaternions normalised where the Xbox didn't, node counts clamped). Each is small, and most are fixes.

## Where it stands [E]

| Run | Result |
|---|---|
| b30, the old bake (converted at load), 150 emulated s | Plays, dies and reverts to checkpoints 14 times; no crash, no strict-mode sites. Heap top 4,988 KB (b109c 5,004); 4,992 with every change below |
| b30, the b108 bake (its first block slim from the bake, three converted), 150 emulated s | The same; the same picture as the old bake's at the same moment. Heap top 4,972 KB |
| b40, the b108 bake (slim layouts from the bake), 120 emulated s | Plays at 30 fps; no crash. Heap top 3,896 KB (b109c 3,892) |
| The main menu, 300 s, on the old `ui.map` and a fresh bake of it | The Xbox's menu, the same picture as b108; 30 fps, 3 ms a frame |
| halo-ce-universal rebuilt with the shared changes, b30 | No crash; nothing changed for it |

The final build (110h, the package) was run on b40 and both menus after its last change; the others ran on the build before trusted maps' second set.

- Builds and links clean on both bases, 0 symbols stubbed.
- **The heap**: 6,016 KB, b107's size (b109c had 6,080), with the same use at run time as b109c. b107's full run of b30 on the PSP peaked at 5,956 KB of its 6,016 [M], so a long run is the test. The keep word `soundN` (the sound cache, default 1,536 KB) is the knob if it fills.

## Found on the way

- **The emulator's sticks have a stale `ui.map`** (2026-10-01, before build 70's 16-bit collision packing). The PSP build reads every map's collision as packed, so the main menu's little BSP read wrong. halo-ce-universal never noticed; ChupathingyCE's checks did ("a bsp2d node is not one of the bsp's", once, in the menu) [E]. Sheepdog01's stick has the bake's `ui.map`, so this is the emulator's alone. Rebaked with the b108 bundle (`/tmp/msmenu2`, 259 s): the main menu draws the same picture on it [E]. Every emulator stick now links that bake (`/home/claude/maps_psp/ui_b108/`: `ui.map`, `ui.pgeo`, `ui.ptex`; stale `ui.vram` files removed), and build 110's menu runs on them clean [E]. The old one is kept in `maps_psp/ui_pre70/` for A/B.

## Next

1. **Hardware**: b30 start to finish on build 110, deathless, with the heap line watched (`heap top`). Then The Library, the heaviest map for sounds.
2. If the heap fills: `sound1408` first (128 KB back). Then the next static candidates: the rest of ChupathingyCE's desktop code paths, the network client's update queues (66 KB, sized for network play).
3. ChupathingyCE upstream: `PSP_FIXUPS_CHECK=1` against each new commit before pulling it.

## Building

The engine checkout needs configuring once, and its generated headers made:

```
# [E] cloud machine: in the ChupathingyCE checkout
python3 configure.py --no-game-browser
ninja build/linux/halo_msvc_semantics.h build/linux/platform_msvc_semantics.h

# [E] cloud machine: in psp_port
HALO_REPO=/home/claude/chupa PSP_OUT=/tmp/psp_chupa python3 psp_build.py --jobs 2
```

- `PSP_MAP_CHECKS=1` builds ChupathingyCE's map checks in (trusted maps off).
- `HALO_REPO=/tmp/halo-sp` (the default) still builds halo-ce-universal.

## The 0.9.1 test build, 6eef2f3 (2026-11-11, in progress; first aimed at v0.9.0d)

**Interim pin** (the maintainer, 2026-11-11 10:47): the 0.9.1 test build's commit `6eef2f3e3ce8` (tag `v0.9.1-testing`, a moving tag, so the sha is pinned) until 0.9.1 is released; then the pin moves to its tag and `PSP_FIXUPS_CHECK=1` runs again. 6eef2f3 is not a descendant of v0.9.0d (merge base 6f6e545; 494 commits past it, v0.9.0d's 154 own commits merged in by content). Network version still 32 (11 to 42). The work below was done against v0.9.0d first (round 1 and round 2's first half), then moved: at 6eef2f3, 42 more campaign anchors failed and 11 multiplayer ones.

The pin moves from v0.7.5d (`d98982b`) to v0.9.0d (`085ad8e`), past ChupathingyCE's GPL change (the maintainer's decision, 2026-11-11; `UPSTREAM.md`). Network version 29 → 32 (joining 11 to 42; `psp_limits.h`). With the engine's configure step run, `PSP_FIXUPS_CHECK=1` listed **83 anchors** that no longer match [D]; none were cascades of an earlier failure. The engine now has console ports of its own (`HALO_XBOX_CONSOLE`, `HALO_CONSOLE_*`); the PSP defines none of them and keeps its own paths, so the PSP takes each new feature's desktop side unless a line below says otherwise.

**Build switches.** The desktop builds now also set `HALO_MAP_LIST` (the map list and map packs that go with Custom Edition, Stubbs mode's lower tag window among them), `HALO_COMPRESSED_MAPS` (Chimera's compressed maps, zstd) and `HALO_BINK_FFMPEG` (the movies' FFmpeg decoder). `psp_build.py` drops them with `HALO_CUSTOM_EDITION`.

**Dropped: the engine now does the fixup's work** [D]
- `FIXES_128` `rasterizer.c#obscurer`: the engine skips the obscurer test unless the debug display of a model's vertices is on. Its keep word `obscurertest` is gone (presets.txt, the launch log).
- `FIXES_36` `main.c`: the frame throttle sleeps a millisecond at a time in every build.
- `FIXES_SOUND` `bipeds.c#unarmed`: melee with no weapon is refused again, as on the Xbox (`upstream_reports.md` §1). Its keep word `unarmedmelee` is gone.
- `FIXES_106` `hs.c`, the call site's size test: the size is checked inside the engine's new `hs_scenario_syntax_data_problem`, where the PSP's map-sized table is now allowed (below).
- `FIXES_CHUPA` `hs.c#walk`: the engine allocates the script check's 76 KB stack only while it walks, then frees it. It differs only when memory runs out: the engine refuses the expression, where the PSP's ran it unchecked.

**Left out on the PSP, as on the engine's own Xbox build** [D]
- The structure's culling (`port/linux/game/structure_culling.c`): buckets allocated as each BSP loads, then tested each frame before the surface walk. `FIXES_CHUPA090` points the PSP at the header's no-op forms, and the unit isn't compiled. The PSP keeps its own walk and the heap.
- The objects' packed bounding spheres (`object_bounds_cache.c`): this is the PSP's own `psp_object_spheres` (build 107) at the same two collision loops. The PSP keeps its own test (`psp_sphere_may_touch`, `psp_sphere_may_cross`) under `#ifdef HALO_PSP`, with the engine's test in `#else`. Its update is in `#else` too, the unit isn't compiled, and `psp_chupa.c` answers the game state's invalidate hook.
- The particles' new tick mode (moved once a tick, unseen ones every 4 ticks, the tick tables): the PSP's own update path replaces the engine's whole block. `display.particle_limit` (0, no limit) and its ring are taken as the engine has them.

**Re-anchored, intent kept** [D]: the sound cache's size (a macro on the PSP; the debug page array stays 1024 pages; the sound count is the engine's `SOUND_CACHE_SOUND_COUNT`), the sound cache's debug name (snprintf), the soundplace hook, the FPU traps at thread start (before the engine's new thread-stop bookkeeping), the log line (the engine's console debug channel kept in `#else`), the lens flare rows (`LENS_FLARE_OCCLUSION_RESULT_ROWS`), main.c's render loop (the new `MAIN_STAGE` and `HALO_PERF_*`, no-ops on the PSP), the GL mirror's page tables (`PLATFORM_CONTIGUOUS_MAXIMUM`, also defined in the PSP's `platform.h` block), the vertical blank thread's loop (`thread_stop_requested`), the memory map (the PSP's tag window ahead of Stubbs mode's; `SOUND_CACHE_SIZE` redefined after `physical_memory_map.h`'s default; `SOUND_CACHE_MAXIMUM_ADDRESS`), the sound driver's `SOUND_CACHE_SIZE` (now from `physical_memory_map.h`, redefined after the include), the PSP's menus hook (before the engine's map-loaded hook), the Z: cache's skip (before the console cache screen's count), objects.c's datum inline and sphere copy, the machine timer (after the engine's elevator carry), the function values read (the tick's values on the PSP: the ME reads there too, and the engine's new `object_get_function_values` returns the drawn frame's interpolated values), the game state image frees, the visibility walk's tail and surface index read (`HALO_ADDRESS_TRANSLATED`), the collision log's clock, the particle creation (the PSP's budget before the engine's limit), the animation keyframe copy (`ANIMATION_ALIGNED_COPIES`), the game state's replace hooks, and the script data's header check (any maximum up to the engine's, the table sized to it).

**Multiplayer only, not compiled or checked** (the rule leaves multiplayer out): `network_objects.c`'s loop also clears the engine's new `objects_host_last_objects` and `objects_client_unsteered_path`. Co-op debt: the PSP's client-only `HOST_OBJECTS 1` shrink doesn't cover `objects_host_last_objects`.

**Round 2: the last 33** [D]

Dropped, the engine now does the fixup's work:
- `FIXES_MENUS` `ui_widget.c#menus`, the column `draw_bitmap_in_rect` starts from (`draw_bitmap_texel_x`) and its use in the texture coordinates: the engine has both (for its wide-screen keyboard). `psp_keyboard_background` uses the engine's variable.
- `FIXES_FRONT` `xbox_files.c#copybuffer`: `CopyFileA` takes its 64 KB buffer from the heap. What went with the fixups: the PSP's 2 KB stack buffer when the heap had no 64 KB; the copy now fails (ENOMEM) instead.
- `FIXES_ASSERTSEVAL` `path_obstacle_avoidance.c#heap` and `hs_compile.c#variable`: the engine marks both `match_vassert_evaluated`. Under `PSP_ASSERTS_OFF` that macro fell back on the PSP's `match_vassert`, which never evaluates, so the two inserts (and the new `network_settings` read) would have been lost: `FIXES_CHUPA090` `cseries.h#evaluated` makes the evaluated forms evaluate, unchecked, when the PSP's asserts are off.
- `FIXES_FRONT` `playlist_profile.c#variants`, the comparison: the engine leaves a default variant whose file already holds its bytes (`playlist_profile_default_file_matches`). The PSP keeps its keep word `variantswrite` (written every time) and the log's count.

Re-anchored, intent kept:
- `game_sound.c` obstruction (`FIXES_106`, `FIXES_107`): the engine's cache is under `#ifndef HALO_XBOX_CONSOLE`; the PSP's reuse rule sits inside the same guard. The five 107 anchors were cascades.
- `ai_debug.c` path storage (`FIXES_107`): the counts are `MAXIMUM_AI_DEBUG_PATH_STORAGE` and `AI_DEBUG_ACTOR_RECORD_COUNT` now (`ai_debug_records`' sizes renamed to match). The actor records moved behind `ACTOR_DEBUG_INFO_GET` in `ai_debug.h`, out of `ai_debug_records`' sight (8 files wrapped before, 1 now): with the PSP's one record allocated, the macro would have written past it. `FIXES_CHUPA090` `ai_debug.h#records` puts its index through `PSP_DEBUG_INDEX`.
- `render.c` (mevisland's push): after the engine's `HALO_PERF_RENDER_PART`, a no-op on the PSP.
- `models.c` (SSPR's level): after the engine's `display.full_model_detail` choice, before the debug override, as before.
- `lruv_cache.c` (the block walk): the engine's release build turns the walk off (`HALO_RELEASE`, which the PSP sets); on the PSP that is left to the keep word `lruvverify` as before, so the engine's line is `!HALO_PSP`.
- `ui_widget.c` and `hud_messaging.c` (the PSP's glyphs): the engine's new `glyph_scale` and `bitmap_scale`; in the HUD the glyph's extra width also moves the engine's new gap for the words after an icon (`hud_message_icon_x1`).
- `terminal.c` (`FIXES_CHUPA` `#render2`): the engine draws the console's output only while it's open by default now (`terminal_render_enable` starts at 2); `terminalon` draws it as 1 would.
- `virtual_keyboard.c` (the legends' glyphs): before the engine's wide-screen sides (`HALO_PORT_WIDE_INTERFACE` is on for the PSP, but they're drawn only on a screen wider than 640).
- `playlist_profile.c`: the loop's bound adds `NUMBER_OF_PORT_DEFAULT_PLAYLIST_PROFILES` (0 without the game browser).

Carried unported, multiplayer only (8: `network_damage.c`, `network_distributed.c`, `network_client_manager.c`, `p2p.c` ×4, `p2p_lobby.c`): a campaign build doesn't compile these units, so `psp_fixups.py` reports their failed anchors (`MULTIPLAYER_ONLY`) and goes on. The engine moved toward the PSP's own work there (`P2P_JOINER_ONLY` searches its stand-ins as the PSP's `psp_proxy_at_port` did; the Online Games item under `HALO_CONSOLE_ONLINE_GAMES`): reconciled when multiplayer work resumes.

The passes: `transport_endpoint_set_winsock.c`'s step line made unique (a second deadline), `gl_never` counts 12 tests of `gl_ready` (the menus' pointer's bgfx form, `halo_draw_picture`) and finds `gl_initialize` in `HALO_BGFX`'s `#else`, and `psp_build.py` drops `-DHALO_BGFX` (the new bgfx renderer). Every other pass counts as on tock162 9e74d6e with the old engine (168 files, unaligned 1, breadcrumbs 12, appended 4, skipped passes 3, designators 36, AI state 3, colours 2, interpolation 1, GL 2, COLLUSERS 24, AIMETERS 11).

New engine units the campaign build compiles as they come (the desktop list leaves out only what it names): `port/linux/game/` audio_probe, chat, conduct_rules, controls_screen, determinism_check, discord_game, follow_camera, gametype_files, host_evidence, host_rules, invite_prompt, line_of_sight, loading_picture, loading_screen, map_packs, menu_style, powerup_timers, profile_names, profile_screen, stubbs_functions, stubbs_mode, team_change (network_* are multiplayer units).

**Moved to 6eef2f3** [D] (round 2, second half):
- **The perf log** (`halo_perf_log.h`, `HALO_PERF_BEGIN`/`END`/`COUNT` around the tick's parts in game.c, physics.c and objects.c's update loop: 26 anchors). The PSP never defines `HALO_PERF_LOG` (nor `HALO_CONSOLE_GL_LAYER`, which turns it on), so they're `((void)0)`; `psp_fixups.py`'s `PERF_STRIP` takes those statement lines and the include out of the three files before the anchors match (none is the lone body of an if or a loop at 6eef2f3). render.c's and main.c's `HALO_PERF_*` lines stay in their anchors, as round 1 re-anchored them.
- `game_sound.c`: the obstruction cache's guard is `HALO_PORT_FRAMES_BETWEEN_TICKS` now (on for the PSP: below).
- `rasterizer_lights.c` (the flare peak): before the engine's `lens_flare_test_frame` count.
- `collision_bsp.c`: the engine's batch of vector tests (a node cache and a start node below the root). BSPR's kept answer is taken and kept only for a walk from the root (`start_node` 0, depth 0); SCRATCHBSP's scratchpad node read sits in the cache's miss branch.
- `structure_visibility.c` (the surfaces' triangle test): the PSP's `psp_material_vertex_point` in place of the engine's new little-endian `vertex_data_read_point`.
- `object_lights.c` (datum_get inline): after the new `plasma_lighting.h` include. `lruv_cache.c`: the engine's skip now also under `HALO_CONSOLE_NO_DEBUG_ASSERTS`; still `!HALO_PSP`. `ui_widget.c` (the boot screen's input hold): before the engine's Guide. `xbox_kernel.c` `platform_log`: the PSP's line write replaces the engine's new format-then-redact block (`log_paths_redact_line`).
- `gl_never` counts 14 `gl_ready` tests.
- Multiplayer carried (11 now): `network_objects.c` ×2 and `p2p_signal.c` join the 8.

**The engine's console switches** (`halo_console_features.h`, the master 2026-11-11 from research's read) [D]:
- `HALO_CONSOLE_WAITED_VISIBILITY_TESTS` set (`psp_build.py`): the lens flares' visibility tests waited in their frame as before; without it each flare gets an async slot (~360 KB of statics) and the build needs the GL layer's `d3d8_visibility_test_number`.
- `HALO_CONSOLE_XBOX_MAP_SIZES` set: flare occlusion rows 8,192 not 32,768 (the PSP's own table is `PSP_LENS_FLARE_MARKERS` already), respawning vehicles 256 not 1,024 (-39 KB).
- `HALO_CONSOLE_LIGHT_LIMITS` left off (a lead for the Lighting box, not the rebase).
- `MAXIMUM_LIGHTS_PER_WINDOW` = `HALO_PORT_MAXIMUM_RENDERED_LIGHTS` = 128 on the PSP (the Xbox's: `halo_port_capacity.h` gives 512 only where not `HALO_ANDROID && !HALO_64BIT`, and the PSP sets `HALO_ANDROID`).
- **Open, for the master: `HALO_PORT_FRAMES_BETWEEN_TICKS`** is on for the PSP (every build that isn't `HALO_XBOX_CONSOLE`). It gates the native builds' between-tick code in about 20 files (observer, contrails, particles, player effects, weather, input, first-person weapons, motion sensor, fog, render objects, sound manager, game_sound's obstruction cache ...). Through v0.9.0d that code had no guard, so the PSP built it on v0.7.5d too; turning it off would put the PSP on the Xbox console's path in all of them (and drop the obstruction cache the PSP's FIXES_106 reuse rule extends). Left on, as the old engine had it, until the master decides; the emulator batch's heap lines say what it costs against tock162.

**The compile** (round 3; lab sierra-build 43063428874 of ab8ec74 failed in 491 of 508 units) [D]:
- **`halo_math.h`**: 6eef2f3 adds `halo_fmod` (the UCRT's x87 fmod avoided) after the trig macros, declaring `double fmod(double, double);` after them. The PSP's single-precision block (build 19) sat after `pow`'s macro, so its `fmod` macro expanded that declaration (the 491 units). It now sits after the engine's `#define fmod(x, y) halo_fmod(x, y)` and undefines it: float arguments take `fmodf`, doubles the C library's `fmod` as before; `halo_fmod` is unused on the PSP.
- **`halo_console_features.h` wasn't in the PSP's units.** The engine's `halo_port_limits.h` includes it; `psp_limits.h` replaces that header (it takes its guard), so only the four desktop units that include it themselves saw any capability, and `HALO_PORT_FRAMES_BETWEEN_TICKS` and `HALO_PORT_WIDE_INTERFACE` were off in every other unit (round 2's "on for the PSP" held only on paper; the sign was `particles.c`, whose `particles_leftover_ticks` is declared under the one and used under `!HALO_XBOX_CONSOLE`). `psp_limits.h` now includes it, with `HALO_CONSOLE_NO_SHADER_PREP` (the GL renderer's shaders made while a map loads) and `HALO_CONSOLE_NO_LOADING_PICTURE` (`loading_picture.c`, new) set first, as a console leaves them; so both capabilities are on in every unit, as the old engine's ungated code was. (`posix_files.c` and `tomlc17.c`, compiled with their own `build.ninja` lines, can't find the header and test none of the capabilities: the include is under `__has_include`.)
- **`psp_capacity.h`**: `HALO_PORT_MAXIMUM_TRANSPARENT_GEOMETRY_GROUPS_EXTRAS` and `HALO_PORT_MAXIMUM_DYNAMIC_TRIANGLES_EXTRAS` (new: the sizes a console above its memory baseline raises, `halo_console_memory.h`) are the PSP's own 384 groups and 32,768 triangles, as the engine gives every build but the Xbox's. They size `rasterizer_transparent_geometry.c`'s and `render_particles.c`'s per-group arrays and the dynamic triangles' index buffer: unchanged from tock162.
- **`ai_debug.h`**: the engine split the Xbox layout's offset asserts into two blocks (`#ifndef HALO_XBOX_CONSOLE`, then `#if !HALO_64BIT && !HALO_XBOX_CONSOLE`), so FIXES_23's `#ifndef HALO_PSP` (opened at the first, closed after `field_7D380`) closed the wrong block and left the second's asserts on the PSP's shrunk `ai_debug_state` (no `path_state` or `path_storage`, one actor record): 29 units. Both blocks' conditions now carry `!defined(HALO_PSP)` instead.
- **`sound_dsound_xbox.c`**: the audio capture's per-sound line (new, `audio_probe_sound_started`) names the permutation, whose name the PSP's sound tags leave out (build 108): the call is left out on the PSP, which has no audio capture.
- **Stubs** (`psp/src`): `coop_scripts_teleport_followers` takes the flag's position; `coop_scripts_unit_near` and `coop_scripts_arrive_at` (new; asked only by a co-op host's `hs_teleport_players_not_in_trigger_volume`) answer FALSE; `text_hires_font` takes the tag's letter widths; `text_hires_enabled` (new, asked by `rasterizer_text.c`) answers 0.

**The first link** (lab sierra-build 43065805314 of df93a04: linked) [D]: 41 names stubbed (tock162 stubs 2: `shader_definition_get`, `text_hires_enabled`) and the program 9.37 MB with a **1,412 KB heap** (tock162 e0cf0fa: 5.12 MB, 5,768 KB): **+4.27 MB of .bss**, from the new desktop units the campaign build compiled as they came: `loading_screen.c` 2.4 MB (the Xbox's loading screen made by the CPU at 640×480), `plasma_occlusion.c` 1.2 MB and `plasma_collision.c` 270 KB (the Light effects), `rasterizer.c`'s model vertices debug display 128 KB, `render_interpolation.c`'s particle streams 60 KB, `audio_probe.c`. And `halo_port_capacity.h`'s two includes were missing too (the PSP's headers replace it): `HALO_CONSOLE_MEMORY_EXTRAS` linked as a stubbed function answering 0, so the transparent geometry groups' and dynamic triangles' maximums would have been 0. Fixed in b62edf5:
- `psp_capacity.h` includes `halo_console_memory.h` (no memory class: every size its baseline); `psp_limits.h` includes `halo_port_features.h` beside the console features, with the extras new since v0.7.5d left out as a console leaves them: `HALO_CONSOLE_NO_LIGHT_EFFECTS`, `_NO_SMOOTH_FUNCTIONS` (an object's functions, change colours and camouflage between ticks; ~220 bytes an object slot), `_NO_PARTICLE_STREAMS`, `_NO_SMOOTH_SHAKE`, `_NO_AUDIO_PROBE`, `_NO_CONTROLS`, `_NO_DELTA_PROFILE`, `_NO_DISCORD`, `_NO_VOICE`. The extras the PSP built on v0.7.5d (player names, hi-res text, telnet, tag validation through psp_build.py's desktop list) are as they were; defining `HALO_CONSOLE` whole would change those, so it isn't.
- `loading_screen.c` joins the desktop units; `progress_bar.c` (FIXES_CHUPA090 `#loadingscreen`) runs its update as on v0.7.5d, without the step gate and the CPU's picture; `loading_screen_skip_pressed` answers FALSE (asked only while `display.loading_screen_*_seconds`, 0 by default, holds the screen).
- `rasterizer.c` (`#debugvertices`): the debug display's vertices take the Xbox console's form, allocated the first time `rasterizer_debug_model_vertices` is turned on.
- Built now (`PLATFORM_FILES`): `port_folders.c`, `save_folder.c` and `thread_stop.c` (`d3d8_gl.c`'s vertical blank thread starts through `thread_stop_create`, whose stub answered "started" without starting it; `xbox_files.c`'s `platform_folder` names its folders through the other two).
- Answered in `psp_chupa.c` where 0 was the wrong answer or a pointer: the PC menus' key caps (`pc_menu_button_key_cap_left` the bounds' own x0), `pc_menu_text_marquee` (NONE: no value scrolls), `pc_menus_chupathingy`, `pc_menu_controller_capture`; `map_family_parse` (an Xbox map, as itself) and `map_family_xbox_path` (FALSE: no pack's header to read; cache_files.c's multiplayer checks then take a map as playable); `ui_point_prompt_part` (NONE: no pointer); `log_paths_redact_line` (no home folder, as a console). `psp_sdl.c`: `SDL_HasGamepad` (true), `SDL_DestroyWindow`, `SDL_GL_DestroyContext`, `SDL_ShowMessageBox` (nothing made).
- Left to the link's stubs (0 is the campaign's answer): `network_aim_assist_blocked`, `network_damage_client_runs_shields`, `network_game_host_cheats_refused`, `network_game_solo_game`, `network_objects_client_deleting`, `network_rules_mouse_is_aim_device` (multiplayer units the campaign build leaves out), `updater_gamescope`, the GL layer's `halo_glDelete*` (behind `gl_never`).
- **The link** (lab sierra-build 43070045230 of 19d1f62, after b62edf5's two units without the memory header's path, 43069739408): program 5.34 MB (text 3,601,144, data 180,552, bss 1,813,916), **heap 5,540 KB** against tock162 e0cf0fa's 5,768 KB (sierra-build 43056428511): **-228 KB**, of it +129 KB of code and +108 KB of .bss (door portals 20 KB, `render_particles.c`'s run arrays ~30 KB, `game_engine_vehicle_homes` +9 KB, the light scratch and rasterizer lights ~15 KB, the particles' tick order and owners 8 KB, `config_settings`/`config_values` +7 KB, `hs_scenario_skipped_nodes` 2 KB ...). 11 names stubbed, each answering 0 as the campaign would (above). [D]
- **`HALO_PORT_FRAMES_BETWEEN_TICKS`'s cost** [D]: on in every unit now, as v0.7.5d's ungated code was; the statics under it are ~5 KB (`game_sound.c`'s obstruction cache 128 × 32 bytes, `input_xbox.c`'s button and key down times, `observer.c`'s tick velocities, `particles.c`'s tick frame indices), plus its code. Not separately measured: the heap lines compare the whole rebase.
- **The first batch** (lab sierra-batch 43070584623, rebase090p1 on 19d1f62): every new-side run faulted at launch (SIGSEGV reading 0x138 in `psp_tag_block_element`, from `sound_render` → `scenario_location_underwater` during the precache's pregame frames) after `map path for 'b30' is too long` / `couldn't find map 'b30' on the DVD`: 6eef2f3's `cache_file_get_map_path` names a map's file through `map_family_xbox_path`, which b62edf5's answer left FALSE. It now composes `<maps folder><name>.map` as v0.7.5d's `cache_file_get_map_path` did. The old side (9e74d6e) ran clean (b30 to game time 1430, d20 2690; c20 to 2828, without `pad: replay ended` there too).
- **The second batch** (lab sierra-batch 43072517988, on 4b0c226): the maps were found, but every new-side run faulted at the first map's start: a float read at 0x0c605a00 (outside user RAM) in `rasterizer_transparent_encloses`, from `render_initialize_for_new_map` (inlined into `game_initialize_for_new_map`) → `models_fix_transparent_part_links`. That is 6eef2f3's energy-before-glass part links: for a one-node model with one glass and one energy part it locks the parts' vertex and index buffers through the desktop's Direct3D to test whether the glass encloses the energy, and on the PSP those buffers are the bake's. The call is now left out on the PSP (FIXES_CHUPA090 `#partlinks`): its parts keep v0.7.5d's draw order. Bringing the links to the PSP from the bake's own vertices is a restoration ("preserve retail's energy-then-two-sided-glass compositing") to price later with pictures. *(A first reading with the text offsets misplaced by 0x1c0 named door_portals.c; door_portals.c is left out anyway, f61df47: a desktop addition the Xbox never had, "`display.doors_block_light = false` leaves the portals open, as the Xbox had them", ~20 KB of .bss; psp/src/psp_chupa.c answers its five calls as the Xbox behaves. The third batch, 43073928447, faulted at the same read.)*
- **The third and fourth batches** (lab sierra-batch 43073928447 on f61df47, still the part links; 43075402560 on d4e2c8f): the maps load and play, then b30 and d20 fault at game time ~83: the Media Engine's line of sight (`psp_collision_test_vector_quiet`, FIXES_122 `#merays`) reads a float as a pointer in `collision_bsp_test_vector_recursive`. 6eef2f3 added a `cache` field to the vector test's context (render batches' node reuse; stock callers pass NULL), and the PSP's own quiet copy of `collision_bsp_test_vector` built the context without it, so the recursion read stack garbage as a cache. The copy now sets it to NULL. Symbols from sierra-build 43076473836. c20-new quit by itself at game time 420 with no fault in the same batch; read after this fix. *(Lesson: a PSP copy of an engine function that builds an engine struct field by field breaks silently when the engine adds a field; a struct initialiser `= {0}` first would have caught it.)*
- **The second batch** (lab sierra-batch 43072517988, 4b0c226; same heap 5,540 KB, 11 stubs): the map is now found, but every new-side run faults earlier, in `game_initialize_for_new_map` (from `main_new_map`), reading a float at 0x0c605a00 (outside user RAM) in a function at halo.text+0x1c16c0 (`rasterizer_transparent_encloses` by 19d1f62's symbols; approximate, the JIT's PC). Not yet diagnosed (round 4). The old side again clean (b30 1429, c20 2832 without `pad: replay ended`, d20 2645). [E]
- **The heap** (round 5) [E]: with the ME's fix the new side ran clean but filled the heap (lab sierra-batch 43077748556: b30 free low 4 KB and 1,608 requests failed, c20 1 KB and 22,585, d20 3 KB and 4; the old side 275, 274 and 333 KB free, none failed). Found and given back:
  - **The sprites' dynamic vertices** (`rasterizer.h`, FIXES_CHUPA090 `#unlit`): 6eef2f3 sizes them 8 a particle and never under 12,288 for every build but the Xbox console's (for Halo PC maps' busy frames), so the buffer and its spare grew from 96 to 288 KB each and, too big for the room beside the heap, came from it: 576 KB. The PSP keeps retail's 4 a particle (4,096 for its 1,024); both fit beside the heap again, and the 128 KB request after them takes the heap as on the old side: ~448 KB of heap back.
  - **The vehicle homes** (FIXES_CHUPA `#homes`): its anchor still matched `MAXIMUM_VEHICLE_HOMES = 1024`, now the `#else` of `HALO_CONSOLE_XBOX_MAP_SIZES`, which the PSP sets, so it had 256 (13 KB) instead of its 80 (4 KB). The anchor is now the `#ifdef` itself. *(Lesson: an engine `#ifdef` added around a fixup's anchor can leave the fixup matching in the branch the PSP doesn't take; the anchor check can't see it, a symbol size diff against the old build can.)*
  - **The particles' limit and tick mode** (FIXES_CHUPA090 `particles.c#limit`): `display.particle_limit` (0 by default) was looked up by name every frame with its 10-second log, and its ring of the particles in the order they were made (4 KB) kept whatever its value; the tick mode's owner table (4 KB) was read only by `particle_render_position`, though the PSP's own update never moves particles by the tick. Both take the Xbox console's form; the PSP's own budget (psp_prio) decides what's made, as before.
  - **`HALO_CONSOLE_NO_NETWORK_TEST`** (`psp_limits.h`): the network tests' bots and the determinism check (`debug.determinism`, a call every tick and at each game's load), new since v0.7.5d; the lab's statecrc is the PSP's own check.
  - **`follow_camera.c`** left out (`psp_build.py`): the multiplayer follow camera, off by default (the Xbox had the death camera alone); `psp_chupa.c` answers its three calls (NONE: the death camera).
  - **No runtime growth:** with the vertices fixed (sierra-batch 43079950449, 2c4cdaa) the in-use peak equals the old side's (b30 5,487 KB against 5,492; c20 5,494 against 5,493): the master's "~70 KB higher" was the full heap's cap. What's left is the program's size.
  - **The proof** (sierra-batch 43080956834, rebase090p1 on e76a15c against 9e74d6e): every map loads and plays, no fault, no strict site, **0 requests failed on all three stretches**; heap 5,596 KB against tock162's 5,768 (program text 3,583,912, bss 1,775,980; 11 stubs). Free low **b30 108 KB (old 275), c20 101 (274), d20 315 (333)**; in-use peak b30 5,487 (5,492), c20 5,494 (5,493), d20 5,280 (5,434). Play differs from the old engine, as expected: end game times b30 1407 (1428), c20 2823 (2832), d20 2692 (2645). Neither side's c20 reaches `pad: replay ended` at exitat240.
  - **Left: ~170 KB of program growth** against tock162, spread thin [D] (symbols, sierra-build 43076473836 against 43056428511): code +81 KB in ~400 functions (`_rasterizer_screen_effect` 6 KB, `config_load` +5 KB, `hud_messaging_update` 5 KB, `render_camera_build_frustum_checked` 4 KB, `shader_transparent_generic_create` 4 KB, `render_contrail` 3 KB ...), ~34 KB of new strings, and .bss: the particles' runs (`render_particles.c`, ~36 KB: positions, directions, keys, order, part ends, depths), the new config (~13 KB), the particles' radix sort scratch (8 KB), `hs_scenario_skipped_nodes` 2 KB, `thread_stop_slots` 2 KB. (`rasterizer_lights`, 7 KB, isn't new: it was halo_linker_common.c's stand-in.) **The particles' runs** (ChupathingyCE #981: a run cut where other transparent things sort among it) are past the Xbox, which drew each run whole at its centroid, so leaving them out keeps retail's look; it means restoring v0.7.5d's whole-run loop (~150 lines, through the PSP's own particle work in that loop), and it also takes a per-particle place, sort key and cut pass off the frame.. Done in round 6 (below).
- **The particles' runs** (round 6, FIXES_CHUPA090RUNS, Sierra-III 4c8b726): each run of particles (one definition's in one cluster) is built whole again, sorted at its centroid, as the Xbox and v0.7.5d built it. 6eef2f3 places every particle of a run first, gives each the depth it would sort at alone, finds where that depth falls among the transparent queue's other groups (a sort of the queue's depths each frame, a binary search per particle), and builds the run in parts cut there, so an object amid a run sorts among its parts. That is past retail, and it costs the frame a pass per particle and the heap its statics. On the PSP, v0.7.5d's loop is back under `HALO_PSP`, word for word, so FIXES_WALKAPART's three particle anchors match again (they had matched nothing since the pin moved; that dict is listed after this one). The cut's helpers and statics sit under `#ifndef HALO_PSP`. A detached particle is placed at `particle->position`, which is what `particle_render_position` answers on the PSP (FIXES_CHUPA090 `particles.c#limit`). The radix sort that ranks the runs (stable, 8 KB of scratch) is kept: it is a speed-up, not part of the cut.
  - **Size** [D] (round 5's symbols, sierra-build 43076473836 against tock162's 43056428511): the cut's statics are 33,026 bytes (`run_positions` and `run_directions` 12,288 each, `run_keys`, `run_order` and `part_ends` 2,048 each, `depths` 1,536, `part_counts` 770). `render_particles` is 5,256 bytes against tock162's 2,096, with the cut's helpers inlined under LTO and the radix sort with them. So **~35 KB** should come back to the heap, an estimate until this commit's build (*pending: the lab's Actions stopped starting runs at ~20:30Z*).
  - **Exactness, expected** [U]: pictures differ only in a frame where a run would have been cut into more than one part, that is, where another transparent group sorts amid a run's particles. A part also sets its own secondary map radius (its particles' average) where the whole run had one. Play can't change: drawing writes only `last_rendered_frame_index` and deletes the particles whose object is gone, in the same frame as before. The proof is batch rebase090r6 (framecrc against e76a15c, with a second e76a15c run as the control's own spread, b30 and d20; statecrc and both seeds; the heap's free low on b30, c20, d20).
  - **Review**: `psp_fixups.py` FIXES_CHUPA090RUNS, four pairs. Two put the cut's statics and helpers (`render_particles_depths` through `render_particles_part_group_bound`) under `#ifndef HALO_PSP`: nothing on the PSP calls them. One puts v0.7.5d's per-run loop under `#ifdef HALO_PSP` ahead of 6eef2f3's, which goes into the `#else`, and the last closes it. It is listed in the fixups' order before FIXES_WALKAPART, whose anchors are in that loop. Checked locally with `PSP_FIXUPS_CHECK=1`, with and without `PSP_WALKAPART=1`: walkapart's failures drop from 8 to 5, its other five (render.c, objects.c, structures.c) are older and unrelated.

