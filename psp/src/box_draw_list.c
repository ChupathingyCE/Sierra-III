/* The draw list box's kept blocks: the store, its entries and fence, the
blocks' making and CALLs, the emitter, pspgu's CPU state around a CALL, and
the check's model of the GE (box_draw_list.h says who owns what, and the GE's
and the caches' rules). Built in only with PSP_DRAWLIST; it runs on the ME as
well as the main CPU, so it reaches no VFPU instruction and no system call
(the main CPU's cache write-back at the carve aside) */
#ifdef PSP_DRAWLIST
#include <pspkernel.h>
#include <pspge.h>
#include <string.h>
#include "box_draw_list.h"

/* pspgu's and pspgum's own state (pspsdk's guInternal.h and gumInternal.h) */
struct dl_gu_list { unsigned int *start, *current; int parent_context; };
struct dl_gu_context
{
	struct dl_gu_list list;
	int scissor_enable, scissor_start[2], scissor_end[2], near_plane, far_plane, depth_offset, fragment_2x,
		texture_function, texture_proj_map_mode, texture_map_mode, sprite_mode[4];
	unsigned int clear_color, clear_stencil, clear_depth;
	int texture_mode;
};
extern struct dl_gu_context gu_contexts[3];
extern struct dl_gu_list *gu_list;
extern int gu_states, gu_curr_context, gu_object_stack_depth, ge_list_executed[2];
extern int gum_current_mode, gum_matrix_update[4], gum_current_matrix_update;
extern ScePspFMatrix4 *gum_current_matrix, *gum_stack_depth[4];
typedef char dl_context_fits[sizeof(struct dl_gu_context) <= sizeof(((struct dl_gu_saved *)0)->context) ? 1 : -1];

/* an entry's hot fields, one 32-byte line: all a held draw's lookup reads
before its stamp. The rest (the stamp, the blocks' places, the state the slot
leaves) is the cold part beside it */
struct dl_hot
{
	const void *key; /* NULL: free */
	unsigned int key2, last_frame;
	unsigned short words;
	unsigned char blocks, pad;
	unsigned int user[DL_USER];
};
typedef char dl_hot_is_a_line_half[sizeof(struct dl_hot) == 32 ? 1 : -1];
struct dl_entry
{
	unsigned int stamp[DL_STAMP];
	unsigned short block[DL_BLOCKS];
	int exit_states, exit_context[5], exit_user[2];
};

int dl_stall_held = 0;
static unsigned char *store;
static unsigned int store_bytes, slot_words;
static struct dl_hot *hot;
static struct dl_entry *table;
static unsigned int *slots;
static int slot_count, entries_used;

int dl_store_carve(void *region, unsigned int bytes, unsigned int slot_bytes)
{
	unsigned int hot_bytes = 0, table_bytes = 0;
	int count;

	if (store || !region || ((unsigned int)region & 63) || (slot_bytes & 63) || !slot_bytes)
		return 0;
	for (count = (int)(bytes / (slot_bytes + sizeof(struct dl_entry) + sizeof(struct dl_hot))); count > 0; count--)
	{
		hot_bytes = ((unsigned int)count * sizeof(struct dl_hot) + 63) & ~63u;
		table_bytes = ((unsigned int)count * sizeof(struct dl_entry) + 63) & ~63u;
		if (hot_bytes + table_bytes + (unsigned int)count * slot_bytes <= bytes)
			break;
	}
	if (count <= 0)
		return 0;
	store = region;
	store_bytes = bytes;
	hot = (struct dl_hot *)store;
	table = (struct dl_entry *)(store + hot_bytes);
	slots = (unsigned int *)(store + hot_bytes + table_bytes);
	slot_count = count;
	slot_words = slot_bytes / 4;
	entries_used = 0;
	memset(store, 0, hot_bytes + table_bytes);
	/* nothing of the region left in this CPU's cache to land over the ME's
	writes or the GE's reads later */
	sceKernelDcacheWritebackInvalidateRange(store, bytes);
	return count;
}

