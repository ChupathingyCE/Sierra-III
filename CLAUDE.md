# Working in Sierra-III

Sierra-III is a native PSP-1000 port of Halo: Combat Evolved, built on a decompiled engine. Start with README.md for the layout and UPSTREAM.md for what it's built on.

Each rule states its current form with one citation; its wording, history and what it replaced are in the lab's `coord/decisions.md`.

## What the repo holds

- **No game data, ever** (Sheepdog01 2026-11-10): no maps, extracted tags, baked packs (`.map .pgeo .ptex .pstn .vram`), sounds or textures from the disc, in any commit, release, Actions artifact or log of this repo. On GitHub, game data lives only in the private lab repository; nothing public ever holds it (not Sierra-III-Release, not a public release, artifact or page); it may move freely between Sheepdog01's own machines. `.gitignore` enforces the common cases; before a push, check that every binary or large file is ours.
  - **One exception** (Sheepdog01 2026-10-12): the XMB art in `psp/xmb/` may use stills of his own gameplay footage (recorded from PC ChupathingyCE) under Microsoft's Game Content Usage Rules, with the README's notice. Only the exported XMB images: never the raw captures or cutouts, nothing extracted from the game's files, no Microsoft logos.
- **No Sony firmware.** `flash0:` modules (e.g. `lowio.prx`) stay off the repo; describing them in docs is fine.
- **Test scaffolding goes in the lab repository** (run specs, workflows, run scripts, results). This repo holds only what the port needs to build, bake and be tested by hand. Sessions that work on the lab get its name and rules from its own CLAUDE.md; `docs/testing_pipeline.md` describes its shape.
- **Commit identity** (Sheepdog01 2026-10-10): author `Claude <noreply@anthropic.com>`, with `Co-authored-by: Sheepdog-01 <15330660+Sheepdog-01@users.noreply.github.com>` (it keeps the commits on his GitHub profile). Commits before 2026-10-10 are `Sheepdog-01`'s; leave them.
- **His handle, never his real name, in anything public or shared outside the lab** (Sheepdog01 2026-11-17): Release, write-ups for other developers, artifacts or pages shared out, and this repo once it opens: **Sheepdog01**, or the GitHub handle **Sheepdog-01**. The lab's leak guard refuses his surname here and his whole name in Release's exports; a shared doc no guard sees (a claude.ai page, say) is checked by the session that writes it. New text attributes his rulings to the handle, `(Sheepdog01, date)`; older lines naming him stay until the sweep before this repo opens, when his first name joins the guard's list.

## Builds

- **Ticks and tocks** (Sheepdog01 2026-10-24):
  - **A tick** is the release candidate: the last tick, the tock's work that passed its proofs, and fixes only. Sheepdog01 hand-tests the newest tick; it gets a fresh profile, strict builds, the release checks, a review walk-through and build notes. A fix found in a tick goes into the tock the same day. A known bug is fixed before a hand run, not carried into it; hand runs have no timetable (Sheepdog01 2026-11-17).
  - **A tock** is where new work lands, proven in the lab (exactness, A/Bs, PSP pairs) but not polished, and not handed to him to play unless he asks.
  - When a tick passes his test it's the release, and the tock becomes the next tick (polished while new work starts on the tock after it); a big win may cut a tick early. Never hold work back to wait on a test. Each shipped build is tagged `bNNN` and its zip added to the `build-archive` Release (a record, not for sharing); a build gets a Release of its own only when he chooses to share it. The lab's `psp/sierra/releases.tsv` says which builds are ticks.

## The goals (Sheepdog01 2026-11-06; the dashboard's `goals`; `docs/redesign_tick.md`)

