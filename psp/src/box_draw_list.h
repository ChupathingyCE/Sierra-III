/* The draw list box's kept blocks: one store and emitter for every kind that
keeps its state between frames (docs/box_draw_list.md §12.1b, the interface).
Built in only with PSP_DRAWLIST. The level draws are its first user
(box_draw_list_level.inc); the light pass and the model parts are to use the
same calls.

Who owns what:
- The store is one block of main RAM handed over once (dl_store_carve): an
  entry table the CPU that writes the list reads and writes, then the slots,
  which the GE reads. The GE can't read the ME's eDRAM, so neither part is
  there; the table isn't either, since the replay also runs on the main CPU.
- A user keys each kept thing by a pointer and a number, and gives a stamp:
  every input its blocks' words are made from. A stamp that differs makes the
  slot again, never in a frame the GE may still read it in (the fence).
- A slot holds up to DL_BLOCKS blocks, each a run of GE words ending in RET,
  written by ordinary pspgu calls between dl_block_begin and dl_block_end,
  and CALLed from the list. A block makes no CALL (one call level).

The GE and the caches:
- While a slot is written the stall address is held (dl_stall_held), so the GE
  is never handed an address inside a slot; dl_run_finish writes the slot's
  lines back and drops them (cache 0x1B, on either CPU) before the stall
  address can reach the CALLs, then hands the stall address on.
- A block sets every state it uses: the user forgets its memos before a slot
  is made, and after the CALLs (the GE then holds the block's state, which no
  memo names). pspgu's own CPU state is put back as the slot's making left it
  (dl_gu_*), and its value at the slot's start is part of the stamp
  (dl_gu_stamp).
- dl_draw and dl_slope write GE words straight into the list (no pspgu call):
  the user passes final addresses (after its own VRAM redirection) and hands
  the stall address on with dl_stall_pass once its draw is done.
- Code the ME runs: no VFPU instruction, no system call. */
#ifndef BOX_DRAW_LIST_H
#define BOX_DRAW_LIST_H

#include <psptypes.h>
#include <pspgu.h>
#include <pspgum.h>

#define DL_STAMP 32
#define DL_BLOCKS 8
/* a slot is made again only once the GE has finished every frame that may
CALL it: the replay of frame N follows the show of N - 2, which waits for
that list's finish (psp_ge.c's show_picture), so three frames on it is free */
#define DL_FENCE 3

/* the store: region (main RAM, 64-byte aligned, the caller's to give up for
good) of bytes, cut into slots of slot_bytes and their entries; the number of
slots, 0 if none fit. Writes the region back from this CPU's cache */
int dl_store_carve(void *region, unsigned int bytes, unsigned int slot_bytes);
int dl_store_slots(void);
unsigned int dl_store_slot_bytes(void);

/* the entry kept for (key, key2), or -1: hint first (the user's last answer for
this key, one line read), then a window of DL_PROBE entries from the pair's
hash, so a held draw reads two or three lines rather than every entry */
#define DL_PROBE 4
int dl_entry_find(const void *key, unsigned int key2, int hint);
/* an entry for (key, key2) with this stamp, at frame (found: dl_entry_find's
answer): its index, with *making 1 when its slot has to be written (new, or
its stamp failed, *failed_word then the first stamp word that differed, else
-1). DL_FENCED: its stamp failed within DL_FENCE frames of a CALL; DL_FULL: no
entry of the pair's window free of the fence */
#define DL_FENCED (-1)
#define DL_FULL (-2)
int dl_entry_get(const void *key, unsigned int key2, const unsigned int *stamp, unsigned int frame, int found,
	int *making, int *failed_word);
/* entries holding a key, kept as they are taken and freed (for the report) */
int dl_entries_used(void);
/* DL_USER words the user keeps with an entry, on its line of hot fields (its
own answers that the stamp is made from, as the textures' places in the
pack); written when making */
#define DL_USER 4
unsigned int *dl_entry_user(int entry);
/* the stamp an entry was made with */
const unsigned int *dl_entry_stamp(int entry);

/* one entry's blocks this frame: dl_run_start, then for each block
"if (dl_block_begin(&run)) { pspgu's state calls } dl_block_end(&run);"
(begin is 1 only while making: the calls then go into the slot), then
dl_run_finish. Kept, the blocks are only CALLed, in the order they were made */
struct dl_run
{
	int entry, making, at, n;
	unsigned int *slot, *list_current, *list_start;
};
void dl_run_start(struct dl_run *run, int entry, int making, const void *key, unsigned int key2,
	const unsigned int *stamp, unsigned int frame);
int dl_block_begin(struct dl_run *run);
void dl_block_end(struct dl_run *run);
/* the slot's words written back (making) or pspgu's state put back as its
making left it (kept); user[2] the user's own two values kept with it (in
when kept, out when making). 0 when a slot outgrew its room or a kept run
CALLed other than its blocks: the user then turns itself off */
int dl_run_finish(struct dl_run *run, int user[2]);
unsigned int dl_run_words(const struct dl_run *run);

/* a pool of pages in main RAM for kept data of any size (dl_pool_carve hands
it a region for good): first fit from a bitmap, a run of contiguous pages. A
pool the GE reads is written back by its user before an address enters the
list, and its user gives a run back only DL_FENCE frames after the last frame
that may have named it; a pool only the CPU that writes the list reads gives
runs back at once */
#define DL_POOL_PAGES 512
struct dl_pool
{
	unsigned char *base;
	unsigned int page_bytes, pages, used, peak, refused;
	unsigned int bits[DL_POOL_PAGES / 32];
};
int dl_pool_carve(struct dl_pool *pool, void *region, unsigned int bytes, unsigned int page_bytes);
void *dl_pool_take(struct dl_pool *pool, unsigned int bytes);
void dl_pool_give(struct dl_pool *pool, void *at, unsigned int bytes);
/* the cache lines of [at, at + bytes) written back and dropped, so the GE
reads what this CPU wrote (cache 0x1B, either CPU) */
void dl_writeback(const void *at, unsigned int bytes);

/* the emitter, straight into the list */
void dl_draw(int prim, int vtype, int count, const void *indices, const void *vertices);
void dl_slope(float slope);
/* the stall address held while nonzero (psp_ge.c's stall wrapper reads it) */
extern int dl_stall_held;
void dl_stall_pass(void);

/* pspgu's state the blocks' words are made from, as six stamp words */
void dl_gu_stamp(unsigned int *words);
/* pspgu's and pspgum's CPU state, for a user that writes a draw twice */
struct dl_gu_saved
{
	unsigned int context[32];
	int states, gum_mode, gum_update[4], gum_current_update;
	ScePspFMatrix4 *gum_current, *gum_depth[4], gum_matrix[5];
};
void dl_gu_save(struct dl_gu_saved *saved);
void dl_gu_restore(const struct dl_gu_saved *saved);

/* the check: a model of the GE's registers over two forms of the same draws
(each a run of list words, into any block of the store it CALLs). The number
of draw commands compared; *differing 1 if any differ (*what: the first
register, or 0x100 + a matrix, 0x105 the palette load, 0x106 the draw
command, 0x107 the indices, 0x108 the vertices, 0x109 the draws' count),
*unsupported 1 if a form holds a command the model doesn't follow; *first_draw the
first differing draw command's number, from 1 */
int dl_check_compare(const unsigned int *a, const unsigned int *a_end, const unsigned int *b,
	const unsigned int *b_end, int *differing, int *what, unsigned int *a_value, unsigned int *b_value, int *unsupported,
	int *first_draw);

#endif
