/* psp_vfpu_quat.c - the rest of the posing maths on the VFPU

quaternion_normalize, quaternions_interpolate_and_normalize and
matrix4x3_rotation_from_quaternion, the steps of posing after the quaternion
product (psp_vfpu.c, keep word vfpupose). The same pattern as that product:

- the element-wise forms ("E") keep every lane's operations in the engine's
  order, and the VFPU's add, multiply and divide round as the FPU's do, so
  their bits are the FPU's. The sums of four squares stay sequential (one
  vadd.s a term), and the square root and the divide stay on the FPU (vsqrt.s
  is a last bit off); only the products, the blends and the matrix's sums
  pack into vector instructions;
- the VFPU's own forms ("D": vdot.q for a sum, vrsq.s for 1/sqrt) are shorter
  but round their own way (within 12 last bits on silicon).

keep.txt "vfpuquatbench": both kinds against the engine's own functions at
start-up, how many differ and by how much, and their time in a chain.

In play (off by default, the FPU's path the other side): "vfpuquat" the
exact interpolation, "vfpuquatd" the D interpolation and normalize; the
section "in play" below says why and how each is checked.

The Media Engine has no VFPU, and its pose job reaches quaternion_normalize
(animation_get_node_orientations): each entry answers 0 when the caller's
stack isn't in main RAM, and the engine's FPU code runs instead.

The engine's quaternions and matrices are only 4-byte aligned, so every load
and store is one float (lv.s, sv.s). */
#include <pspkernel.h>
#include <string.h>

void platform_log(const char *format, ...);
int psp_keep(const char *name);
unsigned long psp_ge_frame_number(void);

extern int psp_vfpu_on;

/* the engine's own (FPU) functions, the reference */
void quaternion_normalize(void *q);
void quaternions_interpolate_and_normalize(const void *q0, const void *q1, float t, void *result);
void matrix4x3_rotation_from_quaternion(void *matrix, const void *quaternion);

struct quat { float i, j, k, w; };

/* real_matrix4x3: a scale, then forward, left, up and position */
struct matrix43
{
	float scale;
	float n[4][3];
};

union bits
{
	float f;
	unsigned int u;
};

/* ---------- the FPU's way, copied from the engine (real_math.c) for the
check word: the start-up check confirms each matches the engine's own
function bit for bit */

static void fpu_normalize(struct quat *q)
{
	float magnitude_squared = q->i * q->i + q->j * q->j + q->k * q->k + q->w * q->w;

	if (magnitude_squared > 0.0f)
	{
		float one_over_magnitude = 1.0f / __builtin_sqrtf(magnitude_squared);

		q->i = one_over_magnitude * q->i;
		q->j = one_over_magnitude * q->j;
		q->k = one_over_magnitude * q->k;
		q->w = one_over_magnitude * q->w;
	}
	else
	{
		q->i = q->j = q->k = 0.0f;
		q->w = 1.0f;
	}
}

static void fpu_interpolate(const struct quat *q0, const struct quat *q1, float t, struct quat *result)
{
	float v = 1.0f - t;
	struct quat blend;

	if (q0->i * q1->i + q0->j * q1->j + q0->k * q1->k + q0->w * q1->w < 0.0f)
		t = -t;
	blend.i = q0->i * v + q1->i * t;
	blend.j = q0->j * v + q1->j * t;
	blend.k = q0->k * v + q1->k * t;
	blend.w = q0->w * v + q1->w * t;
	*result = blend;
	fpu_normalize(result);
}

/* ---------- the VFPU's forms. The VFPU's registers keep their values from
one asm statement to the next (the compiler never uses them), so a form may
hand a value to the FPU for its square root or divide and pick up where it
left off. */

/* the sum of squares in the engine's order, ((ii + jj) + kk) + ww; or with
vdot.q (its own order) when dot_form */
static unsigned int vfpu_load_magnitude(const struct quat *q, int dot_form)
{
	unsigned int out;

	if (dot_form)
		__asm__ volatile(
			"lv.s S000, 0(%1)\n" "lv.s S001, 4(%1)\n" "lv.s S002, 8(%1)\n" "lv.s S003, 12(%1)\n"
			"vdot.q S020, C000, C000\n"
			"mfv %0, S020\n"
			: "=r"(out) : "r"(q) : "memory");
	else
		__asm__ volatile(
			"lv.s S000, 0(%1)\n" "lv.s S001, 4(%1)\n" "lv.s S002, 8(%1)\n" "lv.s S003, 12(%1)\n"
			"vmul.q C010, C000, C000\n"
			"vadd.s S020, S010, S011\n"
			"vadd.s S020, S020, S012\n"
			"vadd.s S020, S020, S013\n"
			"mfv %0, S020\n"
			: "=r"(out) : "r"(q) : "memory");
	return out;
}

