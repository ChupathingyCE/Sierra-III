# Build 150: review walk-through

b150 is the next release candidate (a tick): tock161's code, `3a104119`, built strict with its own profile and layouts. It is b149 (`review_b149.md`, `b4e06773`, which passed Sheepdog01's hand test on 2026-10-29 but was never released) plus everything the tock line proved since: `git log --first-parent b4e06773..3a104119` (157 commits). b148 is still the last release, so the release notes (`release_notes_b150.md`) count from b148; this review counts from b149 and doesn't review b149's own work again.

Most of these changes already have a walk-through of their own, and the tock reviews (`review_tock158.md` to `review_tock161.md`) say how each round came together. This page is the map: what each change does, where its key lines are, how it was proven, and the questions worth asking before the hand test.

Labels: [M] measured on a PSP, [E] emulator, [D] read in the code, [U] inference. The [M] and [E] figures are copied from the commit messages, the reviews and the lab's findings. I did not measure them again.

## 0. At a glance

Every change ships behind a word. "Off word" turns a default off in `halo/keep.txt`; "on word" turns on one that's off.

| # | Change | Area | Default | Word | Its review |
|---|---|---|---|---|---|
| 1 | DRAWPART slice 2 + LIGHTREUSE: models drawn after the walk | drawing | on | `drawpartoff` | `review_drawpart.md` |
| 2 | LIGHTFIRST: objects lit in a pass before the walk | drawing | on | `lightfirstoff` | `review_lightfirst.md` |
| 3 | FLC: the flashlight's cone split carries each corner's work | drawing | on | `lightcarryoff` | `review_tock158.md` |
| 4 | mememo and geoindexme: the ME skips binds the GE has; read-only cache index | drawing (ME) | on | `mememooff`, `geoindexmeoff` | commit `4c909478` |
| 5 | STANDIN (`texavg`): a texture not in memory drawn as its 4 x 4 stand-in | drawing | on (needs the new bake) | `texavgoff` | `review_texavg.md` |
| 6 | B45: shadows' receivers indexed, past-the-rim ones left out | fix | on | `shadowindexoff`, `shadowrimoff` | merge `92b68f0b` |
| 7 | nearhold: the near plane moves back out only after 15 frames | drawing (B45) | on | `nearholdoff` | merge `5a2db0ed` |
| 8 | listrange: a geshow3 jump writes back its pending range first | fix (latent) | on | none | merge `efc8642f` |
| 9 | SKINPACK and SKINPLANBOUND: skinned parts' scratch and plans | heap | on | `skinpackoff`, `skinboundoff` | `skinpack.md`, `skinbound.md` |
| 10 | P2NC: non-collideable objects' partition moves after the walk | tick | on | `p2passoff` | `review_p2pass.md` |
| 11 | FVP: function values and change colours as a pass | tick | on | `fvpassoff` | merge `e7892320` |
| 12 | PSC: scenery pose tables (with c10fix's 32 KB margin) | tick | on | `pscoff` | `review_psc.md` |
| 13 | posefpu (B44): an ME-listed pose made in the ME's form everywhere | tick | on | `posefpuoff` | `review_posefpu.md` |
| 14 | PWSL, SPHSL, RECONLIST: a reconnect's walks answered by their slack | tick | on | `pwsloff`, `sphsloff`, `reconlistoff` | commits `4a70d245`, `0a7f17e2`, `4e0aab7c` |
| 15 | TP1: the passes' gates in line | tick | on | `tp1old` | commit `4db25269` |
| 16 | WXDEAD: the weather's rays skip a search nothing reads | tick | on | `wxdeadoff` | `review_tock158.md` |
| 17 | TIMERCALLS: the instrument timers off | tick | on | `timerson` | `review_timercalls.md` |
| 18 | STACKHOT v2, HOTG, PSP_LTO: layout and link | build | on in the recipe | `PSP_STACKHOT=0`, `PSP_DATA_LAYOUT=none`, no `PSP_LTO` | commits `85ace8d6`, `214931c6`, `16da23ea` |
| 19 | AIME (1b + 2a): the AI thinks on the ME during the record | ME | on | `aimeoff` | merge `8c231384`, commit `a0a35053` |
| 20 | AIME's fixes: aimelook, RW1, the late pass, AIMEMOVE, mevfpu | ME | on | `aimemoveold` (AIMEMOVE) | merges `606f9716`, `751b81bb`, `ef10d1ca`, `d367e0ae` |
| 21 | AIMEOBSLOOK (B54): the AI job looks at its word inside the path search | ME fix | on | `aimeobslookoff` | `review_mestop.md` |
| 22 | LIGHTRINGLOOK (B50): the ME serves the ring inside the light pass | ME fix | on | `lightringlookoff` | `review_lightringlook.md` |
| 23 | MEBUSY: the ME's busy share counted once | measure | on | `mebusyold` | `review_tock158.md` |
| 24 | B46: the bind's and widget sprite's keep words settled on the main CPU | fix | on | none | merge `9887e68d` |
| 25 | MUSADPCM + audio44: the disc's 44.1 kHz music at its own rate | sound | on (needs the new bake) | `musadpcmoff`, `audio44ordinary` | `review_tock159.md` |
| 26 | MUSROOM: the sound cache +256 KB with the full-rate music | sound | on | `musroomoff` | `review_tock159.md` |
| 27 | SNDEDRAM2: the sound cache's last 656 KB in the ME's eDRAM | sound / heap | on | `sndedramoff` | `plan_sound_edram.md`, `review_tock158.md` |
| 28 | SNDTIERBACK v3 (B55): the tier's sounds read again after an ME stop | sound fix | on | `sndtierbackoff` | `review_mestop.md` |
| 29 | PROPCRASH (B49): a found body's shout only while its prop lives | crash fix | on | `shoutnone` | `review_propcrash.md` |
| 30 | KEEP_DEFAULTS_ROOM 384 -> 640, and a dropped default logged | keep words | always | none | `review_tock161.md` |
| 31 | FX15, AIFAR (compromises), aimeringlook, vmetest, aimevisleft | levers | **off** | `fx15`, `aifar`, `aimeringlook`, `vmetest`, `aimevisleft` | `review_aifar.md`, `review_aimeringlook.md`, `review_vmegame.md` |
| 32 | pcsample, pcsampleother, keep_last_launch's pcsample | instruments | off | `pcsample` | commit `732d17df` |
| 33 | The bake: stand-ins (TSI1), `--sound s9full`, `--music-adpcm`, `--music-stream` | bake | see §6 | see §6 | §6 |
| 34 | Profile `campaign-w161a`, layouts `w161a_tgc90`, `data_hotg161a` | profile | n/a | n/a | `review_tock161.md` |

**Unchanged since b149:** `aicad3` (on since b140; Sheepdog01 2026-11-04 13:26: it stays on for this release), the sleep stopgap (`psp_sleepguard.c`: the power switch still does nothing in game; B41's last part is open), `sleepready`.

**Not in this review:** docs-only commits (the layout plan, the big-moves plan, the surveys, the cost map, `pdc_plan.md`, `l15_design.md`, `compromise_table.md`, `node_matrix_readers.md` and the other research), the counting profiles between rounds (`campaign-w150a` to `w160a`), the recon-only and emulator-only switches (`PSP_ME_DATA_LAYOUT`, MEHOTG's build switch, not in the recipe; `aimesimstack`, which changes only the emulator's simulated AI stack), and lanetrace's kinds.

## 1. Drawing

### 1.1 DRAWPART slice 2 + LIGHTREUSE (`review_drawpart.md`; merge `b007e0df`, on in `badb0db7`)
- **What:** the model walk records each `render_model` call (64 records a buffer) and makes them after the walk, so the walk's code and the drawing's each stay in the I-cache across the objects. `lightreuse` (turned on with it) lets the walk take the lighting LIGHTFIRST's pass made.
- **Key line:** `psp_platform.c` `keep_levers`: `{ "drawpart", "drawpartoff" }`.
- **Proof:** exact in play, both seeds and every GE list (dp2x1 41861226372; t160x2: b30 2,280 of 2,280 lists, c10 1,832 of 1,832) [E]. c10 frame -6.1% and -7.4% (PSP-3000 41870028074), b30 flight -4.3% (PSP-1000 41870061338) [M].
- **Open:** B57, c10's later tick +1.2 ms with it though it changes no tick code (seeded PSP-1000 pair 41899228607); the bugfix5657 helper has it.

### 1.2 LIGHTFIRST (`review_lightfirst.md`; merge `9829f95b`)
- The objects' lighting in a pass of its own before the model walk. Exact; c10 PSP-3000 frame -1.9% (real), drawing -1.3 to -3.0% (41852005734) [M].

### 1.3 FLC, `lightcarry` (merge `f1dd211d`)
- The flashlight's light pass splits each triangle against the cone. Each piece's corners now take their answers from the corners they share instead of working them out again; the cone is tested as an octagon. `lightcarrycheck` splits both ways and compares hashes [E]. d20's swamp with the flashlight: frame -10%, drawing -23% (PSP-3000 41781771922); Sheepdog01's own b30 bunker fight 21.6 -> 23.1 fps (41788564764) [M].

### 1.4 mememo and geoindexme (`4c909478`)
- The ME's model parts leave out lighting and texture binds the GE already has, and its geometry cache looks blocks up in the sorted index read-only. Both exact; b30 PSP-3000 beach frame -1.0%, flight drawing -0.8% and -1.3% (41272758085) [M].

### 1.5 STANDIN, `texavg` (`review_texavg.md`; merge `f9d024e9`, on in `81da23ee`)
- A texture that isn't in memory yet is drawn as its 4 x 4 stand-in instead of untextured. It needs the pack's `TSI1` table, which only the new bake writes (§6): an older bake logs "no stand-ins" and draws as before. Sheepdog01 (2026-11-04 00:21): "a restoration of a kind". b30's untextured frames 28 -> 0 [E]; no frame cost on b30 (41779878353) [M].

### 1.6 B45's shadows and nearhold (merges `92b68f0b`, `5a2db0ed`)
- a10's cinematics overflowed the 768 KB display list (shadows 402 KB a frame), which dropped flares and shadows and, in Sheepdog01's b149 test, cut a hole. Shadow receivers are now indexed (`shadowindexoff`) and those past the rim left out (`shadowrimoff`): skips 4,769 -> 0, list peak 554 KB, exact [E].
- nearhold: the near plane moves back out only after 15 frames that allow it (B45's b30 lead). Exact; the emulator's routes barely move the plane, so its effect on b30's forest is Sheepdog01's to see [U].

### 1.7 listrange and the skin plans (merges `efc8642f`, `a716729b`)
- listrange: after a geshow3 JUMP the pending range could end under its start and write nothing back for the rest of the frame. No run had logged a jump; it's a latent fix [D].
- SKINPACK places the skinned parts' conversion scratch above the finished part; SKINPLANBOUND bounds the skin plans' bytes and sweeps the oldest unused. Exact on five maps; the heap's largest block +38-71 KB; a30's failed skin_plan requests 0 [E].

## 2. The tick

- **P2NC, `p2pass`** (`review_p2pass.md`; merge `66fcb26a`): a non-collideable object's partition moves made after the object walk. Exact (t161x1/x2). PSP-1000 c20 pair on the tock's own layouts: frame -2.1 / -3.2 / -7.0 / -7.4% (all real), tick -0.6 ms (41905207966) [M]. This is the tick gain the release notes call "P2".
- **FVP, `fvpassoff`** (merge `e7892320`; c10fix `a61ca506` limits it to units, items and projectiles): the function values and change colours made as a pass. Exact; b30 flight tick -1.0% (PSP-3000 40989795526) [M].
- **PSC, `pscoff`** (`review_psc.md`; merge `28ad68aa`, c10fix): scenery pose tables, kept with a 32 KB margin for busy tables. Exact, psccheck 0; c10fix's PSP-1000 pair: landing tick -2.3%, beach -2.6% (41041918667) [M].
- **posefpu, `posefpuoff`** (`review_posefpu.md`): B44's fix, a pose listed for the ME is made in the ME's form wherever it's made. Not bit-exact by design (d20 parts at tick 61 as its stand-ins change form); local state only, so it falls under the multiplayer boundary rule; d20's side-by-side matches.
- **PWSL, SPHSL, RECONLIST** (`pwsloff`, `sphsloff`, `reconlistoff`): `object_reconnect_to_map`'s point walk, cluster walk and list rebuilt only when the object moved more than the last walk's slack. Exact (play + seeds on five maps; sphsl1 41572394832 wrong 0) [E].
- **TP1, `tp1old`**: VISLAZY's skip and FVP's settle test their first conditions before the call; 2.0% and 0.81% of c10's tick on the PC sampler (41224991658) [M].
- **WXDEAD, `wxdeadoff`**: exact; c10 frame -1.6% (PSP-3000 41779868380) [M].
- **TIMERCALLS, `notimers`** (`review_timercalls.md`): the instrument timers feed only statistics, and each clock read is a system call; off by default (`timerson` keeps them). Calls a tick 282 -> 23 [E].
- **STACKHOT v2, HOTG, PSP_LTO:** two shared collision lists from the game's partition (PSP-1000 b30 frame -1.4/-2.1/-1.4%, 41328728869/41328869128); the tick's hot small variables packed (b30 tick -1.7 to -1.9%, PSP-3000 41308669112/41308798525); link-time optimisation of the engine's hot units with the profile (`CLAUDE.md`'s switch table). All three are in the recipe; only data addresses and code placement move.

## 3. The Media Engine

- **AIME, `aimeoff`** (merge `8c231384`, on in `a0a35053`): the AI pass after a frame's last tick runs on the ME during the record. Sheepdog01 (2026-11-03 08:17): judge it by feel; the AI thinking a few ms earlier in game time isn't something a player perceives. b30 frame -3 to -10% (PSP-3000 41643874495) [M].
- **Its fixes, all on:** aimelook (`606f9716`: the collection's word looked at before each decision pass and inside the searches), RW1 (`e6f63ee0`, `d213d551`: render jobs inside the AI job's yields counted and bounded), 2a-late (`751b81bb`: a late pass hands its actor back), the ring stopping at the AI job's word inside its yields (`803399b3`), AIMEMOVE (`ef10d1ca`, `aimemoveold`: the ME faulted on b40 when an actor's command list teleported its unit; the AI pass now stops at an object's move), mevfpu (`d367e0ae`: the ME has no VFPU; `matrix4x3_multiply` keeps the FPU's form there).
- **AIMEOBSLOOK, `aimeobslookoff`** (`review_mestop.md`): B54, the ME stopped on d20 because a path search's last node held the AI job's word 45 ms past its 20 ms bound. The obstacle-avoidance search now looks at the word between its steps. Exact; safe on the PSP-3000 (41827646476); its b30 price nothing (41832388347) [M].
- **LIGHTRINGLOOK, `lightringlookoff`** (`review_lightringlook.md`): B50, the flashlight's light pass held the ME past ring deadlines; it now drains the ring every ~1 ms of ME clock. PSP-3000 d20 flashlight lates 9/8 -> 0/0 (41807114492) [M].
- **MEBUSY, `mebusyold`**: a measure only. The overlay's ME share no longer passes 100% (Sheepdog01 saw 116%).
- **B46** (`9887e68d`): five impure calls reachable from the ME; the bind's and the widget sprite's keep words are now settled on the main CPU. Exact [E].

## 4. Sound

- **MUSADPCM + audio44** (`review_tock159.md`; on in `60b7089c`): the disc's 44.1 kHz stereo music mixed at its own rate, in the ME's audio job after its mix; the converter's channel at 44,100 Hz by default since `e0a7dc05` (`audio44ordinary` keeps the ordinary channel). Sheepdog01 (2026-11-03 22:38): keep the 44.1 kHz music. Cost +0.8% of b30's frame (41214741040) [M]. **It needs a bake whose music tags are left at 44.1 kHz** (`--sound s9full`, §6); on any other bake the music is the halved voice, as before, through the 44.1 kHz path [D: `full_rate_voice` in `psp_dsound.c` picks voices by format].
- **MUSROOM, `musroomoff`**: the sound cache +256 KB, only with musadpcm and sndedram on. d20's refusals over 300 s 220 -> 133 [E]; the PSP-1000 ladder refused none on any map (41910133801) [M].
- **SNDEDRAM2, `sndedramoff`**: the sound cache's last 656 KB in the ME's eDRAM, filled by the ME's copy job, fenced so no sound spans the two parts. 528 KB of heap back; both PSPs clean. The review of tock158 found and fixed the copy job's bound (it now runs from the job's start).
- **SNDTIERBACK v3, `sndtierbackoff`** (`review_mestop.md`): B55's safety net. After an ME stop, the tier's playing and loading sounds are read again from the disc into the main tier, with no eDRAM or DMA touched. Sheepdog01 (2026-11-04 10:53): an ME stop is a bug, to prevent; this is the net only. Safe on the PSP-1000 after a forced stop (41865182489) [M]; 352 of 444 blocks still play silent there, so it isn't a full recovery.

## 5. Crash fix and keep words

- **PROPCRASH, `shoutnone`** (`review_propcrash.md`): B49, Sheepdog01's b30 hand-run crash at game time 27,037. With aicad3 a found body's stimulus can wait a tick, and the body can be deleted meanwhile; the shout is now set up only while the transition's prop lives. `shoutforce` reproduces the crash, `shoutcensus` logs each case.
- **The defaults' room** (`68a2f134`): `KEEP_DEFAULTS_ROOM` 384 -> 640. The defaults need 358 bytes, ~368 with the release's `nooverlay`; a default that finds no room is now named in the log ("NO ROOM for: ...").
- **The defaults table**: eleven levers in `keep_levers`, read before M6 #2's table so `m6off` leaves them on: `wxdead texavg lightcarry sndedram musadpcm lightringlook aimeobslook lightfirst sndtierback drawpart p2pass`. `keep_defaults` gains `mememo geoindexme notimers aime aime1b aime2a`.

## 6. The bake

What changed in the bake tool since b149's (`git diff b4e06773 3a104119 -- bake tools/texpack.py`):
- **`tools/texpack.py`** writes a `TSI1` table after the averages: each texture's 4 x 4 stand-in as a CLUT4 palette (32 bytes a texture). STANDIN reads it; a build that reads only up to `TAV1` never looks at it.
- **`bake/lib/soundpack.py` and `bake.py`** gain `--sound s9full` (s9, with the music tags left as the disc has them: 44.1 kHz stereo Xbox ADPCM, for the full-rate path), `--music-adpcm` (also writes `maps/<map>.pmad`, the disc's music blocks unchanged; the map is the same as without it) and `--music-stream` (Vorbis `.pmus` packs for the music path, `PSP_MUSIC=1` builds only; `bake/bundle/musicenc.c` is its encoder, built into the bundle).
- **The default is still `--sound s9`** [D, `bake.py`'s `ap.add_argument('--sound', default='s9', ...)`]. The tock's bake w157m was made with `--sound s9full --music-adpcm`. As far as I can read, nothing in the game opens a `.pmad` (no reader in `psp/src` or `psp_fixups.py`) [D], so the full-rate music comes from `s9full` alone. **So a player's double-click bake from this commit's bundle gives the halved music and the stand-ins, not the 44.1 kHz music.** The release cut must either make `s9full` the bake's default (and run the bundle's checks) or ship the halved music; that's a call for the master and Sheepdog01, not made here.
- The game's bake check is unchanged (`psp_bakecheck.c`): b150 plays a bake from b147h's, b148's or b149's tool, without the stand-ins or the full-rate music.

## 7. The profile, layouts and recipe

- **`pgo/campaign-w161a.tgz`**: 13 counting runs at `68a2f134`. Strict builds found no function without counts at `47c1c58b` (41897532654) and at `3a104119` (41904627308). The counting runs use the preconverted bake, so the full-rate mix has no counts (built at -O2, not for size) [D].
- **`psp/layout/w161a_tgc90.txt`** (234 hot functions, 90.1% of the fetches) and **`data_hotg161a.txt`** (208 placements, 98.0%).
- **The recipe:** `PSP_LTO=1 PSP_PGO=use PSP_PGO_PROFILE=campaign-w161a PSP_PGO_STRICT=1 PSP_PGO_EXTRA=-fno-unroll-loops,-fno-peel-loops,-fno-tracer,-fno-ipa-cp-clone PSP_TEXT_LAYOUT=w161a_tgc90 PSP_DATA_LAYOUT=hotg161a`, plus `PSP_METRICS=0` for the release zip.

## 8. Proofs

| What | Lab run | Result |
|---|---|---|
| Exactness, each round against the one before (defaults vs off words vs the old tock) | t158 (`review_tock158.md`), t159x1/x2, t160x1 41875328154, t160x2 41875345878, t161x1 41896491380, t161x2 41896505534 | play and both seeds identical at every tick on b30's route, c20, d20, c10 [E] |
| `drawpartoff` draws as before | t160x2 | GE lists identical; pictures within the default/default spread [E] |
| Strict build at the tick | sierra-build 41904627308 | 0 functions without counts, heap 5,772 KB, EBOOT `0a00f831` (metrics on) |
| Release check | rc161a 41904641396 | 14/14 played, strict 0 sites, the `PSP_METRICS=0` build (EBOOT `29620d20`) links and plays the route and the front door [E] |
| The release zip | release150's sierra-build (`release_notes_b150.md`) | strict, `PSP_METRICS=0`, from the docs head; code identical to `3a104119` |
| PSP-1000 ladder, every map in one launch | sierra-hw 41910133801 | no crash, no ME stop, 0 heap failures, 0 sounds refused; fps in the release notes' table [M] |
| P2NC on the tock | sierra-hw 41905207966 | c20 frame -2.1 to -7.4% (real) [M] |
| tock160 vs tock159, b30 route | 41891744166 vs 41820025872 | cinematic -2.2% frame (drawing -7.1%), flight -2.8% (drawing -5.9%) [M] |
| tock159 vs tock158, b30 route | 41820025872 vs 41815173358 | cinematic +1.5% frame (the full-rate music), rest in the spread; heap free low 511 KB [M] |
| b148 vs b150, the same ladder, `ladderreport.py` | 41811265031 vs 41910133801 | the release notes' table; lab finding `b148_vs_b150_ladder` [M] |

## 9. Things to question

- **The bake's default (§6).** The tested bake (w157m) is `s9full`; the bundle's default isn't. Until that's settled, a player's bake and the lab's differ in their music.
- **B57: drawpart costs c10's later tick +1.2 ms** with no tick code changed. The frame still gains (c10 -6 to -7%); a fix is being worked on, and lands on this tick only if it's proven.
- **B56: mepoints fails a frame's job at point 127 of 128.** Open, being worked on; what a player sees, if anything, isn't known [U].
- **B58: the emulator's heap fills where the PSP's doesn't**, so rc161a's failed requests (c40 1,966, b30's route 671) are no verdict; the PSP ladder had 0 on every map.
- **posefpu isn't bit-exact** by design: local state only, which the multiplayer boundary rule allows; d20's side-by-side is its evidence.
- **SNDTIERBACK is a net, not a recovery**: after a forced stop most of the tier's sounds play, 352 of 444 blocks don't. An ME stop is a bug: none happened on the ladder.
- **One unseeded ladder run a side.** The table's map-by-map gaps of a few percent are inside a run's spread; the gains of 15% and more (a50, c10, c20, b30) are not [U].
- **public_export.py --report** lists 21 lines (run numbers and dates in comments: `psp_platform.c` 1135 and 1142, `psp_me.c` 169 and 175, `psp_dsound.c` 3013, `psp_build.py` 459, 810 and 848, `psp_fixups.py` 43905, `psp_lanetrace.c` 73, the layouts' headers, a name in `bake/guided.py` 592 and `soundpack.py` 429). They're for the release cut: changing a comment in `psp/src` now would move lines the profile counted.

## Sheepdog01's hand test (PSP-1000)

Suggested order; none of it has been run by the lab. The hand build is b150h1 (`3a104119`, strict, metrics on) on bake w157m.
1. a10 as usual, then a few levels of your choice. Look for: the music at full rate (crisper than b149's), no untextured frames as a level opens (stand-ins instead), heap failures 0, ME failures 0, `ME: stopped` absent from the log.
2. b30's opening through the beach: the drawing gains are largest there and on c10 (*343 Guilty Spark*).
3. d20 with the flashlight on in the swamp: FLC's and LIGHTRINGLOOK's ground; the frame should hold better than b149.
4. If something new looks or sounds wrong, the launch menu's presets 18 to 20 turn the new levers off in groups (`psp/presets.txt`), so you can tell which one it is.
5. Pull the logs after each run as `CLAUDE.md` says (`b150_test1.tgz` ...).
