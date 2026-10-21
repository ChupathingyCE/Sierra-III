# Review: the level's lens-flare markers, axes worked out once (branch `lfs`, keep words `lfsonceoff`, `lfscheck`)

Every frame, `rasterizer_lens_flare_submit_for_cluster` worked out each level lens-flare marker's direction and up vectors for every marker of every drawn cluster, before `rasterizer_lens_flare_submit` decided whether to keep the flare. On a10, from the first doors on, that is about 2,590 markers a frame, of which at most 1,024 can be kept. This change works the two vectors out only for the flares that are kept, and remembers them in a small table keyed on the marker's three direction bytes. Once the frame's flares are full, it also skips the frame's remaining markers. It's exact by construction. It's on by default, and `lfsonceoff` puts the old path back. Labels: [M] PSP, [E] emulator, [D] code or data, [U] inference.

## Why it's exact [D]

- **The two words depend on the marker's direction bytes and nothing else.** `compressed_direction` and `compressed_up` come from `i_direction`, `j_direction` and `k_direction` (signed bytes in the BSP's marker block) through a scale by 1/127, `perpendicular3d`, two `normalize3d` and two `compress_real_vector3d_to_int32_clamp`. Nothing else goes in: no camera, no time, no other marker field. So the same three bytes always give the same two words, on any frame, BSP or map, and a table entry can never go stale.
- **Nothing before the copy reads them.** `rasterizer_lens_flare_submit` makes its tests on the far-fade distance, the light colour's alpha and the frustum sphere, then `memcpy`s the parameters into `local_lens_flare_parameters[count++]`. The two words are first read later, by the occlusion tests and the flare draw, and only from that stored copy. Writing them into the stored copy straight after the submit therefore leaves every stored byte as it was. A marker that isn't kept leaves nothing behind, so its vectors were never needed.
- **A full frame keeps nothing more.** With `local_lens_flare_count` at `MAXIMUM_LENS_FLARES_PER_FRAME` (1,024), the submit's only work is the "too many lens flares" error, which it reports once a run (`warned`). Once that report has been made on the primary target, every later call does nothing, so skipping the definition lookup and the call changes no state. The first call past full still goes through, so the error is still reported once. The marker-index and flare-index checks before it still run for every marker, so their own one-time reports are unchanged too.

## The change (`psp_fixups.py`, `FIXES_LFS`; `psp/src/psp_platform.c`)

- In `rasterizer_lights.c`, the loop in `rasterizer_lens_flare_submit_for_cluster`, when the word is on (once keep.txt is read):
  - It fills in the parameters as before, but leaves the two vector words at 0. Then it calls the submit.
  - If the count grew, `psp_lfs_axes` writes the two words into the stored flare, `local_lens_flare_parameters[kept_before]`.
  - When the frame is full and the overflow has already been reported, the marker is counted and skipped.
- `psp_lfs_axes`: a 64-entry direct-mapped table (`(key * 2654435761) >> 26`). Each entry holds a 32-bit key (the three bytes, with bit 24 set; 0 means empty) and the two words: 768 bytes of .bss. A miss works the vectors out with the original code (`psp_lfs_axes_worked_out`, the same lines moved into a function) and stores them.
- `lfscheck` turns the checks on:
  - every answer from the table is worked out again and compared;
  - no marker is skipped: each one past full is submitted anyway, and if one were kept, that would count as a difference;
  - on each new BSP, the distinct direction triples among all of its markers are counted once, up to 1,024, with a 4 KB array on the stack, and logged as `lens flare axes: the BSP's N level markers have D distinct direction triples`.
- The heartbeat line, per frame: `lens flare axes: M level markers a frame, K kept (axes asked), H of those from the table, S past a full frame skipped; lfscheck: N differed`. N counts from the start of the run, and 0 is the expected value.
- `rasterizer_lights_begin_for_new_frame` counts the frames for that line, next to the existing lens-flare peak.

PGO: `rasterizer_lens_flare_submit_for_cluster` and the heartbeat's `object_report` are compiled without their counts until a profile is made at this code: `pgo_mismatch.txt` lists 2 functions. A release's strict link needs a new profile.

## Evidence

One EBOOT (PGO build, non-strict) runs both sides; on against `lfsonceoff`, with `lfscheck` as a third run. All with `nearauto assettrack geo2 fixedticks fixedseed nomouth`, hands off; a10 to 165 s, c20 to 150 s. Every run played to its end [E].

