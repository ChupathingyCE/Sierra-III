/*
The capture's writer (psp_capture.h says how it is called; psp_cap1.h is the
file's format).

A CAP1 file puts its static records before its ticks, but a static is only
met when a tick first refers to it. So while a file is open its ticks go
straight to a scratch file on the stick and its statics collect in memory;
closing the file writes the header, the statics, then the scratch file's
ticks after them. Tick sections start on 64-byte boundaries of the scratch
file and the statics are padded to 64 in the final one, so the boundaries hold.

A file is closed and the next begun at the start of a sampled tick when
another tick as large as the largest so far might pass the file's size cap:
never inside a tick, so a tick's statics are always in its own file. Each
file is complete in itself (its own statics), as the harness reads them.

An entity's record is built in place in the tick's buffer. When an entity
begins inside another (an attached object posed inside its parent's call),
the outer one's unfinished record is lifted aside, the inner one is written
where it stood, and the outer is put back after it: records stay whole and
in the order they finished.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp_cap1.h"
#include "psp_capture.h"

#ifdef PSP_CAPTURE_HOST
/* a desktop build, for the writer's own test (bench/halo_box/host) */
#include <stdarg.h>
#include <stdio.h>
typedef FILE *cap_file;
#define CAP_NO_FILE 0
#define CAP_FOLDER "."
static cap_file cap_open(const char *path, int write) { return fopen(path, write ? "wb" : "rb"); }
static int cap_write(cap_file f, const void *bytes, unsigned int length) { return fwrite(bytes, 1, length, f) == length; }
static int cap_read(cap_file f, void *bytes, unsigned int length) { return (int)fread(bytes, 1, length, f); }
static void cap_close(cap_file f) { fclose(f); }
static void cap_remove(const char *path) { remove(path); }
static void cap_folder(void) {}
static int cap_on_me(void) { return 0; }
static void cap_log(const char *format, ...)
{
	va_list list;

	va_start(list, format);
	vprintf(format, list);
	va_end(list);
	putchar('\n');
}
#else
#include <pspiofilemgr.h>
#include "psp_me.h"
void platform_log(const char *format, ...);
#define cap_log platform_log
typedef SceUID cap_file;
#define CAP_NO_FILE (-1)
#define CAP_FOLDER "ms0:/halo/capture"
static cap_file cap_open(const char *path, int write)
{
	SceUID f = sceIoOpen(path, write ? PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC : PSP_O_RDONLY, 0777);

	return f < 0 ? CAP_NO_FILE : f;
}
static int cap_write(cap_file f, const void *bytes, unsigned int length) { return sceIoWrite(f, bytes, length) == (int)length; }
static int cap_read(cap_file f, void *bytes, unsigned int length) { return sceIoRead(f, bytes, length); }
static void cap_close(cap_file f) { sceIoClose(f); }
static void cap_remove(const char *path) { sceIoRemove(path); }
static void cap_folder(void)
{
	sceIoMkdir("ms0:/halo", 0777);
	sceIoMkdir(CAP_FOLDER, 0777);
}
static int cap_on_me(void) { return PSP_ME_WORK(); }
#endif

#define NEST_MOST 8
#define ASIDE_BYTES (64u * 1024u)

static struct
{
	int open;
	struct psp_capture_plan plan;
	char box[9];
	/* the file being made */
	unsigned int file_number;
	cap_file scratch;
	char scratch_path[80];
	unsigned int scratch_bytes, file_ticks, file_entities, file_first_tick;
	unsigned char *statics;
	unsigned int static_bytes, static_room;
	unsigned int *keys;
	unsigned int key_count, key_room;
	/* the tick being made */
	unsigned char *tick;
	unsigned int tick_room, tick_used, tick_number, tick_entities, largest_tick;
	int in_tick, over;
	/* the entities open, the innermost last: where each one's record starts,
	and its lengths so far; `aside` holds the outer ones' unfinished records */
	struct { unsigned int at, id, kind, flags, input, output; int output_begun; unsigned int lifted; } nest[NEST_MOST];
	int depth;
	unsigned char *aside;
	unsigned int aside_used;
	/* the run */
	unsigned int ticks, entities, files, dropped_ticks, unmatched, bytes;
} cap;

static const unsigned char zeros[64];

static void *room_for(void *old, unsigned int *room, unsigned int need)
{
	void *grown;

	if (need <= *room)
		return old;
	grown = realloc(old, need + need / 2 + 4096);
	if (grown)
		*room = need + need / 2 + 4096;
	return grown;
}

