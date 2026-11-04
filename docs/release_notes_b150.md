# Sierra-III b150: release notes

> **Draft until Sheepdog01's hand test passes.** One thing is still open before it's published: the known issues Sheepdog01's test adds. The bake's default sound profile is settled: from 0234b610 the bundle bakes `--sound s9full` by default, the profile every b150 test used. The release session fills the bake bundles' names and hashes at the cut.

**Sierra-III b150** is Halo: Combat Evolved's campaign on the PSP. It is built for the PSP-1000, and the PSP-2000 and 3000 work too. It contains nothing from the game: you bake the maps yourself, from your own Xbox disc, on your own PC. The full source of this release is in this repository.

**How to play:** follow [`docs/play.md`](docs/play.md) step by step. It covers custom firmware, imaging your disc, the bake, the Memory Stick, the first launch, and what to send when something goes wrong. The short version is at its top.

**The files:**

| File | What |
|---|---|
| `halo_psp_b150.zip` | The game. It goes to the bake tool, which puts it on the Memory Stick's layout |
| `halo-psp-bake-windows-b150.zip` | The bake tool for Windows 10 or 11 |
| `halo-psp-bake-linux-b150.tar.gz` | The bake tool for Linux x86-64 with **glibc 2.39 or later** (Ubuntu 24.04, Debian 13, Fedora 40, current Arch) |

Each file's SHA-256 is beside it on the release page. There is no macOS bake tool with b150.

**Requirements:**
- a PSP with custom firmware (it's tested on ARK-4 on 6.61);
- a Memory Stick with 3.5 GB free;
- your own disc image of Halo for the original Xbox. It's tested against NTSC version 01.10.12.2276: the bake checks every map and refuses other versions and languages rather than guess;
- a PC to bake on, with about 8 GB free.

**Bake again with b150's tool.** Three things in the game need the new bake:
- **the music at the disc's own quality:** the bake now leaves the levels' music as the disc has it, 44.1 kHz stereo, instead of halving it to 22 kHz, and the game mixes it at that rate;
- **stand-ins for textures still loading:** each texture's tiny 4 x 4 copy goes in its pack, so a surface whose texture hasn't arrived yet is drawn in roughly its own colours instead of blank;
- **the models converted for the PSP as the bake goes** (new since b148, as b149 had it): it ends a dropped frame at the start of *The Silent Cartographer* and gives the game more memory.

The maps grow a little: about 40 MB for the converted models, and the music takes about twice its old room. b150 still plays a bake made with b147h's or b148's tool, without those three. If you baked with b143's tool, you must bake again: the game stops and asks for a new bake if it finds those maps.

**A second bake is faster.** The bake keeps its reusable work (texture palettes and converted sounds, about 420 MB) in your user cache folder, so baking again takes about a third of the time. Deleting the folder is always safe.

**What's new since b148** (b149 was a release candidate that wasn't released, so its changes are here too):

*Faster:* on a PSP-1000, against b148 on the same test (each level's opening, second half of its first two minutes), *Truth and Reconciliation* runs 61% more frames a second, *The Library* 28%, *343 Guilty Spark* 20% and *The Silent Cartographer* 19% (table below). The work behind it:
- the models are drawn after the scene's walk instead of one by one inside it, and lit in a pass of their own first, so each step's code stays in the PSP's small caches;
- the Media Engine, the PSP's second processor, now does the AI's thinking while the main CPU draws, works out what's in view, and holds part of the sound cache in its own memory;
- the flashlight's lighting reuses the work its pieces share (*Keyes*' swamp with the flashlight on: about 10% faster);
- a dozen exact speed-ups in the game's own logic: objects that can't collide are filed after the walk, scenery poses come from tables, objects that hardly moved skip their search of the level, and the most-used data and code are laid out to fit the caches.

*Fixed:*
- A crash in *The Silent Cartographer* when an enemy found a body that had just been removed.
- The Media Engine could stop in *Keyes* while an enemy planned a long path, after which the game ran slower for the rest of the level and some sounds went silent. It no longer stops there; if it ever stops, most sounds now carry on.
- In *The Pillar of Autumn*'s cutscenes, shadows filled the frame's drawing list, so lens flares and shadows dropped out and once a hole showed.
- The near Pelican no longer drops out for a frame at the start of *The Silent Cartographer* (bake again).
- The boarding pods in *The Pillar of Autumn* no longer lose their textures and draw purple and pink.

