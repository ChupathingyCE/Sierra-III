/*
PSP_VFPU.C (build 37)

The VFPU: the main CPU's vector unit, idle until now. A 4x4 block of its
registers is a matrix (M000 to M700), a column C (S, then column, then row:
S012 is matrix 0, column 1, row 2), and one instruction multiplies two of
them. Its results can differ from the FPU's in the last bit (it sums in its
own order), so it starts with the game's own matrix product and checks itself
against the FPU at start-up: a large difference (a misread of its matrix
layout, say) turns it off and says so in the log. keep.txt's "novfpu" turns
it off, for comparing frame times.

Every thread that might run it must be made with the VFPU attribute (the
kernel saves its registers only for those, and refuses the instructions
elsewhere): sceKernelCreateThread is wrapped to add it (psp_build.py links
with --wrap).
*/

#include <pspkernel.h>
#include <pspthreadman.h>
#include <string.h>
#include <math.h>

void platform_log(const char *format, ...);
int psp_keep(const char *name);

/* the game's real_matrix4x3: a scale, then forward, left, up and position */
struct matrix43
{
	float scale;
	float n[4][3];
};

int psp_vfpu_on = 0;
unsigned long psp_vfpu_multiplies = 0;

/* every thread with the VFPU (the game's own threads are made through
pthreads, which doesn't ask for it) */
SceUID __real_sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int priority, int stack,
	SceUInt attributes, SceKernelThreadOptParam *options);

SceUID __wrap_sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int priority, int stack,
	SceUInt attributes, SceKernelThreadOptParam *options)
{
	return __real_sceKernelCreateThread(name, entry, priority, stack, attributes | PSP_THREAD_ATTR_VFPU, options);
}

/* the game's matrix4x3_multiply (matrix_math.c), the FPU's way: each row of b
combines the rows of a; b's position is then scaled by a's scale and moved
by a's position */
static void fpu_multiply(const struct matrix43 *a, const struct matrix43 *b, struct matrix43 *result)
{
	float product[4][3];
	int row, column;

	for (row = 0; row < 4; row++)
		for (column = 0; column < 3; column++)
		{
			float value = b->n[row][0] * a->n[0][column] + b->n[row][1] * a->n[1][column] +
				b->n[row][2] * a->n[2][column];

			if (row == 3)
				value = value * a->scale + a->n[3][column];
			product[row][column] = value;
		}
	memcpy(result->n, product, sizeof(product));
	result->scale = a->scale * b->scale;
}

/* the same on the VFPU. a's rows go in as the columns of M000, so M000
times a column v is v[0] a.forward + v[1] a.left + v[2] a.up; b's four rows
go in as the columns of M100, and M000 x M100 gives the product's rows as
the columns of M200. Everything is read before anything is written, so the
result may be a or b */
static void vfpu_multiply(const struct matrix43 *a, const struct matrix43 *b, struct matrix43 *result)
{
	float scale = a->scale * b->scale;

	__asm__ volatile(
		"lv.s S000, 4(%0)\n"  "lv.s S001, 8(%0)\n"  "lv.s S002, 12(%0)\n"
		"lv.s S010, 16(%0)\n" "lv.s S011, 20(%0)\n" "lv.s S012, 24(%0)\n"
		"lv.s S020, 28(%0)\n" "lv.s S021, 32(%0)\n" "lv.s S022, 36(%0)\n"
		"lv.s S030, 40(%0)\n" "lv.s S031, 44(%0)\n" "lv.s S032, 48(%0)\n"
		"lv.s S003, 0(%0)\n"
		"lv.s S100, 4(%1)\n"  "lv.s S101, 8(%1)\n"  "lv.s S102, 12(%1)\n"
		"lv.s S110, 16(%1)\n" "lv.s S111, 20(%1)\n" "lv.s S112, 24(%1)\n"
		"lv.s S120, 28(%1)\n" "lv.s S121, 32(%1)\n" "lv.s S122, 36(%1)\n"
		"lv.s S130, 40(%1)\n" "lv.s S131, 44(%1)\n" "lv.s S132, 48(%1)\n"
		/* the three direction rows */
		"vmmul.t M200, M000, M100\n"
		/* the position: a's rotation of b's, times a's scale, plus a's */
		"vtfm3.t C300, E000, C130\n"
		"vscl.t C300, C300, S003\n"
		"vadd.t C300, C300, C030\n"
		"sv.s S200, 4(%2)\n"  "sv.s S201, 8(%2)\n"  "sv.s S202, 12(%2)\n"
		"sv.s S210, 16(%2)\n" "sv.s S211, 20(%2)\n" "sv.s S212, 24(%2)\n"
		"sv.s S220, 28(%2)\n" "sv.s S221, 32(%2)\n" "sv.s S222, 36(%2)\n"
		"sv.s S300, 40(%2)\n" "sv.s S301, 44(%2)\n" "sv.s S302, 48(%2)\n"
		: : "r"(a), "r"(b), "r"(result) : "memory");
	result->scale = scale;
}

/* matrix_math.c's matrix4x3_multiply calls this when psp_vfpu_on */
void psp_vfpu_matrix4x3_multiply(const void *a, const void *b, void *result)
{
	vfpu_multiply((const struct matrix43 *)a, (const struct matrix43 *)b, (struct matrix43 *)result);
	psp_vfpu_multiplies++;
}

static unsigned int seed = 12345;
volatile float sink;

static float random_value(float range)
{
	seed = seed * 1103515245u + 12345u;
	return ((float)((seed >> 8) & 0xFFFF) / 32768.0f - 1.0f) * range;
}

