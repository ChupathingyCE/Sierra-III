/* halo_claim: the ME partition claimed for the game, nothing else.

The sound tier's window (sndpart, psp/src/psp_dsound.c) lives in the
Media Engine's 1 MB partition at 0x08300000, which nothing allocates and
nothing writes in play, across a sleep or at a relaunch (the lab's
me_partition_sleep_read.md and partmap). The claim makes that so by
contract: sysmem then can't hand the partition to anyone else. The game
can't make it itself, because sysmem's user call refuses partition 4 whatever
the caller's mode (800200d6 on the PSP-1000), and only its kernel export
takes it. So this module, loaded by the game only for sndpart, makes the
claim at start, answers what it got, and gives it back at the game's exit. It
writes no register, starts no thread and touches no memory but sysmem's
own bookkeeping. */
#include <pspkernel.h>
#include <pspsdk.h>
#include <pspsysmem_kernel.h>

PSP_MODULE_INFO("halo_claim", 0x1006, 1, 0);
PSP_NO_CREATE_MAIN_THREAD();

#define ME_PARTITION 4
#define ME_PARTITION_ADDRESS 0x08300000u
#define ME_PARTITION_BYTES (1024u * 1024u)

static SceUID claim = -1;
static int claim_error = 0;
static unsigned int claim_at = 0;

/* the system call (exports.exp): the claim's block, or the error sysmem
gave (< 0); with `at` its head address */
int halo_claim_result(unsigned int *at)
{
	unsigned int k1 = pspSdkSetK1(0);

	if (at && ((unsigned int)at & 0x1FFFFFFFu) >= 0x08800000u && ((unsigned int)at & 0x1FFFFFFFu) < 0x0A000000u)
		*at = claim_at;
	pspSdkSetK1(k1);
	return claim >= 0 ? claim : claim_error;
}

/* the system call (exports.exp): the claim given back; 0, or sysmem's error */
int halo_claim_release(void)
{
	unsigned int k1 = pspSdkSetK1(0);
	int result = 0;

	if (claim >= 0)
	{
		result = sceKernelFreePartitionMemory(claim);
		if (result >= 0)
			claim = -1;
	}
	pspSdkSetK1(k1);
	return result;
}

int module_start(SceSize args, void *argp)
{
	SceUID block;

	(void)args;
	(void)argp;
	block = sceKernelAllocPartitionMemory(ME_PARTITION, "halo sndpart", PSP_SMEM_Addr, ME_PARTITION_BYTES,
		(void *)ME_PARTITION_ADDRESS);
	if (block < 0)
		claim_error = block;
	else
	{
		claim = block;
		claim_at = (unsigned int)sceKernelGetBlockHeadAddr(block);
	}
	return 0;
}

int module_stop(SceSize args, void *argp)
{
	(void)args;
	(void)argp;
	halo_claim_release();
	return 0;
}
