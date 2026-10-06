/* psp_geo.c - the geometry cache (build 68)

Why: a campaign map's tags are 14-17 MB against the PSP-1000's 9 MB tag
window, and 4.5-5.4 MB of that is model vertex and index data, plus 2-4 MB
of the level's render vertices in the BSP. None of it is read by the game's
logic: the renderer draws it, and object lighting looks up three vertices'
texture coordinates now and then. So the map is built without it (the
Invader fork's -G option): the data goes to ms0:/halo/maps/<map>.pgeo, and
every pointer to it in the tags is 0x0C000000 + its offset in that file.

This cache turns such a pointer back into bytes in RAM: psp_geo_lock(address,
size) answers with a resident copy, or NULL and a read queued. Reads are
asynchronous (sceIoReadAsync: the Memory Stick's DMA fills the block while
the CPU goes on; halo_bench v10 on Sheepdog01's PSP-1000: a 1 MB read in 65 ms with
97% of the CPU kept, as long as the file's async priority sits above the
game thread's, or the completion never runs). A block that isn't there yet
is skipped for a frame, as the Xbox's texture cache skipped a texture.

The arena is carved from the texture RAM cache (the volatile partition:
reloadable data, given back before the PSP sleeps). Blocks are evicted
oldest first, never one locked this frame or last.

What the renderer must know: a block's address can be reused for another
range after an eviction, so anything it worked out about the bytes there
(the in-place conversion of model vertices, a skinning plan) is tagged with
psp_geo_generation(), which changes with every load into the block. */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

void platform_log(const char *format, ...);
int psp_keep(const char *name);

#define EXTERNAL_BASE 0x0C000000u
#define HEADER_BYTES 64
#define GEO_ENTRIES 512
#define GEO_PENDING 48
#define RECENT_FRAMES 2
#ifndef ARENA_BYTES
#define ARENA_BYTES (1024 * 1024)
#endif
#define ASYNC_PRIORITY 0x10 /* above the game thread's 0x20 and the audio thread's 0x12 */

struct geo_entry
{
	unsigned int address; /* external */
	unsigned int size;
	unsigned int at; /* resident or loading block, 0 if neither */
	unsigned int last_frame;
	unsigned int generation;
	unsigned char state; /* 0 nothing, 1 queued, 2 loading, 3 resident */
	unsigned char pad[3];
};

static SceUID pack = -1;
static unsigned int pack_size = 0, pack_crc = 0;
static char pack_name[64];
static unsigned int arena_start = 0, arena_end = 0, arena_next = 0;
static struct geo_entry entries[GEO_ENTRIES];
static int entry_count = 0;
static unsigned short pending[GEO_PENDING];
static int pending_count = 0;
static int inflight = -1;
static unsigned int frame = 1, generation_counter = 0;
static unsigned long hits = 0, misses = 0, loads = 0, load_bytes = 0, evictions = 0, too_big = 0, failures = 0,
	miss_frames = 0, queue_full = 0, no_room = 0;
static int frame_missed = 0;
static unsigned int read_start_us = 0;
static unsigned long read_us = 0, read_worst_us = 0;

int psp_geo_open_count(void) { return pack >= 0; }

/* the map's layout, from its first model part's vertex descriptor: a map
built with its geometry outside (invader-build -G) keeps 6 bytes a level
vertex in the tags (three int16 about the material's centroid, times a scale
in the unused uncompressed vertex data's size field), its leaf surface
references as two u16 and its surface index lists as u16; one built as
before keeps the Xbox's 32-byte vertices and 32-bit indices. Read by the
game's own readers of level data (structures.c, structure_visibility.c,
object_lights.c; psp_fixups.py). Set at map open whether or not the pack
file is there */
long psp_structure_vertex_stride = 32;

static int alignment(unsigned int size)
{
	return (size + 63) & ~63u;
}

static struct geo_entry *find(unsigned int address)
{
	int index;

	for (index = 0; index < entry_count; index++)
		if (entries[index].address == address)
			return &entries[index];
	return NULL;
}

static int protected_entry(const struct geo_entry *e)
{
	return e->state == 2 || (e->at && e->last_frame + RECENT_FRAMES > frame);
}