int dl_store_slots(void)
{
	return slot_count;
}

unsigned int dl_store_slot_bytes(void)
{
	return slot_words * 4;
}

static int dl_window(const void *key, unsigned int key2)
{
	unsigned int h = ((unsigned int)key >> 4) * 2654435761u ^ key2 * 40503u;

	return (int)((h >> 8) % (unsigned int)slot_count);
}

int dl_entry_find(const void *key, unsigned int key2, int hint)
{
	int i, at;

	if (!slot_count || !key)
		return -1;
	if (hint >= 0 && hint < slot_count && hot[hint].key == key && hot[hint].key2 == key2)
		return hint;
	for (i = 0, at = dl_window(key, key2); i < DL_PROBE && i < slot_count; i++, at = at + 1 < slot_count ? at + 1 : 0)
		if (hot[at].key == key && hot[at].key2 == key2)
			return at;
	return -1;
}

int dl_entry_get(const void *key, unsigned int key2, const unsigned int *stamp, unsigned int frame, int found,
	int *making, int *failed_word)
{
	int i, at, victim = -1, diff;

	*failed_word = -1;
	if (found >= 0)
	{
		const struct dl_entry *e = &table[found];

		for (diff = 0; diff < DL_STAMP && e->stamp[diff] == stamp[diff]; diff++)
			;
		*making = diff < DL_STAMP;
		if (!*making)
			return found;
		*failed_word = diff;
		return frame - hot[found].last_frame >= DL_FENCE ? found : DL_FENCED;
	}
	/* a free entry of the window first, else the one CALLed longest ago, past the fence */
	for (i = 0, at = dl_window(key, key2); i < DL_PROBE && i < slot_count; i++, at = at + 1 < slot_count ? at + 1 : 0)
	{
		const struct dl_hot *h = &hot[at];

		if (!h->key)
		{
			victim = at;
			break;
		}
		if (frame - h->last_frame >= DL_FENCE && (victim < 0 || h->last_frame - hot[victim].last_frame > 0x80000000u))
			victim = at;
	}
	*making = 1;
	return victim >= 0 ? victim : DL_FULL;
}

int dl_entries_used(void)
{
	return entries_used;
}

unsigned int *dl_entry_user(int entry)
{
	return hot[entry].user;
}

const unsigned int *dl_entry_stamp(int entry)
{
	return table[entry].stamp;
}

void dl_run_start(struct dl_run *run, int entry, int making, const void *key, unsigned int key2,
	const unsigned int *stamp, unsigned int frame)
{
	struct dl_hot *h = &hot[entry];

	run->entry = entry;
	run->making = making;
	run->at = 0;
	run->n = 0;
	run->slot = slots + (unsigned int)entry * slot_words;
	h->last_frame = frame;
	if (making)
	{
		/* a slot that outgrows its room is freed again at the finish */
		if (!h->key)
			entries_used++;
		h->key = key;
		h->key2 = key2;
		memcpy(table[entry].stamp, stamp, sizeof(table[entry].stamp));
		h->blocks = 0;
		dl_stall_held++;
	}
}

int dl_block_begin(struct dl_run *run)
{
	struct dl_entry *e = &table[run->entry];

	if (!run->making)
		return 0;
	run->list_current = gu_list->current;
	run->list_start = gu_list->start;
	gu_list->current = run->slot + run->at;
	/* sceGuCheckList goes on counting the frame's list */
	gu_list->start = gu_list->current - (run->list_current - run->list_start);
	if (run->n < DL_BLOCKS)
		e->block[run->n] = (unsigned short)run->at;
	return 1;
}

void dl_block_end(struct dl_run *run)
{
	struct dl_entry *e = &table[run->entry];
	unsigned int block, *w;

	if (run->making)
	{
		*gu_list->current++ = 11u << 24; /* RET */
		run->at = (int)(gu_list->current - run->slot);
		gu_list->current = run->list_current;
		gu_list->start = run->list_start;
	}
	block = (unsigned int)(run->slot + e->block[run->n < DL_BLOCKS ? run->n : DL_BLOCKS - 1]);
	run->n++;
	/* BASE then CALL, as sceGuCallList writes them */
	w = gu_list->current;
	w[0] = (16u << 24) | ((block >> 8) & 0xF0000u);
	w[1] = (10u << 24) | (block & 0xFFFFFFu);
	gu_list->current = w + 2;
}

