# Review: audiofx (audio plan step 2, docs/audio_fidelity.md)

Base: w140. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

## E4: channel searches over live channels only: counted, not built
- The walks [D]: `update_channels` walks all 73 channel slots once a frame; `sound_channel_summary_build` and `sound_find_best_channel` walk them only for a sound looking for a channel (a sound already playing returns at once in `sound_find_channel`); `sound_find_best_channel` stops at the first free channel that fits.
- Live channels [E]: 2.7-9 voices on average on b30 (the mixer's "voices on average" line), so ~64 empty slots a walk, each a 24-byte record and one compare: 28 cache lines for the whole array.
- Removal estimate: ~64 empty visits and up to 28 lines on a few walks a frame, ~10-20 us a frame on the PSP-1000 [U], against a 60 ms frame (b30 route, PSP-1000 [M]): ~0.03%, far under the ~1% gate. Same answer as the datum-array roster (lever RB). Not built.

## E3: obstruction rays on the ME: counted, designed, not built
- Today's tests [M/E]: PSP-1000 b30 route 9.1 tests a frame on average (23 at most); emulator b30 5.5 (open) to 11.9 (beach, 30 at most), a30 5.3. Each is one `collision_test_vector` from the camera with structure, media, objects, scenery and machines.
- Their cost [M]: "sound_render: refresh and obstruction" is 1.0 ms a frame on the PSP-1000 b30 route (1.7% of the 60 ms frame); the tests were two thirds of that step when it was first measured, so ~0.67 ms (~1.1%) is the most the ME could take off the main CPU.
- Design (reuses `ME_JOB_PERC_RAYS`' machinery): a test that is due goes to a ring job and the sound keeps its cached result (as today past the per-frame budget); the next frame takes the answer. The ME's quiet walk doesn't serve the media bit, so the ME casts without it and the main CPU does the water plane test on the answer's last cluster (one plane test). The hand-over costs the main CPU's whole cache written back and the ME a cold cache, about what 5-12 rays save, so it pays only if it rides an existing hand-over. Results arrive one frame later than today: the gain only, never game state.
- Verdict: at the gate, not past it. Hold until the hand-over can be shared.

## What each restores, and against what

The port's mixer dropped four things the original passes to DirectSound. All four are restorations (Halo on the Xbox), so they are on by default behind `audioold`, which gives the mixer before them.

| Item | Original [D] | Port before | Source for the shape |
|---|---|---|---|
| Muffling | `update_channels` sends each 3D channel's obstruction and occlusion through `SetI3DL2Source` (`sound_dsound_xbox.c`); the Xbox applies both a gain and a high-frequency loss | gain only | I3DL2 level 2 model: the loss `lHFLevel` is taken at the 5 kHz reference frequency, with `flLFRatio` the share that also hits the lows [U: the Xbox filter's exact slope is not documented; a one-pole low-pass that has the stated loss at 5 kHz is the simplest filter that does] |
| Doppler | the engine sends each source's velocity in the listener's frame (`SetVelocity` per channel) and a distance factor; DirectSound's Doppler factor stays at its default of 1 | ignored | DirectSound's Doppler formula, the speed of sound at 343 m/s [U: the Xbox default] |
| Cone angles and orientation | `SetConeAngles`, `SetConeOrientation` per channel, from the sound tag's inner and outer cone | ignored | DirectSound 3D's cone rule [U] |
| Cone outside volume | `SetConeOutsideVolume` from the tag | ignored | same |

Where the Xbox's own numbers could differ from these (the filter's slope, the cone's blend), the PC build is the check: a hand listen with the PC build beside the port, on the stretches below.

## Listening checks for a hand test

- Muffling: stand beside a closed door or behind a wall while a unit fires or speaks on the other side: duller, not only quieter; open the door and the highs return in the same moment. No click when a sound becomes muffled partway through.
- Doppler: a Banshee, Ghost or Warthog passing fast: rising then falling pitch; a stationary listener beside a stationary source: none. Your own movement: running toward a looping emitter lifts its pitch slightly. Nothing should warble or step when a source's speed changes.
- Cones: a few machine and ambient emitters: quieter from behind than in front (on the stretches tested for this review no sound in the tags asked for a cone: if none is heard, that is the tags, not a failure).
- `audioold` in the launch menu or keep.txt for the same spots, to compare.

## Fidelity, each off with `audioold` (psp_dsound.c only, the mixer)
- **Muffling:** `SetI3DL2Source`'s high-frequency loss (`lDirectHF` plus obstruction and occlusion `lHFLevel` times `1 - flLFRatio`) sets a one-pole low-pass per voice, `y += a (x - y)` in Q14, with `a` solved so the filter's gain at I3DL2's 5 kHz reference is that loss. The low-frequency gain stays as before (`i3dl2_gain`). The coefficient is worked out on the main CPU only when the loss changes; the mix adds one multiply and two adds per frame for a muffled voice, and none for a voice with no loss.
- **The mix loop** (`mix_voice`, run on the ME): the filter's coefficient and state are read into locals before the loop and stored once after it, since `output` may alias the record and fields written inside it would be stored every sample. A voice with no loss takes the `else` branch, which only copies the sample into the state, so a filter that starts in a later block starts from the sound, not from 0 (no click). Samples are 16-bit, so a difference under 2^16 times the Q14 coefficient stays inside 32 bits.
- **Doppler:** the game's per-stream velocity (already relative to the listener, in world units a second: `update_channels` takes the source's velocity a tick into the listener's frame, times 30, less the listener's) along the line to the listener, scaled by the distance factor, gives DirectSound's factor-1 shift, held to an octave either way. It scales the voice's step once a block on the main CPU; the mix itself is unchanged.
- **Cones:** `SetConeAngles`, `SetConeOrientation`, `SetConeOutsideVolume` kept per stream; full inside half the inside angle, the outside volume past half the outside angle, a straight line in millibels between them. The facing is divided by its own length (the game sends a normal, but the formula doesn't rely on it). One `acosf` a voice a block, on the main CPU, and only for a voice whose cone is narrower than 360 degrees.
- **The log:** one heartbeat line, `audio: of N voice blocks, muffled M, pitch moved by Doppler D, quieter outside a cone C`, so a test can see each item engaged (0 on a stretch means the game never asked for it there, not that it failed). With `audioold` it says so.
- **Memory:** 52 bytes more in each stream record (heap, one record per channel); no new static tables.
- **Play:** none of these writes game state. Doppler moves when packets finish, which lip sync reads, so exactness runs use `nomouth`.
- **What to listen for:** gunfire and voices behind walls and doors duller, not only quieter; passing Banshees, Ghosts and Warthogs rising then falling in pitch; sounds with cones (some machines and ambient emitters) quieter from behind. `audioold` gives the build before these changes.
