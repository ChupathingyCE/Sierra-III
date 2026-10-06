/*
PSP_MEMORY.C

Xbox contiguous memory (XPhysicalAlloc) for the PSP; replaces
port/linux/src/xbox_memory.c.

The game asks for its game state and tag cache at fixed addresses
(physical_memory_map.c). On the PSP those are blocks of user RAM taken at
exactly that address from the kernel (PSP_SMEM_Addr), the same way
halo_loadtest reserved the tag window. Everything else comes from the C
heap. There is no memory protection on the PSP; the protection the game
asks for is remembered per 4 KB page so XQueryMemoryProtect answers as the
Xbox would.
*/

#include "platform.h"

#include <string.h>
#include <pspsysmem.h>

/* newlib's; the port's own <malloc.h> stands in front of it */
void *memalign(size_t alignment, size_t size);
void free(void *pointer);

#define USER_RAM_BASE 0x08800000UL
#ifdef PSP_RECON_64MB
#define USER_RAM_END 0x0C000000UL /* build 44: 64 MB recon variant (psp_build.py) */
#else
#define USER_RAM_END 0x0A000000UL
#endif
#define PAGE_SIZE_BYTES 0x1000UL
#define USER_PAGE_COUNT ((USER_RAM_END - USER_RAM_BASE) / PAGE_SIZE_BYTES)
#define MAXIMUM_PLACED_BLOCKS 8

/* build 68: a byte a page (the PAGE_ values are under 0x100): 6 KB, not 24 */
static unsigned char page_protection[USER_PAGE_COUNT];
static struct
{
	unsigned long address;
	SceUID block;
} placed_blocks[MAXIMUM_PLACED_BLOCKS];
static pthread_mutex_t memory_lock = PTHREAD_MUTEX_INITIALIZER;

static BOOL in_user_ram(unsigned long address)
{
	return address >= USER_RAM_BASE && address < USER_RAM_END;
}

static void set_protection(unsigned long address, unsigned long size, DWORD protect)
{
	unsigned long page, last;

	/* build 89: size 0 at the start of user RAM made "last" wrap to the top
	of the table and the loop wrote a megabyte past it (review) */
	if (!in_user_ram(address) || !size)
		return;
	if (address + size > USER_RAM_END)
		size = USER_RAM_END - address;
	last = (address + size - 1 - USER_RAM_BASE) / PAGE_SIZE_BYTES;
	for (page = (address - USER_RAM_BASE) / PAGE_SIZE_BYTES; page <= last; page++)
		page_protection[page] = (unsigned char)protect;
}

/* nothing is protected on the PSP, so nothing needs the Linux layer's
special handling of "contiguous" memory (reads bounced through a staging
buffer, write tracking) */
BOOL platform_is_contiguous(const void *address)
{
	(void)address;
	return FALSE;
}

void *platform_contiguous_alloc(unsigned long size, unsigned long alignment,
	unsigned long physical_address, DWORD protect)
{
	void *result = NULL;
	long index;

	if (size >= 0x10000)
	{
		void platform_log(const char *format, ...);

		platform_log("heap: contiguous %lu KB for %p, %p", size / 1024, __builtin_return_address(0),
			__builtin_return_address(1));
	}
	protect &= ~(PAGE_WRITECOMBINE | PAGE_NOCACHE);
	if (!protect)
		protect = PAGE_READWRITE;
	size = (size + PAGE_SIZE_BYTES - 1) & ~(PAGE_SIZE_BYTES - 1);
	if (!size)
		size = PAGE_SIZE_BYTES;

	pthread_mutex_lock(&memory_lock);
	if (in_user_ram(physical_address))
	{
		for (index = 0; index < MAXIMUM_PLACED_BLOCKS && placed_blocks[index].address; index++)
			;
		if (index < MAXIMUM_PLACED_BLOCKS)
		{
			SceUID block = sceKernelAllocPartitionMemory(2, "halo placed", PSP_SMEM_Addr, size,
				(void *)physical_address);

			if (block >= 0 && (unsigned long)sceKernelGetBlockHeadAddr(block) == physical_address)
			{
				placed_blocks[index].address = physical_address;
				placed_blocks[index].block = block;
				result = (void *)physical_address;
			}
			else
			{
				platform_log("XPhysicalAlloc: can't take %lu KB at 0x%08lx (0x%08x); free: %d KB, largest %d KB",
					size / 1024, physical_address, (unsigned)block,
					sceKernelTotalFreeMemSize() / 1024, sceKernelMaxFreeMemSize() / 1024);
				if (block >= 0)
					sceKernelFreePartitionMemory(block);
			}
		}
	}
	else
	{
		result = memalign(alignment > 64 ? alignment : 64, size);
		if (!result)
			platform_log("XPhysicalAlloc: no %lu KB on the heap", size / 1024);
	}
	if (result)
	{
		memset(result, 0, size);
		set_protection((unsigned long)result, size, protect);
	}
	pthread_mutex_unlock(&memory_lock);
	return result;
}

