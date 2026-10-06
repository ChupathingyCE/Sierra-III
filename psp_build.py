#!/usr/bin/env python3
"""Builds the PSP bring-up EBOOT of halo-ce-universal.

1. Mirrors the repository into build/tree (build_var.py's mirror: the
   GCC-vs-clang textual fixes) and applies psp_fixups.py.
2. Compiles the game (source/), the port's game-side files
   (port/linux/game), the Linux platform layer files that build as they are
   against the PSP toolchain's SDL3 and pthreads, and the PSP files
   (psp/src).
3. Links, reporting anything still undefined (and stubbing it, so the
   program links), sizes the heap to end below the game state, and packs
   EBOOT.PBP.

usage: psp_build.py [--jobs N]
environment: HALO_REPO (default /tmp/halo-sp), PSP_OUT (default /tmp/psp_halo)"""
import os, re, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.environ.get("HALO_REPO", "/tmp/halo-sp")
OUT = os.environ.get("PSP_OUT", "/tmp/psp_halo")
TREE = os.path.join(OUT, "tree")
OBJ = os.path.join(OUT, "obj")
SDK = "/opt/psp/pspdev/psp/sdk"
JOBS = int(sys.argv[sys.argv.index("--jobs") + 1]) if "--jobs" in sys.argv else 2
GAME_STATE_BASE = 0x092B0000   # psp_capacity.h
PROGRAM_BASE = 0x08804000      # where the kernel loads the module (first fit, low)
STACK_RESERVE = 0x40000        # main thread and other low allocations made before the heap

PLATFORM_FILES = [
    "bink_null", "xbox_xapi", "xbox_files", "xbox_kernel", "msvc_crt", "msvc_wide", "posix_files",
    "xnet", "halo_linker_common", "port_config", "sdl_platform", "xinput_sdl", "dsound_sdl",
    "d3d8_gl", "d3d8_resources", "xbox_textures", "nv2a_psh", "nv2a_vsh", "xgpu_text", "gl_functions",
    "xiso", "xbdm",
]
PSP_FILES = ["psp_memory", "psp_platform", "psp_sdl", "psp_libc", "psp_ge", "psp_tex", "psp_vfpu", "psp_menu", "psp_geo", "psp_debug", "psp_me"]

# build 38: the game's optimisation level (PSP_GAME_OPT=-Os builds the A/B
# variant that trades speed tricks for smaller code: the hardware profiler
# showed the CPU waiting on instruction fetches for about a quarter of its time)
GAME_OPT = os.environ.get("PSP_GAME_OPT", "-O2")
# build 44: PSP_BIG_RAM=1 builds the campaign recon variant for 64 MB models
# (and the emulator set to one): a 22 MB tag window as the Xbox's, user RAM
# to 0x0C000000, MEMSIZE=1 in the PARAM.SFO so the firmware (and PPSSPP)
# extend the user partition. Recon only: the PSP-1000 is the target.
BIG_RAM = os.environ.get("PSP_BIG_RAM", "") == "1"
# PSP_TICK_STEPS=1 adds the tick breadcrumbs (bipeds, units, actors, object_update;
# psp_fixups.py) that cost a few percent of the tick: recon builds only
TICK_STEPS = os.environ.get("PSP_TICK_STEPS", "") == "1"
PSP = ["psp-gcc", "-G0", GAME_OPT, "-march=allegrex", "-ffp-contract=off", "-fshort-wchar",
       "-fms-extensions", "-fcommon", "-fno-strict-aliasing", "-fwrapv",
       "-fno-delete-null-pointer-checks", "-fno-omit-frame-pointer", "-w",
       "-ffunction-sections", "-fdata-sections", "-fno-math-errno", "-fsingle-precision-constant",
       "-DHALO_PSP=1", "-DHALO_RELEASE=1", "-DHALO_ANDROID=1",
       ] + (["-DPSP_RECON_64MB=1"] if BIG_RAM else []) + os.environ.get("PSP_CFLAGS", "").split() + [
       # build 19: failed asserts are cold paths, laid out away from the hot
       # code (the 16 KB instruction cache holds more of what runs)
       "-DPSP_ASSERTS_COLD=1", "-freorder-blocks-and-partition", "-mno-explicit-relocs",
       # build 66: the inline tag block accessor (build 46's, for the collision
       # files) for the whole game: the HUD, the visibility walk and the
       # objects' updates reach their tag data through it thousands of times a
       # frame, each an out-of-line call with five asserts
       "-DPSP_FAST_TAG_BLOCKS=1",
       "-I" + os.path.join(HERE, "psp/include"), "-I" + SDK + "/include",
       # the MSVC C runtime's _open/_read/_close... (msvc_crt.c) would replace
       # newlib's system-call hooks of the same names, which newlib's own
       # open/read/close call: close -> _close -> close -> ... forever
       ] + ["-D%s=msvc_%s" % (name, name) for name in ("_close", "_fstat", "_lseek", "_open", "_read", "_stat", "_write")] + [

       "-include", os.path.join(HERE, "psp_limits.h"),
       "-include", os.path.join(HERE, "psp_pre.h")]

