/* The draw list's native path, slice 1 (docs/box_draw_list.md §17; build
switch PSP_DRAWNATIVE, keep words "nativeskip" / "nativeskipoff", off by
default): a shadow of the GE's state where the model path's words are
written, so a word that sets a register to the value the GE already holds is
never sent. The census found about half of a model part's state words are
such repeats (§15).

How a word is dropped. pspgu writes every word through sendCommandi, which is
static inline in its own units, so the filter sits on pspgu's state setters,
linked with --wrap (psp_build.py, only in a PSP_DRAWNATIVE build): inside a
model part a wrapper lets pspgu write its words, so pspgu's own bookkeeping
(its enable bits, its context) is exactly what it was, then takes back at
once every word that sets a tracked register to the value the shadow holds,
moving the later words of the same call down. None of these setters moves
the stall address, so the GE has read none of their words yet. The draw is
filtered before it writes: pspgu writes the vertex type only when it's not
0, so a vertex type the GE already holds is passed as 0.

Why it's exact. The shadow is trusted only while it has seen every word
written to the list since it was last in step: a wrapper first checks that
the list is the one it knows and that nothing was written since its last
word. Words written by anything else since then are read once, up to
NS_SCAN_WORDS, following sceGuGetMemory's JUMP over a region it hands out;
past that, or at a CALL, RET, END, SIGNAL, FINISH, BJUMP or an address
offset, a jump backward or past the list's end, another list, or a list that
went back, the whole shadow is forgotten. A bone sub-list's CALL keeps it:
those lists hold bone matrix words only, which are never tracked. Forgotten
too at each frame's start and each ME job's start. Matrices, addresses,
BASE, draws and the GE's actions (CLUT load, texture flush and sync,
transfers, clear) are never dropped, and the frame buffer's words are left
alone.

Both CPUs. With gemeon the ME replays the model parts, through the same
pspgu code: each CPU keeps its own shadow, the ME's on lines only it writes
(PSP_AIME_ME_SIDE, as statememo's lit_me), chosen by PSP_ME_WORK(). On the ME
this is plain integer loads, compares and stores: no VFPU, no system call,
no clock. */
#ifdef PSP_DRAWNATIVE
#include <pspkernel.h>
#include <pspgu.h>
#include <string.h>
#include "psp_me.h"
#include "psp_native.h"

void platform_log(const char *format, ...);
int psp_keep(const char *name);
int psp_keep_number(const char *prefix, int fallback);
#ifdef PSP_RECON
int psp_ge_statecensus_holds(unsigned int cmd, unsigned int value, unsigned int *end);
#endif

struct gu_list_view { unsigned int *start, *current; int parent_context; };
extern struct gu_list_view *gu_list; /* pspgu's (guInternal.h: GuDisplayList) */

/* the most words of others the shadow reads to stay in step (a texture
bind, a marker, a call's two words); more and it forgets instead */
#define NS_SCAN_WORDS 64

/* what a command does to the shadow */
enum
{
	NS_IGNORE = 0, /* changes no tracked register: an address, a draw, BASE, a matrix, an action */
	NS_TRACK, /* a register that holds its value: dropped when it is set to the same */
	NS_JUMP,
	NS_BASE,
	NS_FORGET /* control the shadow can't follow */
};

