# The multiplayer build against the campaign build: an audit (2026-10-17, b137)

Sheepdog01, 09:02: check that every difference between the multiplayer and campaign builds is still sane ("`nomeaudio` should easily be dropped"). Read by a delegate of the master session from `w137` (`73f90b5`), the lab's multiplayer PSP runs (39033944535 PSP-1000 Ratrace and Damnation; 39030473538 PSP-3000 Longest and Carousel; 39024064182 PSP-1000 Prisoner and Wizard; 39017504866 PSP-3000) and b136's campaign ladder (39019552090). Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

## The differences

| What differs | Where | Verdict | Fix |
|---|---|---|---|
| Networking, game-engine, co-op and p2p units compiled in | `psp_build.py` 55–60, 85, 88–98, 275–299 | Sane, except **co-op**: `network_coop`, `network_actors`, `coop_enemies` add **274 KB of bss** [D] for a mode that isn't a goal | Stub co-op in the multiplayer build, as `psp_campaign.c` stubs the campaign-only units |
| Host-only tables (`distributed_batches` 152 KB, `update_server_globals` 128 KB, `late_joiner_players` 64 KB…) | engine; FIXES_MULTIPLAYER ~10446 | Unknown: the PSP can host (ad hoc) | Size them by role once hosting on the PSP is settled |
| Session limits 128/128 (campaign 16/4) | `psp_limits.h` 89–95 | Sane (the wire) | — |
| **Heap 3,668 KB against 5,600** | `psp_build.py` 729–740 | Harmful by side effect: the program is 6.77 MB (bss +1.39 MB, text +0.5 MB); Ratrace "copies' arena: no room for 1056 KB", Damnation 16 KB under the heap's end [M] | The co-op stub, then the host tables |
| **No PGO and no `PSP_TEXT_LAYOUT=b132`** in the multiplayer builds | sierra-build 39037200812 (`PSP_MULTIPLAYER=1` alone) | Stale: never set up. The profile was worth ~18% of c10's tick in the campaign [E/M, `order_file.md`] | `PSP_PGO=use PSP_PGO_PROFILE=campaign-b137 PSP_TEXT_LAYOUT=b132` without STRICT, the heap and `pgo_mismatch.txt` checked; later a multiplayer profile of its own |
| Presets: the release ships `halo/presets.txt`; the multiplayer build reads `halomp/presets.txt` | `build_release.sh` 114–123; `psp_menu.c` 154 | Stale (minor) | Ship `halomp/presets.txt` in the multiplayer zip |
| **Wake-on-write off in networked games** | `psp_fixups.py` ~21319, ~21438 | Unknown, worth proving: the network's own writes were outside wakecheck's proof; every sleeper (50–70 a tick) is summed every tick [D] | `wakecheck` on emulator netplay (`host=server`), hooks for any misses, then on |
| Corpses sleep only with no game engine | ~2890 | Unknown, small | Later |
| `netpace5` (the game thread at 0x70 for 1 ms in 5) | `psp_net.c` 462–490 | Sane (fixed the public-game drop [M, PSP-3000]); its cost unmeasured (≤ 20% [U]) | Measure 5 against 10 against off |
| The ME's pose job, slant and rays | `psp_me.c` 983–999, 1166 | Sane (pose-ahead is keyed by object, model, animation and frame: exact when networked [D]), but **`nomeaudio` keeps the ME off** | Drop `nomeaudio` |
| `nomeaudio` in every multiplayer run and the setup doc's keep line (`nearauto nomeaudio assettrack geo2 sound1024`) | `multiplayer_setup.md` 191; the lab's `sierra-hw`/`hw.sh` defaults (now `nearauto`, 09:15) | **Stale** | Below |
| Also sane: data root `halomp`, menu art and title; network start after the placed blocks; FIXES_MULTIPLAYER's limits (script nodes 1024, client updates 32, host slots 16); `#bootvariants` (campaign only); the settle skipped while a game engine runs; `machinesfar`/`posehalf` campaign only; `fixedticks` local only; interpolation off at 30 ticks a second; vrammodels refused until `7e52cd8` (both builds; small in multiplayer, where the GE waits ≤ 0.45 ms a frame [M]) | | | |

