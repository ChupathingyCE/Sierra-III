	.set noreorder

#include "pspimport.s"

// Build files
// halo_claim_0000.o halo_claim_0001.o halo_claim_0002.o halo_claim_0003.o 

#ifdef F_halo_claim_0000
	IMPORT_START "halo_claim",0x40090000
#endif
#ifdef F_halo_claim_0001
	IMPORT_FUNC  "halo_claim",0x511EB36A,halo_claim_result
#endif
#ifdef F_halo_claim_0002
	IMPORT_FUNC  "halo_claim",0x28CFF6E5,halo_claim_release
#endif
#ifdef F_halo_claim_0003
	IMPORT_FUNC  "halo_claim",0x84206478,halo_claim_probe
#endif
