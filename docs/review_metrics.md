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

**Proof** [E]: an emulator batch on b30 with `objcost particlecensus` should print rows per definition and per effect (the lab's findings, measurements.tsv, carry the run).