/* room in the arena for size bytes: at the ring position, the start, or
after a protected block; whatever unprotected lies in the chosen space is
evicted. 0 if nothing fits */
static int fits(unsigned int start, unsigned int size)
{
	unsigned int end = start + size;
	int index;

	if (end > arena_end)
		return 0;
	for (index = 0; index < entry_count; index++)
	{
		const struct geo_entry *e = &entries[index];

		if (e->at && protected_entry(e) && e->at < end && e->at + alignment(e->size) > start)
			return 0;
	}
	return 1;
}

static unsigned int allocate(unsigned int size)
{
	unsigned int start = 0;
	int index;

	if (!arena_start || size > arena_end - arena_start)
		return 0;
	if (fits(arena_next, size))
		start = arena_next;
	else if (fits(arena_start, size))
		start = arena_start;
	else
		for (index = 0; index < entry_count && !start; index++)
		{
			unsigned int candidate;

			if (!entries[index].at || !protected_entry(&entries[index]))
				continue;
			candidate = entries[index].at + alignment(entries[index].size);
			if (fits(candidate, size))
				start = candidate;
		}
	if (!start)
		return 0;
	for (index = 0; index < entry_count; index++)
	{
		struct geo_entry *e = &entries[index];

		if (e->at && e->at < start + size && e->at + alignment(e->size) > start)
		{
			e->at = 0;
			e->state = 0;
			evictions++;
		}
	}
	arena_next = start + size;
	return start;
}

/* the read in flight, if it has finished */
static void poll(void)
{
	SceInt64 result = 0;
	struct geo_entry *e;

	if (inflight < 0)
		return;
	if (sceIoPollAsync(pack, &result) != 0)
		return;
	e = &entries[inflight];
	inflight = -1;
	read_us += sceKernelGetSystemTimeLow() - read_start_us;
	if ((unsigned int)result == e->size && e->at)
	{
		/* whichever way the kernel filled the block (the card's DMA, or a copy
		through the cache), the CPU's view is made to agree with memory */
		sceKernelDcacheWritebackInvalidateRange((void *)e->at, e->size);
		e->state = 3;
		e->generation = ++generation_counter;
		e->last_frame = frame;
		loads++;
		load_bytes += e->size;
	}
	else
	{
		if (failures++ < 8)
			platform_log("geometry: reading %u bytes at %08x failed (%ld)", e->size, e->address, (long)result);
		e->at = 0;
		e->state = 0;
	}
}

/* starts the next queued read, if none is in flight and the block has room */
/* build 72: synchronous reads unless keep.txt says "geoasync"; at most
SYNC_FRAME_BUDGET bytes a frame (about 12 ms at the Memory Stick's measured
16 MB/s) */
static int async_reads = 0;
static void *heap_arena = NULL;
static unsigned long window_reads = 0, window_us = 0, window_worst_us = 0;
#define SYNC_FRAME_BUDGET (192 * 1024)
static unsigned int frame_read_bytes = 0;
static unsigned long frame_read_us = 0, frame_read_worst_us = 0;

static void pump(void)
{
	while (inflight < 0 && pending_count > 0)
	{
		struct geo_entry *e = &entries[pending[0]];
		unsigned int at;

		if (e->state != 1)
		{
			/* dropped meanwhile */
			memmove(pending, pending + 1, (pending_count - 1) * sizeof(pending[0]));
			pending_count--;
			continue;
		}
		/* build 89: this frame's read share first, then the room: b72-b88
		allocated (evicting) and only then found the share spent, so each
		further miss that frame evicted a block for nothing (review) */
		if (!async_reads && frame_read_bytes >= SYNC_FRAME_BUDGET)
			return;
		at = allocate(alignment(e->size));
		if (!at)
		{
			/* everything is in use this frame: try again next frame */
			no_room++;
			return;
		}
		memmove(pending, pending + 1, (pending_count - 1) * sizeof(pending[0]));
		pending_count--;
		/* the previous tenant's lines, if any are still dirty in the cache,
		go back to memory now rather than over the new bytes later */
		sceKernelDcacheWritebackInvalidateRange((void *)at, alignment(e->size));
		if (!async_reads)
		{
			/* build 72: read now, on this thread. Sheepdog01's b71 run: with the
			reads asynchronous, every frame took 2.5 or 5 s, all of it in the
			frame's end (render_frame_present 97%), where the texture cache's
			own reads (synchronous, the same Memory Stick) wait behind them.
			"geoasync" in keep.txt brings the asynchronous reads back */
			unsigned int start = sceKernelGetSystemTimeLow();
			int got;

			if (frame_read_bytes >= SYNC_FRAME_BUDGET)
			{
				/* this frame's share is read: the rest waits for the next */
				pending[pending_count++] = (unsigned short)(e - entries);
				return;
			}
			got = sceIoLseek32(pack, HEADER_BYTES + (e->address - EXTERNAL_BASE), PSP_SEEK_SET) < 0 ? -1 :
				sceIoRead(pack, (void *)at, e->size);
			if (got != (int)e->size)
			{
				if (failures++ < 8)
					platform_log("geometry: reading %u bytes at %08x failed (%d)", e->size, e->address, got);
				e->state = 0;
				continue;
			}
			sceKernelDcacheWritebackInvalidateRange((void *)at, e->size);
			e->at = at;
			e->state = 3;
			e->generation = ++generation_counter;
			e->last_frame = frame;
			loads++;
			load_bytes += e->size;
			frame_read_bytes += e->size;
			{
				unsigned int took = sceKernelGetSystemTimeLow() - start;

				read_us += took;
				frame_read_us += took;
				window_reads++;
				window_us += took;
				if (took > window_worst_us)
					window_worst_us = took;
			}
			continue;
		}
		if (sceIoLseek32(pack, HEADER_BYTES + (e->address - EXTERNAL_BASE), PSP_SEEK_SET) < 0 ||
			sceIoReadAsync(pack, (void *)at, e->size) < 0)
		{
			if (failures++ < 8)
				platform_log("geometry: couldn't start reading %u bytes at %08x", e->size, e->address);
			e->state = 0;
			continue;
		}
		e->at = at;
		e->state = 2;
		inflight = (int)(e - entries);
		read_start_us = sceKernelGetSystemTimeLow();
	}
}

