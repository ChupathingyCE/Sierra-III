#!/usr/bin/env bash
# Builds a Sierra-III release from this checkout on a clean Linux machine: the
# EBOOT, the two helper PRX modules and a zip laid out for the Memory Stick.
#
#   tools/build_release.sh <output folder>
#
# Needs: the pspdev toolchain, git, Python 3, ninja, cmake, make
# (docs/toolchain.md says how to get each). The engine and the Media Engine
# library are fetched at the commits upstream/pins.env pins, built once into a
# cache, and reused by later builds.
#
# environment:
#   PSPDEV   the pspdev toolchain (required, e.g. /opt/pspdev)
#   CACHE    where the engine and me-core-mapper builds are kept
#            (default ${XDG_CACHE_HOME:-~/.cache}/sierra-iii)
#   ENGINE   chupa (default) or universal: which engine base
#   JOBS     compile jobs (default: every CPU)
#   LABEL    the zip's name (default: the commit's short hash)
#   PSP_*    build switches, passed to psp_build.py (CLAUDE.md lists them)
#
# writes into the output folder:
#   halo_psp_<LABEL>.zip   PSP/GAME/halo/ (EBOOT.PBP, halo_crash.prx,
#                          halo_profile.prx, LICENSE-mecore.md), and
#                          halo/presets.txt when psp/presets.txt exists
#   halo.elf, halo.map     the linked program and its link map
#   build.log              everything the build printed
#
# The same commit builds the same bytes on any machine: the compile records
# no paths or times, and the zip's files carry the commit's time. Reads this
# checkout and the network (the pinned upstreams); writes only under the
# output folder and CACHE.
set -euo pipefail

SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:?usage: tools/build_release.sh <output folder>}
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
: "${PSPDEV:?PSPDEV must name the pspdev toolchain (docs/toolchain.md)}"
CACHE=${CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/sierra-iii}
ENGINE=${ENGINE:-chupa}
JOBS=${JOBS:-$(nproc)}
LABEL=${LABEL:-$(git -C "$SRC" rev-parse --short=10 HEAD)}
export PATH="$PSPDEV/bin:$PATH"
mkdir -p "$CACHE"

# the pins: only the names this script uses are taken from the file
eval "$(cd "$SRC" && set -a && . upstream/pins.env && set +a && env | grep -E '^(ENGINE|MECORE)_')"

log() { echo "build: $*" | tee -a "$OUT/build.log"; }
: > "$OUT/build.log"

fetch_at() {   # fetch_at <url> <full commit> <dir>: a shallow checkout of one commit
    local url=$1 commit=$2 dir=$3
    git init -q "$dir"
    git -C "$dir" fetch -q --depth 1 "$url" "$commit"
    git -C "$dir" -c advice.detachedHead=false checkout -q FETCH_HEAD
}

# Two builds sharing a CACHE can want the same entry at once: the second waits
# for the first, then finds the entry built. Entries are built in <dir>.part
# and renamed into place, so a half-built one is never used.
lock_entry() {
    exec 8>"$(dirname "$1")/.$(basename "$1").lock"
    flock -n 8 || { log "another build is preparing $(basename "$1"); waiting for it"; flock 8; }
}
unlock_entry() { exec 8>&-; }

