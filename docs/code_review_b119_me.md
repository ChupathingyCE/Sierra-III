# Code review: the ME foundation and upstream since the pin (w119, Oct 13, 2026)

Read-only review for the session taking on the Media Engine as a second core (`handoff_2026-10-13b.md` §2.2). Sierra-III `w119` at **`baa632c`** ("ladderreport.py…; build_b119.md (draft)"), me-core-mapper at the pin **`727421b`**, ChupathingyCE pin **`b78e6cf`** against upstream head **`f8abf21`** (2026-10-13). No commits were made. Labels as in CLAUDE.md: [D] read in code, [U] inference, [M]/[E] measured.

Companion docs: `research/research_brief_for_master.md` (what the research changes), `research/research_blocked_list_index.md` (everything else).

## 0. Summary

| # | Finding | Kind | Severity | Cost to act |
|---|---|---|---|---|
| 1 | `psp_me_finish` spins reading uncached words plus a system-time call every ~64 cycles: heavy bus traffic while the ME works | performance | **High for L1** | small |
| 2 | The ME's idle loop does ~10 uncached bus accesses every ~25 µs | performance | Medium | small |
| 3 | me-core already has the interrupt path (`meLibSendExternalSoftInterrupt`, `meLibHalt`, ME handler set-up); the port uses none of it | opportunity | Medium–High | medium |
| 4 | The main CPU drops its whole data cache after each overlapped GE job (`defer_drain`, `defer_hand_over`) | performance | Medium | medium (design) |
| 5 | `psp_me_serve_audio` inside a GE job write-backs and drops the ME's whole cache, twice per audio block | performance | Medium | small |
| 6 | Whole-cache loops cover both ways (the research's §1.2 worry) | correctness | **Resolved: fine** | none |
| 7 | Upstream: 33 commits since the pin; **2 fixup anchors break** (both `errors.c`, debug.txt rolling); 19 other patched files changed with anchors intact; network version 25→28 | upstream | Medium (MP) | small |

Positive notes [D]: ownership is already designed in (`PSP_MAIN_SIDE` link section for main-CPU-only variables; whole 64-byte output lines; slot words accessed only uncached); the stop/timeout paths are careful (jobs pending at a stop are still served; per-type limits after b112's 40 ms incident); `psp_on_me()` tells the CPUs apart by stack address with no shared word. The research's "single-writer" rules are mostly already followed.

## 1. `psp_me_finish` spins on the bus [D, U for size]

`psp/src/psp_me.c:613–638`. While waiting for an overlapped GE job (`gemeoverlap`), each loop iteration does:
- an uncached read of `slot->done`;
- an uncached read of `CONTROL->stop`;
- `sceKernelGetSystemTimeWide()` (a system call that reads the hardware timer);
- 64 `nop`s (~0.2 µs at 333 MHz).

For the first 2 ms that's roughly **3 bus transactions every 1–2 µs**, i.e. on the order of 1–3 million a second [U], on the same bus the ME is using to read its job's inputs (records, model data) from main RAM, and the GE to fetch vertices and textures. The comment's goal (avoid the scheduler tick's latency, b112's X3) is right; the polling rate is far higher than it needs to be.

**Suggested change (exact):** keep spinning, but space the reads: e.g. ~1,000 `nop`s (~3 µs) between looks, and read the clock only every 16th look. Wake latency stays a few µs. Keep word to A/B (e.g. `mefinishslow`).

**Cheap evidence from existing logs:** the ME's own job time per GE job (`ME: … render: … the ME's own time X us a job (the job's code J …)`) under `gemeoverlap` (spinning main CPU) vs. `gemeon` (main CPU sleeps in `psp_me_run`, `sceKernelDelayThread(100)` steps). If the ME's code time per job is higher in the overlap runs for similar work, bus contention from the spin is the likely reason [U].

## 2. The ME's idle loop costs more bus time than the comment says [D, U for size]

`meLibOnProcess` (`psp_me.c:277–296`), every pass (~25 µs, 2,000 `nop`s):
- `control->alive++` (uncached read + write), `control->clock = …` (uncached write);
- for each of the 3 slots, `request` and `done` (6 uncached reads);
- `control->stop` (1 read).

≈ **10 uncached bus transactions a pass**; at b86's measured 551 polls per 20 ms (~27,500 a second) that's **~275,000 a second** [D + arithmetic]. Each uncached access holds the bus for a main-RAM round trip (the ~200 ns class), so plausibly a few percent of bus time while the ME is idle [U]. b86 saw no frame-time change with ME audio on [M], so it's not large; it's worth removing because it's cheap.

**Options (exact):**
- Update `alive`/`clock` only every Nth pass (they're diagnostics).
- Fold the three slots' "work waiting" into one uncached word the main CPU sets (a bit per slot), so a pass reads 2 words, not 7.
- Or idle with `halt` and wake by interrupt (finding 3).

## 3. The interrupt path already exists in me-core [D]

The research (YAPSPD) found the CPU↔ME interrupt register `0xBC100044` and the Allegrex `halt`. **Our own library already wraps both:**
- `meLibSendExternalSoftInterrupt()` (me-lib.c:49): `sync; hw(0xBC100044) = 1; sync`. Called from the main CPU, it interrupts the ME.
- `meLibHalt()` (me-lib.c:45): `.word 0x70000000`. The port already calls it, at stop only (`psp_me.c:299`).
- `meLibExceptionHandlerInit()` (me-core-custom.c:192–243): installs the ME's exception vector (COP0 $25), enables the ME interrupt at system level (`0xbc300008`) and IP2 in Status; external interrupts call the weak hook **`meLibOnExternalInterrupt()`**.
- me-core's `samples/exceptions` demonstrates it end to end (a button press on the main CPU interrupts the ME, whose handler counts it).
- Also present: a hardware mutex API (`meLibHwMutexLock/TryLock/Unlock`; samples/hw-mutex).

So the ME-side half of "idle with `halt`, wake on a job" is library-supported and sampled; what's unproven is **our** use: halting in `meLibOnProcess`'s idle, `psp_me_submit`/`psp_me_run` sending the interrupt, and the interaction with me-core's sleep handler and our kernel `kinit`. The **ME → main CPU** direction (main core's interrupt 31, per YAPSPD) is **not** wrapped by me-core; collecting results by interrupt would need a kernel-side handler registration [U].

**Rig test (small):** a `mehalt` keep word: the ME `halt`s when no slot is waiting; submit/run send the interrupt after bumping `request`. Log wake latency (ME cycle counter at wake minus a timestamp written with the request) and compare the main CPU's `stall (memory)` and `bus access` profiler counters against the polling ME. Include a sleep/resume pass.

## 4. The main CPU's whole-cache drop after overlapped jobs [D]

`psp_ge.c` `defer_drain` (≈1192–1206) and `defer_hand_over`'s fallback (≈1172–1176): after the ME has run a half, the main CPU calls `sceKernelDcacheWritebackInvalidateAll()` before reading what the ME wrote ("this file's state, pspgu's, the list").
- The write-back half is cheap and needed only for dirty lines. The **invalidate** discards the game thread's entire 16 KB data working set at every call that runs at once (frame start/end, text, level vertex queries).
- The research's rule: where one side writes and the other reads, either read through the uncached alias or invalidate **only those lines**. `PSP_MAIN_SIDE` already separates the main CPU's variables; the remaining shared state is the ME-written side of `psp_ge.c`'s statics, pspgu's list pointer, and the list itself.
- **Suggested direction:** put the ME-written state the main CPU must read after a job into its own aligned section (the mirror of `PSP_MAIN_SIDE`, e.g. `PSP_ME_SIDE`), and invalidate that section's range (plus pspgu's few words) instead of the whole cache. The display list itself the main CPU doesn't read.
- **Size it first:** the count of "calls that run at once" a frame (the log's "carried out for" line) × a 16 KB refill (~256 lines × ~216 ns ≈ 55 µs each) gives the ceiling [U]. At a handful a frame it's ~0.2–0.5 ms; worth it only if counts are high.

## 5. Audio served inside a GE job flushes the GE job's cache [D]

`psp_me_serve_audio` → `me_serve` → `meLibDcacheWritebackInvalidateAll()` before **and** after the mix. Called "every few records" from the GE replay on the ME (b113's fix for audio waiting behind render jobs). Each audio block therefore writes back and drops the ME's whole cache twice in the middle of the render job, and the replay then refills its working set.
- **Correctness is fine** (write-back before invalidate).
- **Cheaper, still correct:** for the audio job, me-core's range ops are available on the ME: `meLibDcacheInvalidateRange` on the audio list and voice streams before the mix (they were written by the main CPU), and `meLibDcacheWritebackRange` on the output block and the voice streams after. The GE job's lines stay cached.
- Size: audio blocks are ~1 per 11.6 ms (512 frames at 44.1 kHz); two whole-cache operations per block inside long render jobs. The ME log's job vs. hand-over split shows the cost per audio job today.

## 6. Whole-cache loops: both ways covered [D] (resolves the brief's §1.2)

me-core's `meLibDcacheWritebackInvalidateAll()` (me-lib.c:121–128) loops 0–8,191 in 64-byte steps and issues `cache 0x14` **twice per index**, the same pattern as Daedalus's MediaEnginePRX. That matches a 2-way, 8 KB-per-way, 16 KB D-cache where each index op takes one way. Combined with b87/b90's 0 differing answers over millions of jobs' outputs [M], no action is needed. (me-core also issues its *range* ops twice per line; harmless.)

## 7. Upstream: ChupathingyCE `b78e6cf` → `f8abf21` [D]

33 commits, 80 files, +2,058/−1,038 since the pin (Oct 8 → Oct 13).
- **Anchor check** (`PSP_FIXUPS_CHECK=1 python3 psp_fixups.py <tree>` on clean copies of both trees): the pin matches everything; head has **2 anchors that no longer match, both in `source/cseries/errors.c`** (`write_to_debug_file`'s opening, and the `fprintf/fflush` tail). Cause: `d4b46c82` and `6e57e4a0` (debug.txt counts repeated lines and rolls to `debug.old.txt` at 64 MB). Re-anchor those two; the PSP's own debug-file handling may want to keep the line counting and skip the 64 MB roll.
- **19 other patched files changed with anchors intact** (actor_perception.c, objects.c, units.c, players.c, game_engine.c, hs.c, hs_runtime.c, sound_manager.c, object_lights.c, render_particles.c, hud_*.c, cache_files_windows.c, xbox_texture_cache.c, profile.c, recorded_animation_playback.c, rasterizer_debug.c, reference_lists.h, network_objects.c). Anchors matching doesn't mean behaviour is unchanged; worth a read of the diffs in the tick's hot files (objects.c, units.c, actor_perception.c) for cost changes.
- **Worth taking:** `4276fc5f` "Fix the decompiled code's latent bugs that were still in", `34ac94d0` "Unset locals get values", `095668d0` bounded format widths, `0209d39c` profiler buffer bound: correctness fixes.
- **For the multiplayer session:** network version **25 → 28** (`cf4feb3a`, `a893a678`, `c1805623`, `6f5064f6`: OpenCE build-169 → build-201), OpenCE signalling v4, multiplayer flashlight past Xbox limits, BSP-switch sky fixes. ChupathingyCE compatibility is paramount for MP, so the MP build should follow these.
- **Exactness note:** a bump changes engine code, so per the handoff's rules, compare by off words within one build, and re-baseline before measuring levers.

## 8. Not reviewed, on purpose

- `psp_ge.c` internals beyond the hand-over (L1 moves them to the ME).
- Tick-side hot paths (posing, biped movement, collision walks): already mapped as levers (X9, X10, X13, N3–N5, E16) with cache-map data; a code review adds little until the master session picks one. The research's patterns for them (copy counts/pointers to locals before loops, padding audit with `pahole`, load→compute→store) are in the brief §4 (N, O, J).