static struct geo_entry *entry_new(unsigned int address, unsigned int size)
{
	struct geo_entry *e = NULL;
	int index;

	if (entry_count < GEO_ENTRIES)
		e = &entries[entry_count++];
	else
	{
		/* the entry least recently used that isn't loading or queued */
		for (index = 0; index < GEO_ENTRIES; index++)
		{
			struct geo_entry *candidate = &entries[index];

			if (candidate->state == 1 || candidate->state == 2)
				continue;
			if (!e || candidate->last_frame < e->last_frame)
				e = candidate;
		}
		if (!e)
			return NULL;
		if (e->at)
			evictions++;
	}
	memset(e, 0, sizeof(*e));
	e->address = address;
	e->size = size;
	return e;
}

void *psp_geo_lock(unsigned int address, unsigned int size)
{
	struct geo_entry *e;

	if (pack < 0 || address < EXTERNAL_BASE || address - EXTERNAL_BASE + size > pack_size || !size)
		return NULL;
	e = find(address);
	if (e && e->state == 3 && e->size >= size)
	{
		e->last_frame = frame;
		hits++;
		return (void *)e->at;
	}
	if (e && e->size < size && (e->state == 3 || e->state == 0))
	{
		/* asked for more than was read (a material's lightmap vertices after
		its own, say): read again, the longer way */
		if (e->state == 3)
			evictions++;
		e->at = 0;
		e->state = 0;
		e->size = size;
	}
	misses++;
	frame_missed = 1;
	if (inflight >= 0)
	{
		/* it may be the one just read */
		poll();
		if (e && e->state == 3)
		{
			e->last_frame = frame;
			return (void *)e->at;
		}
	}
	if (alignment(size) > arena_end - arena_start)
	{
		if (too_big++ < 8)
			platform_log("geometry: %u bytes at %08x is more than the cache's %u", size, address, arena_end - arena_start);
		return NULL;
	}
	if (!e)
		e = entry_new(address, size);
	if (!e)
		return NULL;
	if (e->state == 1 && e->size < size)
		e->size = size; /* still queued: read the longer way */
	if (e->state == 0)
	{
		if (pending_count >= GEO_PENDING)
		{
			queue_full++;
			return NULL;
		}
		pending[pending_count++] = (unsigned short)(e - entries);
		e->state = 1;
		e->last_frame = frame;
	}
	pump();
	if (!async_reads && e->state == 3 && e->size >= size)
	{
		e->last_frame = frame;
		return (void *)e->at;
	}
	return NULL;
}

/* the generation of the block holding ptr: changes with every load into
it, so the renderer knows whether what it worked out about the bytes there
still holds. 0: not the cache's (tag data as before) */
unsigned int psp_geo_generation(const void *ptr)
{
	unsigned int at = (unsigned int)ptr;
	int index;

	if (at < arena_start || at >= arena_end)
		return 0;
	for (index = 0; index < entry_count; index++)
	{
		const struct geo_entry *e = &entries[index];

		if (e->at && e->state == 3 && at >= e->at && at < e->at + e->size)
			return e->generation;
	}
	return 0;
}

