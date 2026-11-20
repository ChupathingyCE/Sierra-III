/* The draw list's native path (docs/box_draw_list.md §16-§17), built in by
PSP_DRAWNATIVE=1. Slice 1, keep word "nativeskip": a shadow of the GE's state
where the model path's words are written, so a word that sets a register to
the value the GE already holds is never sent. */
#ifndef PSP_NATIVE_H
#define PSP_NATIVE_H

#ifdef PSP_DRAWNATIVE
/* the main CPU, once a frame before any of its records reach the ME: reads
the keep words */
void psp_native_keeps(void);
/* this CPU's shadow forgotten (a frame's start, an ME job's start) */
void psp_native_forget(void);
/* around a model part: the filter is on only inside one */
void psp_native_part(int on);
/* a block of the port's own words (psp_tex.c's bind): open before the first,
close after the last; the words between are taken back where they set what
the GE already holds. Only words that move no stall address and take no
list memory may come between */
unsigned int *psp_native_open(void);
void psp_native_close(unsigned int *from);
/* BASE and CALL of a bone sub-list (only bone matrix words and a return),
written as pspgu's sceGuCallList writes them, without its stall update */
void psp_native_call_bones(const void *sub);
/* every 300 frames, beside the other GE reports (main CPU) */
void psp_native_report(void);
#endif

#endif
