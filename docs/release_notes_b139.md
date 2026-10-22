# Sierra-III b139: release notes (draft)

The draft of the text for b139's GitHub Release. The master fills the TODOs when the release is named (`go_live.md` 1.3).

---

**Sierra-III b139**, the first public release: Halo: Combat Evolved's campaign on the PSP, built for the PSP-1000 (the PSP-2000 and 3000 work too). It contains nothing from the game: you bake the maps yourself, from your own Xbox disc, on your own PC.

**How to play:** follow [`docs/play.md`](docs/play.md) step by step: custom firmware, imaging your disc, the bake, the Memory Stick, the first launch, and what to send when it goes wrong.

**The files:**

| File | What | SHA-256 |
|---|---|---|
| `halo_psp_b139.zip` | The game: unzip at the Memory Stick's root | <!-- TODO(master) --> |
| `halo-psp-bake-windows-b139.zip` | The bake, Windows 10 or later | <!-- TODO(master): from its .sha256 --> |
| `halo-psp-bake-macos-b139.tar.gz` | The bake, macOS 11 or later (Apple silicon and Intel) | <!-- TODO(master) --> |
| `halo-psp-bake-linux-b139.tar.gz` | The bake, Linux x86-64 (glibc 2.28 or later) | <!-- TODO(master) --> |

**Bake again for this release.** b139's bake makes palettes and wall decals differently (`docs/bake_changes.md`); maps baked for earlier builds won't do.

**What it is:** the whole campaign, all ten levels, on a PSP-1000 at its standard 333 MHz. It doesn't run at full speed yet: the heaviest stretches are slow (README, "Where it stands"). **Multiplayer isn't part of this release.**

**Known issues:**

- **Speed.** Measured on b139's release candidate (each map's opening, the PSP-3000, which measures within 3% of the PSP-1000): *The Maw* runs at 24 frames a second, *Two Betrayals* 21, *Assault on the Control Room* 17, *The Pillar of Autumn* 16, all at about the game's full 30 ticks a second. The rest run slower than real time where the fighting is heavy: *The Silent Cartographer*'s opening flight at 4.5 frames and 9 ticks a second (under a third of real time), *Keyes* 7 and 15, *Halo* 8 and 15, *343 Guilty Spark* 9 and 18, *The Library* and *Truth and Reconciliation* 11 and 22.
- **Sleep.** Quit before you put the PSP to sleep (`docs/play.md`).
- **Now and then a frame draws something without its texture** while that texture is read from the Memory Stick (about 120 frames over the ten openings).
- **Looks not yet like the Xbox:** the far ocean on *The Silent Cartographer* is tinted violet; the Pelican's canopy glass is lighter than on the PC; the floor glass in *The Pillar of Autumn* shows no reflection.

<!-- TODO(master): add what tonight's hand test finds (the XMB-to-menu time and sleep are in play.md's own TODOs) -->

**Licences and credits:** Sierra-III's own code, scripts and documents are under the MIT licence (`LICENSE`). Its end lists what MIT doesn't cover: the XMB wallpaper and the project's own look in `psp/xmb/`, our patches to GPL programs, and third-party files carried as they are. The engine, the bake's tools and the libraries carry their own licences (`UPSTREAM.md`); each bake bundle lists its components in `LICENSES/README.txt` and carries the source of its GPL and LGPL parts in `source/`. The game zip carries the Media Engine library's licence (`LICENSE-mecore.md`). README's Credits name the ports and fonts we borrow from.

Sierra-III was created under Microsoft's "[Game Content Usage Rules](https://www.xbox.com/en-US/developers/rules)" using assets from Halo: Combat Evolved, and it is not endorsed by or affiliated with Microsoft. Halo: Combat Evolved © Microsoft Corporation.

Free, and never for sale. Don't share your baked maps or your disc image.
