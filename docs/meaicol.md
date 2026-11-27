# meaicol: the AI job's own ME time, less what nests in its yields

_The builder, 2026-11-27 04:00 CST, on the lab's brief `briefs/me_aicol.md` (the lead, 03:5x). Branch `builder/meaicol`, cut from slice 1's head dbc76ba0. Build switch `PSP_MEAICOL=1`; log only._

## Why

The slot line's "the ME's own time a job" for the AI slot is the whole job: it holds the audio blocks mixed in the AI's yields, the ring jobs drained there and the render jobs served there (menestrender). The one figure the lab has, 13.5 ms a job on the beach [M, archive], can't say how much of that is the AI's own work. The tick plan needs that split before it books the AI at one pass a tick. The report line was also said to be cut at 512 bytes (job_graph.md section 6, gap 1). The "ME, continued" lines already repeat a cut slot, but nobody has checked that against a log yet.

## The code, the review paragraph

- **`struct aicol_stats aicol`** (psp_me.c, after `nested_cycles_total`, `PSP_AIME_ME_SIDE` and on a 64-byte line of its own like `tick_look`): six ME-side counters, written by the ME in its cache. The AI job's end in `me_serve` writes the whole cache back before `done` (an AI job is never `ranges`), so the main CPU reads them uncached at the collection.
- **Counted only at the AI's own yield sites, only while `aime_now`:** `psp_me_yield`'s audio mix and ring drain, `psp_me_yield_slots`' nested render, and `psp_me_yield_inner`'s audio mix and ring drain. A nested render job clears `psp_aime_job_now` before it runs, so its own looks aren't counted again. Each kind is exclusive and the sum never counts anything twice. The ring's share includes any audio mixed inside a drain (`ME_FEATURE_AUDIO_FIRST`); that is said on the line's field name in this doc. `psp_me_ring_look` (render jobs only) isn't a site.
- **`aicol_collect`** (main CPU), called from `me_run_slot` and `psp_me_finish` for `ME_SLOT_AI` on the real ME path only: it reads the six counters once and books their growth since the last collection to this job. That gives the job's whole time, its time less nested, the nested sum and the wait, with means and worsts **a job**, never a window's. It also sums each kind. The simulated path (emulator) never calls it, so there the line says no job was collected with the ME's clock.
- **The report** (`psp_me_report`, after the "ME, continued" lines): one `ME slot whole (N chars)` line for each part of the slot line, holding its true length (a part past the log's 510 is cut to fit and its N says so). Then the `ME: aicol:` line: jobs, own time whole a job (worst), less nested (worst), nested a job (worst), audio / ring / render a job with their counts, and the main CPU's wait a job (worst) and in all over the window. All of it resets each report except the counters' last seen values. Its worst-case length is 431 characters (toolbox/fmtlen.py).
- **RW1's step counters** (psp_fixups.py's `psp_aime_step_worst`, steps 0-14) are on the head and already print on the `aime2a:` lines ("the job's longest run between looks at the word"). They are each step's **longest** run, not its total a pass. A step's share of the pass would need a new counter, which is not in this brief.
- **Seen while reading, not changed:** `psp_me_yield_inner`'s ring drain isn't added to `nested_cycles_total`, so the busy share counts it twice (once in the AI slot, once in the ring). This is a line to the lead.
- **Heap and pool:** none. BSS: 64 bytes (the ME's counters, in `.data.psp_aime_me`) and ~100 bytes on the main CPU's side, in the on build only.
- **What the on build moves:** placed functions that grow under w_tgme2m1mp_tgc90 are `psp_me_report` (layout line 221), `psp_me_finish` (703) and `me_run_slot` (707). Placed blocks after 221 shift until the next COLOUR anchor. The yields aren't placed. Off, the switch adds nothing: every change is inside `#if PSP_MEAICOL`, and psp_build.py passes `-DPSP_MEAICOL=1` to psp_me.c only when it is set.

## Checks

1. Off: the EBOOT is byte-identical to dbc76ba0's on the same env (a build's summary.txt EBOOT hash against dbc76ba0's).
2. On, an emulator run of b30: every slot's `ME slot whole` line prints with its length, and the `aicol:` line prints (no ME clock in the emulator, so it says none collected). The `aime2a:` lines carry RW1's worsts.
3. On, the PSP-3000 run set (b30_leg2 to 61 s, repeat 2) gives the row `mecol_b30_ai` [M].