def ninja_flags(target):
    out = subprocess.run(["ninja", "-t", "commands", target], cwd=REPO, capture_output=True, text=True).stdout
    args = shlex.split(out.strip().splitlines()[-1]); keep = []; i = 0
    while i < len(args):
        a = args[i]
        if a in ("-include", "-idirafter"):
            keep += [a, args[i + 1]]; i += 2; continue
        if a.startswith(("-I", "-D", "-std=", "-U")) and a != "-D_FILE_OFFSET_BITS=64":
            keep.append(a)
        i += 1
    return keep

def units():
    targets = subprocess.run(["ninja", "-t", "targets", "all"], cwd=REPO, capture_output=True, text=True).stdout
    game = sorted({m.group(1) + ".c" for m in re.finditer(r"^build/linux/obj/(source/.*)\.o: linux_cc", targets, re.M)})
    game = [s for s in game if "/libtiff/" not in s]
    game += sorted({m.group(1) + ".c" for m in re.finditer(r"^build/linux/obj/(port/linux/game/.*)\.o: linux_cc", targets, re.M)})
    items = [(s, ninja_flags("build/linux/obj/" + s[:-2] + ".o"), []) for s in game]
    compat = ["-include", os.path.join(HERE, "psp/include/psp_compat.h")]
    for name in PLATFORM_FILES:
        s = "port/linux/src/%s.c" % name
        if name == "dsound_sdl":
            # build 25: the PSP's own mixer (psp/src/psp_dsound.c), built as the Linux one is
            items.append((os.path.join(HERE, "psp/src/psp_dsound.c"), ninja_flags("build/linux/obj/" + s[:-2] + ".o"), compat))
            continue
        items.append((s, ninja_flags("build/linux/obj/" + s[:-2] + ".o"), compat))
    items.append(("port/third_party/tomlc17/tomlc17.c", ninja_flags("build/linux/obj/port/third_party/tomlc17/tomlc17.o"), []))
    memory_flags = ninja_flags("build/linux/obj/port/linux/src/xbox_memory.o")
    for name in PSP_FILES:
        items.append((os.path.join(HERE, "psp/src/%s.c" % name), memory_flags, compat))
    return items

# build 69: the game's cold code at -Os (menus, saved games and films, the
# netcode, the script interpreter, cinematics, loading, the editor and tool
# leftovers): smaller code for the same behaviour; the hot paths (physics,
# objects, units, AI, maths, models, rendering, the HUD) stay at -O2.
# PSP_NO_COLD=1 builds everything at GAME_OPT as before
COLD_PREFIXES = ("source/interface/", "source/saved games/", "source/saved films/", "source/networking/",
                 "source/hs/", "source/cutscene/", "source/text/", "source/strings/", "source/dialogs/",
                 "source/editor/", "source/tool/", "source/bungie_net/", "source/shell/", "source/bink/",
                 "source/cache/", "source/tag_files/", "source/scenario/", "source/main/", "source/game/")
COLD_KEEP_HOT = ("source/interface/hud", "source/interface/first_person", "source/main/main.c",
                 "source/game/game_time", "source/game/players", "source/game/game.c")
def cold_code(rel):
    if os.environ.get("PSP_NO_COLD") or GAME_OPT != "-O2":
        return False
    r = rel.replace("\\", "/")
    return r.startswith(COLD_PREFIXES) and not r.startswith(COLD_KEEP_HOT)

