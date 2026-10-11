# What Sierra-III is built on

Sierra-III doesn't copy anyone's source into this repository. It pins each upstream at an exact commit, and keeps our changes as our own files plus patches applied on top. The same pins are in `upstream/pins.env`, for scripts.

| Piece | Upstream | Pinned at | Our changes | Licence |
|---|---|---|---|---|
| **Engine (current base)** | [ChupathingyCE](https://github.com/ChupathingyCE/chupathingyce) (0.7.1d) | `b78e6cfa00d007592a21b12363b2cbc1b5217997` | None in the checkout: `psp_fixups.py` changes a mirror of it at build time | its `LICENSE.md` |
| **Engine (previous base, still builds)** | [halo-ce-universal](https://github.com/cybersecurity/halo-ce-universal) | `8fb1647e18a68e164529321bdcbe24557801f23b` | `upstream/halo-ce-universal/*.patch` (two commits, apply with `git am`), plus `psp_fixups.py` | CC0 (its `LICENSE.md`) |
| **Invader** (the map builder the bake uses) | [SnowyMouse/invader](https://github.com/SnowyMouse/invader) | `d43ce550db5b6b68ddea0d4c2080b616555e5279`; submodule `ext/riat` at `c1380cbfc708d89a3713af57b48d3f4368c34f8a` | `upstream/invader/psp-fork.patch` (apply with `git apply`) | GPLv3 |
| **Media Engine library** | [mcidclan/psp-media-engine-custom-core](https://github.com/mcidclan/psp-media-engine-custom-core) (me-core-mapper) | `727421b4d5f006f298f22451864a464eb0e7fd20` | None. `tools/build_release.sh` builds it with cmake into its cache (`PSP_MECORE` points `psp_build.py` at it) | MIT (shipped beside the EBOOT as `LICENSE-mecore.md`) |
| **PPSSPP** (the test harness only) | [hrydgard/ppsspp](https://github.com/hrydgard/ppsspp) | `tools/ppsspp_BASE_COMMIT.txt` | `tools/ppsspp_local_patches.diff`: PSP-1000 memory in headless, the strict interpreter, the cache model, the handle trace, the emulated RAM in a memfd (so instances run side by side in a container) | GPLv2+ |
| **Toolchain** | [pspdev](https://github.com/pspdev/pspdev) | psp-gcc 15.2.0 (psptoolchain-allegrex `a95b7da`, 2026-07-05), at `/opt/psp/pspdev` | None | various |

## Moving the engine pin

1. Check out the new commit.
2. Run `PSP_FIXUPS_CHECK=1 python3 psp_build.py`. It lists every fixup anchor that no longer matches, without building anything.
3. Fix those anchors in `psp_fixups.py`.
4. Build and run the emulator tests.
5. Update the pin above.

The rebase to ChupathingyCE is written up in `docs/chupa_rebase.md`.