/* E: the squares packed, the sum sequential, sqrt and divide on the FPU, the
scaling packed (o * q.i is q.i * o: IEEE's product commutes) */
static void __attribute__((noinline)) vfpu_normalize_e(struct quat *q)
{
	union bits magnitude_squared, one_over;

	magnitude_squared.u = vfpu_load_magnitude(q, 0);
	if (magnitude_squared.f > 0.0f)
	{
		one_over.f = 1.0f / __builtin_sqrtf(magnitude_squared.f);
		__asm__ volatile(
			"mtv %1, S030\n"
			"vscl.q C000, C000, S030\n"
			"sv.s S000, 0(%0)\n" "sv.s S001, 4(%0)\n" "sv.s S002, 8(%0)\n" "sv.s S003, 12(%0)\n"
			: : "r"(q), "r"(one_over.u) : "memory");
	}
	else
	{
		q->i = q->j = q->k = 0.0f;
		q->w = 1.0f;
	}
}

/* D: vdot.q and vrsq.s */
static void __attribute__((noinline)) vfpu_normalize_d(struct quat *q)
{
	union bits magnitude_squared;

	magnitude_squared.u = vfpu_load_magnitude(q, 1);
	if (magnitude_squared.f > 0.0f && magnitude_squared.f < 1e-30f)
	{
		/* the VFPU flushes denormals: a length this small takes the FPU's root */
		union bits one_over;

		one_over.f = 1.0f / __builtin_sqrtf(magnitude_squared.f);
		__asm__ volatile(
			"mtv %1, S021\n"
			"vscl.q C000, C000, S021\n"
			"sv.s S000, 0(%0)\n" "sv.s S001, 4(%0)\n" "sv.s S002, 8(%0)\n" "sv.s S003, 12(%0)\n"
			: : "r"(q), "r"(one_over.u) : "memory");
	}
	else if (magnitude_squared.f > 0.0f)
		__asm__ volatile(
			"vrsq.s S021, S020\n"
			"vscl.q C000, C000, S021\n"
			"sv.s S000, 0(%0)\n" "sv.s S001, 4(%0)\n" "sv.s S002, 8(%0)\n" "sv.s S003, 12(%0)\n"
			: : "r"(q) : "memory");
	else
	{
		q->i = q->j = q->k = 0.0f;
		q->w = 1.0f;
	}
}

/* interpolate and normalize. form 0 (E): the hemisphere's dot and the
length sequential, the blend packed (each lane q0 * v + q1 * t, as the
engine's), sqrt and divide on the FPU. form 1 (D): vdot.q for both sums and
vrsq.s. form 2: the hemisphere's dot sequential (its sign picks the blend, so
a near-zero dot of the wrong sign would turn a node the long way), the length
by vdot.q and vrsq.s. Everything is read before anything is written, so
result may be q0 or q1 */
static void __attribute__((noinline)) vfpu_interpolate(const struct quat *q0, const struct quat *q1, float t,
	struct quat *result, int form)
{
	union bits dot, v, w, magnitude_squared, one_over;

