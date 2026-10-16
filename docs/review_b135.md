# Review: build 135

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). b135 is b134 (`review_b134.md`) on ChupathingyCE `v0.7.5d` instead of `v0.7.4e`: 118 upstream commits, 28 files of the game's source. No speed work and no layout changes, so b135 differs from b134 only by upstream's changes and what the port needed to take them. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

| Branch | What | Kind | Default | Review |
|---|---|---|---|---|
| `w135` | The engine pin: `v0.7.5d` (`upstream/pins.env`, `UPSTREAM.md`) | upstream | on | §1 |
| `w135` | What the port needed: two fixup anchors, the new draw limits, `SDL_strncasecmp`, the watermark off | port | on | §2 |

## Release check

Lab 38925953425, against b134 as built (`2b9b41d`): **play and both seeds identical at every tick** on d20, a50 and c20 (12,000 each) [E]: upstream's changes leave the campaign's simulation as it was on these stretches. b30 and c40 shots right; the multiplayer build plays Blood Gulch. The release build (lab sierra-build 38925960715, `PSP_PGO_STRICT=1`): 0 functions without their counts. The program's code is 10 KB larger (3,470,452 bytes against 3,460,012), and the heap 12 KB smaller (5,644 KB against 5,656).

## §1 What upstream changed that reaches the PSP [D]

Most of the 118 commits are desktop code the PSP doesn't build (the server browser, text chat, map downloads, Discord, the 64-bit Android build). Of the game's source:

| Change (upstream's commit) | Files | On the PSP |
|---|---|---|
| **Checkpoints wait in game ticks, not frames** | `main.c` | A restoration: a checkpoint waits up to 240 ticks for a safe moment, checking every 10, as on the Xbox. It counted frames before; at the PSP's ~2 ticks a frame it waited about twice as long as the Xbox |
| **New campaign profiles start at the beginning**, as on the Xbox | `player_profile.c`, `player_ui.c`, `hs.c` | The game's own profile menu; the launch menu picks the PSP's maps |
| **Weapon HUD: the second magazine's overlays no longer replace the first's**; a weapon's HUD state cleared first | `hud_weapon.c`, `weapons.c` | A fix to what the HUD shows. The clear is our RB1 (b126), now upstream's; §2 |
| **No shield damage sound while an overshield drains** | `hud_unit.c` | A fix |
| **Field of view setting and an optional match timer** | `game_engine.c`, `main.c` | The desktop's settings; the PSP's defaults are unchanged |
| **Network version 29**; Delta games matched by wire; the unreliable queue keeps a datagram's source port; no Running Riot without a spree; co-op respawns after everyone has died | `networking/`, `game_engine.c`, `players.c` | Multiplayer. Hosts of 29 accept clients from 11 to 30; the port's own number (`psp_limits.h`) is the multiplayer session's |
| More transparent geometry, particles and lights a frame **on the desktop** | `render.h`, `rasterizer.h`, `object_lights.c`, `render_particles.c` | The PSP keeps the Xbox's (§2) |
| The build's version in place of the Xbox's build number; high-res text; Escape closes the console; Change Name no longer crashes | `main.c`, `rasterizer_text.c`, `console.c`, `ui_widget*.c` | The watermark is off on the PSP (§2); high-res text is the desktop's (off); the rest are the desktop's menus and console |
| Custom Edition maps' versions, the reason a map is refused | `cache_files.c` | Multiplayer map handling; the campaign's map loading is unchanged |

## §2 What the port needed

**Two fixup anchors** followed upstream's text:
- `platform.h`: upstream's Android condition is now `defined(HALO_ANDROID) && !defined(HALO_64BIT)`; our PSP block around it is unchanged.
- `weapons.c`: upstream now clears the weapon's HUD state, as our RB1 did. The fixup keeps `weaponstateold` working by skipping upstream's clear:
  ```c
  #ifdef HALO_PSP
  	if (psp_weapon_state_zeroed < 0)
  		psp_weapon_state_zeroed = !psp_keep("weaponstateold");
  	if (psp_weapon_state_zeroed)
  #endif
  	csmemset(state, 0, sizeof(*state));
  ```

**The new draw limits** (`psp_capacity.h`): transparent geometry groups 384, particle groups 512, lights 128, the Xbox's, as upstream keeps them for its Android guest and Xbox builds. The desktop's 2048, 2048 and 512 are per-frame arrays the PSP's heap can't spare.

**`SDL_strncasecmp`** (`psp_sdl.c`): the settings parser (`port_config.c`) now compares names and `true`/`false` with it. Without a definition the link's stub would return 0, and every name would match.

**The build-version watermark off** (`psp_fixups.py` FIXES_137): it is drawn from `build_identity.c`, desktop code the PSP doesn't build. `text_hires_enabled` stays a link stub returning 0: the desktop's high-resolution text, off.

## Its own profile and code order

The ten openings and b30's routes, counted at `b384588` (lab 38924533502, 38924539926): every run played through, no crash, no bad access under the strict interpreter. Linked in b132's kept code order, as b133 and b134.
