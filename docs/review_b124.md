# Review: build 124, every change since build 123

For Sheepdog01 to read and question before testing. b124 replaces b123: it carries all of b123 (`review_b123.md`) plus the changes below. The long versions are `review_mewake.md`, `review_lk22.md`, `review_cutstream.md` and `review_b123_lititems.md` (§5–6). Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

Branch `w124`: `w123` plus `mewake` (`9cf4bdf`), `lk22` (`ac050be`), `lititems123` (`775bfa3`) and `cutstream` (`255c7e1`). The PGO profile is still b122's.

| # | Change | Kind | Default | Off word |
|---|---|---|---|---|
| 1 | The Media Engine restarted after a sleep | fix | on | `mewakeoff` |
| 2 | The level's matrix kept whole for the GE (LK22: c40's and a10's wobble) | restoration | on | `levelexactoff` |
| 3 | Cut pieces never dropped: spilled into the display list | restoration (the Pelican wall and other holes) | on | `cutstreamoff` |
| 4 | Ground weapons may sleep; sleeping weapons and machines keep their functions | exact speed + restoration | on | `weapontrackawake`, `machinefuncsheld` |
| 5 | `cutless`: off-screen triangles not cut | speed, not pixel-identical | **off** | on: `cutless` |
| 6 | `funccheck`, `cutcount`, `shotrunN`, `mewaketest` | instruments | off | |

---

## 1. The Media Engine after a sleep (`mewake`)

**What.** b123 held the ME in reset from a wake on, so audio and the rays ran on the main CPU until the next launch: audio cost 5% of the CPU, and everything was slower [M, b122 test 2]. Now the restart happens on the main thread at a frame's end:
- it waits until no job or audio block is in flight;
- it starts the ME again with the library's own reset;
- it waits up to 100 ms for the ME's first answer, and only then gives it jobs.

On a failure the main CPU keeps the jobs, as before.

**Key lines.** `psp_me.c`: `psp_me_wake_request`, `psp_me_wake_poll` (called at the top of `psp_ge_frame_end`), the per-slot busy flags, `ring_epoch`.

**Proven** [E]: a faked sleep (`mewaketest`) restarts in 13–32 ms; play is exact on d20 and a50; and no ME answer differs. **Only your PSP can show** that the ME is powered after a real sleep. The wake's own hitch (the audio gap, one long frame) isn't touched.

## 2. The level's wobble (LK22, `lk22`)

**What.** The GE keeps only the top 24 bits of each matrix number. Models were sent relative to the eye, so they kept their precision. The level was sent with the eye's whole world position, which at c40's control room (about 361, −714, 76) loses 1/64 to 1/128 of a unit. That rounding changed every frame as the camera drifted, so the level jumped against the camera and the models. Your doors (objects) stayed still.

Now the GE's world starts at the eye rounded exactly as the GE would round it. The leftover, under 1/64 unit, goes into the view matrix, and the CPU's camera-space tests add the same leftover.

**Key lines.** `psp_ge.c`: `ge_eye`, `relative()`, the view after `sceGumLookAt`, `ge_eye_camera`.

**Proven** [E]: frame-to-frame motion on c40's bridge falls from 0.65–0.70 to about 0.3 px (the measurement's floor). Off is pixel-identical to b122. Play is exact on d20, a50 and c20. **Your test 11 agreed with the cause:** `near4` didn't stop the wobble.

## 3. Cut pieces never dropped (`cutstream`)

**What.** Triangles at the eye or far off screen are cut on the CPU before the GE draws them. Every cut path had a fixed buffer and dropped what didn't fit, silently. The losses per run: b30 93,000 pieces, a10 190,000, c40 21,000 [E]. That is the Pelican's missing wall, and on your b122 runs the missing pieces of Chief's armour (13,669 lost) and Captain Keyes (6,222) [M].

Now each path fills its buffer, then takes 96-corner blocks from the display list, so nothing is dropped whatever the size. Each spilled block is written back from the cache before the GE reads it. This replaces b123's bigger buffer (`cutroom`, `cutroomoff`, now retired).

**Proven** [E]: 0 pieces lost on every map; pictures differ from b123 only where restored pieces appear; play is exact; no bad access in the strict interpreter.

## 4. Sleeping weapons and machines (`lititems123`)

- **Ground weapons sleep again.** `weapon_new` never sets `tracked_object_index`; it reads 0 and names whatever holds object slot 0, so every ground weapon failed the sleep rule's tracking test. On your b122 beach fight, weapons cost 5.1 ms a tick [M].
  - The tracked object's existence is now part of the sleeper's checksum.
  - A sleeping weapon keeps computing its functions, the values its shaders read.
  - Strict `itemsleepcheck` and statecrc pairs show it exact [E]. The gain on b30's flight is −0.2 ms a tick [E]; the beach fight wasn't in the window.
- **Sleeping machines keep their functions (a restoration).** `funccheck` found sleeping machines on d20 holding stale function values since b121's machine sleep: a device held 1.0 where its update makes 0.8 [E]. This is a likely cause of your "doors stuck at high brightness" on d20 [U]. Fixed the same way; c20 also gains 0.34 ms a tick, because more machines sleep [E].

## 5. `cutless` (off, your A/B)

Triangles wholly past a screen edge aren't cut or drawn: −1.1 to −1.4 ms a frame of renderer CPU [E]. Its frames aren't pixel-identical (a few hundred pixels a shot, mostly one colour step), and the cause isn't found, so it's your call (preset 8).

## The merged build (lab 38663129055) [E]

- Play and both seeds identical to b123 at every tick on d20 (16,411), a50 (26,185) and c20 (19,484).
- b30: 0 cut pieces lost (900 spill blocks), `funccheck` 0 stale.
- a50: a faked sleep restarts the ME in 20 ms.
- The multiplayer build plays Blood Gulch.
- **Open:** `itemsleepcheck` logged 14 "changed while asleep" for a plasma rifle on b30, each with "no word of its own changed". My reading [U] is that its tracked object came or went, which is the designed wake, not a difference; it's being confirmed.
