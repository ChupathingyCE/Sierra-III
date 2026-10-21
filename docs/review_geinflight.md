# Review: geinflight onto ge-vram

The branch joins two parked pieces so one build can test them together: frames in flight (`geinflight`, docs/frames_in_flight.md) and the GE's model bytes in VRAM under the Media Engine (`mevram`, docs/plan_ge_vram.md). Both stay off by default; nothing changes for a build that sets neither word.

Why together: frames in flight measured a large rise in tick time under the overlap [M], and the cause measured was the GE fetching from main RAM beside the ticks (holding the GE until the ticks end took the rise away) [M]. `mevram` draws about 81% of model draws from VRAM copies [M]. The open question is whether that leaves the ticks enough of the bus for frames in flight to pay.

## What the merge changed beyond the two branches

- **`psp_me.c`: one ring server, not two.** The base already has `psp_me_yield`: any GE job on the ME mixes audio and serves the ring between records, and the ring takes pushes while a GE job is out (`slot_yields`, off with `meyieldoff`). The frames-in-flight branch had its own copy of this (`psp_me_serve_ring`, `slot_serves_ring`, set only for its frame job). Its copy is dropped. Its frame job's per-record service now calls `psp_me_yield`:

  ```c
  static void inflight_serve(void)
  {
  	if (!psp_on_me())
  		return;
  	psp_me_yield();
  }
  ```

  The only difference this makes: the frame job's ring service follows `meyieldoff` like every other GE job.
- **`psp_ge.c`: record kinds.** `DEFER_MODEL_DETAIL` (base) and `DEFER_FRAME_END`, `DEFER_WINDOW_BEGIN` (frames in flight) are all kept, in that order.
- **`psp_ge.c`: the window's begin.** The showcase's texture level bias (`showcase_levels`) stays in the list's part (`window_begin_list`): it writes GE state, so under frames in flight it runs on the ME with the rest of the list.
- **`psp_ge.c`: the frame's end.** `mebatchtest` runs before the two parts. The census (recon builds only) goes in the list's part with the kind tallies it sits beside. `vm_frame_end`, and with it `mevram`'s fold of the ME's reads, is in the main CPU's part (`frame_end_show`). Under frames in flight that part runs only once the ME has finished the frame (`inflight_wait_me`), so the fold reads a finished tally. The ME's next frame reads the table as `vm_frame_end` left it, as under `gemeon` alone.
- **`psp_libc.c`:** the base's heap map kept; the heap check for ME work (frames in flight) added to its malloc wrappers.

## Keep words

- `geinflight` (with `gedefer gemeon`): frames in flight. Note: it is off whenever `gemeoverlap` is set (`defer_frame_begin` gives the overlap's half-frame buffer instead), as on its own branch.
- `mevram`: the ME's draws from VRAM copies.
- `mekinds`: per-record-kind timing, each CPU apart (`tools/mekinds.py`).

## Evidence

- [D] The conflicts and their resolutions above; no other line of either branch changed.
- [U] One possible hazard is still open: the main CPU's ticks write the copies' table through level moves (`vm_level_move`) while the ME replays the previous frame. The `gemeoverlap` tests of `mevram` already ran with the main CPU working beside the ME's replay and found nothing. The emulator batch's state and picture comparison checks this; the emulator runs the ME's job synchronously, so it cannot show a race. Only the PSP can.
- Emulator and PSP results: in the commit messages and build notes once read.
