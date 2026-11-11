# CLAIMFILL: copies and fills that claim their destination lines

**What it is.** A build switch (`PSP_CLAIMFILL=1`) that routes `memcpy` and `memset` through two wrappers in `psp/src/psp_libc.c`, and a keep word (`claimfill`, off by default) that makes them claim whole destination lines with `cache 0x18` before writing them, so a line that isn't in the data cache is made there without being fetched from memory first. `claimcheck` is the PSP's proof. The default build has none of it.

**Why.** A store to a line that isn't cached fetches the line, though a copy or fill is about to replace all of it. Sony's own kernel routines never do that fetch: their block fill and copies run `cache 0x18` on each whole line first (`hardware_facts.md`, Firmware). The op runs in user mode and saved ~65 cycles a line on the PSP-1000 [M]. By the emulator's count the tick and frame on b30's flight make ~1,500 such fetches in long copies and fills [E]. The price is a PSP pair's to give.

**The key lines.**

- `__wrap_memcpy`, `__wrap_memset`: the guards, in one test. A call goes to the library's routine unchanged when it is 256 bytes or less, when the keep word is off, when the destination isn't the user partition through the cache (`0x08800000`-`0x0A000000`: never the uncached view, VRAM or the scratchpad), when source and destination overlap or their words can't both be aligned, and on the Media Engine (the op is unmeasured there).
- `claim_copy`, `claim_fill`: the head up to the first line boundary and the tail after the last go through the library's routine. Each whole line between is claimed and then stored in full, sixteen words, straight away. Nothing is claimed that the same call doesn't write in full: a claimed line holds no defined bytes until then.
- `claim_checked_run` (`claimcheck`): the same copy or fill with interrupts off, then every byte of the range compared with what it should hold and the line either side of the claimed lines compared with what it held before. A range over 64 KB is compared without the neighbours and with interrupts on.
- `psp_claim_report`: one line with the map's metrics: copies, fills, lines claimed, checked, disagreed.

**What can go wrong, and how it is caught.** The emulator runs `cache 0x18` as nothing, so it can only prove the routines' logic (play identical; the check's count). A claim that behaves differently on hardware shows only there: one PSP run with `claimfill claimcheck` must report `disagreed 0` before any pair is read. A host test with every claimed line poisoned before it is written (200,000 random copies and fills, the neighbours compared) found no wrong byte.

**Not exact in one sense.** The bytes are the same. The data cache's contents differ (lines made without a fetch, and the victims they push out), which is the point; that is why its price comes from the PSP and not from the cache model, whose `cache 0x18` is still a drop (the lab's M18).

**The build's own guard.** `psp_libc.c` is one of the plain-C units with a compile line of its own, so it takes `-DPSP_CLAIMFILL=1` there, not from the engine's defines. Without it the link's `--wrap` names two functions nobody defines, and the link's stubbing of undefined names would make every copy and fill in the program an empty call: a build that links and never reaches its first log line. The link now refuses to stub a `__wrap_` or `__real_` name and says which.

**The emulator's proof [E]** (the lab's sierra-batch claimfill2, 43068167920; tock162 9e74d6e + this branch at db4e8d1, `PSP_CLAIMFILL=1`, no profile). It proves the routines' logic and counts the work; the claim itself runs as nothing there.

| | b30, the flight to the beach (3,900 ticks) | c20 from its start (3,600 ticks) |
|---|---|---|
| Play and both seeds, the word off / on / on with `claimcheck` | identical on every tick | identical on every tick |
| `claimcheck`: copies and fills compared byte for byte, with the line either side | 689,692, disagreed 0 | 412,862, disagreed 0 |
| Lines claimed a tick in play (ticks 1,800-3,600) | ~1,900 (97 copies + 77 fills, ~11 lines each) | ~1,200 |
| Ceiling at the ~65 cycles a line the PSP-1000 measured, if every claimed line would have missed | ~0.37 ms a tick | ~0.23 ms a tick |

The ceiling is a count [E] times a cost [M], a lead only: a destination line already in the cache saves nothing, and only a PSP pair prices it. ~~Still to do, in order: one PSP run with `claimfill claimcheck`, then the pair on b30's flight.~~ *(done 2026-11-11, below)*

**On the PSP-3000 [M]** (2026-11-11, master; lab findings `claimfill_check_psp3000`, `claimfill_b30_pair`):

- **The claim holds on hardware.** `claimfill claimcheck` over a minute of b30 (sierra-hw 43069705102): 4,071,218 lines claimed, 324,435 compared, 0 disagreed.
- **The price is small and in one place.** The b30 pair, `- | claimfill` x2 to `exitat60` (sierra-hw 43071955144): the whole run -0.2% (106.7 / 106.6 s against 107.0 / 106.8). By 300-tick window the frame is the same within ~1%, except game time 300-600, where the level streams in at the cinematic's end: the frame -5.6% (96.2 / 98.8 ms against 103.9 / 103.1), all of it the drawing (29.0 against 34.6 ms). The tick is unchanged (38.4 against 38.5 ms). Play is the same on both sides.
- **So:** exact, and never worse where measured. It goes on by default at the next profile recount, because the switch moves code and the current profile and layouts were made without it.
