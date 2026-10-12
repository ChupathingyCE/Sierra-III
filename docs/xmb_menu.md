# XMB menu: art, sound and the stick tree

Plan, 2026-10-12. Nothing past the placeholders is built yet.

## What goes where

The XMB shows five optional files packed into `EBOOT.PBP`. Each comes from a different place, decided by who owns it:

| Slot | Content | Source | Path |
|---|---|---|---|
| `ICON0.PNG` | Still tile | Ours | Committed, `psp/xmb/<build>/` |
| `ICON1.PMF` | The ring, spinning (silent loop) | Footage Sheepdog01 records from PC ChupathingyCE | Encoded once by Sheepdog01, shipped |
| `PIC0.PNG` | Sierra-III PSP mark ("PSP" under the III) | Ours | Committed |
| `PIC1.PNG` | The Pillar of Autumn | Footage still from PC ChupathingyCE | Shipped |
| `SND0.AT3` | A clip of the score | The user's own disc | Added by the bake, never shipped |
| title text | "Halo: Combat Evolved…" | `PSP_TITLE` → PARAM.SFO | Build |

Why the split:
- Recorded footage is what Microsoft's Game Content Usage Rules cover; anything shared carries their notice (see the README).
- The score is extracted game data, and its ownership is not public (the composers' 2020 royalty suit against Microsoft settled on undisclosed terms). The bake reads it from the user's disc, the same way maps are made. This is the decomp-port precedent (e.g. Ship of Harkinian): share code, generate assets locally.
- Bake-first is reversible. Music can always be shipped later; a shipped copy can't be taken back.

## The stick tree

Today the user copies two things: the release's `PSP/GAME/halo/` and the bake's `halo/`. The bake will instead write one tree that mirrors the stick:

```
<output>/
  halo/maps/...               the baked maps (as now)
  PSP/GAME/halo/EBOOT.PBP     the release EBOOT, with SND0 added
```

The user drags the output's contents onto the stick root. That's one copy, and the music comes with it.

### How the bake adds SND0

1. Find the clip by **tag path**, not byte offset. Tag paths are the same across releases; offsets aren't.
2. Decode it to 44.1 kHz stereo `.wav`, trim it to a short loop, and normalise the volume (the XMB plays it the instant the entry is highlighted).
3. Encode it to ATRAC3 with `atracdenc`, built during the bake's first-run setup.
4. Repack the EBOOT. A PBP is a header of eight offsets followed by its sections, so swapping SND0 means slicing the sections and writing a new header. It's plain Python with no PSP toolchain, and the program section is untouched.

**Best-effort:** if the encoder can't be built or the clip can't be found, the bake warns and packs the EBOOT without sound. Sound must never be why an install fails.

## Open items

- [ ] Sheepdog01 records the ring loop and the Pillar shot from PC ChupathingyCE.
- [ ] Pick the clip: which track, and where its loop points sit.
- [ ] Check on hardware that `atracdenc`'s ATRAC3 plays in the XMB [U until then].
- [ ] Encode `ICON1.PMF` (UMD Stream Composer under Wine; Sheepdog01's machine only).
- [ ] Final Sierra-III PSP mark replaces the placeholder PIC0.
- [ ] Set `PSP_TITLE` for both builds.
- [ ] Add the GCUR notice to README.md.
- [ ] More disc releases for `hashes.json` (only NTSC 01.10.12.2276 today).