def compile_one(item):
    src, flags, extra = item
    rel = os.path.relpath(src, HERE) if src.startswith("/") else src
    obj = os.path.join(OBJ, rel[:-2] + ".o")
    if cold_code(rel):
        extra = extra + ["-Os"]
    os.makedirs(os.path.dirname(obj), exist_ok=True)
    defines = ["-Dmain=halo_main"] if src.endswith("shell/shell_xbox.c") else []
    if src.endswith("psp_platform.c"):
        defines.append("-DPSP_HEAP_KB=%d" % HEAP_KB)
    # PSP_REUSE: keep an object only when it is newer than its source. Build
    # 46 shipped with a stale psp_tex.o (reused without this check, so an
    # edit to psp_tex.c never reached the EBOOT): the learned-binds file was
    # written without "vramranked", as the first version did
    if (os.environ.get("PSP_REUSE") and os.path.exists(obj) and not src.endswith("psp_platform.c")
            and os.path.getmtime(obj) > os.path.getmtime(os.path.join(TREE, src))):
        return src, 0, "", obj
    cmd = PSP + flags + extra + defines + ["-include", os.path.join(HERE, "psp_post.h"), "-c", src, "-o", obj]
    if src.endswith(("psp_sdl.c", "psp_libc.c", "psp_ge.c", "psp_tex.c", "psp_vfpu.c", "psp_menu.c", "psp_geo.c", "psp_debug.c",
                     "psp_me.c")):
        # plain C against the SDL3 headers, none of the game's MSVC prefix
        cmd = ["psp-gcc", "-G0", "-O2", "-march=allegrex", "-ffunction-sections", "-fdata-sections",
               "-I" + SDK + "/include", "-c", src, "-o", obj]
        if src.endswith("psp_me.c"):
            # build 86: mcidclan's me-core-mapper headers (installed beside the SDK)
            cmd[-4:-4] = ["-I" + os.path.dirname(SDK) + "/include"]
    r = subprocess.run(cmd, cwd=TREE, capture_output=True, text=True)
    if r.returncode and "-mno-explicit-relocs" in cmd:
        # inline assembly that uses $at (".set noat") needs explicit relocations
        r = subprocess.run([a for a in cmd if a not in ("-mno-explicit-relocs", "-freorder-blocks-and-partition")],
                           cwd=TREE, capture_output=True, text=True)
    return src, r.returncode, r.stderr, obj

LIBS = ["-lpspvram", "-lpspaudio", "-lpspvfpu", "-lpspdisplay", "-lpspgum", "-lpspgu", "-lpspge",
        "-lpsphprm", "-lpspdmac", "-lpspctrl", "-lpsppower", "-lpthread", "-lpsprtc", "-lpspdebug", "-lme-core", "-lm"]

def link(objs, stubs):
    with open(os.path.join(OUT, "stubs.c"), "w") as f:
        f.write("/* generated by psp_build.py: symbols nothing defines yet */\n")
        for s in sorted(stubs):
            f.write("int %s(void) { return 0; }\n" % s)
    subprocess.run(["psp-gcc", "-G0", "-O2", "-w", "-c", os.path.join(OUT, "stubs.c"), "-o", os.path.join(OUT, "stubs.o")], check=True)
    with open(os.path.join(OUT, "objs.rsp"), "w") as f:
        for o in objs + [os.path.join(OUT, "stubs.o")]:
            f.write('"%s"\n' % o)
    # build 46: the import stub for halo_profile.prx's system call
    # (psp/profile/exports.exp; psp-build-exports -s writes halo_profile.S).
    # The module loads after the game starts; the firmware links the import
    # then, as it does for mecore's kcall
    # (the new stub format, psp-build-exports -k: two objects, the function's
    # stub referring to the library's entry, so --gc-sections keeps both; the
    # old format's library entry was dropped as unreferenced)
    for part in ("0000", "0001"):
        stub = os.path.join(OUT, "halo_profile_%s.o" % part)
        subprocess.run(["psp-gcc", "-G0", "-I" + SDK + "/include", "-DF_halo_profile_" + part, "-c",
                        os.path.join(HERE, "psp/profile/halo_profile.S"), "-o", stub], check=True)
        with open(os.path.join(OUT, "objs.rsp"), "a") as f:
            f.write('"%s"\n' % stub)
    # build 86: the import stub for mecore's kcall.prx (psp/meaudio/kcall.S,
    # psp-build-exports -k of mecore's kernel/exports.exp), for the same
    # reason: the library's own stub is the old format, which --gc-sections
    # would drop. Ours come first, so the library's member is never pulled
    for part in ("0000", "0001", "0002", "0003"):
        stub = os.path.join(OUT, "kcall_%s.o" % part)
        subprocess.run(["psp-gcc", "-G0", "-I" + SDK + "/include", "-DF_kcall_" + part, "-c",
                        os.path.join(HERE, "psp/meaudio/kcall.S"), "-o", stub], check=True)
        with open(os.path.join(OUT, "objs.rsp"), "a") as f:
            f.write('"%s"\n' % stub)
    cmd = ["psp-gcc", "-G0", "-L" + SDK + "/lib", "-specs=" + SDK + "/lib/prxspecs",
           "-Wl,-q,-T" + SDK + "/lib/linkfile.prx", "-Wl,-zmax-page-size=128", "-Wl,--gc-sections",
           "-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=memalign,--wrap=sceKernelCreateThread,--wrap=sceGeListUpdateStallAddr",
           "@" + os.path.join(OUT, "objs.rsp"), SDK + "/lib/prxexports.o"] + LIBS + ["-o", os.path.join(OUT, "halo.elf")]
    return subprocess.run(cmd, capture_output=True, text=True)

