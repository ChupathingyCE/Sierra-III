/*
The capture's writer: a box's inputs and the old code's outputs for sampled
ticks, written as CAP1 files (psp_cap1.h) for bench/halo_box to run the box
over. Built only into a capture build (PSP_CAPTURE); the hooks that call it
sit at the old code's entry and exit for the box.

The calls, in a tick:

    if (psp_capture_tick(game_tick)) {              once a tick, before its entities
        ...
        psp_capture_static(key, bytes, length);      wherever a hook meets a static: kept once a file
        psp_capture_begin(id, kind, flags);          the old code's entry for an entity
        psp_capture_input(bytes, length);            as many pieces as the contract lists, in its order
        ...the old code runs; entities may nest (an attached object posed inside its parent's call)...
        psp_capture_output(bytes, length);           the old code's exit: pieces, in order
        psp_capture_end();
        ...
    }
    psp_capture_tick_end();                          after the tick's last entity

psp_capture_close() at the run's end (exitat) finishes the file. Every call
is safe when no capture is open, in a tick that isn't sampled, and on the
Media Engine (nothing is written there: a hook reached on the ME is skipped).
*/
#ifndef PSP_CAPTURE_H
#define PSP_CAPTURE_H

struct psp_capture_plan
{
	const char *box;         /* the box's name, up to 8 characters: the file is ms0:/halo/capture/<box>_NNN.cap */
	const char *map, *bench; /* for the header: "b30", the pad bench or route */
	unsigned int commit;     /* the build's commit, its first eight hex digits */
	/* sampling: from tick `first`, `run` ticks in a row every `every` ticks,
	until `ticks_most` are written (0: no limit). A box with no state of its
	own between ticks takes run 1; one that carries state, runs as long as its
	page says */
	unsigned int first, every, run, ticks_most;
	unsigned int file_bytes_most; /* a file is closed and the next begun before it passes this (0: 32 MB) */
	unsigned int tick_bytes_most; /* room for one tick's records, taken from the heap once (0: 512 KB) */
};

int psp_capture_open(const struct psp_capture_plan *plan); /* 1 when open */
int psp_capture_tick(unsigned int tick);                    /* 1 when this tick is sampled */
int psp_capture_sampled(void);                               /* 1 when a hook's records are wanted now */
void psp_capture_static(unsigned int key, const void *bytes, unsigned int length);
void psp_capture_static_ref(unsigned int key, const void *bytes, unsigned int length); /* kept by address, written at the file's end */
void psp_capture_begin(unsigned int id, unsigned int kind, unsigned int flags);
void psp_capture_input(const void *bytes, unsigned int length);
void psp_capture_output(const void *bytes, unsigned int length);
void psp_capture_end(void);
void psp_capture_tick_end(void);
void psp_capture_close(void);

#endif
