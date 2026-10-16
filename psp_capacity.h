/*
PSP_CAPACITY.H

The PSP's memory capacity: its copy of the engine's halo_port_capacity.h.
psp_limits.h includes it, and it takes that header's include guard, so the
engine's own copy (sized for the desktop builds) is never read. Both bases
(halo-ce-universal and ChupathingyCE) read their pool sizes from here.

The values are the Xbox's (in parentheses where a PSP value differs), so a
level's pools are what the campaign was built for. ChupathingyCE's desktop
sizes (8,192 objects, 1,024 actors) are several times the PSP's game state.
*/

#ifndef __HALO_PORT_CAPACITY_H
#define __HALO_PORT_CAPACITY_H

/* ---------- game state

The Xbox game state is 0x345000 bytes at 0x80061000 and ends where the tag
cache begins (0x803A6000). On the PSP it sits just below the tag window
(psp_build.py's GAME_STATE_BASE has the same address: the heap ends below
it). The GPU part holds only the decal vertices, as on the Xbox. */

/* build 113: 64 KB lower (0x092B0000 to b112), for the CPU part's 64 KB
more below; the heap gives them. It must end at or below 0x09600000 */
#ifdef PSP_RECON_64MB
/* the 64 MB variant (emulator only; psp_build.py's PSP_BIG_RAM): above its
22 MB tag window, so the program and the heap have everything below the
window (PSP_PGO=generate's counters need it) */
#define HALO_PORT_GAME_STATE_BASE_ADDRESS 0x0AC00000
#else
#define HALO_PORT_GAME_STATE_BASE_ADDRESS 0x092A0000 /* PSP: just below the tag window (0x09600000) */
#endif
/* PSP, build 107: 64 KB moved from the GPU part to the CPU part, the total
unchanged. The GPU part holds only the decal vertices, 160 KB
(rasterizer_xbox_decals.c: 2,560 pages of 64 bytes) of its 256; b107's
object sphere copies (32 KB) took the CPU part past its end (game_state.c's
assert 159 at start-up, b107e on: harmless while the two parts didn't meet,
since checkpoints save the whole buffer, but one more allocation would have
put the CPU part's end inside the decal vertices) */
/* build 113: 64 KB more, for the render-state cache's 256 more entries of
256 bytes (b112 left 65 KB of the CPU part unallocated) */
#define HALO_PORT_GAME_STATE_CPU_SIZE 0x325000 /* (0x305000; b107-b112 0x315000) */
#define HALO_PORT_GAME_STATE_GPU_SIZE 0x30000 /* (0x40000) */
#define HALO_PORT_GAME_STATE_SIZE (HALO_PORT_GAME_STATE_CPU_SIZE+HALO_PORT_GAME_STATE_GPU_SIZE)
#ifndef PSP_RECON_64MB
typedef char halo_port_game_state_below_tag_window[
	HALO_PORT_GAME_STATE_BASE_ADDRESS + HALO_PORT_GAME_STATE_SIZE <= 0x09600000 ? 1 : -1];
#endif

/* ---------- the tag window and the texture cache

The tag window is 9 MB at 0x09600000, under the 1 MB left for thread
stacks; maps are baked for it (invader-build -X 0x09600000 -T 0x900000).
The multiplayer build's is smaller (below).
Build 44's recon variant (psp_build.py's PSP_BIG_RAM) has the Xbox's 22 MB,
for 64 MB models and the emulator set to one.

The GE takes its textures from the map's texture pack (build 25), so the
game's own texture cache only serves keep.txt's "textures": 64 KB since
build 30 (that switch thrashes it, but works). */