	v.f = 1.0f - t;
	if (form == 1)
		__asm__ volatile(
			"lv.s S000, 0(%1)\n" "lv.s S001, 4(%1)\n" "lv.s S002, 8(%1)\n" "lv.s S003, 12(%1)\n"
			"lv.s S010, 0(%2)\n" "lv.s S011, 4(%2)\n" "lv.s S012, 8(%2)\n" "lv.s S013, 12(%2)\n"
			"vdot.q S030, C000, C010\n"
			"mfv %0, S030\n"
			: "=r"(dot.u) : "r"(q0), "r"(q1) : "memory");
	else
		__asm__ volatile(
			"lv.s S000, 0(%1)\n" "lv.s S001, 4(%1)\n" "lv.s S002, 8(%1)\n" "lv.s S003, 12(%1)\n"
			"lv.s S010, 0(%2)\n" "lv.s S011, 4(%2)\n" "lv.s S012, 8(%2)\n" "lv.s S013, 12(%2)\n"
			"vmul.q C020, C000, C010\n"
			"vadd.s S030, S020, S021\n"
			"vadd.s S030, S030, S022\n"
			"vadd.s S030, S030, S023\n"
			"mfv %0, S030\n"
			: "=r"(dot.u) : "r"(q0), "r"(q1) : "memory");
	w.f = dot.f < 0.0f ? -t : t;
	if (form == 0)
		__asm__ volatile(
			"mtv %1, S031\n" "mtv %2, S032\n"
			"vscl.q C100, C000, S031\n"
			"vscl.q C110, C010, S032\n"
			"vadd.q C120, C100, C110\n"
			"vmul.q C130, C120, C120\n"
			"vadd.s S200, S130, S131\n"
			"vadd.s S200, S200, S132\n"
			"vadd.s S200, S200, S133\n"
			"mfv %0, S200\n"
			: "=r"(magnitude_squared.u) : "r"(v.u), "r"(w.u));
	else
		__asm__ volatile(
			"mtv %1, S031\n" "mtv %2, S032\n"
			"vscl.q C100, C000, S031\n"
			"vscl.q C110, C010, S032\n"
			"vadd.q C120, C100, C110\n"
			"vdot.q S200, C120, C120\n"
			"mfv %0, S200\n"
			: "=r"(magnitude_squared.u) : "r"(v.u), "r"(w.u));
	if (!(magnitude_squared.f > 0.0f))
	{
		result->i = result->j = result->k = 0.0f;
		result->w = 1.0f;
	}
	else if (form == 0 || magnitude_squared.f < 1e-30f)
	{
		/* the exact form, or a length so small the VFPU would flush it */
		one_over.f = 1.0f / __builtin_sqrtf(magnitude_squared.f);
		__asm__ volatile(
			"mtv %1, S201\n"
			"vscl.q C120, C120, S201\n"
			"sv.s S120, 0(%0)\n" "sv.s S121, 4(%0)\n" "sv.s S122, 8(%0)\n" "sv.s S123, 12(%0)\n"
			: : "r"(result), "r"(one_over.u) : "memory");
	}
	else
		__asm__ volatile(
			"vrsq.s S201, S200\n"
			"vscl.q C120, C120, S201\n"
			"sv.s S120, 0(%0)\n" "sv.s S121, 4(%0)\n" "sv.s S122, 8(%0)\n" "sv.s S123, 12(%0)\n"
			: : "r"(result) : "memory");
}

/* the rotation matrix. The scale's sum sequential (or vdot.q when
dot_form), 2 / scale on the FPU, then the products packed with swizzles:
C100 = (x, y, z) = scale (i, j, k); C110 = (xy, xz, yz); C120 = (wz, wy, wx);
C130 = (xx, yy, zz). The six off-diagonal entries are C110 + C120 and
C110 - C120 lane by lane, the diagonal 1 - ((yy + zz), (xx + zz), (xx + yy)),
each the engine's own operation on the engine's own operands */
static void __attribute__((noinline)) vfpu_rotation(struct matrix43 *matrix, const struct quat *q, int dot_form)
{
	union bits scale, one;

	scale.u = vfpu_load_magnitude(q, dot_form);
	if (scale.f != 0.0f)
		scale.f = 2.0f / scale.f;
	else
		scale.f = 0.0f;
	one.f = 1.0f;
	__asm__ volatile(
		"mtv %0, S030\n" "mtv %1, S230\n"
		"vscl.t C100, C000, S030\n"
		"vmul.t C110, C000[x,x,y], C100[y,z,z]\n"
		/* .q: a triple can't swizzle in lane w; the fourth lane is unused */
		"vmul.q C120, C000[w,w,w,w], C100[z,y,x,x]\n"
		"vmul.t C130, C000, C100\n"
		"vadd.t C200, C130[y,x,x], C130[z,z,y]\n"
		"vadd.t C210, C110, C120\n"
		"vsub.t C220, C110, C120\n"
		"vsub.s S300, S230, S200\n" "vsub.s S301, S230, S201\n" "vsub.s S302, S230, S202\n"
		"sv.s S300, 4(%2)\n" "sv.s S220, 8(%2)\n" "sv.s S211, 12(%2)\n"
		"sv.s S210, 16(%2)\n" "sv.s S301, 20(%2)\n" "sv.s S222, 24(%2)\n"
		"sv.s S221, 28(%2)\n" "sv.s S212, 32(%2)\n" "sv.s S302, 36(%2)\n"
		: : "r"(scale.u), "r"(one.u), "r"(matrix) : "memory");
	matrix->scale = 1.0f;
	matrix->n[3][0] = matrix->n[3][1] = matrix->n[3][2] = 0.0f;
}

