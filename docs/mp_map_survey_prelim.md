# The vanilla multiplayer maps: a preliminary memory survey (2026-10-14)

What the 13 stock multiplayer maps cost the PSP's memory, from data that already existed. No new runs. Labels as in CLAUDE.md: [M] measured on the PSP, [E] emulator, [D] read in code or data, [U] inference.

**Sources:**
- The latest full bake, `sierra-bake/38524010598` (b121's bundle, `/bake/campaign121`), for sizes, tag space and bake times.
- The only two emulator runs of the multiplayer build on these maps, `sierra-test/38082751432` (Blood Gulch) and `sierra-test/38083090554` (Sidewinder). Both are build 111 + multiplayer, 6 MB window, 200 s, **no input: the player stood at the spawn**.

## 1. Per map: what the bake makes [M on the desktop, D]

| Map | Tag space (of 6.00 MiB) | `.pgeo` MB | `.ptex` MB | `.pstn` KB | Bake time, desktop |
|---|---:|---:|---:|---:|---:|
| sidewinder | **3.30** | 4.22 | 4.98 | 28.3 | 133 s |
| bloodgulch | 3.10 | 3.84 | **5.19** | 21.6 | 119 s |
| boardingaction | 2.88 | 4.06 | 4.63 | 8.3 | 98 s |
| putput | 2.87 | 4.14 | 3.96 | 16.0 | 108 s |
| damnation | 2.85 | 4.05 | 4.59 | 8.1 | 125 s |
| beavercreek | 2.58 | 3.78 | 4.95 | 18.1 | 111 s |
| prisoner | 2.50 | 3.87 | 4.35 | 26.3 | 114 s |
| hangemhigh | 2.40 | 3.72 | 4.57 | **85.5** | 124 s |
| ratrace | 2.40 | 3.73 | 4.43 | 10.6 | 118 s |
| carousel | 2.39 | 3.71 | 4.33 | 65.2 | 106 s |
| chillout | 2.39 | 3.72 | 4.21 | 14.8 | 112 s |
| wizard | 2.39 | 3.72 | 4.15 | 23.5 | 110 s |
| longest | 2.29 | 3.69 | 4.24 | 20.5 | 113 s |
| ui | 1.06 | 0.12 | 6.46 | — | 87 s |

- **Tag space:** every map uses 38–55% of the 6 MB window. The window is nowhere near the binding limit for stock maps.
- **Bake time:** 98–133 s a map on the desktop [M]. This replaces the earlier "3–10 minutes" estimate for multiplayer maps.
- The texture pack is the largest per-map file after the `.map`, but in the multiplayer build it doesn't touch the heap (§2).

## 2. What's in the multiplayer heap [E, both runs]

The heap took 3,164 KB (Blood Gulch) and 3,184 KB (Sidewinder) from the system, with 0 failed requests.

| Block | KB | Map-dependent? |
|---|---:|---|
| Geometry cache | 1,024 | **No: fixed.** In the heap on every multiplayer map, because the whole texture pack fits the texture caches (Blood Gulch: 483 of 501 textures read in at the start, 5,056 KB), so the cache takes no texture space (b94's rule) |
| The level copies' arena | 1,056 | **The size is fixed; its use isn't:** Blood Gulch 234 KB, Sidewinder 337 KB at spawn, nothing overflowed |
| Rasterizer pool, index buffer, `sounds` data array | 96 + 192 + 86 | No |
| Skinning plans | 38 / 39 | Yes: multi-bone models in view |
| Everything else (sound streams, the decal bake's data, small allocations) | ~650 | Partly: the decal bake scales with `.pstn` |

**Outside the heap** (user RAM freed by the 6 MB window): sound cache 1,536 KB, display list 512 KB, the start-up's contiguous blocks, the network modules. After all that, 900 KB of user RAM outside the heap stayed free (largest block 717 KB).

So **~2.45 MB of the ~3.2 MB is fixed and identical on every map.** What differs between maps is the arena's *use*, the skinning plans and the decal data, all a few hundred KB.

## 3. The game state on multiplayer maps [E]

- 3,306 KB allocated of 3,348, sized for the campaign.
- **Pools with peak 0 on both maps:** actors 457 KB (0 of 256), props 234 KB (0 of 768), script threads 134 KB (0 of 256), swarm components 16 KB, particles 64 KB (0 of 512 at the spawn). Multiplayer maps run no AI.
- **That's ~825 KB a multiplayer-only build could size down** [U]. Caution: the object pool must stay at the host's size (the netcode names objects by datum index, so client and host must agree). Actors and props aren't networked in a non-co-op game, but that needs a code read before cutting.

## 4. What this survey can't say yet

- **Peaks.** Both runs stood at the spawn. The arena's worst view, the skinning plans in a busy fight, and the particles and effects in combat are all unmeasured.
- **11 of 13 maps** have never run in the multiplayer build.
- **Player count.** One player, offline. Remote players add bodies (game state), models in view (skinning plans) and effects.

## 5. The run that fills it in

For all 13 maps in the emulator, multiplayer build, PSP-1000 memory:
1. A **tour**: the player moved through every BSP cluster and every spawn point. Either a route per map (`route.txt`, as b30's), or a scripted wander (ChupathingyCE's `debug.test_input` bot, if it drives offline; to check).
2. Read the heap line (in use, taken from the system, failures), the arena line (peak use, overflow to the heap) and the skinning-plan peak.
3. Then again with N clients (the load generator) for the player-count curve.

All of it is emulator time on the desktop. No hardware.

## Levers found on the way (for the multiplayer build)
1. The `sv_unban` 1 MB buffer (`mp_v075a_and_host_memory.md`): [D], certain.
2. The AI pools at 0 (§3): ~825 KB [U], needs the read above.
3. Host-only netcode and co-op units (`mp_v075a_and_host_memory.md`): ~900 KB [U].
