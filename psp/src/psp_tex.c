/* psp_tex.c: textures for the GE renderer (build 20)

The map's bitmaps are baked offline (tools/texpack.py) into a texture pack,
ms0:/halo/maps/<map>.ptex: swizzled CLUT8 textures with their palettes and
mip chains, at a resolution chosen for the PSP's 272 lines. A texture is
found by the address of its bitmap_data in the tag window, which is fixed
for a map built for the PSP.

Textures live in two caches: the VRAM after the depth buffer (1,232 KB; the
on-screen log there stops once the GE runs), and the 4 MB volatile partition
(or 256 KB of heap without it) for what doesn't fit (the
GE reads either; VRAM is faster and stays off the CPU's bus). Each is filled
like a ring: a new texture goes after the last one, wrapping at the end, and
whatever it lands on is evicted, unless that texture is drawn in the current
frame (then the next cache is tried, or it waits a frame).
Loads are read from the Memory Stick with a budget per frame, so a new view
fills in over a few frames instead of stopping the game; until a texture
arrives its surfaces are drawn in their flat colour. */

#include <pspkernel.h>
#include <pspgu.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <pspsuspend.h>
#include <pspdmac.h>

void platform_log(const char *format, ...);
void *tag_get(long group_tag, long tag_index);
int psp_keep(const char *name);

struct pack_entry
{
	unsigned int key;
	unsigned short width, height;
	unsigned char levels, psm, clut, flags;
	unsigned int offset, size;
};

#define VRAM_CACHE_START 0x000D0000 /* after two frame buffers, the depth buffer and the motion sensor's 16 KB target (psp_ge.c) */
#define VRAM_CACHE_END_FULL 0x00200000 /* the end of VRAM: the on-screen log stops once the GE runs (psp_platform.c) */
/* build 93: benchrun keeps VRAM's last 256 KB for its bus tests (the display
list in VRAM, the VRAM-only GE load; psp_ge.c), for the whole run so every
switch has the same texture placement */
#define VRAM_BENCH_BYTES 0x40000
static unsigned int vram_cache_end = VRAM_CACHE_END_FULL;
#define VRAM_CACHE_END vram_cache_end
#define LOAD_BUDGET_BYTES (96 * 1024) /* a frame's Memory Stick reads: ~7 ms */
#define STAGING_BYTES (32 * 1024) /* build 68: 32 (was 64): 32 KB of zeroed data back; the Memory Stick gives 14.5 MB/s at 32 KB against 15.7 at 64 (halo_bench v10) */

/* the lightmap page the next level surfaces use (rasterizer.c, psp_fixups.py) */
unsigned int psp_lightmap_key = 0;

static SceUID pack = -1;
static struct pack_entry *entries = NULL;
static int entry_count = 0;
/* per entry: where it is in the cache (0 if not resident), the last frame
it was drawn in */
static unsigned int *resident_at = NULL, *last_frame = NULL;
static unsigned char *resident_in = NULL;
/* build 44: binds per entry since the last report, to see which textures
the GE samples most and whether those are the ones in VRAM (the GE reads
VRAM about ten times faster than main memory: YAPSPD 11.7) */
static unsigned short *bind_counts = NULL;
/* build 46: binds per entry over every run on this map, learned into
ms0:/halo/maps/<map>.vram (the earlier runs' totals halved as they load, so
the counts follow how the map is played now); with keep.txt's "vramranked",
the next run's VRAM set is chosen from them instead of by size */
static unsigned int *bind_totals = NULL;
static char map_name[64];
/* per entry: its average colour once worked out (psp_tex_average) */
static unsigned int *averages = NULL;
static int averages_count = 0;
/* build 39: the textures waiting for VRAM until the first frame (preload) */
static unsigned short vram_pending[512];
static int vram_pending_count = 0;

/* a cache: GE addresses [start, end), filled from next; the CPU writes
through write_offset (VRAM's or RAM's uncached view) */
struct arena
{
	unsigned int start, end, next, write_offset;
};
#define RAM_CACHE_BYTES (256 * 1024)
static struct arena arenas[2] = {
	{ 0x04000000 + VRAM_CACHE_START, 0x04000000 + VRAM_CACHE_END_FULL, 0x04000000 + VRAM_CACHE_START, 0x40000000 },
	{ 0, 0, 0, 0x40000000 } };
static void *ram_cache = NULL;
static unsigned int frame = 1, frame_loaded = 0;
static unsigned long loads = 0, load_bytes = 0, misses = 0, waits = 0, evictions = 0, preloaded = 0;
#define RECENT_FRAMES 60
/* this frame's textures: how many, how many bytes; the largest seen */
static unsigned long frame_set_bytes = 0, frame_set_count = 0, frame_set_peak = 0, frame_set_peak_count = 0;
/* build 67: frames in which a bind found its texture missing and couldn't
read it in time (the surface drew untextured that frame), and the longest run
of such frames in a row */
static unsigned long untextured_frames = 0, untextured_run = 0, untextured_run_worst = 0;
static int frame_untextured = 0;
static unsigned char staging[STAGING_BYTES] __attribute__((aligned(64)));

static int alignment(unsigned int size)
{
	return (size + 63) & ~63u;
}

static void bind_totals_save(void);
static void bind_totals_gather(void);
/* build 89: the heartbeat's in-play save (psp_tex_save_pending) */
static volatile int snapshot_pending = 0, snapshot_writing = 0;

