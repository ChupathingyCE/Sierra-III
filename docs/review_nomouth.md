# Review: `nomouth`, a test keep word for exactness runs

**What:** with `nomouth` in keep.txt, `game_sound_set_mouth_aperture` (source/sound/game_sound.c) returns at once, so no speaking unit's mouth aperture is set. Off by default; nothing changes without the word. Test only: it changes the look (mouths stay shut), never for play.

**Why [D]:** lip sync is the one path from the sound manager into game state. `update_channels` gives each speaking unit a mouth aperture read at the channel's estimated play time, which follows the clock and the mixer's progress rather than game ticks. The aperture drives an overlay animation on the unit's nodes, so its node matrices, its weapon's marker and the shots it fires follow the CPU's timing. Runs that change only timing (heap layout, log volume, a faster draw path) part in play for this reason, which made `statecrc` unable to judge them on maps with dialogue. This is the original game's own behaviour, not a port bug.

**Proof [E]:** on a30 with the word, the plain build, `heaproom256`, a log-only change and a heap shift play identically through 9,000 ticks with both seeds equal; without it the same pairs part at tick 5,942. The recon-only version of this switch was measured; this is the same early return, moved into the normal builds.

**Key lines** (psp_fixups.py, `FIXES_112["source/sound/game_sound.c#nomouth"]`): a static flag read once with `psp_keep("nomouth")` and an early return. Cost: one static test per speech sound per frame, in a unit outside the profile-counted set (source/sound is not in `PGO_UNITS`), so the PGO profile's counts are untouched.

**Multiplayer:** compiles into `PSP_MULTIPLAYER=1`; the word is off by default, and lip sync is not part of the networked simulation.
