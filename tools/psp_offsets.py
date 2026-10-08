#!/usr/bin/env python3
"""psp_offsets.py - where a structure's fields sit in the PSP build.

The decomp's structures are laid out for the PSP by psp-gcc with the build's
own flags (32-bit pointers, psp_limits.h's sizes), so their offsets differ
from a host build's. This compiles a probe with exactly the flags a game
source file gets (psp_build.py) and reads the offsets back from the object's
symbol sizes, without running anything on the PSP.

usage: python3 tools/psp_offsets.py SOURCE STRUCT [FIELD ...]
       python3 tools/psp_offsets.py source/units/bipeds.c biped_datum object.idle_ticks unit.timer

The mirror tree (PSP_OUT/tree, from the last psp_build.py run) supplies the
sources, so the offsets are those of the build that ran last."""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, HERE)
import psp_build  # noqa: E402  (no build runs on import)


def offsets(source, struct, fields):
    flags = psp_build.ninja_flags("build/linux/obj/" + source[:-2] + ".o")
    # the whole source, as the build compiled it (psp_fixups.py's prelude
    # and changes included), with the probes after it
    text = open(os.path.join(psp_build.TREE, source), errors="surrogateescape").read()
    with tempfile.TemporaryDirectory() as tmp:
        probe = os.path.join(psp_build.TREE, os.path.dirname(source), "psp_offsets_probe.c")
        with open(probe, "w", errors="surrogateescape") as f:
            f.write(text + "\n#include <stddef.h>\n")
            f.write("char probe_size[sizeof(struct %s) + 1];\n" % struct)
            for k, field in enumerate(fields):
                f.write("char probe_%d[offsetof(struct %s, %s) + 1];\n" % (k, struct, field))
        obj = os.path.join(tmp, "probe.o")
        cmd = psp_build.PSP + flags + ["-include", os.path.join(HERE, "psp_post.h"), "-c", probe, "-o", obj, "-fno-common"]
        run = subprocess.run(cmd, cwd=psp_build.TREE, capture_output=True, text=True)
        os.remove(probe)
        if run.returncode:
            sys.exit(run.stderr[-2000:])
        sizes = {}
        for line in subprocess.run(["psp-nm", "-S", obj], capture_output=True, text=True).stdout.splitlines():
            parts = line.split()
            if len(parts) == 4:
                sizes[parts[3]] = int(parts[1], 16) - 1
    return sizes["probe_size"], [sizes["probe_%d" % k] for k in range(len(fields))]


if __name__ == "__main__":
    args = sys.argv[1:]
    if len(args) < 3:
        sys.exit(__doc__)
    source, struct, fields = args[0], args[1], args[2:]
    size, found = offsets(source, struct, fields)
    print("struct %s: %d bytes" % (struct, size))
    for field, offset in sorted(zip(fields, found), key=lambda p: p[1]):
        print("%6d 0x%04x  %s" % (offset, offset, field))