def main():
    global HEAP_KB
    sys.path.insert(0, HERE)
    os.environ["HALO_REPO"] = REPO
    os.environ["PSP_BUILD"] = OUT
    import build_var
    build_var.REPO, build_var.TREE = REPO, TREE
    build_var.mirror()
    subprocess.run([sys.executable, os.path.join(HERE, "psp_fixups.py"), TREE], check=True)
    HEAP_KB = int(os.environ.get("PSP_HEAP_KB", "4096"))
    items = units()
    results = list(ThreadPoolExecutor(JOBS).map(compile_one, items))
    bad = [(s, e) for s, c, e, o in results if c]
    with open(os.path.join(OUT, "compile_errors.txt"), "w") as f:
        for s, e in bad:
            f.write("=== %s\n%s\n" % (s, e))
    print("compiled %d of %d%s" % (len(results) - len(bad), len(results),
          "; failures in compile_errors.txt: " + ", ".join(os.path.basename(s) for s, e in bad) if bad else ""))
    if bad:
        sys.exit("not linking: %d units failed to compile" % len(bad))
    objs = [o for s, c, e, o in results if not c]
    stubs = set()
    for attempt in range(6):
        r = link(objs, stubs)
        missing = set(re.findall(r"undefined reference to `([^']+)'", r.stderr))
        if r.returncode == 0 or not (missing - stubs):
            break
        stubs |= missing
    if r.returncode:
        print(r.stderr[-3000:]); sys.exit(1)
    open(os.path.join(OUT, "stubbed_symbols.txt"), "w").write("\n".join(sorted(stubs)) + "\n")
    # size the heap: from the program's end up to the game state
    size = subprocess.run(["psp-size", os.path.join(OUT, "halo.elf")], capture_output=True, text=True).stdout.splitlines()[1].split()
    program = int(size[0]) + int(size[1]) + int(size[2])
    heap = (GAME_STATE_BASE - PROGRAM_BASE - program - STACK_RESERVE) // 1024 // 64 * 64
    print("program %.2f MB (text %d, data %d, bss %d); heap %d KB; %d symbols stubbed (stubbed_symbols.txt)"
          % (program / 1048576, int(size[0]), int(size[1]), int(size[2]), heap, len(stubs)))
    if heap != HEAP_KB:
        HEAP_KB = heap
        src = os.path.join(HERE, "psp/src/psp_platform.c")
        item = [i for i in items if i[0] == src][0]
        s, c, e, o = compile_one(item)
        assert c == 0, e
        r = link(objs, stubs)
        assert r.returncode == 0, r.stderr[-2000:]
    subprocess.run(["psp-fixup-imports", os.path.join(OUT, "halo.elf")], check=True)
    subprocess.run(["psp-prxgen", os.path.join(OUT, "halo.elf"), os.path.join(OUT, "halo.prx")], check=True)
    subprocess.run(["mksfoex"] + (["-d", "MEMSIZE=1"] if BIG_RAM else []) +
                   [os.environ.get("PSP_TITLE", "Halo PSP bring-up"), os.path.join(OUT, "PARAM.SFO")], check=True)
    subprocess.run(["pack-pbp", os.path.join(OUT, "EBOOT.PBP"), os.path.join(OUT, "PARAM.SFO"),
                    "NULL", "NULL", "NULL", "NULL", "NULL", os.path.join(OUT, "halo.prx"), "NULL"],
                   check=True, capture_output=True)
    print("wrote", os.path.join(OUT, "EBOOT.PBP"))

if __name__ == "__main__":
    main()
