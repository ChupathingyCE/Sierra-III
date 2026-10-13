#!/usr/bin/env bash
# pgo_merge.sh - merges the counts of several PSP_PGO=generate runs into one
# profile for PSP_PGO=use: pgo/<name>.tgz.
#
#   tools/pgo_merge.sh <name> <gcda.tgz> [<gcda.tgz> ...]
#   tools/pgo_merge.sh campaign-b119 runs/*/gcda.tgz
#
# Each counting run writes its counts (one .gcda file a unit: how often each
# branch went each way) at its gcovdumpN second; the lab returns them as
# gcda.tgz. psp-gcov-tool's merge adds two runs' counts, so the runs are
# folded in one at a time, and each stretch weighs in proportion to how long
# it ran. Count at the build's own commit: GCC matches a function's counts by
# a checksum of its code and line numbers, and drops the counts of any
# function that changed (the PGO notes in psp_build.py).
#
# Needs pspdev's bin on PATH (psp-gcov-tool ships with psp-gcc).
set -euo pipefail
name=${1:?usage: pgo_merge.sh <name> <gcda.tgz> [<gcda.tgz> ...]}; shift
(( $# )) || { echo "pgo_merge: no gcda.tgz given" >&2; exit 1; }
[[ $name =~ ^[A-Za-z0-9_-]+$ ]] || { echo "pgo_merge: the name is letters, digits, - and _ (psp_build.py's rule)" >&2; exit 1; }
command -v psp-gcov-tool >/dev/null || { echo "pgo_merge: psp-gcov-tool not on PATH (pspdev's bin)" >&2; exit 1; }

here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT

i=0
for archive in "$@"; do
    i=$((i + 1))
    mkdir "$work/run$i"
    tar xzf "$archive" -C "$work/run$i"
    if (( i == 1 )); then
        mv "$work/run1" "$work/merged"
    else
        psp-gcov-tool merge "$work/merged" "$work/run$i" -o "$work/next"
        rm -rf "$work/merged" "$work/run$i"; mv "$work/next" "$work/merged"
    fi
    echo "pgo_merge: $i: $archive"
done

mkdir -p "$here/pgo"
(cd "$work/merged" && tar czf "$here/pgo/$name.tgz" -- *)   # names as the earlier profiles have them, no ./
echo "pgo_merge: pgo/$name.tgz from $i runs ($(tar tzf "$here/pgo/$name.tgz" | grep -c '\.gcda$') units)"
