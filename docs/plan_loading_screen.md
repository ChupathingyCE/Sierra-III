# Plan: the loading screen, rebuilt as a set piece

Sheepdog01, 2026-10-14: preserve the original loading screen and its chant, but not where it lives now; rebuild it for our own loading, and use it as padding so the level can load and settle before play starts. Not scheduled yet: campaign speed work comes first.

Labels: [M] PSP, [E] emulator, [D] code, [U] inference.

## 1. What the Xbox does (`source/interface/progress_bar.c`) [D]

- **When:** only while the Xbox copies a map from the disc to its hard-drive cache. `game.c` runs the screen inside that copy loop (`cache_files_precache_is_copying_map`). The PSP never copies a map, so the screen never starts, and neither does the chant (b107's coverage agrees [E]). Today the PSP shows a debug line instead (`psp_menu.c`: "Loading …").
- **The chant:** `compressed_monk_tone`, 18,396 bytes of Xbox ADPCM at 22,050 Hz, typed into the decompiled source. It plays as **four looping copies at different pitches** (`SetPitch` −4681, −6386, −3999, −8773: roughly 1.1, 1.6, 1.0 and 2.1 octaves down, if the units are 1/4096 octave [U]).
- **It follows the progress, so it's a crescendo, not a loop:** each voice swells in and back out over its own part of the load (voice 1 over 0–100%, voice 2 from 40%, voice 3 from 50%, voice 4 from 55%), with a `sin^0.3` volume curve (`progress_bar_render`, `ranges` and `volumes`).
- **The picture:** the frame brightness follows `sin(progress·π)^0.9` (it fades in, then out). There are 40 layers of drifting clouds 5 units apart (`num_layers`, `delta_z`) behind a mask texture that slides with the progress, over a captured still of the last frame (`capture_frame`) or the map's `loading.tga`. The progress is smoothed and given a slow wobble.

## 2. The shape on the PSP

1. **The sound comes from the player's disc, at bake time**, like the XMB's menu music (`bake/lib/snd0.py`).
   - The same bytes should be in the disc's `default.xbe` [U: check against the disc image on the X3D]. The bake finds them, decodes them, and renders the four pitched voices as four short stems (`halo/loading/voiceN`).
   - The engine's copy is compiled out on the PSP: −18 KB in the EBOOT and in RAM, and no disc audio in any build zip.
   - Rendering the pitches at bake time also sidesteps the mixer: the PSP's `IDirectSoundBuffer_SetPitch` is a stub (`psp_dsound.c`).
   - If the bake gets only a maps folder (no XBE), the loading screen is silent.
2. **The mixer plays the stems with the Xbox's volume curves**, driven by the screen's progress. That's four short PCM voices at the Media Engine's audio priority, and the stems are freed when the screen ends.
3. **The picture is rebuilt on the GE** from `progress_bar.c`'s geometry and timing. It's checked against ChupathingyCE or the Xbox, never web screenshots (the remasters differ). It's a restoration, so it's on by default, with a keep word to skip it.
4. **The hold and the settle:**
   - The map loads under the screen. The game doesn't tick until the screen ends, so scripts, AI and the opening cutscene all start at tick 0 as they do now.
   - Once loading is done, the screen's progress runs on as a **settle phase**. It warms what the opening needs first: the first view's geometry, textures and sounds (the stick stalls in b122's tests are the reason). The crescendo plays out in full even when loading is quick.
   - Then the screen fades out, and the first tick runs.

## 3. What to settle before building it

- **The frame lock's clock:** it counts game time in screen refreshes, so held refreshes must not turn into catch-up ticks when play starts. Reset it as the screen ends.
- **How long the settle runs:** a fixed minimum (so the chant can breathe), "until warm", or both, capped either way. It's Sheepdog01's feel call, tried as an A/B.
- **Multiplayer:** a client can't hold a server's game. It can settle only before it spawns, so the screen there ends when the server says play.
- **Memory:** the stems and the screen's textures must fit beside a loading map. They're freed before the first tick.
