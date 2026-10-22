# The button glyph sheet off the Media Engine (mealloc)

**What it fixes.** Under the M6 #2 defaults (gedefer, geinflight, gemeon) the HUD's screen quads are recorded on the main CPU and replayed on the Media Engine. The first button glyph a launch draws (the main menu's prompts, a "hold X to pick up" in a level) unpacked the 32 KB glyph sheet inside that replay: `glyph_sheet_unpack` ends in `sceKernelDcacheWritebackRange`, a system call, and `psp_tex_glyph_room` asks for a missing RAM block with `ram_cache_acquire` (memalign and the volatile partition's lock). The ME makes no system calls and no library calls. [D] The emulator's ME-strict check (gemesim) lists the call from screen_quad's `glyph_sheet_unpack` on the b30 route and on a10; a PSP-1000 front-door run's log shows the menu's first glyphs drawn by the ME's work, and the run itself ended normally [M], so what the call does on the ME is unknown [U]. It breaks the ME's rule either way.

**The change** (`psp/src/psp_ge.c`, `psp/src/psp_tex.c`):
- `glyph_sheet_ready()`: as `psp_ge_screen_quad` records a glyph quad (main CPU), the sheet is unpacked there if the RAM block is new.
- `glyph_sheet()` in the ME's work only reads: `psp_tex_glyph_room_held` (never asks for a block) and the generation; if the block changed since the recording (a sleep between), the quad is left out for that frame.
- `glyph_generation` and `glyph_me_old` are `PSP_MAIN_SIDE`: only the main CPU writes them now.
- Keep word `glyphmeold`: the sheet unpacked wherever it is first drawn, as before.

**The heap check made exact** (`psp/src/psp_libc.c`, `heap_me_check`, gemesim only): it now logs only when the thread asking for the heap is the one doing the ME's work (`psp_me_context` holds its id). The cache file reader, which takes the CPU from the game thread, asked for 20 bytes in `platform_queue_apc` (ReadFileEx's completion) and was logged as the ME's.

**Proof** [E]: play and both random seeds the same at every tick against the base (b30 route 3,750 ticks, a10 route 7,500); the ME-strict site gone with the change and back with `glyphmeold` (a10); the 20-byte heap line gone; heapwalk finds no bad chunk header; the front door (menu, a10 and back) draws its glyphs on both builds. Only the HUD's glyph path changes; nothing in play reads it.
