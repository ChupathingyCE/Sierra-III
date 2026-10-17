# Review: build 137

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). b137 is b136 (`review_b136.md`) plus the release polish of 2026-10-17's morning (Sheepdog01, 02:01): a faster start-up, the loading screen's chant and a settle before play, the multiplayer fixes verified on Milenko's `[C]` servers, the glow clamped as the Xbox does, and two fixes the reviewer found. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

| Change (commit on `w137`) | What | Kind | Default | Switch |
|---|---|---|---|---|
| start-up H6 (`a10afbd`) | Paths on the Memory Stick joined as written, not found by a scan of each folder | start-up | on | `pathscan` |
| startup2 (`4924489`) | The log batched from launch; the campaign makes and lists the game variants only when asked | start-up | on | `logeachline`, `bootvariants` |
| loading (`620cb6d`, `1affed0`) | The load's progress and time; the tag read in 256 KB pieces; the Xbox's chant; the settle | front end, restoration | on | `loadwhole`, `chantoff`, `settleoff`, `settleN`, `loadingoff` |
| wakecheck's owners (`387d76f`) | WT's copy-owner table on the heap, not in `.bss` | memory | on | — |
| the VRAM-room fix (`f92eef1`, from `0299f6c`) | A model copy's room measured by its own size while it waits in quarantine | fix | on | — |
| mp135 to `ae3329c` (`e9209e4`) | The errno race, KCP's 64 KB reserve and NULL checks, onlinejoin's underscore | multiplayer only | on in that build | `kcpreserveN` |
| glowclamp (`220a387`) | The glow clamped to the base, as the Xbox's combiner does, on the showcase map | restoration | on | `glownoclamp`, `glowadd` |
| switch defines (`6c8415a`) | `PSP_DEBUG_TOOLS` and `PSP_TICK_STEPS` reach every unit | build | — | — |
| levelvram (`d995716`, from `208a594`) | The level's vertex streams share vrammodels' VRAM room | speed, exact | on | `nolevelvram` |
| glyphvram (`550e9c0`, from `b894cf6`) | The button glyph sheet run-length coded in the program, unpacked once into VRAM | memory | on | — |
| loadingscreen (`290fdaf`, from `ec7e08a`) | The loading screen's picture: the Xbox's zooming, fading trail of the last frame; the settle's veil | front end, restoration | on | `loadingplain` |

## §Start The start-up

**H6: paths as written.** `platform_translate_path` (xbox_files.c) turned each Xbox path into a stick path by listing every parent folder and comparing names without case. FAT is already case-blind, so it now joins the parts as written; each drive folder is made once (a bitmask).
```c
if (psp_path_scan <= 0) { length = strlen(resolved); snprintf(resolved + length, sizeof(resolved) - length, "/%s", component); continue; }
```
PSP-1000 [M] (39021437349, two runs a side): the saved-game check 26.0 s → 6.9 s, the variants' comparison 11.2 → 2.05 s. `pathscan`: the scan, as before.

