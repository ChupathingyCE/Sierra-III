# Review walk-through: b158 (tick)

b158 is b157 (8d054cd0) plus one fix, the B99 reporter fix. Fixes only, per the tick rule.

## B99: the ME's refused-format report could fault the main CPU

**What happened.** On b157's held c40 bench (the lab's sierra-hw 44458233749, PSP-1000, game time 24,301), the 5 s report crashed in `psp_format_refused_log`. The Media Engine never formats text (it can't take the format lock), so `psp_format_refused` counts each refused format by its address, on lines only the ME writes. The main CPU's log then printed each kind with `%.60s` on that address. One format pointer, 0x801fed88, lay in the ME's own RAM (its stack or its copy of the read-only data), which the main CPU can't read, so `strlen` faulted. Every build has this reporter, Sheepdog01's hand b157 included.

**The fix** (`psp/src/psp_libc.c`):

- `format_refused` keeps each kind's address only as a key (`key[]`, an `unsigned int`), never as a pointer the main CPU reads.
- At a kind's first refusal, `psp_format_refused` copies up to 63 bytes of the format's text into `text[k]`, on the CPU that holds the text. The struct stays aligned to whole lines (its `_Static_assert` still holds), so no line is shared with the main CPU's own data.
- `psp_format_refused_log` prints the copy (`text[k]`) and the address as a number.

**What it doesn't change.** Which formats are refused, how they're counted, and when the log runs. Nothing in the game reads this; it's a report.

**Still open (B99 (b)).** The report's first entry says the ME's AI read a datum that was unused or changed 96 times on c40 by 810 s. That's traced separately, after this fix.
