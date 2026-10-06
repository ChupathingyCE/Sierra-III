	.set noreorder

#include "pspimport.s"

// Build files
// halo_profile_0000.o halo_profile_0001.o 

#ifdef F_halo_profile_0000
	IMPORT_START "halo_profile",0x40090000
#endif
#ifdef F_halo_profile_0001
	IMPORT_FUNC  "halo_profile",0xD98487B8,halo_profile_phase
#endif