## `nomeaudio`

**Why it was there:** mp1's join debugging. Run 0g found it wasn't the ME (`build_mp1.md` 181–196: the cause was ARK-4's WPA2 setting); run 0h joined with the word still set, and every multiplayer PSP run since inherited it. **The ME and Wi-Fi have never run together on a PSP**; no doc records a conflict (the ME works from its own eDRAM; its bookkeeping is static bss) [D].

**What it costs** (it turns off more than audio, `psp_me.c` 988, 999, 1168): the main CPU mixes 1.2 ms a block at 4 voices, **5.1% of the CPU** (~3.5 ms of a 69 ms frame) [M, 39033944535], against 0.3–0.7% with the ME in the campaign; the slant sort on the main CPU, 1.87 ms a frame on Ratrace [M]; the pose job off.

**The A/B (PSP-1000):** (1) CPU, a fixed scene, Wi-Fi up, no game traffic: `env=PSP_MULTIPLAYER=1 data=halomp map=levels\test\ratrace\ratrace keep="nearauto" keepsets="nomeaudio | -" repeat=2 wall=240` (`abreport.py`); (2) coexistence under traffic: one `[C] Slayer` join without `nomeaudio`. Read the heartbeat's fps and tick, the `budget:` line, `audio: mixing`, `ME: running` and `ME: ring` (0 failed), the slant sort's time, the heap, `network: up in`, the tunnel lines (`kcp receive 0+0`, no "dangerously long").

## Why multiplayer frame rates are low, ranked

Every rig run was alone on its server; remote players' cost is unmeasured.

1. **Ticks at a fixed 30 a second take 18–46% of the CPU** [M] (Ratrace 6.0 ms a tick, Damnation 15.4, Carousel 14.4 on the PSP-3000; 2–4.3 ticks a frame, Carousel 7.5). Each ms off the tick gives 3% of the CPU to frames. In `objects_update` (Damnation): **weapons on the ground 4.5 ms for 10**, all lit and asleep (a sleeping lit item still re-links its lights every tick, FIXES_123L [D]); **light fixtures 3.7 ms for 27, never offered sleep** (only vehicles, machines, bipeds, items and scenery are; `psp_fixups.py` ~2937 [D]); the one local biped 3.2–3.9 ms (≤ 1.3 in the campaign) [M].
2. **The frame:** 45 ms on Ratrace, 60 Damnation, 89 Prisoner, 123 Carousel [M], about the campaign's for the same size of scene: the HUD 7–8 ms (campaign ~4); `structure_visibility_find_clusters` 21 ms on Carousel. The GE never limits it.
3. **"The rest": 11–20 ms a frame** against 3–6 in the campaign [M]: the main-CPU audio (3.5 ms), the network threads, netpace's yields [U], `network_test_update` 1.8–2.3 ms.
4. **No profile or code order** [D] (up to ~18% of the tick in the campaign [E]).
5. **`network_distributed_tick`:** 1.5 ms a tick, ~4.5% of the CPU [M].
6. **Wake-on-write off while networked** [D]: ~1 ms a tick at the campaign's rates [U].
7. Models in main RAM (before `7e52cd8`): small here [M].

## Fixes, safest first

1. The `nomeaudio` A/B, then drop the word from the runs and the setup doc (multiplayer session; the lab defaults done).
2. Stub co-op in the multiplayer build: 274 KB of heap (master, with the multiplayer session's stubs).
3. The profile and b132's code order in the multiplayer build, gated on the heap (master).
4. Light fixtures sleep the machines' way, and a cheaper exact light re-link for lit sleepers: exact, shared code, pre-approved; `statecrc` and a netplay client (recon, with lever #2's LD1).
5. Wake-on-write while networked: `wakecheck` on netplay, then on (master, with the multiplayer session's runs).
6. netpace's cost (multiplayer session).
7. The local biped's 3.5 ms, and remote players (a run with the desktop client or bots) (multiplayer session).
8. Presets for `halomp` (master).
