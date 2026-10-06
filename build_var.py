#!/usr/bin/env python3
"""PSP link harness for halo-ce-universal (bring-up, not the real port).

1. Mirrors source/ and port/ into build/tree, applying the few textual fixes
   GCC needs where the Linux build relies on clang's MSVC extensions:
   "static __inline" -> "static __inline__" (the prefix maps __inline to a
   weak inline instead), and the MSVC "ui64"/"i64" integer suffixes.
2. Compiles every game source the Linux build compiles (minus libtiff, a
   bitmap-import library) with psp-gcc, the Linux build's include paths and
   defines, and three small headers (psp_pre.h, psp_post.h, a filtered copy
   of the generated MSVC-semantics header).
3. Links the objects into a PSP executable, generating a stub for every
   symbol nothing defines (the platform layer that doesn't exist yet), and
   reports code and data size.

usage: build.py [--jobs N]   (run anywhere; paths are fixed below)
"""
import os, re, shlex, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

REPO = os.environ.get("HALO_REPO", "/root/halo-ce-universal")
HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.environ.get("PSP_BUILD", os.path.join(HERE, "build"))
TREE = os.path.join(BUILD, "tree")
OBJ = os.path.join(BUILD, "obj")
JOBS = int(sys.argv[sys.argv.index("--jobs") + 1]) if "--jobs" in sys.argv else 2

STATIC_INLINE = re.compile(r"\bstatic(\s+)(__inline|_inline|__forceinline)\b")
MSVC_SUFFIX = re.compile(r"\b(0x[0-9A-Fa-f]+|[0-9]+)(ui64|i64)\b")

def mirror():
    for top in ("source", "port", "build/linux"):
        src_top = os.path.join(REPO, top)
        for root, dirs, files in os.walk(src_top):
            for f in files:
                src = os.path.join(root, f)
                dst = os.path.join(TREE, os.path.relpath(src, REPO))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                if f.endswith((".c", ".h")):
                    text = open(src, errors="surrogateescape").read()
                    text = STATIC_INLINE.sub(lambda m: "static" + m.group(1) + ("__inline__ __attribute__((always_inline))" if m.group(2) == "__forceinline" else "__inline__"), text)
                    text = MSVC_SUFFIX.sub(lambda m: m.group(1) + ("ULL" if m.group(2) == "ui64" else "LL"), text)
                    if os.path.exists(dst) and open(dst, errors="surrogateescape").read() == text:
                        continue
                    open(dst, "w", errors="surrogateescape").write(text)
                elif not os.path.exists(dst):
                    shutil.copy2(src, dst)
    # the generated header marks every inline function #pragma weak; drop the
    # names some unit declares static (GCC refuses a weak static)
    statics = set()
    for root, dirs, files in os.walk(os.path.join(TREE, "source")):
        for f in files:
            if f.endswith((".c", ".h")):
                for m in re.finditer(r"\bstatic[ \t]+[^;{()\n=]*?\b(\w+)[ \t]*\(", open(os.path.join(root, f), errors="surrogateescape").read()):
                    statics.add(m.group(1))
    sem = os.path.join(TREE, "build/linux/halo_msvc_semantics.h")
    lines = open(sem).read().splitlines()
    kept = [l for l in lines if not (l.startswith("#pragma weak ") and l.split()[2] in statics)]
    open(sem, "w").write("\n".join(kept) + "\n")
    return len(lines) - len(kept)

def sources_and_flags():
    targets = subprocess.run(["ninja", "-t", "targets", "all"], cwd=REPO, capture_output=True, text=True).stdout
    srcs = sorted({m.group(1) + ".c" for m in re.finditer(r"^build/linux/obj/(source/.*)\.o: linux_cc", targets, re.M)})
    srcs = [s for s in srcs if "/libtiff/" not in s]
    out = {}
    for s in srcs:
        cmd = subprocess.run(["ninja", "-t", "commands", "build/linux/obj/" + s[:-2] + ".o"], cwd=REPO, capture_output=True, text=True).stdout.strip().splitlines()[-1]
        args = shlex.split(cmd)
        keep, i = [], 0
        while i < len(args):
            a = args[i]
            if a in ("-include", "-idirafter"):
                keep += [a, args[i + 1]]; i += 2; continue
            if a.startswith(("-I", "-D", "-std=", "-U")):
                keep.append(a)
            i += 1
        out[s] = keep
    return out

PSP = ["psp-gcc", "-G0", "-O2", "-march=allegrex", "-ffp-contract=off", "-fshort-wchar",
       "-fms-extensions", "-fcommon", "-fno-strict-aliasing", "-fwrapv",
       "-fno-delete-null-pointer-checks", "-fno-omit-frame-pointer", "-w",
       "-ffunction-sections", "-fdata-sections",
       "-DHALO_PSP=1", "-DHALO_RELEASE=1", "-DHALO_ANDROID=1",
       "-include", os.path.join(HERE, "psp_pre.h")] + os.environ.get("PSP_EXTRA", "").split()

def compile_one(item):
    src, flags = item
    obj = os.path.join(OBJ, src[:-2] + ".o")
    os.makedirs(os.path.dirname(obj), exist_ok=True)
    if os.environ.get("PSP_REUSE") and os.path.exists(obj):
        return src, 0, ""
    cmd = PSP + flags + ["-include", os.path.join(HERE, "psp_post.h"), "-c", src, "-o", obj]
    r = subprocess.run(cmd, cwd=TREE, capture_output=True, text=True)
    if r.returncode and "-mno-explicit-relocs" in cmd:
        # thread-local variables need explicit relocations
        r = subprocess.run([a for a in cmd if a not in ("-mno-explicit-relocs", "-freorder-blocks-and-partition")], cwd=TREE, capture_output=True, text=True)
    return src, r.returncode, r.stderr

def main():
    os.makedirs(BUILD, exist_ok=True)
    dropped = mirror()
    print("mirrored; %d #pragma weak lines dropped for static functions" % dropped)
    items = sorted(sources_and_flags().items())
    results = list(ThreadPoolExecutor(JOBS).map(compile_one, items))
    bad = [(s, e) for s, c, e in results if c]
    with open(os.path.join(BUILD, "compile_errors.txt"), "w") as f:
        for s, e in bad:
            f.write("=== %s\n%s\n" % (s, e))
    print("compiled %d of %d" % (len(results) - len(bad), len(results)))
    objs = [os.path.join(OBJ, s[:-2] + ".o") for s, c, e in results if not c]
    with open(os.path.join(BUILD, "objects.txt"), "w") as f:
        f.write("\n".join(objs) + "\n")

if __name__ == "__main__":
    main()
