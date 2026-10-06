/*
HALO_PORT_CAPACITY.H

Memory capacity of the native builds (Windows, Linux, Android), sized for the
session limits in halo_port_limits.h, which includes this file. The Xbox
sizes are given in parentheses below.

Every machine in a session must be built with the same values: the
distributed netcode names objects and players by their datum index, the
same on every machine (port/linux/game/network_objects.c tracks
MAXIMUM_TRACKED_OBJECTS = HALO_PORT_MAXIMUM_OBJECTS_PER_MAP objects, and a
client's own objects take the upper half of the object array).
*/

#ifndef __HALO_PORT_CAPACITY_H
#define __HALO_PORT_CAPACITY_H

/* ---------- game state

The Xbox game state is 0x345000 bytes at 0x80061000 and ends where the tag
cache begins (0x803A6000). Cache files are linked to that tag cache address,
so the game state cannot grow in place. The native builds put a 16 MB game
state above the tag cache (which ends at 0x819A6000), inside the Xbox memory
window (0x80000000-0x88000000, port/linux/src/platform.h) and below everything
the window hands out top-down (texture and sound caches, Direct3D resources).

The CPU part holds about 13.6 MB of pools at the sizes below (the Xbox pools
fill 3,165,260 of its 0x305000 bytes); the GPU part holds only the decal
vertices, as on the Xbox. */

#define HALO_PORT_GAME_STATE_BASE_ADDRESS 0x092B0000 /* PSP: just below the tag window (0x09600000) */
#define HALO_PORT_GAME_STATE_CPU_SIZE 0x305000 /* PSP: Xbox value */
#define HALO_PORT_GAME_STATE_GPU_SIZE 0x40000 /* PSP: Xbox value */
#define HALO_PORT_GAME_STATE_SIZE (HALO_PORT_GAME_STATE_CPU_SIZE+HALO_PORT_GAME_STATE_GPU_SIZE)

/* ---------- objects */

#define HALO_PORT_MAXIMUM_OBJECTS_PER_MAP 2048 /* PSP: Xbox value */
#define HALO_PORT_OBJECT_MEMORY_POOL_SIZE 0x100000 /* PSP: Xbox value */
/* each of the two reference lists of every cluster partition (collideable
objects, noncollideable objects, lights) */
#define HALO_PORT_MAXIMUM_CLUSTER_REFERENCES 2048 /* PSP: Xbox value */
#define HALO_PORT_MAXIMUM_RENDERED_OBJECTS 256 /* PSP: Xbox value */
#define HALO_PORT_MAXIMUM_CACHED_OBJECT_RENDER_STATES 256 /* PSP: Xbox value */
/* objects one explosion can damage */
#define HALO_PORT_MAXIMUM_AREA_OF_EFFECT_OBJECTS 64 /* PSP: Xbox value */
/* object references shared by all script object lists */
#define HALO_PORT_MAXIMUM_LISTED_OBJECTS_PER_MAP 128 /* PSP: Xbox value */

/* ---------- effects, particles, lights and sounds */

#define HALO_PORT_MAXIMUM_EFFECTS 256 /* PSP: Xbox value */
#define HALO_PORT_MAXIMUM_EFFECT_LOCATIONS 512 /* PSP: Xbox value */
#define HALO_PORT_MAXIMUM_PARTICLES 1024 /* PSP: Xbox value */
#define HALO_PORT_MAXIMUM_PARTICLE_SYSTEMS 64 /* PSP: Xbox value */
#define HALO_PORT_MAXIMUM_SYSTEM_PARTICLES 512 /* PSP: Xbox value */
#define HALO_PORT_MAXIMUM_CONTRAILS 256 /* PSP: Xbox value */
#define HALO_PORT_MAXIMUM_CONTRAIL_POINTS 1024 /* PSP: Xbox value */
#define HALO_PORT_MAXIMUM_LIGHTS_PER_MAP 896 /* PSP: Xbox value */
#define HALO_PORT_MAXIMUM_GAME_LOOPING_SOUNDS 1024 /* PSP: Xbox value */

#endif /* __HALO_PORT_CAPACITY_H */