Every design, lever and report is judged against these, not against "faster".
- **The goal: a 30 fps average** per level over interactive play (frames ÷ seconds, cutscenes excluded): one frame per tick. **Desired: a 30 fps floor**, held everywhere.
- **The hard floor: real time**, 30 ticks a second held in every gameplay stretch, tried or not, so at least 15 fps (below that the game slows instead of skipping ticks). No build may fall below it.
- **60 fps is not a goal** (it would need interpolation). **Even motion is the end goal:** the pacing's 20 fps step is deferred (a 30/15-only pacing later, behind a keep word, his hand A/B); benches keep pacing off.
- **Attack 30/30, not 15/30** (Sheepdog01 2026-11-09): efficiency and untapped hardware first (the same game done with less work or fewer misses; units the tick doesn't use yet), not marginal levers that scrape by to 15/30.

## Before proposing work

- **Don't repeat what's been tried** (Sheepdog01 2026-11-05): before a lever, a census or an investigation, check the lab's `tools/levercheck.py` on its ground and its words (it also searches this repo's `docs/`) and the cost map (`docs/cpu_cost_map.md` and the pages it cites). The proposal quotes what it found and names the cost-map line it attacks.
- **Everything is questioned, analysed for gain and justified for its cost** (Sheepdog01 2026-11-09), from the map of the existing work (`docs/tick_map.md`): what each system computes, where each lever was priced, where else it might gain. **Every price carries its scenario** (device, map, stretch, build): a stop on one stretch isn't a stop everywhere.
- **"Spent", "done" or "no exact cut" isn't a close** (Sheepdog01 2026-11-09): a closure made on exact forms only reopens under "plays and feels the same".
- **A rule that blocks a gain goes to Sheepdog01 as its own question** (Sheepdog01 2026-11-09): what the rule protects, what breaking it buys, and the risk. Don't break it, and don't drop the idea silently.

## What may change, and who decides

- **Speed work is pre-approved unless it takes something away** (Sheepdog01 2026-10-13). The session builds, ships and turns on, without asking:
  - exact speed-ups, and restorations;
  - **changes that play and look the same** though not bit-exact (Sheepdog01 2026-11-05, 2026-11-09): **the session judges that itself and turns the change on.** A few scattered pixels a frame, with no shape a player could see, is the same look; a timing difference far under what a player can feel (the AI deciding from a world one tick old) is the same feel. This covers the simulation, collision included, inside the multiplayer boundary below;
  - **running less often when a player can't tell** (Sheepdog01 2026-11-09): far bipeds at half rate, poses blended between ticks, AI thinking less often. It's efficiency, proven like any plays-the-same change. **AIFAR** (far actors thinking less often) is one: off by default behind `aifar` until the lab is certain it breaks nothing and doesn't make play worse (strict, the outcome census, the PSP price), then the lab turns it on; no hand A/B gate (Sheepdog01 2026-11-17).
