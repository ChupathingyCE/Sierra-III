# Review: the Media Engine after a sleep, second try (`mewake2`)

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). Same shape as `review_mewake.md`, which this follows: **what failed**, **the safety change**, **the exit**, **the restart's next try**, **what the PSP must test**, **the log spam**, **the emulator's evidence**. Labels: [M] measured on a PSP, [E] emulator, [D] read in code or in the build, [U] inference.

Branch `mewake2`, from `w125` (`d18a1d0`).

| Change | Kind | Default | Keep word |
|---|---|---|---|
| The ME's restart after a wake | fix, now opt-in | **off** (the ME stays stopped after a sleep, as in b122-b123) | `mewake` turns it on; `mewakeoff` still wins over it |
| The restart's next try: the ME's bus clock on, its caches cleared before it reads anything, 300 ms to answer, its progress and the system registers logged | fix (only with `mewake`) | off | `mewake` |
| The exit leaves the ME halted or held in reset, never running, and logs the system registers | safety | on | none (it only adds a hold where a release may have left the ME running) |
| The ME's reset bits set and cleared alone, not by writing the whole reset register | safety | on | `meresetwhole` (the whole-register writes, as before) |
| The system registers at launch, sleep, wake, release and exit, in the log | recon | on | none (reads only) |
| The lit items' sleep log: per item, on a change of reason, 4 lines at most | log fix | on | none (log only) |

## What failed [M]

b124, Sheepdog01's test 1 on the PSP-1000 (`b124hw/test1/log/log.txt`, 1,611 lines):
- the sleep and wake: "sleep: awake; 6 files reopened, 0 at a new descriptor, 0 failed" (line 1,372);
- the restart: "ME: restart after the wake FAILED (no idle poll within 100 ms of the reset's release); held in reset, the main CPU runs its jobs" (line 1,389). Every heartbeat after it says "ME: stopped … no jobs", so the main CPU mixed the audio (13.5% of it) from then on;
- the exit: the log's last line is "HOME: exiting", and the PSP hung: no crash file, a battery pull to get it back.

