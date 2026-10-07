# Build 103: what changed and why (2026-10-07)

Built from b102's hardware day (Sheepdog01's PSP-1000, runs 2–6). Labels: **[M]** measured, **[E]** emulator only, **[U]** untested inference.

## 1. File handles: the Library crash

**What b102 run 3 showed [M].** The crash photo's s1 was `0x80020320`, `SCE_KERNEL_ERROR_MFILE`. The firmware's iofilemgr hands out at most 64 descriptors, files and directories together (uofw `iofilemgr.c`, `alloc_iob`: `g_iobCount >= 64`). A directory open was refused. libcglue's `open()` returned the error code itself, newlib's `opendir` only tests for -1, and `readdir` indexed the descriptor table with it. The emulator's trace of the same route peaked at 12 handles, so whatever uses them up happens on hardware only.

**b103:**
- **Handle table** (`psp_trace.c`): every `sceIoOpen`/`sceIoDopen`/close/dclose, with the path and when it opened. The stick report adds a line: `files: N handles open (peak P), opens failed (for lack of handles), closes failed, handed out twice`. A close that fails is logged, since a handle the kernel kept is invisible otherwise.
- **Out of handles:** the first refused open with MFILE writes the whole table to the log. One handle (`log/reserve.txt`) is held from start-up and given up at that moment, so the log, which opens its file for every batch, can still be written.
- **`open()` guard** (`--wrap=open`): any negative result becomes -1 with errno, so `opendir` returns NULL and the game sees a missing file instead of crashing.
- **`handletest`** (launch menu): 15 s in, opens files until the kernel refuses, then closes them. It tells you how many descriptors were free [E: 50 free in PPSSPP with 11 of the game's open].

## 2. Crash handler

- The handle reserve is given up before `crash.txt` is opened, and a failed open is logged with its code instead of "written".
- The log gets every register on one line, then the handle table.
- `badvaddr` is marked stale for bus errors (the CPU only sets it for address errors), and a fault in a branch delay slot is flagged.

## 3. The display list

**What b102 showed [M].** Run 3: 0 level draws in the Flood fight with 340 KB of the list in use. Run 6: about 1,100 draws skipped every frame for half the run.

**b103:**
- **By kind:** each draw entry says what it is, and the list's growth is charged to it. A report every 300 frames gives KB a frame for each kind, the fullest frame, its skipped draws, and the order the kinds came in.
- **Finding [E, The Library]:** the order is `M q L …`: models and queued see-through come **before** the level. So enough bodies can fill the list before the level gets a turn, which explains run 3's empty level.
- **The level's reserve:** until the level's first draw of the frame, every other kind stops 96 KB earlier (`nolevelreserve`: off). The report counts the draws it held back.

## 4. The flashlight

**Why it was weak [M/E].** The Xbox measures the light's falloff per pixel (a 32³ attenuation texture: `rasterizer\distance attenuation`, read from c20's tags; its curve is smoothstep-like and slightly darker mid-range than the port's (1 − d²/r²)²). The port measures at triangle corners. The flashlight is radius 6 units (about 18 m), cone 20°/45° (`flashlight_cyborg.light`).

**b103:** a spotlight's triangle with an edge over a fifth of the radius is split into four, recursively: at most 3 levels, finer near the light (pieces about half their distance from it), and pieces outside the cone dropped (`nolightsplit`: as before).
- [E] In the emulator, the floor pool shows where it didn't before.
- Cost: renderer CPU 19.9 → 22.9 ms with the flashlight on, at a spot facing a large floor. The first version (4 levels, no cone pruning) cost 5.3 ms. Hardware cost to be measured.

## 5. Smaller items

- `scePowerTick(PSP_POWER_TICK_ALL)` from the heartbeat during play: no backlight dimming, no auto-sleep.
- `commands.txt`: says "none" when the file is missing. b102 run 6 logged nothing either way. The emulator logs the line with b102 too, so the file wasn't on the stick at that launch.

## 6. Benchrun: level vertices in VRAM

**The question.** b102 run 5 [M]: the GE's main-RAM reads cost the CPU 8–18 ms a frame. Texels are none of it. Halving the level's vertex bytes (b87, b93) also changed nothing. If the cost is the number of scattered vertex fetches rather than their bytes, only taking the fetches off the main bus will show it.

**b103:** a 9th switch, "level vertices in VRAM": the level's vertex copies are built in 256 KB of VRAM below the bench room, and the VRAM texture cache gives up that 256 KB under benchrun. The RESULT line adds the KB used and any copies refused.
- [E] Blood Gulch: 173–214 KB, 0 refused; the level draws correctly from VRAM.

**Reading it:** if "level vertices in VRAM" recovers what "GE skips the level" saved (3–7 ms), the fetches are the cost, and model vertices in VRAM is the next step. If it doesn't, the cost is elsewhere in a draw.

## 7. Test plan (hardware)

1. **The Library, `assettrack` + `geo2` + `hwprofile` + `handletest`**, with `commands.txt` (deathless). Play into the Flood fight.
   - The `files:` lines show whether handles climb; `handletest` gives the free count at 15 s.
   - The display-list report shows what fills the list, and whether the level now survives the fight.
   - Check the flashlight on floors, and the frame cost with it on and off.
2. **Blood Gulch benchrun**: the 9-switch table, especially switch 8 against switch 3.

## b104 (same day): the b103 load hang

**b103 run 1 [M]:** The Library hung at load, after the stencil bake's 161 KB allocation. HOME and sound still worked. The screen got the line `heap: memalign 161 KB…`, but the file didn't, so the game thread had written it to the screen and then stuck opening the log for it. The heartbeat then waited forever on the log's lock, which the game thread held. The log's open goes through the stick lock, the kernel and b103's handle-table lock; the table lock waited without a timeout. Code review finds no lock cycle; the leading inference is a crash at load while that lock was held. The handler's report is drawn on the GE's frame buffers, which aren't on screen while loading, and it needed the same lock to write `crash.txt`. The emulator doesn't reproduce it.

**b104:**
- **The handle table's lock** waits at most 200 ms, and doesn't wait at all after a crash.
- **The log's lock** waits at most 500 ms in play (100 ms after a crash or HOME).
- **The stick's lock** waits in 2 s steps.
- **On a timeout or long wait**, a `LOCK: … held by <thread>` line goes to the screen, taking no lock and touching no file. A `locks:` line follows in the log.
- **The crash handler** limits its own lock waits first, and when the loading screen is the one showing, draws its report there and stops the log drawing over it.
