/* psp_gulist.h - pspgu's current display list, as the port reads and writes it.

pspgu keeps the list being written in gu_list (guInternal.h: GuDisplayList):
where it starts and where the next command word goes. psp_ge.c moves and
splits lists through it; the short forms of pspgu's own calls (psp_ge.c's
"gustate", psp_tex.c's "bindwords") write their words at current, as
pspgu's sendCommandi does. */
#ifndef PSP_GULIST_H
#define PSP_GULIST_H

struct gu_list_view { unsigned int *start, *current; int parent_context; };
extern struct gu_list_view *gu_list;

#endif