#ifdef PSP_RECON_64MB
#define PSP_TAG_CACHE_SIZE 0x01600000 /* (0x1600000) */
#elif defined(PSP_MULTIPLAYER)
/* The multiplayer build: 6 MB, the same base, ending 3 MB lower. The stock
multiplayer maps take 2.3-3.3 MiB of tag space as baked (Invader's count:
the tags and the BSP; Sidewinder the most, ui 1.06; the bake of
2026-10-10), so 6 MB leaves room for custom maps. The bake builds the
multiplayer maps and ui for it (bake.py's MULTIPLAYER_TAG_SPACE): a
structure BSP loads at the end of the window it was built for, so they load
in both builds. The 3 MB given back is user RAM outside the heap, where the
network modules, the sound cache and the display list go (psp_memory.c's
outside_alloc), and the heap keeps its room for the rest
(docs/multiplayer_plan.md, section 4) */
#define PSP_TAG_CACHE_SIZE 0x600000 /* (0x1600000) */
#else
#define PSP_TAG_CACHE_SIZE 0x900000 /* (0x1600000) */
#endif
#define PSP_TEXTURE_CACHE_SIZE 0x10000 /* (0x1600000) */
#define HALO_PORT_TEXTURE_CACHE_SIZE PSP_TEXTURE_CACHE_SIZE

/* ---------- structure rendering */

#define HALO_PORT_MAXIMUM_RENDERED_ENVIRONMENT_SURFACES 16384
#define HALO_PORT_MAXIMUM_DYNAMIC_TRIANGLES 32768

/* ---------- transparent geometry, particles and lights a frame draws

The Xbox's, as ChupathingyCE's Android guest and Xbox builds keep them: the
desktop builds' larger lists (2048, 2048, 512) are per-frame arrays the
PSP's heap can't spare */

#define HALO_PORT_MAXIMUM_TRANSPARENT_GEOMETRY_GROUPS 384
#define HALO_PORT_MAXIMUM_RENDERED_PARTICLE_GROUPS 512
#define HALO_PORT_MAXIMUM_RENDERED_LIGHTS 128

/* ---------- AI */

#define HALO_PORT_MAXIMUM_ACTORS 256
#define HALO_PORT_MAXIMUM_PROPS 768
#define HALO_PORT_MAXIMUM_SWARMS 32
#define HALO_PORT_MAXIMUM_SWARM_COMPONENTS 256

/* ---------- objects */

#define HALO_PORT_MAXIMUM_OBJECTS_PER_MAP 2048
#define HALO_PORT_OBJECT_MEMORY_POOL_SIZE 0x100000
/* each of the two reference lists of every cluster partition (collideable
objects, noncollideable objects, lights) */
#define HALO_PORT_MAXIMUM_CLUSTER_REFERENCES 2048
#define HALO_PORT_MAXIMUM_RENDERED_OBJECTS 256
/* build 113: 512 (the Xbox's 256 with "renderstates256", render_objects.c).
Each object drawn keeps its lighting here; past the cache's size every miss
scans all of it for the oldest and works the lighting out whole, and on b30's
cliffs (350 scenery objects) it stayed full for minutes (b112 run 5) */
#define HALO_PORT_MAXIMUM_CACHED_OBJECT_RENDER_STATES 512
/* objects one explosion can damage */
#define HALO_PORT_MAXIMUM_AREA_OF_EFFECT_OBJECTS 64
/* script object lists, and the object references they all share */
#define HALO_PORT_MAXIMUM_OBJECT_LISTS_PER_MAP 48
#define HALO_PORT_MAXIMUM_LISTED_OBJECTS_PER_MAP 128
/* widgets (light volumes, antennas, flags, glows, lightning) */
#define HALO_PORT_MAXIMUM_WIDGETS 64
#define HALO_PORT_MAXIMUM_LIGHT_VOLUMES 256

/* ---------- effects, particles, lights and sounds */

#define HALO_PORT_MAXIMUM_EFFECTS 256
#define HALO_PORT_MAXIMUM_EFFECT_LOCATIONS 512
#define HALO_PORT_MAXIMUM_PARTICLES 1024
#define HALO_PORT_MAXIMUM_PARTICLE_SYSTEMS 64
#define HALO_PORT_MAXIMUM_SYSTEM_PARTICLES 512
#define HALO_PORT_MAXIMUM_CONTRAILS 256
#define HALO_PORT_MAXIMUM_CONTRAIL_POINTS 1024
#define HALO_PORT_MAXIMUM_LIGHTS_PER_MAP 896
#define HALO_PORT_MAXIMUM_GAME_LOOPING_SOUNDS 1024

#endif /* __HALO_PORT_CAPACITY_H */
