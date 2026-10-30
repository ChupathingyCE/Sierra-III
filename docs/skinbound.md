# SKINBOUND: the skinning plans' bytes as a second trigger of the table's sweep

**What it changes [D].** The converted-buffer table sweeps once its entry count reaches its mark (512 of 2,048). A scene that keeps reading new geometry into the cache can hold hundreds of KB of plans for blocks long since reloaded (dead entries) before the count gets there.

Now, once the plans pass 192 KB, `model_convert` runs the same `converted_sweep` before converting. That frees the dead entries' plans, which nothing can draw again: a block read back is a new generation and gets a fresh conversion either way. Plans of live entries stay, because their vertices are converted in place. The next bound comes 32 KB past what a sweep kept, so a live set above the bound doesn't sweep on every part.

A `GE: skinbound:` line every 300 frames reports:
- the live and dead plans' bytes;
- the bound's sweeps and the KB they freed;
- the plans made again for vertices a bound's sweep let go. Each of these is a conversion the old order makes too, through its reconversion path.

**Keep words:** on by default at 192 KB. `skinboundN` sets the bound to N KB; `skinboundoff` leaves the entry count as the only trigger.

**Plans measured [E]** (game time 3,600, emulator, default build, bake w148s):

| Map | Live plans | Dead plans | Reached by game time |
|---|---|---|---|
| b30 | 188 KB | 18 KB | 3,600 |
| c10 | 133 KB | 36 KB | 3,600 |
| a30 | 138 KB | 51 KB | 3,600 |
| a30 | 128-160 KB | 62 KB | 5,400 |
| d20 | 69 KB | 237 KB | 3,600 |

**Proof [E]:**
- Play and both seeds are identical on against `skinboundoff`:
  - at 160 KB: b30, c10, d20 and a30 (3,600 ticks), and a30 again over 5,400;
  - at 224 KB: b30, c10, d20 and a30;
  - at 192 KB: b30, a30, and the recon build's a30 over 5,400.
- At 192 KB:
  - b30: 1 sweep, 18 KB of dead plans freed.
  - c10 and a30: 0 sweeps.
  - d20: 2 sweeps, 172 KB freed.
  - Plans made again: 0 on b30, c10 and a30; 3 on d20 (each one a block read back, which the old order converts afresh too).
- Failed requests:
  - The default build's a30 fails none with or without the bound.
  - The recon build's a30 (its heap 128 KB smaller) goes from 12 to 1. The one left is a 26 KB triangle list asked for before the plans reach the bound.
- Strict: d20 shows 0 sites (at 224 KB, to game time 1,778).

**Next (not built):** when SKINPACK's room is refused and dead plans are held, sweep and ask again before the conversion takes its slot. That would close the recon a30's last failure where it happens.