/* ---------- random inputs and last-bit distances */

static unsigned int seed = 24680;
volatile float psp_vfpu_quat_sink;

static float random_value(float range)
{
	seed = seed * 1103515245u + 12345u;
	return ((float)((seed >> 8) & 0xFFFF) / 32768.0f - 1.0f) * range;
}

/* a quaternion of length near 1 (normalized: within a few last bits, as
the keyframes' decompressed ones), or anything from 1e-3 to 1e3 long */
static void random_quat(struct quat *q, int normalized)
{
	float scale = 1.0f;

	q->i = random_value(1.0f);
	q->j = random_value(1.0f);
	q->k = random_value(1.0f);
	q->w = random_value(1.0f);
	if (normalized)
	{
		fpu_normalize(q);
		return;
	}
	switch ((seed >> 4) % 7)
	{
	case 0: scale = 1e-3f; break;
	case 1: scale = 1e-1f; break;
	case 2: scale = 10.0f; break;
	case 3: scale = 1e3f; break;
	default: break;
	}
	q->i *= scale;
	q->j *= scale;
	q->k *= scale;
	q->w *= scale;
	/* one lane in 16 exactly zero, as default rotations have */
	if (((seed >> 12) & 15) == 0)
		q->k = 0.0f;
}

static unsigned int ulps_apart(float a, float b)
{
	int x, y;

	memcpy(&x, &a, sizeof(x));
	memcpy(&y, &b, sizeof(y));
	if ((x < 0) != (y < 0))
		return (x & 0x7fffffff) == 0 && (y & 0x7fffffff) == 0 ? 0 : 0x7fffffffu;
	return x > y ? (unsigned int)(x - y) : (unsigned int)(y - x);
}

struct tally
{
	int differ;
	unsigned int worst;
};

static void tally_floats(struct tally *tally, const float *a, const float *b, int count)
{
	int k, differs = 0;

	for (k = 0; k < count; k++)
	{
		unsigned int apart = ulps_apart(a[k], b[k]);

		if (apart)
			differs = 1;
		if (apart > tally->worst)
			tally->worst = apart;
	}
	tally->differ += differs;
}

/* ---------- the bench (keep.txt "vfpuquatbench") */

#define BENCH_TRIALS 20000
#define BENCH_TABLE 16