static const unsigned char ns_kind[256] = {
	/* 0x00-0x0F: NOP, addresses, draws, BBOX: nothing tracked; JUMP; BJUMP, CALL,
	RET, END, SIGNAL, FINISH (and 0x0D) */
	[0x08] = NS_JUMP, [0x09] = NS_FORGET, [0x0A] = NS_FORGET, [0x0B] = NS_FORGET, [0x0C] = NS_FORGET,
	[0x0D] = NS_FORGET, [0x0E] = NS_FORGET, [0x0F] = NS_FORGET,
	[0x10] = NS_BASE,
	/* the vertex type; the offset address and origin change where a jump goes */
	[0x12] = NS_TRACK, [0x13] = NS_FORGET, [0x14] = NS_FORGET,
	/* the drawing region and the enables */
	[0x15 ... 0x28] = NS_TRACK,
	/* (0x2A-0x41: bone, world, view, projection and texture matrices: ignored) */
	/* viewport, texture scale and offset, screen offset */
	[0x42 ... 0x4D] = NS_TRACK,
	/* shading, normals reversed, the material, lighting and the lights */
	[0x50] = NS_TRACK, [0x51] = NS_TRACK, [0x53 ... 0x58] = NS_TRACK, [0x5B ... 0x9B] = NS_TRACK,
	/* (0x9C-0x9F: the frame and depth buffers: ignored) */
	/* texture addresses and widths, the palette's address */
	[0xA0 ... 0xB1] = NS_TRACK,
	/* (0xB2-0xB7: transfers: ignored) texture sizes, map mode, mode, format */
	[0xB8 ... 0xC3] = NS_TRACK,
	/* (0xC4: CLUT load, an action) palette format, filter, wrap, level, function, env colour */
	[0xC5 ... 0xCA] = NS_TRACK,
	/* (0xCB, 0xCC: texture flush and sync) fog, LOD slope */
	[0xCD ... 0xD0] = NS_TRACK,
	/* (0xD2, 0xD3: pixel format, clear) scissor, depth range, tests, stencil,
	blend, dither, logic op, masks */
	[0xD4 ... 0xE9] = NS_TRACK,
	/* (0xEA on: transfers, weights: ignored) */
};

/* "nativeskipkeepN" (an instrument): N's bits name groups of commands that
are tracked but never dropped, to find which group a difference comes from */
static const unsigned char ns_group_of[256] = {
	[0x12] = 1, /* the vertex type */
	[0xA0 ... 0xAF] = 2, [0xB8 ... 0xBF] = 2, /* texture addresses, widths, sizes */
	[0xB0] = 3, [0xB1] = 3, [0xC0 ... 0xCA] = 3, /* palette address, texture mode, format, filter, wrap, function */
	[0x48 ... 0x4B] = 4, /* texture scale and offset */
	[0x50 ... 0x9B] = 5, /* the material and the lights */
	[0x15 ... 0x28] = 6, /* the region and the enables */
	[0x42 ... 0x47] = 7, [0x4C] = 7, [0x4D] = 7, [0xCD ... 0xD0] = 7, [0xD4 ... 0xE9] = 7, /* the rest */
};
static unsigned char ns_drop[256] __attribute__((aligned(64)));

struct ns_shadow
{
	unsigned int *start, *pos; /* the list, and the place the shadow is in step to */
	unsigned char gen, active, pad[2];
	unsigned int dropped, sent, read, forgets, forgets_far, parts, check_right, check_wrong;
	unsigned char seen[256]; /* a value is known when its byte is gen */
	unsigned int value[256];
} __attribute__((aligned(64)));

/* the main CPU's, and the ME's on lines only the ME writes */
static struct ns_shadow ns_main PSP_MAIN_SIDE = { NULL, NULL, 1 };
static struct ns_shadow ns_me PSP_AIME_ME_SIDE = { NULL, NULL, 1 };

/* written once by the main CPU (psp_native_keeps) and only read after: a
line of its own, so no CPU writes beside it */
static struct __attribute__((aligned(64))) ns_words
{
	int on, logged, check;
	int pad[13];
} ns_words = { -1, 0 };

static void ns_forget_shadow(struct ns_shadow *S)
{
	if (++S->gen == 0)
	{
		memset(S->seen, 0, sizeof(S->seen));
		S->gen = 1;
	}
	S->forgets++;
}

static inline struct ns_shadow *ns_here(void)
{
	struct ns_shadow *S;

	if (ns_words.on <= 0)
		return NULL;
	S = PSP_ME_WORK() ? &ns_me : &ns_main;
	return S->active ? S : NULL;
}