int dl_run_finish(struct dl_run *run, int user[2])
{
	struct dl_entry *e = &table[run->entry];
	struct dl_hot *h = &hot[run->entry];
	struct dl_gu_context *c = &gu_contexts[0];
	int whole = 1;

	if (run->making)
	{
		whole = run->at <= (int)slot_words && run->n <= DL_BLOCKS;
		h->blocks = (unsigned char)run->n;
		h->words = (unsigned short)run->at;
		e->exit_states = gu_states;
		e->exit_context[0] = c->fragment_2x;
		e->exit_context[1] = c->texture_function;
		e->exit_context[2] = c->texture_proj_map_mode;
		e->exit_context[3] = c->texture_map_mode;
		e->exit_context[4] = c->texture_mode;
		e->exit_user[0] = user[0];
		e->exit_user[1] = user[1];
		/* the GE reads the slot from memory: written back before the stall
		address can reach the CALLs that name it */
		dl_writeback(run->slot, (unsigned int)run->at * 4u);
		dl_stall_held--;
		if (!whole)
		{
			h->key = NULL;
			entries_used--;
		}
	}
	else
	{
		whole = run->n == h->blocks;
		gu_states = e->exit_states;
		c->fragment_2x = e->exit_context[0];
		c->texture_function = e->exit_context[1];
		c->texture_proj_map_mode = e->exit_context[2];
		c->texture_map_mode = e->exit_context[3];
		c->texture_mode = e->exit_context[4];
		user[0] = e->exit_user[0];
		user[1] = e->exit_user[1];
	}
	return whole;
}

void dl_writeback(const void *at, unsigned int bytes)
{
	unsigned int line, end = (unsigned int)at + bytes;

	for (line = (unsigned int)at & ~63u; line < end; line += 64)
		__builtin_allegrex_cache(0x1B, line);
	__asm__ volatile("sync");
}

int dl_pool_carve(struct dl_pool *pool, void *region, unsigned int bytes, unsigned int page_bytes)
{
	memset(pool, 0, sizeof(*pool));
	if (!region || !page_bytes || ((unsigned int)region & 63) || (page_bytes & 63))
		return 0;
	pool->pages = bytes / page_bytes;
	if (pool->pages > DL_POOL_PAGES)
		pool->pages = DL_POOL_PAGES;
	pool->base = region;
	pool->page_bytes = page_bytes;
	return (int)pool->pages;
}

static int dl_page_used(const struct dl_pool *pool, unsigned int page)
{
	return (int)((pool->bits[page >> 5] >> (page & 31)) & 1u);
}

void *dl_pool_take(struct dl_pool *pool, unsigned int bytes)
{
	unsigned int need = (bytes + pool->page_bytes - 1) / pool->page_bytes, page, run = 0, k;

	if (!need || need > pool->pages)
	{
		pool->refused++;
		return NULL;
	}
	for (page = 0; page < pool->pages; page++)
	{
		/* a whole word of used pages skipped at once */
		if (!(page & 31) && pool->bits[page >> 5] == 0xFFFFFFFFu)
		{
			page += 31;
			run = 0;
			continue;
		}
		if (dl_page_used(pool, page))
		{
			run = 0;
			continue;
		}
		if (++run == need)
		{
			unsigned int first = page + 1 - need;

			for (k = first; k <= page; k++)
				pool->bits[k >> 5] |= 1u << (k & 31);
			pool->used += need;
			if (pool->used > pool->peak)
				pool->peak = pool->used;
			return pool->base + first * pool->page_bytes;
		}
	}
	pool->refused++;
	return NULL;
}