void psp_tex_close(void)
{
	/* build 47: the learned binds saved as the map closes (b46 wrote them
	every 2,700 frames during play: a 40-56 ms pause each time) */
	bind_totals_gather();
	/* build 89: not over the heartbeat's in-play save of the same file */
	{
		int k;

		snapshot_pending = 0;
		for (k = 0; snapshot_writing && k < 100; k++)
			sceKernelDelayThread(2000);
	}
	bind_totals_save();
	if (pack >= 0)
		sceIoClose(pack);
	pack = -1;
	free(entries);
	free(resident_at);
	free(last_frame);
	free(resident_in);
	free(averages);
	averages = NULL;
	averages_count = 0;
	vram_pending_count = 0;
	entries = NULL;
	resident_at = last_frame = NULL;
	resident_in = NULL;
	free(bind_counts);
	bind_counts = NULL;
	free(bind_totals);
	bind_totals = NULL;
	entry_count = 0;
	if (psp_keep("benchrun"))
		vram_cache_end = VRAM_CACHE_END_FULL - VRAM_BENCH_BYTES;
	arenas[0].end = 0x04000000 + VRAM_CACHE_END;
	arenas[0].next = arenas[0].start;
	arenas[1].next = arenas[1].start;
}

/* the RAM cache: the 4 MB volatile partition outside the game's 24 MB when the
system lends it (it is the sleep save area, so it is given back before the
PSP sleeps and its textures are reloaded after: they are only a cache), or
256 KB of the heap (build 22) */
static int volatile_locked = 0;

/* build 67: "texsqueeze" caps the RAM cache at 1 MB and "texvramonly" leaves
it empty, so a map that otherwise fits the caches whole (Blood Gulch: 5 MB in
1.2 MB of VRAM and the 4 MB partition, 0 evictions in Sheepdog01's b64 run) has to
stream textures from the Memory Stick during play. The first measure of
in-play streaming on hardware: loads, waits, evictions, the worst frame's
reading, and how many frames drew something untextured meanwhile */
#define SQUEEZED_BYTES (1024 * 1024)
static int squeezed = 0;
/* build 68: the geometry cache's share at the end of the block (psp_geo.c) */
static unsigned int ram_cache_bytes = 0, carved_bytes = 0;

static void ram_cache_acquire(void)
{
	void *block = NULL;
	int size = 0, given;

	if (sceKernelVolatileMemTryLock(0, &block, &size) == 0 && block && size > 0)
	{
		volatile_locked = 1;
		ram_cache = block;
	}
	else
	{
		size = RAM_CACHE_BYTES;
		ram_cache = memalign(64, size);
	}
	given = size;
	ram_cache_bytes = (unsigned int)size;
	if (!squeezed)
		squeezed = psp_keep("texvramonly") ? 2 : psp_keep("texsqueeze") ? 1 : -1;
	if (squeezed == 1 && size > SQUEEZED_BYTES)
		size = SQUEEZED_BYTES;
	else if (squeezed == 2)
		size = 0;
	if ((unsigned int)size > ram_cache_bytes - carved_bytes)
		size = (int)(ram_cache_bytes - carved_bytes);
	if (ram_cache)
	{
		arenas[1].start = arenas[1].next = (unsigned int)ram_cache;
		arenas[1].end = (unsigned int)ram_cache + size;
		sceKernelDcacheWritebackInvalidateRange(ram_cache, given);
	}
	platform_log("textures: RAM cache %d KB (%s%s%s)", size / 1024, volatile_locked ? "volatile partition" : "heap",
		squeezed == 1 ? ", squeezed by keep.txt" : squeezed == 2 ? ", left empty by keep.txt" : "",
		carved_bytes ? ", the rest the geometry cache's" : "");
}

/* build 68: bytes at the end of the RAM block for the geometry cache (0 gives
them back). Called as a map's tags load, before its texture pack is read in,
so no texture is in the carved part; the block is acquired if it isn't yet */
void *psp_tex_carve(unsigned int bytes)
{
	int index;

	if (!ram_cache)
		ram_cache_acquire();
	if (!ram_cache || bytes > ram_cache_bytes)
		return NULL;
	carved_bytes = bytes;
	if (arenas[1].end > (unsigned int)ram_cache + ram_cache_bytes - carved_bytes)
		arenas[1].end = (unsigned int)ram_cache + ram_cache_bytes - carved_bytes;
	else if (!bytes && squeezed != 1 && squeezed != 2)
		arenas[1].end = (unsigned int)ram_cache + ram_cache_bytes;
	if (arenas[1].next > arenas[1].end)
		arenas[1].next = arenas[1].start;
	/* anything resident past the end is forgotten */
	for (index = 0; index < entry_count; index++)
		if (resident_at && resident_at[index] && resident_in[index] == 1 &&
			resident_at[index] + alignment(entries[index].size) > arenas[1].end)
			resident_at[index] = 0;
	return bytes ? (void *)((unsigned int)ram_cache + ram_cache_bytes - carved_bytes) : NULL;
}

/* the PSP is about to sleep (psp_platform.c's power callback): the volatile
partition goes back to the system, and its textures are forgotten */
void psp_tex_suspend(void)
{
	int index;

	if (!volatile_locked)
		return;
	for (index = 0; index < entry_count; index++)
		if (resident_in && resident_at[index] && resident_in[index] == 1)
			resident_at[index] = 0;
	arenas[1].start = arenas[1].end = arenas[1].next = 0;
	sceKernelVolatileMemUnlock(0);
	volatile_locked = 0;
	ram_cache = NULL;
}

