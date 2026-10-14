# Audio: fidelity and efficiency plan (2026-10-14)

Sheepdog01's aim (2026-10-14): keep the sound as faithful to the Xbox as the hardware allows, on the PSP-1000's speakers and on headphones, and make it cheaper where that costs nothing heard. Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code, [U] inference.

## Where the port differs from the Xbox today [D]

| Stage | Xbox | Port |
|---|---|---|
| Data | Xbox ADPCM; effects mostly 22 kHz mono, music and ambience 44.1 kHz stereo | S9 bake: every sound at most 22,050 Hz, effects mono, music stereo; still ADPCM (a second generation for re-encoded sounds) |
| Mix | The MCPX chip, 48 kHz [U] | Software mixer on the Media Engine, 22,050 Hz, out through the PSP's sample-rate converter (`psp_dsound.c`) |
| 3D | HRTF | Equal-power stereo pan at 75% width |
| Distance | DirectSound's inverse rolloff | The same |
| Rooms | I3DL2 reverb per BSP cluster's environment | Ignored (`SetI3DL2Listener`) |
| Obstruction, occlusion | Low-pass filter plus gain | Gain only |
| Doppler | Yes | Ignored (`SetVelocity`) |
| Cones | Yes | Ignored |
| Sound cache | 4 MB | 1.75 MB (`soundN`) |

## Where sound costs on the PSP

- **Main CPU:** the game's sound logic per frame: an obstruction ray per audible sound (reused per tick since b107: looping sounds 11.8 → 1.8 ms a frame [M]); channel searches over all 73 channels; the full cache's allocator; `lruv_debug_to_file` on a full cache (7 times in one b30 run [M]).
- **Memory Stick:** sound was 86% of reads in play (b101 [M]); S9 cut it to about a third [E]; most of the rest is re-reading evicted looping sounds (b115 [E]).
- **Media Engine:** 5-15% for audio, 8-15% busy in all on the rig (b122) [M].
- **The audio thread** sleeps while the ME mixes (`psp_me_run`: a first look after 0.2 ms, then every 0.1 ms), so its 1.3-3.6 ms wait a block costs the game only the wake-ups [D] (E7: start the first look at the last job's time).

## The decision (Sheepdog01, 2026-10-14)

- **Effects** stay at 22,050 Hz in the main mix, decoded as they play, at audio priority. Mixing them at 44.1 kHz would double their work for nothing: the disc's effects are 22 kHz.
- **Music** at 44.1 kHz on its own hardware output, in a compressed codec (Opus or Vorbis, ~96-128 kbps), decoded ahead in bursts as a low-priority ME job into a PCM ring (~176 KB a second of buffer), read from the stick in long sequential runs. Smaller than today's S9 music (~198 kbps ADPCM) and full bandwidth. A keep word drops music to 22 kHz if the ME is short.
- **Audio keeps priority on the ME:** ring jobs give way (the main CPU does their work) rather than delay a mix block.
- **One A/B for Sheepdog01:** `audioold` turns every audio change off at once; a second preset turns off only the music path. Speakers and headphones.
- **Arriving on time** is measured on its own: late and failed loads per stream (`iostats`, the sound cache line) in every audio A/B.

## The work, paired

| Step | Efficiency | Fidelity |
|---|---|---|
| 1 | E1 `lruv_debug_to_file` compiled out; E2 the allocator's full scan fixed; E7 the audio thread's first look | n/a |
| 2 | E3 obstruction rays on the ME (b122's ray job machinery); E4 channel searches over live channels only | Muffling (one-pole low-pass per voice from the I3DL2 values), Doppler (the resampler, from velocity), cones |
| 3 | E5 music in its own ring, loops resident | 44.1 kHz compressed music on a second output |
| 4 | E6 a bigger sound cache from freed RAM | Room reverb (one shared, from Halo's per-cluster environment, at 22 kHz); headphone 3D (interaural delay and rear shading) only when `sceHprmIsHeadphoneExist()` |

**Before step 3:** a hardware test on the PSP-1000: a 22 kHz channel and a 44.1 kHz channel playing together, and Opus and Vorbis decode speed in bursts on the ME.

**S9's own A/B:** `campaign121nos` (b30 baked with the disc's sounds, lab run 38604497176) against `campaign121` on the rig: frame, hitches and stick time.
