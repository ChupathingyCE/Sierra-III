/* box_near_vme.h - the near loop's corner verdicts on the VME (box near-vme,
docs/box_near_vme.md section 9). Built in by PSP_NEARVME=1 only; keep word
"nearvme" turns it on, "nearvmecheck" runs the float verdict beside it and
counts where they disagree. */
#ifndef BOX_NEAR_VME_H
#define BOX_NEAR_VME_H
#ifdef PSP_NEARVME

/* a tile's vertices (the kernels' NEAR_VERTICES) and the most slots a batch
may use: 5-8 slots keep the float verdict (the lead 2026-11-21) */
#define NVME_TILE 96
#define NVME_SLOTS 4
#define NVME_ROWS 5

/* a verdict the box leaves to the float verdict: a plane value inside the
band, or a vertex the box couldn't take (its tile didn't fit, the VME failed) */
#define NVME_BAND (-1)

/* the runs' verdicts the walk works out before the corner loop, kept for it
(near_cluster_verdict's 0 mixed, 1 kept, 2 dropped); a run past these is
worked out again by the loop */
#define NVME_RUNS 256

/* one batch, as the near loop has it */
struct nvme_batch
{
	const unsigned char *vertices; /* the part's records */
	int stride; /* bytes a record */
	int position_at; /* the position's three floats, bytes into a record */
	int slots; /* slots in use, 1-4 (the batch's highest node + 1) */
	const float (*depth_row)[3][4]; /* each slot's camera rows (model_geometry) */
	float guard_x, guard_y, guard_x_length, guard_y_length;
	float near_clip, limit;
};

/* each queued vertex's verdict comes back here once: its code (outside()'s
five bits, 32 for camera_corner_out) or NVME_BAND */
typedef void (*nvme_sink)(void *context, unsigned short vertex, int code);

/* the replay job's start and end (psp_ge_me_replay): whether it runs on the
ME itself, the only place the VME is reached */
void nvme_job_begin(int on_me);
void nvme_job_end(void);

/* whether the box takes this frame's batches; whether it checks itself */
int nvme_on(void);
int nvme_checking(void);
/* whether the ME's cycle counter can be read here (on the ME, in a job) */
int nvme_timing(void);

/* a batch: 0 when the box declines it (the loop's float verdict then works
out every corner, as before) */
int nvme_batch_begin(const struct nvme_batch *in, nvme_sink sink, void *context);
/* one vertex of the batch, each once (the walk dedupes) */
void nvme_vertex(unsigned short index);
/* every queued vertex reported to the sink before this returns */
void nvme_batch_end(void);

/* the walk's runs, and the float verdicts' cost, for the ledger */
void nvme_note_runs(unsigned int cycles);
void nvme_note_float(unsigned int cycles, int in_box_batch);
void nvme_note_check(int box_code, int float_code);
/* the check's own cycles (its float verdicts), kept out of the box's price */
void nvme_note_check_cycles(unsigned int cycles);

/* the ME's cycle counter (only where nvme_timing() says so) */
static inline unsigned int nvme_cycles(void)
{
	unsigned int count;

	__asm__ __volatile__("mfc0 %0, $9" : "=r"(count));
	return count;
}

/* the main CPU's report, with the GE's */
void nvme_report(double frames);

#endif
#endif
