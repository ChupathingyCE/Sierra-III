/* psp_vme.c - the VME (the Media Engine's vector unit) from the port's own ME
start, beside the ME's jobs: "vmetest", a measurement, off by default.

The sequence and the library code below are mcidclan's (me-core-mapper's
vme-lib.c and his mat4x4-mul-distributed sample, MIT: psp/src/vme/LICENSE.md),
unchanged except that each firmware call is ours: the register writes Sierra-III's
projects/sierra/research/vme5 round 5 proved on the PSP-1000 (docs/vme_round5.md). Nothing here
calls Sony's ME firmware, a system call or the VFPU on the ME.

What the job proves, per heartbeat window: the VME's 4x4 matrix-by-vector
product is exact against the main CPU's, with the ME's audio and render jobs
running, and both of the VME's waits end within a bound. The first job after
the ME starts sets the VME up in full (wipe and context); the rest load a new
vector and run the context again, as his sample's loop does, which shows the
VME keeps its set-up across the ME's other jobs. */
#include "psp_me.h"
#include "vme/vme-lib.h"

int psp_keep(const char *name);
void platform_log(const char *format, ...);

/* the ME's side (run_job, psp_me.c): no VFPU, no system call, nothing but
plain loads, stores and the VME's registers. Its data is on its own stack and
in the VME's buffers (0x44xxxxxx, uncached), so no line is shared with the
main CPU; what comes back goes in the slot's results */

/* a wait's two bounds, in the ME's cycle counter's ticks: the busy bit may
already have come and gone before the first look (round 5: 7 of 8 pass waits
never saw it within 4,096 spins), so the first loop is short; the second is
the job's real bound, far under the slot's 500 ms */
#define VME_SEEN_BOUND 2000u
#define VME_CLEAR_BOUND 2000000u

struct vme_waits
{
	unsigned int count, never_busy, bounded, longest;
};

/* the ME writes this during its job: on lines of its own (psp_me.h) */
static PSP_AIME_ME_SIDE struct vme_waits *vme_waits_now = 0;

static inline unsigned int vme_cycles(void)
{
	unsigned int count;

	__asm__ __volatile__("mfc0 %0, $9" : "=r"(count));
	return count;
}

static inline void meLibSync(void)
{
	__asm__ __volatile__("sync");
}

/* the clocks, by read-modify-write only: 0xbc100050 is shared, its bit 0 is
the ME's own bus clock (his handler's whole-word write isn't copied) */
static void meCoreBusClockEnableDMACPrimMux(void) { hw(0xbc100050) |= 0x20; meLibSync(); }
static void meCoreBusClockDisableDMACPrimMux(void) { hw(0xbc100050) &= ~0x20u; meLibSync(); }
static void meCoreBusClockEnableVMECtrl(void) { hw(0xbc100054) |= 1; meLibSync(); }
static void meCoreBusClockDisableVMECtrl(void) { hw(0xbc100054) &= ~1u; meLibSync(); }

static void meCoreDMACPrimMuxSetCtrl_0x018(void)
{
	hw(0x440ff008) = 0x18;
	meLibSync();
}

/* the firmware's wait halts the ME for the controller's interrupt; the port's
ME runs with interrupts off, so this polls, both loops bounded and counted,
then makes the firmware's closing write */
static void vme_wait(unsigned int mask)
{
	struct vme_waits *w = vme_waits_now;
	unsigned int start = vme_cycles(), took;

	while (!(hw(0x440ff000) & mask) && vme_cycles() - start < VME_SEEN_BOUND)
		;
	if (!(hw(0x440ff000) & mask))
		w->never_busy++;
	while ((hw(0x440ff000) & mask) && vme_cycles() - start < VME_CLEAR_BOUND)
		;
	took = vme_cycles() - start;
	if (hw(0x440ff000) & mask)
		w->bounded++;
	if (took > w->longest)
		w->longest = took;
	w->count++;
	hw(0x440ff000) = (mask >> 8) | 0x80;
	meLibSync();
}

static void meCoreDMACPrimWaitVMEFinish(void) { vme_wait(0x800); }
static void meCoreDMACPrimWaitTransferFinish(void) { vme_wait(0x200); }

/* the VME's datapath is word registers: word stores only */
static void meCoreMemset(void *dst, int value, unsigned int size)
{
	unsigned int k, word = ((unsigned int)value & 0xffu) * 0x01010101u;

	for (k = 0; k < size / 4; k++)
		((volatile unsigned int *)dst)[k] = word;
}

/* mcidclan's vme-lib.c, the functions his sample reaches */
void vmeLibEnable() {
  meCoreBusClockEnableDMACPrimMux();
  meCoreBusClockEnableVMECtrl();
}

void vmeLibDisable() {
  meCoreBusClockDisableDMACPrimMux();
  meCoreBusClockDisableVMECtrl();
}