/* awake again: the partition back, empty */
void psp_tex_resume(void)
{
	if (!ram_cache)
		ram_cache_acquire();
}

static int load(int index);
static int load_into(int index, int only);
static void preload_vram(void);
static int screen_free;

/* build 38: the whole pack read in at the start when it fits the caches
(Blood Gulch's 5.1 MB in 1.2 MB of VRAM and the 4 MB partition): the ring
of build 33 still evicted textures last drawn over two seconds ago to make
room when the cache could have held everything, and in the tank fight they
came back from the Memory Stick (216 evictions). Largest first, so the big
ones get VRAM; anything that doesn't fit is read when first drawn, as before */
/* an ordering of entries, scratch for preload() and the 900-frame report */
static unsigned short order[1024];

static void preload(int ranked)
{
	unsigned int total = 0, room, start = sceKernelGetSystemTimeLow();
	int index, loaded = 0, order_count = 0, k;

	for (index = 0; index < entry_count; index++)
		total += (entries[index].size + 63) & ~63u;
	room = (VRAM_CACHE_END - VRAM_CACHE_START) + (arenas[1].end - arenas[1].start);
	/* build 39: a pack a little over the caches (Blood Gulch's with the HUD
	at full size: 5322 KB of 5312) is still read in as far as it goes, the
	few left over read when first drawn; one far over is read as drawn */
	if (total > room + room / 8 || entry_count > 1024 || !arenas[1].start)
	{
		platform_log("textures: %u KB doesn't fit the caches' %u KB: read as drawn", total / 1024, room / 1024);
		return;
	}
	if (ranked)
	{
		/* build 46 ("vramranked"): by binds per byte of VRAM, with a
		texture's worth counted as binds x min(size, 16 KB): a bigger texture
		gives the GE more to read a draw, but a draw reads one mip level of
		it, not the whole chain. A guess to be measured against "largest
		first" (the GE's own time is the test: "waited for the GE"). Those
		never bound sort last, largest first, as before */
		static float density[1024];

		for (index = 0; index < entry_count; index++)
		{
			unsigned int size = alignment(entries[index].size);

			density[index] = bind_totals[index] ? (float)bind_totals[index] * (size < 16384 ? size : 16384) / size : 0.0f;
			for (k = order_count; k > 0 && (density[order[k - 1]] < density[index] ||
				(density[order[k - 1]] == density[index] && entries[order[k - 1]].size < entries[index].size)); k--)
				order[k] = order[k - 1];
			order[k] = (unsigned short)index;
			order_count++;
		}
	}
	else
	{
		/* build 79: "vramcold" fills VRAM smallest first, so the large
		textures (the level's, covering most of the screen) are drawn from
		RAM: the worst case of texturing from RAM, measured against the
		usual order to learn what VRAM is worth (a screen-size scratch for
		anti-aliasing would have to come out of it) */
		int cold = psp_keep("vramcold");

		if (cold)
			platform_log("textures: vramcold: VRAM filled smallest first; the large textures are drawn from RAM");
	for (index = 0; index < entry_count; index++)
	{
		/* by size, largest first (an insertion sort: a few hundred) */
		for (k = order_count; k > 0 && (cold ? entries[order[k - 1]].size > entries[index].size :
			entries[order[k - 1]].size < entries[index].size); k--)
			order[k] = order[k - 1];
		order[k] = (unsigned short)index;
		order_count++;
	}
	}
	/* build 39: the largest go to VRAM, but only once the GE draws: until
	then the start-up log on the screen is drawn into VRAM, over the cache
	(build 38's first frames: the level's textures were that log's text). So
	now the rest go to RAM, and the VRAM set is read at the first frame's end */
	{
		unsigned int vram = VRAM_CACHE_END - VRAM_CACHE_START, used = 0;

		vram_pending_count = 0;
		for (k = 0; k < order_count; k++)
		{
			unsigned int size = (entries[order[k]].size + 63) & ~63u;

			if (used + size <= vram && vram_pending_count < 512)
			{
				used += size;
				vram_pending[vram_pending_count++] = order[k];
				continue;
			}
			if (load_into(order[k], 1))
			{
				/* counted as drawn now, so the next one's room never evicts it */
				last_frame[order[k]] = frame;
				loaded++;
				preloaded++;
			}
		}
	}
	frame_loaded = 0;
	platform_log("textures: %d of %d read in at the start (%u KB) in %u ms; %d more into VRAM at the first frame (chosen %s)",
		loaded, entry_count, total / 1024, (sceKernelGetSystemTimeLow() - start) / 1000, vram_pending_count,
		ranked ? "by learned binds per byte" : "largest first");
	if (screen_free && vram_pending_count)
		preload_vram();
}

/* the VRAM set, once the GE has the screen (psp_tex_frame_end) */
static void preload_vram(void)
{
	unsigned int start = sceKernelGetSystemTimeLow();
	int k, loaded = 0;

	for (k = 0; k < vram_pending_count; k++)
		if (!resident_at[vram_pending[k]] && load(vram_pending[k]))
		{
			last_frame[vram_pending[k]] = frame;
			loaded++;
			preloaded++;
		}
	platform_log("textures: %d of %d read into VRAM at the first frame in %u ms", loaded, vram_pending_count,
		(sceKernelGetSystemTimeLow() - start) / 1000);
	vram_pending_count = 0;
	frame_loaded = 0;
}

