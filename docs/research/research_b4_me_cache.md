# B4 findings: ME cache code (ps2dev thread p=58333)

Part of the **Halo CE PSP: performance review checklist** research (check 45, proposals F and G in `research_b1_insomniac.md`).

**Source situation:** the forum post (`forums.ps2dev.org/viewtopic.php?p=58333`) was never archived, and no mirror turned up. Daedalus's `Source/SysPSP/Utility/CacheUtil.h` says its code was "taken from MediaEnginePRX, assume they're originally from" that post. So instead of the post, these notes come from **the code itself**: Daedalus (github.com/DaedalusX64/daedalus, shallow clone Oct 13, 2026), files `SysPSP/Utility/CacheUtil.h` and `SysPSP/PRX/MediaEngine/{main.c, me.c, me.h}`.

**Licensing caution:** Daedalus is GPLv2. Read it for technique; write our own implementation rather than pasting it into Sierra-III, unless the repo's license is decided to be GPL-compatible.

## Why the ME needs its own cache code

The ME can't call kernel functions like `sceKernelDcacheWritebackRange`. So ME-side code manages its cache directly with the MIPS `cache` instruction (`__builtin_allegrex_cache(op, addr)` in the PSP GCC).

## The cache operations used

| Op code | Used as | Notes |
|---|---|---|
| `0x1b` | Write back + invalidate **by address** (one 64-byte line) | Used for ranges. Daedalus's main-CPU-side `dcache_inv_range` also uses `0x1b`, so its "invalidate" actually writes back first. That's the safe choice: a pure invalidate on a line holding someone else's dirty data loses their writes. |
| `0x19` | Invalidate **by address**, no writeback | Used in the ME PRX's `dcache_inv_range`. Only safe when the whole line is owned by the incoming data (64-byte aligned, padded size). |
| `0x14` | Write back + invalidate **by index** | `wbinv_all` loops 0–8191 in 64-byte steps, issuing the op **twice per index**. Reading: 16 KB, 2-way cache = 8 KB per way, and the repeat covers both ways. *Inferred from the code, not verified.* |
| `0x11`, `0x13` + `mtc0 $28/$29` | Store zeroed tags by index | The ME's `inv_all` loops all 16 KB writing empty tags: a hard reset of the cache contents, discarding anything dirty. |

All range helpers step in **64-byte lines**, and `malloc_64` rounds sizes up to 64 and aligns to 64. This is the same rule NJEMU found: shared buffers and flush sizes must be 64-byte aligned, or neighboring data gets written back or discarded along with yours.

## How Daedalus's ME job interface works

- A shared `me_struct`: `start`, `done`, function pointer, one `int` parameter, `result`, pre/post cache fields, `signals`, `init`.
- The main CPU allocates it with `malloc_64` and accesses it **through the uncached alias** (`make_uncached_ptr`), so both sides always see RAM.
- **Boot:** copy a small stub to `0xbfc00040`, write the loop address and the struct pointer to `0xbfc00600/604`, `sceKernelDcacheWritebackAll()`, then reset-enable → bus-clock-enable → reset-disable the ME through `sceSysreg*`.
- **ME loop:** spin until `start != 0` → clear `start` → if requested, invalidate its whole cache → run `func(param)` → if requested, write back its whole cache → set `done = 1`.
- **Main CPU:** `BeginME` fills the struct and sets `start = 1`; `CheckME` polls `done`; `WaitME` spins on `done`.
- The pre/post **address and length fields are ignored**: the ME always does whole-cache operations when asked.

## What to copy and what not to (for check 45)

**Copy the shape:**
- Control block in uncached memory, 64-byte aligned and padded.
- ME-side cache handling with the `cache` instruction in 64-byte steps.
- A simple start/done handshake for coarse jobs. It fits Insomniac's "coarse jobs, one queue per core" advice (B1).

**Don't copy:**
1. **The idle spin on uncached RAM.** The ME's `while (mei->start == 0);` reads main RAM on every pass, continuously, while idle. That's the bus traffic B1 (SPU Wrangling) warns about and a likely contributor to the measured 10–20% texture-fetch slowdown from ME audio. Fix candidates: poll less often (a delay loop between reads), or keep the ME busy with a queue of work, or check whether an ME interrupt/signal mechanism exists. **Measure with the bus_access counter.**
2. **Full-cache flushes on the main CPU per job.** The audio plugin calls `sceKernelDcacheWritebackInvalidateAll()` before every `BeginME`. Checklist check 7 already flags whole-cache flushes as slow; use `WritebackRange` over the job's buffers only.
3. **Whole-cache ops on the ME side when only a range is needed.** Use the range fields that are already in the struct.
4. **One `int` parameter and one result.** Fine for audio; too narrow for batch jobs. Use the single-writer queue design (proposal G) for anything bigger.
5. **`signals`** is updated read-modify-write by `SignalME` with no ownership rule. If both sides write it, updates can be lost. Give each field exactly one writer.

## New open items

- Verify the 2-way, 8 KB-per-way reading of the `0x14` loop against the Allegrex docs (YAPSPD or pspdev), since our own flush helpers will depend on it.
- Does the ME have a usable interrupt or signal from the main CPU so it can sleep instead of spinning? (Not answered by this code.)