static void vfpu_quat_bench(void)
{
	static struct quat table[BENCH_TABLE];
	struct tally normalize_e = { 0, 0 }, normalize_d = { 0, 0 };
	struct tally interpolate_e = { 0, 0 }, interpolate_d = { 0, 0 }, interpolate_d2 = { 0, 0 };
	struct tally rotation_e = { 0, 0 }, rotation_d = { 0, 0 };
	struct quat q0, q1, fpu, vfpu, chain;
	struct matrix43 matrix_fpu, matrix_vfpu;
	unsigned int start, us[10];
	int trial, k;

	for (trial = 0; trial < BENCH_TABLE; trial++)
		random_quat(&table[trial], 1);
	for (trial = 0; trial < BENCH_TRIALS; trial++)
	{
		float t = (random_value(1.0f) + 1.0f) * 0.5f;

		random_quat(&q0, 0);
		fpu = q0;
		quaternion_normalize(&fpu);
		vfpu = q0;
		vfpu_normalize_e(&vfpu);
		tally_floats(&normalize_e, &fpu.i, &vfpu.i, 4);
		vfpu = q0;
		vfpu_normalize_d(&vfpu);
		tally_floats(&normalize_d, &fpu.i, &vfpu.i, 4);

		random_quat(&q0, 1);
		random_quat(&q1, trial & 1);
		quaternions_interpolate_and_normalize(&q0, &q1, t, &fpu);
		vfpu_interpolate(&q0, &q1, t, &vfpu, 0);
		tally_floats(&interpolate_e, &fpu.i, &vfpu.i, 4);
		vfpu_interpolate(&q0, &q1, t, &vfpu, 1);
		tally_floats(&interpolate_d, &fpu.i, &vfpu.i, 4);
		vfpu_interpolate(&q0, &q1, t, &vfpu, 2);
		tally_floats(&interpolate_d2, &fpu.i, &vfpu.i, 4);

		matrix4x3_rotation_from_quaternion(&matrix_fpu, &q1);
		vfpu_rotation(&matrix_vfpu, &q1, 0);
		tally_floats(&rotation_e, &matrix_fpu.scale, &matrix_vfpu.scale, 13);
		vfpu_rotation(&matrix_vfpu, &q1, 1);
		tally_floats(&rotation_d, &matrix_fpu.scale, &matrix_vfpu.scale, 13);
	}

	/* chained: each call's input takes a lane of the last one's output (as a
	node chain's results feed the next), so the compiler can't drop or
	overlap the calls; every form pays the same copy */
#define NORMALIZE_CHAIN(call) \
	chain = table[0]; \
	start = sceKernelGetSystemTimeLow(); \
	for (trial = 0; trial < BENCH_TRIALS; trial++) \
	{ \
		struct quat q = table[trial & (BENCH_TABLE - 1)]; \
		q.i = chain.j; \
		call(&q); \
		chain = q; \
	}
	NORMALIZE_CHAIN(quaternion_normalize)
	us[0] = sceKernelGetSystemTimeLow() - start;
	psp_vfpu_quat_sink = chain.w;
	NORMALIZE_CHAIN(vfpu_normalize_e)
	us[1] = sceKernelGetSystemTimeLow() - start;
	psp_vfpu_quat_sink = chain.w;
	NORMALIZE_CHAIN(vfpu_normalize_d)
	us[2] = sceKernelGetSystemTimeLow() - start;
	psp_vfpu_quat_sink = chain.w;

	chain = table[0];
	start = sceKernelGetSystemTimeLow();
	for (trial = 0; trial < BENCH_TRIALS; trial++)
		quaternions_interpolate_and_normalize(&table[trial & (BENCH_TABLE - 1)], &chain, 0.3f, &chain);
	us[3] = sceKernelGetSystemTimeLow() - start;
	psp_vfpu_quat_sink = chain.w;
	for (k = 0; k < 3; k++)
	{
		chain = table[0];
		start = sceKernelGetSystemTimeLow();
		for (trial = 0; trial < BENCH_TRIALS; trial++)
			vfpu_interpolate(&table[trial & (BENCH_TABLE - 1)], &chain, 0.3f, &chain, k);
		us[4 + k] = sceKernelGetSystemTimeLow() - start;
		psp_vfpu_quat_sink = chain.w;
	}

#define ROTATION_CHAIN(call) \
	matrix_fpu.n[1][2] = 0.0f; \
	start = sceKernelGetSystemTimeLow(); \
	for (trial = 0; trial < BENCH_TRIALS; trial++) \
	{ \
		struct quat q = table[trial & (BENCH_TABLE - 1)]; \
		q.i = matrix_fpu.n[1][2]; \
		call; \
	}
	ROTATION_CHAIN(matrix4x3_rotation_from_quaternion(&matrix_fpu, &q))
	us[7] = sceKernelGetSystemTimeLow() - start;
	psp_vfpu_quat_sink = matrix_fpu.n[2][2];
	ROTATION_CHAIN(vfpu_rotation(&matrix_fpu, &q, 0))
	us[8] = sceKernelGetSystemTimeLow() - start;
	psp_vfpu_quat_sink = matrix_fpu.n[2][2];
	ROTATION_CHAIN(vfpu_rotation(&matrix_fpu, &q, 1))
	us[9] = sceKernelGetSystemTimeLow() - start;
	psp_vfpu_quat_sink = matrix_fpu.n[2][2];

	platform_log("VFPU quat bench: normalize, E (squares packed, sum sequential, sqrt and divide on the FPU): differs from the FPU in %d of %d (worst %u ulp); D (vdot, vrsq): %d (worst %u ulp)",
		normalize_e.differ, BENCH_TRIALS, normalize_e.worst, normalize_d.differ, normalize_d.worst);
	platform_log("VFPU quat bench: interpolate and normalize, E: differs in %d of %d (worst %u ulp); D (vdot both, vrsq): %d (worst %u ulp); D2 (sequential sign dot, vdot length, vrsq): %d (worst %u ulp)",
		interpolate_e.differ, BENCH_TRIALS, interpolate_e.worst, interpolate_d.differ, interpolate_d.worst,
		interpolate_d2.differ, interpolate_d2.worst);
	platform_log("VFPU quat bench: rotation from quaternion, E (products packed, scale sum sequential, 2/scale on the FPU): differs in %d of %d (worst %u ulp); D (vdot scale): %d (worst %u ulp)",
		rotation_e.differ, BENCH_TRIALS, rotation_e.worst, rotation_d.differ, rotation_d.worst);
	platform_log("VFPU quat bench: %d chained calls, us: normalize FPU %u, E %u, D %u; interpolate and normalize FPU %u, E %u, D %u, D2 %u; rotation FPU %u, E %u, D %u",
		BENCH_TRIALS, us[0], us[1], us[2], us[3], us[4], us[5], us[6], us[7], us[8], us[9]);
}

