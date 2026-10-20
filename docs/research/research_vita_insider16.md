# Research: halo-ce-vita 1.1.1-insider.16, read for Sierra-III

A second look at BirchWoodGod's Vita port, at its developer package `1.1.1-insider.16` (commit `82a9f97e`, 2026-10-20), compared file by file with `b6a7a29`, the version `research_vita.md` §12 last read. The package is a pre-release, given to the maintainer with a request not to pass it on; this note keeps to ideas and pointers, and nothing of it is in this repository.

Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code or data, [U] inference. Their numbers are from Vita hardware or their Linux harness, and are marked as theirs.

**Terms:** as `research_vita.md`'s header. BirchWoodGod's own code is ours to take; anything he credits to others is idea-only until its author agrees, except OpenCE's, which is CC0.

## The short answer

**Little that is new for the campaign.** Of ~280 changed or added files, nearly all are multiplayer menus, friends and parties, Forge, Custom Edition and MCC maps, movies and ragdolls [D]. Within the engine there is one tick speed-up worth a count (**L1, dark lights**), one render cull we already mostly have (L2), a client clock fix for the multiplayer session (L3), and two cheap diagnostics (§4). **Nothing new for the open look list** (§5).

| # | Idea (their file) | Exact? | For us | Worth [U] |
|---|---|---|---|---|
| L1 | Attached lights that are dark stay off the map (`objects/object_lights.c`, `light_attached_reconnect`) | Render-exact; game state differs | Count first, then a keep word | **Medium**: a share of `object_connect_lights`' 2.57 ms a tick on b30 |
| L2 | Lens flares out of view get no occlusion test (`rasterizer/rasterizer_lights.c`; OpenCE's) | Yes | Mostly in already | ~0 |
| L3 | A client a few ticks behind the host for a second takes the host's time (`networking/network_client_manager.c`) | Changes the client's clock | Multiplayer session's call | Unknown |
| D1 | A memory pool that runs out says so in release builds (`memory/stack_memory_pool.c`) | Logging only | Cheap | Low, diagnostic |
| D2 | A progress heartbeat that tells a long load from a hang (`port/linux/src/frame_timing.c`) | Logging only | Maybe for the PSP agent | Low |

## 1. L1: dark attached lights left off the map

- **What [D].** Every tick an object's lighting is recomputed, `object_connect_lights` takes each attached light off the map and puts it back: its marker found by name, its sphere's clusters walked, its cluster references remade. The dynamic lighting flag is never cleared, so for a unit, weapon or vehicle that is every tick. Their change works out each attached light's colour first, from the object's function value and change colour, exactly as `lights_preprocess_scene` will. A light whose colour is black is left off the map (or taken off), and goes back as before on the first tick it isn't black. Switch: `HALO_DARK_LIGHTS`, on by default.
- **Why it is safe [D, their reading].** Only the render reads a light's cluster lists, and the render already passes over a black light: no rasterizer light, no lens flare, and nothing taken from it by an object it reaches. The one difference is that a dark light no longer takes one of a scene's 128 visible-light places, which only a scene of more than 128 could show. Unattached (effect) lights, and lights whose object can't be got, go back as before.
- **Their measurement.** In a 32-player crossplay game, lights were 15–20% of a client's tick, and 60% of them were put back. The rest were the plasma weapons' flares (theirs).
- **For us.** `object_connect_lights` measured 2.57 ms a tick on b30 [M] (`levers_next_b138.md`, lever 2). Campaign lights that are dark most of the time [U]: muzzle flashes between bursts, flashlights that are off, and perhaps lights driven by a weapon's charge or overheat. **The count comes first:** per tick on b30, a10 and c20, the attached reconnects and how many of them are dark. If half are dark, it saves ~1 ms a tick on b30 [U]. The test costs a function value and a colour blend per light. That is cheaper than the marker lookup and cluster walk it replaces [U].
- **Checks before it ships.**
  1. **Sleep and the relink memo.** Our sleeping objects, and `relinkmemo`, skip `object_connect_lights`. A light left off the map by an object that then sleeps stays off until the object wakes. This is exact only if a sleeping object's function values can't change [U].
  2. **The state hash.** The light datum's connected flag and its cluster references now differ from the original's, as T7's illumination fields do in `research_vita.md` §7. The play-identical check must mask them.
  3. **Fit with lever 2.** Lever 2 (re-linking lights once a frame) and L1 add up: L1 removes work, and lever 2 moves what is left.
- **Credit.** BirchWoodGod's code, under his grant. Suggested keep word: `darklights`.

## 2. L2: out-of-view lens flares (OpenCE's)

- **What [D].** Before a lens flare's occlusion test is submitted, a frustum test of a sphere that takes in the test's offset point and size (three occlusion radii). First-person weapon flares are exempt, because OpenCE draws them at the weapon's own projection. Their reason: on the Vita each test is a draw and a visibility slot, and a level with many lights submitted hundreds a frame.
- **For us [D].** Our stand-in for the Xbox's query (`psp_fixups.py`, FIXES_31) already casts no ray for a flare with no pixels on screen, and it caps rays at 24 a frame. All that is left to save is the projection of each off-view flare. That is worth ~0 [U] unless a count shows hundreds of flares a frame. Leave it.
- **Credit.** OpenCE `744040c8`, xshxdex98 (CC0).

## 3. L3: the client's clock (for the multiplayer session)

- **What [D].** `network_game_client_handle_game_update` now has a slack constant, `TICKS_PER_SECOND / 6`. A client whose clock stays more than that behind the host's for a whole second takes the host's time, once. The original corrects only a gap of a second or more, so a client that joins mid-match can play 0.4–1 s behind the host all game (their comment; the insider.15 changelog). Short stalls are still caught up tick by tick and never skipped. This is a client-side change, with nothing new on the wire at network version 29 [D].
- **For us [U].** A PSP that can't hold 30 ticks a second falls behind steadily. Under this rule it would jump forward every second, which may be better or worse than lagging. That depends on how our client keeps up today, and it is the multiplayer session's question. Our compile-only rule tonight leaves it untried.
- **Credit.** Probably BirchWoodGod's, though the port's networking also carries others' work. Check the file's history before taking code. The idea is free.

## 4. Diagnostics worth copying

- **D1. Pool exhaustion in release builds** (`stack_memory_pool.c`). When a pool runs out, the request is dropped and nothing is said: a screen opens without a widget, or a text box draws blank. They now log it, at most once a second. Their insider.16 fix (the menus' pool made 4× bigger, which mended blank browser rows and failed joins) is the kind of bug this catches. For us it is one log line where the pool gives up, useful on ui.map and the HUD [U].
- **D2. A load heartbeat** (`frame_timing.c`, `halo_progress_heartbeat`). Long-running loads bump a counter, so the watchdog can tell a stretch with no frames but a moving counter (a long load) from a hang. For us [U]: a stuck Memory Stick read and a hang look the same to the PSP agent today. Low priority.

## 5. The look list: nothing new

Cut-out (alpha-tested) shaders and bump map alpha, see-through and sky water, tracers and contrails, light gels, Cortana's hologram, glass: none of the code behind these changed since `b6a7a29` [D]. The renderer's changes are the interface drawn at display resolution, a scope mask, a compressed bitmap's swizzle, Custom Edition maps and movies. Their renderer runs the Xbox's combiners on the GPU (`research_vita.md` §5), so for these issues the references are still the decomp's own `rasterizer_xbox_*.c` and the PC reference runs. Their contrail changes (`render/render_contrails.c`) guard against the tick and the render overlapping, which only matters to us if the ME ever runs the two at once.

## 6. Not for us

- **Ragdolls** (`port/linux/game/ragdolls.c`, new; the idea is from jojoxdk's MCC physics mod). A killed character's body is drawn as a ragdoll; only the picture changes. It is a look change, and it adds tick work for every body.
- **The interface on its own layer.** The 3D picture is drawn smaller and scaled up, with the interface at the display's resolution. It belongs to the Vita's dynamic resolution.
- Movies from uncached memory, Custom Edition and MCC maps, Forge, parties and friends, the Halo 3-style menus.

**What we could offer back:** unchanged (`research_vita.md` §6).