/* the external address of the resident bytes at ptr (the lasting identity
of what's there), 0 if ptr isn't the cache's */
unsigned int psp_geo_address_of(const void *ptr)
{
	unsigned int at = (unsigned int)ptr;
	int index;

	if (at < arena_start || at >= arena_end)
		return 0;
	for (index = 0; index < entry_count; index++)
	{
		const struct geo_entry *e = &entries[index];

		if (e->at && e->state == 3 && at >= e->at && at < e->at + e->size)
			return e->address + (at - e->at);
	}
	return 0;
}

int psp_geo_external(unsigned int address)
{
	return pack >= 0 && address >= EXTERNAL_BASE && address < EXTERNAL_BASE + pack_size;
}

void psp_geo_frame_end(void)
{
	if (async_reads)
		poll();
	frame_read_bytes = 0;
	pump();
	if (frame_read_us > frame_read_worst_us)
		frame_read_worst_us = frame_read_us;
	frame_read_us = 0;
	if (frame_missed)
		miss_frames++;
	frame_missed = 0;
	if (pack >= 0 && frame % 300 == 0)
	{
		unsigned int resident = 0, resident_bytes = 0;
		int index;

		for (index = 0; index < entry_count; index++)
			if (entries[index].state == 3)
			{
				resident++;
				resident_bytes += entries[index].size;
			}
		platform_log("geometry: %lu hits, %lu misses (%lu frames missed something); %lu loaded (%lu KB), %lu evicted, %lu waited for room, %lu for the queue; %u blocks resident (%u KB of %u); reading %lu ms in all, worst frame %lu ms (%s)",
			hits, misses, miss_frames, loads, load_bytes / 1024, evictions, no_room, queue_full, resident, resident_bytes / 1024,
			(arena_end - arena_start) / 1024, read_us / 1000, frame_read_worst_us / 1000, async_reads ? "asynchronous" : "synchronous");
	}
	frame++;
}

static void forget_all(void)
{
	int index;

	if (inflight >= 0)
	{
		SceInt64 result;

		sceIoWaitAsync(pack, &result);
		inflight = -1;
	}
	for (index = 0; index < entry_count; index++)
	{
		entries[index].at = 0;
		entries[index].state = 0;
	}
	pending_count = 0;
	entry_count = 0;
	arena_next = arena_start;
}

void psp_geo_close(void)
{
	if (pack >= 0)
	{
		forget_all();
		sceIoClose(pack);
		pack = -1;
		platform_log("geometry: %s closed", pack_name);
	}
	pack_size = 0;
	arena_start = arena_end = arena_next = 0;
}

