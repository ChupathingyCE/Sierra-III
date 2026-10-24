# Review: build 141 (the b140 polish release)

For the owner to read and question before testing (CLAUDE.md, "How AI is used here"). Build 141 is `w140` at 0915fb4 (the code frozen at 8bb0fe5, its profile, and the release's timer default moved into the keep table): build 140 (`review_b140.md`, built from `bc40b5a`) plus the commits since. They are the two fixes from your b140p1 hand test (`review_b140p1.md`), nearcine from the same test, the API review's two defects, the white-models ceiling, the heap's reserve, the release's timers, and the build number. Two of those commits are b140's own proof (docs only). This page lists the net change, not each flip on the way: the real-time look went off in `7114b95` and back on in `16630ea`, and the meter load went in on in `2f6118a` and became opt-in in `16630ea`.

Labels: [M] measured on a PSP (which one is named), [E] emulator, [D] read in code or data, [U] inference. Every change has a keep word that turns it off (or on, for the opt-in ones).

## What changed

| Change | What and why | Key line | Default | Word | Proof |
|---|---|---|---|---|---|
| **The look by real time stays on** (`7114b95`, `16630ea`) | You found the capped turn felt variable, so b140p1 turned the real-time turn off. In the p1 hand test run 3 found it "decent", so it goes back on as the restoration it is | `psp_fixups.py:32081` `psp_look_real = psp_keep("lookrealoff") == 0 && !psp_keep("fixedticks");` | on | `lookrealoff` | none new; your p1 test run 3 [M by eye] |
| **lookreal off under fixedticks** (`da431c2`) | In a fixedticks run the look read the wall clock, so an exact A/B whose route moves the stick wasn't repeatable (the `&& !psp_keep("fixedticks")` above). From the API review of b140 | as above | on, except under `fixedticks` | `fixedticks` | api-job 40073567486 found it |
| **HUD meter load: opt-in** (`2f6118a`, `16630ea`) | The recording loads a meter's map before the quad is queued. On the PSP it left a10's bars empty and lost model palettes (Chief's change colours), so it is off. Kept for the bars' A/B | `psp_ge.c:17891` `meter_load = psp_keep("meterload") != 0;` | off | `meterload` turns it on | your p1 hand test, runs 1 and 2 [M by eye]; the emulator draws the bars (`2f6118a`) |
| **nearcine** (`93be989`) | In a cutscene, a model nearer the eye than 1.4x the game's plane gets a plane a quarter as far. The 16:9 view shows the sides 4:3 cut off, and there a close-up cut faces open (a10's Keyes, the backs of his eyes, t236). The depth range coarsens only while the cutscene lasts | `psp_ge.c:4673` `near_cine = !psp_keep("nearcineoff");` | on | `nearcineoff` | none yet (see Not in this build) |
| **Keep defaults' room 192** (`da431c2`) | `keep_defaults_added` was filled by unchecked `strcat` with 91 of its 96 bytes used, so the next default word would have overwritten what follows it. The room is 192 and the log list is bounded | `psp_platform.c:1028` `#define KEEP_DEFAULTS_ROOM 192` and `:1082` `if (strlen(keep_defaults_added) + length + 2 <= sizeof(keep_defaults_added))` | on | none (a fix) | api-job 40073567486 found it |
| **White models: pretouch's ceiling 95%** (`d3dc2e7`) | At 85% the frame's set passed the ceiling, models' maps went unmarked, and the replay drew them untextured: 73 frames (47 in a row) against 27 (13) at 95%, with no waits for room | `psp_tex.c:2216` `pretouch_percent = psp_keep_number("txptceil", 95);` | 95 | `txptceil85` (the number) | wt1 40073476643, read by api-job 40074692149 [E] |
| **The heap's reserve** (`794e3cc`, from `d3dc2e7`) | Level copies and the GE's speed-up tables (triangle lists, skin plans, spheres, node weights) are refused while the heap's room is under the reserve, and their draws take the paths they have without them. The engine's own mallocs no longer fail. On the ME it refuses outright, because the heap's lock is a system call | `psp_ge.c:6649` `heap_reserve_kb = psp_keep_number("copyreserve", 256);` | 256 KB | `copyreserve0` | a10 with copyreserve256, wt2 40075994851: requests failed 15 -> 0, no draw skipped, free low 19 -> 208 KB [E]; a30's failures came from the tables (link map of 40077320339) |
| **The failing caller logged** (`d3dc2e7`) | The heap's FAILED line names the caller, so psp-addr2line says what each failure loses | `psp_libc.c:651` `"heap: %s of %u bytes FAILED for %p (large requests so far %lu KB)%s"` | on | none (a log line) | |
| **Instrument timers off in the release** (`e6bbea5`) | The timers' clock calls (per model part, per moving particle) feed only statistics a `PSP_METRICS=0` build never prints, and cost b30 ~3% of its game speed. The release starts with them off | `psp_platform.c:1152` `psp_timers_off = 1;` (inside `#if !(defined(PSP_METRICS) && PSP_METRICS)`, unless `timerson`) | off in `PSP_METRICS=0` builds | `timerson` keeps them | lean: +2.8% game time in 600 s, lab 40075822966 [M PSP-3000] |
| **The release default in the keep table** (`0915fb4`) | e6bbea5 put a PSP_METRICS=0 branch inside keep_load, so the release's keep_load differed from the measuring build's and the strict release refused the profile (lab 40081634440). The default is now a table entry, PSP_RELEASE_DEFAULTS, on the table's last line, so both builds compile keep_load the same and one profile fits | `psp_post.h` `#define PSP_RELEASE_DEFAULTS { "notimers", "timerson" },` | release only | `timerson` | strict release build 40082747122: 0 without counts |
| **Loading on the always-on clock** (`e6bbea5`) | The loading screen's pacing, settle and input release read `psp_microseconds`, which `notimers` and lean already zeroed (lean's broken settle). They now read the heartbeat clock, which no word turns off | `psp_loading.c:203` `unsigned long now = psp_heartbeat_clock();` | on | none (a fix) | api-job 40079622693 classed the clock sites one by one: the loading screen's 12 reads were the only ones that decide anything |
| **Seed probe built -G0** (`2cd206b`) | `PSP_CFLAGS=-G8` moved the probe's 6-byte table into small data, so its section read came back empty and the build stopped. A build-tool fix only | `psp_build.py:859` `... + flags + ["-G0", "-DPSP_SEED_NAME=" + name_c, ...` | on | none | |
| **Build number** (`8bb0fe5`) | 141 in the log, the menu and the crash report | `psp_platform.c:19` `#define PSP_BUILD_NUMBER 141` | | | |

