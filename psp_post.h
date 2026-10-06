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
