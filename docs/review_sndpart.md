# Review: sndpart on by default

**What it does.** The sound tier is the part of the sound cache the ME fills by its own copy job. It gets a second window: 880 KB in the ME's 1 MB partition at 0x08300000. That partition sits outside the game's heap, so the main tier in the heap shrinks by the same 880 KB. The sound block takes 384 KB of heap instead of 1,264.

**The claim.** Nothing allocates partition 4 in play. Still, the window is held by contract, not by luck:
- `psp/claim/` builds `halo_claim.prx`, a kernel module with nothing in it but the claim. The game loads it from beside the EBOOT.
- At start it claims the partition's bottom 960 KB through sysmem's kernel export. The game can't make that claim itself: sysmem's user call refuses partition 4 (800200d6).
- At the exit the game gives the block back (`halo_claim_release`, logged as "given back to sysmem (00000000)").
- It claims 960 KB, not the whole 1 MB, because sysmem refuses to free a block that is the whole partition (80020001). A partial block it frees.
- The window starts at the partition's 4 KB mark: the first page holds the word the ME library reads at a restart.

**Proof** (the lab's rows `sndpart_*`; all [M]):
- Every piece is hashed as copied and as held. On every launch: 0 copies failed, late or hashed wrong.
- b30 on the PSP-1000 (sierra-hw 44081373586): the heap's free low 724 → 1,604 KB. The frame is in the noise (cinematic −0.0%, flight −1.8%).
- c20 on the PSP-3000 (sierra-hw 44085436995): free low 576 → 1,456 KB. The frame is −0.7% on the first stretch.
- Sounds refused: no more than without it (b30 2 against 1–2; c20 0 against 0).
- The claim was given and given back on every launch of both maps. A HOME exit's check rides on Sheepdog01's hand run.

**Where it won't go.** sndpart refuses itself under:
- `sndtierdma`: the DMA reaches only the eDRAM;
- `mehalt` and `mewake` (unless `mewakeoff`): their paths enter Sony's image in the partition;
- `partmap`, `partmapall` and `meddrprobe`: they pattern the partition.

If the claim is refused, the tier runs as before.

**Keep words.**
- `sndpart` is on by default; `sndpartoff` turns it off.
- `claimprobe` gives the claim back and makes it again mid-game, as its proof.
- `sndpartnoclaim` is a measuring form only: the window without the claim.

**Code.**
- `psp/src/psp_dsound.c`: `SNDPART_*`, the window at tier setup and the refusals.
- `psp/src/psp_me.c`: `psp_me_claim_partition`, and the release in `psp_me_exit`.
- `psp/claim/main.c`: the module.
- `psp/src/psp_platform.c`: the default.
- `tools/build_release.sh`: packs `halo_claim.prx` beside the EBOOT.

The design and every step's reading are in `docs/plan_sound_edram.md`.
