# Review: build 136

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). b136 is b135 (`review_b135.md`) plus everything that made the 03:00 cutoff on 2026-10-17: one speed lever, the front end's first step, the renderer session's menu and glow work, the controls, and the multiplayer work, with two instrument sets built only on request. Each branch has its own walk-through; this one says what each brings, the key lines, and how they were merged. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

| Branch (head) | What | Kind | Default | Its own review |
|---|---|---|---|---|
| `wake-on-write` (`e2f4c79`) | A sleeping object checksummed only when something marked it | speed, exact | on in local games (`wakeonwriteoff`) | §WT below |
| `frontdoor` (`d8506f9`) | The game starts on its main menu; L or `menu.txt` for the launch menu; the 26 default game variants no longer rewritten every boot | front end | on (`variantswrite` restores the rewrite) | `review_frontdoor.md` |
| `glowmaps` (`8b826c2`) | Self-illumination glow maps, the main menu as the hardware's showcase, `psp_tex_carve` fixed both ways, the bake's texture quality steps (off) | restoration, look | on (`glowflat`, `noshowcase`) | `review_glowmaps.md` |
| `screenglow` (`22cab73`) | The glow screened over the base instead of added | look, nearer the original | on (`glowadd`) | §Screen below |
| `controls` (`d9885aa`) | Crouch toggle, reload tap / action hold, the launch menu's nub page, the port's own PSP button glyphs | controls, look | on (`crouchhold`, `actiontap`, `xboxglyphs`) | `review_controls.md` |
| `dithershift` (`e2a441d`) | The blended passes dithered again, the pattern moved each pass | look | **off**; the shift builds only with `PSP_DITHERSHIFT=1`; `ditherlift`, `ditherprobe` opt-in | `review_dithershift.md` |
| `mp135` (`0c8a207`) | The multiplayer work on b135, network version 29, the public-game drop fixed (`netpace5`) | multiplayer only | on in the multiplayer build | `review_mpb4.md` |
| `recon134` (`3514f18`) | The recon censuses | instruments | not built unless `PSP_RECON=1` | `recon_134.md` |

## Release check

Lab 39018086383, strict with b136's own profile, against b135 as built (`5950c5e`) [E]:
- **Play and both random seeds identical to b135 at every one of 12,000 ticks** on d20, a50 and c20 (`tools/statecmp.py`). The whole-state hashes differ from tick 0 in bookkeeping only: the decal vertex cache, the actors' and objects' allocations, which carry WT's marks and the controls' state.
- b30 and c40 shots right on the b136 bake; strict clean on every run.
- The main menu: the showcase sharper than `noshowcase` at the same moments; strict clean.
- a10's cryo prompt draws the PSP glyph ("NUB" over the nub, "Use [nub] to look around"): `glyphs4`'s first run.
- b30 with `PSP_DITHERSHIFT=1` and `dithershift`: shots for Sheepdog01's look.
- **Blood Gulch did not run** in the batch: the lab's batch stages the multiplayer build into `ms0:/halo`, while `mp135` keeps it in `ms0:/halomp`, and the run timed out with no log. The multiplayer session smoke-tests it with its own harness.

## §WT Wake-on-write (lever WT)

### WT.1 How it works

**Before:** every tick, each sleeping object's whole datum (up to 2 KB) was checksummed and compared with the sum it fell asleep with, to catch anything that changed it from outside its own update. 99.7%+ of those sums found nothing [E, `sleepcensus`], and they cost 35–45% of the scenery step they fall in on b30 [E, wakesteps135].

**Now:** the code that changes an object from outside its update *says so*. `psp_wake()` marks the object, and its parents, because the checksum covers a family:
```c
void psp_wake(long object_index)
{
	...
	while (object_index != NONE && depth++ < 8)
	{
		...
		psp_wake_marked[absolute_index] = 1;
		object_index = header->datum->object.parent_object_index;
	}
}
```
At the check, an unmarked sleeper keeps the sum it slept with; anything else is summed as before (`psp_wake_family_checksum`):
```c
	psp_wake_marked[absolute_index] = 0;
	if (!psp_wake_on || marked || psp_sleep[absolute_index].identifier != header->identifier ||
		psp_sleep[absolute_index].quiet < 2 || header->type == _object_type_machine || header->type == _object_type_control ||
		(psp_wake_on == 1 && game_connection() != _game_connection_local))
		return psp_family_checksum(header);
```
- **Why a mark is cheap to get wrong safely:** a false mark costs one checksum, what every tick cost before. Only a *missed* writer is a bug (the sleeper stays asleep when it should wake).
- **Why machines and controls are left out:** device groups change their state without touching the object.
- **Why the sleep state's own identifier is compared:** a slot reused by a new object must not inherit an old sum.

### WT.2 The writers, and how they were found

