# B5 findings: Gamasutra "Postcard from GDC Europe 2005: PSP 2005 Overview"

Part of the **Halo CE PSP: performance review checklist** research. Source: archived Gamasutra feature by David Jenkins, Aug 31, 2005 (`gamasutra.com/features/20050831/jenkins_01.shtml`), fetched Oct 13, 2026.

**Status: done, and thinner than hoped.** It's a one-page conference report, the same summary the checklist already cites. There is no fuller write-up behind it; the full talk (Mike Froggatt, MediEvil Resurrection) would only be in GDC Europe materials, if anywhere.

## What it adds

- **Scratchpad "ineffective"** and memory performance "not great," a half to a third of the PS2. Already reflected in check 14.
- **GE clipping:** the MediEvil team "solved [it] only by implementing their own full 3D clipping system." Supports check 32 (subdivide large polygons at cook time; measure the CPU clipper).
- **FPU IEEE754 compliance:** porting PS2 code produced "a large number of exceptions." PS2 floats weren't IEEE; the Xbox's x86 floats are, so Halo should be closer to the Allegrex. **New small check:** confirm the FPU control register setup (which exceptions are enabled or trapped, and how denormals are handled) so stray float exceptions or slow denormal handling don't show up in hot paths. *Denormal performance on the Allegrex FPU is unverified.*
- **VFPU** described as close to the PS2's vector units and easy to use.
- **QA notes, not performance:** toggling the Wi-Fi switch mid-game caused many QA failures; the Hold switch silently disables input and wasted dev time. Relevant to the multiplayer build and to test-rig troubleshooting.
- At the time, firmware capped the CPU at 222 MHz. Not relevant now; the port runs at 333.