*Also new:*
- The music is the disc's, at full quality (bake again).
- The bake copies its log to the Memory Stick (`halo/log/bake/bake-log.txt`), so one log archive carries everything a report needs (`docs/play.md`, step 8).
- Three new launch menu presets (hold L as the game starts; presets 18 to 20) turn the new changes off in groups: if something looks or sounds wrong, they tell which change it comes from, and a report that says so helps.

**How fast:** the whole campaign, all ten levels, on a PSP-1000 at its standard 333 MHz. It doesn't run at full speed yet. Measured on a PSP-1000 over the second half of each level's first two minutes, nobody at the controls, one run each (so differences of a few percent are noise):

| Level | Frames a second, b150 | b148 |
|---|---:|---:|
| *Assault on the Control Room* | 30 | 30 |
| *Truth and Reconciliation* | 30 | 18 |
| *The Maw* | 30 | 30 |
| *Two Betrayals* | 28 | 26 |
| *The Pillar of Autumn* | 21 | 21 |
| *343 Guilty Spark* | 21 | 17 |
| *The Library* | 17 | 13 |
| *Keyes* | 14 | 14 |
| *Halo* | 13 | 13 |
| *The Silent Cartographer* | 8 | 7 |

**Multiplayer isn't part of this release.**

**The performance readout** (frame rate and timings) is off. Turn it on with `overlay` in the launch menu (hold L as the game starts), or from the in-game debug menu.

**Known issues:**

- **Speed:** see above. The heaviest stretches are slow, and the slowest levels play in slow motion.
- **The AI thinks every other tick** to save time, so enemies react a little more slowly than on the Xbox (since b140; `aicad3off` in the launch menu turns it off, at a cost in speed).
- **Some animations look a little off**, a few are cut short, and enemy limbs can vanish around a death.
- **Cutscene audio** can drift from its picture; *The Pillar of Autumn*'s second bridge cutscene runs much slower than the first.
- **The screen can go black for a moment** when a level loads its next section, and fringes can flicker in the very heaviest scenes.
- **Not yet like the Xbox:**
  - lens flares show through walls, and some level lights pop on and off, both in *The Silent Cartographer*'s bunker;
  - parts of some characters' armour (the Chief, the Elites, the Grunts, the crew) can draw white where their colour should be;
  - in *The Pillar of Autumn*: the bridge's panels are dark, with a shadow seam across its floor; some colour casts; the tutorial's lights don't change colour; textures can be missing as the lifepod's ending pans;
  - after a level, the main menu can draw untextured;
  - the shield bar's lower half is darker, and its recharge sound isn't heard;
  - the Pelican's canopy glass is lighter than on the PC, and its side panes stay dim;
  - there's a slight missing polygon at *The Silent Cartographer*'s far horizon;
  - some lights on world geometry are missing.
- **Sleep:** the power switch doesn't sleep the PSP while the game runs. Quit to the XMB first, then sleep it there (`docs/play.md`). Holding the switch for about 8 seconds still turns the PSP off.

**Build it yourself:** the EBOOT's SHA-256 starts `29620d204385bb82`. On Linux with the pspdev toolchain (`docs/toolchain.md`):

```
PSPDEV=/opt/pspdev LABEL=b150 \
PSP_METRICS=0 PSP_LTO=1 PSP_PGO=use PSP_PGO_PROFILE=campaign-w161a PSP_PGO_STRICT=1 \
PSP_PGO_EXTRA=-fno-unroll-loops,-fno-peel-loops,-fno-tracer,-fno-ipa-cp-clone \
PSP_TEXT_LAYOUT=w161a_tgc90 PSP_DATA_LAYOUT=hotg161a tools/build_release.sh <output folder>
```

**Licences and credits:** Sierra-III's own code, scripts and documents are under the MIT licence (`LICENSE`). The end of `LICENSE` lists what MIT doesn't cover:
- the XMB wallpaper and the project's own look in `psp/xmb/`;
- our patches to GPL programs;
- third-party files carried as they are.

The engine, the bake's tools and the ideas borrowed from other ports are credited in `UPSTREAM.md` and the README. New in this release: mcidclan's VME library and sample (MIT, `psp/src/vme/` with its licence), built in but off in play.

Sierra-III was created under Microsoft's "[Game Content Usage Rules](https://www.xbox.com/en-US/developers/rules)" using assets from Halo: Combat Evolved, and it is not endorsed by or affiliated with Microsoft. Halo: Combat Evolved © Microsoft Corporation.

Free, and never for sale. Don't share your baked maps or your disc image.
