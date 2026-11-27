/* Kill test 2's stage stamps ("ktstamp", docs/ktstamp.md): log-only clock reads on the ticks psp_object_sample
times, for ask B's split of new-only's biped pass and old-only's biped work. Built only with PSP_KTSTAMP=1
(psp_build.py's -DPSP_KTSTAMP; psp_post.h includes this header then, and defines the hooks as nothing
otherwise); counting only with keep word ktstamp. Every hook is an expression placed on a line that was
already there, so no function's first line moves: the off form's text and the profiles' line checksums
are the kill test's own */
#ifndef PSP_KTSTAMP_H
#define PSP_KTSTAMP_H

enum
{
	PSP_KT_PASS_FIELD,    /* new: store_biped_pass, whole */
	PSP_KT_I,             /* new: the intents (store_coll.c's I loop and the sweep's close) */
	PSP_KT_G,             /* new: the gathers and the sweep's end */
	PSP_KT_M,             /* new: each staged biped's datum update and motion */
	PSP_KT_SETTLE,        /* new: store_biped_settle and the ends */
	PSP_KT_OWED,          /* new: store_pose_make_skipped's makes, wherever they fall */
	PSP_KT_BUPDATE,       /* old: objects.c's biped branch (type update, damage and export, node matrices) */
	PSP_KT_BPHYS,         /* old: bipeds.c's biped_update_physics calls */
	PSP_KT_FIELDS
};

/* the marks: 0-5 the pass's stage edges, 6 bupdate's start, 7 bphys's */
#define PSP_KT_MARKS 8

extern int psp_kt_on;             /* this tick is timed (keep word on, a sampled tick) */
extern int psp_kt_count;          /* keep word on: owed makes counted by site every tick */
extern unsigned long psp_kt_marks[PSP_KT_MARKS];
unsigned long psp_kt_clock(void);
void psp_kt_add(int field, unsigned long mark);
void psp_kt_pass(void);
void psp_kt_owed(void *site, unsigned long mark);
unsigned long psp_kt_tick(unsigned long scale);

#endif