void dl_pool_give(struct dl_pool *pool, void *at, unsigned int bytes)
{
	unsigned int need = (bytes + pool->page_bytes - 1) / pool->page_bytes, first, k;

	if (!at || !need)
		return;
	first = (unsigned int)((unsigned char *)at - pool->base) / pool->page_bytes;
	for (k = first; k < first + need && k < pool->pages; k++)
		pool->bits[k >> 5] &= ~(1u << (k & 31));
	pool->used -= need;
}

unsigned int dl_run_words(const struct dl_run *run)
{
	return (unsigned int)run->at;
}

static unsigned int dl_bits(float f)
{
	union { float f; unsigned int u; } bits;

	bits.f = f;
	return bits.u;
}

void dl_draw(int prim, int vtype, int count, const void *indices, const void *vertices)
{
	unsigned int *w = gu_list->current;

	*w++ = (18u << 24) | ((unsigned int)vtype & 0xFFFFFFu);
	if (indices)
	{
		*w++ = (16u << 24) | (((unsigned int)indices >> 8) & 0xF0000u);
		*w++ = (2u << 24) | ((unsigned int)indices & 0xFFFFFFu);
	}
	*w++ = (16u << 24) | (((unsigned int)vertices >> 8) & 0xF0000u);
	*w++ = (1u << 24) | ((unsigned int)vertices & 0xFFFFFFu);
	*w++ = (4u << 24) | ((unsigned int)prim << 16) | ((unsigned int)count & 0xFFFFu);
	gu_list->current = w;
}

void dl_slope(float slope)
{
	*gu_list->current++ = (208u << 24) | (dl_bits(slope) >> 8);
}

/* through psp_ge.c's wrapper (--wrap), which on the ME publishes the address
for the main CPU's forwarding thread instead of calling the kernel */
void dl_stall_pass(void)
{
	if (!dl_stall_held && gu_curr_context == GU_DIRECT && !gu_object_stack_depth)
		sceGeListUpdateStallAddr(ge_list_executed[0], gu_list->current);
}

void dl_gu_stamp(unsigned int *words)
{
	const struct dl_gu_context *c = &gu_contexts[0];

	words[0] = (unsigned int)gu_states;
	words[1] = (unsigned int)c->fragment_2x;
	words[2] = (unsigned int)c->texture_function;
	words[3] = (unsigned int)c->texture_proj_map_mode;
	words[4] = (unsigned int)c->texture_map_mode;
	words[5] = (unsigned int)c->texture_mode;
}

void dl_gu_save(struct dl_gu_saved *s)
{
	int m;

	memcpy(s->context, &gu_contexts[0], sizeof(struct dl_gu_context));
	s->states = gu_states;
	s->gum_mode = gum_current_mode;
	s->gum_current_update = gum_current_matrix_update;
	s->gum_current = gum_current_matrix;
	s->gum_matrix[4] = *gum_current_matrix;
	for (m = 0; m < 4; m++)
	{
		s->gum_update[m] = gum_matrix_update[m];
		s->gum_depth[m] = gum_stack_depth[m];
		s->gum_matrix[m] = *gum_stack_depth[m];
	}
}

void dl_gu_restore(const struct dl_gu_saved *s)
{
	struct dl_gu_list list = gu_contexts[0].list;
	int m;

	memcpy(&gu_contexts[0], s->context, sizeof(struct dl_gu_context));
	gu_contexts[0].list = list;
	gu_states = s->states;
	gum_current_mode = s->gum_mode;
	gum_current_matrix_update = s->gum_current_update;
	for (m = 0; m < 4; m++)
	{
		gum_matrix_update[m] = s->gum_update[m];
		gum_stack_depth[m] = s->gum_depth[m];
		*gum_stack_depth[m] = s->gum_matrix[m];
	}
	gum_current_matrix = s->gum_current;
	*gum_current_matrix = s->gum_matrix[4];
}