- **Play exact** (`statecrc`, `tools/statecmp.py`): the play hash and both random seeds are the same at every tick, on against off and on against check, on a10 (4,950 ticks) and c20 (4,500). The whole-state hash differs only in the allocations known to follow the emulator's speed (`objects`, `object looping sounds`), as in every earlier exactness check.
- **Axes exact** (`lfscheck`): every answer from the table, worked out again and compared, **0 differed** on a10 and c20 over the whole runs, and no marker past a full frame was ever kept.
- **Pictures** (`framecrc`, `tools/framecmp.py`): not identical frame for frame, but nothing the change makes. On against off, 5 of 2,465 frames differ on a10 and 4 of 2,248 on c20. Each is a single frame on its own. Off against check also differs, at 2 frames on each map, although both submit every marker and use the same axes. Sorted three ways, each of the three runs is the odd one somewhere: on at 4 frames on a10 and 2 on c20, off at 1 and 2, check at 1 and 0. That is the run-to-run picture difference that follows the emulator's speed. The on side is the fastest of the three, so it is odd most often [E, U].
- **The table is big enough** (lfscheck's count at each BSP's load): a10's two BSPs have 13 and 34 distinct direction triples among 790 and 505 markers, and c20's has 60 among 553, against 64 entries. On a10 the busy stretch's kept flares came from the table 100% of the time. On c20, where triples collide in the direct-mapped table, it was 44 of 48 a frame (92%). A miss only works the axes out, so a collision costs time, never correctness [E].
- **The skip:** a10 fills the frame's 1,024 flares between game time about 3,650 and 4,200. There, 16-568 markers a frame (window means) are skipped past a full frame. Elsewhere, and on c20, it never fires [E].
- **Heap:** the same on both sides (in use peak 5,501 KB on a10 and 5,382 KB on c20, free low 374 KB on a10, 0 requests failed). The table is 768 bytes of .bss, so the heap, sized from the program's end, is at most 1 KB smaller [D, E].
- **Gain** (the cache models, hands off, `lights_preprocess_scene` ms a frame of the emulator's clock, by profile window):
  - **a10's three windows from game time 3,136 to 4,114 (105-137 s, the doors on): 2.56 / 2.93 / 3.41 against 7.66 / 8.00 / 8.15, so 4.7-5.1 ms a frame less.** Over the whole run to game time 4,114 (frame-weighted), 1.67 ms a frame. Before the doors (to game time 3,136), 0.1-0.6 ms.
  - **c20: 0.22-0.25 against 0.38-0.42, about 0.16 ms a frame.**
  - The drawing phase's work over the whole run: a10's instruction fetches fall 11.7% (7,743 M to 6,839 M) and its data accesses 9.7%; c20's fall 3.1% and 2.2%. Data misses are unchanged (+0.1%).
  - The flare occlusion tests and the other passes are unchanged, within the windows' spread.
- **Priced** (a record lever, 1x): a10's gain is 1.67 ms a frame over the whole run and 4.7-5.1 ms over 105-137 s, against the 1% gate of 0.53 ms: **3x the gate whole, about 9x where a10's frame is slowest.** c20's 0.16 ms is a fifth of its 0.82 ms gate. It pays on a10 only, and costs nothing elsewhere. How much of a10's gain reaches the PSP's frame depends on whether the record lane binds in that stretch. Only a PSP pair says so [U].

## Other per-marker repeats in the flare submit [D]

- `lens_flare_definition_get` (`tag_get`) runs per marker for a definition that is the same for every marker of one structure lens flare. It's the build-106 fast path (a few loads), and in the full-frame case it's now skipped anyway. The kept markers still look it up, because the submit's tests read the definition.
- `render_frustum_sphere_visible` and the far-fade test run per marker. They depend on the camera, so they aren't repeats. A cluster-level bound could reject whole clusters first, but it would need a bound for each cluster's markers (memory a10 doesn't have) [U].
- **a10 fills the frame's 1,024 flares.** The heartbeat's peak is 1,024 from about 120 s, and the log's "too many lens flares submitted to frame" error appears. Which flares are kept depends on the order the clusters are drawn in. That is unchanged here, and it may also have been the Xbox's behaviour (same limit), but whether a10's flares all draw on the original is not checked [U].
