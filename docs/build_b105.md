# Build 105: what changed and why (2026-10-07)

Built from b104 run 1 (build_b103.md, last section). Labels: **[M]** measured on hardware, **[E]** emulator, **[U]** inference.

## 1. File descriptors

**The limit [M, b104 run 1]:** descriptors 3–12 were in use and the 13th was refused (`SCE_KERNEL_ERROR_MFILE`). The PSP hands out the lowest free number, so the game can hold about ten files at once, not the 64 uofw's table suggested. Where that limit lives (the FAT driver, the firmware, ARK-4) is still [U].

**b105 holds fewer:**
- `exit.txt` is gone. It answered its question in b90 (Kommander's Exit never calls the exit callback).
- `stdout` shares `stderr`'s stream instead of opening `stderr.txt` a second time.
- The game's `debug.txt` lines go into the log as `debug.txt: …` lines (`psp_fixups.py`, `errors.c`), and `gamestate.txt` isn't written: the heartbeat's "game state:" survey already has every pool with its peak.
- The reserve descriptor is taken back at the next heartbeat after it was used, quietly (through the real open, so a retry isn't counted as a failure).
- The `files:` line splits failed opens into "for lack of handles", "not there" and "otherwise".
- `handletest` creates its file first, then opens it read-only. b104 asked for read-only plus create in one call, which the PSP's FAT driver refuses (`0x80010016`).

[E, PPSSPP with `PSP_MAXFD=13`, a new local patch that hands out descriptors below 13 only] The Library holds 6 descriptors in play (stderr, savegame, map, geometry pack, texture pack, reserve). The only refusal is `handletest`'s own, by design: it found 4 more free.

## 2. The chain that starved b104

- **Geometry cache placement:** it now asks the texture code for the size of the pack that will actually be used (`psp_tex_pack_bytes`, the asset track's when chosen). A failed stat is retried twice, logged, and counted as "too big", so the cache leaves the heap. A wrong guess that way costs texture caching; the other way it starved the heap.
- **The controller:** `SDL_GetGamepads` returns a fixed list instead of a malloc on every pad read. When b104's heap ran dry, the 8-byte malloc failed and the game saw no gamepad.

## 3. Memory

- **Debug-only data out** (lever 3): `PSP_DEBUG_TOOLS=1` in `psp_build.py` keeps them; the shipping build leaves out `meclassify`'s buffers (68 KB; the menu hides the option) and shrinks the game's render-debug cache from 512 entries to 16 (29 KB → about 1 KB).
- **The display list out of the program's zeroed data** (lever 1): the heap grows from 4,928 to **5,376 KB**. The list (384 KB) is taken when the GE starts, from free user RAM outside the heap when a block that large is free there (keeping 128 KB spare), else from the heap.
  - [E] The emulator has 575 KB free outside the heap but no block over 307 KB, so there it comes from the heap: net gain about 64 KB. The log's `GE: display list` line shows how the free space lies on the PSP.
  - Taken first thing at start-up instead, the block landed where the tag window is placed later at a fixed address, and the map couldn't load [E]. Hence: only when the GE starts.
- **Heap failures** are counted, logged one by one only for the first 64, and shown on screen (`HEAP FULL: N requests failed`), overlay on or off. A `heap:` line every 5 s gives use, free space and its number of pieces.

## 4. A latent crash: number formatting across threads

[E] With two emulators running side by side, The Library crashed once in newlib's `_dtoa_r` (the float-to-text converter), reading `0x00333033` (text used as a pointer). pspdev's newlib keeps one state for all threads (a single `_impure_ptr`), and `_dtoa_r` keeps scratch memory there and returns a string the next call frees. The heartbeat (priority 0x18) can interrupt the game thread mid-`%f` and format its own float. **b105 wraps the printf engines, `vfprintf`, `strtod` and the scanf path's `_strtod_r` in one lock** (`psp_libc.c`; recursive for its holder, skipped after a crash). Four more side-by-side runs after the fix: clean.

## 5. Instruments

- **Bus-priority probe** (`hwprofile`): `halo_profile.prx` gains a read-only system call that returns a fixed list of the DDR, GE, EDRAM and system-controller registers (psp_hardware_reference.md §9.1; the flush, the hardware mutex and the soft interrupt are left out). Read at start-up (GE idle) and 10 s into play (GE busy): `bus probe (...)` lines. Skipped in the emulator. **The new EBOOT needs the new `halo_profile.prx`** (shipped together).
- **Pool, stack and Direct3D buffer peaks:** already logged (`game state:`, `stacks`, `dynamic geometry, frame peaks`); runs on every mission give lever 2 its evidence.
- From the b105 dev tree, untested in a real fight yet: **corpse sleep** (`corpsesawake` turns it off) and the **checkpoint diagnostic** (scan time apart from write time).

## 6. Emulator results [E]

| Run | Ticks | Result |
|---|---:|---|
| The Library, Sheepdog01's b104 keep words, descriptor limit 13 | 5,291–5,744 | no crash, 0 heap failures, heap top 4,716 of 5,376 KB, 6 descriptors |
| Two Library runs side by side (after the formatting lock) | 5,736 each | clean |
| The Library + Blood Gulch side by side | 5,741 / 4,855 | clean |
| The Library, strict interpreter (unaligned, range and FPU checks) | 554 | 0 findings, through `handletest` |
| b104, same conditions, for comparison | 5,297 | 14 refusals for lack of handles (the shortage reproduced; its timing differs from hardware) |

## 7. Hardware test plan

1. **The Library**, `assettrack geo2 hwprofile handletest`, deathless. Play into the Flood fight and stay a while. Look for: no `HEAP FULL` on screen; controls alive; `handletest:` in the log; the `GE: display list` line; both `bus probe` lines; the `files:` lines.
2. If the fight runs: **corpse sleep** shows in the `objects_update` line's asleep counts (`corpses`).