**startup2.** Where the remaining 6.9 s went (a delegate's read of the same run [M/D]): ~390 stick calls for the profiles, 2.0 s of them waiting for the stick's lock, and the log. Before game time 0 every log line was an open, a write and a close, and the heartbeat, above the game's priority, wrote its 81-line state dump that way at the first frame (3.0 s waiting on the stick).
- The log batches from the heartbeat's start (`psp_platform.c`); a crash still writes the batch, a hang loses at most a second.
- The campaign build no longer makes the 26 multiplayer game variants at start-up and lists them only once something asks (`FIXES_FRONT #bootvariants`: `ui_widget.c`'s check, `playlist_profile.c` `psp_variants_asked`, `saved_game_files.c` `enumerate_default_profiles`). The multiplayer build is unchanged.
Left for later [D]: the multiplayer build's variants read twice, `hdmu.map` rebuilt and read back three times, the free-space count (0.66 s for a 40 MB check).

## §Loading The loading screen

`docs/plan_loading_screen.md`. A load is one blocking chain on the main thread; nothing drew during it.
- **Progress** (`psp_loading.c`): begin and end bracket the old map's disposal through the new map's load (`main.c`, both paths); the tag data, read in 256 KB pieces (`cache_files.c`; `loadwhole`: one read), reports each piece. loading1 [E]: play and both seeds identical with the pieces and with `loadwhole` at all 9,000 ticks.
- **The chant:** the Xbox's own `progress_bar.c` setup played `compressed_monk_tone` as four looping buffers at four pitches (−4681, −6386, −3999, −8773 in 1/4096 octave from 48 kHz [U: the XDK's unit]: 21,738, 16,290, 24,397, 10,876 Hz), each swelling over its own range of the load, `sin(tπ)^0.3` of 3,500–4,500 mB over −5,000. The PSP's buffers are null objects, so each voice is a stream with the tone queued as every packet it holds (`psp_dsound_loop_start`): over a minute of loop with no completions, which a blocking load never runs.
- **The settle** (`psp_main_settle`, main.c): after the load, the level's first view is drawn with no tick, as `main_loop` draws a frame, until the texture cache reads nothing for three frames in a row and the screen's progress has played out, capped at 3 s. The engine's frame-lock reset after a load drops the time. Campaign only (a game engine's frames wait for its third tick).
- **The picture** (the renderer's `loadingscreen`, `review_loadingscreen.md`): the Xbox's feedback trail of the frame shown when the load began, zooming and fading to black; the settle's frames under an opaque veil that ends on black. From the menu the menu has already faded to black, so the trail has nothing to carry; between levels it carries the level's last view. The `loading.tga` band needs the bake to carry the disc's picture: b138.
- **The settle at boot** hung on its first frame (loading2: a10, b30 and Blood Gulch from `init.txt` sat at game time 0), so it runs only on the menu's and a level's own path (`de63bea`). loading3 [E]: the menu into a10 plays identically with and without it at all 29,825 ticks, both seeds; it read no textures there (a10's first view was already in VRAM from the learned set), so what it buys on the PSP is still to be measured.

**For Sheepdog01:** the chant's 18 KB are disc audio, typed into upstream's decompiled source, so every Sierra-III EBOOT has carried them in `.data`; b137 plays that copy and adds nothing. The 06:00 report said b137 would bake it from the disc and compile the copy out: that isn't built, and compiling out alone would silence the chant. Before a public release: keep it as upstream does, or bake it (b138).

## §VRAM The VRAM-room overlap (the reviewer's 05:30 catch)

`vm_note` released a model's copy on a size change and then set `e->bytes` to the new size while the old copy waited in quarantine; `vm_place` measured gaps by `e->bytes`, so a smaller copy could be placed over one the GE might still read. Each copy now keeps its own size (`held`); `vrammodels` is on by default (600 KB), so b136 shipped the bug.

## §Glow The clamp

`review_glowmaps.md`, "The clamp". On ui.map's showcase only: `min(base × lighting + glow, base)`, a `GU_MIN` pass with the base, out of line and cold; `multipurpose_pass` +168 B against b136. glowclamp1 [E]: by eye the ring matches ChupathingyCE's.

## §Levelvram The level's streams in the models' VRAM room

`review_levelvram.md`. The level's vertex streams are ranked with the model buffers by reads a byte and share the 600 KB room; released on rewrite, free and arena move. Exact (levelvram4: d20's play identical at all 6,000 ticks) [E]. PSP-3000 [M]: c20 frame −7% (39025354526), d20 −7.6% and −5.0% past the opening (39025400180). A PSP-1000 pair goes in the release check.

## §Glyphs The glyph sheet in VRAM

The controls' 32 KB glyph sheet left `.rodata`: run-length coded in the program (14 KB, `tools/psp_glyphs.py`), unpacked once into VRAM at 0xD0000; the texture cache starts 32 KB later. Heap +16 KB in both builds (the multiplayer build's Prisoner came within 4 KB of its heap's end on mp135). glyphvram1 [E]: the menu draws as before to the pixel.

## §MP The multiplayer fixes (multiplayer build only)

`review_mpb4.md` 8–11. Verified joining and playing `[C] Slayer` on both PSPs (39017504866, 39024064182) [M], and on `w137` `de63bea` (39030473538, PSP-3000: both runs to the wall, no drop, heap top 3,532 and 3,600 KB). Heap at the final code: 3,668 KB (b136 3,652).

## Release check

Lab 39034419300, strict with b137's own profile (`campaign-b137`, 13 runs at `290fdaf`: lab 39031784260, 39031780754), against b136 as built (`e41a305`) [E]:
- **Play and both random seeds identical to b136 at every one of 12,000 ticks** on d20, a50 and c20 (`tools/statecmp.py`); the whole-state hashes differ in bookkeeping only (the decal vertex cache, contrails, particles, looping sounds, actors' and objects' allocations).
- b30 and c40 shots right; the main menu's showcase and a10's prompts drawn; strict clean on every run.
- The menu into a10 by the menus: the saved-game check 115 ms, the load 1,032 ms, the settle 4 frames (0 texture reads), then play.
- The multiplayer build plays Blood Gulch (game time 12,008).

The zip's commit (`b7524d5`) adds only the build number on its own line: the code differs from the release check's in 3 bytes (the number's immediates), and the profile matches every function (strict).