Unrelated to the ME but in the same stretch: two `debug.txt` asserts from the cache reader (`cache_files_windows.c` #1387-1388, line 1,347) as the PSP went to sleep, a read failing as the stick went away. The reopen at the wake dealt with it, as in b122.

## The exit: where it hung

**Our side got all the way to the firmware [M + D].** `exit_game` stops the ME, then logs "HOME: exiting", then flushes the log, then calls `sceKernelExitGame`. The line is on the stick, so the flush ran, and the next call is `sceKernelExitGame`. Every wait before it has a limit: the ME stop 100 ms, the log's lock 100 ms while exiting.

**After that, the firmware reboots into the XMB and boots its own ME image, and that boot waits for the ME without a limit [D].** uOFW's reading of `me_wrapper` (6.60): `module_start` → `sceMeBootStart(2)` → `sub_00001C30(image, wait=1)`, which releases the ME's reset and then loops `while (*0xbfc00700 == -3) sceKernelDelayThread(1000)` until the ME's boot code answers. An ME that can't run hangs the XMB there, with no crash: what Sheepdog01 saw.

**So the likeliest story is one cause for both failures: after this sleep, the ME couldn't run [U].** Candidates, most likely first:

1. **The ME ran with uncleared caches and wrote noise over memory [U, strongest].**
   - A sleep powers the PSP's chip down, the ME's caches with it [U: the usual reading of the PSP's standby; the firmware's ME wake re-boots the ME from scratch, which fits].
   - The firmware clears both of the ME's caches first thing in every ME boot: `TagLo = TagHi = 0`, then `cache 0x1` across the instruction cache and `cache 0x11` across the data cache, before any read through them (uOFW `me_wrapper/init.S`) [D].
   - The library's start handler doesn't (`meLibHandler`, `me-core.h`) [D]. At launch that's fine: the firmware's boot cleared them, and they hold the firmware's own lines since.
   - Our ME code's first act is a write-back of the whole data cache (`meLibOnProcess`: `meLibDcacheWritebackInvalidateAll`) [D]. On noisy caches, every line that happens to read "valid and dirty" is written to whatever address its tag happens to name, which can be in the kernel's memory or in hardware registers. An address that names nothing gives a bus error, which takes the ME into an exception handler it doesn't have yet. Either way it never gets to its first idle poll, and what it wrote can hang the next thing that reads it, the reboot included.
   - The library's own `sleep-wake` sample would survive this by luck: its ME loop counts in an uncached word and touches no cached data, so nothing is ever written back [D].
   - This one is b124's alone: b122 never released the ME after a sleep.
2. **The firmware's boot itself can't start the ME after this sleep [U].** The library's `kinit` replaced the firmware's ME sleep handler (`SceMeRpc`), so none of the firmware's wake steps ran: at `0x10005` it turns the AVC power back on if it was on before the sleep, turns the ME's bus clock on and boots the ME; at `0x1000f` and `0x100000` it settles the AVC power [D, uOFW]. The reboot's `sceMeBootStart` does the bus clock and the boot again, but not the AVC power. If one of those steps matters, **b122 would hang the same way**: this can't be ruled out from here.
3. **Our reset writes released other blocks mid-sleep [U].** The sleep handlers wrote the whole reset register (`0xbc10004c = 0x14`), in b122 too. That holds the ME and also lets go of every other block the firmware's drivers held in reset at that moment, mid-suspend and mid-resume (the memory stick's reset is bit 8, the AVC's bit 5). The firmware's own driver sets and clears one block's bits at a time [D, uOFW `sysreg_driver.c`]. This branch does the same now (below).
4. **The firmware's ME sleep handler now points into our program [D, unlikely].** `kinit` overwrites the handler pointer and keeps no copy. After the exit unloads the game, the pointer names freed memory until the reboot replaces the list. But this is so on every exit, and exits without a sleep have worked since b86 [M], so it isn't what changed.

**Could b122 hang the same way?** Through candidate 1, no: b122 never released the ME after a sleep. Through 2 or 3, yes. Sheepdog01's b122 sleep tests never exited afterwards, so it's open. Test 1 below answers it on this build, whose default is b122's behaviour.

**What the library doesn't offer.** It has no way to give the ME back:
- `kinit` doesn't keep the firmware's handler, so there is nothing to put back before `sceKernelExitGame`;
- the firmware's `sceMeBootStart` won't boot its image again while it believes it is running (`meStarted` is already 2).

Putting the handler back would need a small library change: `kinit` returning the old pointer, and a call to restore it. That is a change for mcidclan's library, and only worth asking for if the tests below point at candidate 4.

## The safety change

**1. The restart is opt-in** (`psp_me_wake_poll`). Without `mewake`, the wake logs "ME: stays stopped after the wake (the restart is opt-in: mewake); the main CPU runs its jobs", as b122-b123 behaved. `mewakeoff` still turns it off, even with `mewake`.

**2. The exit leaves the ME as the firmware's boot expects it** (`psp_me_exit`, called by `exit_game` in place of `psp_meaudio_stop`, which is gone):
- the stop as before: the ME asked to halt, up to 100 ms, held in reset if it doesn't;
- then a hold if a release may still have it running (`me_maybe_running`: set at launch and at a restart's release, cleared when the ME is seen halted or is held). It matters if HOME lands while a restart waits for the ME; on an ordinary exit the ME has halted, and nothing changes;
- the system registers logged against launch's: "ME: system registers (reset, bus clocks, clocks, AVC power) at exit … (marks …); launch …". If a hang comes after that line, it's in the firmware, and the line says the state the firmware got the ME in.

**3. The ME's reset bits alone** (`reset_hold`, `reset_release`): `|= 0x14` and `&= ~0x14`, with interrupts off around them, as the firmware's sysreg driver does. Used by the sleep and wake handlers, every hold, and the restart's release. The launch's release is still the library's `meLibReset` (a whole-register write); it has worked since b86 and is left alone. `meresetwhole` brings back the whole-register writes everywhere.

**4. The registers, logged** (`struct me_sysregs`, `sysregs_read`): resets `0xbc10004c`, bus clocks `0xbc100050` (the ME's is bit 0), clocks `0xbc100058`, AVC power `0xbc100070`, and the two start marks (below). They are read at launch (a log line after "ME: the ME runs"), in the kernel's sleep and wake handlers (reads only), around the restart, and at exit. After a wake the heartbeat prints them all on one line, under the wake's line.

## Why the restart failed, and the next try (with `mewake`)

The next try does, in order, what the firmware's own ME wake does and the library skips (`me_restart_kernel`, kernel mode):
1. reads the registers (the "release" column);
2. holds the reset and turns the ME's bus clock on (`0xbc100050 |= 1`). The firmware does this before every ME boot (`sceSysregMeBusClockEnable`) and turns it off before a sleep; the library's handler never touches the main CPU's copy of it;
3. writes the ME's reset vector `0xbfc00000` as two pieces:
   - `psp_me_wake_stub`: the firmware boot code's two cache loops, copied instruction for instruction, sized from the ME's own Config register, with a mark before and after;
   - straight after it, the library's handler (`_me_section`, 276 bytes in this build; the stub is 100). The disassembly shows the handler has no address of its own in it: one relative branch, one absolute jump to `meLibOnPreProcess` [D, `psp-objdump` of this build], so it runs the same 100 bytes along;
4. writes back and drops the main CPU's caches, and lets go of the reset's two bits.

The main thread then waits up to **300 ms** (was 100) for the first idle poll.

**The marks: how far the ME got [new].** Two words of the reset SRAM (`0xbfc006c0`, `0xbfc006c4`), away from the library's and the firmware's words, are written by the ME as its start goes on:

| Mark | Written by | Meaning |
|---|---|---|
| `57a90001` | the stub, first instruction | the ME runs: power, clock and reset are fine |
| `57a90002` | the stub, after the caches | the caches are cleared |
| `57a90003` | `meLibOnProcess`, first line | through the library's handler (its VME wait, the jump) |
| `57a90004` | `meLibOnProcess`, after its write-back | into its loop |

A failure now says how far the ME got, for example "FAILED (no idle poll within 300 ms of the reset's release; the ME's start got nowhere: not its first instruction (power, clock or reset))". The marks are written at launch too (the last two), so the launch line shows the mechanism working: launch should read `… (marks xxxxxxxx 57a90004)`.

**What each answer would mean:**

| The log says | Then |
|---|---|
| restarted, marks `…0002 …0004` | the caches or the clock were it; candidate 1 or 2 confirmed for the restart |
| failed, "nowhere" (`…0000`) | the ME doesn't run at all: power or clock. Compare the wake column's bus clocks and AVC power with launch's |
| failed, `…0002` and no `…0003` | stuck in the library's handler: likeliest its wait on `0xbcc00010` (the VME block), which may want the AVC power the firmware's wake turns back on |
| failed, `…0003` and no `…0004` | died in the first write-back: the caches weren't the whole story |

## What Sheepdog01's PSP must test

In this order. Pull the logs after each run. Tests 1 and 3 can hang the PSP at the exit, so do them last in a session, with a battery pull in mind; the log reaches the stick before the hang.

1. **Default, real sleep, then exit** (no keep words added): play a minute, sleep with the power switch for ~10 s, wake, play 30 s, then HOME → exit. **Stop:** the XMB is back, or the PSP hangs (then pull the battery).
   - Look for "ME: stays stopped after the wake (the restart is opt-in: mewake)", the registers line under it, and the exit's registers line.
   - If the exit **hangs**, the restart isn't the cause (none ran), so it's candidate 2 or 3. b122 would very likely hang too.
   - If it **returns**, b124's hang came from the restart's release (candidate 1, or the ME left half-started), and the default is safe.
2. **The restart without a sleep, on the rig** (`mewaketest mewake`, `psp=psp1000`, about 3 minutes; no hands). It proves the stub and the moved handler start an ME that hasn't slept: expect "ME: restarted after wake in N ms" with marks `57a90002 57a90004`. If this fails, the stub itself is wrong, and test 3 is pointless until it's fixed.
3. **`mewake`, real sleep, then exit**: as test 1 with `mewake` in keep.txt. Read the wake's line, the stage, and the registers. Then HOME → exit, last.

**Time:** test 1 about 3 minutes. Test 3 the same, plus the battery pull if it hangs.

## The log spam

`psp_lit_item_seen` (`psp_fixups.py`, `FIXES_123L`) printed a lit item's line whenever its reason changed, remembering one item per set of 64 (`absolute_index & 63`). Weapons #909 and #973 share set 13, so each pushed the other out and both printed every tick: 342 of the log's 1,611 lines, and 360 lit-item lines in all [M]. Log lines are stick writes, and streaming waits behind them (b122 test 14 [M]: the logging thread held the stick 7.7 s in one run).

Now:
- each set remembers two items (`last[64][2]`, the older one replaced);
- an item prints only when its reason changes, at most `PSP_LIT_ITEM_LINES` (4) times;
- the 400-line cap on the whole log stays.

The per-tick counts in the object report don't change. The other `sleep:` lines in this log were one, the wake's. The rest of the sleep code's lines are behind check words (`itemsleepcheck`, `sleepdebug`).

## Proven in the emulator

Batch `mewake2a` (lab run 38682898490), this branch's `33cf9a8`. The ME was simulated, with audio, slant and rays on it, as in `mewake123a`. The emulator runs no ME code and has no sleep: `mewaketest` drives everything but the kernel calls (the stop, the sleep's hold as the kernel handler does it, the jobs on the main CPU, the wake's request, the wait for the clients, the counters and the switch back). `me_restart_kernel`, the stub, the marks and the register reads are the PSP's to prove.

**The default and the opt-in [E]:**

| Run | The wake's line |
|---|---|
| d20 and a50, `mewaketest` (the default) | `ME: stays stopped after the wake (the restart is opt-in: mewake); the main CPU runs its jobs`, then "ME: stopped … no jobs" to the end, as after a b122-b123 sleep |
| d20, `mewaketest mewake` | `ME (simulated): restarted after wake in 31 ms (0 frame ends waiting for its clients)` |
| a50, and the a50 strict run, `mewake` | `… in 21 ms` |
| Blood Gulch, multiplayer build, `mewake` | `… in 15 ms` |

**Play exact** (`statecmp.py`, statecrc hands off, `fixedticks fixedseed`): the play hash and both random seeds are the same at every common tick:

| Pair | Common ticks |
|---|---|
| d20, `mewake` against the default | 21,486 |
| d20, `mewake` against no test | 21,384 |
| a50, `mewake` against the default | 36,000 |

The whole-state hash differs only in the allocations known to follow the emulator's speed (objects, contrail point, object looping sounds; on a50 also actor and first person weapons), as in `review_mewake.md`. It differs from the test's stop on, where the jobs' cost moves between the "ME" and the main CPU.

**The rules across the switch** (strict interpreter, `gemesim mewrites`, the stop and the restart inside the stretch):
- **a50** (rays, `perccheck`), 2,974 ticks:
  - `MESTRICT: no system calls in work meant for the Media Engine`;
  - ME rays: 7,298 answered, 7,169 taken and checked, **0 mismatched**;
  - `MEWRITES`: 23 lines written by both sides, all in `0x09fb…` (the main thread's stack, where the emulator runs the jobs), as before.
- **b30** (slant with `meslantcheck`, `meringtest`), 1,527 ticks:
  - no system calls;
  - slant: **0 of 793,414** triangles differing, 0 fallen back;
  - ringtest: **0 wrong of 5,720 jobs**, 0 failed;
  - `MEWRITES`: 7 lines by both, all in `0x09fb…`.
- **Multiplayer:** the build compiles, starts Blood Gulch, restarts and plays to game time 6,341. No failure lines but the known load-time `memalign` one.

All 10 runs: segv 0, strict 0 sites, no crash file.

**The lit items' log [E].** The most lines one item printed in a run: 2 on d20 and a50, 4 on b30 (weapon #973, at the cap). The b30 strict run printed 21 lit-item lines in 1,527 ticks.

**The exit after a faked sleep returns [E]** (batch `mewake2b`, lab run 38688506641, on the build runner). d20 with `statecrc mewaketest exitat25`: the test's stop at 450, the wake at 540, the exit at 750 ("exitat: exiting"), and the emulator ended the run itself after 21 s of wall:
- the default: "ME: stays stopped after the wake (the restart is opt-in: mewake)", then the exit;
- `mewake`: "ME (simulated): restarted after wake in 31 ms", then "ME (simulated): stopped (exitat)" and the exit.

The emulator can't show the PSP's hang (the firmware's reboot and ME boot aren't emulated), only that our exit path no longer waits on anything after a faked sleep.

**A stop that isn't this branch's [E].** In `mewake2a`, the two exit runs without `statecrc` stopped logging at game time 482, right after "SOUND CACHE BLOWN", and ran into their wall. `mewake2b` shows the same stop at the same line on w125 (`d18a1d0`, `mewaketest exitat25`) and on this branch without `mewaketest`. So it's w125's: d20 hands off with `nearauto assettrack geo2 fixedticks fixedseed` and no `statecrc` stalls after the sound cache overflows at 482. The `statecrc` runs, slower per tick, get past it. It's left for its own look: a sound-cache stall could be a hang on the PSP too.

## What doesn't change [D]

- **Without a sleep or `mewaketest`,** the launch adds one kernel call that reads four registers and two SRAM words, and one log line. The ME's code adds two uncached stores to SRAM at its start. The exit adds a register read and a log line.
- **The ME can't reach the new code.** `mewalk.py` on this build (`meLibOnProcess` as root, `--syms`) reaches none of `psp_me_exit`, `me_restart_kernel`, `psp_me_wake_poll` or the stub, and finds no system call; its other lines are the replay job's known ones.
- **Play is the same with and without the restart:** it only moves the ME's jobs between the CPUs, and they are exact either way (the statecrc pairs below).
- **Multiplayer:** nothing in the simulation changes; the build compiles (below).

## Risks

- **The bus clock bit [U].** Setting it is what the firmware does before every ME boot, but the main CPU's copy has stayed as the firmware left it until now. If it was on already, nothing changes.
- **The stub on a CPU we can't test in the emulator [U].** The loops are the firmware's own ME boot code, so the ME runs them at every boot. What's new is running them ahead of the library's handler. Test 2 checks this without a sleep.
- **A restart that still fails** costs a 300 ms freeze at the wake (was 100). The ME is then held in reset, and the exit leaves it there.
- **The reset bits alone** change what the sleep and wake handlers write by default. Only the ME's two bits move now; b124 also moved every other block's. If something relied on that (nothing known), `meresetwhole` brings it back.
