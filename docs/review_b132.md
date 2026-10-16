# Review: build 132

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). b132 is b131 (`review_b131.md`) plus the branches below, and its own profile (`campaign-b132`). Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

| Branch | What | Kind | Default | Words | Review |
|---|---|---|---|---|---|
| `order132` | The code laid out by its calls (phase B, T2): a link order file | exact speed-up | (§1) | the build switch `PSP_TEXT_LAYOUT` | §1 |
| `mpon132` | The Media Engine pose job on by default | exact speed-up | on | `poseaheadoff` (off); `poseahead`, `poseaheadcheck` | §2 |
| `mpon132` | The launch's register read leaves the ME's own RAM alone | safety | on | `memarksatlaunch` (as before) | §3 |
| `nmreaders132` | Who reads each object's node matrices within the tick | recon | off | `nmreaders` | §4 |

## Why b131 was slower than b130

b131's ladder on the PSP-1000 ticked 10–17% slower than b130's on c10, a30, c40 and a10 [M]. Not surface bounds: c10 with and without it the same on the PSP-1000 (lab 38810074163) and in the cache model [M, E]. The cache model reproduced the rest (lab `reg131`) [E]: the same code laid out by b131's profile misses 42% more instructions on c10 and 48% on a30 than laid out by b130's; built without any profile, b131 is only 2.6% slower than b130. The profile decides where the hot code sits, and that placement is luck: worth ~18% of c10's tick for b130, two thirds of it lost for b131. §1 is the answer.

## §1 The code laid out by its calls (`psp_build.py` `PSP_TEXT_LAYOUT`; the lab's `tools/codeorder.py`; `docs/order_file.md`)

(To come: the layout chosen, its scores on the ten maps, and the build's check.)

## §2 The ME pose job on by default (`psp_fixups.py` FIXES_135 `objects.c#poseaheadon`)

**What.** b131's pose job (its review §2) on wherever the ME runs the job ring:

```c
psp_pose_ahead_me = !psp_keep("poseaheadoff") && !psp_keep("poseahead") && psp_me_ring_ready();
psp_pose_ahead_mode = psp_keep("poseaheadoff") ? 0 :
	psp_keep("poseaheadcheck") ? 2 : psp_keep("poseahead") || psp_pose_ahead_me ? 1 : 0;
```

Without the ME (`nomeaudio`) it stays off: making the poses ahead on the main CPU would only add the copies. Tick-step recon builds keep it off unless asked.

**Why now.** b131 shipped it off because it hung the PSP-3000 once. That hang came at the ME's start-up, before any map loaded (§3): the pose job never ran. The same configuration then ran clean on the 3000, and **exact on the hardware**: 164,981 poses decoded again on the main CPU and compared bit for bit, 0 differed (lab 38812680216) [M].

**Worth** [M]: b30's flight tick −1.15 ms (−2.0%, PSP-1000, four runs a side); c20's tick −1.6 ms (−6.4%, PSP-3000, two pairs, both pairs within 0.1 ms; lab 38815782829). On the main CPU the job costs ~0.86 ms a tick on b30, most of it copying each pose back (~7 µs a pose, its lines read cold) (`posetime`, lab 38815306822).

## §3 The launch's register read (`psp/src/psp_me.c` `me_sysregs_running_kernel`)

**What.** At launch, with the ME running, the log's system-register line reads the registers alone, not the two words of the ME's own RAM (0xbfc006c0, 0xbfc006c4) it may be running from. The exit and the wake still read them, with the ME halted or held. The launch line shows them as 0; `memarksatlaunch` reads them as before.

**Why.** The PSP-3000's one start-up hang (lab 38784036485; its log recovered by `psp-probe`) came after "ME: the ME runs" and before this line: inside the read or its log write. Reading the ME's RAM under its feet is the one part of it no other moment does [U that it was the cause: one hang in many launches].

## §4 `nmreaders` (`psp_fixups.py` FIXES_134 `objects.c#nmreaders`)

**What.** Emulator recon for lever NW (node matrices whole on the ME; `plan_node_matrices_me.md`): each compute in `objects_update` classed by its first read after (the object's own update or its attachments', another object's update, later in the tick, drawing, none), by nodes and by object type, with the call sites. Off unless asked; its 48 KB table comes from the heap only then.
