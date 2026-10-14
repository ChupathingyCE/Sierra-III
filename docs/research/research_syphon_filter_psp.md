# Shipped PSP shooters: what is publicly documented

Researched 2026-10-13 with WebSearch and WebFetch. Every claim has a URL.

## Headline finding

Public technical detail on Bend's PSP engine is very thin. I found no GDC Vault session, Gamasutra/Game Developer postmortem, Edge/1UP tech deep-dive, or Digital Foundry piece on Dark Mirror, Logan's Shadow or Resistance: Retribution.
- What exists is marketing-grade developer quotes (PlayStation Blog, VideoGamer, PocketGamer) and reviewer observations.
- Hard numbers (poly budgets, frame rate target, memory split, actor caps) are essentially absent.
- The best hard-ish data points are: Retribution shows "15 Leapers at once" versus "4-6 terrorists" in Logan's Shadow; Bend rebuilt streaming and animation for Retribution; Retribution replaced face-button aiming with an aim-assist box.
- The most technically useful adjacent sources are Ready at Dawn's Q&A (shared-bus note, 333 MHz effects) and the Amaze GDC 2006 abstract (memory / streaming / bone counts).

Evidence labels used below:
- [Q] = quoted from a developer in a secondary source.
- [R] = reviewer observation.
- [W] = Wikipedia summary. Note: the WebFetch summariser is a small model, so treat [W] rows as pointers to verify.
- [U] = my inference.
- [X] = checked and absent from the source.

---

## 1. Syphon Filter: Dark Mirror (PSP, 2006)

| Topic | Known | Label | Source |
|---|---|---|---|
| Engine overhaul | Press release says "the graphics engine was overhauled to use the PSP's full visual capacity" (no specifics). | Q (press release) | https://gamedeveloper.com/press-release/logans-back |
| AI states | Enemies have "idle, curious, alarmed" awareness states; dynamic difficulty system. | Q (press release) | https://gamedeveloper.com/press-release/logans-back |
| Content scale | 23 single-player missions, 5 online MP maps; PSP MP up to 8 players. | Q / W | https://gamedeveloper.com/press-release/logans-back ; https://en.wikipedia.org/wiki/Syphon_Filter:_Dark_Mirror |
| Visual detail | Studio history page says Dark Mirror added clothing tags, buckles, footwear variety, snow footprints, dust clouds. | Q (studio) | https://www.bendstudio.com/blog/history-of-bend-studio/ |
| Lighting / FX | Flashlight, infrared (screen goes red), thermal and night vision effects; reviewer compared flashlight to Doom 3's. | R | https://www.gamespot.com/articles/syphon-filter-dark-mirror-preview-the-rundown/1100-6143660/ |
| Fog and lighting | "Surprisingly advanced fog and lighting effects". | R | https://www.trustedreviews.com/reviews/syphon-filter-dark-mirror |
| Frame rate | Reviewer: handles multiple opponents and busy locations "without ever seeming to struggle or lose a frame". No number given. | R | https://www.trustedreviews.com/reviews/syphon-filter-dark-mirror |
| Load times | "Doesn't even leave you waiting long between missions". | R | https://www.trustedreviews.com/reviews/syphon-filter-dark-mirror |
| Aiming controls | One nub moves; face buttons aim ("actually genius", a precision game "could not have worked on the PSP any other way"). | R | https://www.pocketgamer.com/syphon-filter-dark-mirror/review/ |
| Controls (alt. view) | Shoulders aim/fire/zoom; D-pad + face buttons cycle gear; takes time to learn, 4-part training mission. | R | https://archive.triblive.com/local/local-news/control-features-push-dark-mirror-to-the-top |
| AI behaviour | Enemies "take cover, pop around corners, reposition". | R | https://www.pocketgamer.com/syphon-filter-dark-mirror/review/ |
| Cover | Players can hide against walls and fire around corners; roll removed. | W | https://en.wikipedia.org/wiki/Syphon_Filter:_Dark_Mirror |
| Enemy count scaling | "Enemy count scales with play style": blasting through spawns more enemies. No numbers. | R | https://www.gamespot.com/articles/syphon-filter-dark-mirror-preview-the-rundown/1100-6143660/ |
| Emulator hints | PPSSPP forum: heavy on geometry/draw calls in some views; buffered rendering and framebuffer copies cause trouble. This is user speculation, not developer data. | U | https://forums.ppsspp.org/showthread.php?tid=421 |

