
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
