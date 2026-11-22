# Review: mixkinds (H1, one mix loop per voice kind)

Keep word `mixkinds`, **off by default** until its proof (memixcheck and audiocheck in the emulator, then a PSP pair). Exact: the same samples, the same voice state after, the same decodes in the same order. Box: audio (the lab's REBUILD §3.3; the lab's `audio_me.md` H1, and the design in the lab's `sound.md`, "H1, the first step").

## Why

`mix_heard` (MEMIX) mixes every heard voice with one loop that asks, every output frame, whether the voice is PCM, whether the frame's 64-frame ADPCM block changed, whether there is a next frame to interpolate and where it lives, whether the voice is mono, and whether the low-pass is on. Every heard voice in play is mono ADPCM [M, the lab's findings `audio_by_stage_b30`], so most of those tests answer the same for the whole voice, and the rest the same for a whole run inside one decoded block. The census put the loop at 58-63 ME cycles a voice-frame without decoding, two thirds of the audio job [M on the 11-15 build; the lab's `sound.md` 248-251; the audio_ground ruling]. The lead sized H1 at ~0.9-1.4 ms of a ~2.2 ms block [U on M].

## What it does

`kinds` is chosen once a call: `mix_kinds && mono && adpcm && step != 0` (a zero step never leaves its frame; the general loop keeps it). Then, at each run (one voice, one decoded block, one packet), the run ends at `limit = min(frame | 63, last)` and the loop is chosen:

| Kind | When | A frame's work | Frames it takes |
|---|---|---|---|
| plain | `step == 0x10000`, fraction 0, no low-pass | one load, two multiply-shift-adds into the sums, the ramp's two adds | every frame to `limit` (the count worked out once) |
| plain filtered | as plain, low-pass on | plus the one-pole filter | the same |
| pitched | any other step, or a fraction | plus the next sample's load, the interpolation's multiply, the 16.16 step | frames before `limit` |
| pitched filtered | as pitched, low-pass on | plus the filter | the same |

The frame at `limit` in a pitched run (its next sample across the block's end, in the other buffer, or none at the packet's last frame) goes through the general loop, one frame, as before; a step that jumps past `limit` starts the next block's run. Stereo voices, PCM voices and a mono voice whose two filter states differ keep the general loop.

Why each is exact:
- Each frame's arithmetic is the general loop's, operation for operation and in the same `int` types.
- In the pitched loop, a frame with no fraction is still interpolated: `l + ((d * 0) >> 15)` is `l`, the general loop's answer without a next sample. A frame before `limit` is never the block's 63rd, so its next sample is in the same block.
- The plain loop's fraction stays 0 (`0 + 0x10000` masked), so nothing is lost by not stepping it; the filter's state is set to the last sample read, as the general loop leaves it.
- Decodes: a block is decoded where the general loop decodes it (a run's first frame in a new block; the edge frame's next block through the general loop), so the decode calls, their order and the decode cycles are the same.

No VFPU, no system call, no `double`, only aligned halfword loads: it runs on the ME as the general loop does. `mix_heard` isn't in any MEXEC list (`mexec_rbtsw.txt` leaves the mixer out), so the copy's room is unchanged.

**The loops' size on the PSP:** not yet read (no listing until the lab's first build). The build's `halo.map` gives `psp_dsound_mix_list`'s growth (mix_heard inlines into it); the PGO profile won't count the changed function, so the first build needs a fresh profile or the mismatch allowed.

## The proof

- **Host test** [D]: the old loop (mixkinds off) and the kinds (on) from this file's own `mix_heard`, with a stand-in decoder, on 300,000 random voices (1-512 frames, 1-4 packets, every kind, steps 0 to 5x, full-scale samples, ramps): outputs, filter states, position, fraction, packets finished and the decode calls in order identical in all, with `long` 64- and 32-bit. Two planted faults were caught (5,376 and 3,916 of 50,000).
- **memixcheck** compares the general loop under `memixold` with `mix_heard`: with `mixkinds` on, that is the kinds against the per-sample loop. Its report adds a line of the voice blocks by the kind they started in, so a check that never reached a kind says so.
- **audiocheck** with `mixkinds` on: every block identical (mixkinds isn't an approximate path).
- **The PSP pair**: `- | mixkinds` on b30_leg2 and c20_leg2, reading `ME: audio split` and the replay's own time.

## Words

- `mixkinds`: H1 on. Logged at the audio's start: `audio: mixkinds: ...`.
