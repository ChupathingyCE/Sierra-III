TARGET = halo_profile
OBJS = main.o
CFLAGS = -O2 -G0 -Wall
# PSP_KSAMPLE=1 (recon only): the kernel's PCs binned (ksample.h); the build's
# environment reaches make, so the switch that builds the EBOOT builds this too
ifeq ($(PSP_KSAMPLE),1)
CFLAGS += -DPSP_KSAMPLE=1
endif
BUILD_PRX = 1
USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1
LIBS =
PRX_EXPORTS = exports.exp
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
