# Review: tock158

The tock after tock157 (`4b623d1`): five merges, one fix found in review, and four levers turned on by default. Every change keeps a word that turns it off. Each merged branch has its own review; this walk-through says what each brings and where its key lines are.

| Change | Default | Off word | Its own review |
|---|---|---|---|
| PROPCRASH: a found body's shout taken up only while its prop lives | on (a fix) | `shoutnone` | `docs/review_propcrash.md` |
| MEBUSY: the ME's busy share counted once | on (a measure) | `mebusyold` | (below) |
| vmetest: the VME beside the ME's jobs | **off** | `vmetest` turns it on | `docs/review_vmegame.md` |
| FLC: the flashlight's split carries each corner's work to its pieces | **on** | `lightcarryoff` | (below) |
| SNDEDRAM2: the sound cache's last pages in the ME's eDRAM | **on** | `sndedramoff` | `docs/plan_sound_edram.md` |
| SNDEDRAM2's copy job bounded from its start (found in this review) | on | (part of `sndedram`) | (below) |
| WXDEAD: the weather's rays skip a search nothing reads | **on** | `wxdeadoff` | `docs/optimization_ranking.md` |
| STANDIN (`texavg`): a texture not in memory drawn as its 4 x 4 stand-in | **on** | `texavgoff` | (below) |

## PROPCRASH (`psp_fixups.py`, FIXES_PROPCRASH)

B49, the crash in Sheepdog01's b30 hand run. With `aicad3`, an actor's resting tick runs perception but not its decision, so a found body's stimulus can wait a tick, and the body's unit can be deleted meanwhile. The decision then sets a shout about prop NONE, and `action_guard_control` faults reading it. The fix, in `action_guard_setup_from_combat_transition`: the shout is set up only while the transition's prop lives, as `action_guard_replace_prop` already does for a guard that has the shout. It changes state only on a tick where it fires. `shoutcensus` logs each case, `shoutforce` reproduces the crash.

## MEBUSY (`psp/src/psp_me.c`)

A slot job that yields (the AI's, the GE's) runs other ring work inside its yields. The busy share counted that work twice: once in the nested job, once in the slot job that held it. `nested_cycles_total` now gathers the cycles spent inside the yields and the busy share takes them off the slot job's own figure. It's a measure only and changes no play. On the PSP: mean 69% / max 82% counted once, against 74% / 89% before (lab finding me_busy_counted_once).

## vmetest (`psp/src/psp_vme.c`, `psp/src/vme/`)

mcidclan's VME library and his mat4x4 sample (MIT, `psp/src/vme/LICENSE.md`; credited in the README), run as the slot job `ME_JOB_VME` from the port's own ME start. It's off by default: with `vmetest`, the main CPU queues the test now and then, and the result is checked against the VFPU and counted. The clocks are set by read-modify-write only. Both waits are bounded. It's groundwork for moving work onto the VME; it does nothing in play.

## FLC (`psp/src/psp_ge.c`, `lightcarry`)

The flashlight's light pass splits each triangle against the cone (up to nine halvings). Each piece's corners used to be worked out from scratch: distance from the light, side of the cone's pyramid, reach, place from the camera. FLC carries each corner's answers to the pieces that share it, and tests the cone as an octagon round it. It's exact: `lightcarrycheck` splits both ways and compares every piece's hash. On the PSP-3000, d20's swamp with the flashlight on: frame -10%, drawing -23% (sierra-hw 41781771922).

## SNDEDRAM2 (`psp/src/psp_dsound.c`, `psp/src/psp_me.c`, FIXES_SNDEDRAM)

The sound cache keeps its 1,792 KB, but its last 656 KB sit in the ME's eDRAM, fenced so that no sound spans the two parts. A tier sound is read into a 2 x 64 KB staging area and copied in by the ME's own copy job (`ME_JOB_SOUNDCOPY`, in the probe slot). It's collected once done and never waited for. On a failure the tier closes and the main part plays on. The ME mixes the tier's voices (the main CPU can't read the eDRAM). It plays the same, and 528 KB of heap come back. Both PSPs ran it clean.

### Why the copy job's worst was 312-354 ms with 0 late (and the fix)

The question from the PSP-1000 pair (sierra-hw 41785586799): hand-over to collected averaged ~100 ms, with a worst of 312-354 ms, yet 0 were counted late against the 250 ms bound.

- **Why [D]:** the bound is tested only in `psp_sound_upload_poll`, which runs once a pass from `sound_cache_idle`, and only while the job is still undone. The first poll after 250 ms found every job done, so none was late. The collected figure is mostly the poll's gap: b30's opening has frames of 260-400 ms (the run's `metrics hitch` lines), and the ~100 ms average is about a frame. A copy of two 60 KB pieces takes well under a millisecond on the ME [U].
- **What was wrong:** the bound ran from the hand-over, so it also counted the job's wait behind the ME's ring jobs. A heavy frame's drawing there has run past a second (c40's snow, 1.2 s). That would have counted as a failure of the copy, and three failures close the tier.
- **The fix:** the ME marks a slot job begun before it does anything else (`struct me_slot`'s `begun`, written in `me_serve`; `psp_me_submitted_begun`). The 250 ms bound now runs from the first poll that sees the job begun, so it bounds the copy alone. The wait to be started gets its own 2 s bound (`TIER_JOB_WAIT_US`), so an ME that never serves its slots is still caught. A second census line gives the ME's own time on each copy (`psp_me_last_job_us`) and the worst wait seen, so the collected figure can be split into the copy, the wait and the poll. The struct stays 128 bytes.

```
if (copy_begun_at ? now - copy_begun_at > TIER_JOB_LATE_US : now - copy_at > TIER_JOB_WAIT_US)
```

## The defaults (`psp/src/psp_platform.c`, `keep_levers`)

WXDEAD, texavg, lightcarry and sndedram go in a table of their own, read before M6 #2's table. That way `m6off` and its instruments, which turn M6 #2 off whole, leave these four on. `KEEP_DEFAULTS_ROOM` goes from 320 to 384 so the four new words fit beside the others (words past the room are dropped silently).

- **WXDEAD:** exact; c10 frame -1.6% on the PSP-3000 (sierra-hw 41779868380).
- **STANDIN:** Sheepdog01, 2026-11-04 00:21: "it's a restoration of a kind". It needs the pack's TSI1 table: the tock's bake has it, and an older bake logs "no stand-ins" and draws untextured as before. The pair (41779878353) showed no frame cost on b30; 43-75 frames a run drew a stand-in instead of nothing.
- **MUSADPCM** (the disc's 44.1 kHz music): stays off unless its gate passes (below).

## Proofs

See the lab's findings and coord/sessions/tock158.md for the run ids: exactness (tock158x1, tock158x2), the profile campaign-w158a, the layouts w158a_tgc90 and data_hotg158a, the strict build and the release check.