void vmeLibConfigTransfer(const int status) {
  hw(0x440ff000) |= status | 0x80;
  hw(0x440ff004) |= 0x10;
  meLibSync();
}

void vmeLibTrigger() {
  meCoreDMACPrimMuxSetCtrl_0x018();
}

void vmeLibSetFlow(const int mode) {
  hw(0x440ff008) = mode;
}

void _vmeLibStart() {
  vmeLibSetFlow(VME_DEFAULT);
  meLibSync();
}

void _vmeLibFinish() {
  vmeLibTrigger();
  meCoreDMACPrimWaitVMEFinish();
}

void vmeLibClearLocalBuffer(const int dst, const int count) {
  vmeLibConfigTransfer(0);
  hw(0x440ff014) = count - 1;
  hw(0x440ff018) = dst;
  hw(0x440ff008) = 0x21;
  meCoreDMACPrimWaitTransferFinish();
}

void vmeLibSetInnerAGU1(const int offset, const int count, const int stride) {
  hw(0x440ff020) = offset;
  hw(0x440ff024) = count;
  hw(0x440ff028) = stride;
}

void vmeLibSetInnerAGU2(const int offset, const int count, const int stride) {
  hw(0x440ff030) = offset;
  hw(0x440ff034) = count;
  hw(0x440ff038) = stride;
}

void vmeLibWipe() {
  vmeLibConfigTransfer(0);
  _vmeLibStart();
  meCoreMemset((void*)VME_DATAPATH_BASE, 0, 0x01a8);
  _vmeLibFinish();
  vmeLibStart();
  vme_icn(INPUT, 0x3210);
  vme_icn(FLOW, 0x3210);
  vme_icn(ARCH, 0x3210);
  vme_set(ENABLE, FU_1, 0xf0000000);
  vme_pe0(agu_top(MODE), 0x84000000);
  vme_pe0(agu_top(COUNT), 0x00010000, 2048 - 1);
  vme_pe0(vme_fu(PRIMARY), 0x00004000);
  vme_pe0(vme_fu(SECONDARY), 0x00004000);
  vme_pe1(vme_fu(PRIMARY), 0x00004000);
  vme_pe1(vme_fu(SECONDARY), 0x00004000);
  vme_pe2(vme_fu(PRIMARY), 0x00004000);
  vme_pe2(vme_fu(SECONDARY), 0x00004000);
  vme_pe3(vme_fu(PRIMARY), 0x00004000);
  vme_pe3(vme_fu(SECONDARY), 0x00004000);
  vmeLibFinish();
  _vmeLibStart();
  meCoreMemset((void*)VME_DATAPATH_BASE, 0, 0x01a8);
  _vmeLibFinish();
  vmeLibClearLocalBuffer(0, 0x2000);
  vmeLibClearLocalBuffer(0x8000, 0x2000);
  vmeLibSetInnerAGU1(0, 0, 0);
  vmeLibSetInnerAGU2(0, 0, 0);
  hw(0x440ff01c) = 0;
  hw(0x440ff02c) = 0;
  hw(0x440ff03c) = 0;
  meLibSync();
}

/* his sample's context: 4x4 matrix-by-vector, one matrix row on each of the
four processing elements */
static void vme_mat4x4_context(void) {
  vmeLibStart();
  vme_icn(AGU_TOP, 0);
  vme_icn(AGU_BASE, VME_DEF_MAPPER);
  vme_icn(AGU_WRITE, 0);
  const int count = 4 - 1;
  {
    const u32 mux = vme_mux(TOP_0, BASE_0);
    vme_pe0(vme_fu(PRIMARY), mux, 0x00240000);
    vme_pe0(agu_top(MODE), VME_DEF_MODE);
    vme_pe0(agu_top(COUNT), VME_DEF_STEP, count);
    vme_pe0(agu_base(MODE), VME_DEF_MODE, 4);
    vme_pe0(agu_base(COUNT), VME_DEF_STEP, count);
    vme_pe0(agu_write(MODE), VME_DEF_MODE, vme_cyc(0x06));
    vme_pe0(agu_write(COUNT), VME_DEF_STEP, count);
  }
  {
    const u32 mux = vme_mux(TOP_1, BASE_0);
    vme_pe1(vme_fu(PRIMARY), mux, 0x00240000);
  }
  {
    const u32 mux = vme_mux(TOP_2, BASE_0);
    vme_pe2(vme_fu(PRIMARY), mux, 0x00240000);
  }
  {
    const u32 mux = vme_mux(TOP_3, BASE_0);
    vme_pe3(vme_fu(PRIMARY), mux, 0x00240000);
  }
  vmeLibFinish();
}

/* his sample's input: the matrix (rows 0-3 to the four top buffers) and the
four vectors his Triangle and Cross step through */
static const unsigned int vme_matrix[16] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};
static const unsigned int vme_vectors[16] = {
	0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x04, 0x08, 0x00, 0x00, 0x00, 0x01,
};

