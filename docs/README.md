# Sierra-III docs: the index

Every write-up about the port is in this folder: design notes, measurements, plans, and each build's notes. The files stay where they are (findings and tools cite them by path); this page is the way in. A new doc gets its line here in the same commit.

## Start here

- [`port_overview.md`](port_overview.md): what the port is and how its pieces fit.
- [`redesign_plan.md`](redesign_plan.md): where the work is going: the frame budgeted from zero and rebuilt as boxes ([`rebuild_plan.md`](rebuild_plan.md) is its October start).
- [`cpu_cost_map.md`](cpu_cost_map.md): where the main CPU's time goes, and every way of cutting it with its ceiling.
- [`psp_hardware_reference.md`](psp_hardware_reference.md) and [`hardware_facts.md`](hardware_facts.md): the PSP-1000 as measured.
- [`play.md`](play.md): from a stock PSP and your own disc to a playable game.

## The rebuild and its boxes

- [`box_biped_motion.md`](box_biped_motion.md): Box: biped motion, with the collision service (bottleneck, 2026-11-11; v0, spec started)
- [`box_collision.md`](box_collision.md): Box: the collision service (bottleneck, 2026-11-11; v0, spec started)
- [`box_draw_list.md`](box_draw_list.md): The draw list box
- [`box_far_level.md`](box_far_level.md): The far level as one part (the GE lists and forms box, its first far form)
- [`box_lights.md`](box_lights.md): The Lights box
- [`box_posing.md`](box_posing.md): Box: posing, the pilot (bottleneck, 2026-11-11; v0, spec)
- [`box_stick_reader.md`](box_stick_reader.md): Box: the stick reader (streaming)
- [`box_tick_schedule.md`](box_tick_schedule.md): Box: the tick schedule (slice 1, the replay's pick-up points; keep word `ticksched`)
- [`box_world_store.md`](box_world_store.md): Box: the world store (bottleneck, 2026-11-11; v0, spec in progress)
- [`capacity_model.md`](capacity_model.md): Workload and capacity (ledger 2)
- [`compromise_table.md`](compromise_table.md): What lifting the compromise freeze would buy (b30, c10, c20, d20)
- [`direction_check_2026-11-09.md`](direction_check_2026-11-09.md): Direction check at R's milestone (2026-11-09 17:50, master)
- [`draw_map.md`](draw_map.md): The drawing map: the main CPU's drawing by part (2026-11-09, master)
- [`draw_rebuild.md`](draw_rebuild.md): The drawing rebuild: the main CPU's model walk and the ME's replay (design, 2026-11-01)
- [`exact_state.md`](exact_state.md): What must stay exact: the state the multiplayer host reads or checks (ledger 7)
- [`frame_map.html`](frame_map.html): the frame map as an interactive page: a stretch's frame on one timeline (main CPU, GE, ME)
- [`frame_picture.md`](frame_picture.md): The frame picture (design v0): each box's ms a tick and a frame by cause (work, code wait, data wait, the rest), from the profiler at box boundaries
- [`frame_rate_targets.md`](frame_rate_targets.md): Frame-rate targets: 15, 20 or 30 fps (analysis b87, hardware results b87 run 3)
- [`frame_side_surveys.md`](frame_side_surveys.md): Frame-side surveys (2026-10-29)
- [`frame_timeline.md`](frame_timeline.md): The frame timeline (ledger 3)
- [`job_graph.md`](job_graph.md): What depends on what: the job graph (ledger 6)
- [`layout_plan.md`](layout_plan.md): The whole-frame layout plan: every system placed by unit, memory, rate and budget
- [`parity_budget.md`](parity_budget.md): The parity budget: what 30 fps asks of each stretch (2026-11-07, master)
- [`rebuild_plan.md`](rebuild_plan.md): Rebuilding the engine around the PSP: the plan (2026-10-09)
- [`redesign_b.md`](redesign_b.md): B, the memory map by design: the design page (Phase 1 draft)
- [`redesign_plan.md`](redesign_plan.md): The redesign plan: the frame budget, the parts, and how they're built
- [`redesign_r.md`](redesign_r.md): R, retained rendering: the design page (Phase 1)
- [`redesign_r_slice1.md`](redesign_r_slice1.md): R, slice 1: the scaffold (a build spec)
- [`redesign_recon.md`](redesign_recon.md): The redesign's recon: what we map before we build
- [`redesign_s.md`](redesign_s.md): S, the tick by how often its work changes: the design page (Phase 1 draft)
- [`redesign_tick.md`](redesign_tick.md): The redesign: the tick
- [`redesign_tick_target.md`](redesign_tick_target.md): The tick at the target: ceilings and the gap
- [`tick-rate-sites.md`](tick-rate-sites.md): Tick-rate literals: classified sites
- [`tick_b30_psp.md`](tick_b30_psp.md): Where b30's tick goes on the PSP-1000 (2026-10-15)
- [`tick_map.md`](tick_map.md): The tick's work, system by system

## Where the time goes: cost maps and measurements

- [`big_levers.md`](big_levers.md): Big levers: what can close the gap (2026-10-10)
- [`cache_work.md`](cache_work.md): The cache work: loop phasing, prefetch, data layout (draft, 2026-10-20)
- [`cpu_cost_map.md`](cpu_cost_map.md): The main CPU's cost map
- [`dma_engines.md`](dma_engines.md): The DMA engines: what can feed the VME, and how fast
- [`engine_performance.md`](engine_performance.md): Halo CE on the PSP-1000: engine performance audit
- [`engine_performance_b66.md`](engine_performance_b66.md): Engine performance, continued: build 66 (overnight 2026-10-04)
- [`hardware_big_moves.md`](hardware_big_moves.md): The rebuild's big moves, ranked (hardware session)
- [`hardware_facts.md`](hardware_facts.md): Measured hardware facts
- [`hardware_surveys.md`](hardware_surveys.md): The tick, system by system: what eight surveys found (directions 1 and 2)
- [`hardware_utilization.md`](hardware_utilization.md): Hardware utilization: does the port use the PSP well?
- [`fix_b98.md`](fix_b98.md): FIXES_B98: a conversation that has ended is read as none (B98's fix)
- [`hotg.md`](hotg.md): HOTG: the program's hot variables packed by heat
- [`sound_game_time.md`](sound_game_time.md): Sound game time: game logic that waits on sound playback (B98 b, B91; design)
- [`hw_capabilities.md`](hw_capabilities.md): The PSP-1000's hardware: what we use, what we don't, and what to chase
- [`levers_next_b138.md`](levers_next_b138.md): The next levers, ranked by payout (2026-10-17, after b137)
- [`mehotg.md`](mehotg.md): MEHOTG: the ME's hottest variables packed by heat
- [`memory_census.md`](memory_census.md): The memory census (M7): each pool, cache and arena against its size, per map
- [`memory_ledger.md`](memory_ledger.md): The memory ledger: what each block of RAM and VRAM holds, and what a KB of it buys
- [`memory_savings_b137.md`](memory_savings_b137.md): RAM and VRAM: where it goes, what to cut, and how to use both (2026-10-17, b137)
- [`memory_traffic.md`](memory_traffic.md): Memory traffic is the crux
- [`optimization_ranking.md`](optimization_ranking.md): What to chase: every speed lever, ranked (2026-10-09)
- [`order_file.md`](order_file.md): The order file: the game's code laid out by its calls (phase B, T2)
- [`prefetch_map.md`](prefetch_map.md): Where a prefetch pays: the `cache 0x1E` map
- [`psp_hardware_reference.md`](psp_hardware_reference.md): The PSP-1000 for the Halo port: a hardware reference

## The Media Engine, sound and streaming

- [`aime_me.md`](aime_me.md): AIME step 2a: what the ME would have to run (reach census)
- [`audio.md`](audio.md): Halo PSP port: audio path design
- [`audio_ab.md`](audio_ab.md): Audio A/B: proving that a mix sounds the same
- [`audio_addendum_2026-10-03.md`](audio_addendum_2026-10-03.md): 6. Addendum, 2026-10-03: the Media Engine, read in full and tested in prep
- [`audio_fidelity.md`](audio_fidelity.md): Audio: fidelity and efficiency plan (2026-10-14)
- [`geometry_streaming.md`](geometry_streaming.md): Geometry streaming (build 68): render data out of the tag window
- [`me_poses.md`](me_poses.md): The next Media Engine job: next tick's base poses (plan, 2026-10-15)
- [`me_programme.md`](me_programme.md): The Media Engine programme
- [`media_engine.md`](media_engine.md): The Media Engine in the game (builds 86–87)
- [`mevis_landing.md`](mevis_landing.md): Visibility on the Media Engine: landing mevis (option C's step 3, re-scoped)
- [`mevis_slice2.md`](mevis_slice2.md): Visibility on the ME, slice 2: the answer ready when it's read
- [`mexec.md`](mexec.md): MEXEC: the Media Engine's hot code run from its eDRAM
- [`meprofile.md`](meprofile.md): meprofile and meparts (recon): the Media Engine's own miss counters, by job and by step of the render job, code against data
- [`tgme.md`](tgme.md): TGME: the code layout coloured for both caches, the ME weighted as it runs (its copy, its jobs, the pads' windows)
- [`plan_music_stream.md`](plan_music_stream.md): The music path: Vorbis from the stick, decoded on the ME, on a 44.1 kHz output
- [`plan_preconvert.md`](plan_preconvert.md): PRECONVERT: model vertex buffers baked in the GE's form
- [`plan_sndstream.md`](plan_sndstream.md): Long sounds streamed (SNDSTREAMLONG): the plan
- [`plan_sound_edram.md`](plan_sound_edram.md): The sound cache's second tier in the ME's eDRAM (SNDEDRAM2, `sndedram`)
- [`streaming.md`](streaming.md): Streaming: the Memory Stick as a stage of the pipeline (2026-10-11)
- [`vme_capability.md`](vme_capability.md): What the VME can carry, and at what cost (C5)
- [`wake_on_write.md`](wake_on_write.md): Sleepers woken by writes (lever WT): design (2026-10-16)

## Plans and designs

- [`dead_code_inventory.md`](dead_code_inventory.md): Dead code and oversized data: the inventory (2026-10-08, b107 dev tree)
- [`deadcode.md`](deadcode.md): The dead-code pass: code no level needs, out of the link (2026-11-15)
- [`decal_ring.md`](decal_ring.md): The decal ring (lever DECALRING, 2026-11-13)
- [`design_b25_bake.md`](design_b25_bake.md): B25's fix in the bake (design, 2026-10-26)
- [`design_inflightkb.md`](design_inflightkb.md): Design: inflightkb, M6 #2's record buffer sized so a frame never waits on the ME mid-walk
- [`design_mepoll.md`](design_mepoll.md): Design: mepoll, fewer wake-ups of the threads that wait on the Media Engine
- [`design_model14.md`](design_model14.md): The 14-byte model vertex (keep word `model14`, bake flag `--model14`)
- [`go_live.md`](go_live.md): Going public: the checklist
- [`l15_design.md`](l15_design.md): L15: the campaign at 15 ticks a second, design note
- [`pdc_plan.md`](pdc_plan.md): Prebuilt drawing commands (PDC): the census and the build plan
- [`plan_biped_collision_me.md`](plan_biped_collision_me.md): Plan: biped collision and the Media Engine
- [`plan_front_end.md`](plan_front_end.md): Plan: the front end (start-up, main menu, loading screen)
- [`plan_ge_vram.md`](plan_ge_vram.md): The GE's bytes off main RAM (M6 rank 1: L2b and lever #1)
- [`plan_loading_screen.md`](plan_loading_screen.md): Plan: the loading screen, rebuilt as a set piece
- [`plan_m6_limits.md`](plan_m6_limits.md): M6: the limits per map, and the levers re-ranked by them
- [`plan_memory_stalls.md`](plan_memory_stalls.md): Plan: the memory stalls, the speed work's first goal (2026-10-16)
- [`plan_node_matrices_me.md`](plan_node_matrices_me.md): Node matrices on the Media Engine (lever NW, phase D): what the recon says
- [`shelf.md`](shelf.md): The shelf: experiments kept for later
- [`skinbound.md`](skinbound.md): SKINBOUND: the skinning plans' bytes as a second trigger of the table's sweep
- [`skinpack.md`](skinpack.md): SKINPACK: each skin part's room asked for before its scratch
- [`sspr_bake.md`](sspr_bake.md): SSPR at bake: levels of detail from a pixel-error budget (step 1, rigid models)
- [`stencil_bake.md`](stencil_bake.md): Stencil bake (build 87): wall decorations drawn on the wall's own triangles

## Multiplayer

- [`build_mp1.md`](build_mp1.md): mp1: the first multiplayer build for the PSP (2026-10-10)
- [`build_mp2.md`](build_mp2.md): mp2: mp1 with network packets the PSP can read (2026-10-10)
- [`build_mp3.md`](build_mp3.md): mp3: mp2 with a network stack that can't run dry (2026-10-10)
- [`build_mp4.md`](build_mp4.md): mp4: the multiplayer build in a folder of its own (2026-10-11)
- [`build_mp5.md`](build_mp5.md): mp5: build 117's engine, and ad hoc play PSP to PSP (2026-10-13)
- [`build_mpb1.md`](build_mpb1.md): mpb1: the ONLINE GAMES screen (2026-10-15)
- [`build_mpb2.md`](build_mpb2.md): mpb2: internet play made to explain itself (2026-10-16)
- [`build_mpb3.md`](build_mpb3.md): mpb3: the public list proven in the emulator (2026-10-16)
- [`build_mpb4.md`](build_mpb4.md): mpb4: the multiplayer build on b135 (2026-10-16)
- [`milestone_first_netgame.md`](milestone_first_netgame.md): Halo: Combat Evolved on the PSP-1000: the first network game against a PC
- [`milestone_first_public_game.md`](milestone_first_public_game.md): Halo: Combat Evolved on the PSP-1000: the first public internet game
- [`mp_build_audit.md`](mp_build_audit.md): The multiplayer build against the campaign build: an audit (2026-10-17, b137)
- [`mp_client_only.md`](mp_client_only.md): The multiplayer build as a client
- [`mp_framerate_7players.md`](mp_framerate_7players.md): The multiplayer build's frame rate with 7 players (diagnosis, 2026-10-17)
- [`mp_gamemode_wargame.md`](mp_gamemode_wargame.md): Game modes across Halo, and what each kind would ask of the PSP (2026-10-14)
- [`mp_map_survey_prelim.md`](mp_map_survey_prelim.md): The vanilla multiplayer maps: a preliminary memory survey (2026-10-14)
- [`mp_v075a_and_host_memory.md`](mp_v075a_and_host_memory.md): Multiplayer: v0.7.5a anchor check, and the memory a client-only build can drop (2026-10-14)
- [`multiplayer_plan.md`](multiplayer_plan.md): Multiplayer on Sierra-III: where it stands and the plan (2026-10-10, evening)
- [`multiplayer_recon.md`](multiplayer_recon.md): Multiplayer on the PSP: recon (C&C now, Halo later)
- [`multiplayer_setup.md`](multiplayer_setup.md): Multiplayer
- [`psp_browser_design_input.md`](psp_browser_design_input.md): The PSP's server browser: what upstream's code gives us (2026-10-14)
- [`psp_wifi_hotspot.md`](psp_wifi_hotspot.md): PSP Wi-Fi via the desktop hotspot (Linux, NetworkManager)
- [`review_mpb1.md`](review_mpb1.md): Review: mpb1, the ONLINE GAMES screen (branch `mpbrowse`)
- [`review_mpb2.md`](review_mpb2.md): Review: mpb2, internet play made to explain itself (branch `mpbrowse`)
- [`review_mpb3.md`](review_mpb3.md): Review: mpb3 (branch `mpbrowse`, `7f0dc04`)
- [`review_mpb4.md`](review_mpb4.md): Review: mpb4, the multiplayer work on b135 (branch `mp135`)

## Building, baking, testing and playing

- [`asset_baking.md`](asset_baking.md): Asset baking for the PSP port: what has to shrink, and how
- [`bake_census.md`](bake_census.md): What the game recomputes that a bake could carry (direction 4's census)
- [`bake_changes.md`](bake_changes.md): The new bake (go_live 3.3): numpy, Pillow and shapely only
- [`campaign_bringup.md`](campaign_bringup.md): Bringing up a10, c10, c40, d20 and d40 (2026-10-08, b107 dev tree)
- [`campaign_census.md`](campaign_census.md): Campaign map census (all ten maps) — measured 2026-10-02 and 2026-10-04
- [`campaign_recon_b30.md`](campaign_recon_b30.md): Campaign recon: The Silent Cartographer (b30) on the PSP port — 2026-10-03
- [`campaign_window_fit.md`](campaign_window_fit.md): Campaign maps against the PSP-1000's tag window (2026-10-06, all ten maps)
- [`chupa_rebase.md`](chupa_rebase.md): The ChupathingyCE rebase (build 110, 2026-10-09)
- [`halo-psp-feasibility.md`](halo-psp-feasibility.md): Halo CE → PSP-1000: feasibility, budget and plan
- [`milestone_campaign_poc.md`](milestone_campaign_poc.md): Halo: Combat Evolved on the PSP-1000: campaign proof of concept
- [`pad_path.md`](pad_path.md): The pad path: a replay that holds Sheepdog01's route on any build
- [`pad_recorder.md`](pad_recorder.md): The pad recorder: Sheepdog01's fights as repeatable benches (design, 2026-11-08, master)
- [`play.md`](play.md): Playing Sierra-III: from a stock PSP to a playable game
- [`port_overview.md`](port_overview.md): How the Halo PSP port works (overview, build 102)
- [`psp_clock_handoff.md`](psp_clock_handoff.md): Handoff: PSP-1000 loses its clock setting when the battery is pulled
- [`public_release.md`](public_release.md): The public release: how it's made
- [`renderer_debugging.md`](renderer_debugging.md): Renderer debugging notes (builds 52–90)
- [`research_application.md`](research_application.md): Applying the research (2026-10-14)
- [`stage_state_note.md`](stage_state_note.md): Stages need a world that holds still: the shared state that walls the tick's stages
- [`ai_policy.md`](ai_policy.md): How AI is used here: review, comments, upstreaming to ChupathingyCE, crediting what we borrow (moved from CLAUDE.md, 2026-11-18)
- [`build_switches.md`](build_switches.md): Building the port, and every build switch (moved from CLAUDE.md, 2026-11-18)
- [`install.md`](install.md): The emulator, the lab's runners, hardware runs, installing on the stick and pulling logs (moved from CLAUDE.md, 2026-11-18)
- [`testing_pipeline.md`](testing_pipeline.md): How the project tests: the shape of the pipeline
- [`toolchain.md`](toolchain.md): Building Sierra-III from scratch
- [`upstream_reports.md`](upstream_reports.md): Reports for ChupathingyCE
- [`xmb_menu.md`](xmb_menu.md): XMB menu: art, sound and the stick tree

## Investigations, censuses and recon

- [`b105_candidates.md`](b105_candidates.md): b105 candidates (worked out 2026-10-07, while b104 waits for hardware)
- [`b107_candidates.md`](b107_candidates.md): b107 candidates (worked out 2026-10-08, after b106 shipped)
- [`b109_candidates.md`](b109_candidates.md): Build 109: candidates (2026-10-09)
- [`b112_candidates.md`](b112_candidates.md): Build 112: candidates (2026-10-10)
- [`b113_candidates.md`](b113_candidates.md): Build 113: candidates (2026-10-10)
- [`b115_candidates.md`](b115_candidates.md): Build 115: candidates (2026-10-11, a draft for the next-steps talk)
- [`b116_candidates.md`](b116_candidates.md): Build 116: candidates (2026-10-11, a draft after b115's tests)
- [`b117_candidates.md`](b117_candidates.md): Build 117: candidates (2026-10-12; b116's PSP tests are done)
- [`b118_candidates.md`](b118_candidates.md): What 118 carries so far (branch `w118`)
- [`b41_sleep_design.md`](b41_sleep_design.md): B41: a sleep the game is ready for (design)
- [`b91_redo_seed.md`](b91_redo_seed.md): REDOSEED: each actor's thinking draws from a seed of its own
- [`code_review_b119_me.md`](code_review_b119_me.md): Code review: the ME foundation and upstream since the pin (w119, Oct 13, 2026)
- [`code_review_b89.md`](code_review_b89.md): Code review, 2026-10-06 (builds 88–89)
- [`discussion_2026-10-14.md`](discussion_2026-10-14.md): Discussion, 2026-10-14 afternoon: findings and decisions
- [`frames_in_flight.md`](frames_in_flight.md): Frames in flight: the display list built on the ME one frame behind (design, 2026-10-16)
- [`ge_audit.md`](ge_audit.md): The GE audit: how the port feeds the GE, and what is left to squeeze (work in progress)
- [`hud_sharpness.md`](hud_sharpness.md): HUD sharpness (2026-10-13, for build 120)
- [`library_hardware_b95.md`](library_hardware_b95.md): The Library on the PSP-1000: the BSP address bug (b95 → b96)
- [`node_matrix_readers.md`](node_matrix_readers.md): Node-matrix readers: a census for the node-matrix pass
- [`overnight_b102.md`](overnight_b102.md): Overnight run, 2026-10-07: build 102
- [`overnight_b108.md`](overnight_b108.md): Overnight 2026-10-09: memory waits, spare hardware, the last three maps
- [`recon_134.md`](recon_134.md): Recon 134: sizing the big levers before building them (2026-10-16)
- [`visual_pass_recon.md`](visual_pass_recon.md): Visual quality pass: preliminary recon (2026-10-04)

## Reviews of single changes

Each is the walk-through of one change or branch, written for Sheepdog01's review before he tests it.

- [`review_aicad.md`](review_aicad.md): Review: aicad2 and aicad3 (AI thinking at half rate; compromises, off by default)
- [`review_aifar.md`](review_aifar.md): Review: aifar (far actors out of the fight think 1 tick in 4) and RENDERTICK's count
- [`review_aimcache.md`](review_aimcache.md): AIMCACHE: the aim and look screens' deltas kept and applied again
- [`review_aimeaudiolook.md`](review_aimeaudiolook.md): aimeaudiolook: audio mixed at a long job's looks near its bound (B60)
- [`review_aimepathlook.md`](review_aimepathlook.md): Review: aimepathlook, the AI job looks while a path is built (B56, the ME's side)
- [`review_aimeringlook.md`](review_aimeringlook.md): Review: aimeringlook (B50, a late point-physics job rests the ring)
- [`review_angletint.md`](review_angletint.md): Review: angletint (see-through tints by the angle, Cortana)
- [`review_audiofx.md`](review_audiofx.md): Review: audiofx (audio plan step 2, docs/audio_fidelity.md)
- [`review_bakever.md`](review_bakever.md): Review: the bake check (go_live 3.4), branch `bakever`
- [`review_bootscreen.md`](review_bootscreen.md): Review: the boot screen and a visible load (branch `bootscreen`, on `w139`)
- [`review_boxposing.md`](review_boxposing.md): Review: the posing box cut in (`PSP_BOXPOSING=1`)
- [`review_bspr2.md`](review_bspr2.md): Review: BSPR v2, a biped's last collision answers kept in its own datum (`bspsame`, `bspsamecheck`)
- [`review_c10hot.md`](review_c10hot.md): Review: c10hot (the profile's hot threshold)
- [`review_canopy.md`](review_canopy.md): review_canopy: the Pelican canopy (glass tint)
- [`review_claimfill.md`](review_claimfill.md): CLAIMFILL: copies and fills that claim their destination lines
- [`review_clut4exact.md`](review_clut4exact.md): Review: clut4exact, the exact CLUT4 set
- [`review_clut4look.md`](review_clut4look.md): Review: clut4ship, the CLUT4 look set on by default
- [`review_controls.md`](review_controls.md): Review: the controls branch (for the next build)
- [`review_cutstream.md`](review_cutstream.md): Review: cutstream (no cut piece dropped; where the cutting goes)
- [`review_decalfade.md`](review_decalfade.md): Review: dynamic decals fade out after 30 s (branch `decalfade`, on w140 924682c)
- [`review_dithershift.md`](review_dithershift.md): Review: the dither moved each pass (branch `dithershift`, on `glowmaps`)
- [`review_drawpart.md`](review_drawpart.md): Review: DRAWPART slice 2 and LIGHTREUSE, the model walk in two stages
- [`review_fix-alpha.md`](review_fix-alpha.md): Review: fix-alpha (see-through and cut-out surfaces)
- [`review_fix-alpha3.md`](review_fix-alpha3.md): Review: fix-alpha3
- [`review_fix-cortana.md`](review_fix-cortana.md): Review: fix-cortana (H3's seam through Cortana)
- [`review_fix-frontend.md`](review_fix-frontend.md): Review: fix-frontend (b139's front-end findings)
- [`review_fix-grating.md`](review_fix-grating.md): Review: fix-grating (hand test 3 items 4 and 5: a10's grating, c10's hanging moss)
- [`review_fix-h42.md`](review_fix-h42.md): Review: fix-h42, the lighter wedge beside b30's bunker door frame
- [`review_fix-lights.md`](review_fix-lights.md): Review: fix-lights (b139's hand test: H6, H22, H9, H23, H33, H41, H42, H24)
- [`review_fix-listres.md`](review_fix-listres.md): Review: fix-listres (a10's decals and floor stencils vanish)
- [`review_fix-memory.md`](review_fix-memory.md): Review: fix-memory (b139's hand test: H4, H40, H12, H21, H28, H10, H32, H5)
- [`review_fix-models.md`](review_fix-models.md): Review: fix-models (b139's hand test: H1, H3, H17, H34, H35, H36, H39)
- [`review_fix-rcheck.md`](review_fix-rcheck.md): Review: fix-rcheck (a crash when the heap can't grow the immediate-draw buffer)
- [`review_fix-revert.md`](review_fix-revert.md): Review: fix-revert (sleep tables across a game state load)
- [`review_fix-sim.md`](review_fix-sim.md): Review: fix-sim (H31, H25/H26, H27)
- [`review_fix-tick140.md`](review_fix-tick140.md): Review: fix-tick140 (w140's tick rise on the openings)
- [`review_fix-tracers.md`](review_fix-tracers.md): Review: fix-tracers (b139's hand test: H28, tracers drawn as lines)
- [`review_flatround.md`](review_flatround.md): Review: flatround (finding 14, the olive bridge glass)
- [`review_frontdoor.md`](review_frontdoor.md): Review: the front door (branch `frontdoor`, on b135)
- [`review_gebusy2.md`](review_gebusy2.md): Review: `gebusy2`, the GE's busy time under M6 #2 (branch `gebusy2`, base w140 bc12443)
- [`review_geinflight.md`](review_geinflight.md): Review: geinflight onto ge-vram
- [`review_gelfaces.md`](review_gelfaces.md): Review: gelfaces (a light's gel by its six cube faces), for b140
- [`review_geofmt.md`](review_geofmt.md): Review: geofmt (the geometry pack's formats)
- [`review_geshow2.md`](review_geshow2.md): Review: the frame shown off the game thread (S10a; keep words `geshow2`, `geshow2copy`, `geshow2noco
- [`review_geshow3.md`](review_geshow3.md): Review: the next frame replayed into a second display list (S10a's variant; keep words `geshow3`, `g
- [`review_glowmaps.md`](review_glowmaps.md): Review: the self-illumination glow maps and the main menu showcase (branch `glowmaps`)
- [`review_levelvram.md`](review_levelvram.md): Review: the level's streams in vrammodels' VRAM room (branch `levelvram`, on `glowmaps`)
- [`review_levers126.md`](review_levers126.md): Review: the research levers, first batch (`rv2`, `rv1`, `rv4`, `restore126`)
- [`review_lfs.md`](review_lfs.md): Review: the level's lens-flare markers, axes worked out once (branch `lfs`, keep words `lfsonceoff`,
- [`review_lightfirst.md`](review_lightfirst.md): Review: LIGHTFIRST, the objects' lighting before the model walk
- [`review_lightringlook.md`](review_lightringlook.md): Review: lightringlook (B50, the light pass holds the ME past a ring job's deadline)
- [`review_lk22.md`](review_lk22.md): Review: LK22, the level bouncing against the models (branch `lk22`)
- [`review_lk22b.md`](review_lk22b.md): Review: LK22b, the level given from an origin near the eye (branch `lk22b`)
- [`review_loadingscreen.md`](review_loadingscreen.md): Review: the loading screen's picture (branch `loadingscreen`, on `w137`)
- [`review_lodpsp.md`](review_lodpsp.md): Review: models' level of detail picked nearer, on by default (keep word `lodpspoff`; a compromise)
- [`review_lookb124.md`](review_lookb124.md): Review: Sheepdog01's b122 look reports, on w124 (branch `lookb124`)
- [`review_lookfix.md`](review_lookfix.md): Review: the stick turns the view by real time (`lookreal`)
- [`review_m6default.md`](review_m6default.md): Review: M6 #2 on by default in the campaign (branch `merge-w140-m6default`)
- [`review_me-foundations.md`](review_me-foundations.md): Review: me-foundations (the Media Engine programme, stages 0 and 1)
- [`review_me-jobs.md`](review_me-jobs.md): Review: me-jobs (ME programme, job 1: `mepoints`)
- [`review_me-stall.md`](review_me-stall.md): Review: me-stall (why a batch record looked held for 58 ms)
- [`review_mealloc.md`](review_mealloc.md): Two writes off the Media Engine: the batch spheres and the glyph sheet (mealloc)
- [`review_menus.md`](review_menus.md): Review: the menus as the PSP's (for b139)
- [`review_mepointsclaim.md`](review_mepointsclaim.md): Review: mepointsclaim, a points job the ME hasn't begun is not a late job (B56)
- [`review_mestop.md`](review_mestop.md): Review: aimeobslook and sndtierback (B54, B55: the ME stopped on d20, and the sound tier silent afte
- [`review_metrics.md`](review_metrics.md): Review: branch `metrics` (the measurement round's instruments)
- [`review_mewake.md`](review_mewake.md): Review: the Media Engine started again after a sleep (`mewake`)
- [`review_mewake2.md`](review_mewake2.md): Review: the Media Engine after a sleep, second try (`mewake2`)
- [`review_nomouth.md`](review_nomouth.md): Review: `nomouth`, a test keep word for exactness runs
- [`review_obsray.md`](review_obsray.md): obsrayreuse: two repeated rays of the obstacle-avoidance pill test reused (OBSRAY)
- [`review_ocean.md`](review_ocean.md): Review: ocean (the planar fog seen from above)
- [`review_p2pass.md`](review_p2pass.md): Review: P2NC, tick pass P2 (keep word `p2pass`, on by default; `p2passoff` turns it off)
- [`review_perf-cache.md`](review_perf-cache.md): Review: perf-cache (the CPU caches: prefetch, data layout, loop phasing)
- [`review_perf-exact.md`](review_perf-exact.md): Review: perf-exact (exact speed-ups, after w140)
- [`review_perf-ge.md`](review_perf-ge.md): Review: perf-ge (the GE feed: two exact speed-ups, and bindblocks brought over)
- [`review_perf-levers.md`](review_perf-levers.md): Review: perf-levers (pass 2, off w140)
- [`review_perf-memory.md`](review_perf-memory.md): Review: the memory census (branch `perf-memory`, M7)
- [`review_posefpu.md`](review_posefpu.md): Review: posefpu, a pose made ahead takes one form whoever makes it
- [`review_profwait.md`](review_profwait.md): Review: profwait (a profile write waits for the last one by sleeping)
- [`review_propcrash.md`](review_propcrash.md): Review: a found body's shout taken up only while its prop lives (FIXES_PROPCRASH)
- [`review_prs.md`](review_prs.md): Review: PRS, perception passes over objects it only stamps
- [`review_psc.md`](review_psc.md): Review: PSC, a looping animation's poses kept and made once a map
- [`review_psp_metrics.md`](review_psp_metrics.md): Review: `PSP_METRICS`, each map's figures in the log
- [`review_relief.md`](review_relief.md): Review: the baked bump relief (`relief`)
- [`review_ringdetail.md`](review_ringdetail.md): Review: the menu ring for b139 (`ringdetail`)
- [`review_ringfix.md`](review_ringfix.md): Review: a late ring job rests the ring instead of turning it off (keep words `ringgiveup`, `ringrest
- [`review_rso.md`](review_rso.md): Review: RSO, a still object's records sent again (branch `rso`, keep words `rso` `rsoall` `rsocheck`
- [`review_smemooff.md`](review_smemooff.md): Review: the sphere memo off by default (SMEMO; keep words `spherememo`, `spherememocheck`, `spherewa
- [`review_streamoom.md`](review_streamoom.md): Review: a level material with no room for its copy (branch `streamoom`)
- [`review_sv.md`](review_sv.md): Review: heaproom256 (heap from the volatile block)
- [`review_texavg.md`](review_texavg.md): Review: texavg, a missing texture drawn as its 4x4 stand-in (branch `master/texavg`, on w142 a735763
- [`review_timercalls.md`](review_timercalls.md): Review: the timer calls (research/timercalls, 2026-11-02)
- [`review_tock158.md`](review_tock158.md): Review: tock158
- [`review_tock159.md`](review_tock159.md): Review: tock159
- [`review_tock160.md`](review_tock160.md): Review: tock160
- [`review_tock161.md`](review_tock161.md): Review: tock161
- `review_sndpart.md`: sndpart on by default, the sound tier's 880 KB window in the ME partition under halo_claim.prx's claim (2026-11-17)
- [`review_trim125.md`](review_trim125.md): Review: the dead-weight pass (branch `trim125`)
- [`review_txpt.md`](review_txpt.md): Review: texture pre-touch at record time (branch `txpt`, keep word `texpretouch`)
- [`review_vehprobe.md`](review_vehprobe.md): Review: UVB, one sphere about a moving vehicle before its mass points' queries
- [`review_vislazy.md`](review_vislazy.md): VISLAZY: undrawn objects' node matrices made only when read
- [`review_vmegame.md`](review_vmegame.md): Review: vmetest, the VME in a game build (recon/vmegame)
- [`review_vrbroom.md`](review_vrbroom.md): Review: `blipdepth`, the motion sensor's target in the depth buffer (VRB2)

## Build by build

History: each build's notes (`build_`), its review walk-through (`review_`), its hardware report (`hardware_`) and its release notes, side by side. [`builds.md`](builds.md) lists every build with the commit it was made from.

| Build | Notes | Review | Hardware | Release notes |
|---|---|---|---|---|
| b103 | [build](build_b103.md) |  |  |  |
| b105 | [build](build_b105.md) |  |  |  |
| b106 | [build](build_b106.md) |  | [hardware](hardware_b106.md) |  |
| b107 | [build](build_b107.md) |  | [hardware](hardware_b107.md) |  |
| b108 |  |  | [hardware](hardware_b108.md) |  |
| b111 | [build](build_b111.md) |  | [hardware](hardware_b111.md) |  |
| b112 |  |  | [hardware](hardware_b112.md) |  |
| b113 | [build](build_b113.md) |  | [hardware](hardware_b113.md) |  |
| b114 | [build](build_b114.md) |  | [hardware](hardware_b114.md) |  |
| b115 | [build](build_b115.md) |  | [hardware](hardware_b115.md) |  |
| b115.1 | [build](build_b115.1.md) |  |  |  |
| b116 | [build](build_b116.md) |  | [hardware](hardware_b116.md) |  |
| b117 | [build](build_b117.md) |  | [hardware](hardware_b117.md) |  |
| b118 | [build](build_b118.md) |  | [hardware](hardware_b118.md) |  |
| b119 | [build](build_b119.md) |  | [hardware](hardware_b119.md) |  |
| b120 | [build](build_b120.md) | [review](review_b120.md) | [hardware](hardware_b120.md) |  |
| b121 | [build](build_b121.md) | [review](review_b121.md) |  |  |
| b121_aiperc |  | [review](review_b121_aiperc.md) |  |  |
| b121_bipeds |  | [review](review_b121_bipeds.md) |  |  |
| b121_machines |  | [review](review_b121_machines.md) |  |  |
| b121_me |  | [review](review_b121_me.md) |  |  |
| b121_tints |  | [review](review_b121_tints.md) |  |  |
| b122 | [build](build_b122.md) | [review](review_b122.md) |  |  |
| b122_aihalf |  | [review](review_b122_aihalf.md) |  |  |
| b122_farrate |  | [review](review_b122_farrate.md) |  |  |
| b122_look |  | [review](review_b122_look.md) |  |  |
| b122_merays |  | [review](review_b122_merays.md) |  |  |
| b122_posing |  | [review](review_b122_posing.md) |  |  |
| b122_vehcam |  | [review](review_b122_vehcam.md) |  |  |
| b123 | [build](build_b123.md) | [review](review_b123.md) |  |  |
| b123_flags |  | [review](review_b123_flags.md) |  |  |
| b123_lititems |  | [review](review_b123_lititems.md) |  |  |
| b123_mehyg |  | [review](review_b123_mehyg.md) |  |  |
| b124 | [build](build_b124.md) | [review](review_b124.md) |  |  |
| b125 | [build](build_b125.md) | [review](review_b125.md) |  |  |
| b126 | [build](build_b126.md) | [review](review_b126.md) |  |  |
| b127 | [build](build_b127.md) | [review](review_b127.md) |  |  |
| b129 | [build](build_b129.md) | [review](review_b129.md) |  |  |
| b131 | [build](build_b131.md) | [review](review_b131.md) |  |  |
| b132 | [build](build_b132.md) | [review](review_b132.md) |  |  |
| b133 | [build](build_b133.md) | [review](review_b133.md) |  |  |
| b134 | [build](build_b134.md) | [review](review_b134.md) |  |  |
| b135 | [build](build_b135.md) | [review](review_b135.md) |  |  |
| b136 | [build](build_b136.md) | [review](review_b136.md) |  |  |
| b137 | [build](build_b137.md) | [review](review_b137.md) |  |  |
| b138 | [build](build_b138.md) | [review](review_b138.md) |  |  |
| b139 |  | [review](review_b139.md) |  | [notes](release_notes_b139.md) |
| b140 | [build](build_b140.md) | [review](review_b140.md) |  |  |
| b140h1 |  | [review](review_b140h1.md) |  |  |
| b140h2 |  | [review](review_b140h2.md) |  |  |
| b140h3 |  | [review](review_b140h3.md) |  |  |
| b141 | [build](build_b141.md) | [review](review_b141.md) |  |  |
| b143 | [build](build_b143.md) | [review](review_b143.md) |  | [notes](release_notes_b143.md) |
| b147h |  |  |  | [notes](release_notes_b147h.md) |
| b148 |  | [review](review_b148.md) |  | [notes](release_notes_b148.md) |
| b149 |  | [review](review_b149.md) |  | [notes](release_notes_b149.md) |
| b150 |  | [review](review_b150.md) |  | [notes](release_notes_b150.md) |
| b158 |  | [review](review_b158.md) |  |  |

## Subfolders

- [`research/`](research/): the sources behind the levers: other projects' write-ups, read and cited (`research_vita.md` records the Vita port's grant).
- [`for_other_ports/`](for_other_ports/): what this port learned that other PSP ports can use.