Not found: frame rate target, poly or texture budgets, memory budget, streaming design, ME use, tools.

## 2. Syphon Filter: Logan's Shadow (PSP, 2007)

| Topic | Known | Label | Source |
|---|---|---|---|
| Clock | Uses the full 333 MHz (versus the 222 MHz default). Quote attributed to technical director Chris Reese. | Q | https://blog.playstation.com/2007/07/27/syphon-filter-logan%E2%80%99s-shadow-taps-into-all-333mhz-on-psp/ ; https://www.pocketgamer.com/articles/003746/r/ |
| What 333 MHz bought | Per the same posts: larger environments with more interactive elements, water rendering/gameplay, Havok physics, "faster AI response times". No numbers. | Q | same two URLs |
| Physics | Havok; reviewer cites ragdoll and "realistic dynamics". | Q / R | https://www.pocketgamer.com/articles/003746/r/ ; https://www.engadget.com/2007-09-15-psp-fanboy-hands-on-logans-shadow.html |
| Enemies on screen | Garvin (2008): "4-6 terrorists at a time" in Logan's Shadow. | Q | https://www.videogamer.com/?p=96918 |
| Effects stack | Reviewer: volumetric smoke, real-time water, particle and shader effects together with AI and physics; "rivals many PS2 games". | R | https://www.engadget.com/2007-09-15-psp-fanboy-hands-on-logans-shadow.html ; https://www.engadget.com/2007-10-02-psp-fanboy-review-syphon-filter-logans-shadow.html |
| Water | "Not physically modelled to console realism but looks close". | R | https://worthplaying.com/article/2007/5/23/previews/42209-psp-preview-syphon-filter-logans-shadow/ |
| Load times | "Fairly long, about a dozen seconds per level." | R | https://www.engadget.com/2007-10-02-psp-fanboy-review-syphon-filter-logans-shadow.html |
| Load times (alt.) | "Relatively short." The two reviewers disagree. | R | https://gamespot.com/reviews/syphon-filter-logans-shadow-review/1900-6180591/ |
| Aiming schemes | 3 schemes: (a) face buttons as a second stick (judged worst), (b) its reverse, (c) "classic": lock-on + manual aim + strafe buttons on the nub (judged best). | R | https://gamespot.com/reviews/syphon-filter-logans-shadow-review/1900-6180591/ |
| Aiming (other view) | Analog nub + face buttons for fine adjustment; auto-cover/peek/blindfire; "Who needs a second analog stick?" | R | https://www.engadget.com/2007-10-02-psp-fanboy-review-syphon-filter-logans-shadow.html |
| Aim failure mode | Manually aiming at an EM-suit enemy's knees exits aim mode and can get you killed. | R | https://gamespot.com/reviews/syphon-filter-logans-shadow-review/1900-6180591/ |
| AI behaviour | Cover and help each other, sometimes get stuck; some charge aggressively. | R | https://www.engadget.com/2007-10-02-psp-fanboy-review-syphon-filter-logans-shadow.html |
| Ally AI | Buddy "will respond to enemies in a more believable manner" (Garvin). | Q | https://www.pocketgamer.com/syphon-filter-logans-shadow/hands-on-with-syphon-filter-logans-shadow/ |
| Slim comfort | Reviewer found controls easier on the PSP-2000's looser stick. | R | https://worthplaying.com/article/2007/5/23/previews/42209-psp-preview-syphon-filter-logans-shadow/ |

Not found: frame rate target, engine name, memory budget, streaming, ME, tools.

## 3. Resistance: Retribution (Bend, 2009)

