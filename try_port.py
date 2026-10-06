#!/usr/bin/env python3
"""Compiles port/ files for the PSP with the Linux build's flags; prints errors."""
import os, shlex, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
REPO = os.environ.get("HALO_REPO", "/tmp/halo-sp")
TREE = os.environ.get("PSP_TREE", "/tmp/psp_build_sp/tree")
OBJ = os.environ.get("PSP_PORT_OBJ", "/tmp/psp_port_obj")
HERE = os.path.dirname(os.path.abspath(__file__))
PSP = ["psp-gcc", "-G0", "-O2", "-march=allegrex", "-ffp-contract=off", "-fshort-wchar",
       "-fms-extensions", "-fcommon", "-fno-strict-aliasing", "-fwrapv",
       "-fno-delete-null-pointer-checks", "-fno-omit-frame-pointer", "-w",
       "-ffunction-sections", "-fdata-sections", "-fno-math-errno", "-fsingle-precision-constant",
       "-DHALO_PSP=1", "-DHALO_RELEASE=1", "-DHALO_ANDROID=1",
       "-I/opt/psp/pspdev/psp/sdk/include",
       "-include", os.path.join(HERE, "psp_pre.h")] + os.environ.get("PSP_EXTRA", "").split()
def flags(src):
    cmd = subprocess.run(["ninja", "-t", "commands", "build/linux/obj/" + src[:-2] + ".o"], cwd=REPO,
                         capture_output=True, text=True).stdout.strip().splitlines()[-1]
    args = shlex.split(cmd); keep = []; i = 0
    while i < len(args):
        a = args[i]
        if a in ("-include", "-idirafter"):
            keep += [a, args[i + 1]]; i += 2; continue
        if a.startswith(("-I", "-D", "-std=", "-U")) and a != "-D_FILE_OFFSET_BITS=64":
            keep.append(a)
        i += 1
    return keep
ok = []
for src in sys.argv[1:]:
    obj = os.path.join(OBJ, src[:-2] + ".o"); os.makedirs(os.path.dirname(obj), exist_ok=True)
    r = subprocess.run(PSP + flags(src) + ["-include", os.path.join(HERE, "psp_post.h"), "-c", src, "-o", obj],
                       cwd=TREE, capture_output=True, text=True)
    errs = [l for l in r.stderr.splitlines() if "error" in l]
    print("%-45s %s" % (src, "OK" if r.returncode == 0 else "FAIL (%d errors)" % len(errs)))
    for l in errs[:int(os.environ.get("SHOW", "4"))]: print("    " + l[:220])
    if r.returncode == 0: ok.append(obj)
open(os.path.join(OBJ, "ok.txt"), "w").write("\n".join(ok) + "\n")
