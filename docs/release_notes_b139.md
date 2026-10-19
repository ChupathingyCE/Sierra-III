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

<!-- TODO(master): known issues from build_b139.md and the hand test -->

**Licences and credits:** Sierra-III's own code is under the licence in `LICENSE`. <!-- TODO(master): once go_live 4.1 lands, name it (MIT, psp/xmb/ carved out) --> The engine, the bake's tools and the libraries carry their own licences (`UPSTREAM.md`); each bake bundle lists its components in `LICENSES/README.txt` and carries the source of its GPL and LGPL parts in `source/`. The game zip carries the Media Engine library's licence (`LICENSE-mecore.md`). README's Credits name the ports and fonts we borrow from.

Sierra-III was created under Microsoft's "[Game Content Usage Rules](https://www.xbox.com/en-US/developers/rules)" using assets from Halo: Combat Evolved, and it is not endorsed by or affiliated with Microsoft. Halo: Combat Evolved © Microsoft Corporation.

Free, and never for sale. Don't share your baked maps or your disc image.
