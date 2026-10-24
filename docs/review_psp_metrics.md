# Review: `PSP_METRICS`, each map's figures in the log

The heartbeat's lines are 5 s windows, and its cache and heap lines run from the
launch, so a map's own figures had to be cut out of a log by hand, and nothing
said what a slow frame was doing. `PSP_METRICS` adds four figures per map,
written as each map ends (a load begins, or the game exits) and every 60 s of
game time meanwhile (`so far`), so a run stopped from outside still leaves its
last map's figures. `tools/hwreport.py` prints them as a per-map table.

## The switch

- **On in every build unless `PSP_METRICS=0`** (`psp_build.py`): test builds get
  it with no change to how they're made. The final release is built with
  `PSP_METRICS=0`, and a build beside the others with it set keeps the
  stripping proven: it must build, and its link map and EBOOT must hold none
  of the `metrics ` strings or `psp_metrics_` symbols.
- **Every hook is a macro** (`psp/include/psp_metrics.h`). With the switch off
  each compiles to nothing, or to the one call it stands in for, and
  `psp/src/psp_metrics.c` is empty.
- **No hook adds a call site.** A new call changes its host's control flow, and
  the profile-guided build then drops the host's counts (the first build lost
  them for `psp_frame_account`, `exit_game`, `loading_begin` and
  `psp_loading_end`). So each hook that calls takes the place of a call its
  host already makes (`psp_frametrace_tick`, `psp_frametrace_frame`,
  `psp_heap_map`, `psp_me_exit`) and makes that call itself. The readers'
  stick counts and the checkpoint mark are plain stores.

## The lines

| Line | What | From |
|---|---|---|
| `metrics budget (MAP, ends)` | each second of play: tick ms/s (ticks/s x tick ms), draw ms/s (fps x draw ms), frame end ms/s, rest ms/s, adding to 1,000 | the tick and frame accounting (`psp_platform.c`) |
| `metrics hitches (MAP, ends)` | frames over 100, 250 and 500 ms; the worst; the >100 ms frames by cause | frame to frame, as frametrace's |
| `metrics hitch (MAP)` | each frame over 250 ms as it happens (32 a map): its cause, ticks, draw, end, each reader's stick time | |
| `metrics io (MAP, ends)` | the map's own blocking reads per reader: reads, KB, ms | `psp_tex.c`, `psp_geo.c`, `psp_anim.c` |
| `metrics heap (MAP, ends)` | the heap's free low point and its largest free block's low point (with the game time of each), in-use peak, failed requests, user memory's low points | a walk of the heap's chunks every 5 s of game time (`psp_heap_survey`, `psp_libc.c`) |

A hitch's cause is the first that fits: a checkpoint written or read since the
last frame (`psp_state_write`, `psp_state_read`), a cutscene running, one
reader's stick time over a quarter of the frame (the largest), the map's first
30 frames, or other. The heap's free space counts the top chunk with what the
system hasn't handed over yet, since a request can grow into both.

## Cost

- **Per frame and per tick:** a few adds and compares; per read: three adds.
- **Every 5 s of game time:** one walk of the heap's chunks under its lock (the
  heap line gives the slowest walk, `walk N us`).
- **Memory:** about 0.4 KB of statics, nothing from the heap.
- **Measured** [E]: see the emulator A/B below (the same build with and without
  the switch, the same stretch).

## Proof

- **Builds** [D]: the campaign build with the switch on and one with
  `PSP_METRICS=0` (same profile, same layout) both link. Each lists the same
  20 functions compiled without their profile counts as the branch it came
  from: the hooks cost no function its counts.
- **Stripped** [D]: the `PSP_METRICS=0` link map defines no `psp_metrics_` or
  `psp_heap_survey` symbol (`psp_metrics.o` is empty), its text is 5.7 KB
  smaller, and its EBOOT holds none of the `metrics budget`, `metrics hitch`,
  `metrics heap`, `metrics io` strings (the on build's holds each).
- **Lines** [E]: an emulator batch (cache model) on b30's timed route, a
  ladder to b40 after 90 s and the game's exit at 180 s: each map printed its
  four lines at its end (b30 at the map change, b40 at the exit) and every 60 s
  before that. Every budget adds to 1,000 ms (b30 442+497+30+31, b40
  216+245+19+520). The figures are the map's own: b30 258 texture reads and
  3,308 KB, b40 161 and 2,221 KB (b40's 30 s "so far" line, 112 reads, is
  below b30's total, so nothing carries over); the heap's peak and low points
  differ by map. The hitches carry causes (b30: 14 cutscene, 1 checkpoint,
  11 other; b40: 1 cutscene), and the checkpoint test's frames name a
  checkpoint. `metrics hitch` (over 250 ms) did not print: no frame in the
  cache model is that slow. `hwreport.py` and the lab's rows reader parse all
  four lines on both maps.
- **Cost** [E]: against the same build with `PSP_METRICS=0`, same stretch,
  heartbeat averages (cache model; repeats agree to the digit): b30 tick
  15.11 ms on, 15.12 off; frame 29.17 on, 29.16 off; b40 tick 3.53 on, 3.54
  off; frame 8.32 on, 8.35 off. The difference is under 0.4% and not
  positive: the cost is below what the model can tell from code layout. The
  play is identical: every player position logged at the same input-read
  count matches to the digit between on and off (16 of 16).
- **Test harness** [D]: `tools/emu.sh` took the last "game time N" on any log
  line as the run's game time; the metrics lines name earlier game times (a
  heap low point's), so it now skips them.
