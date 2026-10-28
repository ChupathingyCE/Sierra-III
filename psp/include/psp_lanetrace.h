/* psp_lanetrace.h - "lanetrace": the main CPU's, the GE's and the Media
Engine's work on one clock, frame by frame (psp/src/psp_lanetrace.c). With
PSP_METRICS=0 every hook compiles to nothing */
#ifndef PSP_LANETRACE_H
#define PSP_LANETRACE_H

#define PSP_LANE_PHASE 1   /* the main CPU's phase changed: c = the phase */
#define PSP_LANE_GE 2      /* the GE busy from `from` to t: b = the draws' kind */
#define PSP_LANE_ME_SLOT 3 /* a slot job handed over at `from`, seen done at t: b = slot << 8 | type, c = the ME's own us */
#define PSP_LANE_ME_RING 4 /* a ring job pushed at `from`, collected at t: b = type, c = the ME's own us */
#define PSP_LANE_FRAME 5   /* a frame ended at t: from = its number, b = its ticks */

#ifdef PSP_METRICS
extern volatile int psp_lane_on;
void psp_lane_note(unsigned int kind, unsigned int from, unsigned int t, unsigned int b, unsigned int c);
void psp_lane_arm(void);
void psp_lane_frame(unsigned long frame_number, unsigned int ticks);
#define PSP_LANE(kind, from, t, b, c) do { if (psp_lane_on) psp_lane_note((kind), (from), (t), (b), (c)); } while (0)
#define PSP_LANE_ARM() psp_lane_arm()
#define PSP_LANE_FRAME_END(frame_number, ticks) do { if (psp_lane_on) psp_lane_frame((frame_number), (ticks)); } while (0)
#else
#define PSP_LANE(kind, from, t, b, c) ((void)0)
#define PSP_LANE_ARM() ((void)0)
#define PSP_LANE_FRAME_END(frame_number, ticks) ((void)0)
#endif

#endif
