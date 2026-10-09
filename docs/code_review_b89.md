# Code review, 2026-10-06 (builds 88–89)

Seven parallel read-only reviews covered:
- psp_ge.c (three parts);
- the platform, input and menus;
- the texture and geometry caches;
- the ME, audio and memory code;
- the game-code patches (psp_fixups.py).

Every finding below was re-read against the code before it was acted on. Labels: [fixed b89], [deferred], with the reason. Emulator checks for b89: default run, red base with objects, firing at a wall (light pass), benchrun timing, Hang 'Em High's bake, a truncated .pstn.

## What started it
The learned VRAM ranking (`<map>.vram`) was never saved on hardware from b47 onward, because it was only written at map close or HOME exit. Every log said "not yet saved". In b87's bench, 90% of texture binds came from RAM. Fixed in b88 by saving during play; see renderer_debugging.md and memory_traffic.md.

## Fixed in b89

| Where | Bug | Effect |
|---|---|---|
| object_lights.c patch (psp_fixups.py) | `lights_distant_lighting_at_point` read vertex normals and lightmap incident radiosity with the Xbox's 32-byte stride from the 6-byte position copy that -G maps keep | **Every object's distant-light direction, shadow vector and radiosity accuracy came from garbage**, with reads past the array. Visible in the emulator at red base: the rifle lit pale blue-grey in b88, normal in b89. Now read from the renderer's copies (`psp_ge_level_vertex_lighting`; streams keep the radiosity word) |
| psp_dsound.c mixer | `(l1 - l0) * fraction` overflowed 32 bits | Full-scale clicks on loud, fast-changing sound at a pitch other than 1, on both mixers |
| psp_dsound.c silent path | A silent voice that had run dry kept advancing | The start of what the game queued next was skipped |
| psp_ge.c cone textures | `aligned(16)` was on the array, not on each element; slots 0 and 2 had texels 8 bytes off | The flashlight cone was shifted a quarter of its width and its clamped edge wasn't black, giving bright bands |
| psp_ge.c light pass (mode 2, the default) | One nudge per draw, sized from the nearest corner, even corners behind the eye | Up to 600+ depth steps, lifting walls 5–10 units away by 0.5–2 units, so light was painted over things in front. Now drawn in distance bands with their own nudge |
| psp_ge.c detail passes | `angle_near_any` ignored the pieces cut at the eye | The ground at the feet could lose its detail map |
| psp_ge.c see-through slots | A slot taken before `list_take` failed kept an earlier frame's pointers | Garbage geometry when the list nearly fills. Slot given back; slots cleared; unfilled ones skipped |
| psp_ge.c on-wall answers | Keyed by a pointer into the game's per-frame triangle pool | Another overlay could inherit a surface's answers, so a symbol sank or a beam showed through. Now keyed by the list's content |
| psp_ge.c .pstn parser | Walls stored at the loop index but counted by successes; no bounds checks | Crash on allocation failure or a damaged file. Every record is now checked, and a damaged file is refused whole (tested with a truncated copy) |
| psp_ge.c level streams | The 16-bit copies leaked on every trim; the budget left out the 16-bit copies and the radiosity | Memory drain with levelcompact on bigger maps |
| psp_ge.c, psp_platform.c logs | Three lines longer than the 512-character log buffer (the cut-corner peak, keep words); bake and compact counters never reset | Lost diagnostics; wrong per-window numbers |
| psp_geo.c pump | Allocated (evicting) before checking the frame's read share | A block evicted for nothing on every further miss that frame |
| psp_sdl.c zoom | `(int)(zoom_until - now) > 0` with zoom_until 0 | A phantom held zoom 35.8 minutes after the PSP booted |
| psp_platform.c benchrun | Counted the first 20 s's 1-second beats as windows | View 0 was measured on 1 s windows right after load (b87's noisy first view) |
| psp_platform.c exit | b87–b88 did Memory Stick work before stopping the ME | An exit could hang there if a write blocked. The ME now stops first; exit.txt is opened at start-up and written through that handle; no texture save at exit |
| psp_platform.c | Callback registration results never checked | Now logged ("callbacks: …") |
| psp_platform.c | Two log flushes could share one static buffer | Garbled or duplicated lines. Flushes are now serialized |
| psp_platform.c, psp_dsound.c | FPU traps unmasked on the callback and audio threads | A non-finite gain would have frozen the PSP |
| psp_tex.c .vram | Truncate-then-write with unchecked writes; two writers possible | A failed save wiped the learning. Now written to .tmp and renamed (the loader falls back to .tmp), with the map-close and in-play saves kept apart |
| psp_me.c stop | The ME halted with a job pending; the caller waited 40 ms holding the mixer lock | An audible gap and a false "failed" on HOME or sleep. The ME now serves its slots before looking at the stop word; no new jobs once stopping; the waiter gives up at once if the ME halted |
| psp_me.c timer | Count stood still on hardware | b88 clears Cause.DC; b89 also writes Count, as me-core's own timer does. Untested on hardware |
| psp_memory.c | `set_protection` with size 0 wrote 1 MB past a 6 KB table | Latent crash |
| vehicles (psp_fixups.py) | A vehicle could sleep without the physics' at-rest bit | A machine (lift) or another vehicle couldn't move it |
| hs_runtime.c (psp_fixups.py) | Each word on the script stack took 8 bytes, against 4 on the Xbox | The deepest scripts of a10 (616 B), c10 (620) and b40 (520) overran the 512-byte stack into the next thread's datum. Campaign only; tested only through console commands so far |

## Deferred (with the reason)

**Campaign work:**
- `converted[]` (model buffers) never forgets dead addresses; after 2,048 of them, models stop drawing. b89 logs it once. The real fix is to keep the converted state beside the geometry cache's own block.
- Learned VRAM ranking is inert on packs over 1.125× the caches or over 1,024 entries, which probably means every campaign map.
- Collision structs are packed at compile time, but a map without -G is accepted at run time. A clear refusal is needed.

**Sleep/wake pass (Sheepdog01's call):**
- pack descriptors aren't reopened after wake;
- failed reads still evict;
- the suspend callback races a texture load in flight;
- `geoasync` stalls for good after a sleep with a read in flight.

**levelcompact:** a test switch with no measured gain (b87).
- Negative texture coordinates are encoded as signed shorts that the GE reads as unsigned, giving wrong textures.
- The float passes (shadows, lights, cut pieces, stencil layers) no longer match its 16-bit walls.
- Leave it off.

**Diagnostics:**
- The frameend line's see-through numbers are always 0.
- "HUD quads" includes the see-through flush (now labelled).
- The texture "worst frame reading" is frozen at the start-up preload.

**Performance:**
- Read budgets are in bytes, but the cost is per read (2.2–2.5 ms each).
- `allocate()` can cost O(n²) on a full cache.
- Every model draw does a linear `psp_geo_generation` scan.
- The ME's mixer is held for zero-voice blocks.

**Minor:**
- The launch menu's options page overruns the 34-row screen.
- keep.txt has three different size limits.
- The soften pass samples past its buffer (off by default).
- The profiler ring breaks with `PSP_PROFILE_FRAMES` 2 (debug only).
- The cluster surface-list path is broken for the reimport-only case.

**Not a bug, a choice:** the swap isn't synced to the vertical blank (tearing), to save the wait with two buffers.

**ARK-4:** its L+R+SELECT/START+DOWN exit combos skip our exit callback entirely. select+down (crouch) + select+L (grenade) + R (fire) can trigger it in play.