- **Choosing and proving a non-exact change** (Sheepdog01 2026-10-30, 2026-11-09): an exact form that gains about the same is the one built (it keeps the cheap proof); a non-exact form needs a measured reason named in the build's review. Its proof is side-by-sides and an outcome census (encounters end the same way), and its exactness check is the build with it switched off. A/Bs stay inside one build (`keepsets`), both sides carrying the new maths; benches compare builds by stretch, not tick by tick. Bit-exactness is a proof tool (statecrc: play and both seeds identical), not a requirement: the extra proof is a cost to weigh, never a reason to rule a change out.
- **Far and tiny objects and effects are drawn approximately, at overshooting thresholds, tuned back by eye later** (Sheepdog01 2026-11-10; the lab's `drawing_sizing.md`). The far forms (sprites, a far level as one part, baked shading with the GE's lighting off, one bone a vertex, passes left off, coarser levels; particles, decals and lights not made under a size on screen) are efficiency: the lab builds, tests and turns them on (on by default, opt-out), each behind its keep word, its threshold a keep-word number, until Sheepdog01 has seen it in his own play; he opts out if he notices something he doesn't like, and no shot sets are made for the thresholds (Sheepdog01 2026-11-18). Their proof is play and seeds identical (drawing only), not "looks the same". Explosions, fire, detonations, projectile visuals and tracers stay to the horizon; near objects and first person keep the rules above. Anything not visible at the PSP's resolution is examined as an opportunity (Sheepdog01 2026-11-15).
- **Compromises go in the overclocking bucket** (Sheepdog01 2026-11-10). A compromise is a speed-up that detracts: draw distance, fewer effects, lower quality, a difference a player can tell. It's untouched until the finished game state, priced only when Sheepdog01 asks (its exact cost told to him), and no part of any plan until he decides; when he does, it's shown to him as an A/B. One found is catalogued (the lab's `psp/sierra/findings/levers.tsv`, kind `compromise`, with its measured or estimated worth) and left there. L15, FX15, RENDERTICK and PRIO60 are already in the tock (`docs/compromise_table.md`), each off behind its own keep word, not A/B'd or planned on; L15 is held in reserve until everything else is done, as overclocking is. The aim is to approximate the original's look and feel, not replicate it (Sheepdog01 2026-10-22).
- **Look or behaviour changes made for reasons other than speed:** one a player can tell (the HUD's size, say) is Sheepdog01's call after an A/B; one a player can't tell is the session's, proven as a plays-the-same change (B98's sound waits clocked by game time; Sheepdog01 2026-11-18). Restorations are neither.
- **Restoring the original isn't a change** (Sheepdog01 2026-10-11): where the port draws or behaves differently from Halo on the Xbox (the ocean showing through the seabed or drawn white, missing or wrong textures), the fix goes on by default without waiting for an A/B, with a keep word that turns it off; the build notes say what it restores.
- **What "the original" means** (Sheepdog01 2026-10-12): Halo on the Xbox, or the PC edition (close enough, though some of its features and visuals are broken). Where the tags and the code can't settle how something should look, ChupathingyCE is the faithful source. Screenshots from a web search are not: the remasters and later versions draw very differently.
- **The main menu is a showcase** (Sheepdog01 2026-10-16): ui.map may take whatever quality the PSP can give it (textures at full size and colour, model detail, extra passes), exempt from the frame-time limits on restorations, as long as nothing it does costs the other maps anything: its own pack, and switches that act on ui.map only, each with a keep word.
- **Every change ships behind a keep word**, so one build can test both sides and any change can be switched off. **Approval never holds up the work** (Sheepdog01 2026-10-13): a change that needs his call (other than a compromise) goes into the next build anyway, off by default behind its keep word, with its A/B in the build's tests; speed work carries on around it.

## The multiplayer boundary (Sheepdog01 2026-11-09)

Halo's netcode isn't lockstep (`docs/hardware_utilization.md` §6): the PSP is only ever a client, nothing it computes is compared bit for bit, and no tick is checked. Collision, node matrices, AI, physics and drawing never go on the wire; they reach the host only through the client's movement and hit reports, which the host takes within loose bounds. So **no simulation state has to stay bit-exact for multiplayer.** What it requires:
- **play well inside the tolerances:** the host takes the client's own biped within 3.5 units (the client snaps past 3.0) and its vehicle within 5; a change that makes the PSP's movement drift shows as snaps;
- **true hit reports:** the client picks the region and material it saw hit, within 3-6 units;
- **matching discrete values:** objects placed at map load take the host's datum indices (nothing removed so that slots shift), the game rules and the 30 Hz tick are the host's, and the join's network version and tag checksum match (`psp_limits.h` moves with the pin).

The campaign build has no server: there the only line is that it plays and feels the same.

## Multiplayer (`docs/multiplayer_plan.md`, "The lab's rules for multiplayer work")

- **Compatible by design; co-op is the end goal** (Sheepdog01 2026-10-30): structural work is designed for single player, co-op host and client. A change not yet built for network play ships campaign-only with a `co-op:` note in its `levers.tsv` evidence; one that can never work in a network game (a different tick rate, as L15) is single-player only, with Sheepdog01's OK.
- **A contained lane** (Sheepdog01 2026-11-12): only the multiplayer session works on multiplayer, in its own files and `FIXES_` groups; outside it, compatibility is kept on paper and the campaign doesn't run `PSP_MULTIPLAYER=1` checks. Once multiplayer work resumes in full, a change that would cost multiplayer to help the campaign goes to Sheepdog01 first.

## Proper fixes, and where work runs (Sheepdog01 2026-10-27, 2026-10-28)

- **Failures:** when work moved off the main CPU (the ME's jobs, the GE's lists) fails or runs long, fix it where it runs: find why it ran long, make the job fit its deadline, keep its waits bounded. Routing the hard case back to the main CPU, or switching a lever off, isn't the fix; the existing fallbacks stay as the safety net. A stopgap ships only with Sheepdog01's OK, named as one, with its proper fix tracked (the lab's bugs.md or levers.tsv) and done next.
- **Placement:** which unit runs a job (the main CPU with its VFPU, the ME, the GE) is decided by measurement on the PSP, in either direction, judged by the overall balance between them, not by the job's own speed on one unit: a move pays when a PSP pair shows the frame faster (ticks first) with the load spread better. Moving work onto the main CPU to use the VFPU, so measured, is a placement, not a fallback.

## The rebuild (Sheepdog01 2026-11-05, 2026-11-11)

The lab's `research/opportunities/REBUILD.md` is the spine: the frame budgeted from zero as **boxes** (one subsystem each, the port's own code in `psp/src/` with its own data, a contract and a budget on every unit), built forward from the spec, not patched over the old path. How a box is built, priced and proven: `docs/redesign_plan.md` §9. Who calls what:
- **Budget first:** a part starts when the lead puts it in REBUILD's box list with its ceiling on each unit and stretch against the plan's frame budget (§1); it needs no go from Sheepdog01 (Sheepdog01 2026-11-18).
- **Cut in under its own build switch, off by default, on the lead's call**, with no go from Sheepdog01 (Sheepdog01 2026-11-11); proven on vs off in one build, or in two (old-only, new-only) when both paths don't fit the heap together (Sheepdog01 2026-11-16, 2026-11-10).
- **Priced in the frame,** alone or with a codependent group; the standalone price is a sanity check, not a gate (Sheepdog01 2026-11-16).
- **Levers aren't filler** (Sheepdog01 2026-11-14): work on the old path runs on a PSP only as an ask with a question, a reader and the decision it feeds; an idle PSP with no such ask stays idle. Lever work is tagged `[lever]` and isn't reported as rebuild progress.
- **Levers stay off a part's ground:** once a part is on the box list, a lever on a ground it replaces isn't started unless it pays before the part lands and the lead asks for it (Sheepdog01 2026-11-18; the lab's `tools/levercheck.py` names the part).

## The hardware rules

- The target is the PSP-1000: 333 MHz, 24 MB user RAM, 16 KB caches. Never rely on overclocking.
- Write the CPU's cache back before the GE, the Media Engine or DMA reads what it wrote.
- Unaligned word loads fault on hardware (the emulator forgives them).
- `double` is software: keep it out of hot paths.
- **The Media Engine has no VFPU and no kernel** (Sheepdog01 2026-10-30): code that can run on the ME must not reach a VFPU instruction or a system call, and a path newly moved onto the ME is proven on a PSP, because the emulator lets the ME run both. A stack-pointer test tells the ME from the main CPU only while nothing can move the ME off its own stack, so a jump is taken only on the CPU that set it (one taken on the ME into the main CPU's jump buffer ran on the main thread's stack and corrupted it). CP0's CPU number can't be read from the main CPU (user mode). Instruments that read the clock return early on the ME (`PSP_ME_WORK()`).
- **No calls to the power rails** (Sheepdog01 2026-11-17): no syscon power-rail call ships, even behind a keep word; any kernel call that touches power or the hardware's rails goes to him first as its own question.

## Evidence

- **Labels in docs:** [M] measured on the PSP, [E] emulator or estimate, [D] read in code or data, [U] inference.
- **Only the PSP says where time goes and how long** (Sheepdog01 2026-11-09): the emulator answers correctness, play, pictures and exact counts; rankings, ceilings and budgets come from PSP runs, and an emulator time figure is a lead at most (its split of the tick by step misled: the lab's findings `combat_steps_psp_vs_emu`).
- **The emulator dismisses on correctness and counts, never on time where it's blind** (Sheepdog01 2026-11-05): it may stop a change because play parts, a picture breaks, or an exact count shows the work it targets is too small. It may not stop one on a time figure when the cost runs through something it doesn't model (the Media Engine, interrupts and the GE's signals, DMA, contention for main RAM between the units): those are priced by a PSP pair or parked with a `revive_if`.

## How AI is used here (Sheepdog01 2026-10-13; `docs/ai_policy.md`)

Sheepdog01 decides what to build, tests the ticks on hardware, and is responsible for it; Claude writes the code, and every commit says so (the identity above).
- **Each build ships with a review walk-through** (`docs/review_bNNN.md`): every change merged since the last build, its key lines and why they're there, short enough for him to read and question before testing.
- **Comments say why, not what, and not when.** Build history belongs in commit messages and build notes: when a comment is touched, move its history out and keep the reason.
- **Upstreaming to ChupathingyCE, and crediting code or ideas borrowed from another project:** `docs/ai_policy.md`, read before either.

## Docs

- `docs/` holds every write-up about the port; `docs/README.md` is its index, and a new write-up gets its line there in the same commit. Start with `redesign_plan.md`, `optimization_ranking.md`, `chupa_rebase.md` and `psp_hardware_reference.md`.
- **Findings are documented as we go** (Sheepdog01 2026-11-10): a finding (a number, a cause, a diagnosis, a correction) goes into the doc where that work is done in the same turn it's learned (the part's design page, the cost map, `hardware_facts.md`, a review). One that changes an earlier line updates it where it stands (marked superseded, dated), so no stale claim is left beside the new one.
- **One home per figure** (2026-11-16): hardware figures in `hardware_facts.md`, prices and run figures in the lab's findings tables; other pages cite the home. A superseded figure gets its row in `docs/superseded.tsv` in the same commit; `tools/factcheck.py --stale` lists pages still stating one.
- **`cpu_cost_map.md` guides the speed work** (Sheepdog01 2026-11-04): a lever's proposal names the line it attacks; a result that changes a figure there updates the page in the same turn.
- **`hardware_facts.md` holds every measured hardware figure** (Sheepdog01 2026-11-09): a new measurement adds or updates its bullet in the same turn, with its evidence label and source, and a question it settles leaves the Open list. When two measurements disagree, both stay, marked CONFLICT, with what differs. `tools/factcheck.py` checks every citation still says what the page says it does.
- `docs/research/` holds the research behind the levers; cite it from a lever's doc rather than restating it.
- **GitHub is the home; the claude.ai project ("PSP Game Development") is the summary:** its `living/` docs say where things stand and point here. A new write-up goes in `docs/` first; the project gets a copy only when it should be read in chat.

## Building, testing and installing

- **Building** and every build switch: `docs/build_switches.md` (from a clean machine: `docs/toolchain.md`). The engine checkout is never edited: `psp_fixups.py` applies anchored pairs to a mirror of it.
- **Keep words:** `ms0:/halo/keep.txt` (or the launch menu), read by `psp_keep()` in `psp/src/psp_platform.c`; `docs/optimization_ranking.md` (appendix B) lists the speed-related ones.
- **The emulator, the lab's runners, hardware runs, installing and pulling logs:** `docs/install.md`.
- **The stick** (Sheepdog01 2026-10-10, 2026-10-11): delete only within `$psp/halo`, `$psp/PSP/GAME/halo`, `$psp/halomp` and `$psp/PSP/GAME/halomp`; leave ARK-4's files and `overconfig.txt` alone; never format or wipe the stick.
- **Logs** (Sheepdog01 2026-10-10, 2026-10-11): pulled off the stick after each run, one `.tgz` per run named for the build and the test; a network test's also carries the server's `debug.txt` (`docs/install.md` has the commands). Give each test its stop condition and an honest time: a bench that waits for game time runs slower than the clock at low frame rates.
- **Slated to move to Sheepdog-01/Sheep-PSP-Tools** (shared with the other PSP ports; here for now): the emulator harness (`tools/emu.sh`, `strictfilter.py`, `routestats.py`, `tools/ppsspp_local_patches.diff`), `tools/hwreport.py`, `tools/pcprofile.py`, `tools/cachemap.py`, and `bench/`.
