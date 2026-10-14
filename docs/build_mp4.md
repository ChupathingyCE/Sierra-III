# mp4: the multiplayer build in a folder of its own (2026-10-11)

Both builds on one stick, ready at once (Sheepdog01, 2026-10-11): no files swapped between the campaign's setup and the multiplayer tests. Also, the PSP follows ChupathingyCE's legacy table from a file. Otherwise mp3 (`build_mp3.md`).

## The build

| | |
|---|---|
| Name | `mp4` |
| Source | Sierra-III `mp-recon` `2d57041`, `PSP_MULTIPLAYER=1`, ChupathingyCE base |
| EBOOT | sha256 `e8c58a26d6c3f05e…`; `halo_psp_mp4.zip` (unzips to `PSP/GAME/halomp/`, over mp3) |
| Kernel modules | mp1's (unchanged) |

## What changed from mp3

**Its own folder, `ms0:/halomp`** (`psp/include/psp_paths.h`): `config.toml`, `keep.txt`, `init.txt`, `log/`, `save/`, `crash.txt`, `shots/`, the test files (`input.txt`, `commands.txt`, …), and `legacy.json`. The campaign build keeps `ms0:/halo` exactly as before.

| | Campaign build | Multiplayer build (mp4 on) |
|---|---|---|
| Settings, keep words, log, saves | `ms0:/halo` | `ms0:/halomp` |
| Maps (`.map .pgeo .ptex .pstn .vram`) | `ms0:/halo/maps` | `ms0:/halo/maps`, the same files |
| Starts on | the launch menu | the game's main menu (the launch menu with `ms0:/halomp/menu.txt`) |

- The maps stay shared: one bake to keep up to date. The engine's `d:\maps` goes to `ms0:/halo/maps` (`psp_fixups.py`, `xbox_files.c`), the rest of `d:\` (`init.txt`, `debug.txt`) to `ms0:/halomp`.
- The crash handler module still writes its one start-up line to `ms0:/halo/log/crash_handler.txt`; the crash report itself goes to `ms0:/halomp/crash.txt`.

**The legacy table** (`psp_chupa_platform.c`): the PSP reads a table file once, unsigned, as a PC reads its local override: `network.legacy_table`'s file, or `legacy.json` in `ms0:/halomp`. Only its row for wire `chupa-24a` is read. With ChupathingyCE's published table there (announce 25, join 11–25), the PSP joins a server that announces 25 as well as one pinned to 24. Without the file, the built-in numbers (24, 11–24), logged once: `network: ms0:/halomp/legacy.json: no legacy table; the built-in numbers …`.

**In the emulator** [E] (`sierra-netplay` 38116299502): settings read from `halomp`, the maps from `halo/maps`, joined and played 5,863 ticks.

## Install (once)

```fish
pspmount
unzip -o ~/Downloads/halo_psp_mp4.zip -d $psp                 # PSP/GAME/halomp
mkdir -p $psp/halomp
cp $psp/halo/config.toml $psp/halomp/config.toml               # the network settings (broadcast = "<hotspot-address>")
echo "nearauto nomeaudio assettrack geo2 sound1024" > $psp/halomp/keep.txt   # mp3's whole game's words
curl -sL https://raw.githubusercontent.com/ChupathingyCE/chupathingyce/delta-table/legacy.json -o $psp/halomp/legacy.json
umount $psp
```

- `legacy.json` is ChupathingyCE's published table, as the PCs have it.
- The campaign's folder isn't touched; the restore (`build_mp1.md`, D) is its own.
- To join: Multiplayer → System Link → `sierra-mp1`. The scripted join is still there (`network_test = "join"` in `ms0:/halomp/config.toml`).

## Logs

```fish
pspmount
tar czf $HOME/psp/logs/mp4_test1.tgz -C $psp/halomp log -C ~/games/halo-server debug.txt
umount $psp
```