/* ---------- the check's model of the GE: at every draw command, the
registers either form set since it began must be equal, and the draw itself
(its kind and count, its vertices and indices by place or by content) */
#define DL_MATRICES 5
struct dl_sim
{
	const unsigned int *pc, *end, *ret;
	unsigned int regs[256], vaddr, iaddr, clut[3], prim, called;
	unsigned char set[256], vaddr_set, iaddr_set, clut_set, unsupported;
	unsigned int matrix[DL_MATRICES][96];
	unsigned char matrix_set[DL_MATRICES][96], matrix_at[DL_MATRICES];
};
static struct dl_sim sims[2];

/* 1 at a draw (s->prim), 0 at the end of the words */
static int sim_next(struct dl_sim *s)
{
	/* bones, world, view, projection, texture: an index command, then its data */
	static const unsigned char matrix_index[DL_MATRICES] = { 0x2A, 0x3A, 0x3C, 0x3E, 0x40 };
	int m;

	for (;;)
	{
		unsigned int word, command, argument;

		/* the list's words end at end; a block's at its RET, within its slot */
		if (!s->ret && s->pc >= s->end)
			break;
		if (s->ret && ++s->called > slot_words)
		{
			s->unsupported = 1;
			return 0;
		}
		word = *s->pc++;
		command = word >> 24;
		argument = word & 0xFFFFFFu;
		switch (command)
		{
		case 0x00: /* NOP */
		case 0x07: /* bounding box */
		case 0x0C: case 0x0E: case 0x0F: /* END, SIGNAL, FINISH */
		case 0xCB: case 0xCC: /* texture flush, sync */
			continue;
		case 0x01:
			s->vaddr = ((s->regs[0x10] & 0xF0000u) << 8) | argument;
			s->vaddr_set = 1;
			continue;
		case 0x02:
			s->iaddr = ((s->regs[0x10] & 0xF0000u) << 8) | argument;
			s->iaddr_set = 1;
			continue;
		case 0x04:
			s->prim = argument;
			return 1;
		case 0x0A:
		{
			unsigned int target = ((s->regs[0x10] & 0xF0000u) << 8) | argument;
			unsigned int at = (unsigned int)store & 0x0FFFFFFFu;

			if (s->ret || target < at || target >= at + store_bytes)
			{
				s->unsupported = 1;
				return 0;
			}
			s->ret = s->pc;
			s->called = 0;
			s->pc = (const unsigned int *)(store + (target - at));
			continue;
		}
		case 0x0B:
			if (!s->ret)
			{
				s->unsupported = 1;
				return 0;
			}
			s->pc = s->ret;
			s->ret = NULL;
			continue;
		case 0xC4: /* the palette, loaded from where CBP names */
			s->clut[0] = s->regs[0xB0];
			s->clut[1] = s->regs[0xB1];
			s->clut[2] = argument;
			s->clut_set = 1;
			continue;
		case 0x05: case 0x06: case 0x08: case 0x09:
			s->unsupported = 1;
			return 0;
		}
		for (m = 0; m < DL_MATRICES; m++)
		{
			if (command == matrix_index[m])
			{
				s->matrix_at[m] = (unsigned char)(argument % 96u);
				break;
			}
			if (command == matrix_index[m] + 1u)
			{
				unsigned int at = s->matrix_at[m] % 96u;

				s->matrix[m][at] = argument;
				s->matrix_set[m][at] = 1;
				s->matrix_at[m] = (unsigned char)(at + 1);
				break;
			}
		}
		if (m < DL_MATRICES)
			continue;
		s->regs[command] = argument;
		s->set[command] = 1;
	}
	return 0;
}

static unsigned int vertex_bytes(unsigned int vtype)
{
	static const unsigned char sizes[4] = { 0, 1, 2, 4 };
	unsigned int size = 0, align = 1, t;

#define DL_ADD(bytes, count) do { unsigned int b_ = (bytes); size = (size + b_ - 1) / b_ * b_ + b_ * (count); \
	if (b_ > align) align = b_; } while (0)
	if ((t = (vtype >> 9) & 3) != 0)
		DL_ADD(sizes[t], ((vtype >> 14) & 7) + 1);
	if ((t = vtype & 3) != 0)
		DL_ADD(sizes[t], 2);
	if ((t = (vtype >> 2) & 7) != 0)
		DL_ADD(t == 7 ? 4u : 2u, 1);
	if ((t = (vtype >> 5) & 3) != 0)
		DL_ADD(sizes[t], 3);
	if ((t = (vtype >> 7) & 3) != 0)
		DL_ADD(sizes[t], 3);
