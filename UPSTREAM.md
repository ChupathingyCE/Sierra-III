# What Sierra-III is built on

Sierra-III doesn't copy anyone's source into this repository. It pins each upstream at an exact commit, and keeps our changes as our own files plus patches applied on top. The same pins are in `upstream/pins.env`, for scripts.

| Piece | Upstream | Pinned at | Our changes | Licence |
|---|---|---|---|---|
| **Engine (current base)** | [ChupathingyCE](https://github.com/ChupathingyCE/chupathingyce) (release `v0.9.1a`, 2026-11-16) | `2e5eac00e3d6655e094fbbcd13253ac2b10ee773` | None in the checkout: `psp_fixups.py` changes a mirror of it at build time | GPL-3.0-or-later (its `LICENSE`; CC0 1.0 up to v0.7.5d) |
| **Invader** (the map builder the bake uses) | [SnowyMouse/invader](https://github.com/SnowyMouse/invader) | `d43ce550db5b6b68ddea0d4c2080b616555e5279`; submodule `ext/riat` at `c1380cbfc708d89a3713af57b48d3f4368c34f8a` | `upstream/invader/psp-fork.patch` (apply with `git apply`); `upstream/invader/no-editor.patch` (Qt6 and SDL2 only for the tag editor, so the bake bundle's build needs neither) | GPLv3 |
| **atracdenc** (the bake's encoder for the XMB menu sound) | [dcherednik/atracdenc](https://github.com/dcherednik/atracdenc) | `cb170709f836cb43dc0312455188c791159d81dc`; submodule `src/lib/libgha` at `00a95243254cfb1e43b05888ebc68e9aa401507a` | None (`bake/bundle/build_tools.sh` builds it on a static libsndfile 1.2.2, WAV only) | LGPL-2.1 (libsndfile: LGPL-2.1+) |
| **Media Engine library** | [mcidclan/psp-media-engine-custom-core](https://github.com/mcidclan/psp-media-engine-custom-core) (me-core-mapper) | `727421b4d5f006f298f22451864a464eb0e7fd20` | None. `tools/build_release.sh` builds it with cmake into its cache (`PSP_MECORE` points `psp_build.py` at it) | MIT (shipped beside the EBOOT as `LICENSE-mecore.md`) |
| **PPSSPP** (the test harness only) | [hrydgard/ppsspp](https://github.com/hrydgard/ppsspp) | `tools/ppsspp_BASE_COMMIT.txt` | `tools/ppsspp_local_patches.diff`: PSP-1000 memory in headless, the strict interpreter, the cache model, the handle trace, the emulated RAM in a memfd (so instances run side by side in a container) | GPLv2+ |
| **Toolchain** | [pspdev](https://github.com/pspdev/pspdev) | psp-gcc 15.2.0 (psptoolchain-allegrex `a95b7da`, 2026-07-05), at `/opt/psp/pspdev` | None | GCC: GPLv3, its runtime (libgcc) under the GCC Runtime Library Exception. Linked into the EBOOT: pspsdk (BSD-3-Clause), newlib's libc and libm (BSD-style, `COPYING.NEWLIB`), pthread-embedded (LGPL-2.1); their texts are in the toolchain's `psp/share/licenses/` |

**Our patches carry their upstream's licence.** `upstream/invader/*.patch` (GPLv3) and `tools/ppsspp_local_patches.diff` (GPLv2+) are changes to those programs, so they stay under those programs' licences, whatever licence Sierra-III's own code takes.

## Moving the engine pin

**The pin is past ChupathingyCE's GPL change** (the maintainer's decision, 2026-11-11: the rebuild is based on ChupathingyCE's latest release). ChupathingyCE is GPL-3.0-or-later from the commit that changed its licence (it ships as 0.8.0); v0.7.5d and the commits before it stay CC0. From this pin every EBOOT is a GPLv3+ combined work: Sierra-III's own files keep their MIT licence, a release conveys the whole under GPL-3.0-or-later, and `psp_fixups.py`'s anchored engine text is GPL text. Sierra-III-Release already publishes each release's source, which the GPL asks of a release.

1. Check out the new commit.
2. Run `PSP_FIXUPS_CHECK=1 python3 psp_build.py`. It lists every fixup anchor that no longer matches, without building anything.
3. Fix those anchors in `psp_fixups.py`.
4. Build and run the emulator tests.
5. Update the pin above.

The rebase to ChupathingyCE is written up in `docs/chupa_rebase.md`.

**halo-ce-universal is retired** (2026-10-13). ChupathingyCE became independent of OpenCE on Oct 9 and tags its releases; halo-ce-universal (OpenCE) moves fast and away from the original. Its pin, its two patches and the fixups' universal halves left the tree in b125; the builds made on it stay reproducible from their own commits, which still carry them. **Pin ChupathingyCE to its release tags** (`vX.Y.Z`), not to commits between them. The interim pin on the 0.9.1 test build's commit `6eef2f3` (2026-11-11) ended at 0.9.1's release: the pin is `v0.9.1a` (2026-11-16, docs/chupa_rebase.md, "v0.9.1a").