## Things to question

- **The reserve's guard on the ME.** `heap_reserve_allows` returns 0 on the ME (`psp_ge.c:6654` `if (psp_on_me())`, `return 0;`), so a table the ME would have built is simply not built. The commit doesn't say how often that happens [D for the path, U for the cost].
- **The reserve changes draws.** Refused tables mean the slower paths. a10's run shows no draw skipped [E], but no PSP pair has shown what they cost in time.
- **The pretouch ceiling's price.** Raising it to 95 lets the frame's set take more of the caches' room. The b30 beach case (2,559 KB of 2,632 marked) is what 85 was set against (`psp_tex.c:2056`), and the "loads then waited for room" case is the one to watch [U].
- **meterload's cause.** The hand test says the bars' fault is upstream of the map (`review_b140p1.md`); this build doesn't fix it.

## Not in this build

- **The HUD bars that pulse at empty.** a10's shield and health bars show, pulse at empty and never fill. They are pre-existing since at least b139, and the hand test (`review_b140p1.md`, run 1) puts the fault upstream of the palette, where the meter's fill comes from. Not fixed.
- **nearcine on a close-up.** The change is read in code, but no run has shown a cutscene close-up with it on (the commit names only your shot t236). It is unproven until a10's Keyes is looked at.
- **BSPR v2** (a biped's last collision answers kept in its own state, branch `bspr141`) is held for the next build: exact on this base (lab 40071989304), b30 -1.3 ms a frame on the flight and -1.0 on the beach [M PSP-3000, 40072862303], a30 in the noise [M PSP-1000, 40079602611].
- From b140's list and unchanged here: the tutorial lights staying yellow on a10, animations looking cut short, VISLAZY v2 and UNSEENPOSE.

## Proof of this build

- **Profile:** `pgo/campaign-b141.tgz`, 13 counting runs at 8bb0fe5 on the b140z bake (pgo-gen141ra 40080605284, rb 40080607969: every campaign opening, b30's flight, bench, run and hallway); merged with psp-gcov-tool 15.2.0.
- **Strict builds** (`PSP_PGO_STRICT=1`): 0 functions without counts. b141 (metrics on) 40082744222, EBOOT 6ad8950f7b47f1b6; **b141rel (`PSP_METRICS=0`, the one that ships) 40082747122, EBOOT 867cc24547f73d14**, heap 5,736 KB.
- **Release check** (rc141 40081685090, the strict interpreter): every campaign map's opening to 60 s, b30's route and the front door to a10: 0 sites, no crash, all ended normally [E]. The shipping EBOOT's own b30 route and front door (rc141rel 40083128939, at 0915fb4): 0 sites, no crash, ended normally [E].
- **The PSP ladder** (ladder45, metrics build, timers on), fps over the second half of each opening [M]:

  | Map | b140 PSP-3000 | b141 PSP-3000 (40081727024) | b141 PSP-1000 (40081723957) |
  |---|---:|---:|---:|
  | b30 | 6.5 | 6.5 | 6.7 |
  | b40 | 27.8 | 27.6 | 27.3 |
  | c10 | 12.8 | 13.0 | 13.4 |
  | c20 | 14.0 | 14.4 | 14.2 |
  | c40 | 29.9 | 29.8 | 29.9 |
  | d20 | 7.8 | 8.8 | 8.1 |
  | d40 | 29.5 | 29.7 | 29.8 |

- **Relaunches** of the shipping EBOOT: PSP-1000 x5, lab 40083100054 (in flight at writing). b140rel on the PSP-1000: 5 of 5 exited (40073256355).
- **Timers off** (the shipping EBOOT, default vs `timerson`, PSP-3000 x2 by game time reached in 600 s): lab 40084340672 (in flight at writing). Before it: lean on the measuring build, b30 +2.8% game time [M PSP-3000, 40075822966], a30 +0.4% (in the noise, 40079598701).

