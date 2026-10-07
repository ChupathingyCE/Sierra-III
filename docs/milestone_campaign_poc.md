# Halo: Combat Evolved on the PSP-1000: campaign proof of concept

**Milestone, October 7, 2026.** The Library, Halo CE's seventh campaign mission, runs on a stock-memory PSP-1000: intro cinematic, Flood combat, grenades, death and checkpoint reloads, several checkpoints in. Our longest unbroken session is about six and a half minutes of game time, with no crash.

## What this is
- A port of the **Halo CE Xbox decompilation** (halo-ce-universal) to the original PSP, built from scratch for the PSP's hardware: its graphics chip (the GE), its 32 MB of RAM and its second CPU, the Media Engine.
- Target: a **PSP-1000**, the original 2004 model: 333 MHz, 32 MB of RAM, about 24 MB of it for the game. It runs custom firmware (6.61, ARK-4), which homebrew needs. No overclock was used for this run.
- All game data comes from **my own Xbox disc**, rebuilt for the PSP by tools that ship with the port. **No game files are distributed.**
- Started **September 30, 2026**. This milestone is build 101, a week in.

## What works
- **The Library (c20) loads and plays:** the opening cinematic, then the first Flood encounters, shooting, grenades, death and reload from the last checkpoint, and several checkpoints further in, through the main elevator room.
- **Sound**, mixed on the PSP's Media Engine, a second CPU normally reserved for Sony's own media decoding.
- **A renderer written for the GE** (the PSP's graphics chip): textures re-baked to the PSP's 4- and 8-bit palette formats, the level's lightmaps, detail maps, decals, dynamic lights, shadows, the HUD.
- **Streaming:** the level and model geometry, the textures and the sounds stream from the Memory Stick as they're needed, because a campaign mission is far larger than the PSP's RAM (The Library's map file is 120 MB).
- The **multiplayer maps** (Blood Gulch and others) run too, at roughly 12–26 fps depending on the view.

## Where it stands (measured on the PSP-1000)
- **Frame rate in The Library: ~6–7 fps typical, up to ~10.** Playable as a demonstration, not yet as a game. The end goal is 30 fps, with 20 as the fallback.
- The game's logic alone takes ~23 ms per tick (~33 ms in heavy fights), against 33 ms for a whole frame at 30 fps, and that's before any drawing.
- Memory is the crux. The CPU and the graphics chip share one memory bus, and the CPU spends about half its time waiting on memory.

## Performance: where the time goes
Almost all the heavy lifting runs on the main CPU today. Besides the renderer's buffer work and the Memory Stick streaming, very little has been optimized yet, and most of the PSP's hardware is still idle. Measured on the PSP-1000 in The Library:

- **The game tick is all main CPU.** Objects take 28–39 ms a tick in a fight. Each character costs about 1 ms per tick, most of it animation and movement; the Flood come in crowds. The level's ~60 doors, lifts and devices add 7.5 ms, and the AI 5–8.5 ms. That's already over the 33 ms a 30 fps frame allows, before any drawing.
- **The CPU spends about half its time waiting on memory.** The tick's cycles are ~56% memory stalls, mostly instruction-cache misses: Halo's code is far bigger than the PSP's 16 KB instruction cache.
- **The renderer's CPU side** (deciding what's visible, cutting triangles at the camera, sorting, building the graphics chip's command lists, skinning characters) takes 30–120 ms a frame. The graphics chip itself sits idle most of the frame, waiting for the CPU.
- **The CPU and the graphics chip fight over one memory bus.** On Blood Gulch, the graphics chip's reads from main RAM cost 11–29% of the frame (measured by moving its work to video memory only).

**Where the headroom is** (estimates, not yet measured):
- **The Media Engine**, a second 333 MHz CPU with 2 MB of its own memory, is ~6% busy (sound mixing only). Animation decoding, particles and sound decoding could move there.
- **The VFPU**, the PSP's vector unit, is barely used yet: today it only does matrix products.
- **The graphics chip can do more of the work.** It can skin characters in hardware (blended bone matrices), reuse stored command lists, and read compact vertex formats directly. That cuts both CPU work and bus traffic.
- **Code layout:** keeping hot code together and cold code out of the way (profile-guided ordering) attacks the instruction-cache misses directly.
- **The 400 MHz overclock** isn't used yet.

## Known issues
- Geometry and textures pop in at area thresholds and in the large elevator room: the streaming caches are too small for the biggest spaces.
- Occasional sound cut-outs in busy scenes, for the same reason (the sound cache).
- Memory headroom is nearly gone: The Library fits the PSP-1000's tag memory with 12 KB to spare, and the heap peaked within 12 KB of its limit.
- Switching to the mission's later level sections hasn't been reached on hardware yet.
- The other nine campaign missions are 2.5–3.5 MB too large for the PSP-1000's memory. The Library fits first because it has no Marines or Covenant to carry.

## What's next
1. Performance: cut the graphics chip's main-RAM reads, starting with model vertices, then the game tick (character animation, idle level machinery, how the code is laid out).
2. Memory: find ~4 MB across the build (debug data out, compact formats, animation streaming) to fit every mission and grow the streaming caches.
3. Every campaign mission loading and playable.

**Goal: a playable campaign on the PSP-1000 by November 15, 2026, Halo: CE's 25th anniversary.** Multiplayer between PSPs is the stretch goal.

## Thanks
- **Claude** (Anthropic), my partner on this from the first feasibility question: the code, the tools, the hardware tests and the debugging. None of this would have happened without it.
- The Halo CE decompilation project (halo-ce-universal), without which none of this exists.
- **Invader** (SnowyMouse), the Halo map toolkit the asset pipeline is built on.
- **mcidclan**'s Media Engine core for the PSP.
- The **pspdev** toolchain and the PSP homebrew community.
- The **PPSSPP** emulator, used for testing between hardware runs.