/* ---------- in play (psp_fixups.py FIXES_VFPUQUAT)

"vfpuquat": the interpolation's E form, the FPU's bits (on silicon 929 ->
683 ns a call); the E normalize and rotation are slower than the FPU's, so
they stay off. "vfpuquatd" (wins over vfpuquat): the VFPU's own forms of the
interpolation and the normalize, vdot.q for the length and vrsq.s for its
reciprocal root (2.2x and 1.55x on silicon, within 12 last bits). The
interpolation's hemisphere dot stays sequential: its sign picks which way a
node turns, and a near-zero dot taken the other way would turn it the long
way round; only the length's rounding moves. Posing is local to the client
(hardware_utilization.md section 6), so the last bits may differ when the
side-by-sides show it plays and looks the same; statecrc can't be its proof.
"vfpuquatcheck": the FPU's way beside each call, the calls that differ
counted (and their worst last-bit distance); the exact form plays the FPU's
bits, the D forms their own. */

int psp_vfpuquat_mode = 0; /* 0 off, 1 exact interpolation, 2 the D forms */
static int vfpuquat_check = 0;
static unsigned long calls[2], differ[2], report_frame = 0;
static unsigned int worst[2];

/* the Media Engine's stack is in its eDRAM: only 0x08000000-0x0fffffff (main
RAM) is the main CPU's */
static int on_main_cpu(void)
{
	unsigned int stack;

	__asm__ volatile("move %0, $sp" : "=r"(stack));
	return (stack >> 27) == 1;
}

/* the main CPU's calls a frame (and with the check word, those that differed)
every 300 frames */
static void report(void)
{
	unsigned long frame = psp_ge_frame_number();

	if (frame - report_frame < 300)
		return;
	if (report_frame)
		platform_log("VFPU quat: calls a frame (%s): normalize %lu, interpolate %lu%s (differed %lu, %lu; worst %u, %u ulp)",
			psp_vfpuquat_mode == 2 ? "vfpuquatd" : "vfpuquat",
			calls[0] / (frame - report_frame), calls[1] / (frame - report_frame),
			vfpuquat_check ? "; vfpuquatcheck" : "", differ[0], differ[1], worst[0], worst[1]);
	report_frame = frame;
	memset(calls, 0, sizeof(calls));
	memset(differ, 0, sizeof(differ));
	memset(worst, 0, sizeof(worst));
}

static void check_tally(int which, const struct quat *fpu, const struct quat *vfpu)
{
	const float *a = &fpu->i, *b = &vfpu->i;
	int lane, differs = 0;

	for (lane = 0; lane < 4; lane++)
	{
		unsigned int apart = ulps_apart(a[lane], b[lane]);

		if (apart)
			differs = 1;
		if (apart > worst[which])
			worst[which] = apart;
	}
	differ[which] += differs;
}

/* quaternion_normalize: the D form only (mode 2) */
int psp_vfpu_quaternion_normalize(void *q)
{
	if (psp_vfpuquat_mode != 2 || !on_main_cpu())
		return 0;
	if (vfpuquat_check)
	{
		struct quat fpu = *(struct quat *)q;

		fpu_normalize(&fpu);
		vfpu_normalize_d((struct quat *)q);
		check_tally(0, &fpu, (const struct quat *)q);
	}
	else
		vfpu_normalize_d((struct quat *)q);
	if ((++calls[0] & 255) == 0)
		report();
	return 1;
}