/* bytes onto the tick's buffer; a tick that outgrows its room is dropped whole at its end */
static void put(const void *bytes, unsigned int length)
{
	if (cap.over || length > cap.tick_room - cap.tick_used)
	{
		cap.over = 1;
		return;
	}
	if (bytes)
		memcpy(cap.tick + cap.tick_used, bytes, length);
	else
		memset(cap.tick + cap.tick_used, 0, length);
	cap.tick_used += length;
}

static void pad4(void)
{
	if (cap.tick_used & 3u)
		put(0, 4u - (cap.tick_used & 3u));
}

/* a header's name: zero padded, not ended by a zero when it fills its field */
static void name(char *field, unsigned int size, const char *text)
{
	unsigned int length = text ? (unsigned int)strlen(text) : 0;

	memcpy(field, text, length < size ? length : size);
}

static int scratch_begin(void)
{
	snprintf(cap.scratch_path, sizeof(cap.scratch_path), CAP_FOLDER "/%s.tmp", cap.box);
	cap.scratch = cap_open(cap.scratch_path, 1);
	cap.scratch_bytes = cap.file_ticks = cap.file_entities = 0;
	cap.static_bytes = cap.key_count = 0;
	return cap.scratch != CAP_NO_FILE;
}

/* the file being made, written out: header, statics, the scratch file's ticks */
static void file_finish(void)
{
	struct cap1_header header;
	char path[80];
	cap_file out, in;
	int got, good = 1;

	if (cap.scratch == CAP_NO_FILE)
		return;
	cap_close(cap.scratch);
	cap.scratch = CAP_NO_FILE;
	if (!cap.file_ticks)
	{
		cap_remove(cap.scratch_path);
		return;
	}
	snprintf(path, sizeof(path), CAP_FOLDER "/%s_%03u.cap", cap.box, cap.file_number);
	memset(&header, 0, sizeof(header));
	header.magic = CAP1_MAGIC;
	header.version = CAP1_VERSION;
	name(header.box, sizeof(header.box), cap.box);
	name(header.map, sizeof(header.map), cap.plan.map);
	name(header.bench, sizeof(header.bench), cap.plan.bench);
	header.commit = cap.plan.commit;
	header.tick_first = cap.file_first_tick;
	header.ticks = cap.file_ticks;
	header.entities = cap.file_entities;
	header.static_bytes = cap.static_bytes;
	out = cap_open(path, 1);
	in = cap_open(cap.scratch_path, 0);
	if (out == CAP_NO_FILE || in == CAP_NO_FILE)
		good = 0;
	else
	{
		good = cap_write(out, &header, sizeof(header)) && (!cap.static_bytes || cap_write(out, cap.statics, cap.static_bytes));
		if (good && (cap.static_bytes & 63u))
			good = cap_write(out, zeros, 64u - (cap.static_bytes & 63u));
		/* (between ticks, so the tick's buffer is free to copy through) */
		while (good && (got = cap_read(in, cap.tick, cap.tick_room)) > 0)
			good = cap_write(out, cap.tick, (unsigned int)got);
	}
	if (out != CAP_NO_FILE)
		cap_close(out);
	if (in != CAP_NO_FILE)
		cap_close(in);
	cap_remove(cap.scratch_path);
	if (good)
	{
		cap.files++;
		cap.bytes += 64u + ((cap.static_bytes + 63u) & ~63u) + cap.scratch_bytes;
		cap_log("capture %s: %s written: %u ticks, %u entities, %u bytes of statics in %u records, %u bytes", cap.box, path, cap.file_ticks,
			cap.file_entities, cap.static_bytes, cap.key_count, 64u + ((cap.static_bytes + 63u) & ~63u) + cap.scratch_bytes);
	}
	else
		cap_log("capture %s: %s could not be written", cap.box, path);
	cap.file_number++;
}