/* layout probe (keep.txt "vfputest"): known matrices through vmmul and vtfm */
static void vfpu_probe(void)
{
	/* A columns: (1 2 3), (4 5 6), (7 8 9); B columns: (1 0 0)... diag(1,10,100) */
	static float a[9] __attribute__((aligned(16))) = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	static float b[9] __attribute__((aligned(16))) = { 1, 0, 0, 0, 10, 0, 0, 0, 100 };
	static float v[3] __attribute__((aligned(16))) = { 1, 10, 100 };
	static float out[12] __attribute__((aligned(16)));

	__asm__ volatile(
		"lv.s S000, 0(%0)\n" "lv.s S001, 4(%0)\n" "lv.s S002, 8(%0)\n"
		"lv.s S010, 12(%0)\n" "lv.s S011, 16(%0)\n" "lv.s S012, 20(%0)\n"
		"lv.s S020, 24(%0)\n" "lv.s S021, 28(%0)\n" "lv.s S022, 32(%0)\n"
		"lv.s S100, 0(%1)\n" "lv.s S101, 4(%1)\n" "lv.s S102, 8(%1)\n"
		"lv.s S110, 12(%1)\n" "lv.s S111, 16(%1)\n" "lv.s S112, 20(%1)\n"
		"lv.s S120, 24(%1)\n" "lv.s S121, 28(%1)\n" "lv.s S122, 32(%1)\n"
		"lv.s S300, 0(%2)\n" "lv.s S301, 4(%2)\n" "lv.s S302, 8(%2)\n"
		"vmmul.t M200, M000, M100\n"
		"vtfm3.t C310, M000, C300\n"
		"sv.s S200, 0(%3)\n" "sv.s S201, 4(%3)\n" "sv.s S202, 8(%3)\n"
		"sv.s S210, 12(%3)\n" "sv.s S211, 16(%3)\n" "sv.s S212, 20(%3)\n"
		"sv.s S220, 24(%3)\n" "sv.s S221, 28(%3)\n" "sv.s S222, 32(%3)\n"
		"sv.s S310, 36(%3)\n" "sv.s S311, 40(%3)\n" "sv.s S312, 44(%3)\n"
		: : "r"(a), "r"(b), "r"(v), "r"(out) : "memory");
	platform_log("VFPU probe: vmmul columns (%g %g %g) (%g %g %g) (%g %g %g); vtfm (%g %g %g)",
		out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7], out[8], out[9], out[10], out[11]);
}

/* start-up: 2000 random products both ways; the VFPU stays on if they agree
to within a few last bits relative to the values' size */
void psp_vfpu_initialize(void)
{
	struct matrix43 a, b, fpu, vfpu;
	float worst = 0.0f;
	int trial, k, row, column, aliased_ok = 1;
	unsigned int start, fpu_us, vfpu_us;

	for (trial = 0; trial < 2000; trial++)
	{
		a.scale = trial % 4 ? 1.0f : 0.5f + (random_value(1.0f) + 1.0f);
		b.scale = trial % 3 ? 1.0f : 0.5f + (random_value(1.0f) + 1.0f);
		for (k = 0; k < 9; k++)
		{
			(&a.n[0][0])[k] = random_value(1.0f);
			(&b.n[0][0])[k] = random_value(1.0f);
		}
		for (k = 9; k < 12; k++)
		{
			(&a.n[0][0])[k] = random_value(200.0f);
			(&b.n[0][0])[k] = random_value(5.0f);
		}
		fpu_multiply(&a, &b, &fpu);
		vfpu_multiply(&a, &b, &vfpu);
		for (row = 0; row < 4; row++)
			for (column = 0; column < 3; column++)
			{
				float size = fabsf(fpu.n[row][column]) + (row == 3 ? 1.0f : 0.01f);
				float difference = fabsf(fpu.n[row][column] - vfpu.n[row][column]) / size;

				if (!(difference <= worst))
					worst = difference;
			}
		if (fpu.scale != vfpu.scale)
			worst = 1.0f;
		if (trial == 0)
		{
			/* in place, as the game sometimes calls it */
			struct matrix43 copy = a;

			vfpu_multiply(&copy, &b, &copy);
			aliased_ok = memcmp(&copy, &vfpu, sizeof(copy)) == 0;
		}
	}
	if (psp_keep("vfputest"))
		vfpu_probe();
	/* and the time each takes, for the log */
	/* chained (each product feeds the next, as a skeleton's do), so the
	compiler can't drop the loop */
	start = sceKernelGetSystemTimeLow();
	for (trial = 0; trial < 2000; trial++)
		fpu_multiply(&a, &b, &b);
	fpu_us = sceKernelGetSystemTimeLow() - start;
	start = sceKernelGetSystemTimeLow();
	for (trial = 0; trial < 2000; trial++)
		vfpu_multiply(&a, &fpu, &fpu);
	vfpu_us = sceKernelGetSystemTimeLow() - start;
	sink = b.n[3][0] + fpu.n[3][0];
	psp_vfpu_on = !psp_keep("novfpu") && worst < 1e-5f && aliased_ok;
	platform_log("VFPU: matrix product checked against the FPU, worst relative difference %g%s; 2000 products %u us (FPU %u us); %s",
		(double)worst, aliased_ok ? "" : ", WRONG in place", vfpu_us, fpu_us,
		psp_vfpu_on ? "on" : psp_keep("novfpu") ? "off (novfpu)" : "OFF: it disagrees with the FPU");
}
