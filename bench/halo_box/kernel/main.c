/* halo_boxk: the bench's kernel side. The CPU's own counters (cycles, the
cycles it waited on memory, cache misses, write-backs) sit at 0xBC400000,
where only kernel mode reads; this module switches them on and hands the 19
of them to the bench through one system call. Nothing else: no thread, no
interrupt, no register written but the counters' own. */
#include <pspkernel.h>

PSP_MODULE_INFO("halo_boxk", 0x1006, 1, 0);
PSP_NO_CREATE_MAIN_THREAD();

#define PROFILER ((volatile unsigned int *)0xBC400000)
#define COUNTERS 19

/* the system call (exports.exp): the counters as they stand, in the order of
the SDK's PspDebugProfilerRegs after its enable word */
int halo_boxk_counters(unsigned int *out)
{
	unsigned int k;

	for (k = 0; k < COUNTERS; k++)
		out[k] = PROFILER[1 + k];
	return COUNTERS;
}

int module_start(SceSize args, void *argp)
{
	unsigned int k;

	(void)args;
	(void)argp;
	PROFILER[0] = 0;
	for (k = 1; k <= COUNTERS; k++)
		PROFILER[k] = 0;
	PROFILER[0] = 1;
	return 0;
}
