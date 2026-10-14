# B3 findings: Ghoti PSP optimization tutorials I, II, IV

Part of the **Halo CE PSP: performance review checklist** research. Source: ghoti.nl archived 2009-02-28 (links from PSP-Archive/3D-PSP-Tutorials). Parts III, V and VI were never archived.

**Verdict: low value.** These are beginner tutorials. Parts I and II are generic C++ advice; part IV is a short VFPU intro. Three items are worth acting on, all under check 37/38.

## Worth acting on

1. **VFPU thread attribute.** Every thread that runs VFPU code needs `THREAD_ATTR_VFPU` (main thread via `PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | THREAD_ATTR_VFPU)`, other threads in `sceKernelCreateThread`). Without it the VFPU state isn't saved on context switch. **Check:** every thread that touches VFPU code in the port has the flag.
2. **Library swap quick win.** Linking `-lpspgum_vfpu -lpspvfpu` instead of `-lpspgum -lpsp…` math gives VFPU versions of the GU matrix-stack math. **Check:** what the port links today; only matters if `sceGum` is used in the frame.
3. **VFPU trig form.** `vsin.s`/`vcos.s` take the angle in quarter turns, so the wrapper multiplies by the `VFPU_2_PI` constant first. These are approximations (see the checklist's VFPU notes), so they belong under **R4/R5**: decide where approximate trig is acceptable (rendering, effects) versus where it can change gameplay or PC↔PSP sync (aim, projectiles, AI).

## Cautions the tutorial doesn't mention

- A per-call wrapper that moves one scalar in and out (`mtv` → op → `mfv`) pays transfer cost every call, and `__asm__ volatile` blocks the compiler from scheduling around it. The real gains come from **keeping data in VFPU registers across a batch** (posing a node's blend, transforming a batch of points), as check 37 already says.
- The claimed 130 → 350 → 495 fps in an early build of their game is anecdotal and from a much lighter workload. It shows libm trig can be a hotspot; it doesn't predict gains for Halo.

## Not applicable or outdated

References vs. pointers, prefix vs. postfix, temporary objects, copy-construct vs. assign: C++ object-overhead advice. The Halo decomp is C, and modern GCC generates the same code for references and pointers. "Unroll loops for 2–3×" conflicts with the 16 KB I-cache; it's covered properly by check 39 (measure `-Os` vs `-O2`).