#undef DL_ADD
	return ((size + align - 1) / align * align) * (((vtype >> 18) & 7) + 1);
}

static int sim_compare(const struct dl_sim *a, const struct dl_sim *b)
{
	unsigned int r, vtype = a->regs[0x12], count = a->prim & 0xFFFFu, index_bytes, vertices;
	int m, i;

	for (r = 0x11; r < 256; r++)
		if ((a->set[r] || b->set[r]) && (a->set[r] != b->set[r] || a->regs[r] != b->regs[r]))
			return (int)r;
	for (m = 0; m < DL_MATRICES; m++)
		for (i = 0; i < 96; i++)
			if ((a->matrix_set[m][i] || b->matrix_set[m][i]) &&
				(a->matrix_set[m][i] != b->matrix_set[m][i] || a->matrix[m][i] != b->matrix[m][i]))
				return 0x100 + m;
	if (a->clut_set != b->clut_set || (a->clut_set && memcmp(a->clut, b->clut, sizeof(a->clut))))
		return 0x105;
	if (a->prim != b->prim)
		return 0x106;
	index_bytes = (vtype >> 11) & 3 ? 1u << (((vtype >> 11) & 3) - 1) : 0;
	vertices = count;
	if (index_bytes)
	{
		if (!a->iaddr_set || !b->iaddr_set)
			return 0x107;
		if (a->iaddr != b->iaddr && memcmp((const void *)a->iaddr, (const void *)b->iaddr, count * index_bytes))
			return 0x107;
		/* the vertices the indices reach */
		vertices = 0;
		for (i = 0; i < (int)count; i++)
		{
			unsigned int v = index_bytes == 1 ? ((const unsigned char *)a->iaddr)[i] :
				index_bytes == 2 ? ((const unsigned short *)a->iaddr)[i] : ((const unsigned int *)a->iaddr)[i];

			if (v + 1 > vertices)
				vertices = v + 1;
		}
	}
	if (!a->vaddr_set || !b->vaddr_set)
		return 0x108;
	if (a->vaddr != b->vaddr && memcmp((const void *)a->vaddr, (const void *)b->vaddr, vertices * vertex_bytes(vtype)))
		return 0x108;
	return 0;
}

int dl_check_compare(const unsigned int *a, const unsigned int *a_end, const unsigned int *b,
	const unsigned int *b_end, int *differing, int *what, unsigned int *a_value, unsigned int *b_value, int *unsupported,
	int *first_draw)
{
	int draws = 0, more_a, more_b;

	memset(sims, 0, sizeof(sims));
	sims[0].pc = a;
	sims[0].end = a_end;
	sims[1].pc = b;
	sims[1].end = b_end;
	*differing = 0;
	*first_draw = 0;
	for (;;)
	{
		int r;

		more_a = sim_next(&sims[0]);
		more_b = sim_next(&sims[1]);
		if (!more_a || !more_b)
			break;
		draws++;
		r = sim_compare(&sims[0], &sims[1]);
		if (r && !*differing)
		{
			*differing = 1;
			*first_draw = draws;
			*what = r;
			*a_value = r < 256 ? sims[0].regs[r] : 0;
			*b_value = r < 256 ? sims[1].regs[r] : 0;
		}
	}
	*unsupported = sims[0].unsupported || sims[1].unsupported;
	if (!*unsupported && more_a != more_b && !*differing)
	{
		*differing = 1;
		*first_draw = draws + 1;
		*what = 0x109;
		*a_value = *b_value = 0;
	}
	return draws;
}
#endif
