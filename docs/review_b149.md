# Build 149: review walk-through

b149 is the next release candidate (a tick). It is b148 (`review_b148.md`, the build Sheepdog01 hand-tested on 2026-10-28) plus the tock's work that passed its proofs by the morning of 2026-10-29. This draft covers every change in `git log --first-parent 5462c996..98d64ea4` (32 commits). b148's own work is not reviewed again.

Labels: [M] measured on a PSP, [E] emulator, [D] read in the code, [U] inference. The [M] and [E] figures are copied from the commit messages and the lab's findings. I did not measure them.

## 0. At a glance

| # | Change | Area | Default | Off word | Already in b148? |
|---|---|---|---|---|---|
| 1 | B41's proper fix (`psp_sleep.c`) | sleep | on, but the stopgap still holds the switch | `sleepreadyoff` | no |
| 2 | B41 second-ask fix and the stall reporter | sleep | on | `stallreportoff` (reporter only) | no |
| 3 | B41's stopgap (`psp_sleepguard.c`) | sleep | on | `sleepallowed` | **yes** |
| 4 | The ME's wake words (`mewake*`) | sleep | off | n/a (opt-in) | **yes** |
| 5 | B8 closed: model conversions baked | bake | on: the bake makes them by default (`--no-preconvert`) | `preconvertoff` | no |
| 6 | The bake's log copied to the stick | bake | on | n/a | no |
| 7 | `bake-bundle` runs only by hand | bake | n/a | n/a | no |
| 8 | The kept cache and menu music (snd0) | bake | on | `--no-cache` | **yes** (forwarded) |
| 9 | mechunk made safe (c1safe) | ME | on (no word) | none | no |
| 10 | mevisland | ME and visibility | on | `mevislandoff`, `PSP_MEVISLAND=0` | no |
| 11 | mevauto: where the ME walk runs | ME and visibility | on | `mevislandalways` is the other side | no |
| 12 | mectx: VFPU posing refuses in the ME's context | ME and visibility | on | none | no |
| 13 | VFPUP: quaternion product on the VFPU | tick | on | `novfpupose` | no |
| 14 | VFPUQD: the VFPU's own normalize and interpolate | tick | on | `novfpuquatd` | no |
| 15 | SCRATCHBSP | tick | on | `noscratchbsp` | no |
| 16 | NRCHK | tick | on | `nonrchk` | no |
| 17 | SLEEPDBG and COLLUSAGE | tick | on (layout) | `PSP_DEADCODE_INLINE=1` | no |
| 18 | GSCHECK | tick | checks left out | `gscheck` puts them back | no |
| 19 | ACCLEAF | tick | on (layout) | `PSP_ACCLEAF_OLD=1` | no |
| 20 | OBJPLACE | tick | on | `wsmirroroff`, `hdrclearall` | no |
| 21 | texrecent (B34's partial fix) and texcensus | textures | on at 8 | `texrecent0` | no |
| 22 | carveshare (B34's fix) | textures | on | `carveshareoff` | no |
| 23 | B38: Chief's armour flicker | fix | on | none | **yes** |
| 24 | lanetrace's release stubs | instrument | n/a | n/a | **yes** |
| 25 | Profile `campaign-w149c` | profile | n/a | n/a | no |

**Already in b148, so not reviewed again:**
- B38 (0a590ca).
- B41's stopgap (1bcba0f).
- lanetrace's two fixes (a93cc32, 5d983e5).
- The mewake words (a657956).
- The snd0 and kept-cache bake changes (ab8f28b).
- The b148 public export (50d3962).
- **Evidence** [D]: all seven are cherry-picks or forwards of b148's own commits. `git diff --stat 5462c996 HEAD` lists none of `psp/src/psp_sleepguard.c`, `psp/src/psp_lanetrace.c`, `tools/texpack.py` or `bake/lib/snd0.py`.
- What `psp_me.c` and `psp_platform.c` show in that diff is new work only.

**Not in this review:**
- Docs-only commits (the hardware survey, `docs/b41_sleep_design.md`) and the `bake.py` comment rewording (98d64ea).
- The profiles `campaign-w149a` and `w149b` (§7).

## 1. Sleep and power: B41's proper fix

**The problem** (b148 section 5): a plain sleep crashed the PSP-1000 and corrupted the firmware's settings. The power callback's thread stopped the ME wherever its frame was, and gave the volatile partition back while the game thread and the GE forwarder kept running (`docs/b41_sleep_design.md`, "What goes wrong today"). [D]

**What it does** (new `psp/src/psp_sleep.c`, 388 lines, and `psp_sleep.h`, behind keep word `sleepready`):
- The callback only asks. The game thread stops itself at its next quiet point, which is a frame's first window or a load's frame. It finishes what the GE layer has out, halts the ME, gives the partition back, parks until the wake, then retakes everything and restarts the ME.

**Key lines:**
- The callback asks and waits a second, or two if the game thread had already begun to stop:
  - `psp_sleep.c:82` `#define ASK_MS 1000`
  - `psp_sleep.c:138` `psp_sleep_due |= DUE_SLEEP;`
  - `psp_sleep.c:139` `for (waited = 0; sleep_state != SLEEP_PARKED && waited < ASK_MS; waited++)`
- If the game thread hasn't stopped, the old way runs:
  - `psp_sleep.c:165` `platform_log("sleep: the game didn't stop itself in %d ms (state %d); the old way", ASK_MS, sleep_state);`
- It hooks the power callback:
  - `psp_platform.c:2170` `if ((flags & PSP_POWER_CB_SUSPENDING) && psp_sleep_suspending())`
  - `psp_platform.c:2172` `if ((flags & PSP_POWER_CB_RESUME_COMPLETE) && psp_sleep_resumed())`
- The quiet points are the first window and a load's frame:
  - `psp_ge.c:6197-6198` `if (!window_main_done && !PSP_ME_WORK())` / `psp_sleep_check();`
  - `psp_ge.c:23654-23655` `if (ge_ready)` / `psp_sleep_check();`
  - `psp_sleep.h:32` `if (psp_sleep_due)` is the whole cost when no sleep is asked.
- The GE side is drained in order, with a bound:
  - `psp_ge.c:25141-25147` `if (inflight_pending)` / `inflight_sync();` ... `while (sceGeDrawSync(1) != 0 && waited < 200)`
  - A queue still busy after 200 ms is broken with `sceGeBreak(1, &unused)` (`psp_ge.c:25156`).
- The game thread halts the ME, parks, then restarts it:
  - `psp_sleep.c:318` `psp_me_sleep_stop();`
  - `psp_sleep.c:335` `SceUInt timeout = 60000000;` is the park, bounded at a minute.
  - `psp_sleep.c:350-351` `psp_me_wake_request();` / `psp_me_wake_poll();`
- The restart's kernel call is named for the stall reporter:
  - `psp_me.c:3426` `PSP_WAIT_BEGIN(PSP_WAIT_ME_RESTART);`
- `sleepdryN` runs the same stop and wake without a kernel sleep. It fills the partition with a pattern for 2 s and checks it after (`psp_sleep.c:266-267`, `:272-273`). `powerflags` logs every callback's flags (`psp_sleep.c:106`).

**Default:**
- `psp_platform.c:1115` `{ "sleepready", "sleepreadyoff" },` puts it on by default.
- The stopgap still holds the switch off. `psp_sleep.c:4` says "switch off unless "sleepallowed", until a real sleep has passed by hand." `psp_sleepguard.c:33` still has `result = scePowerLock(0);`.
- [U] So in play, with default keep words, `sleepready` is not exercised by the power switch. A player's sleep does nothing. This was the stopgap's point, and b148 shipped it.

**Second ask while stopped** (9677aef, `psp_sleep.c:66-68`, `:124-130`):
- The game thread's own flag, `game_parked`, says it is waiting. A second slide of the switch returns at once with the stop held (`psp_sleep.c:126-129` `if (game_parked || sleep_state == SLEEP_PARKED)` ... `return 1;`).
- Any wake releases the stop (`release_parked`, `psp_sleep.c:171-187`).
- The stand-in wake used by the dry test does not reopen live files. `psp_sleep.c:180` `if (reopen && !psp_keep("noreopen"))`; the dry stop calls `release_parked(0)` (`:214`).
- The earlier PSP-3000 crash (40828919863) was that reopen, per the commit message. [U] Its cause is research's theory, tested by the fix, not proven separately.

**The stall reporter** (`psp_sleep.c:358-388`, on unless `stallreportoff`):
- It counts each watched wait as it begins and ends. The counts are `PSP_WAIT_SHOW_SYNC`, `PSP_WAIT_SHOW_SETTLE`, `PSP_WAIT_LIST_DROP`, `PSP_WAIT_LOADING_SYNC` and `PSP_WAIT_ME_RESTART` in `psp_sleep.h`. The heartbeat looks every 5 s (`psp_platform.c:3701` `psp_sleep_stall_check();`, after `beat > 20`).
- A wait at the same odd count on two looks is named. On the third look the GE queue is stopped: `psp_sleep.c:379` `else if (looks[k] == 2 && k != PSP_WAIT_ME_RESTART)`.
- [U] This is the one part that acts in play without a sleep. A genuine wait over ~10 s at one of those four GE sites would be cut. The design says the sites can't legitimately wait that long (`docs/b41_sleep_design.md`, "Bounded waits everywhere").

**Proof named:**
- sleepdry1 40819287900 [E]: play and seeds identical, 21 stops.
- PSP-3000 pair 40822061362 [M]: 10 of 10 dry stops clean, the real ME restarted in 1 ms.
- Compile 40824084938.
- 9677aef: PSP-3000 40832569434 [M], 10 dry stops each asked twice, all held.
- **Not proven:** a real kernel sleep with `sleepready`. The design doc's status line (`docs/b41_sleep_design.md:3`) lists "Sheepdog01's one real sleep" as left.

## 2. The bake

**B8 closed: the model conversions baked (PRECONVERT; a35868f).** Each model vertex buffer an opaque draw converts at run time is converted by the bake instead.
- **Bake side:** `bake.py --preconvert` runs `tools/modelconvert.py` last for each map but `ui` (`bake/bake.py:557-559`):
  - `if PRECONVERT and name != 'ui':`
  - `run([sys.executable, os.path.join(LIB, 'modelconvert.py'), final, pgeo], log_path, cwd=LIB, env=env)`
  - The bundle carries the tool (`bake/bundle/assemble.py:24`).
- **On by default in the bake (the cut's own change):** `PRECONVERT = True` (`bake/bake.py:126`). `--no-preconvert` leaves it out, and `--preconvert` stays for old scripts. The guided bake passes no flag (`bake/guided.py:759`), so a player's bake now makes the baked blocks: B8 was closed by baking the conversions (Sheepdog01, 07:42), and a fix that only an opt-in flag reaches doesn't reach players. Packs grow 2.5-5 MB a map (~42 MB over the campaign); the heap gets ~200 KB back.
- **Loader side:**
  - The pack header's word 15 marks it. `psp_geo.c:1302` `if (header[15])`, and `psp_geo.c:219` `#define PCV_VERSION 0x31564350u /* 'PCV1' */`.
  - Refusals are logged with a reason and the run-time path is kept: an unknown version, `preconvertoff`, `model14`, the main menu (`psp_geo.c:1305-1307`), a region that doesn't fit, or no heap.
  - `psp_vertex_lock` returns the baked block's records (`psp_geo.c:1589`). A block whose header names another buffer falls back to the raw one (`:1609` `if (block[0] != PCV_MAGIC || block[7] != address)`).
  - The see-through queue locks the raw buffer (`psp_vertex_lock_raw`) and converts its own rigid copy, as before.
  - A skinned block's offsets become addresses after each read and are range-checked against the block (`psp_ge.c:12356` `psp_ge_pcv_relocate`).
  - `model_convert` answers a baked buffer at once on either CPU (`psp_ge.c:12564` `if (psp_pcv_on)`), and `model_converted_now` tells modelpretouch there is nothing to do (`psp_ge.c:15778`).
- **Why a second round:** the first PSP pair lost badly on b30's flight (frame +36%). A baked buffer never got a VRAM copy, because only main-CPU conversions called `vm_note`. `psp_ge_pcv_locked` (`psp_ge.c:12419`) now notes it at the block's first lock after a read (`docs/plan_preconvert.md`, "Round 2").
- **Compatibility:** BAKE_FORMAT is still 2 (`bake/bake.py:171`) [D]: the region rides past the pack's data, so old bakes load as before. Older builds read only up to header word 3 and never see the region (`psp_geo.c` comment above `PCV_VERSION`).
- **Check word:** `preconvertcheck` converts each block's raw buffer at run time once a load and compares records, plans and sphere bits (`psp_ge.c:12751`).
- **Proof named:**
  - preconv6 40876831542 [E]: play and seeds identical on the new bake (b30 3,600 ticks, c10 7,200), `preconvertcheck` 0 of 262 differ, strict 0.
  - Identical to the tock on bake w148a.
  - PSP-3000 round 2 (40845838294) [M]: even. No speed gain; the gain is heap (~200 KB) and the removed conversions (b30 235 -> 41, c10 158 -> 18 [E]).
  - Pack cost: +2.5-5 MB a campaign map.
- **To question:**
  - Its PSP rows are "even", so what it buys is heap and fewer run-time conversions. It costs 42 MB over ten maps on the stick.

**The bake's log on the stick (c3c9d55, 0d462b0).**
- `bake/guided.py:589-600` `copy_log_to_stick`: it writes `halo/log/bake/bake-log.txt` after the log is closed, only on success (`:659-660` `if code == 0:`), best-effort (`except OSError: pass`).
- `guided_check.py` asserts the copy equals the bundle's log and contains `finished in`.
- Why: the usual pull of `halo/log` brings the bake's story back with the game's. The game's rotation touches only `log/NNN_*`.
- `docs/play.md:162` lists the file.

**`bake-bundle` by hand only (82c6fe4).** `.github/workflows/bake-bundle.yml` loses its `push: tags: ['b*']` trigger and keeps `workflow_dispatch` (`:23`). Release bundles are now built by hand.

**Forwarded from b148, not reviewed again:** the kept cache, the menu's music, the snd0 retail fix (ab8f28b). `bake.py` changed since for preconvert (§2) and the menu sound's comment (98d64ea).

## 3. The ME and visibility

### 3.1 mechunk made safe (c1safe; 772279a)

**What it fixes:**
- mechunk hands the ME the frame in pieces while the main CPU still writes it. Words that both CPUs write on one cache line lose one side's value when either CPU writes the line back.
- Research's cache-model trace (mechunkall) found 9 lost lines [E] (the merge message's "9 -> 0").
- Everything is placement and bookkeeping. No keep word, and it computes nothing differently.

**Key lines:**
- Each CPU has its own counters, on lines of their own:
  - `psp_ge.c:4235-4236` `static unsigned long ge_entry_us_by[2][16] __attribute__((aligned(64)));` / `#define ge_entry_us (ge_entry_us_by[PSP_ME_WORK() ? 1 : 0])`
- The main CPU's words are marked:
  - `psp_ge.c:280` `static PSP_MAIN_SIDE unsigned long stall_forwarded;`
  - `psp_ge.c:3690` `static PSP_MAIN_SIDE unsigned long stall_forwarded = 0, stall_timed = 0, stall_timed_us = 0;`
- Tests that read main-CPU words now test `psp_on_me()` first, so the ME never reads those words' lines at all:
  - `psp_ge.c:604` `#define DEFER_RECORDING() (!psp_on_me() && defer_on && !defer_replaying)`
- The ME's texture bookkeeping is kept apart and merged by the main CPU at the frame's end:
  - `psp_tex.c:229-234` `} me_book __attribute__((aligned(64)));`
  - `psp_tex.c:239` `static void frame_set_mark(int index)`
  - `psp_tex.c:3113` `me_book_merge();`
- The geometry table takes only protected entries from the ME, so a half-rewritten entry can't pass for the block it wants:
  - `psp_geo.c:996` `static int entry_holds_for(int index, unsigned int at, int me)`
  - `psp_geo.c:993` counts the refusals: `static unsigned long me_unprotected[16] __attribute__((aligned(64)));`
- The ME's rays read their own copy of the owed node-matrix bits:
  - `psp_fixups.py:29596` `unsigned long psp_nms_me[16 + ...] __attribute__((aligned(64)));`
  - `psp_fixups.py:29598` `void psp_nms_pending_for_me(void)`
  - `psp_fixups.py:29484` reads it: `if (psp_nms_me[0] && ((psp_nms_me[16 + ((unsigned short)absolute_index >> 5)] >> (absolute_index & 31)) & 1))`
- The emulator's chunk publish now writes back first, as the PSP path does (`psp_ge.c:2135` `sceKernelDcacheWritebackAll();`).

**Proof named:** c1safe1-3 exact (play and seeds identical); the mechunkall trace's lost lines 9 -> 0 [E]. No PSP pair was named for c1safe itself. The profile (§7) and the candidate's PSP rows (§8) run with it. This is placement of words, so by the Sheepdog01 2026-10-27 08:18 rule its price belongs to a PSP pair; the candidate pairs in §8 are that.

### 3.2 mevisland (60db1e9)

**What it does:** the frame's visibility (which clusters and objects are seen) is made on the ME. The push happens where the main CPU's own would run. The collect happens at its first reader, `lights_preprocess_scene`. No tick runs between, and the span's work doesn't write what visibility reads, so the answer is the main CPU's bit for bit (`docs/mevis_landing.md`, "Where the ME's visibility can overlap"). [D, claim in a doc; the proof is the emulator's identical play]

**Key lines:**
- Hooked in `render_window`:
  - `psp_fixups.py:36527-36528` `if (!psp_mevisland_push())` / `structure_visibility_compute();`
  - `psp_fixups.py:36540` `psp_mevisland_collect();`, just before `lights_preprocess_scene();`
  - `psp_fixups.py:36552` `psp_mevisland_settle();` collects a view that never reached its reader (a film).
- The object list comes from the ME, and the marks are made again on the main CPU:
  - `psp_fixups.py:36565` `if (!psp_mevisland_objects(render_object_globals.rendered_object_indices, &render_object_globals.rendered_object_count))`
  - `psp_fixups.py:38926-38928` `object_marker_begin();` / `for (k = 0; k < n; k++)` / `object_mark_function(indices[k]);`
- The job and its answer:
  - `psp_me.c:2576` `int psp_me_push_visibility_into(void *record, void *answer, unsigned int answer_bytes, unsigned int *ticket)`
  - `psp_me.c:1060` `#if defined(PSP_WALKAPART) || defined(PSP_MEVISLAND)`
- Safety net (`psp_fixups.py`):
  - `:38711` `#define PSP_ML_LATE_US 50000`
  - A refused push returns 0 and the main CPU makes its own (`:38799-38803`).
  - A late answer: `psp_ml_late++; structure_visibility_compute();` (`:38829-38833`).
  - A walk cut at `PSP_MV_DEPTH` or past its stack is made again here (`:38839-38852`).
  - The depth is `#define PSP_MV_DEPTH 14` and the stack `#define PSP_MV_STACK (72 * 1024)` (`:38296-38297`).
- **Default:** `psp_platform.c:1120` `{ "mevisland", "mevislandoff" }, PSP_RELEASE_DEFAULTS`. It is built in unless `PSP_MEVISLAND=0` (`psp_build.py`, `MEVISLAND = os.environ.get("PSP_MEVISLAND", "") != "0"`), which also drops 23 KB of the visibility copies. `mevislandcheck` makes the main CPU's own too and counts differences (`psp_fixups.py:38758-38759`).

**Proof named:**
- PSP-3000 b30 pair 40845560977 [M]: drawing -2.2 ms in the flight, -4.4 ms at the landing.
- mevl7 40864813654 [E] on the merged tree: play and seeds identical; c20 within its own spread.
- The landing holds 152 KB of heap. The 144 KB state and 160 KB stack of mevis were cut (72 KB stack, depth 14) because they pushed the level's material copies into the carve (`docs/mevis_landing.md`, "Its memory").

**To question:**
- On b30 the main CPU still waits 2.8-3.8 ms a view at the first reader (`docs/mevis_landing.md`, "Step 2's result"). The win is the overlap, not the ME's speed.

### 3.3 mevauto: where the walk runs, by measurement (be46097)

**Why:** c10's span before the first reader is too short to hide the ME's walk, so the main CPU waited it out. The lab's c10 PSP-1000 pair read tick +0.8 to +1.5 ms (`docs/mevis_landing.md`, "Placement by measurement"). Under the placement rule, that is a placement to measure view by view.

**Key lines** (`psp_fixups.py`):
- `:38726-38731` the constants: `PSP_ML_MARGIN_US 500`, `PSP_ML_MIN_WALK_US 1500`, `PSP_ML_CHECK 32`, `PSP_ML_REST 240`, `PSP_ML_PUSHES_BEFORE_REFRESH 600`, `PSP_ML_REFRESH 16`.
- A view made on the main CPU is timed (`:38783-38792`). `if (!psp_ml_always && psp_ml_rest > 0)` ... `psp_ml_main_avg = psp_ml_main_avg ? (psp_ml_main_avg * 7 + took * 8) / 8 : took * 8;`
- Each wait for the ME's answer is timed, and the decision is taken every 32 used answers (`:38878-38888`): `if (psp_ml_wait_avg / 8 + PSP_ML_MARGIN_US >= psp_ml_main_avg / 8 || psp_ml_main_avg / 8 < PSP_ML_MIN_WALK_US)` sets `psp_ml_rest = PSP_ML_REST;`.
- `mevislandalways` (or `mevislandcheck`) pushes every view (`:38759`), the A/B's other side.

**Default:** on with mevisland. Both paths are exact, so the choice changes no answer.

**Proof named:** PSP-1000 c10 40879669115 [M] (off | default | always, 2 runs each): tick -0.6 ms (-2.8%, real) to even; frame -1.1 to -4.1 ms; drawing -0.7 to -3.9 ms; the placement beats always-push. PSP-3000 b30 40887002037 [M] (default | always, 2 runs each): the cinematic frame -2.5%, drawing -6.4%, tick -2.2% (real); the flight's drawing +0.5 ms (+1.1%, real), its frame and tick even. Exact: mevl8 on b30, a10, c20 [E]. The merge message adds that the c10 tick cost in the tock-vs-b148 pricing came from the two tocks' profiles, not mevisland.

**To question:**
- The numbers are two runs of one build. The first 32 views are on the main CPU (`psp_ml_rest = PSP_ML_CHECK` at `:38732`), then it decides.
- On b30, the candidate's own pair puts the tock ~1% behind w149b on the flight's frame (§8; mevauto's own b30 pair, 40887002037, prices that drawing at +0.5 ms).

### 3.4 mectx (3d77078)

- **What:** VFPU posing's guards now also refuse while `psp_me_context` marks the ME's work run on the main CPU:
  - `psp_vfpu_quat.c:516` `return (stack >> 27) == 1 && !psp_me_context;`
  - `psp_vfpu.c:552` `if ((stack >> 27) != 1 || psp_me_context)`
- **Why:** the ME has no VFPU, and the guards tell the ME apart by its stack. Where the ME's work runs on the main CPU (the emulator's stand-in, or a job taken back by the fallback), the stack looks like the main CPU's, so the ME's share posed with the VFPU forms. Now such work poses with the FPU, as the real ME does (the lab's levers row: "on the PSP only the fallback replay's posing").
- **Default:** on, no word. `novfpuquatd` still turns VFPUQD off.
- **Proof named:** mectx1/2 [E]: `novfpuquatd` old = new every tick; the emulator's main-CPU VFPU normalizes fall 937 -> 108 a frame on b30; A/A identical.
- **To question [U]:** this fixes the ME's share only. The main CPU's own poses use the VFPU's D forms (§4.2), so a pose's last bits depend on which CPU makes it. That is the design (local only), but statecrc cannot see it.

## 4. The tick's speed levers

Each is priced in the lab's findings. PSP figures say which PSP.

### 4.1 VFPUP: the quaternion product on the VFPU (46447ea)

- **What:** `quaternions_multiply` goes to the VFPU's element-wise form, which keeps each lane's operations in the engine's order. Its bits are the FPU's (20,000 of 20,000 the same on the PSP-3000's vfpubench, per the commit's fixups comment). [D for the code; the 20,000 is a lab figure]
- **Key lines:**
  - `psp_fixups.py:37307` `if (psp_vfpupose_on && psp_vfpu_quaternions_multiply(q0, q1, result))`
  - `psp_vfpu.c:596` `psp_vfpupose_on = differ == 0;`: it turns itself off if 2,000 start-up products differ from the FPU's.
  - `psp_vfpu.c:675` `if (psp_vfpu_on && !psp_keep("novfpupose"))`
  - `vfpuposecheck` plays the FPU's bits and counts differences (`psp_vfpu.c:554-563`).
- **Default:** on. Off word `novfpupose`.
- **Proof:** hwpose1 40740687145 [E], play and both seeds identical on b30 and c20. PSP-3000 pair 40741608139 [M]: tick ~0.2 ms faster on every stretch, the cinematic frame -1.6%.

### 4.2 VFPUQD: the VFPU's own normalize and interpolate (d8bc529)

- **What:** `quaternion_normalize` and `quaternions_interpolate_and_normalize` use vdot.q and vrsq.s (`psp/src/psp_vfpu_quat.c`, new, 666 lines). **Not bit-exact:** the file header says the D forms "round their own way (within 12 last bits on silicon)" (`psp_vfpu_quat.c:14`).
- **Key lines:**
  - `psp_fixups.py:37340` `if (psp_vfpuquat_mode == 2 && psp_vfpu_quaternion_normalize(q))`
  - `psp_vfpu_quat.c:619` `mode = !psp_keep("novfpuquatd") ? 2 : psp_keep("vfpuquat") ? 1 : 0;`
  - `psp_vfpu_quat.c:659` `else if (mode == 2 && (tally_interpolate.worst > 64 || tally_normalize.worst > 64))` turns it off at start-up.
  - Each entry answers 0 on the ME (no VFPU) and the engine's FPU code runs.
  - `psp_platform.c:4150` `psp_vfpu_quat_initialize();` is called after the matrix product's check.
- **Default:** on (`mode` is 2 unless `novfpuquatd`). `vfpuquat` is the exact interpolation instead, off.
- **Proof named:** census nothing broken (b30 40771077761, c20 40771289569 [E]); PSP-3000 pair 40775475465 [M]: b30 tick -0.8 ms in the flight, -0.3 ms in the cinematic. Sheepdog01 16:13 chose it on.
- **To question:**
  - The start-up gate accepts 64 ulp; the bench said 12. A change that isn't bit-exact is acceptable only for local-only state, by the Sheepdog01 2026-10-28 12:27 rule. In the campaign the player's PSP is also the server, so "local" holds for multiplayer and not for campaign play. The proof named is a census and a pair, not statecrc, as the rule allows.
  - `psp_vfpu_quat.c:5` still says "keep word vfpupose"; the off word is `novfpupose`. Comment only.

### 4.3 SCRATCHBSP (1999aa4)

- **What:** the level collision BSP's top nodes, each with its plane, copied breadth first into the scratchpad. The walks start at the root, and the tag data spreads those nodes over hundreds of cache lines (`psp/include/psp_scratchbsp.h:3-8`).
- **Key lines:**
  - `psp_scratch.c:373` `if (psp_keep("noscratchbsp"))` leaves it off.
  - `psp_scratch.c:443` `slot->children[c] = PSP_SB_TAG | (long)used;` marks a copied child.
  - `psp_scratchbsp.h:45-46` `#define PSP_SB_HERE(bsp, index) (((index) & PSP_SB_TAG) || ((index) == 0 && !psp_sb_on_me() && (void const *)(bsp) == psp_sb_tree))`
  - `psp_ge.c:4586` `psp_scratch_bsp_take();` runs before `cut_take()`.
- **Why safe:** the ME can't see the scratchpad, so its walks read the tag data; the copy has the same bits (`psp_scratchbsp.h:15-17`). A `PSP_MAP_CHECKS` build leaves it out (`psp_scratch.c`, `#ifdef PSP_MAP_CHECKS`).
- **Cost to something else:** the models' cut corners go to the heap (`psp_scratch.c:347-351`, "the models' cut corners go to the heap").
- **Default:** on, off word `noscratchbsp`.
- **Proof:** play and seeds identical on b30, c20, d20 (40770037236 [E]). PSP-3000 pair 40775509445 [M]: b30 tick -0.3 ms on the cinematic and the flight.

### 4.4 NRCHK (0f6d3f2)

- **What:** the root node's validity test in `object_compute_node_matrices` is left out. Its failing branch only writes `debug.txt`, so play can't change (`psp_fixups.py` comment above `FIXES_NRCHK`).
- **Key lines:**
  - `psp_fixups.py:37460` `psp_nrchk_mode = psp_keep("nrchknan") ? 1 : psp_keep("nonrchk") ? 0 : 2;`
  - `nrchknan` keeps only the 13 exponent checks for NaN or infinity.
- **Default:** the test is left out. `nonrchk` puts it back.
- **Proof:** exact (the test only writes `debug.txt`). PSP-3000 c10 40793130396 [M]: tick -5 to -8%. b30 40795042670: tick -0.5 to -1.5%.

### 4.5 SLEEPDBG and COLLUSAGE (0645045)

- **What:** `psp_sleepy_update`'s debug reports (`sleepdebug`, `itemsleepcheck`, `flipwatch`, `checksumcheck`) move into cold, never-inlined functions called under the same tests. The collision usage log's empty calls compile to nothing. This is a layout change, so it is a build switch, not a keep word.
- **Key lines:**
  - `psp_fixups.py:37543` `static void __attribute__((noinline, cold)) psp_checksum_check_sum(...)`
  - `psp_fixups.py:37575` `psp_sleep_debug_carries`, and `:37606` `psp_flipwatch_sleep_log`.
  - `psp_fixups.py:37534` `DEADCODE_INLINE = os.environ.get("PSP_DEADCODE_INLINE", "") == "1"` builds the old layout.
- **Default:** the new layout.
- **Proof:** deadchk1 40807412345 [E] exact. PSP-3000 c10 without PGO (40812138009 vs 40812168360) [M]: tick -0.3 to -0.5 ms (-1.2 to -2.5%).
- **Caveat from the commit:** with the stale campaign-w142g profile the new side measured +5 to +8%, because 8 functions lost their counts. "Its next PGO build wants a profile counted on this code" (that is §7).

### 4.6 GSCHECK (4a589fd)

- **What:** the tick's two `game_state_check_data_arrays` walks are left out. They only log on failure, and nothing else reads their state.
- **Key lines:** `psp_fixups.py:38125` `psp_gscheck_on = psp_keep("gscheck");`, then `if (psp_gscheck_on)` before each call.
- **Default:** the checks are off. `gscheck` puts them back, for NATIVE-7's witness (Sheepdog01 21:14). The `lights_port_recover` call stays.
- **Proof:** deadchk1 [E] exact. PSP-3000 c10 40809345943 [M]: tick -0.2 to -0.3 ms early.
- Not a conflict: 0645045 says it "supersedes gscheck", but `FIXES_GSCHECK` is in the tree.

### 4.7 ACCLEAF (2fea725)

- **What:** the keep tests on `object_get`, `object_header_block_get`, `object_get_and_verify_type` and `datum_get`'s found path are removed. The same pointers; only the tests go.
- **Key lines:**
  - `psp_fixups.py:38156-38157` the replacement is `return object_header_get(object_index)->datum;`
  - `psp_fixups.py:38151` `ACCLEAF_OLD = os.environ.get("PSP_ACCLEAF_OLD", "") == "1"` builds the old side.
- **Default:** tests out. `objallold`, `hdrblkold`, `notypehdr` and `dgfastold` work only in a `PSP_ACCLEAF_OLD=1` build.
- **Proof:** accleaf1 40831782100 [E], play and both seeds identical, b30 and c10, strict 0.
- **To question:** the lab's lever row: PSP-1000 c10 no PGO, tick -1.0 ms (-3 to -6%), **drawing +1.1-1.2 ms ("layout, being tested")**, frame -0.4 to -1.4% [M]. The candidate's c10 rows in §8 are net positive with this in, but nothing in the repo says the drawing rise was explained.

### 4.8 OBJPLACE (4b62976)

- **What:** the object walk's passes over settled scenery answer "no child, not garbage" from a bit beside the sleep tables, instead of two lines of each sleeper's datum. `objects_update`'s last loop stores the do-not-update clear only into headers that have it set.
- **Key lines** (`psp_fixups.py`):
  - `:30601` `static unsigned long psp_ws_clear[(MAXIMUM_OBJECTS_PER_MAP + 31) / 32];`
  - `:30633` `psp_ws_unnote(absolute_index);` clears the bit as the full path starts.
  - `:30643` and `:30651` `psp_ws_note(absolute_index, header);` set it where the full path leaves.
  - `:30707` `if (!psp_ws_said_clear(absolute_index, header))` is the fast answer.
  - `:30729` `if (psp_hdr_clear_all || TEST_FLAG(object_header->flags, _object_header_do_not_update_bit))`
  - `:30627` the bits are cleared when a game state loads.
- **Why exact:** a set bit says "no child and not garbage". The comment says attaching, detaching or `object_set_garbage` marks the object, a marked sleeper takes the full path, and the full path clears the bit first (`:30597-30600`). [D for the comment; I did not trace every writer. `wsmirrorcheck` is the proof.]
- **Default:** on. `wsmirroroff` and `hdrclearall` put the old reads and stores back; `wsmirrorcheck` compares.
- **Proof:** objp1 40832948572 [E]: play and seeds identical every tick, the check 0 wrong bits of 0.76 M (b30) and 2.0 M (c10), strict 0. Bus census: object pool b30 1,210 -> 1,141 KB a frame, c10 691 -> 614. PSP-1000 c10 40839486853 [M] (the lab's row, not the merge message): tick -0.2 to -0.3 ms (-1.0 to -1.3%), frame in the noise. It is a cache change, so this is the PSP price the lab rule wants.

### 4.9 texrecent (B34's partial fix; ca4b40d, 504e6b8)

- **What:** a middle pass in `load_into` that still keeps what was drawn in the last N frames, so a texture drawn on alternate frames isn't taken while staler ones sit further round the ring. `texcensus` (test) logs which textures go without and what evicted them.
- **Key lines:**
  - `psp_tex.c:1851` `recent_middle = psp_keep_number("texrecent", 8);`
  - `psp_tex.c:1252` `census = psp_keep("texcensus") ? calloc(...) : NULL;`
- **Default:** on at 8. `texrecent0` turns it off (a value of 2 or less disables the pass).
- **Proof:** PSP-1000, a10 on Sheepdog01's path, one run against the census's [M]: waits for room 609 -> 154, untextured frames 164 -> 123; the boarding craft kept three of its four looks. texr1 40770268439 [E]: play and seeds identical on b30, a10, c20, d20, a30.
- **To question:**
  - It touches the texture cache, so its price is PSP-priced (Sheepdog01 2026-10-27 08:18). The only pair is one PSP-1000 run a side.
  - The emulator note: "under a cut cache it parts from off only where any texture-read change does".
  - The proper fix (only the mip levels sampled, TEXFIT1) is still ahead.

### 4.10 carveshare (B34's fix; b9f08f7)

- **What:** the level carve's idle 128 KB steps are lent back to the texture RAM cache and taken back when the copies want room.
  - The copies sit at the carve's top, so its free bytes gather at its bottom, next to the texture cache.
  - A bottom step free for 8 frame ends in a row, in a frame that wanted no growth, goes back, one a frame.
- **Key lines:**
  - `psp_ge.c:7145` `#define CARVE_SHARE_FRAMES 8`
  - `psp_ge.c:7996` `carve_share = psp_keep("carveshare") != 0;`
  - `psp_ge.c:8134` `static void __attribute__((cold)) carve_share_frame_end(unsigned int deficit)`
  - `psp_ge.c:7506` `carve_compact_up` moves used blocks to the top, in order.
  - `psp_ge.c:8140` onward settles the GE before the carve moves.
- **Default:** on, `carveshareoff` off. `psp_platform.c:1115`.
- **Proof:** PSP-3000 pair 40796321893 (a10) [M]: room waits 515 -> 0, frames drawing something untextured 181 -> 48 (longest run 60 -> 12), frame time unchanged (24.5 vs 24.2 ms). carve1 40787855363 [E]: five maps identical, no lendings where the carve doesn't idle, strict 0.

## 5. Already in b148 (forwarded; do not review twice)

| Change | Commit | Where it was reviewed |
|---|---|---|
| B38: Chief's armour flicker (main-CPU variables on lines of their own) | 0a590ca | b148 section 4 |
| B41's stopgap (`psp_sleepguard.c`) | 1bcba0f | b148 section 5 |
| lanetrace's release hooks and `*_off` stubs | a93cc32, 5d983e5 | b148 section 7 |
| The ME's wake words (`mewakefw`, `mewakeearly`, `mewakeprobe`), opt-in | a657956 | b148 section 3 |
| The kept cache, menu music, snd0 retail fix | ab8f28b | b148 section 6 |
| b148's public export | 50d3962 | n/a (`public/README.md` went back to b147h's in the tock; the cut brings b148's back) |

Evidence [D]: `git diff --stat 5462c996 HEAD` has none of `psp_sleepguard.c`, `psp_lanetrace.c`, `texpack.py` or `snd0.py`. `bake.py` changed for preconvert (§2) and a comment's wording.

## 6. Under the hood

- `visspan` (recon build only, `psp_ge.c`) times the span from visibility to its first reader. It is the step-1 instrument for mevisland and is compiled out otherwise.
- The campaign `CLAUDE.md` gains `PSP_MEVISLAND=0`, `PSP_DEADCODE_INLINE=1` and `PSP_ACCLEAF_OLD=1` in the build-switches table. `docs/optimization_ranking.md` gains NRCHK, SLEEPDBG, GSCHECK, ACCLEAF and OBJPLACE.
- `tools/cachemap.py` gained scratchpad naming for the cache map's census.

## 7. The profile

`pgo/campaign-w149c.tgz` (86247f1): the tock `be460974`'s own profile, 13 counting runs on the preconvert bake `preconv1` (pgo-gen149ca/cb 40886416979, 40886467539). Why it matters:
- The tick's levers changed counted functions (ACCLEAF, SLEEPDBG, OBJPLACE, mevisland).
- SLEEPDBG's note says a stale profile cost +5 to +8%.
- `campaign-w148c/d` are removed.
- The batch `rc149a` names it with `PSP_PGO_STRICT=1` (`rc149a.toml:7`). Its "about" line quotes a strict build 40887888962 with 0 functions without counts [D, a lab file I read; not a result I was given].
- `campaign-w149a` and `w149b` (profiles of earlier tock states) are still in `pgo/`. They are not used by this tick.

## 8. Proofs

| What | Run | Result |
|---|---|---|
| B41 sleepdry1 (emulator) | 40819287900 | play and seeds identical, 21 stops |
| B41 dry stops, PSP-3000 | 40822061362 | 10 of 10 clean, ME restarted in 1 ms |
| B41 second ask, PSP-3000 | 40832569434 | 10 stops each asked twice, all held |
| mevisland, b30, PSP-3000 | 40845560977 | drawing -2.2 ms flight, -4.4 ms landing |
| mevisland exactness | mevl7 40864813654 | play and seeds identical |
| mevauto, c10, PSP-1000 | 40879669115 | tick -0.6 ms to even; frame -1.1 to -4.1 ms |
| preconvert exactness | preconv6 40876831542 | play and seeds identical, check 0 of 262, strict 0 |
| preconvert, PSP-3000 | 40845838294 | even |
| ACCLEAF exactness | accleaf1 40831782100 | identical, strict 0 |
| OBJPLACE exactness | objp1 40832948572 | identical, check 0 wrong of 0.76 M and 2.0 M |
| NRCHK, PSP-3000 | 40793130396 (c10), 40795042670 (b30) | tick -5 to -8% (c10), -0.5 to -1.5% (b30) |
| SCRATCHBSP, PSP-3000 | 40775509445 | b30 tick -0.3 ms |
| VFPUQD, PSP-3000 | 40775475465 | b30 tick -0.8 ms flight |
| carveshare, PSP-3000 | 40796321893 | room waits 515 -> 0; frame unchanged |
| texrecent, PSP-1000 | (one run; census 40770268439 is the emulator side) | waits 609 -> 154 |
| The candidate against b148, PSP-3000 b30 | 40888171017 vs 40847119413 | frame -0.7 to -3.0%, tick -3.3% on the flight |
| The candidate against b148, PSP-1000 c10 | 40889089669 vs 40846990494 | frame -3.8 to -7.8%, real in every stretch |
| Release check | `rc149a` at 98d64ea4: the cut commit (73b4dca) changes only the bake, docs and a comment, and builds the same EBOOT (581c423dbc4a89ba, strict, 0 functions without counts) | sierra-batch 40893224278 |

Notes:
- The candidate pairs ran on bake w148a, not a preconvert bake [D, the lab's rows name it]. So they say nothing about preconvert's effect.
- The two candidate rows are PGO each, strict, `w142a_tgc90` layout, 2 runs each.

## 9. Things to question

- **`sleepready` is on, and unreachable.** The stopgap still holds the switch. So the proper fix is untested against the real power switch with a default keep.txt. The sleep test plan (below) is what turns the hold off.
- **Preconvert is now the bake's default** (it was opt-in in the tock): the release check runs on `preconv1`, a bake made with it, and the hand test re-bakes with the new bundle.
- **VFPUQD isn't bit-exact** (up to 64 ulp accepted). It was on Sheepdog01's say (16:13). The PSP-3000 b30 pair and the census are its evidence.
- **ACCLEAF's drawing +1.1 ms** on the PSP-1000 (no PGO) has no explanation in the repo.
- **mevisland's doc** had the walk's depth and stack wrong; fixed at the cut (14 deep, 72 KB: `PSP_MV_DEPTH`, `PSP_MV_STACK`).
- **The stall reporter acts in play.** It stops the GE's queue on a third look. Off word `stallreportoff`.
- **`psp_sleep.c:67`'s comment** carried a date and a name; reworded at the cut, no line moved (the profile still fits).
- **A hand test of b149 itself has not happened.** The candidate's own PSP pairs name bake w148a, and only the release check runs on the preconvert bake players will make.

## Sheepdog01's hand test (PSP-1000)
- **b148's release docs** lived on b148's own branch; the cut brings them into the tock, with `play.md` and `public/README.md` from b148, brought to b149.

Suggested order; none of it has been run.
1. Re-bake with b149's bundle (the conversions are now baked: B8), then play b149 on it as usual (a10 for 18 min as b148's test did). Look for: no flicker (B38), no untextured boarding craft (texrecent, carveshare), heap failures 0, ME failures 0.
2. Add `powerflags` to keep.txt and slide the switch once with the stopgap on (B41's step 0). The log's `power: callback flags` line says whether the suspend reaches the callback while the lock is held.
3. **Only then**, with `sleepallowed` in keep.txt, the one real sleep (B41's last open step): play 1 min, sleep ~10 s, wake, play 30 s, HOME, exit. The log should say `sleep: the game stopped itself in N ms; the PSP may sleep`, then `sleep: awake;`, then `sleep: sleepready: going on after N ms stopped`. A hung exit needs a battery pull, as on b124 and b132, so do it last.
4. Play b30's opening on the new bake: the near Pelican shouldn't drop out at its start (B8).
