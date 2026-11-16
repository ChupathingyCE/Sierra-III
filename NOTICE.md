# Sierra-III: licence notice

Sierra-III, a native PSP port of Halo: Combat Evolved built on ChupathingyCE.
Copyright (C) 2026 The Sierra-III authors.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details. The licence's full text is in `LICENSE`.

## Why GPL-3.0-or-later

The engine this port builds on, ChupathingyCE, is GPL-3.0-or-later from its 0.8.0 release, and Sierra-III's pin is past that change (`UPSTREAM.md`). Every EBOOT built from this repository is a combined work with the engine. So Sierra-III takes the same licence for its own work, and the whole is conveyed under it.

## Earlier history stays MIT

Sierra-III's own work was published under the MIT licence (`LICENSES/MIT.txt`) up to the commit that adds this file. Copies taken from that earlier history keep the MIT licence. From this commit on, Sierra-III's own work is GPL-3.0-or-later. ChupathingyCE's own commits up to v0.7.5d stay CC0 1.0, as ChupathingyCE publishes them.

## What this licence does not cover

These files are not ours to license, or not under these terms:

1. **`psp/xmb/campaign/PIC1.PNG`**, the campaign's XMB wallpaper, is made with stills of our own Halo: Combat Evolved gameplay footage. It's used under Microsoft's Game Content Usage Rules (https://www.xbox.com/en-US/developers/rules) and isn't under the GPL. It may be shared only as those rules allow (not for commercial use). Halo: Combat Evolved (c) Microsoft Corporation. Sierra-III is not endorsed by or affiliated with Microsoft.

2. **The project's own look:** the Sierra mark (`psp/xmb/sierra_mark.svg`) and the XMB art made from it or for the project (`psp/xmb/campaign/ICON0.PNG`, `psp/xmb/mp/*.PNG`). Copyright (c) 2026 The Sierra-III authors, all rights reserved. They may be shared only as part of unmodified copies of Sierra-III. A fork picks its own look.

3. **Our patches to other GPL programs keep those programs' licences:**
   - `upstream/invader/*.patch`, changes to Invader: GNU GPL version 3 (Invader's licence);
   - `tools/ppsspp_local_patches.diff`, changes to PPSSPP: GNU GPL version 2 or later (PPSSPP's licence).

4. **Third-party files carried as they are, each under its own licence:**
   - `psp/xmb/fonts/Audiowide-Regular.ttf`: the Audiowide font by Astigmatic, SIL Open Font License 1.1 (`psp/xmb/fonts/OFL.txt`);
   - `psp/include/GLES2/`, `psp/include/GLES3/`, `psp/include/KHR/`: the Khronos Group's OpenGL ES and KHR headers, under the MIT or Apache 2.0 licence each names in its header;
   - `bake/bundle/licenses/`: the Rust standard library's licence texts, as the Rust project publishes them.

## The engine and the libraries

The engine (ChupathingyCE, GPL-3.0-or-later) is not in this repository. The build fetches it at the commit `upstream/pins.env` pins, and `psp_fixups.py` changes a copy of it. Its anchored engine text is the engine's, under its licence. `UPSTREAM.md` lists every upstream with its pin and licence.

A built release carries the notices of the libraries linked into it (`PSP/GAME/halo/licenses/`). Each bake bundle carries `LICENSES/` for the programs inside it. Each release's complete source is published in Sierra-III-Release, as the GPL asks of a release.
