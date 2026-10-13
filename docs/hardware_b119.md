# Build 119 on Sheepdog01's PSP-1000 (2026-10-13)

Labels: **[M]** measured on the PSP, **[E]** emulator, **[D]** read in code, **[U]** inference. Build notes: `build_b119.md`.

| # | Test | Result |
|---|---|---|
| 3 | b30, cinematic bench | **162.2 ms a frame (6.17 fps)**; b118 170.3. Flight 242.2 (b118 250.1). No crash [M] |
| 4 | the same, 119's speed-ups off | 170.7 ms: the speed-ups are worth 8.5 ms a frame on the cinematic, all of it drawing (49.5 against 56.4 ms, −12%); the tick unchanged (54.4 against 54.6) [M] |
| 5 | b30, played (landing, beach) | Beach **9.1 fps**, frame 110.4 ms, tick 36.0 (b116: 5.8 fps; beach fights vary ~10% between runs). Sheepdog01: "feels much better compared to a few days ago…the lows are getting more and more playable"; "it looks much better as well" [M] |
| 1 | the ladder | **All ten maps in one session, no crash** (below) [M] |
| 2, 6 | the slow clock; the probes | Not run yet: later evenings |

## The ladder (test 1)

Second half of each map's 90 s (`tools/ladderreport.py`).

| Level | fps | Tick (ms) | Ticks a second | b118: fps / ticks a second |
|---|---:|---:|---:|---|
| The Pillar of Autumn (a10) | 14.9 | 6.6 | 25.6 | 14.4 / 25.4 |
| Halo (a30) | 7.0 | 11.5 | 13.9 | 6.5 / 12.8 |
| Truth and Reconciliation (a50) | 9.2 | 32.0 | 18.3 | 9.0 / 17.9 |
| The Silent Cartographer (b30) | 3.9 | 59.3 | 7.8 | 3.8 / 7.6 |
| Assault on the Control Room (b40) | 14.6 | 15.7 | **28.1** | 15.9 / 24.7 |
| 343 Guilty Spark (c10) | 9.1 | 22.7 | 18.1 | 8.7 / 16.9 |
| The Library (c20) | 8.0 | 26.3 | 15.7 | 7.8 / 15.3 |
| Two Betrayals (c40) | 13.5 | 16.2 | 26.7 | 13.4 / 26.2 |
| Keyes (d20) | 7.6 | 16.9 | 15.0 | 7.5 / 13.7 |
| **The Maw (d40)** | **19.0** | 5.5 | 28.2 | 18.3 / 28.0 |

**Reading it:**
- **b40 is the clock fix:** 24.7 → 28.1 ticks a second, so its fps drops (15.9 → 14.6). That's each frame carrying the ticks the slow clock threw away: the game at real speed.
- **Below 15 fps, ticks a second is 2 × fps** (the engine's cap of a fifteenth of a second a frame). Those maps get faster only as their frames do.
- **The Maw is above the floor's fps (19.0) at 28.2 ticks a second:** close to 30, not there. Above 15 fps the cap shouldn't bind on an average frame, so the missing ticks are likely the slow frames' spikes [U]. The log can't settle it yet: its frames carry 1 or 2 ticks, never 3 (25 and 62 in the last window), and a frame cut short by the cap also shows as 2.
- **The rig agreed with this PSP-1000 within ~2% on fps** on every map (`build_b119.md`, the rig's table) [M].

## The look

Sheepdog01's remarks after tests 3–5 went into the handoff's look list (`handoff_2026-10-13c.md` §2b, LK1–LK12): the HUD, weapon textures and effects, flat armour and vehicles, vehicle LODs, ground textures against objects, the dark shade under b30's beach structure with bright objects under it, c10's vines and fog, flickering plants, and the Pelican's dust banding. Water now shows in c10's cutscene (b115: flat white).