/* build 46: the learned bind counts (see bind_totals). The file: "VRM1",
the entry count, the sum of the entries' keys (so a re-baked pack starts
over), then a count per entry */
static unsigned int key_sum(void)
{
	unsigned int sum = 0;
	int index;

	for (index = 0; index < entry_count; index++)
		sum += entries[index].key;
	return sum;
}

static int bind_totals_load(void)
{
	char path[128];
	unsigned int header[3];
	SceUID file;
	int ok = 0, index;

	snprintf(path, sizeof(path), "ms0:/halo/maps/%s.vram", map_name);
	file = sceIoOpen(path, PSP_O_RDONLY, 0);
	if (file < 0)
	{
		/* build 89: a save whose rename didn't happen (vram_write) */
		snprintf(path, sizeof(path), "ms0:/halo/maps/%s.vram.tmp", map_name);
		file = sceIoOpen(path, PSP_O_RDONLY, 0);
	}
	if (file < 0)
		return 0;
	if (sceIoRead(file, header, sizeof(header)) == sizeof(header) && header[0] == 0x314D5256 /* VRM1 */ &&
		header[1] == (unsigned int)entry_count && header[2] == key_sum() &&
		sceIoRead(file, bind_totals, entry_count * sizeof(unsigned int)) == (int)(entry_count * sizeof(unsigned int)))
		ok = 1;
	sceIoClose(file);
	if (!ok)
	{
		memset(bind_totals, 0, entry_count * sizeof(unsigned int));
		platform_log("textures: %s is from another pack: learning again", path);
		return 0;
	}
	for (index = 0; index < entry_count; index++)
		bind_totals[index] /= 2;
	return 1;
}

/* this census period's binds into the totals (the census does the same) */
static void bind_totals_gather(void)
{
	int index;

	if (!bind_totals || !bind_counts)
		return;
	for (index = 0; index < entry_count; index++)
	{
		bind_totals[index] += bind_counts[index];
		bind_counts[index] = 0;
	}
}

