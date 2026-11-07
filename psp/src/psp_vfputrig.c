/*
PSP_VFPUTRIG.C

Sine and cosine on the VFPU for the work that only draws or sounds (lever
E17; on by default, "vfputrigoff" for the libm): the sprites' rotation, the
shaders' texture animation, the HUD's rotated bitmaps, local-only effect
cones and the mixer's panning. The VFPU's vsin/vcos take quarter turns and
agree with the float libm to a few units in the last place, which no picture
or panning shows; the simulation's callers stay on the libm, where the server
and the seeds need the same bits (CLAUDE.md, the multiplayer boundary).

Only matrix 6 of the VFPU's registers is used: the port's other VFPU code
keeps state in matrices 0 to 3 between its own calls (psp_ge.c's batch
matrix), and nothing uses 4 to 7. The Media Engine has no VFPU, so a call
made there takes the libm.
*/

#include <math.h>
#include "psp_me.h" /* psp_cpu_is_me */

int psp_keep(const char *name);
int psp_keep_loaded(void);

static int vfputrig = -1;

int psp_vfputrig_on(void)
{
	if (vfputrig < 0)
	{
		if (!psp_keep_loaded())
			return 0;
		/* on by default: drawing -3% and the frame -1.3% on b30's flight
		(PSP-1000 42356299435), the pictures the same */
		vfputrig = psp_keep("vfputrigoff") ? 0 : 1;
	}
	return vfputrig > 0 && !psp_cpu_is_me();
}

/* sine and cosine of an angle in radians, together (vrot.p's [s, c]) */
void psp_vfpu_sincosf(float angle, float *sine, float *cosine)
{
	float s, c;

	if (!psp_vfputrig_on())
	{
		*sine = sinf(angle);
		*cosine = cosf(angle);
		return;
	}
	__asm__ volatile (
		"mtv      %2, S600\n"
		"vcst.s   S601, VFPU_2_PI\n"
		"vmul.s   S600, S600, S601\n"
		"vrot.p   C610, S600, [s, c]\n"
		"mfv      %0, S610\n"
		"mfv      %1, S611\n"
		: "=r"(s), "=r"(c) : "r"(angle));
	*sine = s;
	*cosine = c;
}
