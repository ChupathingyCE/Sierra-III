/*
CAP1: a capture of one box's inputs and the old code's outputs over sampled
ticks of a pad bench (bench/halo_box/README.md). The port's capture switch writes
it; bench/halo_box reads it. Little-endian; every record starts on a 4-byte
boundary; a tick section starts on a 64-byte boundary of the file and is
complete in itself, so a reader holds the static section and one tick.

    header (64 bytes)
    static section (header.static_bytes): records of
        { key, length, bytes padded to 4 }
    tick sections, header.ticks of them, each:
        { 'TICK', tick, entities, bytes }   then, in `bytes` bytes,
        entities records of
        { id, kind (16 bits), flags (16), input_length, output_length,
          input bytes padded to 4, output bytes padded to 4 }
        then zero padding to the next 64-byte boundary of the file

What a key, an id, a kind and the bytes mean is the box's contract (its page,
docs/box_<name>.md); only the box's adapter reads them. A capture holds game
state: it is game data, kept where the lab keeps game data, never here.
*/
#ifndef PSP_CAP1_H
#define PSP_CAP1_H

#define CAP1_MAGIC 0x31504143u /* "CAP1" */
#define CAP1_TICK 0x4b434954u  /* "TICK" */
#define CAP1_VERSION 1u

struct cap1_header
{
	unsigned int magic, version;
	char box[8];    /* the box's name, zero padded */
	char map[8];    /* "b30" */
	char bench[20]; /* the pad bench or route the ticks come from */
	unsigned int commit;       /* the build's commit, its first eight hex digits */
	unsigned int tick_first;   /* the first sampled tick's game time */
	unsigned int ticks;        /* tick sections in this file */
	unsigned int entities;     /* entity records in this file */
	unsigned int static_bytes; /* the static section's size, a multiple of 4 */
};

struct cap1_static { unsigned int key, length; /* then length bytes, padded to 4 */ };
struct cap1_tick { unsigned int magic, tick, entities, bytes; };
struct cap1_entity
{
	unsigned int id;
	unsigned short kind, flags;
	unsigned int input_length, output_length; /* then both, each padded to 4 */
};

typedef char cap1_header_is_64_bytes[sizeof(struct cap1_header) == 64 ? 1 : -1];

#endif
