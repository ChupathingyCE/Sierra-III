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

## v0.9.0d (2026-11-11, in progress)

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

**Still to do** (`coord/handoff/rebase090.md` in the lab): 33 anchors in game_sound.c (the obstruction cache, now under `HALO_XBOX_CONSOLE` guards: 51-56), ai_debug.c, render.c, models.c, lruv_cache.c, ui_widget.c, terminal.c, hud_messaging.c, virtual_keyboard.c, path_obstacle_avoidance.c, hs_compile.c, playlist_profile.c, xbox_files.c, and the multiplayer files (network_damage.c, network_distributed.c, network_client_manager.c, p2p.c, p2p_lobby.c); then the compile, the new units' fit (`port/linux/game`'s new files), `psp_limits.h` against the engine's new `halo_port_limits.h` defines (`HALO_PORT_WIRE_NETWORK_*`, `HALO_PORT_ADVERTISED_*`), and the emulator proof.