int psp_capture_open(const struct psp_capture_plan *plan)
{
	if (cap.open || !plan || !plan->box || !plan->box[0])
		return 0;
	memset(&cap, 0, sizeof(cap));
	cap.plan = *plan;
	name(cap.box, 8, plan->box);
	if (!cap.plan.every)
		cap.plan.every = 1;
	if (!cap.plan.run || cap.plan.run > cap.plan.every)
		cap.plan.run = cap.plan.run ? cap.plan.every : 1;
	if (!cap.plan.file_bytes_most)
		cap.plan.file_bytes_most = 32u * 1024u * 1024u;
	cap.tick_room = cap.plan.tick_bytes_most ? cap.plan.tick_bytes_most : 512u * 1024u;
	cap.tick = (unsigned char *)malloc(cap.tick_room);
	cap.aside = (unsigned char *)malloc(ASIDE_BYTES);
	cap.scratch = CAP_NO_FILE;
	cap_folder();
	if (!cap.tick || !cap.aside || !scratch_begin())
	{
		cap_log("capture %s: not started (%s)", cap.box, cap.tick && cap.aside ? "its scratch file can't be made" : "no memory for a tick's records");
		free(cap.tick);
		free(cap.aside);
		return 0;
	}
	cap.open = 1;
	cap_log("capture %s: on: from tick %u, %u in a row every %u, %s%u ticks; files of at most %u bytes, a tick of at most %u", cap.box,
		cap.plan.first, cap.plan.run, cap.plan.every, cap.plan.ticks_most ? "" : "no limit of ", cap.plan.ticks_most,
		cap.plan.file_bytes_most, cap.tick_room);
	return 1;
}

int psp_capture_tick(unsigned int tick)
{
	if (!cap.open || cap_on_me())
		return 0;
	if (cap.in_tick)
		psp_capture_tick_end();
	if (tick < cap.plan.first || (tick - cap.plan.first) % cap.plan.every >= cap.plan.run ||
		(cap.plan.ticks_most && cap.ticks >= cap.plan.ticks_most))
		return 0;
	/* a new file before this tick, if another tick like the largest might not fit the one being made */
	if (cap.file_ticks && 64u + ((cap.static_bytes + 63u) & ~63u) + cap.scratch_bytes + cap.largest_tick * 2u + 4096u > cap.plan.file_bytes_most)
	{
		file_finish();
		if (!scratch_begin())
		{
			cap_log("capture %s: stopped: the next scratch file can't be made", cap.box);
			cap.open = 0;
			return 0;
		}
	}
	cap.in_tick = 1;
	cap.over = 0;
	cap.tick_number = tick;
	cap.tick_used = sizeof(struct cap1_tick);
	cap.tick_entities = 0;
	cap.depth = 0;
	cap.aside_used = 0;
	return 1;
}

void psp_capture_static(unsigned int key, const void *bytes, unsigned int length)
{
	struct cap1_static record;
	unsigned int k, padded = (length + 3u) & ~3u;
	void *grown;

	if (!cap.open || !cap.in_tick || cap_on_me())
		return;
	for (k = 0; k < cap.key_count; k++)
		if (cap.keys[k] == key)
			return;
	grown = room_for(cap.statics, &cap.static_room, cap.static_bytes + sizeof(record) + padded);
	if (grown)
		cap.statics = (unsigned char *)grown;
	grown = room_for(cap.keys, &cap.key_room, (cap.key_count + 1) * (unsigned int)sizeof(*cap.keys));
	if (grown)
		cap.keys = (unsigned int *)grown;
	if (cap.static_bytes + sizeof(record) + padded > cap.static_room || (cap.key_count + 1) * sizeof(*cap.keys) > cap.key_room)
	{
		cap.over = 1; /* a tick whose static couldn't be kept is no use */
		return;
	}
	record.key = key;
	record.length = length;
	memcpy(cap.statics + cap.static_bytes, &record, sizeof(record));
	memcpy(cap.statics + cap.static_bytes + sizeof(record), bytes, length);
	memset(cap.statics + cap.static_bytes + sizeof(record) + length, 0, padded - length);
	cap.static_bytes += (unsigned int)sizeof(record) + padded;
	cap.keys[cap.key_count++] = key;
}

void psp_capture_begin(unsigned int id, unsigned int kind, unsigned int flags)
{
	if (!cap.open || !cap.in_tick || cap_on_me())
		return;
	if (cap.depth >= NEST_MOST)
	{
		cap.over = 1;
		cap.depth++; /* its end still pairs with it */
		return;
	}
	if (cap.depth > 0 && !cap.over)
	{
		/* the entity this one begins inside: its record so far is lifted aside */
		unsigned int outer = cap.nest[cap.depth - 1].at, length = cap.tick_used - outer;

		if (length > ASIDE_BYTES - cap.aside_used)
			cap.over = 1;
		else
		{
			memcpy(cap.aside + cap.aside_used, cap.tick + outer, length);
			cap.aside_used += length;
			cap.nest[cap.depth - 1].lifted = length;
			cap.tick_used = outer;
		}
	}
	cap.nest[cap.depth].at = cap.tick_used;
	cap.nest[cap.depth].id = id;
	cap.nest[cap.depth].kind = kind;
	cap.nest[cap.depth].flags = flags;
	cap.nest[cap.depth].input = cap.nest[cap.depth].output = 0;
	cap.nest[cap.depth].output_begun = 0;
	cap.nest[cap.depth].lifted = 0;
	cap.depth++;
	put(0, sizeof(struct cap1_entity));
}