void platform_contiguous_free(void *address)
{
	long index;

	if (!address)
		return;
	/* the null renderer's render targets live in VRAM (psp_fixups.py) */
	if (((unsigned long)address & 0x1FFFFFFF) >= 0x04000000 && ((unsigned long)address & 0x1FFFFFFF) < 0x04200000)
		return;
	pthread_mutex_lock(&memory_lock);
	for (index = 0; index < MAXIMUM_PLACED_BLOCKS; index++)
	{
		if (placed_blocks[index].address == (unsigned long)address)
		{
			sceKernelFreePartitionMemory(placed_blocks[index].block);
			placed_blocks[index].address = 0;
			pthread_mutex_unlock(&memory_lock);
			return;
		}
	}
	free(address);
	pthread_mutex_unlock(&memory_lock);
}

/* ---------- XAPI */

LPVOID WINAPI XPhysicalAlloc(SIZE_T size, ULONG_PTR physical_address, ULONG_PTR alignment, DWORD protect)
{
	void *result = platform_contiguous_alloc(size, alignment, physical_address, protect);

	if (!result)
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
	return result;
}

VOID WINAPI XPhysicalFree(LPVOID address)
{
	platform_contiguous_free(address);
}

BOOL WINAPI VirtualProtect(LPVOID address, SIZE_T size, DWORD new_protect, PDWORD old_protect)
{
	unsigned long value = (unsigned long)address;

	if (old_protect)
		*old_protect = in_user_ram(value) && page_protection[(value - USER_RAM_BASE) / PAGE_SIZE_BYTES] ?
			page_protection[(value - USER_RAM_BASE) / PAGE_SIZE_BYTES] : PAGE_READWRITE;
	set_protection(value, size, new_protect & ~(PAGE_WRITECOMBINE | PAGE_NOCACHE));
	return TRUE;
}

VOID WINAPI XPhysicalProtect(LPVOID address, SIZE_T size, DWORD new_protect)
{
	VirtualProtect(address, size, new_protect, NULL);
}

DWORD WINAPI XQueryMemoryProtect(LPVOID address)
{
	unsigned long value = (unsigned long)address;
	DWORD protect = PAGE_READWRITE;

	if (in_user_ram(value) && page_protection[(value - USER_RAM_BASE) / PAGE_SIZE_BYTES])
		protect = page_protection[(value - USER_RAM_BASE) / PAGE_SIZE_BYTES];
	return protect;
}

/* ---------- memory_watch.c's interface: nothing to watch without page
protection, and the null renderer caches nothing */

void memory_watch_initialize(void) {}
void memory_watch_protect(unsigned long address, unsigned long size) { (void)address; (void)size; }
unsigned long memory_watch_generation(unsigned long address, unsigned long size) { (void)address; (void)size; return 0; }
unsigned long memory_watch_serial(void) { return 0; }
void memory_watch_prepare_write(void *address, unsigned long size) { (void)address; (void)size; }
void memory_watch_forget(void *address, unsigned long size) { (void)address; (void)size; }