int psp_vfpu_quaternions_interpolate_and_normalize(const void *q0, const void *q1, float t, void *result)
{
	int form = psp_vfpuquat_mode == 2 ? 2 : 0;

	if (!on_main_cpu())
		return 0;
	if (vfpuquat_check)
	{
		struct quat fpu, vfpu;

		fpu_interpolate((const struct quat *)q0, (const struct quat *)q1, t, &fpu);
		vfpu_interpolate((const struct quat *)q0, (const struct quat *)q1, t, &vfpu, form);
		check_tally(1, &fpu, &vfpu);
		/* the exact form plays the FPU's bits, so a difference can't change
		the game; the D forms play their own */
		*(struct quat *)result = form ? vfpu : fpu;
	}
	else
		vfpu_interpolate((const struct quat *)q0, (const struct quat *)q1, t, (struct quat *)result, form);
	if ((++calls[1] & 255) == 0)
		report();
	return 1;
}

/* start-up (psp_platform.c, after psp_vfpu_initialize): the bench if asked;
then 2000 random inputs (normalized and not; the blend in place both ways
too) through the forms the keep word asks for, against the engine's own
functions. The exact form goes on only if none differ; the D forms only if
none is further than 64 last bits from the engine's (the bench: 12) and in
place gives the same bits as apart */
void psp_vfpu_quat_initialize(void)
{
	struct quat q0, q1, fpu, vfpu, copy;
	int trial, mode, form, differ_interpolate = 0, differ_normalize = 0, aliased_wrong = 0, differ_copy = 0;
	struct tally tally_interpolate = { 0, 0 }, tally_normalize = { 0, 0 };

	if (!psp_vfpu_on)
		return;
	if (psp_keep("vfpuquatbench"))
		vfpu_quat_bench();
	mode = psp_keep("vfpuquatd") ? 2 : psp_keep("vfpuquat") ? 1 : 0;
	if (!mode)
		return;
	form = mode == 2 ? 2 : 0;
	for (trial = 0; trial < 2000; trial++)
	{
		float t = (random_value(1.0f) + 1.0f) * 0.5f;

		random_quat(&q0, 1);
		random_quat(&q1, trial & 1);
		quaternions_interpolate_and_normalize(&q0, &q1, t, &fpu);
		vfpu_interpolate(&q0, &q1, t, &vfpu, form);
		differ_interpolate += memcmp(&fpu, &vfpu, sizeof(fpu)) != 0;
		tally_floats(&tally_interpolate, &fpu.i, &vfpu.i, 4);
		copy = q0;
		vfpu_interpolate(&copy, &q1, t, &copy, form);
		aliased_wrong += memcmp(&vfpu, &copy, sizeof(copy)) != 0;
		copy = q1;
		vfpu_interpolate(&q0, &copy, t, &copy, form);
		aliased_wrong += memcmp(&vfpu, &copy, sizeof(copy)) != 0;
		fpu_interpolate(&q0, &q1, t, &copy);
		differ_copy += memcmp(&fpu, &copy, sizeof(fpu)) != 0;

		if (mode == 2)
		{
			random_quat(&q0, 0);
			fpu = vfpu = copy = q0;
			quaternion_normalize(&fpu);
			vfpu_normalize_d(&vfpu);
			differ_normalize += memcmp(&fpu, &vfpu, sizeof(fpu)) != 0;
			tally_floats(&tally_normalize, &fpu.i, &vfpu.i, 4);
			fpu_normalize(&copy);
			differ_copy += memcmp(&fpu, &copy, sizeof(fpu)) != 0;
		}
	}
	vfpuquat_check = psp_keep("vfpuquatcheck");
	if (aliased_wrong || (vfpuquat_check && differ_copy))
		mode = 0;
	else if (mode == 1 && differ_interpolate)
		mode = 0;
	else if (mode == 2 && (tally_interpolate.worst > 64 || tally_normalize.worst > 64))
		mode = 0;
	psp_vfpuquat_mode = mode;
	platform_log("VFPU: posing maths (%s): 2000 inputs, differ from the FPU: interpolate %d (worst %u ulp), normalize %d (worst %u ulp); in place wrong %d; the check's copies %d; %s%s",
		form ? "vfpuquatd" : "vfpuquat", differ_interpolate, tally_interpolate.worst, differ_normalize, tally_normalize.worst,
		aliased_wrong, differ_copy, mode ? "on" : "OFF: it disagrees with the FPU",
		mode && vfpuquat_check ? " (vfpuquatcheck: every call checked)" : "");
}