/* the shadow brought in step with the list: the words others wrote since its
last one read, or everything forgotten */
static void ns_sync(struct ns_shadow *S)
{
	unsigned int *at = S->pos, *end = gu_list->current;
	unsigned int base = 0, view, words = 0;

	if (at == end && S->start == gu_list->start)
		return;
	if (S->start != gu_list->start || !at || end < at)
	{
		ns_forget_shadow(S);
		goto done;
	}
	view = (unsigned int)at & 0x40000000u;
	while (at < end)
	{
		unsigned int word = *at++, cmd = word >> 24;

		if (++words > NS_SCAN_WORDS)
		{
			S->forgets_far++;
			ns_forget_shadow(S);
			break;
		}
		switch (ns_kind[cmd])
		{
		case NS_TRACK:
			S->value[cmd] = word & 0xFFFFFFu;
			S->seen[cmd] = S->gen;
			continue;
		case NS_BASE:
			base = (word << 8) & 0x0F000000u;
			continue;
		case NS_JUMP:
			{
				/* sceGuGetMemory's jump over the region it hands out */
				unsigned int *to = (unsigned int *)(((base | (word & 0xFFFFFFu)) & ~3u) | view);

				if (to >= at && to <= end)
				{
					at = to;
					continue;
				}
			}
			/* fall through: a jump elsewhere */
		case NS_FORGET:
			ns_forget_shadow(S);
			at = end;
			continue;
		}
	}
	S->read += words;
done:
	S->start = gu_list->start;
	S->pos = end;
}

/* "nativeskipcheck" (PSP_RECON, with statecensus and gemeonoff): a word the
shadow would leave out, asked of the census's reader first; one the GE
doesn't hold is counted, logged and sent. 1 to leave it out */
#ifdef PSP_RECON
/* (out of line and out of the ME's copy: main CPU only, and recon only) */
static int __attribute__((noinline)) ns_held(struct ns_shadow *S, unsigned int cmd, unsigned int value, unsigned int *end)
#else
static inline int ns_held(struct ns_shadow *S, unsigned int cmd, unsigned int value, unsigned int *end)
#endif
{
#ifdef PSP_RECON
	if (ns_words.check > 0)
	{
		int held = psp_ge_statecensus_holds(cmd, value, end);

		if (held > 0)
			S->check_right++;
		else if (held == 0)
		{
			if (S->check_wrong++ < 40)
				platform_log("nativeskipcheck: command %02X value %06X left out, the GE doesn't hold it (part %u, %u forgets)",
					cmd, value, S->parts, S->forgets);
			return 0;
		}
	}
#else
	(void)S;
	(void)cmd;
	(void)value;
	(void)end;
#endif
	return 1;
}

/* the words a setter wrote from "from" on: those that set what the GE holds
taken back, the rest moved down over them */
static void ns_take_back(struct ns_shadow *S, unsigned int *from)
{
	unsigned int *at = from, *out = from, *end = gu_list->current;

	while (at < end)
	{
		unsigned int word = *at++, cmd = word >> 24;
		unsigned char kind = ns_kind[cmd];

		if (kind == NS_TRACK)
		{
			unsigned int value = word & 0xFFFFFFu;

			if (S->seen[cmd] == S->gen && S->value[cmd] == value && ns_drop[cmd] && ns_held(S, cmd, value, from))
			{
				S->dropped++;
				continue;
			}
			S->value[cmd] = value;
			S->seen[cmd] = S->gen;
		}
		else if (kind != NS_IGNORE)
			ns_forget_shadow(S); /* (no setter writes control; never expected) */
		S->sent++;
		if (out != at - 1)
			*out = word;
		out++;
	}
	gu_list->current = out;
	S->pos = out;
}

unsigned int *psp_native_open(void)
{
	struct ns_shadow *S = ns_here();

	if (!S)
		return NULL;
	ns_sync(S);
	return gu_list->current;
}

