# Review: vmetest, the VME in a game build (recon/vmegame)

One change, off by default behind the keep word `vmetest`: a measurement, not a speed-up. It runs the VME (the Media Engine's vector unit) from the port's own ME start, beside the ME's audio and render jobs, and checks its answers. It's the step `docs/vme_round5.md` names next, and it gates the first VME lever (the audio mix, RW7).

Credit: the sequence and the library code are mcidclan's (m-c/d): his me-core-mapper's `vme-lib.c` and his `mat4x4-mul-distributed` sample, MIT (`psp/src/vme/LICENSE.md`). The README's Credits needs his line at the merge.

## What changed

**`psp/src/vme/`** (new): his `vme-lib.h`, `vme-opcodes.h` and `hw-registers.h`, unchanged, and his licence. Only macros and constants are used from them.

**`psp/src/psp_vme.c`** (new):
- His `vme-lib.c` functions that the sample reaches (`vmeLibEnable`/`Disable`, `ConfigTransfer`, `Wipe`, `ClearLocalBuffer`, `SetInnerAGU1/2`, `Trigger`, `SetFlow`, `_vmeLibStart`/`_vmeLibFinish`) and his sample's context, as he wrote them.
- In place of each firmware call, round 5's register writes, proven on the PSP-1000 (bench-hw 41321706189):
  - the clocks by read-modify-write only (`0xbc100050 |= 0x20`, `0xbc100054 |= 1`). The first word is shared, and its bit 0 is the ME's own bus clock, so his handler's whole-word write isn't copied;
  - the trigger: `0x18` into `0x440ff008`;
  - both waits (pass `0x800`, transfer `0x200`) poll `0x440ff000`. The firmware halts for an interrupt instead, and the port's ME runs with interrupts off. Each wait is bounded in ME cycles: 2,000 for the busy bit to show (round 5 found it already gone in 7 of 8 waits), 2,000,000 for it to clear. Each wait is counted, then gets the firmware's closing write.
- `psp_vme_job` (on the ME): the first job sets the VME up in full (wipe, matrix, vector, context); later jobs load a new vector and run the context again, as his sample's loop does. That also tests whether the VME keeps its set-up across the ME's other jobs. Results: the four products, waits, waits that never saw busy, waits that hit their bound, the longest wait. Its data is on the ME's stack and in the VME's uncached buffers. The one word it writes in RAM (the waits pointer) is `PSP_AIME_ME_SIDE`. In the emulator (`on_me` 0) it touches nothing.
- `psp_me_vme_probe` (main CPU, once a heartbeat window): queues the job on the probe slot, checks the product against integer arithmetic (his inputs are small integers, so this is the same as his VFPU check), and logs a `vme:` line on the first pass, every 8th and any mismatch. It refuses with `soundedram` or `edramcheck`, which use the same slot. A failed job (the ME stopped) means the next one sets up in full again.

**`psp_me.h`**: `ME_JOB_VME 17` and the two prototypes. **`psp_me.c`**: the `case` in `run_job`; the slot's deadline for this job is 500 ms, as edramcheck's (it can wait behind a render job). **`psp_platform.c`**: the probe called after the eDRAM probe. **`psp_build.py`**: the new unit.

## Proof so far
- Reach of `psp_vme_job` (sierra-build 41767604727): 3 functions, 1 KB; no firmware call, no system call, no VFPU, no indirect call [D].
- Emulator (sierra-batch 41767969641, 41768289668): play and both seeds identical at every tick on b30, c10 and c20, tock vs vmetest off (ME off and ME on) and off vs on [E]. With vmetest on, the whole state's objects differ at some ticks outside play (the A/A doesn't), [U] the probe's job inside the heartbeat. Findings `vme_game_exact`.
- PSP-3000 pair (sierra-hw 41769264100): pending.

## For the merge
`run_job`, `me_run_slot` and `heartbeat_thread` compile without their profile counts, so `PSP_PGO_STRICT=1` needs the next profile recount.
