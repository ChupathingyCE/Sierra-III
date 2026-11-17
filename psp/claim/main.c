/* halo_claim: the ME partition claimed for the game, nothing else.

The sound tier's window (sndpart, psp/src/psp_dsound.c) lives in the
Media Engine's 1 MB partition at 0x08300000, which nothing allocates and
nothing writes in play, across a sleep or at a relaunch (the lab's
me_partition_sleep_read.md and partmap). The claim makes that so by
contract: sysmem then can't hand the claimed part to anyone else. The game
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
/* the partition's bottom 960 KB, not all of it: sysmem refuses to free a
block that is the whole partition (80020001, mid-game and at the exit; the
lab's sierra-hw 44069624252) but frees this one (44073328977). The sound
window, the bottom 4 KB page aside, fits in it (psp_dsound.c, SNDPART_MOST) */
#define CLAIM_BYTES (960u * 1024u)

static SceUID claim = -1;
static int claim_error = 0;
static unsigned int claim_at = 0;
/* partition 4's free size right after the claim: the game's own partition
list is a snapshot halo_profile.prx takes once, and can't see the claim */
static int free_after_claim = -1;
static SceUID claim_make(void)
{
	/* from the bottom, not at an address: sysmem keeps a kernel partition's
	addresses in the cached kernel segment, and refuses both 0x08300000 and
	0x88300000 by address (800200d9 on the PSP-3000, the lab's sierra-hw
	44063972557 and 44073328977). The bottom is the partition's base, and the
	game checks the address it got */
	return sceKernelAllocPartitionMemory(ME_PARTITION, "halo sndpart", PSP_SMEM_Low, CLAIM_BYTES, NULL);
}

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

/* the system call (exports.exp), under the game's keep word claimprobe: the
claim given back and made again mid-game, so a refusal at the exit can be told
from a refusal of the call itself. out[6]: the free size after the claim at
start, before this probe's free, the free's result, the free size after it,
the claim's result, the free size after it. Returns the claim, or < 0. */
int halo_claim_probe(int *out)
{
	unsigned int k1 = pspSdkSetK1(0);
	int r[6], k;

	r[0] = free_after_claim;
	r[1] = sceKernelPartitionMaxFreeMemSize(ME_PARTITION);
	r[2] = claim >= 0 ? sceKernelFreePartitionMemory(claim) : -1;
	if (r[2] >= 0)
		claim = -1;
	r[3] = sceKernelPartitionMaxFreeMemSize(ME_PARTITION);
	r[4] = -1;
	if (claim < 0)
	{
		r[4] = claim_make();
		if (r[4] >= 0)
		{
			claim = r[4];
			claim_at = (unsigned int)sceKernelGetBlockHeadAddr(claim);
		}
	}
	r[5] = sceKernelPartitionMaxFreeMemSize(ME_PARTITION);
	if (out && ((unsigned int)out & 0x1FFFFFFFu) >= 0x08800000u && ((unsigned int)out & 0x1FFFFFFFu) < 0x0A000000u - sizeof(r))
		for (k = 0; k < 6; k++)
			out[k] = r[k];
	pspSdkSetK1(k1);
	return claim >= 0 ? claim : r[4];
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
	block = claim_make();
	if (block < 0)
		claim_error = block;
	else
	{
		claim = block;
		claim_at = (unsigned int)sceKernelGetBlockHeadAddr(block);
	}
	free_after_claim = sceKernelPartitionMaxFreeMemSize(ME_PARTITION);
	return 0;
}

int module_stop(SceSize args, void *argp)
{
	(void)args;
	(void)argp;
	halo_claim_release();
	return 0;
}
