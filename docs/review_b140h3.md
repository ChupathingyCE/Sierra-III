# Review: build 140, hand test 3 (b140h3)

For the owner to read and question before testing (CLAUDE.md, "How AI is used here"). b140h3 is `w140` at b41f0ae: b140h2 (`review_b140h2.md`, w140 34eed63) plus two changes. Everything in b140h2's review still holds; this page covers only what's new.

Labels: [M] measured on a PSP (which one is named), [E] emulator, [D] read in code or data, [U] inference.

**Install:** the zip is `sierra-build/39890849648/halo_psp_b140h3.zip` on the lab's results branch (EBOOT 196709597141b376, heap 5,716 KB). It needs the **b140z** bake.

## What changed since b140h2

| Change | What it does | Default | Proof | Review |
|---|---|---|---|---|
| **The restart after a death** (restartfix 550b5ce) | Reverting to a checkpoint read the saved state over the live game state and only then asked the heap for a 4 KB page buffer; on a full heap that failed, the revert was refused, and the map restarted on a half-overwritten state (a crash). The buffer now comes from the checkpoint's own 32 KB stage, idle during a read. Latent since 10-12; a long game near the heap's edge could hit it | on (`stateentrymalloc` = before) | heap forced full: the old path crashed 5 of 5 (b30 deaths, game_revert, a10, c20), the fix survived 5 of 5 [E]; play + both seeds equal to 34eed63 for 6,000 ticks on b30 and a10 [E] | restartfix branch, coord 2026-10-22 21:14 |
| **hblow**: the heartbeat below the game thread | The thread that formats the logs and reports every 5 s ran above the game thread, so each report put an ~8 ms spike into one frame; on a map whose frame sits near 33 ms (the Blood Gulch 26-29 fps cycling) that tips a frame past the vertical blank | on (`hblowoff`) | d40 hands off, the calm stretch, PSP-1000 without frametrace (39887276929): the median of each 5 s window's worst frame **26.4 / 26.2 ms -> 18.1 / 18.0**, frame average 14.3 -> 12.8-13.0 [M]; a thread priority only, play + both seeds equal [E] | fdrop branch (f5c0e03) |

## Proof of this build

- **Emulator** (h3smoke 39890853300): b30 and a30 routes under the strict interpreter, 0 sites, no crash; the front door boots to the menu and loads a10; the keep lines show `aicad3` and `hblow` added, `texpretouch` not. The b30 run with deathless off reached its exit without the player dying, so the restart path wasn't exercised there: its proof is restartfix's own runs above.
- **PSP-1000 relaunches** (b30 route + deathless, exitat180; 39890880477): **5 of 5 exited**, each with its `GE: exit:` line.

## What to watch

As b140h2, plus: **die and let the checkpoint restart** at least once (the restart fix), and on Blood Gulch or any calm stretch whether the fps counter still cycles (hblow).
