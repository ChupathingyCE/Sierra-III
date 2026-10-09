# Handoff: PSP-1000 loses its clock setting when the battery is pulled

A standalone investigation. Written 2026-10-05.

**Goal:** work out why the PSP's date and time are lost when the battery is removed, decide whether that is normal or a fault, and fix it if it is a fault.

## The device
- **Model:** PSP-1000, firmware 6.61, ARK-4 (CFW front end: Kommander).
- **Motherboard revision:** not yet known (TA-0xx). Find it out before opening anything.
- **Machine for any desktop work:** the P14S laptop (fish shell).

## Symptoms (as Sheepdog01 reported them)
- Pulling the battery and letting the PSP sit resets the date and time.
- The PSP often asks for the date and time at boot. Not yet established: whether this only happens after the battery has been out or run flat, or also with a charged battery left in.
- Files written while the clock is unset are dated **31 Dec 1984**.
- Kommander's date picker only offers the years 2000–2099.
- Once a date is set, the clock appears to keep time normally until the next reset.
- **Observed once, unconfirmed:** while the clock was unset, software reading the time (`sceKernelLibcGettimeofday`) behaved as if the clock were stalled or advancing slowly. Nothing has logged its raw values yet.

## Background (my understanding, not verified for the PSP-1000)
- The real-time clock lives in the SysCon (the system controller chip). It runs from the main battery.
- Without a battery, only a small on-board capacitor keeps the clock alive, long enough to bridge a quick battery swap, not hours.
- If that's right, **losing the time after the battery has been out for a while is expected behaviour, not a fault.**
- Things that would point to a real fault:
  - the time is lost after a short swap of a few seconds;
  - it's lost with a charged battery left in;
  - it's lost across a normal power-off or sleep.

## Hypotheses, ranked (none verified)
1. **Normal behaviour:** the backup capacitor only bridges a short swap, and the battery was out longer than that.
2. **A weak or deeply discharged battery pack:** if it sags or cuts out, the SysCon loses power and the time is lost even with the battery in.
3. **A failed backup capacitor or its charging path on the board:** short swaps lose the time too.
4. **A SysCon or clock-chip fault:** it would show up as losing the time, or as drifting, even with good power.

## Tests to narrow it down (no tools needed)
1. **Short swap:** set the date, pull the battery for 10 s, and reinsert it. Is the time kept?
2. **Find the threshold:** repeat with 1 min, 10 min and 1 hour out.
3. **Battery in, powered off:** set the date, power off normally, and check after an hour, then overnight.
4. **Battery in, asleep:** sleep the PSP for an hour, then check.
5. **Swap packs:** repeat tests 1 and 3 with a different, known-good, fully charged battery if one is available.
6. **Note conditions:** for any lost time, note the charge level beforehand and whether the battery had run flat.

**How to read the results:**
- Time kept in tests 1, 3 and 4, and lost only after long absences: this is normal (hypothesis 1).
- Time lost with the battery in, but cured by another pack: the battery (hypothesis 2).
- Time lost after even a 10 s swap, with any battery: the board's backup (hypothesis 3).

## Optional measurement Claude can build
A tiny homebrew that logs these once a second for 60 s, to a text file on the Memory Stick:
- `sceRtcGetCurrentTick` (the clock chip, through the RTC library)
- `sceKernelLibcGettimeofday` (the wall clock the C library uses)
- `sceKernelGetSystemTimeWide` (the time-since-boot counter, as a reference)

Run it once with the clock unset and once with it set. That would show whether the unset clock is frozen, slow or jumping, and whether a set clock drifts.

## Safety rules for the Memory Stick
- ARK-4's files live on the stick; don't delete or move them. They include SEPLUGINS, ARK150on660, FasterARK, CustomIPL and DC10.
- Leave `overconfig.txt` alone.
- Don't format or wipe the stick.
