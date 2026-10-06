/* PSP bring-up: module information for the linked game */
#include <pspkernel.h>
PSP_MODULE_INFO("halo", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024); /* all user memory but 1 MB */
