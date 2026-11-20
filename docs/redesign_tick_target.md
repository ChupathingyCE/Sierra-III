# The tick at the target: ceilings and the gap

The tick-at-target brief (the lab's `projects/sierra/coord/handoff/tick_hw_brief_2026-11-06.md`; Sheepdog01 11:33, 11:51, 12:01-12:09): at 30 fps, each level's own average and after R, the heavy stretches' ticks must lose 50-60% (`redesign_plan.md` §1b). This page sums every ceiling per heavy stretch against that cut, by exhaustion, so the gap left is where compromises enter, stretch by stretch, for Sheepdog01 to decide. Owner: the bottleneck session. Started 2026-11-06.

Labels: [M] measured on a PSP, [E] emulator or cache model, [D] read in code or data, [U] inference.

*Brought onto the tock 2026-11-09 from `docs/cost-map` (5f1eb7d), whose history doesn't join the tock's: it was missing here, and so from the plan sessions read first. The tick side's live work is the tick30 session's (Sheepdog01 2026-11-09 15:37-15:55; the lab's `projects/sierra/coord/handoff/tick30_2026-11-09.md`).*

## 1. What a heavy tick is made of

**b30's flight, one tick (42.4 ms, b151's bare bench)**:

| Part | ms | Source |
|---|---:|---|
| Working cycles | ~15.3 | 36% not waiting (`tick_mem_pct` 64) [M] |
| Instruction-miss stalls | ~10 | the miss mix [U]: ~51% of the base misses by cost (b30's ladder: I 3,749 / D 2,702 / writebacks 895 per million cycles, ~57-73 cycles a miss; `cpu_cost_map.md` §1-§2). The flight's own split isn't counted |
| Data-miss and writeback stalls | ~9.6 | ~49% of them [U] |
| The GE's toll on those misses | ~7.5 | gereadskind [M]: vertices 2.1, lists 2.2, indices 1.9, textures 1.4 a tick |

What it says:
- **The working cycles alone (~15.3) are about the whole target (16.6).** Memory work can't reach the cut alone; the tick must also do less work.
- **The toll is measured at 15 fps.** At the target the GE's work on the flight is ~1,800 ms a second, so it reads RAM all the time and the toll per tick likely rises [U]. Getting the GE off RAM (A1, A2) is worth more at the target than at 15 fps.
- **A realistic path stacks three things:** the toll removed; misses cut deeply (halved is already large); and the work cut by something like half. The last is where Sheepdog01's compromises with little visible cost come in (§3).

**By system, on the PSP** (corrected 2026-11-09; the lab's findings row `tick_by_system_b30_flight_correction`, ledger `cpu/tick/*` on b30's flight):
- **By function** (the PC sampler, PSP-3000, sierra-hw 42114568213, b151's code) [M]: collision and BSP 25.6%, AI on the main CPU 16.5 (its rays count as collision; the thinking runs on the ME), node matrices and animation 11.3, units' own code 9.3, other objects 7.7, libm and memcpy 5.3, effects 3.4, tags 2.9, audio 2.4, scripts 1.6, the rest 14.5.
- **By object kind** (objcost, PSP-1000, sierra-hw 42141971439) [M]: object updates 34.9 ms a tick as instrumented, bipeds 24.3 of it; the moving bipeds 23.6 (~24 a tick at ~970 µs each). **Corrected 2026-11-16 (audit, after Qwen4 Max's read):** these ms come from a census build whose tick was ~47-60 ms, not the shipped 39.9 ms; as shares of that tick (object updates 58-74%), bipeds are ~16-21 ms of the shipped tick [U], so a pot sized as a share of 24.3 ms runs 15-70% high.
- **Bipeds by band** (S4's census, PSP-1000, sierra-hw 42672087890) [M]: far and out of the fight 30%, in the fight at 20-40 units 36%, at 10-20 units 19%.

The split this line used to give (AI 26%, posing 33%, collision 14%) was a PSP total × the emulator's step split [M × E]. Under the 2026-11-09 rule it doesn't rank: the PSP puts collision first and posing at a third of that figure. **What it changes:** "the AI thinking less often" frees mostly the ME, which is also over at the target (§2a), not the tick. The tick's ground is the moving bipeds' updates: their physics, the collision they call, and their posing.

## 2. What doesn't help at the target

- **Work run once a frame instead of every tick** (FX15, RENDERTICK): at 30 fps a frame is one tick, so they're worth nothing there. Only a lower fixed rate with interpolation is.
- **Placements onto the ME on the heavy stretches:** the ME is over at the target too (b30's flight ~1,650 ms a second, §1b).
- **The GE is over at the target** on b30's flight (1,796), the landing (1,688) and b40's cliff (1,074): no tick work fixes that. It needs fewer vertices; it's §4's second column.

### 2a. The ME at the target, rebuilt from its parts (2026-11-06 19:50) [U on M]

§1b scales the whole replay (×0.57 after R, ×30 frames), audio and ring included, so it reads ~1,650 ms a second on b30's flight. Rebuilt from the replay's own split instead (C4, `c4_replay_by_kind_b30_flight` [M, the ME's cycle counter]: 67.9 ms a frame = model parts 37.8 and their begin/shape/passes ~3.3, audio and ring served 12.6, the level 6.8, see-through 3.65, screen quads, shadows and decals 2.8):

| Piece | Today, ms a frame | After R, ms a frame | At 30 fps, ms a second |
|---|---:|---:|---:|
| Model parts (R's steps 39.2 → 8-10, `redesign_r.md`) | ~41 | ~9-11 | ~270-330 |
| The level | 6.8 | 6.8 (R's slice 6 unverified) | ~205 |
| See-through, screen quads, shadows, decals | 6.5 | 6.5 | ~195 |
| Audio (real time) and the ring served | — | — | ~120-220 (the bench's frame rate to real time) |
| The AI pass and ring a tick (30 × (19.5 + 4.1)/2) | — | — | ~355 |
| **Total** | | | **~1,145-1,305** |

- **So the ME after R at 30 fps is ~1,150-1,300 ms a second on the flight, not ~1,650:** still over, by ~150-300.
- **What brings it under:** the stand-in and the pass trims draw fewer parts (each part after R ~30-40 µs; the sprites replace ~100 tiny parts a frame, ~-90 ms a second [U]); MECODE (~-30 to -50); PCMBAKE (small on the flight). That's ~1,000-1,150: about level, **so little ME room for moving tick work onto it on the flight** (S3, S4's ME form). The beach, the snowfield and d20's walk keep their room.
- **This is the number to measure, not estimate:** R's per-part ME cost comes from its milestone pair on the PSP (the emulator is blind to the ME's time); the level's (6.8) from R's slice 6.

## 3. The ceilings, by lever

### 3.1 Animation LOD (posing drawn far bipeds less often)

**animlod1** (recon, sierra-batch 42281022915 [E], shares only; row `animlod1_posing_by_lod`):
- Bipeds are 69-75% of posing. Drawn bipeds under 16 pixels are **44.7% of b30's flight's posing** (37.2% on the landing, 2.7% on b40); 16-32 pixels 5.0%, 2.0%, 5.5%. Undrawn bipeds 11.9%, 23.5%, **49.2% on b40**.
- A biped's pose: decode 6-11%, the overlays, preprocess and interpolation 52-64%, the walk and matrices 27-37%; ~3.4-4.4 µs a node.
- Blending a node matrix costs 0.44-0.57 µs (lerp) or ~1.0 µs with re-orthonormalising, against 3.4-4.4 to pose.

The forms and their ceilings on b30's flight (posing ~33% of the tick, ~14 ms [M × E]):

| Form | What it does | Covers | Saves of posing | ms a tick [U] |
|---|---|---:|---:|---:|
| A | Drawn bipeds under 32 px posed every other tick, the matrices blended (lerp + re-orthonormalise) | 49.7% | ~18-21% | ~2.5-3.0 |
| B | Tiered: under 16 px every 4th tick, lerp only (orthonormality errors don't show at that size); 16-32 px every other tick | 49.7% | ~30% | ~4.2 |
| C | Under 16 px: the overlays and preprocess (52-64% of a pose) cheapened or left out | 44.7% | up to ~25-30% | ~3.5-4 |

The landing scales about as the flight; **b40 is different:** its posing is in undrawn bipeds the AI reads markers from, so the form there is posing only the chains to the nodes the AI reads (UNSEENPOSE's `revive_if`: a root-only decode).

All forms are "plays the same": hit tests on those bipeds use the blended pose. Each needs an outcome census and Sheepdog01's side-by-sides.

### 3.2 LOD by the PSP's pixel size (the GE's vertices)

**lodcensus1** (recon, sierra-batch 42282211840 [E]; row `lodcensus1_model_vertices`): b30's flight sends **~55k model vertices a frame, 66% of them in models with no lower LOD level.** **Draws under 16 px carry 47% of all model vertices**, and 95% of those have no lower level: marines, grunts, elites and the leafy trees ship one level only. Dropping a level where one exists saves only ~15% of model vertices on the flight (~10% landing, ~9% b40), mostly the Pelican near the camera. Top senders: the Pelican ~10k, the leafy trees ~7k together, marine_armored ~5.7k, grunt ~5.2k, elite ~3k. The GE's time on b30 is models 32.0 of 41.6 ms a frame, the level 6.5 (framesplit [M]).

**So the tags' own levels are spent** (why lodpsp gave -0.7%): far vertices only shrink if a lower form is *made*. The tiny-object stand-in (§3.5) is that form for bipeds and trees under 16 px: if the GE's model time follows its vertices (it's ~89% vertex-bound, C6), it's ~40-45% of the models' GE time on the flight, ~13-14 ms a frame of 41.6 [U], the flight's GE at the target from ~1,800 to ~1,200 ms a second. SSPR (R's slice 9, simplified levels made at bake) is the form for 16 px and up.

### 3.3 Opaque fog

**fogcensus1** (recon, sierra-batch 42283421832 [E], every campaign map; row `fogcensus1_maps`): **opaque fog (density 1.0) appears only on c10, in 86% of frames** (22-120 units; the engine already pulls the far plane in to the fog distance in render_window, down to 22), on c20 in 6% and in a10's indoors in 4%. Everywhere else it stays below 1.0: b30 0.10-0.50, a30 0.50, a50 0.10-0.50, d40 0.60-0.90, and **near-opaque on d20 (0.90-0.95), b40 (0.50-0.95) and c40 (to 0.95)**; the far plane is 1,024 there.
- **c10: already taken.** The CPU culls use the pulled-in far plane (recon, read in code [D]): objects and subclusters are tested against all six planes, and the portal walk stops past it, except when a sky model is visible (a small gap, not counted).
- **d20, b40, c40: nothing there for models.** 0 model draws a frame land past the fog's far distance (b40 6 vertices of 7,618; recon's fogpast, sierra-batch 42286493206 and 42287307168 [E]); the level past the fog comes with geocensus1's distance bands.

### 3.4 Second passes; effects and particles (for Sheepdog01's understanding first, Sheepdog01 12:09)

**passcensus1** (recon, sierra-batch 42291893015, shots 42291904672 [E]; row `passcensus1_model_passes`). Each extra pass sends the part's geometry again.

| Pass | What it draws | b30 model vertices | c10 | d20 | Shots without it (pixels off by > 8/255) |
|---|---|---:|---:|---:|---|
| Base | the texture, lit | 46% | 57% | 47% | — |
| Change colour | the armour, cloth or foliage tint through the multipurpose mask | **23%** | 2% | 21% | b30 0.9-1.0% (tree leaves, the far marines) |
| Glow | self-illumination (lights, displays, Covenant glow) | **19%** | 22% | 12% | b30 0.02% |
| Two-sided back | the back faces of two-sided parts (foliage, cloth) | 11% | 15% | 5% | b30 0.3-0.4% (foliage) |
| Sheen, detail | specular sheen, detail maps | under 1% | ~5% | 16% | 14-49 px: the noise floor |
| Reflection | cube-map reflection | none drawn on b30 | | | 0 |

- **Under 16 px, the change and glow passes alone are 29% of b30's model vertices for 2.4% of the models' pixels** (~8 vertices a pixel). Models under 16 px are 57% of model vertices for 4.9% of model pixels.
- **What it suggests** [U, for Sheepdog01's eye]: the extra passes on small or far models cost far more than they show. Ways, cheapest first:
  - **leave glow (and sheen, detail) out below a size:** at b30's noise floor in the shots, 19% of model vertices;
  - **fold the change colour into the base for small models:** a composite texture made at bake per colour permutation (marines' armour colours are few), drawn in one pass, so the tint stays and the second geometry pass goes;
  - **leave the two-sided back out below a size** (foliage, 0.3%);
  - and under the stand-in's threshold (§3.5) the question goes away: a sprite has one pass.
- c10's weight is at 32-64 px (39% of its vertices); d20's first person is 33% of its model vertices.
- **Measured (the master's passes1/passes2, b30's flight [E]; rows `passes2_vertices`, `sspr3_figure`):** GE vertices a frame left out, of the models' ~212k with all passes:

  | Left out | Under 16 px | Under 32 px |
  |---|---:|---:|
  | Glow, sheen, detail | 16,059 | 20,610 |
  | Two-sided backs | 3,233 | 5,966 |
  | Change colour | 18,978 | 25,066 |
  | **All three** | **38,270 (~18%)** | **51,642 (~24%)** |

  Play identical on every side; pictures 0-389 pixels a shot at 32 px. For scale, SSPR step 1 (rigid only) at 1 px leaves out 3,494: the bipeds' step 2 and the passes are where the vertices are. At ~89% vertex-bound, all three at 32 px is ~21% of the models' GE time, **~-290 ms a second at the target on the flight** [U on E].

**fxcensus1** (recon, sierra-batch 42292411030 [E], emulator µs, shares only; row `fxcensus1_effect_systems`):
- **d20 is the heavy one:** effects_update ~1,060 µs a tick for 175 live effects, mostly cheap level emitters called 30-40 times a tick (coolant bubble bursts 23%, holo controls 13%, sparks, the holo sphere) and the burning flame (29%, 166 µs a call); particles 2,437 µs a frame (200 live, 119 physics calls a frame, ~12k level hits over the run). c20: effects 245, particles 1,214. b30: effects 83 (the plasma grenade's armed effect 75%), particles 886.
- **Drawn particles by size:** under 4 px are **48% on d20** (28% under 2), 43% on b30; c20 is spread evenly.
- No particle systems or weather drawn on these stretches.
- **The two expensive single effects** (recon, fxcensus2-4 [E]; row `fxcensus_flame_grenade`):
  - **The armed plasma grenade (404 µs an update): a plain fix.** It asks for 32 particles an update, and 67% are then thinned or refused by the port's own particle budget, but only *after* each pays a BSP location lookup and a visibility test the budget never reads. Deciding the budget first (in `psp_particle_admit`) is ~60% of the grenade, ~40% of b30's effects_update; the same look (the port's own budget), not bit-exact (the thinning counter also counts unseen asks). Speed work that looks the same: built as PADMIT, on by default in the tock (effects_update -23% b30, -14% d20 [E]).
  - **d20's burning flame (164 µs an update): a speed win that looks the same** (Sheepdog01 18:33). ~85% is a scorch decal placed every event (a ray, then projection and clipping onto the BSP, ~500-610 µs each, 1.5 a tick). The lever: no new decal from a looping effect while its last one is live within its radius. The scorch saturates anyway, but the stacked darkening would change. Ceiling ~25% of d20's effects_update. **FLAMEDECAL, built behind its word, on by default once d20's side-by-sides show the same look** (Sheepdog01 18:33).
- **What it suggests** [U, for Sheepdog01's eye]: the decorative level emitters (bubbles, holo flicker, sparks) at a lower fixed rate with interpolation, or updated only when on screen; particles under ~2 px faded out early (they're near-invisible); the expensive single effects (burning flame, the armed plasma grenade) read for why one call costs 166-295 µs.

### 3.5 The tiny-object stand-in (Sheepdog01 13:44-13:46)

At under ~16 px a biped or tree is a handful of pixels. Two tiers:
- **Under ~16 px: a sprite.** Each character (and tree) rendered at bake into a small sheet, ~8 directions × 8 animation phases at ~24 px, 4-bit with a palette per armour colour or rank (~18 KB a character, ~100-150 KB the cast, in the RAM texture cache: VRAM's texture share is full). One quad a biped: no posing beyond the root and the marker chains the AI reads, almost no GE vertices.
- **~16-32 px: a baked pose library.** A few dozen poses a character (walk and run cycles at ~8 phases, idle, crouch, fire, dying; ~10-13 KB a character); a biped snaps to the nearest and skips overlays and blending (52-64% of a pose).

**tinybip1** (recon, sierra-batch 42284716914 [E]; row `tinybip1_tiny_bipeds`; drawn bipeds under 32 px on b30's flight and landing):
- 20 a frame on the flight, 16 on the landing; grunts 44-56%, then elites, marines, jackals; 14-23 distinct characters × permutations per 300 ticks.
- **What reads them:** markers ~4-6 reads a draw, nearly all **head and body** (the AI perceiving and aiming at them), the hand 3-8%; node matrices ~7-10 a draw (their own compute and attachments, the draw, and a few % each from effect_update, impulse sounds, the autoaim pill, the collision instance).
- **Hits:** damage reaches a tiny biped twice per 100 tiny draws, and **most hits on bipeds land on tiny ones** (258 of 351 on the flight). A tiny biped is almost never big the next frame (0 flight, 5 landing).
- **Animations:** a handful of base idles and moves, but **aim and look overlays are on in 85-100% of tiny draws**, and a blend runs in 9-12%.

**What that means for the design:** the simulation reads a tiny biped's skeleton every tick (markers, hit regions), so the skeleton the simulation reads must stay true; what can change is how the pose is *drawn*. The form that follows:
- **Pose only the chains the simulation reads** (root, pelvis, spine, head; the weapon hand when it fires), with the same maths and overlays, so markers and the hit regions on those nodes are exact; the other nodes are made on demand if anything reads them (VISLAZY's rule at node grain).
- **Draw the biped as a sprite** under ~16 px, which needs none of the other nodes. A pose library for 16-32 px would have to carry the aim and look overlays, so it's weaker there than first thought.
- If the simulation reads no other node of a tiny biped, this is **exact for play**, and only the picture changes.
- **tinynodes1** (recon, sierra-batch 42286250804 [E]; row `tinynodes1_read_set`): the read set is small: the head and body (spine1) markers on all, plus marines' left hand; single-node reads grunt 8 of 17 nodes, elite 11 of 26, marine 9 of 20, jackal 2 of 27 (the attachment compute and the damage effects, on exactly the nodes hits land on). **The catch: `collision_model_instance_new` reads the whole node array** (357 times on the flight), so either the chains include every node the collision model binds, or the rest are made on demand when it asks (VISLAZY's rule at node grain, exact if made from the same state). **collnodes1** (recon, sierra-batch 42288867640 [E]; row `collnodes1_collision_node_sets`): the collision models bind most of the skeleton (live collision nodes: grunt 12 of 17, marine 16 of 20, elite 19 of 26, jackal 21 of 27), so **the rest are made on demand**: `collision_model_instance_new` reaches a tiny biped only 0.46% of its calls (691 of 149,698 on b30, ~0.2 a tick; 8 on b40), almost all from `object_test_vector`, some before the biped's own update in the tick and some after.
- **Why on demand is exact:** a read before the biped's update computes from the state its last update left, which is what the eager pose computed then; a read after computes from the state this update left. It holds while nothing outside the biped's own update changes its animation state between update and read (damage's impulses are the case to check, as `vislazycheck` did for VISLAZY).
- **Cost:** ~0.2 full poses a tick on the flight: nothing.

Ceilings [U]: if the read chains are ~8-12 of a biped's ~15-22 nodes and a pose's cost follows its nodes, the tick saves ~40-50% of the tiny bipeds' posing, ~2.5-3.5 ms a tick on the flight; the GE ~13-14 ms a frame (§3.2). The sheets are made from the game's models on Sheepdog01's machine and stay local, as every bake.

### 3.5b Every kind of geometry against the PSP's pixels

**geocensus1** (recon, sierra-batch 42289844984 counts, 42290762636 pixels [E]; row `geocensus1_geometry_vs_pixels`; pixels are textured pixels from every pass, overdraw included):

| Kind | b30 | c10 | b40 | d20 | Verdict |
|---|---|---|---|---|---|
| World models, vertices a pixel | **1.37** (121,658 vertices a frame, 74% of the GE's, 11% of pixels) | 0.70 (70%) | 0.46 | 0.37 | **the over-detail** (§3.6) |
| First person | ~0.5 a pixel, 11-17k vertices a frame on every map (22-34k px) | | 28% of the vertices | 21% | a candidate, always on screen: Sheepdog01's eye |
| The level | under 0.1 in every distance band (b30 past 100 units 0.24, 3.4k vertices) | | | | **not a target**: a far level triangle already covers ~17-46 px |
| See-through | 20-31% of pixels for ~2-3k vertices | | | | pixel-bound, not vertex |
| HUD | ~70k px for 150 vertices | | | | nothing to cut |

- The level's render geometry and collision BSP are separate [D], so a simplified render mesh wouldn't touch play; but with under 0.1 vertices a pixel there's little to gain, and 41-52% of a material's positions carry more than one lightmap UV (seams decimation would have to keep).
- **So the geometry work is models first, then a careful look at first person** (the weapon and arms, 11-17k vertices a frame on every map, ~0.5 a pixel: decimated at a small *e*, if Sheepdog01's eye allows).

### 3.6 Levels of detail from a pixel-error budget (Sheepdog01 14:03: how much can we give up without visible loss)

The tags' cutoffs are per model, set by Bungie's artists against 480 lines (the port scales them by 272/480; lodpsp a step further), and most models drawn small have one level only (§3.2). So instead of retuning them:
- **At bake, each model is decimated into levels**, each with its largest shape error in model units (a quadric error metric with silhouette edges weighted and bone weights kept valid for skinned models).
- **Each level's switch distance follows from arithmetic:** it's drawn once that error projects under *e* pixels at the PSP's field of view and 272 lines. The cutoffs stop being taste.
- **One dial, *e*, sets what we give up:** A/B bakes at e = 0.5, 1 and 2 pixels, side-by-side on b30's flight and landing, b40 and a cinematic close-up (Sheepdog01's eye decides), with the GE's vertices counted in the same runs. Possibly a different *e* by kind (bipeds sharper than rocks and trees).
- **Below the sprite threshold** the stand-in (§3.5) takes over.
- **Close-ups:** a smoothed level made at bake (subdivision with the model's normals) for models filling the screen (cinematics, the main menu, a body at your feet), where few models are drawn. The GE's own Bezier patches are a research angle, not a plan.

This is SSPR (R's slice 9) brought forward as its own path: a bake step and the cutoffs it writes; the runtime's LOD pick stays the engine's. **The master builds it** (2026-11-06 14:12): new levels written as the pack's existing PCV blocks (R and the geometry cache take them as any part); step 1 rigid models (meshoptimizer's simplifier, MIT, credited), the three bakes and the vertex census; step 2 skinned, after Sheepdog01's look. Foliage (alpha-tested leaf cards) likely needs card removal rather than edge collapse, with its own *e*.

**What it could buy** [U, the decimation ratios guessed until the bakes]: at e ≈ 1 px, kept vertices ~15% under 16 px, ~30% at 16-32, ~50% at 32-64, ~75% at 64-256, ~100% above (the near Pelican): **~55% of the flight's model vertices**. Models are ~77% of the GE's time and the GE is ~89% vertex-bound, so ~45-50% of the models' GE time, **~20 ms a frame of the flight's ~60**, the GE at the target from ~1,800 to ~1,100-1,200 ms a second; the landing (58% of its vertices under 16 px) around or under 1,000. Smaller meshes also fit the model copies' VRAM, cutting the tick's toll.

### 3.7 The VME (Sheepdog01 16:07: any way it buys time back)

**Recon's placement survey** (2026-11-06 16:10, read only, from A2, the Part A bench and frame_ledger): **none of today's ME jobs gives the VME more work than it costs the ME to feed it.** The yardstick: feeding costs ~7k ME cycles a voice on A2's path, ~1.8k in the bench's resident pass. So a job pays only where its arithmetic is well over ~2-4 ME cycles a word fed, streams contiguously, fits 24-bit fixed point and doesn't branch per element.
- **The replay** (b30 69 ms a frame): state, batching, list building, near tests: branchy or copy-bound; bone matrices ~1.5 MACs a word. Not a fit; R is the plan for it.
- **Audio** (~15): the VME's share is the multiply-accumulate, ~2k of a voice's 43k cycles: nets under 1 ms of the ME a second.
- **The AI pass, the ring jobs** (poses ahead, perception rays, particle points): pointer chasing, BSP walks, small batches.
- **Where it could still pay, if such work lands on the ME:** VMETEX (bulk texture repacking, ~0 ME beside it [M]) and VMEDMACOPY's bulk copies.
- **A CPU-side lever it turned up, PCMBAKE** (recon's pcmcensus1, sierra-batch 42304370871 [E × M]; row `pcmbake_sizing`): the most-decoded sound permutations baked to PCM per map (chosen by decodes per KB, music left out), read from the eDRAM in place of decoding. Exact if the bake uses the game's own decoder. b30: 544 KB covers 34.6% of decodes, 1,200 KB 47.5%; d20's swamp 14.1% / 22.2% (its heard set is wide). **The ME it frees: b30's beach ~2.0 ms a frame at 544 KB, ~2.8 at 1,200; d20's walk ~0.6 / ~1.0** [U on M].
  - **It competes for the eDRAM's free 544 KB** with MECODE (120-230 KB for ~1-1.6 ms an AI pass), R's ME state and MEVGATHER (`redesign_b.md` §3.2). MECODE buys more per KB, so PCMBAKE takes what's left (~150-350 KB on B's likely fit) unless the sound tier (800-1,456 KB) itself goes to PCM. B places it.

### 3.8 What the AI costs, by whom

**aitiers1** (recon, sierra-batch 42311872596 [E], shares; row `aitiers1_ai_by_tier`; aicad3 on, thinking timed inline):
- **The actors in the fight are the cost:** 78% of AI time on b30's flight, 89% on the landing and b40's cliff. Calm actors far off (≥ 40 units) are 5-11%, so AIFAR's tier is small.
- **On b30's flight 74% of AI time is actors drawn under 32 px**, mostly 20-40 units away; undrawn 15%. The landing: under 32 px 63%, undrawn 34%. b40: undrawn 76% (40% fighting, undrawn, under 10 units).
- **Per update:** a fighting actor's thinking update 190-265 µs against 85-95 calm, ~60 resting. By step: decide 24-38%, perceive 25-37%, move 10-14%, combat 6-9%, emotion 4-7%. A fighting update is half deciding.
- **What it points at** (Sheepdog01's ruling 3, the in-fight bipeds): on the flight the AI's cost is small, distant fighters, the same tiny bipeds as §3.5. A tier that thinks less often for in-fight actors under ~32 px (a longer decide interval, perception on alternate thinking updates) is the candidate "plays the same" compromise, under an outcome census. Path search vs firing positions inside decide comes with icsweep1.
- **pvsrays1 (the AI's rays against cluster visibility) is closed by reading, no census needed** [D, ChupathingyCE d98982b8]:
  - Every long AI sight ray already goes through `ai_test_line_of_sight`, which returns "obstructed" on a cluster PVS miss before any collision walk (`ai.c` 2319, `scenario_test_pvs`). That covers perception, firing positions, the fire target, flee and search.
  - The rays without that gate are short local probes, where both ends sit in one cluster or neighbours, or are rare: `actor_looking`'s 3-unit look probes, `actor_combat`'s miss shots beside the target, `path_obstacles`, `action_charge`, the grenades' ballistic arcs.
  - So what's left to take is ~0. On b30's open stretches the PVS is near-total anyway, so the walk's cost is in rays that are really clear.

### 3.9 The tick's code working set (icsweep1)

**icsweep1** (recon, sierra-batch 42311430811 [E], the cache model on b151; row `icsweep1_icache_sweep`): the tick phase's instruction misses a tick as built (16 KB 2-way), then fully associative 16 KB and 2-way 32 / 64 / 128 KB:

| Stretch | As built | 16 KB fully assoc. | 32 KB | 64 KB | 128 KB | Stall as built, ms a tick |
|---|---:|---:|---:|---:|---:|---:|
| b30 flight | 45.3k | -7% | -27% | -60% | -77% | 8.70 → 6.31 / 3.48 / 2.00 |
| b30 landing | 36.7k | -7% | -27% | -57% | -74% | 7.06 → 5.16 / 3.06 / 1.84 |
| c20 | 29.0k | -5% | -40% | -71% | -85% | 5.58 → 3.36 / 1.62 / 0.84 |
| b40 | 17.7k | -6% | -25% | -53% | -71% | 3.40 → 2.54 / 1.58 / 1.00 |

- **The misses are capacity, not conflicts:** a fully associative 16 KB cache removes only 5-8% (layout work since `cache_work.md` took the conflicts).
- **The upper bound:** a tick whose every phase's hot code fit the 16 KB cache would behave like a 64-128 KB cache: ~5.2-6.7 ms a tick on the flight, ~4-5 on the landing [U on E]. That's the instruction side only; staging also re-fetches each object's data once a stage.

**The survey (Sheepdog01 18:56: has this been covered?). Yes, thoroughly, and the realistic figure is much smaller than the bound.** `levercheck.py --ground code-layout` and `cpu_cost_map.md` §4a:
- **Stage-major by subsystem was sized on tock162** (§4a, the data price counted): the net ceilings are bipeds 1.1 ms a tick on b30 (2.6 c20), node matrices 0.7-0.95, collision 0.75 (the only subsystem whose code fits as one pass), vehicles 0.2, the AI ≤ 0.2, the rest ~0. They overlap (collision runs inside the bipeds' and vehicles' updates) and **none has an exact stage cut**: each subsystem's output is read later in the same walk (a biped's move by later objects' collision; node matrices by children and collision; the global seed's draws).
- **Every try at it stopped:** PHASEB (the bipeds stage-major: no exact cut; the plays-the-same form ~0.7-1.1 ms), FISSB (biped_update and unit_update split inside: the pot 1-4%, parked), LPH (loop phasing: the cache model scored +11.5% where the real batch lost 34%, so the model can't score phasing), ARMPASS, NB, LEAF (node matrices batched: rejected or shelved on the PSP), CODESPAD (code from the scratchpad: 14.3 cycles an instruction), IPREFETCH (no I-side prefetch from user mode), HG and FH (hot set groupings: slower on the PSP), MB (staged model drawing: the cut misses saved nothing on the PSP-1000).
- **What's already in:** TG's placement by the temporal graph (tick -4.6 to -13% [M]), PGO with hot/cold block splitting (`-freorder-blocks-and-partition`), LTO on the hot units. icsweep1 confirms what they left: conflicts only 5-8%, the rest capacity.
- **Sheepdog01's rulings:** bipeds stage-major was taken out on 2026-11-04.
- **So the realistic figure is ~1-2 ms a tick on the flight**, plays the same (node matrices and collision staged, overlapping), and **~0 exact**. The bound isn't reachable because staging pays the data back and the game reads each subsystem's output within the walk.

**What else could help the instruction side** (not tried, or not tried this way) [U]:
- **Fewer big calls, not reordered ones:** each object update re-fetches its ~300-400 code lines, so I-misses scale with the updates run. The stand-in (§3.5), S4's half rate and animation LOD cut calls, and their ms above already include the misses they save.
- **Code shrunk where it runs hot:** the hot functions' executed paths carry multiplayer, network and Xbox branches the campaign never takes. PGO's block splitting moves cold blocks out, but not whole call paths decided at build time. A campaign-only build of the hottest updates (unit_update 384 lines, biped_update 299) with those paths compiled out is untried.
  - **Counted by deadpaths1** (recon, 42324366275 [E], row `deadpaths1_cold_in_hot_lines`): bytes that never run are ~13% of the tick's hot-line bytes (the top 40 functions). Packed out, the tick would touch 10-12% fewer lines. By kind:
    - the port's own instruments and keep-word alternatives: 20-26% of those bytes by deadpaths2's full listing (deadpaths1's 32-38% came from each function's top 15 lines only);
    - campaign constants: ~1-2%;
    - the rest (69-72%): gameplay branches, which only a finer split than PGO's blocks would move (untried).
  - If the instruction misses follow the lines touched, packing out every dead byte is worth ~0.5-1 ms a tick on the flight [U on E]; it overlaps the ~1-2 ms above.
  - **What a release build can drop: deadpaths2** (recon, sierra-batch 42329745884 [E], row `deadpaths2_release_instruments`, the top 60 functions): the port's part out leaves 28 fewer lines touched on b30 (1.5%) and 46 on c20 (2.7%), so ~0.1-0.2 ms a tick at the flight's and c20's instruction stall. It sits in 255 separate sites (game_tick, actor_update, psp_sleepy_update and psp_object_update hold the most), and the master parked PSPRELEASE on that count (levers.tsv, 2026-11-06 21:12). The instruments that do run in the tick are the ME-or-main tests (`psp_cpu_is_me`, `psp_work_on_me`), a few instructions each.
  - **Side finds:**
    - the limits enum makes every SHORT/CHAR constant 64-bit (FIXES_ENUM32, in A/B as enum32a);
    - software doubles are ~2.6% of c20's instruction misses, mostly `__floatdisf` from `object_compute_function_values_with`, ~83 calls a tick (dsoft1).
- **Whole-line writes** (wholelines1, recon, 42329063869 [E], row `wholelines1_whole_line_writes`): lines that stores fill and then write whole are 4.6% of b30's tick data misses (1,481 a tick) and 4.0% of c20's.
  - The cost is ~0.32 ms a tick on b30 and ~0.12 on c20. `cache 0x18` saves about half, so ~0.16 / 0.06 ms.
  - memcpy (55-60%) and memset (6-21%) hold most of them, so the one change is those two issuing 0x18 for destination lines they write whole. Drawing's share is larger (12-13% of its data misses).
  - Price: one PSP pair (the cache rule).
- **The data side is as large as the instruction side** (~9.6 ms a tick on the flight, §1), and its open levers (OBJHOT, TAGHOT, BSPLAYOUT for the level BSP span's 13-15% of data misses) are small one by one.
- **Where the misses are** (b30's flight, all phases, 60.9k a tick): objects 20% (node matrices, object_update, weapon_update), units 11% (biped_update_moving, unit_update), collision 14% (cluster_partition_reconnect the top), render 9%, AI 9%, the port 7%, libc 5%.
- **Inside the AI:** the path search is 20-39% of its instructions but 1-3% of its misses (tight loops); perception and actors.c hold most of them.

### 3.10 The hardware's functions: used, tried, unexplored (Sheepdog01 19:58)

An audit of what the PSP offers against what the code uses (`git grep` over psp/src and psp_fixups.py, levers.tsv, `hardware_utilization.md`, `vme_capability.md`, `psp_hardware_reference.md`) [D]:

| Function | Status |
|---|---|
| VFPU (matrix and vector unit) | used (psp_vfpu.c); SKELBATCH (posing batched on it) no gain on the PSP: posing's cost is misses, not arithmetic; RM (4-wide bounds tests for collision) proposed, never built, main-CPU tick only |
| `cache 0x1E` (prefetch) | tried (PX, prefetch map); in the gap table at 0.8-2.6 ms |
| **`cache 0x18` (create a line without reading it)** | **never used**; wholelines1 counts the tick's whole-line writes |
| Scratchpad (16 KB) | used (SCRATCHBSP); code from it rejected (CODESPAD) |
| DMAC | used for copies; beside the tick it doubles the CPU's misses |
| The ME, its eDRAM | used (the replay, the AI pass, audio, the ring); full at the target on the heavy stretches |
| The VME | surveyed: no ME job fits (§3.7) |
| GE hardware lights, skinning (8 bones), clip planes, stencil, env-map texgen, CLUT | used |
| GE bounding-box test (BBOX + BJUMP) | counted, rejected (BBX: under the 1% gate; the CPU's culls already take most) |
| GE vertex morphing (up to 8 weighted vertex sets) | priced by bench 17 (~65 ns a target a vertex above the lighting floor: 2 targets lit 211 ns, the same as a lit rigid vertex) and closed because Halo's models have no blend shapes (`hw_capabilities.md`); never tried with blend shapes we bake |
| GE Bezier and spline patches | never used; a close-up research angle only (§3.6) |
| GE logic ops, colour test | not used; no use found |
| The AVC decoder, colour-space converter | VMETEX's repacker only; no tick work fits |
| Firmware audio decoders (ATRAC3+, MP3 through the ME's firmware) | not used: the ME runs the port's own code, so the firmware's ME codecs aren't available beside it [U] |
| Compiler: `-march=allegrex` (min/max, ext/ins, madd for integers), PGO, LTO, block partitioning | used |

**What the earlier audits (`hw_capabilities.md` §2 and §4, `optimization_ranking.md`'s untried list, built on mcidclan's ME, VME and DMA work) left open, and the deep dives on each (Sheepdog01 20:14-20:20; bottleneck, 2026-11-06 20:2x)** [D unless marked]:

1. **The bus's priority (C4), reopened.** Sources read: uofw (`me_wrapper`, `ge.c`, `memoryop.c`, `hardware.h`), jpcsp's DDR model and `sceDdr` (empty stubs), our regsnap (`hw_capabilities.md` §6.3-6.4). What's new:
   - **DDR 0x30 (0x11110410) is one of the four words `sceDdrChangePllClock` rewrites** (0x30, 0x34, 0x40, 0x44). Words that follow the clock are timings, so 0x30 is more likely the DRAM's timing than a per-master table [U, from where it's written]. Tightening DRAM timings would be memory overclocking, the same class as the ruled-out CPU clock: **not proposed.**
   - **The one arbitration-like name left is `sceDdrWriteMaxAllocate`** (by its name, how many write slots a master may hold [U]). No public caller, no arguments. The only test is a call with guessed arguments, which may hang the bus until a power cycle: Sheepdog01's call, and not recommended blind.
   - **What it could be worth is capped at the target.** Priority moves bus slots from the GE to the CPU. The CPU's toll is ~7.5 ms a tick on the flight, but the GE column is already at ~1,050-1,150 of 1,000 after the geometry work (§4), so a priority helps only as far as the GE hides the extra delay in its own prefetching [U]. Net between 0 and the toll; not measurable without the write.
   - **Side find for R:** the GE has two CALL return registers (`RADR1`, `RADR2`), so lists nest at most two deep in hardware. Deeper nesting goes through the firmware's signal CALL (`SCE_GE_SIGNAL_CALL`, a stack in the list's context), which costs a GE interrupt and a handler on the main CPU each call [D, uofw `ge.c` 2046-2148].
2. **Cache line locks.** The Allegrex's cache ops, from the firmware's own use (uofw `ipl.c`, `sysmem/start.S`): D index load tag 0x10 returns both ways' tags at once (TagLo, TagHi; bit 20 dirty, bits 0-18 the address >> 13); D index store tag 0x11/0x13; D hit ops 0x18 (create dirty exclusive), 0x19, 0x1A, 0x1B, 0x1E (fill); I hit invalidate 0x08, I index store tag 0x01/0x03; I fill 0x0E faults in user mode (IPREFETCH [M]). **No source names a lock op**; 0x0F and 0x1F (after the fills) are the likeliest codes [U].
   - **Ceiling, from icsweep1's per-line fetches** (b30 to game s 107, misses spread over each function's lines by fetches) [U on E]: the hottest 2 / 4 / 8 KB of code hold ≤ 8 / 13 / 21% of the instruction misses. Locking them takes that much of a 2-way 16 KB cache from everything else and leaves those sets one-way; 16 → 32 KB moved misses 27%, so giving up 2-4 KB costs ~4-10% back. **Net ~0-4% of the instruction misses, ≤ ~0.35 ms a tick.**
   - The data side has its lock already: the scratchpad (SCRATCHBSP, STACKHOT).
   - **Verdict: not worth a PSP probe of its own.** If a bench ever runs kernel-mode cache ops at the alias (`hw_capabilities.md` §6.5), the 0x0F/0x1F test rides along: fill and lock a line, stream 32 KB through its set, time the line.
3. **The 0xBCC00000 unit is the ME eDRAM's controller, the twin of the GE's eDRAM controller at 0xBD500000.** The register layouts match word for word:

   | Word | GE eDRAM (uofw `sceGeEdramInit`, `sceGeEdramSetRefreshParam`; regsnap) | ME eDRAM (uofw `me_wrapper/init.S`; mcidclan `me-lib.h`) |
   |---|---|---|
   | 0x00 | config, reads 0x00612223; bits 20-23 a refresh field | 0x01612223 (me-lib), -1 (m-cid's fast setting) |
   | 0x10 | write 1, wait for bit 0 to clear (reset) | the same, in both |
   | 0x20 | refresh interval, 0x6C4 by default (23 bits) | -1 (me-lib) |
   | 0x30 | a 10-bit refresh field; reads 8 | 8 (firmware), 1 (me-lib, m-cid) |
   | 0x40 | mode: 1, or 3 for the second refresh mode | 2 (firmware, me-lib), 1 (m-cid) |
   | 0x70 | address translation off | 1 (firmware), 0 (me-lib) |

   - So its "background processing" is the eDRAM's refresh, and m-cid's setting changes its timing.
   - **It was priced already:** MEBCC (`mebcc1`, our `psp_me.c`) on the PSP-3000, b30 (sierra-hw 40112944207 [M]): the ring's jobs 940.6 vs 943.4 µs, fps 8.23 vs 8.16, every stretch within 2%. So the refresh costs the ME's eDRAM ≤ ~0.3%.
   - Slowing the GE eDRAM's (VRAM's) refresh through the same fields would gain a similar fraction and risk decay that grows with heat. **Closed: not a lever.** The PSP-1000 is untested, with no reason to differ.
4. **The GE's commands and the Allegrex's instructions.**
   - **GE features the port doesn't use:** 8-bit vertices and indices, 16-bit vertex colours (MVDIET and MV14 sized the vertex diet: dropped, revive under R or B), Bezier and spline patches (§3.6), logic ops and the colour test (no use found), and immediate-mode vertices carried in the list (only tiny sprites would gain). Through mode and sprites, swizzled textures, CLUT 4 and 8, culling, texture slope and LOD modes, skinning, lights and env-map texgen are in use.
   - **The instruction mix in the hot code** (`a6b3f6e1`'s disassembly, weighted by icsweep1's per-function fetches; the top 80 functions, 72.5% of fetches) [U on E]:
     - loads and stores 29%, ALU 25%, branches and the rest 27%, FPU ~6%, VFPU ~2% (`vfpu_multiply`, `vfpu_interpolate`);
     - the Allegrex's own (min, max, ext, ins, seb/wsbh, movz, movn) ~1.8%: GCC uses them;
     - the slow ops (`div.s` 0.12%, `div`/`divu` 0.06%, `sqrt.s` 0.04%) ≤ 0.2% of instructions. Even at a full ~25-cycle stall each that's ≤ ~5% of the working cycles (~0.7 ms of 15.3), and likely far less, since they sit outside the inner loops.
     - The exact executed count needs a per-opcode tally in the cache model. That's worth adding to the next emulator patch; nothing else on the instruction side is a lever.
5. **The link-time wraps.** From icsweep1's `funcfetches.txt` (b30 to game s 107, every phase) [E]: every wrap and the allocator together (`__wrap_*`, `heap_*`, `_malloc_r`, `_free_r`) are ~2.6 M of 15.8 G fetches (0.02%) and ~0.1 M of 173 M instruction misses (0.06%). The format path (`_svfprintf_r`) is 0.08% of fetches and 0.18% of misses. **So the wraps are ~0 (≤ 0.01 ms a tick): closed.** E5's old 4% share is gone.

**So the hardware angles add nothing exact to §4.** Bus priority is the one with a real ceiling, and it is a blind write.

**Closed since (2026-11-07):** bus priority is no lever: the full read found no arbiter to set, and the bus is route-around only (the lab's `projects/sierra/docs/research/bus_arbiter_closure.md`). Of the hardware angles above, none is left exact.

**A new use for a priced function: GE morphing for small bipeds.** The GE can blend up to 8 copies of a mesh by weights, per vertex, for free on the CPU side. For bipeds drawn ~16-32 px, a decimated mesh (§3.6) baked in a dozen or two key poses a character (walk and run phases, idle, crouch, fire, dying), drawn as a morph between the two nearest keys, animates with no posing on the CPU or ME and no skinning matrices. The tick still poses the chains the simulation reads (§3.5). Memory: at ~100 vertices a decimated grunt, ~1.5-2 KB a key, ~25-40 KB a character [U]. It fills the gap between the sprite (under 16 px) and full posing (over 32 px), where a pose library would need the aim overlays (§3.5).

## 4. The sum and the gap

**b30's flight, first pass** (2026-11-06 16:20; ceilings as each section says, before realisation; the other stretches follow as their counts land). The tick needs 42.4 → ~16.6 ms (-25.8), more if the toll rises at 30 fps (§1); the GE needs ~1,800 → 1,000 ms a second.

| Lever | Kind | Tick, ms a tick | GE at the target, ms a second | Label |
|---|---|---:|---:|---|
| Tiny bipeds posed by their read chains (§3.5) | exact for play | ~3-3.5 | — | U on E |
| Error-budget LODs, rigid + skinned (§3.6) | look, Sheepdog01's *e* | via the toll ~0.5-1 | ~-600 to -700 (with the stand-in's sprites under 16 px) | U on E |
| Extra passes trimmed on small models (§3.4) | look | via the toll, small | ~-290 at 32 px, measured vertices (overlaps the sprites' cut under 16 px) | U on E |
| S4's far bipeds at half rate (`redesign_s.md`) | plays the same | ~2.3 | — | U on M |
| Animation LOD form A on 16-32 px (§3.1) | plays the same | ~0.5 | — | U on E |
| R's list bytes (the toll's 'lists' 2.2) | exact | ~1 | (R's own column) | U on M |
| Prefetch in reshaped loops (cost map §4) | exact | 0.8-2.6 | — | E |
| T7 and smaller tick pieces | exact | ~0.5 | — | U |
| The tick's code working set by phase (§3.9) | plays the same (no exact cut) | ~1-2 (bound 5.2-6.7) | — | U on E |
| Dead bytes out of the hot lines (deadpaths1-2, §3.9; overlaps the row above) | exact | ~0.1-0.2 reachable (the port's part, PSPRELEASE parked); the gameplay branches' rest needs a finer split, untried | — | U on E |
| memcpy/memset with `cache 0x18` (wholelines1, §3.9) | exact | ~0.16 | — | E |
| **Sum** | | **~10.4-14.2** (the dead-bytes row at its reachable part) | **~-650 to -750** | |
| **Gap** | | **~11.5-15.5 ms a tick** | **~+50 to +150** | |

What it says:
- **The GE column nearly closes** with the geometry work (LODs, the stand-in, the passes), the flight from ~1,800 to ~1,050-1,150 ms a second [U], before measured numbers replace the guesses.
- **The tick column doesn't close by a long way.** With every ceiling above at its top, the flight's tick is still ~29-32 ms against 16.6 [U]. Collision (25.6% of the tick on the PSP, §1) and the AI's main-CPU share (16.5%, aicad3 already on in the bare bench) carry no lever here; S3 (AITICKOLD) doesn't play the same yet; the code's working set now has its ceiling (§3.9).
- **So by exhaustion so far, the flight's tick at 30 fps needs compromises in the simulation itself**, beyond Sheepdog01's ruling 3's first step: the in-fight bipeds at a lower rate, the AI thinking less often, or fewer actors simulated at full fidelity. Those are Sheepdog01's to decide stretch by stretch, with outcome censuses. The exact angles left: the code working set (§3.9, now in the sum), S3's fix, and bus priority (§3.10), a blind write that is Sheepdog01's call.
