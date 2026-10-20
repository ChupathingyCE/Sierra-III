# Review: branch `metrics` (the measurement round's instruments)

The plan is `docs/plan_measurement_round.md`. Labels: [M] measured on the PSP, [E] emulator, [D] read in code or data, [U] inference.

## The `objcost` and `particlecensus` counters printed no rows

**What was wrong** [E, D]. With both keep words on, an emulator run on b30 and a30 printed each counter's header and nothing under it (`objcost by type` empty, `particlecensus ... made 0`), while the per-type objects line and the particles line beside them were full. The fixup anchors all match (`PSP_RECON=1 PSP_FIXUPS_CHECK=1` reports none missing), so the calls were being made. They were refused at the door: `psp_objcost` dropped any `definition < 0` and `psp_particle_census` any `tag < 0`. A tag index is `index | salt << 16` (`DATUM_INDEX_NEW`), and a map's tag salts are 0x8000 and over (an Xbox map's start at 0xE174), so every real tag index is negative as a `long`. Every call was refused.

**The fix** (`psp/src/psp_platform.c`): NONE (-1) is the only "no tag", as the draw census in `psp_ge.c` already tests it.

```c
const char *name = tag != -1 ? tag_get_name(tag) : NULL;          /* tag_short_name */
if (!objcost_on || definition == -1)                               /* psp_objcost */
long tag = effect != -1 ? effect : particle;                       /* psp_particle_census */
if (!particle_census_on || tag == -1 || outcome < 0 ...)
particle_census[k].from_effect = effect != -1;
```

`tag_short_name` had the same test, so even a filled row would have been named "?".

**Cost:** none outside recon builds. Both counters exist only with `PSP_RECON=1` and their keep words; nothing a play build runs changed.

**Proof** [M]: a 450 s b30 replay on the PSP-1000 with `objcost particlecensus geocluster` prints rows per definition (named, not "?"; the by-type line too) and per effect in every ten-second window; the figures are in the lab's findings (measurements.tsv). Three things the run showed: the instrumented build costs 15% of the frame (+18 ms of 121, 16 of them in the tick), so its absolute times are an upper bound; `tools/passtable.py` mistook the census rows for steps (see below); and `hwthreads` gave nonsense on hardware (below).

## Found reading the first instrumented hardware run

- **passtable.py reads the census rows as steps.** `profile.txt` now holds `objcost:` and `particlecensus:` rows inside each window; the parser skips a row when a colon sits in its first 12 characters, which holds for `objcost:` and not for `particlecensus:`, so every effect row became a step (172 ms a frame of "unclassified"). Fix: skip both prefixes outright. Not yet made.
- **`hwthreads` is unusable on the PSP-1000.** In 223 of 226 windows the shares exceed 100% (others 1,000-40,000%, idle up to a wrapped 4.8e15%, no switches counted after load) [M]. Only the loading line is sane. `psp/profile/main.c`'s `threads_account` needs debugging on hardware (units of the run clocks, the idle clock wrapping, which thread the phase calls come from).
