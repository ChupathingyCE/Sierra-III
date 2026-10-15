# Review: build 126

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). b126 is b125 (`review_b125.md`) plus the branches below; b125 was never on your PSP, so its tests carry over (`build_b126.md`). Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

| Branch | What | Kind | Default | Words | Review |
|---|---|---|---|---|---|
| `rv2` | A path search's state cleared by its header alone | exact speed-up | on | `pathclearfull` (off), `pathclearpoison` (check) | `review_levers126.md` §1 |
| `rv1` | A deleted object's fix-up walks only the types with a handler | exact speed-up | on | `deletedwalkall` (off), `deletedcheck` (check) | §2 |
| `rv4` | The nearby firing-position test searches only when needed; path smoothing scans from the last step | exact speed-up | on | `aipathold` (off), `aipathcheck` (check) | §3 |
| `restore126` | RB1: the HUD's weapon state zeroed; RH1: a still unit's aiming change as x86 gives it | restorations | on | `weaponstateold`, `aimchangeold` (off) | §4, §5 |
| `frametrace` | Every frame timed and split, a line each 300 frames | instrument | off | `frametrace` | below |

The first three come from BirchWoodGod's halo-ce-vita (used with his permission), RB1 from DamnationCE (CC0) via the Vita port's notes, RH1 from JackTulli's Halo 64 (CC0). Each names its source in its commit and review, and the README's Credits names the ports.

## Release check

Lab 38709536698, against b125 as built: the play hash and both seeds identical at every common tick on d20 (15,095), a50 (25,231) and c20 (18,991) [E]; the whole state differs where expected (RB1's `hud weapon interface`, RH1's byte in `objects`, the decal vertex cache that always differs across builds, and the allocations that follow the emulator's speed). b30 and c40 shots right; the multiplayer build plays Blood Gulch at 30 fps. b126 is built with its own profile, `campaign-b126` (0 coverage mismatches).

## What it's worth

- **The PSP-1000** [M], b30 on the b113 route, on/off/on/off: **about 1% of the tick.**
  - With the Media Engine's audio (lab 38699650570): the flight's tick **−0.9%** (real).
  - With `nomeaudio` (lab 38706758115), the tick with the levers against without: flight −0.1%, landing +0.4%, beach −1.3%, all in the noise.
  - The beach can only be judged by repeats: the PSP's play parts between runs of the *same* side from ~tick 1,600 (the dead bodies differ), `nomeaudio` or not [M].
- **Cache models** [E] (lab 38698483247): beach tick −7.7% (instructions −8.1%), a50's opening −0.4%. b30's two sides played different fights there, and the PSP doesn't find the 7.7%, so read it as play that differed, not the levers [U]. The exact pairs (play identical at every tick) are the levers' proof of exactness, not of worth.

## `frametrace` (`psp/src/psp_frametrace.c`, `tools/frametrace.py`)

**Why.** Averages can't tell a steady overrun from a rare hitch, and at 30 ticks a second only a frame over 66.7 ms loses game time. The floor you set, 15 fps at 30 ticks, is a question about the slow frames.

**What.** With the word, each frame is timed end to end and split into its ticks, its drawing, the frame's end (waiting for the GE, the swap), the rest, and the blocking Memory Stick reads made meanwhile. Every 300 frames one log line gives the window's average, its frames over 66.7 ms and the game time lost, its three slowest frames split, and the run's 99th and 99.9th percentile frame times (a 1 ms histogram, allocated only when the word is on). `python3 tools/frametrace.py LOG` prints the summary and groups the slow frames by what took most of each.

**Proven** [E] (lab 38701110999): play and seeds identical with it on (d20, 32,041 ticks); its lines come out every 300 frames and read back.

**First PSP figures** [M] (lab 38706758115, b30's b113 route, `nomeaudio`, four runs): 99% 340–347 ms, 99.9% 489–652 ms; every frame over 66.7 ms, so the game played 32–34% of real time. The windows' slowest frames: drawing 13–17, ticks 13–15, stick reads 1 (768–1,138 ms, mostly reading). The worst of those is the log's doing: near game time 3,600 the log's write held the stick 1.3–1.5 s and an animation read waited ~770 ms behind it (b127's `logbatch127` starts on the log's side).

**Hooks.** `psp_tick_account` and `psp_frame_account` (`psp_platform.c`) pass each tick's and frame's time; the texture, geometry and animation readers add each blocking read's time to `psp_stick_read_us` (asynchronous geometry reads, which overlap other work, aren't counted).

## Found along the way

- **d20's hands-off stop** after "SOUND CACHE BLOWN" (seen on two early b125 commits) doesn't reproduce on b125's code or b126, and the sound cache's debug dump wasn't the cause (lab 38697891379). Likely gone with streamoom or trim125's heap changes [U].
- **b30 across builds** (open #25): zero-initialising the stack doesn't stop the split (tick 1,357); mixing audio on the main CPU delays it (1,459) but doesn't stop it; the decal vertex cache already differs at tick 0. Drawing feeding back into play is the next suspect [U].
- **The sleep system's checksums** are ~10% of b30's beach tick in the cache models, and the checked object and tag lookups ~13%: the two largest port-side items left in b30's tick (findings, lab 38698483247).