# --- the Media Engine library (me-core-mapper), once per pin -----------------
MECORE_DIR=$CACHE/mecore-$MECORE_COMMIT
lock_entry "$MECORE_DIR"
if [[ ! -f $MECORE_DIR/lib/libme-core.a ]]; then
    log "me-core-mapper $MECORE_COMMIT: building into $MECORE_DIR"
    tmp=$(mktemp -d "$OUT/mecore-src.XXXXXX")
    fetch_at "$MECORE_URL" "$MECORE_COMMIT" "$tmp"
    cmake -S "$tmp" -B "$tmp/build" -DCMAKE_TOOLCHAIN_FILE="$PSPDEV/psp/share/pspdev.cmake" >> "$OUT/build.log" 2>&1
    cmake --build "$tmp/build" -j "$JOBS" >> "$OUT/build.log" 2>&1
    rm -rf "$MECORE_DIR.part"; mkdir -p "$MECORE_DIR.part/lib" "$MECORE_DIR.part/include/me-core-mapper/kernel"
    cp "$tmp"/build/libme-core*.a "$MECORE_DIR.part/lib/"
    for h in "$tmp"/*.h; do [[ $h == *me-core-mapping.def.h ]] || cp "$h" "$MECORE_DIR.part/include/me-core-mapper/"; done
    cp "$tmp"/kernel/*.h "$MECORE_DIR.part/include/me-core-mapper/kernel/"
    cp "$tmp/LICENSE.md" "$MECORE_DIR.part/LICENSE.md"
    rm -rf "$MECORE_DIR"; mv "$MECORE_DIR.part" "$MECORE_DIR"; rm -rf "$tmp"
fi
unlock_entry

# --- the engine, checked out and configured once per pin ---------------------
case $ENGINE in
    chupa)     url=$ENGINE_CHUPA_URL;     commit=$ENGINE_CHUPA_COMMIT ;;
    universal) url=$ENGINE_UNIVERSAL_URL; commit=$ENGINE_UNIVERSAL_COMMIT ;;
    *) echo "build: ENGINE is chupa or universal, not '$ENGINE'" >&2; exit 1 ;;
esac
ENGINE_DIR=$CACHE/engine-$ENGINE-$commit
lock_entry "$ENGINE_DIR"
if [[ ! -f $ENGINE_DIR/build/linux/halo_msvc_semantics.h ]]; then
    log "engine $ENGINE $commit: checking out into $ENGINE_DIR"
    rm -rf "$ENGINE_DIR.part"
    fetch_at "$url" "$commit" "$ENGINE_DIR.part"
    if [[ $ENGINE == universal ]]; then
        git -C "$ENGINE_DIR.part" -c user.name=build -c user.email=build@localhost am -q "$SRC"/upstream/halo-ce-universal/*.patch
    fi
    if [[ $ENGINE == chupa ]]; then cfg=(--no-game-browser); else cfg=(); fi
    (cd "$ENGINE_DIR.part" && python3 configure.py "${cfg[@]}" >> "$OUT/build.log" 2>&1 \
        && ninja build/linux/halo_msvc_semantics.h build/linux/platform_msvc_semantics.h >> "$OUT/build.log" 2>&1)
    rm -rf "$ENGINE_DIR"; mv "$ENGINE_DIR.part" "$ENGINE_DIR"
fi
unlock_entry

# --- the port --------------------------------------------------------------------
log "Sierra-III $(git -C "$SRC" rev-parse HEAD) on $ENGINE, $JOBS jobs"
(cd "$SRC" && HALO_REPO="$ENGINE_DIR" PSP_OUT="$OUT/build" PSP_MECORE="$MECORE_DIR" \
    python3 psp_build.py --jobs "$JOBS") >> "$OUT/build.log" 2>&1 || { tail -30 "$OUT/build.log"; exit 1; }
make -C "$SRC/psp/crash" >> "$OUT/build.log" 2>&1
make -C "$SRC/psp/profile" >> "$OUT/build.log" 2>&1

# --- the release zip -----------------------------------------------------------
pkg=$OUT/pkg; rm -rf "$pkg"; mkdir -p "$pkg/PSP/GAME/halo"
cp "$OUT/build/EBOOT.PBP" "$SRC/psp/crash/halo_crash.prx" "$SRC/psp/profile/halo_profile.prx" "$pkg/PSP/GAME/halo/"
cp "$MECORE_DIR/LICENSE.md" "$pkg/PSP/GAME/halo/LICENSE-mecore.md"
top=(PSP)
if [[ -f $SRC/psp/presets.txt ]]; then
    mkdir -p "$pkg/halo"; cp "$SRC/psp/presets.txt" "$pkg/halo/presets.txt"; top+=(halo)
fi
# every file and folder carries the commit's time and fixed permissions, so
# the zip's bytes depend only on the commit (Python's zip tool also sorts)
stamp=$(git -C "$SRC" log -1 --format=%ct)
find "$pkg" -type d -exec chmod 755 {} +
find "$pkg" -type f -exec chmod 644 {} +
find "$pkg" -exec touch -h -d "@$stamp" {} +
rm -f "$OUT/halo_psp_$LABEL.zip"
(cd "$pkg" && python3 -m zipfile -c "$OUT/halo_psp_$LABEL.zip" "${top[@]}")
cp "$OUT/build/halo.elf" "$OUT/build/halo.map" "$OUT/"
log "$(grep -E '^program ' "$OUT/build.log" | tail -1)"
log "wrote $OUT/halo_psp_$LABEL.zip ($(sha256sum "$OUT/build/EBOOT.PBP" | cut -c1-16) EBOOT)"