| Topic | Known | Label | Source |
|---|---|---|---|
| Animation | Garvin: "complete upgrade on our animation system, adding support for multiple skeletons" for the Chimera types. | Q | https://www.videogamer.com/?p=96918 ; https://blog.playstation.com/2008/09/16/resistance-retribution-developer-interview-and-walkthrough |
| Character lighting | Specular highlights on characters (Grayson's jacket). | Q | same |
| Textures | Character textures enlarged. | Q | same |
| Streaming | "More robust streaming system" supporting more level detail, sounds, character models and animations. No mechanism described. | Q | same |
| Enemy density | New enemy system: "15 Leapers at once", versus 4-6 terrorists in Logan's Shadow. | Q | https://www.videogamer.com/?p=96918 |
| Draw distance | Several systems optimised "to extend draw distance". | Q | https://www.videogamer.com/?p=96918 |
| Aim system | Face-button aiming from the Syphon games was replaced by an aim-assist box: reticle snaps to enemies inside the box. | Q (studio) | https://www.bendstudio.com/blog/history-of-bend-studio/ |
| Aim details | Bracketed window auto-targets enemies inside it; tap a face button to switch target; stays locked while strafing. | R | https://gamespot.com/reviews/resistance-retribution-review/1900-6206356/ |
| Aim-assist limits | Disabled when looking down sights (needed for headshots); not used for secondary attacks/heavy weapons (manual aim, D-pad zoom). | R | https://gamespot.com/reviews/resistance-retribution-review/1900-6206356/ |
| Cover | Auto-cover when near objects; reviewer says "too sticky" at times. | R | https://giantbomb.com/posts/making-alternate-history-with-resistance-retributi ; https://gamespot.com/reviews/resistance-retribution-review/1900-6206356/ |
| PS3 pad mode | PSP Plus with a DualShock 3 disables aim assist and raises difficulty. | R / W | https://giantbomb.com/posts/making-alternate-history-with-resistance-retributi ; https://en.wikipedia.org/wiki/Resistance:_Retribution |
| Multiplayer | 8 players. | Q / W | https://www.bendstudio.com/blog/history-of-bend-studio/ ; https://en.wikipedia.org/wiki/Resistance:_Retribution |

Not found: frame rate, engine name, memory budget, how streaming is implemented, AI update budget.

## 4. Brief entries

### Killzone: Liberation (Guerrilla, 2006)
| Known | Label | Source |
|---|---|---|
| Camera moved from FPS to elevated third-person (isometric-style); dynamic camera zooms to threats; devs said it made the camera harder to implement but AI behaviour easier to observe. | Q | https://www.gameswelt.de/killzone-liberation/special/killzone-liberation-interview-1617 |
| "Pseudo-isometric Intelligent Camera System" (PICS), auto pan/zoom when an enemy approaches. | Q | https://gamingnexus.com/Article/1195/Killzone-Liberation-Interview |
| Enemies take cover and throw grenades; allies take orders (move, target, plant). | R | https://www.psu.com/feature/12361/Inside-PlayStation-Network---Killzone--Liberation |
| 16 missions, in-engine cutscenes, 2-player co-op. | Q | gameswelt URL above; gamingnexus URL above |
| Housemarque reportedly provided coding support. | W | https://en.wikipedia.org/wiki/Killzone:_Liberation |
| Not found: engine, frame rate, enemy caps. The elevated camera is a design-level performance/readability choice, but no source ties it to performance. | X / U | |

### SOCOM: U.S. Navy SEALs Fireteam Bravo (Zipper, 2005)
| Known | Label | Source |
|---|---|---|
| "Target Lock" replaced face-button look control, which the dev found unresponsive. Four fewer buttons than the PS2 pad, so tap/hold double-duty. | Q | https://www.gamingnexus.com/Article/951/Interview-SOCOM-Fire-Team-Bravo |
| Textures less detailed than PS2; possibly fewer animations; feature parity otherwise claimed. | Q | same |
| R trigger auto-targets nearest enemy, L trigger cycles; fixed camera behind the player. | R | https://gamespot.com/articles/socom-us-navy-seals-fireteam-bravo-updated-hands-on/1100-6136569/ |
| Environments: lower-res textures, more open space, less scene dressing; enemies in fewer numbers in some places; multiplayer maps smaller by design. | R | same |
| Frame rate "rarely hitched". | R | same |
| Not found: engine, AI counts. | X | |

### Star Wars Battlefront: Renegade Squadron (Rebellion, 2007)
| Known | Label | Source |
|---|---|---|
| Up to 16 online, 8 ad-hoc; Conquest and CTF only. | Q / W | https://www.gamespot.com/articles/star-wars-opens-new-psp-battlefront/1100-6170474/ ; https://en.wikipedia.org/wiki/Star_Wars_Battlefront:_Renegade_Squadron |
| Longer view distance, higher-res textures, better lighting than Battlefront II (lead designer; Wikipedia cites IGN dev blog, not fetched). | Q via W | https://en.wikipedia.org/wiki/Star_Wars_Battlefront:_Renegade_Squadron |
| Frame rate "solid, even with a lot of weapon-fire onscreen" (E3 build). | R | https://gamespot.com/articles/e3-07-star-wars-battlefront-renegade-squadron-impressions/1100-6174358/ |
| Lock-on called "a waste of time" by Eurogamer; controls generally criticised; no second nub. | R via W | https://en.wikipedia.org/wiki/Star_Wars_Battlefront:_Renegade_Squadron |
| Not found: bot counts, engine. | X | |

### Medal of Honor: Heroes (EA / Team Fusion, 2006)
| Known | Label | Source |
|---|---|---|
| New dynamic AI system, increased enemy counts (examples: "16 enemies", "a dozen AI opponents"); larger levels; 32-player infrastructure, 15 maps, 6 modes. | Q (this is the Heroes 2 Q&A; check that the page is about Heroes 2 before citing it for Heroes 1) | https://gamespot.com/articles/medal-of-honor-heroes-qanda-the-greatest-little-war/1100-6155252/ |
| Developer Team Fusion; 32 online players. | W | https://en.wikipedia.org/wiki/Medal_of_Honor:_Heroes |
| WARNING: the "32 players at 60 fps" claim on Shacknews is about the Wii Heroes 2, not PSP. Do not use it. | n/a | https://www.shacknews.com/article/48853/medal-of-honor-heroes-2 |
| Not found: PSP engine, frame rate. | X | |

### Call of Duty: Roads to Victory (Amaze, 2007)
| Known | Label | Source |
|---|---|---|
| Auto-aim snaps crosshair when pointing near enemies, accuracy falls off with distance; L = iron sights snapping to the head; auto-aim can be disabled. | R | https://gamespot.com/articles/call-of-duty-roads-to-victory-hands-on/1100-6165562/ |
| Must stop moving to throw grenades, swap weapons or crouch (default scheme); one nub + few buttons called a major limitation for PSP FPS. | R | same |
| Geometry and textures show the expected PSP "slight detail hit". | R | same |
| Amaze is also the studio behind the GDC 2006 PSP graphics talk below. | | |

### Ghost Recon Predator (Ubi Paris, 2010)
| Known | Label | Source |
|---|---|---|
| Third-person, Sri Lanka, 3-player ad-hoc co-op. Nothing technical found. | Q | https://www.co-optimus.com/article/4439/ghost-recon-predator-announced-for-psp.html |

### Crisis Core: Final Fantasy VII (Square Enix, 2008)
| Known | Label | Source |
|---|---|---|
| Original engine not specified. Planned scenes cut "due to UMD limitations". Kitase: cutscenes so impressive it could almost be a PS2 game. Nothing on frame rate or streaming. | W | https://en.wikipedia.org/wiki/Crisis_Core:_Final_Fantasy_VII |

### God of War: Chains of Olympus (Ready at Dawn, 2008)
| Known | Label | Source |
|---|---|---|
| Engine is Ready at Dawn's in-house engine, built for Daxter, never designed for PSP, also runs on Windows for development. | Q | https://www.gamedeveloper.com/game-platforms/q-a-ready-at-dawn-s-road-to-i-daxter-god-of-war-i-psp |
| Malenfant (RAD president, Mar 2007): "single shared bus, so graphics work can slow down the CPU"; team ran internally at 333 MHz. | Q | same |
| Tools: heavy investment; most content data-driven, doable without programmers. Team about 40. | Q | same |
| Daxter deliberately did not push the hardware, to avoid a "science experiment". | Q | same |
| Engine was upgraded 1.0 to 2.0 for GoW; nearly every system reworked; camera rewritten. | Q | https://www.pocketgamer.com/articles/003616/r/ |
| 333 MHz let them add more enemies, more particles, more visual detail; "our tech didn't have to change at all"; "like flipping a switch". At 222 MHz, wide vistas (Helios Temple, Hades) and big fights would struggle. | Q | https://gamespot.com/articles/god-of-war-chains-of-olympus-screenshot-comparison/1100-6187331/ |
| Seamless loading was hard; team ran a movie file (Daxter's opening cinematic) in the background as a workaround (as relayed by Wikipedia, citing GamesRadar 2025). | W | https://en.wikipedia.org/wiki/God_of_War:_Chains_of_Olympus |
| GameTap reviewer praised "seamless loading". | R | https://www.gamedeveloper.com/game-platforms/critical-reception-sony-ready-at-dawn-s-i-god-of-war-chains-of-olympus-i- |
| G4 reported occasional tearing and frame-rate drops. | R via W | https://en.wikipedia.org/wiki/God_of_War:_Chains_of_Olympus |
| Fixed, cinematic camera; no camera control; evade moved to L+R held with stick. | Q | https://www.shacknews.com/article/47049/god-of-war-chains-of |
| Engine later released as middleware ("Ready at Dawn Engine", 2009) for 360, PSP, PS3. | Q | https://mcvuk.com/?p=134879 |

---

## 5. General PSP data points from GDC (not game-specific)

| Known | Source |
|---|---|
| GDC 2006, Amaze Entertainment (Jason Piel, Phil Trumbo), "Creating World-Class Graphics on the PSP". Abstract: main constraints are memory, streaming and bone counts; environment artists work within polygon limits; lighting must balance prelit and dynamic; characters have limited bones and texture sizes; facial animation by morph targets or bones; engine must be adapted to PSP. Page is tagged free, audio only. | https://gdcvault.com/play/1013191/Creating-World-Class-Graphics-on (and 1013424, same title) |
| GDC Europe 2005 PSP overview: memory performance roughly half to a third of PS2 and "not great"; scratchpad considered ineffective; scalar FPU is IEEE754 (PS2 ports raised many exceptions); GE needs a custom full 3D clipping system. | https://www.gamedeveloper.com/business/postcard-from-gdc-europe-2005-psp-2005-overview |
| The ME was off-limits to retail code on official firmware (only kernel multimedia decode). No source I found says any shipped shooter used it as a general second CPU. | https://wololo.net/2026/06/16/unlocking-the-psps-dual-core-setup/ ; https://ppsspp.org/docs/development/ppsspp-internals/media-engine |

---

## 6. Lessons applicable to a Halo PSP port

Mostly [U] (inference). The facts underneath are cited above.

- **Actor caps.**
  - Bend's own numbers: about 4-6 simultaneous enemies on Logan's Shadow, and a rebuilt enemy system to get 15 Leapers on Retribution (https://www.videogamer.com/?p=96918). Retribution's Leapers are small, fast melee types, so [U] 15 is a count for cheap actors, not a general ceiling.
  - Reviewers describe Dark Mirror and Logan's Shadow as having small groups with cover AI. [U] Halo's Covenant squads (about 4-8 AI plus vehicles) are in the same range if AI is simple.
  - SOCOM FB and Battlefront both shipped with fewer enemies and smaller maps than the console versions (https://gamespot.com/articles/socom-us-navy-seals-fireteam-bravo-updated-hands-on/1100-6136569/). Cutting actor counts and map size is the norm, not a failure.
- **AI LOD / update budgets.** No source gives AI update rates. Dark Mirror's idle/curious/alarmed states (https://gamedeveloper.com/press-release/logans-back) are a cheap state-based design. [U] Time-slicing distant AI and dropping perception to the 3 coarse states is consistent with that. Bend credited faster AI response to the 333 MHz clock (https://blog.playstation.com/2007/07/27/syphon-filter-logan%E2%80%99s-shadow-taps-into-all-333mhz-on-psp/), which implies AI was CPU-bound at 222 MHz.
- **Animation / skinning.** Bend's Retribution work was "multiple skeletons" and bigger character textures, not fewer bones (https://www.videogamer.com/?p=96918). The Amaze GDC abstract names bone count as a top constraint. [U] Per-archetype skeletons with small bone counts are the shipped pattern.
- **Streaming.** Retribution needed a "more robust streaming system" for level detail, sounds, models and animations (https://www.videogamer.com/?p=96918). RAD's Chains of Olympus hid load behind a background movie (https://en.wikipedia.org/wiki/God_of_War:_Chains_of_Olympus). Logan's Shadow loads about 12 s per level (https://www.engadget.com/2007-10-02-psp-fanboy-review-syphon-filter-logans-shadow.html). [U] Level-sized loads with a hidden or cinematic cover are accepted; seamless streaming was a late-generation achievement.
- **Aim assist / lock-on.** The documented trajectory across games:
  - Face-button aim is hard (SOCOM called it unresponsive and shipped Target Lock; Retribution called it "unwieldy" and shipped an aim-assist box).
  - Bend's Syphon games kept face-button aiming plus an optional lock-on "classic" scheme (https://gamespot.com/reviews/syphon-filter-logans-shadow-review/1900-6180591/).
  - [U] For Halo (a hitscan/projectile FPS with a single nub) a bracketed auto-target window that is switchable by a face button and disabled in scope is the design with the most precedent: Retribution (https://gamespot.com/reviews/resistance-retribution-review/1900-6206356/).
  - CoD RtV's iron-sights head snap shows what other studios did with the L button (https://gamespot.com/articles/call-of-duty-roads-to-victory-hands-on/1100-6165562/).
- **Fixed vs variable frame rate.** No source states a target for any of these games. Reviewers say Dark Mirror "never lost a frame" (https://www.trustedreviews.com/reviews/syphon-filter-dark-mirror) and SOCOM FB "rarely hitched". [U] That suggests locked or near-locked rates with content budgeted to fit, but I cannot confirm 30 vs 60 from public sources. G4 reported tearing and drops on Chains of Olympus, so even the best-regarded technical showpiece was not perfect.
- **Baked vs dynamic lighting.** The Amaze abstract frames it as a balance of "prelit and dynamic". Bend added specular on characters only (https://www.videogamer.com/?p=96918). [U] Baked world lighting plus a few dynamic/character-only effects (flashlight, specular) is the shipped pattern, with no source showing more.
- **333 MHz.** Both Bend (https://blog.playstation.com/2007/07/27/syphon-filter-logan%E2%80%99s-shadow-taps-into-all-333mhz-on-psp/) and Ready at Dawn (https://gamespot.com/articles/god-of-war-chains-of-olympus-screenshot-comparison/1100-6187331/) shipped at 333 MHz and used the headroom for enemies, particles and AI. This does not conflict with the "never rely on overclocking" rule, since the target is 333 MHz stock, but note that the late games were designed for it from the start.
- **Memory bus.** RAD's note that graphics traffic slows the CPU (https://www.gamedeveloper.com/game-platforms/q-a-ready-at-dawn-s-road-to-i-daxter-god-of-war-i-psp) matches the port's measured memory-latency-bound profile. It is not evidence of a fix.
- **Media Engine.** I found no evidence that any of these shooters used the ME for game work (retail ME access was restricted, see section 5). Treat ME offload as unprecedented in shipped titles. [U]

## 7. Blocked, members-only or not retrievable: worth a manual fetch

| URL | Why it might be valuable | Status |
|---|---|---|
| https://discmaster.textfiles.com/file/18543/God%20of%20War%20-%20Chains%20of%20Olympus%20(Press%20Disc)%20(Europe).iso/Q&A/GoW-CoO_QandA.doc/GoW-CoO_QandA.pdf | Ready at Dawn Q&A from the European press disc; may hold engine and 333 MHz detail | WebFetch returned ROBOTS_DISALLOWED. Not routed around. |
| https://gdcvault.com/ (search "PSP", "Bend", "Sony Bend", "Syphon Filter", 2006-2009) | My searches surfaced no Bend session, but GDC Vault search is JS-driven and search engines index it poorly | Could not query the site's search. Human search recommended. |
| Edge, "Interview: Taking God of War to PSP" (March 2008, cited by Wikipedia) | Cited for RAD engine, camera and cut content | Not found online by search. Likely print/Edge archive. |
| IGN Star Wars Battlefront: Renegade Squadron developer blogs, 24 Jul 2007 | Cited for view distance and texture claims | Not located by search; try IGN archive / Wayback. |
| GameSpy interview with K.C. Coleman, 6 Jun 2007 (Battlefront controls) | Control design decisions | Not located; try Wayback. |
| 1UP "Previews: God of War PSP" (Dec 2007), 1UP Matt Leone | Cited for 333 MHz changes | 1UP site is offline; try Wayback. |
| GameSpy Dark Mirror interview with Sony Bend (cited by Wikipedia for cover system) | May hold Bend design and tech detail | Not located; try Wayback. |
| 1UP preview by Alice Liang (18 May 2007) and IGN First Look by Jeff Haynes (17 May 2007), Logan's Shadow | Cited by Wikipedia for cover/melee mechanics | Not fetched. Try Wayback. |
| IGN "Medal of Honor Storms PSP" and "Heroic Cast" (Aug/Oct 2006), GameSpot "Medal of Honor enlists for PSP" | Heroes 1 technical details | Not fetched. |

## 8. Gaps worth stating plainly

- No public source I could reach gives any of these for any game: AI update budget, memory split, poly counts, texture formats or VRAM use, ME use, actual frame-rate target.
- The reviewer quotes are subjective. The two Logan's Shadow reviewers disagree on load length.
- Wikipedia rows were summarised by a small model and cite print magazines I could not verify.
- A better route to hard numbers is likely reverse engineering (disassembling Dark Mirror or Retribution on PPSSPP or hardware) rather than interviews. That is outside this task's scope.