void psp_capture_input(const void *bytes, unsigned int length)
{
	if (!cap.open || !cap.in_tick || cap.depth < 1 || cap.depth > NEST_MOST || cap_on_me())
		return;
	if (cap.nest[cap.depth - 1].output_begun)
	{
		cap.over = 1; /* an input after an output: the hooks are out of order */
		return;
	}
	put(bytes, length);
	cap.nest[cap.depth - 1].input += length;
}

void psp_capture_output(const void *bytes, unsigned int length)
{
	if (!cap.open || !cap.in_tick || cap.depth < 1 || cap.depth > NEST_MOST || cap_on_me())
		return;
	if (!cap.nest[cap.depth - 1].output_begun)
	{
		pad4();
		cap.nest[cap.depth - 1].output_begun = 1;
	}
	put(bytes, length);
	cap.nest[cap.depth - 1].output += length;
}

void psp_capture_end(void)
{
	struct cap1_entity record;

	if (!cap.open || !cap.in_tick || cap_on_me())
		return;
	if (cap.depth < 1)
	{
		cap.unmatched++;
		return;
	}
	cap.depth--;
	if (cap.depth >= NEST_MOST)
		return;
	pad4();
	if (!cap.over)
	{
		record.id = cap.nest[cap.depth].id;
		record.kind = (unsigned short)cap.nest[cap.depth].kind;
		record.flags = (unsigned short)cap.nest[cap.depth].flags;
		record.input_length = cap.nest[cap.depth].input;
		record.output_length = cap.nest[cap.depth].output;
		memcpy(cap.tick + cap.nest[cap.depth].at, &record, sizeof(record));
		cap.tick_entities++;
	}
	if (cap.depth > 0 && !cap.over)
	{
		/* the entity this one ran inside: its record goes back, after this one */
		unsigned int length = cap.nest[cap.depth - 1].lifted;

		cap.aside_used -= length;
		cap.nest[cap.depth - 1].at = cap.tick_used;
		put(cap.aside + cap.aside_used, length);
		cap.nest[cap.depth - 1].lifted = 0;
	}
}

void psp_capture_tick_end(void)
{
	struct cap1_tick head;

	if (!cap.open || !cap.in_tick || cap_on_me())
		return;
	cap.in_tick = 0;
	if (cap.depth)
	{
		cap.unmatched += (unsigned int)cap.depth;
		cap.over = 1;
	}
	if (cap.over || !cap.tick_entities)
	{
		cap.dropped_ticks += cap.over != 0;
		return;
	}
	head.magic = CAP1_TICK;
	head.tick = cap.tick_number;
	head.entities = cap.tick_entities;
	head.bytes = cap.tick_used - (unsigned int)sizeof(head);
	memcpy(cap.tick, &head, sizeof(head));
	if (cap.scratch_bytes & 63u)
	{
		unsigned int pad = 64u - (cap.scratch_bytes & 63u);

		if (cap_write(cap.scratch, zeros, pad))
			cap.scratch_bytes += pad;
	}
	if (!cap_write(cap.scratch, cap.tick, cap.tick_used))
	{
		cap_log("capture %s: stopped at tick %u: the stick refused a write", cap.box, cap.tick_number);
		file_finish();
		psp_capture_close();
		return;
	}
	if (!cap.file_ticks)
		cap.file_first_tick = cap.tick_number;
	cap.scratch_bytes += cap.tick_used;
	cap.file_ticks++;
	cap.file_entities += cap.tick_entities;
	cap.ticks++;
	cap.entities += cap.tick_entities;
	if (cap.tick_used > cap.largest_tick)
		cap.largest_tick = cap.tick_used;
}

void psp_capture_close(void)
{
	if (!cap.open || cap_on_me())
		return;
	if (cap.in_tick)
		psp_capture_tick_end();
	file_finish();
	cap_log("capture %s: closed: %u ticks, %u entities, %u bytes in %u files; the largest tick %u bytes; %u ticks dropped (over their room, or their hooks unpaired: %u)",
		cap.box, cap.ticks, cap.entities, cap.bytes, cap.files, cap.largest_tick, cap.dropped_ticks, cap.unmatched);
	free(cap.tick);
	free(cap.aside);
	free(cap.statics);
	free(cap.keys);
	memset(&cap, 0, sizeof(cap));
}
