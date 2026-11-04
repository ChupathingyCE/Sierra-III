/* PSP bring-up: after the Linux prefix and the MSVC-semantics header.

The Linux prefix makes every __inline function "static" (one private copy
per unit); the build mirror has already turned the source's own
"static __inline" into "static __inline__", so the rest become weak inline
functions here: one copy survives the link, as with MSVC's COMDAT. */
#undef __inline
#undef _inline
#undef __forceinline
#define __inline __inline__ __attribute__((weak))
#define _inline __inline__ __attribute__((weak))
#define __forceinline __inline__ __attribute__((weak, always_inline))

/* MSVC storage classes and calling conventions */
#define __declspec(x) __declspec_##x
#define __declspec_align(n) __attribute__((aligned(n)))
#define __declspec_noreturn __attribute__((noreturn))
#define __declspec_dllexport
#define __declspec_dllimport
#define __declspec_selectany __attribute__((weak))
#define __cdecl
#define __stdcall
#define __fastcall
#define _cdecl
#define _stdcall
#define __int64 long long
#define __int32 int
#define __int16 short
#define __int8 char

/* newlib's <ctype.h> defines these as macros; the game declares some of
them as functions */
#undef isalpha
#undef isdigit
#undef isspace
#undef isupper
#undef islower
#undef isalnum
#undef isxdigit
#undef ispunct
#undef isprint
#undef iscntrl
#undef isgraph
#undef tolower
#undef toupper

/* build 114: a keep word's answer remembered at its call site, for the
asks made per object or per draw (shadows, decals, the HUD, every bitmap a
draw binds): psp_keep remembers answers itself, but each ask still hashes
the word and compares it, about a thousand asks a frame on b30 (b112's
cache-model run: 61,000 instructions a frame of drawing in psp_keep).
keep.txt is read once at start-up (psp_platform.c, keep_load); before
that, the answer is 0 and isn't remembered */
int psp_keep(const char *name);
int psp_keep_loaded(void);
#define PSP_KEEP_ONCE(word) __extension__ ({ \
	static signed char psp_keep_once_ = -1; \
	if (psp_keep_once_ < 0 && psp_keep_loaded()) \
		psp_keep_once_ = (signed char)psp_keep(word); \
	psp_keep_once_ > 0; })

/* the release's extra keep defaults (psp_platform.c's keep_defaults): kept
here so the table's line count, and every function's line after it, is the
same in both builds and one profile fits them */
#if defined(PSP_METRICS) && PSP_METRICS
#define PSP_RELEASE_DEFAULTS
#else
#define PSP_RELEASE_DEFAULTS { "nooverlay", "overlay" },
#endif

/* TRUE on the Media Engine, or in work the emulator runs as the ME's
(psp_me.h's PSP_ME_WORK, for engine units, which don't include it) */
#ifndef PSP_CPU_IS_ME_DEFINED
#define PSP_CPU_IS_ME_DEFINED
/* Which CPU is running this, the main CPU or the Media Engine: the ME has
no VFPU and no kernel, so code both run takes its VFPU and system-call
routes only on the main CPU. The test is the stack: the ME's is in its
eDRAM (below 16 MB in any segment), the main CPU's threads' in main RAM.
It holds because nothing moves the ME onto another stack: the one jump the
ME may take (1b's stop, actors.c) goes only to a jump its own CPU set and
is refused on the ME into a stack outside its eDRAM (counted, logged). The
CPU number itself (coprocessor 0 register 22) can't be the test: the main
CPU runs in user mode, where reading it faults (code 11, as the VFPU does
on the ME); the ME's is logged at its start */
static __inline__ __attribute__((always_inline)) int psp_cpu_is_me(void)
{
	unsigned int psp_cpu_sp_;

	__asm__ ("move %0, $sp" : "=r"(psp_cpu_sp_));
	return (psp_cpu_sp_ & 0x1FFFFFFFu) < 0x01000000u;
}
#endif

extern volatile unsigned int psp_me_context;
static __inline__ __attribute__((always_inline)) int psp_work_on_me(void)
{
	return psp_me_context != 0 || psp_cpu_is_me();
}

/* "aime"'s thinking pass ahead (actors.c): its stops, queues and prop log
act only in the pass's own context, on the ME or on the stack the pass runs
on in the main CPU (its thread's, or the emulator's stand-in for the ME's,
psp_aime_sp_low-high). Another thread's work during the pass, or the main
CPU's own frame while the ME's pass is out, is never stopped or queued: a
stop there would jump onto the pass's frames from another stack */
/* a variable the Media Engine writes while its job runs ("aime2a"'s pass
and what it reaches): psp_build.py's link script keeps these together on
64-byte lines of their own, so no line holds both a word the ME writes and
one the main CPU writes during the job (each CPU's write-back cache would
put back its stale copy of the other's words). A static gets it before its
type, a struct after its name */
#ifndef PSP_AIME_ME_SIDE
#define PSP_AIME_ME_SIDE __attribute__((section(".data.psp_aime_me")))
#endif

/* On the ME that is the AI job alone (psp_aime_job_now[0]: 0 in any other
job, and in the jobs its yields run, psp_me.c): another ME job reaching a
stop while the main CPU's pass is mid-actor would otherwise jump onto the
main CPU's stack */
extern unsigned int psp_aime_sp_low, psp_aime_sp_high;
extern int psp_aime_job_now[16];
static __inline__ __attribute__((always_inline)) int psp_aime_here(void)
{
	unsigned int psp_sp_;

	if (psp_cpu_is_me())
		return psp_aime_job_now[0] != 0;
	__asm__ ("move %0, $sp" : "=r"(psp_sp_));
	return psp_sp_ - psp_aime_sp_low < psp_aime_sp_high - psp_aime_sp_low;
}

/* "meprobe" (PSP_CFLAGS=-DPSP_MEPROBE=1, a recon build): the ring's main-CPU
calls (psp_me_wait, psp_me_push, psp_me_hold) and the paths to them noted
when the Media Engine's work reaches them: on the PSP the ME itself; in the
emulator the AI job's simulated pass (its own stack, psp_me_context set).
Each site with its caller's return address, so the chain can be named from
the link map. Compiled out otherwise: the expression is a plain 0 */
#ifdef PSP_MEPROBE
void psp_meprobe_note(int site, void *ra);
void psp_meprobe_report(void);
#define PSP_MEPROBE_EXPR(site) ((psp_cpu_is_me() || (psp_me_context && psp_aime_here())) ? \
	(psp_meprobe_note((site), __builtin_return_address(0)), 0) : 0)
#define PSP_MEPROBE_REPORT() (psp_meprobe_report(), 0)
#else
#define PSP_MEPROBE_EXPR(site) 0
#define PSP_MEPROBE_REPORT() 0
#endif
