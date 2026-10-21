# Review: fix-rcheck (a crash when the heap can't grow the immediate-draw buffer)

Branch `fix-rcheck`, off `w139`; the same commit cherry-picked onto `w140` as `fix-rcheck-w140`. One fixup, no keep word (below). Evidence labels as in CLAUDE.md.

Status: **fixed: compiled (campaign and `PSP_MULTIPLAYER=1`, on both bases), crash reproduced and gone in the emulator, normal path exact.**

## The crash [D]

A run on a30 whose heap had 128 KB less room than usual stopped at game time ~640 with a write to address 0, in `set_attribute` (d3d8_gl.c, with `immediate_emit` inlined), called from `_rasterizer_screen_effect` while the interface drew the screen.

ChupathingyCE's `immediate_emit` (port/linux/src/d3d8_gl.c) keeps the vertices of a `Begin` ... `End` batch in `device.immediate_vertices`, and grows it when it fills: 256 vertices of 16 attributes x 4 floats (64 KB) the first time, then doubling. The `realloc`'s result was stored without a check, so when the heap refused the grow the next `memcpy` wrote to `NULL + count * 256`. On the PSP the heap is nearly full on the heaviest maps (a30 with the GE's in-flight copies), so the first screen effect drawn after the heap fills can crash the game. The unchecked realloc is upstream's too; the PC never runs out.

## The fix (`psp_fixups.py`, FIXES_CHUPA `d3d8_gl.c#immediate`)

```c
unsigned long capacity = device.immediate_capacity ? device.immediate_capacity * 2 : 256;
float *vertices = realloc(device.immediate_vertices, capacity * floats * sizeof(float));
...
if (!vertices)
{
	/* logged once */
	device.immediate_active = FALSE;
	device.immediate_count = 0;
	return;
}
device.immediate_vertices = vertices;
device.immediate_capacity = capacity;
```

- The old buffer and its capacity stay (a failed `realloc` leaves its block as it was), so nothing leaks and a later grow can still succeed.
- The rest of the batch is dropped whole: `immediate_active` goes FALSE, so the batch's later vertices aren't collected, and `End` sees a count of 0 and draws nothing. No primitive is drawn in part.
- On the PSP that draw would have drawn nothing anyway: `End` goes through `prepare_draw`, which returns at once because the GL device is never made (`DEVICE_GL_READY` is FALSE, `gl_never`). Only the buffer's growth was live. So dropping the batch changes nothing on screen [D].
- The first refusal is logged once: `immediate draw: no heap for N vertices (B bytes): such batches are skipped`.

**Why no keep word.** When the `realloc` succeeds, the code does what it did before: the same request, the same sizes, the same buffer. Only the failure path is new, and the old failure path was a crash. A keep word would only bring the crash back.

**Why not reserve the buffer when the device is made.** Reserving 64 KB up front would cost every map 64 KB of heap whether it draws an immediate batch or not, on a heap that is already the limit (c10 has 16 KB free). And since the PSP never draws from this buffer, the cleaner fix would be to stop collecting immediate vertices at all on the PSP. That changes what the heap does on the normal path (64 KB more free once a screen effect has run, so later allocations land elsewhere). The heap layout reaches play in places (a pre-existing sensitivity under study), so that one is left for its own A/B. This fix keeps the normal path exact.

**The other allocations in d3d8_gl.c [D].** `immediate_emit`'s grow is the only one the PSP reaches while drawing:
- `quad_indices` (malloc, unchecked), `stream_upload_swizzled`'s scratch (malloc, unchecked), the vertex-array, render-target, framebuffer, fragment-shader and mip-composite caches (calloc, unchecked), `write_screenshot` (malloc): every one is reached only after `prepare_draw` succeeds, from `bind_targets`, from `Present`'s or anti-aliasing's GL path, or from a GL-only pointer read. All are behind `DEVICE_GL_READY`, so on the PSP the compiler drops them [D].
- `vertex_shader_handle_new` and `program_entry_add` check their results already; `CreateVertexShader`'s copy is already gone on the PSP (FIXES_107).

## Proof [E]

One emulator batch, with fixed ticks and seeds:

- **The crash, reproduced and fixed.** On a30 with a test-only word that holds 128 KB of the heap, the old code stops at game time 640 with a write to address 0, as in the run that found it. The same build plus this fix plays the whole route to its exit (game time 5,983). It logs `immediate draw: no heap for 256 vertices (65536 bytes): such batches are skipped` once, at tick 703, where the heap refused the first 64 KB grow. Later batches are refused the same way and skipped (the heap's log shows 63 refusals of that grow in all), with no crash. Nothing is lost on screen: the PSP never drew these batches (above).
- **The normal path is unchanged** (no heap pressure, statecrc, game time 0-75 s):
  - a10 on w139, before and with the fix: the whole state, play and both seeds are identical at all 2,250 ticks.
  - a10 on w140, before and with the fix: identical too.
  - c10 on w139: play and both seeds are identical at all 2,250 ticks. The whole-state hash differs from tick 502, in the `objects` and `object looping sounds` allocations only. No heap request failed on either side, so the new path never ran. The program is 1 KB larger, and those blocks hold addresses [U]; play doesn't depend on it.
- a30 with no test word (12 refused heap requests, none of them this grow) plays the same as before up to the bullet at tick 5,942. There, play splits into one of the outcomes already known to depend on the heap's layout. That split is a pre-existing sensitivity, not this change [E].

## Cost and hot code

`immediate_emit` runs per vertex of an immediate batch (the screen effects, the progress bars); the new code runs only on a grow, at most a few times a map. The per-vertex copy is unchanged. d3d8_gl.c isn't one of the profiled units, so the PGO profile has no counts in it to miss: the w139 build with the release's profile reports 0 functions without counts.