Each hook is one call at the top of the writer (`/* psp_fixups.py (FIXES_WAKE) */`):
- **Events** (the object trace's list, `objtrace134`): damage (`object_cause_damage`), garbage, visibility, position, attach to a marker or node, detach, delete, map disconnect and reconnect, activate, deactivate, unit control, a unit's driver and gunner, the render state cache.
- **Pushes:** item, biped, vehicle and projectile accelerate (blasts, physics, a vehicle's bump).
- **Found by `wakecheck`** (beyond the trace):
  - shield and body depletion: physics' ground test depletes the shield of what a mass point rests on (a tree);
  - the garbage list's neighbour: taking an object off the list rewrites the link of the one before it, so that one is marked too;
  - the AI's pathfinding answer kept in a biped: marked only when the kept answer changes, since the AI asks often. The hook copies the fields before and compares after; they are 24 bytes into a 32-byte buffer [D];
  - a sleeper's own functions turning one on or off (`functions_active_flags` is a word the checksum compares).

**How `wakecheck` finds a writer:** it sums every unmarked sleeper anyway and logs any that changed, with the words that changed. In a 64 MB build it keeps every sleeper's bytes, so the changed word's offset names the field, and the field names the writer.

**The proof** [E]: no unmarked change in 7.5 million unmarked-sleeper checks (b30 hands off and on its route, c40; wake135e); play and both seeds identical to the checksum build at every tick on a50, c10 and d20 (lab 38995738909).

**The speed** [M]:
- PSP-1000, b30's b113 route: frame −5.4 ms cinematic, −8.7 flight, −6.9 beach (−3.7 to −4.8%); tick −2.3 to −2.8 ms (38996729280).
- PSP-3000, a50: tick −1.6 ms (−6%) through the level; frame −2.2 to −5.8 ms (38996776422).

### WT.3 On by default in local games

**On by default in local games.** The keep words:
```c
		psp_wake_on = (psp_wake_check || psp_keep("wakeonwrite")) ? 2 : !psp_keep("wakeonwriteoff");
```
- `wakeonwriteoff`: every sleeper summed every tick, as b135. Added to preset 4 (the speed-ups off).
- `wakeonwrite`: on in networked games too.
- `wakecheck`: the check mode, local or networked. Both win over the off word.

**Why not in networked games yet:** the proof above is the campaign's. In a networked game, the network's own updates write into objects, and nothing has checked that each of those goes through a marked writer (`wake_on_write.md` §5 asks for a netplay run first). If one doesn't, a client-side sleeper would ignore the server's update until something else woke it. So a networked game sums as before; `game_connection()` is the engine's own local-or-network test, which b122's far-rate levers (`machinesfar`, `posehalf`, in the same file) already use for the same reason: a client simulates in step with a server. The multiplayer build's offline maps (no connection) get the lever.

**What it would take to turn it on there:** one multiplayer run on a live server with `wakecheck` in keep (no misses logged over a few minutes of play), then `wakeonwrite` on by default.

Also: the build number, the launch log's keep-word line, and the keep-word appendix in `optimization_ranking.md`.


## §Front The front door

The first screen is chosen in `psp_menu()` before anything else (`review_frontdoor.md` §1): a scripted run (`input.txt`, `nomenu.txt`) goes on as before, so the rigs see no difference; L held or `menu.txt` opens the launch menu; otherwise the game's own main menu, with `init.txt`'s `map_name` lines skipped so the launch menu's last map doesn't take over the boot (`FIXES_FRONT` `console.c#frontdoor`). A main-menu boot plays with `keep.txt`, or writes the first preset's words when there is none.

**The start-up** [M, PSP-1000]: the menu took no input for 56 s on frontdoor3, because its saved-game check rewrote the 26 default multiplayer game variants on every boot. Now a variant whose file already holds the same 124 bytes is skipped (`FIXES_FRONT` `playlist_profile.c#variants`): frontdoor4 and b136's own runs show 26 kept, 0 written. The check still takes **27 s** (lab 39016543583): comparing the 26 files alone takes 11.5–12 s, far more than the stick's known costs, and the one stick lock is not the cause (`stickparallel` changes nothing). That is the next start-up fix, not in b136.

## §Glow Glow maps, the showcase, and the screen blend

- **Glow maps** (`review_glowmaps.md`): the bake writes base × the multipurpose map's green per self-illuminated model shader; the GE adds that over the lit part, as the Xbox lights the base. The menu ring's white band goes.
- **The showcase** (ui.map only): the menu's own pack at full size and colour, its models at their most detailed level, and the geometry cache's spare RAM on the menu given to textures. 2.8 MB a frame, no frame cost [E]. It covers the 3D scene, not the 2D interface sheets: the UI's softness (Sheepdog01, 02:13) is the renderer session's next fix (`menuui`), not in b136.
- **Screened, not added** (`screenglow`, on; `glowadd` restores the add):
```c
	if (glow_add)
		sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF);
	else
		sceGuBlendFunc(GU_ADD, GU_FIX, GU_ONE_MINUS_SRC_COLOR, 0xFFFFFF, 0);
```
  Added, the ring's bright clouds clipped to white; screened, the glow's share shrinks as the frame nears white, in the same one pass. Against the PC reference the ring is 0.1% white screened, 17.5% added, 0.0% on the PC [E]: nearer the original, still brighter than it. The Xbox clamps lighting plus glow before the base multiplies it; that exact fix (`glowclamp`) is the renderer session's next step. **On by default as an improvement on b136's own add: Sheepdog01's call.**

## §Controls Controls and glyphs

`review_controls.md`: crouch toggles (`crouchhold` holds it), reload on a tap and action on a hold (`actiontap` for the engine's own), a nub page in the launch menu, and the port's own PSP button glyphs on the HUD prompts and menus (`xboxglyphs` for the Xbox's). It was written on b134 against ChupathingyCE v0.7.4e; every one of its anchors still applies on v0.7.5d (the fixup check in b136's builds).

## §Dither The dither moved each pass (off)

`review_dithershift.md`: the 16-bit frame rounds every blended pass, and since b117 those passes go undithered, which shows as bands and the levels' "oily" blotches. `dithershift` dithers them again with the pattern moved each pass so it can't stack into b116's checkerboard. On the PSP-3000 (lab 39010447206) [M]: the fixed pattern on b30's flight 7.1 → 2.2 levels, the blotches become an even grain, drawing within 0.3%. The shift's tests sit in the hot path, so they build only with `PSP_DITHERSHIFT=1`; the default build carries `ge_dither` +176 bytes and the probe's cold code. **Off: Sheepdog01's look call** (the release check's b30 shots with the switch; `ditherlift` overshoots the PC's brightness, so it stays opt-in either way).

## §MP The multiplayer work (multiplayer build only)

`review_mpb4.md`: ONLINE GAMES on ChupathingyCE 0.7.5d, network version 29 (joining 11 to 30), and the fix for the drop within ~30 s of every public game: the PSP's Wi-Fi driver threads (priority 0x27) got no time while the game thread caught up, so `netpace5` drops the game thread for 1 ms in every 5 while online [M, PSP-3000, lab 39009339059: a public game to the wall]. All in multiplayer-only files and `FIXES_INTERNET`; the campaign build differs only by mp5's crash string.

**Heap** [D, local build]: the multiplayer build's heap is 3,652 KB in b136. A 13-player game reached 3,716 of 3,720 KB on mpb4 and KCP crashed on a NULL allocation; b136's shared additions cost ~68 KB there. Before the release calls multiplayer working, either campaign-only data stays out of the multiplayer build or KCP gets its reserve (the multiplayer session's open bug 1).

## §Recon The censuses (not built by default)

`recon_134.md`: `sleepcensus`, `modelcensus`, `drawcensus`, `objtrace`, `mebusN`. `FIXES_RECON` is registered only with `PSP_RECON=1`, and the C hooks are `#ifdef PSP_RECON` or a macro that is empty without it; recon's local build showed `.text` and `.rodata` byte-identical without the switch. They are in b136 because they shift line numbers, and the profile must be counted after them.

## The merges

On `w136` (`9557c71`, WT), in order: `frontdoor`, `glowmaps`, `controls` (02:00), then `screenglow`, `dithershift`, `mp135`, `recon134` (03:00). The conflicts:
- `psp_fixups.py`, the fixup groups' registration line, every merge: both sides' groups kept; the multiplayer groups where `mp135` put them, `FIXES_WAKE`, `FIXES_FRONT`, `FIXES_CONTROLS` at the end, `FIXES_RECON` only with `PSP_RECON`. `FIXES_RECON` itself was inserted whole before `shelved()` (git's three-way split it across other groups).
- `psp_menu.c`'s header: the front door's text kept (`mp135` had removed its own `menu.txt` check for it); `psp_menu()` still checks the front door first.
- `psp_tex.c`: the showcase's pack choice kept, with `mp135`'s `PSP_MAPS_ROOT`.
- `psp_build.py`: the plain units (`psp_ge.c` and the rest) now take `-iquote psp/include`, `-DPSP_MULTIPLAYER`, `-DPSP_DITHERSHIFT` (for `psp_ge.c`) and `-DPSP_RECON` when those switches are set. Other switches (`PSP_DEBUG_TOOLS`, `PSP_TICK_STEPS`) still don't reach them; that's a fix for after b136 (the reviewer's note).

## Its own profile and code order

`pgo/campaign-b136.tgz`: 13 counting runs at `6d4807e` (b136's final code) on the b136 bake, with the default keep words (lab 39016449375, 39016477106; every run ok, strict clean). Built with `PSP_PGO_STRICT=1`: links with no function missing its counts. Code order: b132's kept layout, as b135.