void psp_native_close(unsigned int *from)
{
	struct ns_shadow *S;

	if (!from || !(S = ns_here()))
		return;
	/* (the list moved under the block: left as written, and forgotten) */
	if (S->start != gu_list->start || gu_list->current < from)
	{
		ns_forget_shadow(S);
		S->start = gu_list->start;
		S->pos = gu_list->current;
		return;
	}
	ns_take_back(S, from);
}

/* pspgu's state setters: written, then the repeats taken back */
#define NS_SETTER(name, params, args) \
	void __real_##name params; \
	void __wrap_##name params \
	{ \
		unsigned int *from = psp_native_open(); \
		__real_##name args; \
		psp_native_close(from); \
	}

NS_SETTER(sceGuEnable, (int state), (state))
NS_SETTER(sceGuDisable, (int state), (state))
NS_SETTER(sceGuBlendFunc, (int op, int src, int dest, unsigned int srcfix, unsigned int destfix), (op, src, dest, srcfix, destfix))
NS_SETTER(sceGuDepthMask, (int mask), (mask))
NS_SETTER(sceGuDepthFunc, (int function), (function))
NS_SETTER(sceGuDepthOffset, (unsigned int offset), (offset))
NS_SETTER(sceGuTexFunc, (int tfx, int tcc), (tfx, tcc))
NS_SETTER(sceGuTexWrap, (int u, int v), (u, v))
NS_SETTER(sceGuTexScale, (float u, float v), (u, v))
NS_SETTER(sceGuTexOffset, (float u, float v), (u, v))
NS_SETTER(sceGuTexLevelMode, (unsigned int mode, float bias), (mode, bias))
NS_SETTER(sceGuTexMapMode, (int mode, unsigned int lu, unsigned int lv), (mode, lu, lv))
NS_SETTER(sceGuTexProjMapMode, (int mode), (mode))
NS_SETTER(sceGuTexEnvColor, (unsigned int color), (color))
NS_SETTER(sceGuTexSlope, (float slope), (slope))
NS_SETTER(sceGuFog, (float near, float far, unsigned int color), (near, far, color))
/* (sceGuColor is sceGuMaterial with all three: wrapped once, there) */
NS_SETTER(sceGuMaterial, (int mode, int color), (mode, color))
NS_SETTER(sceGuColorMaterial, (int components), (components))
NS_SETTER(sceGuAmbient, (unsigned int color), (color))
NS_SETTER(sceGuSpecular, (float power), (power))
NS_SETTER(sceGuLight, (int light, int type, int components, const ScePspFVector3 *position), (light, type, components, position))
NS_SETTER(sceGuLightColor, (int light, int component, unsigned int color), (light, component, color))
NS_SETTER(sceGuLightAtt, (int light, float atten0, float atten1, float atten2), (light, atten0, atten1, atten2))
NS_SETTER(sceGuLightMode, (int mode), (mode))
NS_SETTER(sceGuAlphaFunc, (int func, int value, int mask), (func, value, mask))
NS_SETTER(sceGuFrontFace, (int order), (order))
NS_SETTER(sceGuShadeModel, (int mode), (mode))
NS_SETTER(sceGuPixelMask, (unsigned int mask), (mask))

/* the matrices: never tracked, so the shadow stays in step past them */
void __real_sceGuSetMatrix(int type, const ScePspFMatrix4 *matrix);
void __wrap_sceGuSetMatrix(int type, const ScePspFMatrix4 *matrix)
{
	struct ns_shadow *S = ns_here();

	if (S)
		ns_sync(S);
	__real_sceGuSetMatrix(type, matrix);
	if (S)
		S->pos = gu_list->current;
}

void __real_sceGuBoneMatrix(unsigned int index, const ScePspFMatrix4 *matrix);
void __wrap_sceGuBoneMatrix(unsigned int index, const ScePspFMatrix4 *matrix)
{
	struct ns_shadow *S = ns_here();

	if (S)
		ns_sync(S);
	__real_sceGuBoneMatrix(index, matrix);
	if (S)
		S->pos = gu_list->current;
}

