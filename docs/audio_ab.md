# Audio A/B: proving that a mix sounds the same

Sheepdog01, 2026-11-05: audio that sounds the same without being the same bits is open (the approximate levers: virtual voices, far voices at half rate, the muffling filter as a FIR, a shorter 44.1 kHz filter), but "a clean A/B, not playing and hoping things are noticed". The target is the PSP's speakers; headphones are the stricter case, and the one judged.

Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

## The rule every approximate lever follows

Each approximate path is one bit of `stream->approx` (`AUDIO_APPROX_*` in `psp/src/psp_dsound.c`), set from `audio_approx` in `prepare_voice` and tested per voice in the mixer, with its own keep word. So the check can mix one block both ways from the same voices. A path that can only be switched at launch can't be measured this way, so it doesn't ship.

## Layer 1: the null test in the game (`audiocheck`) [E]

Each 22,050 Hz block is mixed with no approximate path and with `audio_approx`, from the same voices (copies, as `memixcheck` does); the block plays the approximate one, and the next block's two mixes both start from its voices. Each block's error is so measured alone, sample-aligned, from one run: no capture to line up, no timing between runs to tell apart.

Per report: the difference in dB below the mix (all, and three bands: under ~300 Hz, ~300 Hz-4 kHz, over ~4 kHz, by one-pole filters on both signals), its peak of 32,767, the worst block against its own mix, and the blocks whose voices' states after differed (a virtual voice that resumes in another place shows here).

- With no approximate path on, every block is identical: the check of the check.
- `audiocheckcal` adds a 1 kHz tone at -60 dB of full scale (peak 33) to the approximate side. audcheck2 (42072112242) read it at 63.1 dB below full scale, peak 32: a sine's RMS is 3 dB under its peak, so the meter is right [E].
- The mix is quiet: b30's flight runs ~47-50 dB below full scale, the beach ~20-25 [E]. So "below the mix" alone misleads in quiet stretches (the same tone read anywhere from 77 dB over to 42 dB under it), and the check logs the level against full scale as well. The bands are one-pole pairs (12 dB an octave): coarse, enough to say where a difference lies, not a spectrum.
- Not yet in it: the 44.1 kHz work (the music, the half-band filter). A lever there extends the check first.

## Layer 2: a perceptual score [E]

The same run writes both sides' output as raw PCM on the X3D runner (`/work`), and a step scores the pair with ViSQOL (Apache-2.0) and/or PEAQ (ITU-R BS.1387). Only the scores reach the results branch. The captures are made from the game's sounds, so they're game data: `push-results.sh` refuses audio files (2026-11-05), and the files stay on Sheepdog01's machines.

## Layer 3: Sheepdog01's blind ABX [M by ear]

The two captures of a stretch, copied to Sheepdog01's machine over his network, played in an ABX test (foobar2000's, or a small script): 16 trials on headphones. 12 or more right (p < 0.05) means a difference is heard; about 8 is chance.

## The gauntlet

A fixed set of stretches, each chosen for a way the approximations could be heard:

| Stretch | Why |
|---|---|
| b30's opening cinematic (game time 150-570) | Dialogue and music, few voices: any added noise is exposed |
| b30's flight (300-2000) | The Pelican's engine under Doppler, the music at full rate |
| b30's beach (2700-3700) | The peak: 20-40 voices, gunfire, distant fights (where virtual voices act) |
| c20 | Indoors, muffled voices behind walls (the lowpass) |
| d20's swamp | Muffled, sparse, quiet: a quiet mix shows small errors |

And beside it a synthetic set that needs no game data at all: sweeps, tones, noise bursts and clicks, played through the mixer's own code on the host at every gain, pitch and muffling the game uses. It checks the maths of each path in isolation, and can be committed.

**Prerecorded references:** the exact mix of each gauntlet stretch, captured once and kept on the X3D, so a later build is compared with it as well as with its own exact side. That needs the mix rendered in step with the game's ticks (a capture mode where audio blocks are made from the game time, not the audio thread's clock), since otherwise a faster build hears a stretch at slightly different moments. Built when the first lever reaches layer 2.

## Verdicts (proposed thresholds, the master's, not a standard)

| Measured | Verdict |
|---|---|
| The difference at least 60 dB below the mix in every band, or under -90 dB of full scale (16-bit output's own floor is ~-98), and the score's drop under 0.1 | Sounds the same: the session's call, on by default |
| Between that and plainly audible | Sheepdog01's ABX decides |
| Clearly audible | A compromise: catalogued, not built (CLAUDE.md) |

Started 2026-11-05 (master). Layer 1 is in (`audiocheck`, `audiocheckcal`); layers 2-3, the gauntlet's captures and the lockstep render come with the first approximate lever.
