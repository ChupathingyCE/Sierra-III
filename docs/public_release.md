# The public release: how it's made

The public repository (`Sheepdog-01/Sierra-III-Release`) is a separate repository, not a mirror of this one (ruling of 2026-10-19, replacing `go_live.md`'s "same hashes" plan). Its history starts fresh: one commit per release, each a copy of the tree `tools/public_export.py` builds from that release's commit here. It holds what a player and a porter need, and nothing personal or about the test setup: no names but the owning account's, no machines, consoles, network details, or the lab and its runs.

## Making it

```
tools/public_export.py <commit> /tmp/public-bNNN --pgo campaign-bNNN --gitleaks <gitleaks>
```

- `<out>` must not exist and must not be inside a git work tree.
- `--pgo` names the profile the release was built with (`pgo/<name>.tgz`); only that one goes out. Left out, the newest `campaign-bNNN` goes.
- `tools/public_export.py - x --report` reads the work tree as it is on disk and lists every finding, writing nothing: the quick loop while fixing text.
- gitleaks: any v8 (`gitleaks dir`); checked with 8.21.2.

It refuses, and writes nothing, when a file kept has
1. a deny-list match (`DENY` in the tool): a person's name or address, a home path, a place, a machine (the desktop, the AI box, the laptop and their specs), a console's ID, the development infrastructure (the lab, its runners and workflows, the coordination files), a USB port, a network detail (addresses, the hotspot's names, MACs), an Actions run number. It reads text files line by line, binaries' readable strings, and the names and contents of archives (`pgo/*.tgz`). Third-party licence texts may carry their authors' addresses (`ALLOW`);
2. a Markdown link to a file left out;
3. a refused file type (game data, firmware, build output);
4. a secret, by gitleaks.

**A refusal is fixed here, in the source text, never in the exported copy:** neutral wording ("a PSP-1000", "the tester", "the maintainer's call", "b102's run 3"), run numbers dropped. Dates stay. Code comments change without moving a line, so `__LINE__` and the profile's line checksums stay put.

Backticked citations of files left out (`review_b126.md`, say) are listed, not refused: the public README says the notes cite development documents that stay here.

## What goes out (RULES in the tool)

Every tracked file must match a rule, so a new folder fails the export until it has one.

| In | Why |
|---|---|
| `psp/`, `psp_build.py`, `psp_fixups.py`, `psp_*.h`, `upstream/`, `tools/build_release.sh` | the build |
| `pgo/<the release's profile>.tgz`, `psp/layout/` | the release's own optimisation inputs, so its EBOOT can be rebuilt byte for byte |
| `bake/`, `.github/workflows/bake-bundle.yml`, the five tools the bake runs (`texpack.py`, `stencilbake.py`, `dez.py`, `lightmap_masks.py`, `bspread.py`), `tools/tag_groups_check.py` | the bake and its bundles |
| `tools/psp_glyphs.py` | draws `psp/glyphs/` |
| `tools/emu.sh`, `strictfilter.py`, the PPSSPP pin and patches, `bench/` | testing in the emulator (`toolchain.md`), and the benchmarks behind the hardware reference |
| `LICENSE`, `UPSTREAM.md`, `.gitignore`, `.gitattributes`, `public/README.md` (as `README.md`) | the repository's face |
| `docs/play.md`, `toolchain.md`, `for_other_ports/`, `port_overview.md`, `asset_baking.md`, `bake_changes.md`, `psp_hardware_reference.md`, `release_notes_bNNN.md` | the player's and the porter's documents |
| `docs/research/research_vita.md`, `research_halo64.md`, `chant_provenance.md` | provenance: what was borrowed, under what grant |

| Out | Why |
|---|---|
| `CLAUDE.md`, this document, `tools/public_export.py` | the development process (the tool's deny-list names what must not go out) |
| `README.md` | the development README; `public/README.md` is the public one |
| the rest of `docs/` | development history: build notes, hardware reports, reviews, candidates, plans, recon, handoffs, multiplayer setup (not in the release) |
| the rest of `tools/` | analysis tools for the development runs |
| older `pgo/` profiles | each serves only the build it was counted for |

## The public history

The first release, after the maintainer approves the tree:

```
tools/public_export.py <commit> /tmp/public-bNNN --pgo campaign-bNNN --commit-message "Sierra-III bNNN"
```

makes `/tmp/public-bNNN` a repository with that one commit, authored and committed as `Claude <noreply@anthropic.com>` (`PUBLIC_IDENTITY`: the author of the code, with no personal address); push it to the new repository by hand. Each later release: export into a new folder, copy it over a clone of the public repository (`rsync -a --delete --exclude .git <export>/ <clone>/`), and commit it as the same identity, one commit per release:

```
git -C <clone> add -A
git -C <clone> -c user.name=Claude -c user.email=noreply@anthropic.com commit -m "Sierra-III bNNN"
```

Nothing in the public tree names this repository's commit; the release notes give the build's hash.
