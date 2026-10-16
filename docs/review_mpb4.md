# Review: mpb4, the multiplayer work on b135 (branch `mp135`)

What changed since mpb3 (`7f0dc04`, on w131), in the order to read it. mpb4 is `mp134` (b134 + the multiplayer work) moved onto b135 (ChupathingyCE `v0.7.5d`), plus one fix from the PSP-1000's first public game (`milestone_first_public_game.md`). Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

## 1. Onto b135

`mp134`'s own changes (`w134..mp134`) applied onto `w135` as one three-way patch.
- **Conflicts:** two. `psp_sdl.c` takes both includes (b135's `strings.h` for `SDL_strncasecmp`, the multiplayer build's `psp_paths.h`). The fixups' registry line is b135's, with `FIXES_MULTIPLAYER`, `FIXES_PACKET_ALIGNMENT` and `FIXES_INTERNET` after `FIXES_CHUPA` as before.
- **Anchors:** every multiplayer fixup still matches `v0.7.5d`.
- **The campaign [D]:** built from `mp135` and from b135's `5950c5e` (no profile, same engine), the code is the same instruction for instruction. The one difference is mp5's crash-screen string (`ms0:/halo/crash.txt`), which moves later data by 4 bytes, as in every multiplayer branch since mp5.

## 2. What the new engine needed (`FIXES_INTERNET`, `psp_build.py`, `psp_limits.h`)

- **Network version 29**, the pin's, joining 11–30 (`psp_limits.h`). Today's public servers announce 29.
- **`p2p_copies.c`** joins `INTERNET_UNITS`. Upstream's lobby now shows a game heard under several network versions once, at the newest (`p2p_listing_older_copy`). The ONLINE GAMES screen's own pass over the list (mpb3, change 1) goes.
- **`#joinedhost`:** `network_game_client_joined_host_identifier` stays inside `HALO_GAME_BROWSER`. Its record of the joined host is written only by the web game list (to join again after a map download), and the PSP's `#invite` fixup had brought the function into the PSP's build without it.
- **`#downloaded`:** the downloaded Custom Edition maps' list goes from 512 names to 8 (`map_families.c`). The PSP downloads none, and the list and its text copy took 64 KB of heap.
- **Heap:** 3,724 KB (mpb3: 3,744).
- **Stubbed:** `map_download_get`, `map_download_start`, `map_family_find`, `browser_screen_open_map_offer`, `text_hires_enabled`. Each caller reads 0 as "no" [D]: Custom Edition maps aren't found, no download starts, no map offer shows, no high-res text.

## 3. The tunnel's thread above the game thread (`#priority`, `psp_internet.c`)

**Why:** in the first public game (sierra-hw 38918999662) the host's messages stopped within about 10 s of the join, and the client gave up.
- Internet play does all its network work on one thread (`p2p.c`). It starts at priority 0x5c [E: mpb4's log], far below the game thread's 0x20.
- The PSP's scheduler runs a thread only while every higher one waits [D]. Behind a game it couldn't keep up with, the game thread ran 1–2.5 s of catch-up ticks at a time without waiting, so the tunnel likely got no time [U].

**The fix:** the thread raises itself to 0x1f as it starts: above the game thread, below the vertical blank thread (0x1e). It waits in `select()` between packets and KCP's clock, so it takes only what it needs: 0.0% of the CPU in mpb4's emulator runs [E].

```c
	(void)unused;
#ifdef HALO_PSP
	{
		extern void psp_internet_thread_priority(void);
		psp_internet_thread_priority();
	}
#endif
	pthread_mutex_lock(&p2p_lock);
```

**Keep word:** `tunnelslow` leaves it at 0x5c.

## Open

- Whether the raised thread keeps a public game alive on the PSP-1000 (`build_mpb4.md`).
- The tick in a 12-player game (43 ms against the host's 33.3) and why 30 biped updates a tick.
