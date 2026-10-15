# Review: build 125, every change since build 124

For Sheepdog01 to read and question before testing. b125 replaces b124: it carries all of b124 (`review_b124.md`) plus the changes below. The long versions are `review_lookb124.md`, `review_lk22b.md`, `review_mewake2.md`, `review_streamoom.md` and `review_trim125.md`. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

Branch `w125`: b124 (`ea66684`) plus `w124-itemcheck`, `lookb124`, `research-halo64`, `research-vita`, `lk22b`, `mewake2`, `sceneladder`, `streamoom` and `trim125`. The bake is still `campaign121`, the PGO profile still b122's.

| # | Change | Kind | Default | Keep word |
|---|---|---|---|---|
| 1 | The level's GE copies given from a shared origin near the eye (c40's dance, a10's window frame) | restoration | on | off: `levellocaloff` |
| 2 | A level material with no room waits for room, instead of failing and churning every frame (a10's `memalign … FAILED`) | fix | on | off: `streamroomoff` |
| 3 | b30's far water: no depth push for see-through non-decals; maps clamped; added water fades with the fog | restorations | on | off: `seethroughpush`, `seethroughwrap`, `waterunfogged` |
| 4 | The ME's restart after a sleep made opt-in, with a new try; the exit holds the ME; reset bits set alone | safety + fix | restart **off** | on: `mewake`; `meresetwhole` |
| 5 | The PC renderer's GL path out of the link; halo-ce-universal retired | dead code out (exact) | — | none (the code never ran) |
| 6 | `ditherhalf`: the dither at half strength | your look A/B | off | on: `ditherhalf` |
| 7 | `sceneladder`: the openings' cutscenes one after another | showcase | off | on: `sceneladder` |
| 8 | `geprecision`, `itemsleepcheck` counting wakes as wakes, the lit-item log capped | instruments, logs | off / log | `geprecision` |
| 9 | Two research notes, README Credits, the "Credit what we borrow" rule | docs | — | — |

No code in b125 is borrowed: the research notes (#9) are read, not yet used.

---

## 1. The level from a shared origin (`lk22b`)

**Why.** b124's fix made the level's *translation* exact for the GE, and it stopped the dance in the emulator, but not on your PSP [M, b124 test 2]. So the PSP's GE loses precision somewhere the emulator doesn't model; the likeliest place is where it folds the model and view matrices together [U]. Models never danced because both their numbers are small. The level's were world-sized: vertices ~800 units out, multiplied by a rotation whose low bits were lost.

**What.** The level's GE copies now give their positions from `level_origin`, a point on a 32-unit lattice near the eye, so every number the GE sees is a few tens of units, as for models.
- `level_origin_follow()` moves the origin once the eye is more than 24 units from it (seldom: twice in 300 frames walking on c20 [E]).
- `level_snap()`: positions snapped to 1/4096 of a unit when a copy is made. A lattice point minus a snapped position is exact in a float, so a copy can move between origins any number of times and lose nothing.
- `level_copies_follow()` (frame end, when neither the GE nor the ME reads the copies) moves copies drawn from an old origin to the new one.
- Every user of a copy's positions (level draw, see-through surfaces, stencil layers, shadows, lights, decals, the ME's measure jobs) works from the copy's origin.

**Key lines.** `psp_ge.c`: `LEVEL_ORIGIN_STEP`/`LEVEL_ORIGIN_SLACK`, `level_snap`, `level_model`, `level_frame`, `level_copies_follow`.

**Proven** [E]: off (`levellocaloff`) draws as w125 did; play exact on d20, a50, c20. **Only your PSP can show** whether the dance stops: that's test 1. `geprecision` (the rig measures it) tells whether the GE scatters on large positions.

## 2. Room for a level material (`streamoom`)

**Why.** Your a10 rerun [M]: from game time ~5,900, `memalign of 109600 bytes FAILED` every frame, a level material undrawn, 21 fps down to 9. The view's copies didn't fit the arena plus what the heap had left, and each frame's retry let go every copy not yet drawn, which the frame then rebuilt from the pack: the churn. A counting bug (a heap piece counted before the heap answered) also left the arena walking all 512 copies every frame.

**What.** A material that fails goes on a waiting list (64). Its draws skip it before any work. At the frame's end it is built if the free room plus the copies not drawn this frame hold it; otherwise the heap is asked once a second. The heap count is taken when the heap gives.

**Key lines.** `psp_ge.c`: `stream_room_ask`, `stream_room_wait`, `stream_room_frame_end`, the count in `copy_alloc`.

**What it can't do.** Make room that isn't there: when the view is bigger than the arena plus the heap, one material is still left out; the difference is which one, and no churn. Drawing all of that view needs smaller copies or a bigger arena: your call, later.

**Proven** [E]: with the arena squeezed to 512 KB on a10 (`streamsqueeze512`), failed builds 310,690 → 0 and the frame 36.5 → 22.5 ms; it draws no more under the same budget, it stops paying for the churn. No cost without pressure; play exact on d20, a50, c20. **Not shown in the emulator:** your exact case, the heap refusing 107 KB pieces, and the once-a-second heap probe; the a10 test answers it.

## 3. b30's water (`lookb124`)

From your b122 tests 8, 10 and 13:
- **Far water flicker** (LK13): the port pushed see-through surfaces that aren't decals one or more depth steps toward the eye, so the far ocean tied with the seabed. The Xbox doesn't push them. Off: `seethroughpush`.
- **Shore steps**: maps now clamp where the Xbox clamps them (`seethroughwrap`); added water fades with the fog, as the Xbox's final combiner does (`waterunfogged`).
- **Not fixed, found**: the shore waves' mask map; c10's fern "sawbands"; d20's bright doors (recon words `nobackpass`, `noglowpass`).

**Proven** [E]: shots differ only along the horizon and waterline; play exact on d20, a50, c20; the multiplayer build's Blood Gulch shots are pixel-identical with the off words.

## 4. The ME after a sleep (`mewake2`)

**Why.** b124's restart failed on your PSP ("no idle poll within 100 ms"), and the exit afterwards hung [M, b124 test 1].

**What.**
- The restart is now **opt-in** (`mewake`): by default the ME stays stopped after a wake and the main CPU runs its jobs, as b122-b123 did.
- With `mewake`, the next try does what the firmware's own ME wake does and the library skips: the ME's bus clock on, its caches cleared by a stub before the library's handler, 300 ms to answer. Marks in the reset SRAM say how far the ME got.
- `psp_me_exit` (replacing `psp_meaudio_stop`) leaves the ME halted or held in reset, never running, and logs the system registers.
- The reset bits are set and cleared alone, as the firmware's driver does (`meresetwhole` brings back whole-register writes).
- The lit items' sleep log: 4 lines an item at most.

**Key lines.** `psp_me.c`: `psp_me_wake_poll`, `me_restart_kernel`, `psp_me_wake_stub`, `psp_me_exit`, `reset_hold`/`reset_release`, `sysregs_read`.

**Your PSP answers** whether the default exits cleanly after a real sleep, and whether `mewake` restarts. The table in `review_mewake2.md` says what each log line means.

## 5. The GL path out, halo-ce-universal retired (`trim125`)

**Why.** ChupathingyCE draws through an OpenGL translator; on the PSP it never runs (`SDL_CreateWindow` returns NULL, so `gl_ready` stays FALSE and `psp_ge.c` draws), but the compiler couldn't prove the calls dead. halo-ce-universal stopped being a base on 2026-10-13.

**What.** `psp_fixups.py`'s `gl_never` pass makes every `gl_ready` test a constant FALSE (it checks there are exactly 10) and drops `gl_initialize`'s call; four GL units leave the file list. The 73 `Either` pairs became their ChupathingyCE halves; the old link harness and one-off scripts are deleted.

**Result** [D]: program −111 KB, **heap +108 KB** (the heap is sized from the program's end), which helps #2. No keep word: nothing changes at run time.

## 6–8. Words for your A/Bs and the rig

- `ditherhalf` (`psp_ge.c`): a half-span 2×2 matrix in place of the 4×4 dither; `nodither` turns it off. The default is unchanged: your call.
- `sceneladder` (`psp_platform.c`, shares `ladder_load_next` with the ladder): a map's turn ends 3 s of game time after its cutscene stops; a map with no cutscene by 30 s is passed over; after d40 the game plays on. Preset 13.
- `geprecision`: one measurement of the GE's precision on the first frame, then play goes on.
- `itemsleepcheck`: a weapon whose function flags flip as a function crosses its bound counts as woken, not as "changed while asleep" (b124's open item).

## The merged build (lab 38693919478, 38693952445) [E]

w125 `dce92ba` against b124 as released (`ea66684`), same bake and PGO, hands off, `fixedticks fixedseed statecrc` (`tools/statecmp.py`):
- **Play and both random seeds identical to b124 at every common tick** on d20 (14,281 ticks), a50 (23,911) and c20 (18,173). The whole state differs where it follows the emulator's speed and the renderer's caches (`objects`, `contrail point`, `object looping sounds`, `first person weapons`, `decal vertex cache` from tick 0, as in b124's own check against b123), and in everything at the last shared tick (a run cut mid-write).
- b30 and c40: 24 shots each, drawn as expected; no strict-interpreter sites, no crash.
- The multiplayer build (`PSP_MULTIPLAYER=1`) plays Blood Gulch at 30 fps for 6,926 ticks. Its one `memalign of 1081344 bytes FAILED` at start is the copies' arena falling back to the general heap, as in b124's multiplayer run.
- `sceneladder` from c40 (lab 38693952445): c40's turn ended after its cutscene, then d20's, and d40 "was the last map on the stick; playing on" until the 1,800 s wall (game time 65,357); no crash, no strict sites.
