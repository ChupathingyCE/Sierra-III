# Halo PSP port: audio path design

Research, 2026-10-02. Scope: how Halo's sound system works, what Blood Gulch's sound data looks like, and a design for baked PSP audio. Prototypes are in `/home/claude/research/audio/`.

**How numbers are labelled.**
- **[M]** measured: by running something here, or quoted from the hardware runs in the brief.
- **[C]** computed exactly: from the map file or from format arithmetic.
- **[D]** documented: in source code, an SDK, or PPSSPP (whose SAS timings were measured on a real PSP).
- **[E]** estimated: needs a hardware test or a profile.

---

## 0. Summary

| Question | Answer |
|---|---|
| Is sceSas usable from homebrew? | **Yes, in the SDK.** `pspsascore.h`, `libpspsascore.a` and a working user-mode sample (`psp/sdk/samples/sascore`) are in `/opt/psp/pspdev` [D]. Our test EBOOT runs it in PPSSPP [M]. It has not been run on Sheepdog01's 6.61 PSP-1000 yet. |
| What does sceSas run on? | **The Media Engine** [D: PPSSPP `sceSas.cpp` "runs on the Media Engine"; psyz PR #165 "sceSas runs on the separate PSP CPU Media Engine"]. The ME cost model below was measured on hardware by PPSSPP. With 32 VAG voices at 22 kHz, a 256-sample grain takes **~2.8 ms of every 5.8 ms (~48% of the ME) at 333 MHz** [D model; M in PPSSPP]. |
| **Conflict to decide** | The feasibility plan says "the ME is ours (libme-core)". **That takeover removes Sony's ME firmware, and SAS with it.** Baking to VAG works for both routes. The output backend is the open decision; see §3a. **Update 2026-10-03 (§6): the safe-task route lets sceSas and bounded custom ME jobs coexist, so the choice is no longer either/or.** |
| Baked format | **VAG (PS-ADPCM), mono streams.** It costs 1.6% more bytes than Xbox ADPCM at the same rate [C]. Its re-encode quality equals IMA's on the same input: median SNR 29.2 dB against 27.8 dB over 40 Blood Gulch permutations [M]. Recommended bake: 22.05 kHz maximum, SFX in mono, music in stereo. That makes Blood Gulch **9.95 MiB instead of 11.66 MiB** [C]. |
| Memory | Not every sound fits in memory: Blood Gulch is 7.2–10 MiB even baked [C]. **Keep Halo's sound cache.** Size it at 2 MB minimum, ideally **4 MB in the volatile partition** (the Xbox size; the data is reloadable, so it suits that partition) [E]. 512 KB thrashed [M]. |
| Streaming | Halo already streams long sounds (music, dialogue, ambience) through the sound cache, in ~64 KB chunks with one chunk of look-ahead [D: `update_channel_for_looping_sound`]. No new streamer is needed. The PSP layer adds **ring-buffer voices** so that queued chunks play without gaps. This is validated: in PPSSPP, ring output is bit-identical to playing the whole buffer [M]. |
| CPU | Expected **~1.5–4.5 ms/frame** in total [E], against the 16–22 ms measured during cache thrash [M]. The game's own sound logic dominates, with its obstruction raycasts. Mixing and decoding are off the main CPU. |
| Hardware test ready | `sastest/EBOOT.PBP` measures four things on the PSP-1000: module load and memory cost, SAS cost and main-CPU loss at 0–32 voices, and ring streaming. Output from ring and direct playback is saved so it can be compared. |

---

## 1. How Halo's sound system works (Xbox decomp)

### 1.1 Data: tags

| Tag | Struct (source) | What it holds |
|---|---|---|
| `snd!` | `sound_definition`, 0xA4 (`sound/sound_definitions.h`) | `sound_class` (51 classes, `sound_classes.c`); `sample_rate` enum (0 = 22050, 1 = 44100); `encoding` (0 mono, 1 stereo); `compression` (0 PCM, 1 Xbox ADPCM, 2 IMA, 3 OGG on PC); min/max distance, cone, gain/pitch/skip modifiers by `scale`; promotion sound; **pitch ranges** |
| pitch range | 0x48 | natural pitch, bend bounds, `playback_rate`, `actual_permutation_count`; **permutations** |
| permutation | 0x7C | name, skip fraction, gain, `next_permutation_index` (**chains**: long sounds are split into ~64 KB chunks linked by this index), `samples` tag_data (size and file offset of the sample data in the map file), mouth data (lip sync at 30 Hz), subtitle |
| `lsnd` | `looping_sound_definition`, 0x54 | up to 4 tracks (start / loop / stop / alternate sounds, fades) and 32 detail sounds (random one-shots around the listener). Music is an `lsnd` whose loop sound has class `music` |

On the Xbox all sample data is **Xbox ADPCM**. Each block is 36 bytes holding 64 samples per channel; stereo blocks are 72 bytes, interleaved [D: `sound_dsound_xbox.c`, `dsound_sdl.c`]. `sound_new_impulse` refuses anything that is not Xbox ADPCM, mono 22 kHz or stereo 22/44 kHz (`sound_manager.c:2394`) [D]. **The bake must keep that check passing, or patch it.**

### 1.2 Runtime pipeline

| Stage | Function (file) | Per frame / per tick |
|---|---|---|
| Game requests | `object_impulse_sound_new`, `unattached_…`, `scripted_sound_new`, `game_looping_sound_new` (`game_sound.c`) | When events happen. `sound_new_impulse` rolls skip and pitch, checks `source_audible`, picks pitch range and permutation, **starts a cache load at once** (`_sound_cache_sound_request(load=TRUE)`), and delays by speed of sound when the source is more than 250 ms away |
| Ambience and music | `game_sound_update` (`game_sound.c`), per tick | BSP cluster sound environment, background music `lsnd`, potentially-audible-set (PAS) culling of object looping sounds |
| `sound_render` (`sound_manager.c:3807`), once a frame | `refresh_listener` | Listener matrix per local player; underwater splash |
| | `process_looping_sounds` | Track state machines; detail sounds |
| | `refresh_sounds` | Per live sound: `track_proc` (follows the object), **`source_audible` → `compute_sound_obstruction` → `collision_test_vector` raycast** camera→source (cached per tick by the port), inaudible fades, dialogue ducking (`nondialog_gain` → 0.7) |
| | `prioritize_sounds` | Per sound: cache request with reference; **`sound_find_channel`**, which builds a channel summary (O(channels)) or a best channel (O(channels), using priority and then distance). Preempts lower-priority or farther sounds. On a cache miss, the class's mode decides: **discard** (the permutation is forced next time) or **postpone** |
| | `update_channels` | Per channel: fade; **3D: transform to listener space and call `set_channel_location` (DirectSound3D position, velocity, cone, I3DL2 obstruction/occlusion)**; 2D: linear distance attenuation; gain and pitch go to `set_channel_properties`; looping tracks queue their next permutation; lip-sync aperture |
| Platform | `platform_sound_dsound` (`sound_dsound_xbox.c`), a 13-function `sound_platform_definition` | Each game channel is a DirectSound **stream** fed 2304-byte ADPCM packets straight from the sound cache (4 in flight). The MCPX hardware does the mixing, HRTF 3D, I3DL2 reverb and Doppler |
| Sound cache | `xbox_sound_cache.c` on `lruv_cache` | 1024 pages × 4 KB = **4 MB** [D]. LRU of variable-size blocks. Blocks being played (software or hardware reference counts) or touched this tick cannot be evicted. Loads are async `cache_file_read`s from the map file |

### 1.3 Channels and voices

`sound_preferences.c` sets the default virtual channels [D]. Channel type flags: 3D bit 0, stereo bit 1, 44k bit 2, compressed bit 3.

| Pool (flags) | Virtual channels | Hardware voices ("actual") | Takes |
|---|---|---|---|
| 8: mono 22k 2D | 9 | 10 | unspatialized mono |
| 9: mono 22k 3D | **46** | 51 | every spatialized sound |
| 10: stereo 22k | 9 | 10 | stereo 22k (first-person, some impacts) |
| 14: stereo 44k | 9 | 10 | music, stereo ambience |
| **Total** | **73** | 81 | |

Class limits cap instances at 4–6 per definition and 1–4 per object [D]. **How many voices actually play together in combat has not been measured.** From the class limits, a 4–6 combatant firefight gives an estimated **20–35** [E]. It can be measured on the Linux port: the `debug_sound_channels` flag, or a counter of `sound_channels[i].sound_index != NONE` per frame.

### 1.4 Why `sound_render` was expensive on the PSP

The measurements come from the brief [M]: 3–4 ms a frame while walking, and 16–22 ms during combat thrash ("SOUND CACHE BLOWN" 3× in 20 s with 512 KB). The causes, from reading the code [E]:
1. **The Linux DirectSound shim's work counts toward this.** With `audio.enabled = false` (`HALO_NO_AUDIO`), `dsound_sdl.c` still decodes every submitted ADPCM packet to PCM. It does this inside `IDirectSoundStream_Process`, called from `sound_render`, and mallocs for each packet. It also runs a 48 kHz float mixer thread (`silent_clock_thread`) on the same CPU. **None of this exists in the design below**, because decode and mix move to the ME.
2. **Raycasts.** Every absolute-spatialized sound gets one `collision_test_vector` from the camera per tick (`compute_sound_obstruction`).
3. **O(sounds × 73 channels) loops** in `sound_channel_summary_build` and `sound_find_best_channel`. These also call `source_distance_squared` for each candidate.
4. **Thrash.** Blood Gulch's bullet-impact permutations are 22 kHz *stereo*, ~34 KB each [C]; ten of them fill a large part of 512 KB.
   - Each miss is a Memory Stick read: 1.75 ms per random 4 KB, plus transfer [M].
   - Postponed sounds retry `lruv_block_new`, a full scan, every frame.
   - On allocation failure the game writes `lruv_debug_to_file("d:\\stabbed.txt")` from the main thread, up to once every 10 s. **This should be compiled out on the PSP.**

---

## 2. Blood Gulch sound census

Script: `audio/snd_census.py` (struct offsets from the decomp headers). Raw output: `census_bloodgulch.txt`, `census_ui.txt`, `*_sounds.json`.

| | bloodgulch.map | ui.map (main menu) |
|---|---|---|
| `snd!` tags / `lsnd` tags | 315 / 14 [C] | 29 / 2 [C] |
| Permutations (chunks) | 916 [C] | 259 [C] |
| Logical sounds (chain heads) / multi-chunk | 899 / 4 (all `countryday2` ambience loops) [C] | 83 / 4 (all title music) [C] |
| Sample data | **11.66 MiB**, file offsets 0x39D800–0x2304D40, before the tag data at 0x2304E00 [C] | 11.83 MiB (11.03 MiB of it music) [C] |
| Total audio | 826.8 s [C] | 296.2 s [C] |
| Largest chunk | 66,240 B (chunking is ~64 KB; none exactly 65,536) [C] | 65,520 B [C] |

**Blood Gulch formats** [C]: all Xbox ADPCM.

| Format | Permutations | KiB | Seconds |
|---|---|---|---|
| mono 22,050 | 834 | 8,656 | 714.6 |
| stereo 22,050 | 61 | 2,147 | 88.6 |
| stereo 44,100 | 21 | 1,140 | 23.5 (the `countryday2` ambience) |

**By class (top)** [C]:

| Class | KiB |
|---|---|
| projectile_impact | 1,866 |
| object_impacts | 1,667 |
| scripted_effect (multiplayer announcer) | 1,340 |
| unit_footsteps | 1,160 |
| ambient_nature | 1,140 |
| particle_impacts | 1,025 |
| vehicle_engine | 681 |
| weapon_ready | 638 |
| weapon_fire | 495 |

**Durations** [C]:
- Median 0.64 s, p90 1.74 s, p99 3.70 s, maximum 5.34 s (`sniper_posing`).
- 845 of 916 permutations are under 2 s.

**Biggest tags** [C]:

| Tag | Size |
|---|---|
| `countryday2` | 1,140 KiB, 44k stereo, 21 chunks |
| `bullets_projectile` | 335 KiB, 22k **stereo** |
| `warthog_engine5` | 284 KiB |
| `chief\deathviolent` | 215 KiB |
| `dirt_skid` | 199 KiB |

Blood Gulch is a multiplayer map. **Campaign maps add music and scripted dialogue.** Music follows `ui.map`: 44.1k stereo Xbox ADPCM in ~64 KB / 1.32 s chunks, about 49.6 KB/s [C]. No campaign map is on disk here, so its census is still to do.

---

## 3. PSP audio design

### 3a. Output layer: three options

| | **A. Sony sceSas** (ME runs Sony's firmware) | **B. Our mixer on the ME** (libme-core takeover) | C. Our mixer on the main CPU |
|---|---|---|---|
| Available | In the SDK: header, lib, sample [D]; runs in PPSSPP [M] | Needs libme-core raw-mode work (the feasibility plan) | Now |
| Voices | **32 maximum per instance**, mono only [D] (PPSSPP supports one instance) | Any number | Any number |
| Decode | VAG (and PCM, ATRAC3) on the ME | VAG decode in our ME code | On the CPU |
| ME cost, 32 voices, 22k sources, 44.1k out | **~2.8 ms per 5.8 ms grain ≈ 48% at 333 MHz** [D model; M PPSSPP] | ~13–17% at 25–40 cycles/sample/voice [E] | none |
| Main CPU | One blocking call per grain (5.8 ms) on an audio thread. The thread should sleep while the ME works [D: PPSSPP models a wait] (to be checked on hardware [E]) | Queue writes | **2.6–5.7 ms/frame** at 24–32 voices, 44.1k out [E] |
| Features | Pitch (≤4× of 44.1k), per-voice L/R and send volumes, ADSR, one global reverb (9 presets), end flags | Whatever we write (no reverb at first) | Same as B |
| Conflicts | **Rules out using the ME for raycasts or other jobs** | Shares the ME with the other planned ME jobs | Costs the bottleneck CPU |

**Recommendation.**
1. **Bake VAG regardless.** Both A and B consume it, and it decodes cheaply in software.
2. Write the platform layer against a small internal voice API (`psp_voice_play`, `psp_voice_stream`, `set_gain_lr`, `set_pitch`, `stop`, `ended`), so the backend can be swapped.
3. **Implement the SAS backend first.** It is the shortest path to working audio and can be tested in PPSSPP.
4. Switch to B only if the ME takeover lands (v7 ME results, plus a profile showing that ME raycasts pay off). The mixer is then an ME job: VAG decode, linear resample, two gains.
5. **This needs Sheepdog01's decision**, because the 2026-10-01 "ME is ours" decision and the audio note "sceSas, on the Media Engine" contradict each other.

### 3a′. SAS backend specifics

- **Audio thread** (priority above the game):
  - Loop: apply queued voice commands, call `__sceSasCore(core, buf[i])`, then `sceAudioOutputPannedBlocking(ch, …, buf[i])`; double-buffered.
  - Grain 256 = **5.8 ms**; the game-to-speaker latency is ~2 grains plus the frame [C].
  - All `__sceSas*` calls stay on this thread. The game thread posts commands to a lock-free single-producer, single-consumer ring.
- **Modules:** `sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC)`, then `PSP_MODULE_AV_SASCORE`, then `__sceSasInit(grain 256, 32, STEREO, 44100)`. The `SceSasCore` struct is 3,616 B and must be 64-byte aligned [D].
- **Pitch:** `SAS pitch = rate × game_pitch / 44100 × 4096`, clamped to 1–0x4000. A 22,050 Hz source at pitch 1.0 is 0x800 [D].
- **3D:**
  - The game already transforms 3D sources into listener space (`update_channels`).
  - `set_channel_location` and `set_channel_properties` keep the state. Once per frame the layer computes, as `dsound_sdl.c::spatialize` does:
    - the DirectSound inverse-distance rolloff between min and max distance;
    - an equal-power L/R pan from the listener-space direction;
    - × gain × cone gain × (1 − obstruction/occlusion) as a gain.
  - Output is `__sceSasSetVolume(L, R, sendL, sendR)` with 0–0x1000 per channel. Send = gain × the class's `wet_gain` for reverb.
  - **No per-voice filters**, so obstruction, occlusion and underwater are gain-only. There is no Doppler; pitch could fake it.
  - **Volume changes are ramped over ~4 grains** in the audio thread, to avoid zipper noise from 30 Hz steps.
- **Voice mapping:**
  - Each game channel maps to one SAS voice; a stereo channel takes 2 voices, hard left and hard right.
  - Override `default_sound_preferences.virtual_channel_counts` to fit 32. With SFX baked mono: **2D mono 4, 3D mono 24, stereo-22k 2 (4 voices), stereo-44k 0**.
  - Halo's own priority and distance preemption decides what plays.
- **End detection:** `get_channel_state` reads `__sceSasGetEndFlag` (refreshed after each grain) [D].
- **Stopping:** key-off with instant release. Keep `sound_cache_sound_hardware_lock` until the end flag is seen and one more grain has passed, because the ME may still be reading the data.
- **Reverb:** map the cluster's `sound_environment` (I3DL2 room size and decay) to a SAS preset (off, room, small, medium, large, hall…). Costs ~0.64 µs/sample at 222 MHz, about 2% of the ME [D].

### 3a″. Gapless playback: ring-buffer "stream voices"

A SAS voice plays one contiguous VAG buffer; nothing can be queued behind it [D]. Halo queues the next permutation for:
- looping tracks (random loop permutations, engine loops);
- chains (music, long dialogue).

For these channels the layer uses a **ring voice**:
- An 8 KB ring (4 × 2 KB segments, 3,584 samples each), set to loop with flags 6 (loop start) at block 0 and 3 (loop end) at the last block.
- The audio thread copies VAG blocks from the cached permutation(s) into whichever segment the voice has passed.
- The play position is **tracked by counting grains** (position += pitch × grain each grain), with a 512-sample margin.
- After writing a segment, call `sceKernelDcacheWritebackRange` (the ME is not cache-coherent).
- This is what `sastest` implements. In PPSSPP the ring output is **bit-identical to playing the whole buffer directly** [M]: 1 differing sample of 308,480, the first one. The ring needs ~37 refills for 6.1 s of sound [M].

Impulses play directly from their cache block, with no copy; 895 of Blood Gulch's 899 logical sounds are single chunks [C]. Looping tracks, such as engine loops or `countryday2`, use ring voices even when each permutation is a single chunk, because the next permutation is queued behind the current one.

### 3b. Baked format

| Property | Xbox | PSP bake (recommended) |
|---|---|---|
| Codec | Xbox ADPCM, 36 B / 64 samples / channel = 0.5625 B/sample | **VAG**, 16 B / 28 samples / channel = **0.5714 B/sample (+1.6%)** [C] |
| Channels | mono, or interleaved stereo | **mono streams only.** SFX are downmixed (3D spatialization drops stereo anyway). Music stays stereo, stored **planar: [L stream][R stream]**, each ending in a flag-7 block; the split is size/2 |
| Rate | 22,050 / 44,100 | **22,050 maximum.** Optionally 16 kHz or 11 kHz for chosen classes, with the real rate kept per permutation, e.g. in `sample_buffer_size` (unused at runtime [D: no reader in the source]); the definition's `sample_rate` enum stays 22k |
| Chunking | ~64 KB chunks chained by `next_permutation_index` | Same scheme, ≤64 KB (VAG stereo at 22k ≈ 2.6 s, mono ≈ 5.2 s) |
| Prediction state | per block header | **The first block of every permutation uses filter 0** (no prediction), so a permutation decodes correctly whatever played before it. Implemented as `vag_encode(first_filter0=True)` |
| `compression` field | 1 | New value `4 = psp_vag`, plus fixups to `sound_new_impulse` and `sound_valid_for_channel`, so that an unbaked map fails loudly. (Keeping 1 also works but hides mistakes) |
| Placement | Sample data in the map, outside the tag window | Same. Invader-fork bake writes VAG blobs 16-byte aligned (cache blocks are 4 KB-aligned pages) |

**Prototype results** (`xbadpcm2vag.py`):

| Sound | Xbox | VAG bake | Round-trip SNR vs its PCM input |
|---|---|---|---|
| AR fire `brown2_1` (mono 22k, 0.70 s) | 8,712 B | 8,880 B (102%) [M] | 28.3 dB [M] |
| same at 16 kHz | 8,712 B | 6,448 B (74%) [M] | 26.6 dB [M] |
| `bullets_projectile` 1 (stereo 22k, 1.73 s) → mono | 42,840 B | 21,776 B (51%) [M] | 25.7 dB [M] (L/R correlation 0.894) |
| `countryday2lp1` (stereo 44.1k, 1.32 s) → 22k mono | 65,520 B | 16,656 B (25%) [M] | 21.1 dB [M]. Noise-like and quiet (−41 dBFS); 26–30% of its energy was above 11 kHz and is lost; L/R correlation 0.37, so mono loses width |
| 40-permutation sample (`compare_codecs.py`) | | | **VAG median 29.2 dB (min 20.3) vs IMA re-encode 27.8 dB (min 20.1); VAG better in 22/40** [M] |
| SDK `sound.pcm.raw` (44.1k, clean source) | | 0.572 B/sample | 37.2 dB [M] |

The VAG decoder decodes the SDK's `fanfare.vag` cleanly: −32.7 dBFS, 3 kHz spectral centroid, no clipping [M]. Its output matches PPSSPP's SAS (correlation 0.984 against reference; the remainder is the mixer's resampling) [M]. WAVs for listening are in `audio/out/`. The Xbox ADPCM is the only source available, so each transcode is a second generation. **This needs a listening test on the PSP's speakers.**

**Bake size options** (`bake_sizes.py`) [C]:

| Option | bloodgulch MiB | ui.map MiB (music) |
|---|---|---|
| Xbox ADPCM today | 11.66 | 11.83 (11.03) |
| A: VAG, rates and channels kept | 11.87 | 12.03 (11.21) |
| **B: 22.05k max, SFX mono, music stereo** | **9.95** | **6.37 (5.61)** |
| C: B with SFX at 16k | 7.23 | 6.16 |
| D: B with footsteps/impacts/casings at 11k | 7.96 | 6.19 |
| E: everything 22k mono (music too) | 9.95 | 3.56 (2.80) |

### 3c. Memory and streaming

| Item | Size | Basis |
|---|---|---|
| Sound cache (lruv, 4 KB pages) | **2 MB minimum, 4 MB recommended** | 512 KB thrashed [M]. The Xbox used 4 MB for 11.66 MiB [D]; option B makes the same data 15% smaller [C]. An infantry firefight's sound groups total ≤4.9 MB of VAG if *every* permutation were used; ~1.5–2.5 MB is realistic, since only 2–3 surface materials are in play [E] (`working_set_estimate.txt`) |
| Where | **Volatile partition (4 MB at 0x08400000)** | The data is reloadable: on suspend, stop all sounds, flush the cache, unlock; on resume, reload. Main RAM is readable by the ME |
| SAS core | 3.6 KB | [D] |
| Output buffers | 2 × 1 KB | [C] |
| Ring voices | 8 × 8 KB = 64 KB | [E: count] |
| AV codec + SAS modules | **unknown** | Test [1]. PPSSPP shows 0 because it emulates them at a high level [M] |

**Streaming.**
- Music, dialogue and long ambience already stream through the sound cache: `update_channel_for_looping_sound` requests the next chunk as soon as the current one starts. So the read deadline is one chunk's duration: **~2.6 s for a 64 KB stereo-22k VAG chunk** [C].
- A 64 KB read costs ~1.75 ms seek plus ~4 ms transfer at 16 MB/s [M rates → E].
- Music at VAG 22k stereo needs **25.2 KB/s** (0.16% of Memory Stick bandwidth) [C]; dialogue at 22k mono needs 12.6 KB/s [C].
- Reserve ~2 chunks of cache per active stream, ~128 KB each.
- First plays still miss for classes in **discard** mode, as on the Xbox (the forced-permutation trick plays them next time). A larger cache reduces this.

### 3d. CPU budget per 33.3 ms frame

| Item | Cost | Basis |
|---|---|---|
| Game sound logic (`sound_render`: refresh, prioritize, update; obstruction raycasts) | **~1–2 ms walking, 2–4 ms in combat** | [E] from the 3–4 ms [M], which included shim decode and mixing. 73 → 32 channels shrinks the O(sounds × channels) loops 2.3×. **Needs profile sections** around `refresh_sounds`, `prioritize_sounds`, `update_channels` and `compute_sound_obstruction` |
| Spatialize, 32 channels | ~0.05–0.1 ms | [E] |
| Audio thread, ~5.7 grains/frame (commands, sasCore call, output) | ~0.1–0.3 ms, **if sasCore sleeps while the ME mixes** | [E]; test [2] measures the main CPU kept |
| Ring refills (memcpy 2 KB per ~160 ms per stream voice) | <0.05 ms | [E] |
| Mixing and decoding | **0 on the main CPU** (ME: ~48% at 32 voices, ~37% at 24, no reverb) | [D/M PPSSPP model] |
| **Total** | **~1.5–4.5 ms** | [E]. The thrash alone cost 16–22 ms [M] |

Levers if the game logic is still too costly:
- Raycast obstruction every 2nd or 3rd tick, or only for the N loudest sounds.
- Skip `update_channels` work for channels whose state has not changed.

### 3e. Files and functions that change

| File | Change |
|---|---|
| **new** `port/psp/src/sound_psp.c` | `struct sound_platform_definition platform_sound_psp`, all 13 hooks (initialize/dispose, listener, begin/end_scene, queue/update/stop/state, pause, flush, location, properties). Contains the voice table, ring voices, spatialize, audio thread, SAS backend (later also an ME backend), and power callback handling |
| `sound/sound_manager.c` | `platform_definitions[0] = &platform_sound_psp` under `HALO_PSP` (fixup). Drop the `audio.enabled → enabled = FALSE` PSP fixup once output exists. Accept `compression == psp_vag` in `sound_new_impulse` (line 2394) |
| `sound/sound_preferences.c` | Channel counts that fit 32 SAS voices (fixup) |
| `cache/xbox_sound_cache.c` | Base and size from the volatile partition; dcache writeback/invalidate of the block after the load completes and before SAS reads it; compile out `lruv_debug_to_file` on the PSP. The page-count fixup is already done |
| `cache/physical_memory_map.c` | `physical_memory_get_sound_cache_base_address` → volatile buffer |
| `sound/sound_dsound_xbox.c`, `port/linux/src/dsound_sdl.c` | Not linked on the PSP (or kept for `bink_playback.c`'s `dsound_get`, which can return NULL) |
| `sound/game_sound.c` | Optional: throttle `compute_sound_obstruction` |
| `port/psp/src/psp_platform.c` | Stop setting `HALO_NO_AUDIO`; suspend/resume hooks (stop sounds, `sound_cache_flush`, unlock/relock volatile) |
| Invader fork (bake) | `snd!` permutations: Xbox ADPCM → PCM → (downmix, resample) → VAG (`first_filter0`, planar stereo, ≤64 KB chunks), then rewrite `samples` size/offset, `encoding`, `sample_rate`, `compression` and the per-permutation rate field. `xbadpcm2vag.py` is the reference implementation |

---

## 4. Prototypes (all in `/home/claude/research/audio/`)

| File | What it does |
|---|---|
| `snd_census.py` | Parses `snd!`/`lsnd` from an Xbox map; tables by format, class and duration; JSON dump |
| `xbadpcm2vag.py` | Xbox ADPCM decoder (as in `dsound_sdl.c`); VAG encoder (closed-loop search over 5 filters × 13 shifts, loop flags, first-block filter 0); VAG decoder (as in PPSSPP); resample/downmix; WAV/VAG output; `--selftest` against the SDK files |
| `compare_codecs.py` | VAG vs IMA second-generation SNR over N permutations |
| `bake_sizes.py` | Exact baked sizes per option |
| `sastest/` | **Hardware test EBOOT** (`main.c`, `Makefile`, `EBOOT.PBP`), `make_data.py` (bakes the AR shot and the `countryday2` chain into `halo_vag.h`), `check_ring.py` (compares captured output with the reference). PPSSPP run: `sastest/ms/PSP/GAME/halosas/result.txt` |

PPSSPP results for `sastest` [M, emulator only]: modules load, `sasInit` OK, audio channel 7.

| Voices | sasCore µs per grain |
|---|---|
| 0 | 160 |
| 8 | 814 |
| 16 | 1,468 |
| 24 | 2,121 |
| 32 | 2,775 |

These match PPSSPP's hardware-fitted model. The "main CPU kept" column reads 101% in the emulator, which is not meaningful there.

---

## 5. Risks and open questions (hardware tests)

| # | Question | How to settle it |
|---|---|---|
| 1 | Do AVCODEC and SASCORE load from our user-mode EBOOT on 6.61 PSP-1000, and **how much RAM do they take?** | `sastest` [1] (prints free memory before and after) |
| 2 | **Does `__sceSasCore` sleep the calling thread while the ME mixes, or spin?** | `sastest` [2] "main CPU kept" at 0–32 voices |
| 3 | **ME ownership: SAS or a libme-core takeover?** They are mutually exclusive | Sheepdog01's decision, after v7 ME results and a profile of ME-worthy jobs |
| 4 | Cache coherency and DMA: the ME reads RAM directly. A homebrew (pspoke #12) had SAS go silent on hardware for some memory layouts; allocating from `PSP_SMEM_High` fixed it, root cause unknown | Write back the dcache after every ring refill and cache load; run `sastest` [3] on hardware and listen for gaps; try a buffer at another address |
| 5 | Do the loop-flag semantics (6/3) and grain-count position tracking hold on hardware, given the key-on delay of 32 × pitch + 1 samples [D: PPSSPP]? | `sastest` [3]: ring vs direct by ear. Optionally capture both on the PSP and run `check_ring.py` on the saved `.raw` files |
| 6 | 32-voice limit: is a second SAS instance possible? (PPSSPP supports only one) | Hardware test (try `__sceSasInit` on a second core struct) |
| 7 | Volume steps per grain: zipper noise | Ramp in the audio thread; listen |
| 8 | Suspend/resume with the cache in volatile memory; SAS state after resume | Power callback test |
| 9 | Second-generation ADPCM loss (median ~29 dB SNR), and losing stereo width and >11 kHz in ambience at 22k mono | Listening test on PSP speakers and headphones; keep stereo for `countryday2`-type ambience if it matters (+0.3 MiB) |
| 10 | Real voice counts and working set in combat on Blood Gulch and b30/a10 | Log channel use and sound cache requests on the Linux port during a combat replay; simulate cache sizes offline |
| 11 | Campaign maps (music, long dialogue) | Run `snd_census.py` and `bake_sizes.py` on b30 and a10 once extracted |
| 12 | Grain size against ME load and latency (256 vs 512 change the ME share by <2% [D model]) | Keep 256 unless the ME is short |

## Sources
- PPSSPP `Core/HLE/sceSas.cpp`, `Core/HW/SasAudio.cpp` (`/tmp/ppsspp`), including `EstimateMixUs`. Its comment: "Measured on a PSP (pspautotests audio/timing/sastiming) at 222MHz … 32 VAG voices at 512 samples take 8.7ms".
- PSPSDK `pspsascore.h` and `samples/sascore` (`/opt/psp/pspdev`).
- [psyz PR #165, sceSas replacing SPU emulation (runs on the ME)](https://github.com/Xeeynamo/psyz/pull/165)
- [pspoke issue #12, sceSas silent on hardware for some memory layouts](https://github.com/IbrahimIrfan/pspoke/issues/12)
- [mcidclan/psp-media-engine-reload](https://github.com/mcidclan/psp-media-engine-reload) (ME architecture; says nothing about SAS)
- Halo decomp: `sound/sound_manager.c`, `sound_dsound_xbox.c`, `sound_classes.c`, `sound_preferences.c`, `game_sound.c`, `cache/xbox_sound_cache.c`, `memory/lruv_cache.c`; Linux port `port/linux/src/dsound_sdl.c`.

---

## 6. Addendum, 2026-10-03: the Media Engine, read in full and tested in prep

Labels as above; **[C]** = claimed by the library's author about their own hardware tests, not yet reproduced by us.

### 6.1 The conflict in §3a is resolvable

mcidclan's `psp-media-engine-safe-task` runs custom code on the ME **without disrupting the firmware's ME syscalls** [C: Method 1 and MIST tested on Phat]. sleep/awake keep working. The one rule: a custom task must run to completion before the next ME syscall or a sleep event — bounded jobs, not a forever-loop. So **sceSas (A) and our own bounded ME jobs can share the ME**, serialised: a sceSas grain, then a batch of line-of-sight rays or a vertex classification, then the next grain. Route B (our mixer as the ME's only tenant, via the custom core) remains the fallback if the shared route proves fragile on the PSP-1000.

Two things to know about the PSP-1000 specifically [D, from `me-core-mapping.def.h`]: the firmware's ME image on the fat is only half-mapped by mcidclan's tables (the sceSas mixer entry `_meCoreProcessAudioMixer` is among the unmapped ones), while the hardware mutex, cache and DMAC primitives are mapped. A safe-task job that uses only its own code and the `meLib*` helpers is not affected.

### 6.2 What the recon tests measure (morning of 2026-10-03, `halo_psp_me44.zip`)

- `halo_me`: custom-core start-up on the fat's table, a 16-voice fixed-point mix timed on the ME and on the SC (the same inner loop as `psp_dsound.c`'s), FPU maths parity, a 10 s stress, a clean stop; `ms0:/halo/log/me_test.txt`.
- `halo_me_audio`: mcidclan's shared double-buffer sample (ME synthesises, SC plays through `sceAudioOutputPannedBlocking`). Hearing the tone = the end-to-end audio path through the ME works on this unit.
- Both take the firmware's ME services down until reboot. The game as built doesn't use them (software mixer, `sceAudio` DMA output), so it still runs afterwards; reboot anyway before the next game session.

### 6.3 Measured software-mixer load, for the comparison

The SC software mixer (`psp_dsound.c`, 16 voices, 22.05 kHz stereo, 512-frame blocks): 724 µs per 23.2 ms block at 3.7 voices on Blood Gulch, 1,486 µs at 8.2 voices (15 peak) on b30's beach fight, in the emulator [M]; Sheepdog01's hardware logs (b40–b43) show no late blocks and no clipping. Hardware block cost is in those logs' `audio:` lines. The reported "cutouts" are the game stopping sounds (`sounds stopped by the game before their end`, counted since build 43), not the mixer falling behind.

### 6.4 What this changes in the plan

1. Keep the SC software mixer as the shipping path until an ME path is proven on hardware: it is cheap (a few % of the CPU) and already correct.
2. The sceSas route still requires the VAG bake and the ring-buffer trick; its payoff is ME-side decode and mixing (≈3–6% of the SC back) plus reverb. Worth it only once the bake pipeline exists for other reasons (it will: memory).
3. The bigger ME prize is not audio but the tick (see `campaign_recon_b30.md`): rays and ground probes against the collision BSP. The safe-task route lets those coexist with whatever audio backend wins.

Sources added: `/home/claude/research/src/psp-media-engine-safe-task` (README, MIST.md, samples), `/home/claude/mecore` (custom core, built and installed; samples `poc`, `audio-shared-buffer`, `sleep-wake`, `exceptions`), Daedalus's `AudioPluginPSP.cpp` (ME audio in a shipped emulator: uncached `me_struct`, `BeginME` per audio list).


### 6.5 Results on Sheepdog01's PSP-1000, 2026-10-03 [M]

- `halo_me`: firmware table recognised as the PSP-1000's (`meLibDefaultInit` 3); the ME counted 5,740 → 300,865 in 0.2 s. At the firmware default clock (222 MHz, bus 111; the test does not set 333): 16 voices × 512 frames 1,531 µs on the ME vs 1,552 µs on the SC, **bit-identical output**; 20,000 `sinf`/`sqrtf` 27,990 vs 28,031 µs, identical results; 10 s stress with the ME mixing flat out (6,480 blocks) while the SC spun freely; stopped cleanly on request.
- `halo_me_audio` (mcidclan's sample): the three generated sounds played correctly. **Pressing HOME crashed the PSP**: the sample never stops its ME loop, so when the XMB reloads into the memory the ME is executing from, the ME runs garbage. Rule for the game: stop the ME (and wait for its acknowledgement) before any exit path — HOME, quit, and the suspend power callback.
- For comparison, the game's own SC mixer on hardware: 1.6 ms per 23 ms block at 6.4 voices (-O2 build), 2.8 ms with -Os; worst block 6.8 ms. That is the time an ME mixer would hand back.