/* a map's tags were loaded: its geometry pack, if it was built with one.
Called before the texture pack opens, so the texture cache's RAM share is
settled first */
void psp_geo_map_opened(const char *name, unsigned int map_crc, const void *first_vertex_descriptor)
{
	char path[128];
	unsigned int header[16];
	void *arena;
	int outside = first_vertex_descriptor && ((const unsigned int *)first_vertex_descriptor)[1] >= EXTERNAL_BASE;

	psp_geo_close();
	psp_structure_vertex_stride = outside ? 6 : 32;
	if (!name || !name[0])
		return;
	if (!outside)
		return;
	snprintf(path, sizeof(path), "ms0:/halo/maps/%s.pgeo", name);
	pack = sceIoOpen(path, PSP_O_RDONLY, 0);
	if (pack < 0)
	{
		platform_log("geometry: the map keeps its geometry outside its tags, and there is no %s: nothing will be drawn", path);
		return;
	}
	if (sceIoRead(pack, header, sizeof(header)) != sizeof(header) || header[0] != 0x4F454750 /* PGEO */ || header[1] != 1)
	{
		platform_log("geometry: %s is not a geometry pack", path);
		sceIoClose(pack);
		pack = -1;
		return;
	}
	if (map_crc && header[2] != map_crc)
		platform_log("geometry: %s was built for another version of the map (crc %08x, map %08x): expect garbage",
			path, header[2], map_crc);
	pack_size = header[3];
	snprintf(pack_name, sizeof(pack_name), "%s", name);
	async_reads = psp_keep("geoasync");
	if (async_reads)
		sceIoChangeAsyncPriority(pack, ASYNC_PRIORITY);
	frame_read_bytes = 0;
	frame_read_us = frame_read_worst_us = 0;
	/* build 73: from the heap, not carved from the texture cache. Carved
	(b68-b72), Blood Gulch's 5 MB of textures no longer fit the RAM cache and
	were read from the Memory Stick as drawn, mid-frame, every frame: Sheepdog01's
	b71/b72 runs spent 1.7-5 s a frame at the frame's end. The heap has had
	1.5 MB it never reached (peak 3.4 of 5.0 MB) */
	if (!heap_arena)
		heap_arena = memalign(64, ARENA_BYTES);
	arena = heap_arena;
	if (!arena)
	{
		platform_log("geometry: no RAM for the cache; %s closed", path);
		sceIoClose(pack);
		pack = -1;
		pack_size = 0;
		return;
	}
	arena_start = arena_next = (unsigned int)arena;
	arena_end = arena_start + ARENA_BYTES;
	hits = misses = loads = load_bytes = evictions = too_big = failures = miss_frames = queue_full = no_room = 0;
	read_us = read_worst_us = 0;
	platform_log("geometry: %s, %u KB external; cache %u KB at %08x", path, pack_size / 1024, ARENA_BYTES / 1024, arena_start);
	{
		/* build 87: the stencil bake, keyed by this pack's addresses */
		extern void psp_ge_stencils_open(const char *name, unsigned int map_crc);

		psp_ge_stencils_open(name, map_crc);
	}
}

/* the PSP is about to sleep: the volatile partition goes back to the system
(psp_tex_suspend), and the blocks in it are forgotten */
void psp_geo_suspend(void)
{
	/* the arena is in the heap, which sleep keeps: only a read in flight is
	finished first */
	if (pack >= 0 && inflight >= 0)
	{
		SceInt64 result;

		sceIoWaitAsync(pack, &result);
		poll();
	}
}

void psp_geo_resume(void)
{
}

/* build 73: this heartbeat window's Memory Stick reads for geometry, for the
log's io: line (psp_platform.c) */
void psp_geo_io_window(unsigned long *reads, unsigned long *us, unsigned long *worst_us)
{
	*reads = window_reads;
	*us = window_us;
	*worst_us = window_worst_us;
	window_reads = window_us = window_worst_us = 0;
}

/* ---------- the renderer's and object lighting's way in */

struct vertex_buffer
{
	short type;
	unsigned short pad;
	long count;
	long offset;
	void *base_address;
	void *hardware_format;
};
struct triangle_buffer
{
	short type;
	unsigned short pad;
	long count;
	void *base_address;
	void *hardware_format;
};
long rasterizer_geometry_get_vertex_size(short type);

/* a vertex buffer's bytes: the cache's copy when the map keeps them outside,
the tag data otherwise (the D3D shim's lock is Data itself: physical and
virtual addresses are the same on the PSP). NULL: not there this frame */
void *psp_vertex_lock(const struct vertex_buffer *buffer)
{
	unsigned int address;

	if (!buffer || !buffer->hardware_format)
		return NULL;
	/* the hardware format is the 12-byte descriptor {Common, Data, Lock}; Data
	is the pointer the map gave */
	address = ((const unsigned int *)buffer->hardware_format)[1];
	if (psp_geo_external(address))
		return psp_geo_lock(address, (unsigned int)buffer->count * (unsigned int)rasterizer_geometry_get_vertex_size(buffer->type));
	return (void *)address;
}

/* a level vertex buffer's address as the map gives it, in the tags or in the
pack: psp_ge.c's level paths draw from their own copies of a material, and
take the pack's bytes through the cache only while building them */
void *psp_level_vertices(const struct vertex_buffer *buffer)
{
	if (!buffer || !buffer->hardware_format)
		return NULL;
	return (void *)((const unsigned int *)buffer->hardware_format)[1];
}

void *psp_index_lock(const struct triangle_buffer *buffer)
{
	unsigned int address, count;

	if (!buffer || !buffer->hardware_format)
		return NULL;
	address = ((const unsigned int *)buffer->hardware_format)[1];
	count = buffer->type == 1 /* precompiled strip */ ? (unsigned int)buffer->count + 2 : 3 * (unsigned int)buffer->count;
	if (psp_geo_external(address))
		return psp_geo_lock(address, count * 2);
	return (void *)address;
}