/* the draw: its vertex type left out when the GE has it. Its other words
(BASE, the addresses, PRIM) are never tracked, and its stall update writes
nothing into the list */
void __real_sceGuDrawArray(int prim, int vtype, int count, const void *indices, const void *vertices);
void __wrap_sceGuDrawArray(int prim, int vtype, int count, const void *indices, const void *vertices)
{
	struct ns_shadow *S = ns_here();

	if (!S)
	{
		__real_sceGuDrawArray(prim, vtype, count, indices, vertices);
		return;
	}
	ns_sync(S);
	if (vtype)
	{
		unsigned int value = (unsigned int)vtype & 0xFFFFFFu;

		if (S->seen[0x12] == S->gen && S->value[0x12] == value && ns_drop[0x12] &&
			ns_held(S, 0x12, value, gu_list->current))
		{
			S->dropped++;
			vtype = 0;
		}
		else
		{
			S->value[0x12] = value;
			S->seen[0x12] = S->gen;
			S->sent++;
		}
	}
	__real_sceGuDrawArray(prim, vtype, count, indices, vertices);
	S->start = gu_list->start;
	S->pos = gu_list->current;
}

void psp_native_call_bones(const void *sub)
{
	struct ns_shadow *S = ns_here();

	if (S)
		ns_sync(S);
	sceGuSendCommandi(0x10, ((unsigned int)sub >> 8) & 0xF0000);
	sceGuSendCommandi(0x0A, (unsigned int)sub & 0xFFFFFF);
	if (S)
		S->pos = gu_list->current;
}

void psp_native_part(int on)
{
	if (ns_words.on <= 0)
		return;
	if (PSP_ME_WORK())
	{
		ns_me.active = (unsigned char)on;
		ns_me.parts += on != 0;
	}
	else
	{
		ns_main.active = (unsigned char)on;
		ns_main.parts += on != 0;
	}
}

void psp_native_forget(void)
{
	if (ns_words.on <= 0)
		return;
	ns_forget_shadow(PSP_ME_WORK() ? &ns_me : &ns_main);
}

void psp_native_keeps(void)
{
	if (ns_words.on >= 0 || psp_on_me())
		return;
	ns_words.on = psp_keep("nativeskip") && !psp_keep("nativeskipoff");
	ns_words.check = psp_keep("nativeskipcheck") != 0;
	{
		int k, keep = psp_keep_number("nativeskipkeep", 0);

		for (k = 0; k < 256; k++)
			ns_drop[k] = ns_kind[k] == NS_TRACK && !(ns_group_of[k] && (keep >> (ns_group_of[k] - 1)) & 1);
		sceKernelDcacheWritebackRange(ns_drop, sizeof(ns_drop));
		if (ns_words.on && keep)
			platform_log("GE: nativeskip: command groups %d tracked but never left out (nativeskipkeep)", keep);
	}
	sceKernelDcacheWritebackRange(&ns_words, sizeof(ns_words));
	if (ns_words.on)
		platform_log("GE: nativeskip: model parts' state words that set what the GE already holds are left out");
}

void psp_native_report(void)
{
	if (ns_words.on <= 0 || psp_on_me())
		return;
	/* the ME's figures as its last job left them (the main CPU drops its
	copies of what the ME wrote after each job) */
	if (ns_words.check > 0)
		platform_log("nativeskipcheck: %u words left out that the census's reader agrees the GE holds, %u it doesn't (sent)",
			ns_main.check_right, ns_main.check_wrong);
	platform_log("nativeskip: since the start, main CPU: %u parts, %u state words sent, %u left out, %u read of others', %u forgets (%u past %d words); ME: %u parts, %u sent, %u left out, %u read, %u forgets (%u past %d)",
		ns_main.parts, ns_main.sent, ns_main.dropped, ns_main.read, ns_main.forgets, ns_main.forgets_far, NS_SCAN_WORDS,
		ns_me.parts, ns_me.sent, ns_me.dropped, ns_me.read, ns_me.forgets, ns_me.forgets_far, NS_SCAN_WORDS);
}
#endif