/* args: 0 the vector (0-3), 1 set up in full first. results: 0-3 the VME's
product, 4 waits, 5 waits that never saw the busy bit, 6 waits that hit
their bound, 7 the longest wait in cycles. 0xFFFFFFFF in 0 when not on the ME
(the emulator has no VME: nothing is touched) */
void psp_vme_job(volatile unsigned int *args, volatile unsigned int *results, int on_me)
{
	struct vme_waits waits = { 0, 0, 0, 0 };
	unsigned int out[4], k;
	const unsigned int *vector = &vme_vectors[(args[0] & 3u) * 4];

	if (!on_me)
	{
		results[0] = 0xFFFFFFFFu;
		for (k = 1; k < ME_JOB_RESULTS; k++)
			results[k] = 0;
		return;
	}
	vme_waits_now = &waits;
	vmeLibEnable();
	if (args[1])
	{
		vmeLibWipe();
		for (k = 0; k < 4; k++)
			for (out[0] = 0; out[0] < 4; out[0]++)
				((volatile unsigned int *)(VME_TOP_BUFFERS + 0x2000 * k))[out[0]] = vme_matrix[k * 4 + out[0]];
		for (k = 0; k < 4; k++)
			((volatile unsigned int *)(VME_BASE_BUFFER_0 + 16))[k] = vector[k];
		vme_mat4x4_context();
	}
	else
	{
		for (k = 0; k < 4; k++)
			((volatile unsigned int *)(VME_BASE_BUFFER_0 + 16))[k] = vector[k];
		vmeLibStart();
		vmeLibRefresh();
	}
	/* his vmeExtGetWord4FromDistributedVMac: word 3 of each base buffer */
	for (k = 0; k < 4; k++)
		out[k] = ((volatile unsigned int *)(VME_BASE_BUFFERS + 0x2000 * k))[3];
	vmeLibDisable();
	for (k = 0; k < 4; k++)
		results[k] = out[k];
	results[4] = waits.count;
	results[5] = waits.never_busy;
	results[6] = waits.bounded;
	results[7] = waits.longest;
}

/* the main CPU's side: once a heartbeat window, after the eDRAM probe */
void psp_me_vme_probe(void)
{
	static int asked = -1;
	static unsigned int passes, exact, wrong, full_exact, refresh_exact, waits, never_busy, bounded, longest,
		simulated, failed, set_up;
	unsigned int args[2], results[ME_JOB_RESULTS] = { 0 }, want[4], k, j, ok;

	if (asked < 0)
	{
		asked = psp_keep("vmetest") != 0;
		if (asked && (psp_keep("soundedram") || psp_keep("edramcheck")))
		{
			platform_log("vme: refused: soundedram or edramcheck shares the probe slot");
			asked = 0;
		}
		if (asked)
			platform_log("vme: vmetest on: mcidclan's mat4x4 on the VME once a heartbeat window, checked here");
	}
	if (!asked || !psp_me_running())
		return;
	args[0] = passes & 3u;
	args[1] = !set_up;
	if (!psp_me_run(ME_SLOT_PROBE, ME_JOB_VME, args, 2, results, 500))
	{
		/* the ME stopped or restarted: set up in full again */
		failed++;
		set_up = 0;
		return;
	}
	passes++;
	if (results[0] == 0xFFFFFFFFu)
	{
		simulated++;
		if (simulated == 1 || (simulated % 64) == 0)
			platform_log("vme: %u jobs simulated (the emulator has no VME; nothing touched)", simulated);
		return;
	}
	for (k = 0; k < 4; k++)
	{
		want[k] = 0;
		for (j = 0; j < 4; j++)
			want[k] += vme_matrix[k * 4 + j] * vme_vectors[args[0] * 4 + j];
	}
	ok = results[0] == want[0] && results[1] == want[1] && results[2] == want[2] && results[3] == want[3];
	if (ok)
	{
		exact++;
		if (args[1])
			full_exact++;
		else
			refresh_exact++;
		set_up = 1;
	}
	else
	{
		wrong++;
		set_up = 0;
		if (wrong <= 8)
			platform_log("vme: pass %u (%s, vector %u) MISMATCH: got %u %u %u %u, want %u %u %u %u", passes,
				args[1] ? "full" : "refresh", args[0], results[0], results[1], results[2], results[3], want[0],
				want[1], want[2], want[3]);
	}
	waits += results[4];
	never_busy += results[5];
	bounded += results[6];
	if (results[7] > longest)
		longest = results[7];
	if (passes == 1 || (passes % 8) == 0 || !ok)
		platform_log("vme: %u passes: %u exact (%u full, %u refresh), %u mismatched, %u not run; waits %u, busy never seen %u, hit their bound %u, longest %u cycles",
			passes, exact, full_exact, refresh_exact, wrong, failed, waits, never_busy, bounded, longest);
}
