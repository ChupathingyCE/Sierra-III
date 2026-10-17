# Review: the loading screen's picture (branch `loadingscreen`, on `w137`)

The GE side of the loading screen: the three entry points `psp_loading.c` calls (the master's interface, `psp/src/psp_loading.h`). A restoration of the Xbox's picture, as far as it goes without `loading.tga`. Labels: [M] PSP, [E] emulator, [D] code or data, [U] inference.

## What the Xbox draws (ChupathingyCE at the pin, `source/interface/progress_bar.c`) [D]

- `progress_bar_render`, each frame: the target cleared to black; the picture **on the screen** (the front buffer, as a texture) drawn back over it through a 4-tap blur (taps 0.1 texel apart), on a quad 1/128 of the half-screen past each edge, sampling texels 3 to 637 of 640 across and 0 to 480 down, weighted 0.9 (`SRCALPHA, ONE` over black). So the last picture zooms out a little each frame and fades: a trail. Above 0.9 of the progress the weight falls as 0.9 − 9 × (p − 0.9), to 0 at the end.
- Then `loading.tga` (320 × 240) times a 128 × 16 mask made in code (sin across, sin^0.75 down, a border of 5/255), the mask sliding from off the left to off the right as the progress goes (`progress*768 − 128`), at the brightness sin(pπ)^0.9, added.
- `loading.tga` is the disc's `maps\loading.tga`, copied to the utility drive at start-up (`progress_bar_initialize`).
- `capture_frame` is set only when no scenario is loaded (a cold start): the screen is cleared first, so the trail starts from black. From the menu or a level, it starts from the last frame.
- The 40 cloud layers and the noise texture are unused (the master's 05:15 read).

## On the PSP (`psp/src/psp_ge.c`, "the loading screen")

- `psp_ge_loading_capture`: the buffer on the screen, as is: the loading frames draw into the other buffer and read this one, so nothing is copied (no heap).
- `psp_ge_loading_frame` (phase 0): opens its own frame, draws the trail, finishes, syncs and swaps, as a game frame's end does. A cold start (`captured` NULL) draws black frames, as the Xbox's clear.
- `psp_ge_loading_veil` (phase 1, inside the settle's game frame, before its swap): flushes the world's see-through draws first (`list_hud_phase`), so none lands on top, then draws the same trail opaque (src × weight + dst × 0) over the level's first view. The view stays hidden, and the screen ends on black at progress 1, as the Xbox's did.
- `loading_trail`: 15 sprites 32 pixels wide, the Xbox's mapping in the PSP's pixels (x' = 2.25 + (x + 1.875) × 634/645, y' = (y + 1.0625) × 480/483.75). One bilinear tap stands for the 4-tap blur [U: taps 0.1 texel apart differ from one tap by less than the 16-bit frame shows]. Undithered: the trail feeds on itself and a dither pattern would build up. The state the frame's drawing expects is restored after (as `buffer_state(0)`).
- Keep words: `loadingplain` (black frames, no trail), `loadshotsN` (a screenshot every Nth loading frame, for tests). The master's `loadingoff` turns the whole screen off.

## Not yet

- **The `loading.tga` band.** It needs the bake to carry the disc's `maps\loading.tga` into the Memory Stick's maps folder (game data: local only), and 150 KB for the picture while it shows: the depth buffer is free during the load but not during the settle; a 160 × 120 copy (38 KB) or 8-bit (77 KB) are the candidates. Next build.
- The Xbox's wobble on the progress (±0.05, a 21 s period) is the master's side (`view.progress`).

## Cost

No hot code: it draws only during a load and the settle. `loading_trail` 1,020 bytes, the three entry points ~0.6 KB [D, psp-gcc 15.2.0 -O2].
