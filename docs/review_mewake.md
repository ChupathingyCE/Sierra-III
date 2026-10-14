# Review: the Media Engine started again after a sleep (`mewake`)

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). The same shape as the other reviews: **what**, **key lines**, **why**, **proven**, **only the PSP can prove**. Labels: [M] measured on a PSP, [E] emulator, [D] read in code or in the build, [U] inference.

Branch `mewake`, from `w123` (`6c514cf`).

| Change | Kind | Default | Off word |
|---|---|---|---|
| The ME restarted after a wake | fix | on | `mewakeoff` (b122's behaviour: the ME stays stopped until the next launch) |
| `mewaketest`: a sleep's hold on the ME at tick 450, the wake at tick 540, without a real sleep | recon | off | |

## The problem [M]

b122, Sheepdog01's test 2 on the PSP-1000 (2026-10-14). The wake brought the files back ("sleep: awake; 6 files reopened, 0 failed"), but the ME stayed in reset. The kernel's wake handler (`meLibOnWake`) holds it there and sets `me_state = -1`, and nothing ever started it again. From then on the heartbeat said "ME: stopped … no jobs", and every ME job ran on the main CPU:
- audio mixing took 5% of the main CPU;
- one audio gap of 236 ms;
- one 2.9 s frame right after the wake;
- perception's rays and the slant sort ran on the main CPU until the next launch.

## What the ME needs to start again [D]

Read in me-core-mapper at the pin (`727421b`, `me-core-custom.c`, `me-core.h`):
- **What the launch does.** `meLibDefaultInit()` writes and loads `kcall.prx`, then puts the library's `eventHandler` in place of the firmware's own ME sleep handler (`kinit`: it rewrites the handler pointer of the "SceMeRpc" sysevent handler). Then, in kernel mode, `meLibInit()` picks the firmware's table and calls `meLibReset()`.
- **What `meLibReset()` does:**
  1. copies the library's start handler (`meLibHandler`, the `_me_section`, 264 bytes) to the ME's reset vector at `0xbfc00000`;
  2. writes back and invalidates the main CPU's caches;
  3. raises the reset bits (`0x14`) and drops them.
- **What runs after the reset.** The ME runs that handler, which jumps to `meLibOnPreProcess` and then `meLibOnProcess`: **our code, in this program, in main RAM**. Main RAM is kept through a sleep (self-refresh). The ME's stack is set fresh by the handler in the ME's own eDRAM (`0x80200000` or `0x80400000`), so nothing has to survive there.
- **So the restart is `meLibReset()` again**, through `kcall`. `kcall.prx` and the event handler stay in place through a sleep, since they are kernel memory.
- **Whether `0xbfc00000` survives a sleep is [U].** The library's own `sleep-wake` sample releases the reset on wake without copying the handler again, which suggests the author found it kept. `meLibReset` copies it again anyway, so it doesn't matter.

## The design

**1. The kernel's handlers stay small.** `meLibOnSleep` and `meLibOnWake` still hold the ME in reset and set `me_state = -1`. They now also count `me_sleeps`, so a restart can tell when a second sleep landed while it was running. Nothing waits in kernel mode.

**2. The power callback only asks** (`psp_platform.c`, `PSP_POWER_CB_RESUME_COMPLETE`). After the files are reopened, it calls `psp_me_wake_request()`, which sets a flag. The callback thread can't know whether the game's threads are partway through an ME job, so it never touches the ME itself.

**3. The main thread does the restart at a frame end** (`psp_ge_frame_end` → `psp_me_wake_poll`, `psp_me.c`). This place was chosen because the main thread is the ring's one producer (`psp_me.h`). Between frames it has no ring job half handed over, and perception's rays are settled at `actors_update`'s end. The poll costs one load a frame while no wake is pending.

**4. No job can be in flight across the switch.** Three mechanisms ensure it:
- **`slot_busy[]`, one flag a slot.** Its only writer is the slot's own client thread: audio, render or the heartbeat's probe. That thread raises the flag *before* it reads `me_state`. `psp_me_run` raises it for the whole call; `psp_me_submit` raises it until `psp_me_finish` ends. The restart goes ahead only when every flag is down and no half is `submitted`, which it checks while `me_state` is still -1. From that point a client that raises its flag sees -1 and runs the job itself, until the restart's very last store. A thread still waiting on a job from before the sleep keeps its flag up until its own timeout ends the wait, and the restart tries again at the next frame end.
- **The counters carry on.** Each slot's `request` and `done` are set to the main CPU's own `slot_request`. Both ring counters are set to `ring_next`, and the ME takes its `ring_popped` from `RING_POPPED` when it turns the ring on (that value is 0 at launch, as before). So the new ME never runs a ring entry from before the sleep.
- **`ring_epoch`.** A ticket from before the restart that is waited for afterwards gets `ring_fail(0)`: "the main CPU does it", and the ring stays on. Without this, `RING_POPPED` (moved past that ticket) would say the job was done, the stale entry's ticket wouldn't match, and `ring_fail(2)` would turn the ring off for the session.

**5. The start sequence, in order** (`psp_me_wake_poll`):
1. reset the control block (`stop`, `alive`, `clock`, `jobs`, `halted`), the slots and the ring indexes, as above;
2. set `features` (ring, mehalt) **before** the release. Launch sets them after its start check. The ME reads them on its first pass, so the ring is served from the first frame; a ring job not done within 10 ms would turn the ring off for the session;
3. call `kcall_2(me_release_kernel)`, i.e. `meLibReset()`: the handler copied again, the caches, the reset pulse;
4. wait up to 100 ms (`WAKE_ANSWER_US`), in 0.5 ms sleeps, for the ME's first idle poll (`control->alive != 0`);
5. only then set `me_state = 1`.

If the ME doesn't answer, or `me_sleeps` moved meanwhile, the ME is held in reset again and the main CPU keeps the jobs, as in b122. The heartbeat says why.

**6. The ME isn't trusted again after a failure.** `me_restartable` is set at launch. Any stop other than the sleep's own (`psp_me_sleep_stop`, which the power callback now calls instead of `psp_meaudio_stop("sleep")`) clears it, for example a job that timed out. A wake then leaves the ME stopped and says so. A ring that failed before the sleep (`ring_dead`) stays off too.

**7. The log.** The heartbeat's ME report prints one line, once:
- `ME: restarted after wake in N ms (its first idle poll X us after the reset's release; F frame ends waiting for its clients)`. N counts from the wake's callback to the ME answering, which is the time the main CPU ran the jobs alone after the wake;
- `ME: restart after the wake FAILED (no idle poll within 100 ms of the reset's release | the PSP slept again meanwhile); held in reset, the main CPU runs its jobs`;
- `ME: stays stopped after the wake (mewakeoff | it was stopped for a failure before the sleep); the main CPU runs its jobs`;
- in the emulator: `ME (simulated): restarted after wake in N ms (…)`.

## Key lines

`psp_me.c`:

```c
/* psp_me_run: the flag up before me_state is read (inside me_run_slot) */
slot_busy[which] = 1;
done = me_run_slot(which, type, args, arg_count, results, first_look_us);
slot_busy[which] = 0;
```

```c
/* psp_me_wake_poll: wait for the clients, then the shared words, then the release */
for (k = 0; k < ME_SLOTS; k++)
	if (slot_busy[k] || submitted[k])
	{
		wake_frames++;
		return;
	}
...
for (k = 0; k < ME_SLOTS; k++)
	SLOT(k)->request = SLOT(k)->done = slot_request[k];
ring_epoch = ring_next;
ring_restarted = 1;
RING_PUSHED = RING_POPPED = ring_next;
control->features = (me_halting ? ME_FEATURE_HALT : 0u) | (ring_on ? ME_FEATURE_RING : 0u);
...
kcall_2(me_release_kernel, 0);          /* meLibReset() in kernel mode */
while (control->alive == 0 && (now = sceKernelGetSystemTimeWide()) - release < WAKE_ANSWER_US)
	sceKernelDelayThread(500);
if (control->alive == 0 || me_sleeps != sleeps)
{
	kcall_2(me_hold_reset, 0);
	...
}
me_state = 1;
```

```c
/* meLibOnProcess, on the ME: where the ring starts after a restart */
ring_popped = RING_POPPED;
```

```c
/* psp_me_wait: a ticket from before the restart is the main CPU's to do */
if (ring_restarted && ticket - ring_epoch >= ring_next - ring_epoch)
	return ring_fail(0, ticket, index);
```

One more line in `psp_me_wait`: the gemesim window, which a simulated push opens, is now closed whenever the ME was *launched* simulated (`me_restartable == 2 || me_state == 2`), and before the epoch check. That way a stopped or stale ticket can't leave it open in the emulator. This changes nothing on the PSP, where `me_restartable` is never 2.

`psp_platform.c`:
- the power callback calls `psp_me_sleep_stop()` on suspend and `psp_me_wake_request()` at the end of the resume;
- `mewaketest` in `psp_tick_account`;
- one keep-words line.

`psp_ge.c`: `psp_me_wake_poll()` at the top of `psp_ge_frame_end`, after the defer drain and before `frame_open` is checked.

## What doesn't change with no sleep [D]

- **The launch path is the same.** The only new work is that `me_restartable` is set.
- **On the ME,** `ring_popped = RING_POPPED` reads 0 at launch, the value the local already had.
- **In the hot paths,** the slot calls store a flag before and after (two stores a job, no system call). `psp_me_wait` gains one test of `ring_restarted`. The frame end makes one load and one branch.
- **The ME can't reach the new code.** `mewalk.py` on this build (`meLibOnProcess` as root, with `--syms`) reaches neither `psp_ge_frame_end` nor `psp_me_wake_poll`, `me_release_kernel` or `psp_me_wake_test_sleep`.
- **Both builds compile.** The campaign build compiles and links locally (4.83 MB, 0 symbols stubbed), and in the lab with the multiplayer build (batch `mewake123a`, below).

## Proven in the emulator [E]

PPSSPP runs no ME code and has no sleep. With the ME simulated, `mewaketest` drives the path the wake takes, apart from the reset itself:
- the stop as the power callback does it;
- `me_state = -1` and the sleep count, as the kernel handler sets them;
- the main CPU carrying every job (audio, rays, slant) for 3 s of game time;
- the wake request, the wait for the clients, the counters and the epoch, and the switch back.

Batch `mewake123a` (lab run 38649535810, this branch's `a80d52f`). The ME was simulated with audio, slant and rays all on it (no `nomeaudio`). All 9 runs ended normally: segv 0, strict 0 sites, no crash.

**The path runs, and the jobs come back.** One line in every run, then the usual "ME: SIMULATED" heartbeats with `0 failed`:

| Run | The wake's line |
|---|---|
| d20, `mewaketest` | `ME (simulated): restarted after wake in 32 ms (0 frame ends waiting for its clients)` |
| a50, `mewaketest` | `… restarted after wake in 21 ms (0 frame ends …)` |
| a50, strict | `… in 21 ms` |
| b30, strict | `… in 27 ms` |
| Blood Gulch, multiplayer build | `… in 13 ms` |
| d20 and a50, `mewaketest mewakeoff` | `ME: stays stopped after the wake (mewakeoff); the main CPU runs its jobs` |

- In the emulator, N is the time from the wake's request at a tick's end to the next frame end. It is not the PSP's figure.
- No run logged a ring failure, a slot job timeout or a gemeoverlap failure.

**Play exact** (`statecmp.py`, statecrc, hands off, `fixedticks fixedseed`). The play hash and both random seeds are the same at every common tick:

| Pair | Common ticks |
|---|---|
| d20, wake against wakeoff | 21,620 |
| d20, wake against no test | 21,460 |
| a50, wake against wakeoff | 36,830 |
| a50, wake against no test | 36,830 |

- **What differs.** The whole-state hash differs only in allocations already known to follow the emulator's speed: objects, particle, contrail point, object looping sounds, first person weapons, actor (`review_b122_merays.md`).
- **Where it starts.** The difference begins at the test's stop (against no test) or at the wake (against wakeoff). Those are the points where the jobs' cost moves between the "ME" and the main CPU and the frames' timing changes [U, as before: not narrowed with `stateblocksN`].

**The rules across the switch** (strict interpreter, `gemesim mewrites`, the stop and restart inside the stretch):
- **a50** (rays, `perccheck`), 2,986 ticks:
  - `MESTRICT: no system calls in work meant for the Media Engine`;
  - ME rays: 7,298 answered, 7,169 taken and checked, **0 mismatched**, 0 after a failed job;
  - `MEWRITES`: 22 lines written by both sides, all in `0x09fb…`, the main thread's stack where the emulator runs the jobs. That is the same picture as before this change.
- **b30** (slant with `meslantcheck`, `meringtest`), 1,530 ticks:
  - no system calls;
  - slant: **0 of 793,414** triangles differing, 0 fallen back to the main CPU;
  - ringtest: **0 wrong of 5,744 jobs**, 0 failed, 0 bursts short. The ticket numbering carried on across the restart, and no stale answer was taken;
  - `MEWRITES`: 6 lines written by both sides, all in `0x09fb…`.
- **Multiplayer:** the build compiles, starts Blood Gulch, restarts and plays to tick 6,793. Its one "FAILED" line is a 1,081,344-byte `memalign` at load (line 115), well before the test and not the ME's. Earlier multiplayer runs report heap shortfalls at load on Blood Gulch too [U: not compared line for line].

**What this doesn't reach.** The emulator runs no ME code, so `kcall`, `meLibReset`, the first-poll wait and the hold on failure didn't run. They are the PSP's to prove (below).

## What only the PSP can prove

1. **That the ME starts after a real sleep at all.** Three things are [U]:
   - whether the ME's power and clock are on after the resume, given that the library has replaced the firmware's own ME sleep handler, which would normally bring the ME back;
   - whether `0xbfc00000` keeps the handler (it is copied again anyway);
   - whether the handler's register set-up (bus clock, the memory unlock) is enough after a sleep.

   The library's own sleep-wake sample releases the reset on wake and expects the ME to run on, which is a sign it works, but nobody here has seen it on a PSP. **Test (b123's test, or test 14 tonight):** play, sleep the PSP with the power switch for ~10 s, wake it, play a minute. Look for "ME: restarted after wake in N ms" in `log.txt`. The next heartbeats should say "ME: running" with audio and ring jobs, and the audio cost should fall back from ~5% of the main CPU.
2. **That the reset path works on the PSP without a sleep** (`mewaketest`, keep word). At tick 450 it does exactly what a sleep does to the ME: the polite stop, then the kernel's hold (`meLibOnSleep` through `kcall`). At tick 540 it asks for the wake. This proves the `meLibReset` release, the first poll and the ring's restart on real hardware, so a real-sleep failure would then point at power or clocks. It can run on the rig (no hands needed) once a PSP is free; tonight none is.
3. **How long it takes.** Expected [U]: under 1 ms from the release to the first poll (launch saw the ME polling within its 2 ms first look), plus up to one frame waiting for the clients. The 236 ms audio gap and the 2.9 s frame are the sleep's own and may stay: this fix shortens what comes after them, not the wake itself.
4. **What happens if it hangs.** A failure costs at most a 100 ms frame on the wake, then b122's behaviour.

## Risks

- **The ME comes back wrong [U].** For example, it runs but with the cycle counter stopped. Jobs that then time out stop it through the usual path (`psp_me_stop`), and it stays stopped through later wakes. The log will show "a job timed out" after the restart line. The cycle rate is not measured again: `me_cycles_per_us` stays at the launch figure.
- **A sleep during the restart.** If the PSP sleeps again between the `me_sleeps` check and the store of `me_state = 1` (a window of a few instructions), `me_state` could read 1 with the ME in reset. The first job would then time out, after 40 ms for audio, and stop the ME, with `me_restartable` cleared, so it stays stopped until the next launch. Unlikely, and it fails safe.
- **eDRAM.** Anything the ME left in eDRAM before the sleep is gone, so `edramcheck` would report changes after a wake. That is expected.
- **`gemeoverlap` (opt-in).** A render half handed over just before the sleep fails on the wake (its existing failure path: "the Media Engine failed with half the buffer out"). That turns `gemeoverlap` off for the session, as it would in b122.
- **Multiplayer.** No simulation code changes; the ME's jobs are exact either way, and the restart only moves work between CPUs. The multiplayer build compiles with the change (batch, above).