/* build 89: <map>.vram written whole or not at all: to <map>.vram.tmp,
every write checked, then renamed over the old file (the loader takes the
.tmp if the rename didn't happen). b46-b88 truncated the file first and
reported success without checking the writes, so a failed or interrupted
save wiped what had been learned (review). The bytes written, or < 0 */
static int vram_write(const char *map, const unsigned int *totals, unsigned int entries, unsigned int sum)
{
	char path[128], temp[136];
	unsigned int header[3];
	SceUID file;
	int ok = 0;

	snprintf(path, sizeof(path), "ms0:/halo/maps/%s.vram", map);
	snprintf(temp, sizeof(temp), "%s.tmp", path);
	file = sceIoOpen(temp, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
	if (file < 0)
		return file;
	header[0] = 0x314D5256;
	header[1] = entries;
	header[2] = sum;
	if (sceIoWrite(file, header, sizeof(header)) == (int)sizeof(header) &&
		sceIoWrite(file, totals, entries * sizeof(unsigned int)) == (int)(entries * sizeof(unsigned int)))
		ok = 1;
	sceIoClose(file);
	if (!ok)
	{
		sceIoRemove(temp);
		return -1;
	}
	sceIoRemove(path);
	if (sceIoRename(temp, path) < 0)
		return -2;
	return (int)(sizeof(header) + entries * sizeof(unsigned int));
}

/* build 88: the learned binds saved DURING play. The save at the map's
close and at HOME (b47) never reached the Memory Stick on hardware: every
log from b70 to b87 says "not yet saved", so the VRAM set has been largest
first since the b68 packs replaced b46's file, and b87's bench found 90% of
the texture binds served from RAM (b46, ranked: 88% from VRAM). Here the
game thread copies the totals at a census (cheap: a few KB) once the map
has been played for 1,800 frames and then every 9,000, and the heartbeat
thread writes the copy (psp_tex_save_pending): the Memory Stick's 40-56 ms
(b46) land on that thread, not on a frame */
static unsigned int *snapshot = NULL, snapshot_capacity = 0, snapshot_entries = 0, snapshot_sum = 0;
static char snapshot_name[64];
static unsigned int map_open_frame = 0, last_snapshot_frame = 0, snapshots_taken = 0;

static void learned_snapshot(void)
{
	if (!bind_totals || !map_name[0] || snapshot_pending || entry_count <= 0)
		return;
	if (frame - map_open_frame < 1800)
		return;
	if (snapshots_taken && frame - last_snapshot_frame < 9000)
		return;
	if (snapshot_capacity < (unsigned int)entry_count)
	{
		free(snapshot);
		snapshot = malloc(entry_count * sizeof(unsigned int));
		snapshot_capacity = snapshot ? (unsigned int)entry_count : 0;
		if (!snapshot)
			return;
	}
	memcpy(snapshot, bind_totals, entry_count * sizeof(unsigned int));
	snapshot_entries = (unsigned int)entry_count;
	snapshot_sum = key_sum();
	snprintf(snapshot_name, sizeof(snapshot_name), "%s", map_name);
	last_snapshot_frame = frame;
	snapshots_taken++;
	snapshot_pending = 1;
}

/* the heartbeat thread (psp_platform.c), every heartbeat */
void psp_tex_save_pending(void)
{
	unsigned int start;
	int written;

	if (!snapshot_pending)
		return;
	start = sceKernelGetSystemTimeLow();
	snapshot_writing = 1;
	written = vram_write(snapshot_name, snapshot, snapshot_entries, snapshot_sum);
	snapshot_writing = 0;
	if (written > 0)
		platform_log("textures: learned binds saved during play to %s.vram (%d bytes) in %u ms; the next launch fills VRAM by them",
			snapshot_name, written, (sceKernelGetSystemTimeLow() - start) / 1000);
	else
		platform_log("textures: learned binds NOT saved to %s.vram (%08x: %s)", snapshot_name, (unsigned int)written,
			written == -1 ? "a write failed" : written == -2 ? "the rename failed; the .tmp will be read" : "couldn't open the file");
	snapshot_pending = 0;
}

/* the game is quitting (HOME; psp_platform.c's exit callback) */
void psp_tex_save_learned(void)
{
	bind_totals_gather();
	bind_totals_save();
}

static void bind_totals_save(void)
{
	unsigned int start = sceKernelGetSystemTimeLow();
	int written;

	if (!bind_totals || !map_name[0] || entry_count <= 0)
		return;
	written = vram_write(map_name, bind_totals, (unsigned int)entry_count, key_sum());
	platform_log("textures: learned binds written to %s.vram (%d%s) in %u ms", map_name, written,
		written > 0 ? " bytes" : ": FAILED", (sceKernelGetSystemTimeLow() - start) / 1000);
}

/* a map's tags were loaded: its texture pack, if there is one */
void psp_tex_map_opened(const char *name)
{
	char path[128];
	unsigned int header[3];
	SceUID file;

	if (!name || !name[0])
		return;
	psp_tex_close();
	snprintf(path, sizeof(path), "ms0:/halo/maps/%s.ptex", name);
	file = sceIoOpen(path, PSP_O_RDONLY, 0);
	if (file < 0)
	{
		platform_log("textures: no %s (bake it with tools/texpack.py); surfaces stay flat", path);
		return;
	}
	if (sceIoRead(file, header, sizeof(header)) != sizeof(header) || header[0] != 0x31585450 /* PTX1 */ ||
		header[2] != header[1] * sizeof(struct pack_entry))
	{
		platform_log("textures: %s is not a texture pack", path);
		sceIoClose(file);
		return;
	}
	entries = malloc(header[2]);
	resident_at = calloc(header[1], sizeof(unsigned int));
	last_frame = calloc(header[1], sizeof(unsigned int));
	resident_in = calloc(header[1], 1);
	bind_counts = calloc(header[1], sizeof(unsigned short));
	if (!ram_cache)
		ram_cache_acquire();
	if (!entries || !resident_at || !last_frame || !resident_in || sceIoRead(file, entries, header[2]) != (int)header[2])
	{
		platform_log("textures: can't read %s's directory", path);
		sceIoClose(file);
		psp_tex_close();
		return;
	}
	pack = file;
	entry_count = header[1];
	platform_log("textures: %s, %d textures; caches %d KB of VRAM and %d KB of RAM", path, entry_count,
		(VRAM_CACHE_END - VRAM_CACHE_START) / 1024, (int)(arenas[1].end - arenas[1].start) / 1024);
	{
		int learned = 0, ranked;

		snprintf(map_name, sizeof(map_name), "%s", name);
		map_open_frame = frame;    /* build 88: the in-play save counts from here */
		snapshots_taken = 0;
		bind_totals = calloc(entry_count, sizeof(unsigned int));
		if (bind_totals)
			learned = bind_totals_load();
		/* build 47: by learned binds whenever they exist (b46's A/B: 13% ->
		88% of binds from VRAM, nothing visible changes); "vramlargest" in
		keep.txt goes back to largest first */
		ranked = learned && !psp_keep("vramlargest");
		platform_log("textures: learned binds %s; VRAM set %s", learned ? "read" : "not yet saved (learning this run; saved when the map closes or the game quits)",
			ranked ? "by binds per byte" : learned ? "largest first (vramlargest)" : "largest first until learned");
		preload(ranked);
	}
}

static int find(unsigned int key)
{
	int low = 0, high = entry_count - 1;

	while (low <= high)
	{
		int middle = (low + high) / 2;

		if (entries[middle].key == key)
			return middle;
		if (entries[middle].key < key)
			low = middle + 1;
		else
			high = middle - 1;
	}
	return -1;
}

/* room for size bytes in an arena without evicting any texture drawn in the
last `recent` frames (1: this frame only) */
static int protected_at(int index, int which, unsigned int recent)
{
	return resident_at[index] && resident_in[index] == which && last_frame[index] + recent > frame;
}

static unsigned int fits(int which, unsigned int start, unsigned int size, unsigned int recent)
{
	unsigned int end = start + size;
	int index;

	if (end > arenas[which].end)
		return 0;
	for (index = 0; index < entry_count; index++)
	{
		unsigned int at = resident_at[index];

		if (protected_at(index, which, recent) && at < end && at + alignment(entries[index].size) > start)
			return 0;
	}
	return 1;
}

/* at the ring position, the arena's start, or just after a protected
texture; whatever unprotected is in the chosen space is evicted */
static unsigned int allocate(int which, unsigned int size, unsigned int recent)
{
	struct arena *arena = &arenas[which];
	unsigned int start = 0, candidate;
	int index;

	if (!arena->start || size > arena->end - arena->start)
		return 0;
	if (fits(which, arena->next, size, recent))
		start = arena->next;
	else if (fits(which, arena->start, size, recent))
		start = arena->start;
	else
	{
		for (index = 0; index < entry_count && !start; index++)
		{
			if (!protected_at(index, which, recent))
				continue;
			candidate = resident_at[index] + alignment(entries[index].size);
			if (fits(which, candidate, size, recent))
				start = candidate;
		}
	}
	if (!start)
		return 0;
	for (index = 0; index < entry_count; index++)
	{
		unsigned int at = resident_at[index];

		if (at && resident_in[index] == which && at < start + size && at + alignment(entries[index].size) > start)
		{
			resident_at[index] = 0;
			evictions++;
		}
	}
	arena->next = start + size;
	return start;
}

static unsigned long read_us = 0, frame_read_us = 0, frame_read_worst = 0;
/* build 73: this heartbeat window's reads, for the log's io: line */
static unsigned long window_reads = 0, window_us = 0, window_worst_us = 0;
void psp_tex_io_window(unsigned long *reads, unsigned long *us, unsigned long *worst_us)
{
	*reads = window_reads;
	*us = window_us;
	*worst_us = window_worst_us;
	window_reads = window_us = window_worst_us = 0;
}
static int dma_failures = 0;

/* only: -1 either cache (VRAM first), 0 VRAM only, 1 RAM only */
static int load_into(int index, int only);

static int load(int index)
{
	return load_into(index, -1);
}

static int load_into(int index, int only)
{
	struct pack_entry const *entry = &entries[index];
	unsigned int at = 0, done = 0, read_start;
	int which = 0;

	/* build 33: a texture drawn in the last two seconds isn't evicted while
	either cache (VRAM, then the RAM partition) has room elsewhere. Before,
	VRAM was a ring that evicted last frame's textures while the 4 MB in RAM
	sat mostly empty, and on the PSP surfaces vanished and came back as their
	textures were read from the Memory Stick again */
	if (only != 1)
		at = allocate(which = 0, alignment(entry->size), RECENT_FRAMES);
	if (!at && only != 0)
		at = allocate(which = 1, alignment(entry->size), RECENT_FRAMES);
	if (!at && only != 1)
		at = allocate(which = 0, alignment(entry->size), 1);
	if (!at && only != 0)
		at = allocate(which = 1, alignment(entry->size), 1);
	if (!at)
	{
		waits++;
		return 0;
	}
	read_start = sceKernelGetSystemTimeLow();
	if (sceIoLseek32(pack, entry->offset, PSP_SEEK_SET) != (int)entry->offset)
		return 0;
	while (done < entry->size)
	{
		unsigned int part = entry->size - done > STAGING_BYTES ? STAGING_BYTES : entry->size - done;

		if (sceIoRead(pack, staging, part) != (int)part)
		{
			platform_log("textures: reading texture %d failed", index);
			return 0;
		}
		/* build 46: by DMA (2.6 to 3 times memcpy's speed into VRAM:
		halo_bench v9), the staging buffer written back from the CPU's cache
		first; the cache never holds the caches' lines (written only through
		the uncached view, and written back and invalidated when acquired), so
		nothing stale is written over the copy later. memcpy through the
		uncached view if the DMA call fails */
		sceKernelDcacheWritebackRange(staging, part);
		if (dma_failures || sceDmacMemcpy((void *)(at + done), staging, part) < 0)
		{
			if (!dma_failures++)
				platform_log("textures: sceDmacMemcpy failed; copying with the CPU");
			memcpy((void *)((at + done) | arenas[which].write_offset), staging, part);
		}
		done += part;
	}
		/* build 35: the Memory Stick's time, measured (about 1.2 MB/s expected) */
	read_us += sceKernelGetSystemTimeLow() - read_start;
	frame_read_us += sceKernelGetSystemTimeLow() - read_start;
	{
		unsigned long took = sceKernelGetSystemTimeLow() - read_start;

		window_reads++;
		window_us += took;
		if (took > window_worst_us)
			window_worst_us = took;
	}
	resident_at[index] = at;
	resident_in[index] = (unsigned char)which;
	loads++;
	load_bytes += entry->size;
	frame_loaded += entry->size;
	return 1;
}

/* the bitmap_data of a bitmap tag's bitmap: the key a texture is found by */
unsigned int psp_tex_bitmap_key(long bitmap_tag_index, int bitmap)
{
	char *group;
	long count;
	char *address;

	if (bitmap_tag_index == -1 || !entries)
		return 0;
	group = tag_get(0x6269746D /* bitm */, bitmap_tag_index);
	if (!group)
		return 0;
	count = *(long *)(group + 96);
	address = *(char **)(group + 100);
	if (bitmap < 0 || bitmap >= count || !address)
		return 0;
	return (unsigned int)(address + 48 * bitmap);
}

/* how many bitmaps a bitmap tag holds (a numeric shader's digits; build 25) */
int psp_tex_bitmap_count(long bitmap_tag_index)
{
	char *group;

	if (bitmap_tag_index == -1)
		return 0;
	group = tag_get(0x6269746D /* bitm */, bitmap_tag_index);
	return group ? (int)*(long *)(group + 96) : 0;
}

/* makes the texture the GE's current one; 0 if it isn't there (yet) */
int psp_tex_bind(unsigned int key, int *has_alpha)
{
	int index, level;
	struct pack_entry const *entry;
	unsigned int palette_bytes, address, width, height;

	if (!key || !entries)
		return 0;
	index = find(key);
	if (index < 0)
		return 0;
	entry = &entries[index];
	if (!resident_at[index])
	{
		misses++;
		if (frame_loaded >= LOAD_BUDGET_BYTES || !load(index))
		{
			frame_untextured = 1;
			return 0;
		}
	}
	if (last_frame[index] != frame)
	{
		frame_set_bytes += alignment(entry->size);
		frame_set_count++;
	}
	last_frame[index] = frame;
	if (bind_counts && bind_counts[index] < 65535)
		bind_counts[index]++;
	/* 8-bit (256 colours) or 4-bit (16) indices into a 16- or 32-bit palette */
	palette_bytes = (entry->clut == GU_PSM_8888 ? 4 : 2) * (entry->psm == GU_PSM_T4 ? 16 : 256);
	address = resident_at[index];
	sceGuClutMode(entry->clut, 0, 0xFF, 0);
	sceGuClutLoad(palette_bytes / 32, (void *)address);
	sceGuTexMode(entry->psm, entry->levels - 1, 0, 1);
	address += palette_bytes;
	width = entry->width;
	height = entry->height;
	for (level = 0; level < entry->levels; level++)
	{
		unsigned int bytes = entry->psm == GU_PSM_T4 ? (width + 1) / 2 : width;
		unsigned int row = ((bytes < 16 ? 16 : bytes) + 15) & ~15u, rows = (height + 7) & ~7u;

		/* the buffer width is in pixels */
		sceGuTexImage(level, width, height, entry->psm == GU_PSM_T4 ? row * 2 : row, (void *)address);
		address += row * rows;
		width = width > 1 ? width / 2 : 1;
		height = height > 1 ? height / 2 : 1;
	}
	sceGuTexFilter(entry->levels > 1 ? GU_LINEAR_MIPMAP_LINEAR : GU_LINEAR, GU_LINEAR);
	if (has_alpha)
		*has_alpha = entry->flags & 1;
	return 1;
}

/* the size a texture was baked at (texture coordinates in the GE's 2D mode
are in texels); 0 if it isn't in the pack */
/* a resident texture's 32-bit palette, read through the uncached view (for
the HUD's meters, whose look is worked out per palette entry; psp_ge.c) */
int psp_tex_palette(unsigned int key, const unsigned int **palette, int *count)
{
	int index = key && entries ? find(key) : -1;

	if (index < 0 || !resident_at[index] || entries[index].clut != GU_PSM_8888)
		return 0;
	*palette = (const unsigned int *)(resident_at[index] | 0x40000000);
	*count = entries[index].psm == GU_PSM_T4 ? 16 : 256;
	return 1;
}

/* a resident texture's 16-bit (5650) palette, the same way (the change
colour's mask, in a multipurpose map's blue; build 31) */
int psp_tex_palette16(unsigned int key, const unsigned short **palette, int *count)
{
	int index = key && entries ? find(key) : -1;

	if (index < 0 || !resident_at[index] || entries[index].clut != GU_PSM_5650)
		return 0;
	*palette = (const unsigned short *)(resident_at[index] | 0x40000000);
	*count = entries[index].psm == GU_PSM_T4 ? 16 : 256;
	return 1;
}

/* a resident texture's average colour (ABGR), from a sample of its top
level's texels through the palette; kept per texture (build 36: the
flashlight's added light, which can't take a second texture) */
int psp_tex_average(unsigned int key, unsigned int *abgr)
{
	int index = key && entries ? find(key) : -1;
	struct pack_entry const *entry;
	unsigned int palette_bytes, bytes, row, rows, total, step, at, sum[3] = { 0, 0, 0 }, n = 0;
	const unsigned char *texels;

	if (index < 0 || !resident_at[index])
		return 0;
	if (averages_count != entry_count)
	{
		free(averages);
		averages = calloc(entry_count, sizeof(unsigned int));
		averages_count = averages ? entry_count : 0;
		if (!averages)
			return 0;
	}
	if (averages[index])
	{
		*abgr = averages[index];
		return 1;
	}
	entry = &entries[index];
	palette_bytes = (entry->clut == GU_PSM_8888 ? 4 : 2) * (entry->psm == GU_PSM_T4 ? 16 : 256);
	bytes = entry->psm == GU_PSM_T4 ? (entry->width + 1) / 2 : entry->width;
	row = ((bytes < 16 ? 16 : bytes) + 15) & ~15u;
	rows = (entry->height + 7) & ~7u;
	total = row * rows;
	step = total / 2048 + 1;
	texels = (const unsigned char *)((resident_at[index] + palette_bytes) | 0x40000000);
	for (at = 0; at < total; at += step)
	{
		unsigned int value = texels[at], k;

		for (k = 0; k < (entry->psm == GU_PSM_T4 ? 2u : 1u); k++)
		{
			unsigned int i = entry->psm == GU_PSM_T4 ? (value >> (4 * k)) & 15 : value, r, g, b;

			if (entry->clut == GU_PSM_8888)
			{
				unsigned int c = ((const unsigned int *)(resident_at[index] | 0x40000000))[i];

				r = c & 0xFF; g = (c >> 8) & 0xFF; b = (c >> 16) & 0xFF;
			}
			else
			{
				unsigned int c = ((const unsigned short *)(resident_at[index] | 0x40000000))[i];

				r = (c & 31) * 255 / 31; g = ((c >> 5) & 63) * 255 / 63; b = (c >> 11) * 255 / 31;
			}
			sum[0] += r; sum[1] += g; sum[2] += b;
			n++;
		}
	}
	if (!n)
		return 0;
	averages[index] = 0xFF000000 | ((sum[2] / n) << 16) | ((sum[1] / n) << 8) | (sum[0] / n);
	*abgr = averages[index];
	return 1;
}

/* whether the pack has a texture under this key (build 36: a model's base map
with its detail map folded in is under the base's key | 1) */
int psp_tex_has(unsigned int key)
{
	return key && entries && find(key) >= 0;
}

int psp_tex_size(unsigned int key, int *width, int *height)
{
	int index = key && entries ? find(key) : -1;

	if (index < 0)
		return 0;
	*width = entries[index].width;
	*height = entries[index].height;
	return 1;
}

/* the end of a frame: a new load budget, and a report every 300 frames */
/* psp_ge.c, once the GE has the screen and the on-screen log has stopped:
the VRAM set goes in before the first frame's draws (at the first frame's
end, those draws had already read some of it into VRAM on their own, in no
order, and two of the set no longer fit) */
void psp_tex_screen_free(void)
{
	screen_free = 1;
	if (vram_pending_count && entries)
		preload_vram();
}

void psp_tex_frame_end(void)
{
	if (vram_pending_count && entries)
		preload_vram();
	if (frame_set_bytes > frame_set_peak)
	{
		frame_set_peak = frame_set_bytes;
		frame_set_peak_count = frame_set_count;
	}
	frame_set_bytes = frame_set_count = 0;
	if (frame_untextured)
	{
		untextured_frames++;
		if (++untextured_run > untextured_run_worst)
			untextured_run_worst = untextured_run;
	}
	else
		untextured_run = 0;
	frame_untextured = 0;
	if (entries && frame % 300 == 0)
		platform_log("textures: %lu loaded (%lu KB), %lu requests waited for the next frame, %lu for room; %lu evicted; largest frame %lu textures, %lu KB; %lu frames drew something untextured (longest run %lu)",
			loads, load_bytes / 1024, misses - (loads - preloaded), waits, evictions, frame_set_peak_count, frame_set_peak / 1024,
			untextured_frames, untextured_run_worst);
	if (frame_read_us > frame_read_worst)
		frame_read_worst = frame_read_us;
	frame_read_us = 0;
	if (entries && frame % 300 == 0 && read_us)
		platform_log("textures: reading took %lu ms in all (%lu KB/s); the worst frame spent %lu ms reading",
			read_us / 1000, (unsigned long)((unsigned long long)load_bytes * 1000000ULL / 1024 / read_us), frame_read_worst / 1000);
	if (entries && bind_counts && frame % 900 == 450)
	{
		/* the twelve most bound textures and where they live (build 44) */
		char line[640];
		int length = 0, k, index, vram_binds = 0, ram_binds = 0, vram_bytes = 0, ram_bytes = 0;

		/* build 67: the VRAM set in thirds by binds per byte over the period,
		so a run says what the least useful third of VRAM carries: the third
		the display list (384 KB) or hot CPU data would displace */
		{
			int order_count = 0, k2, third = 0;
			unsigned int third_bytes[3] = { 0, 0, 0 }, third_binds[3] = { 0, 0, 0 }, cumulative = 0, vram_total = 0;

			for (index = 0; index < entry_count; index++)
			{
				float density;

				if (!resident_at[index] || resident_in[index])
					continue;
				vram_total += alignment(entries[index].size);
				density = (float)bind_counts[index] / (float)alignment(entries[index].size);
				for (k2 = order_count; k2 > 0; k2--)
				{
					float other = (float)bind_counts[order[k2 - 1]] / (float)alignment(entries[order[k2 - 1]].size);

					if (other >= density)
						break;
					order[k2] = order[k2 - 1];
				}
				order[k2] = (unsigned short)index;
				order_count++;
			}
			for (k2 = 0; k2 < order_count; k2++)
			{
				unsigned int size = alignment(entries[order[k2]].size);

				while (third < 2 && cumulative >= vram_total * (third + 1) / 3)
					third++;
				third_bytes[third] += size;
				third_binds[third] += bind_counts[order[k2]];
				cumulative += size;
			}
			platform_log("textures: VRAM by binds per byte, best third %u binds in %u KB, middle %u in %u KB, worst %u in %u KB (%d textures resident)",
				third_binds[0], third_bytes[0] / 1024, third_binds[1], third_bytes[1] / 1024, third_binds[2], third_bytes[2] / 1024,
				order_count);
		}
		for (k = 0; k < 12; k++)
		{
			int best = -1;

			for (index = 0; index < entry_count; index++)
				if (bind_counts[index] && (best < 0 || bind_counts[index] > bind_counts[best]))
					best = index;
			if (best < 0 || length > (int)sizeof(line) - 64)
				break;
			length += snprintf(line + length, sizeof(line) - length, " %08x %ux%u %s %u;", entries[best].key, entries[best].width,
				entries[best].height, resident_at[best] ? (resident_in[best] ? "RAM" : "VRAM") : "out", bind_counts[best]);
			if (bind_totals)
				bind_totals[best] += bind_counts[best];
			bind_counts[best] = 0;
		}
		for (index = 0; index < entry_count; index++)
		{
			if (resident_at[index] && !resident_in[index])
			{
				vram_binds += bind_counts[index];
				vram_bytes += entries[index].size;
			}
			else if (resident_at[index])
			{
				ram_binds += bind_counts[index];
				ram_bytes += entries[index].size;
			}
			if (bind_totals)
				bind_totals[index] += bind_counts[index];
			bind_counts[index] = 0;
		}
		platform_log("textures: most bound over 900 frames (key size place binds):%s; the rest: VRAM %d binds (%d KB resident), RAM %d binds (%d KB)",
			line, vram_binds, vram_bytes / 1024, ram_binds, ram_bytes / 1024);
		learned_snapshot();
	}
	frame++;
	frame_loaded = 0;
}
