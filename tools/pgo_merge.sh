#!/usr/bin/env bash
# pgo_merge.sh - merges the counts of several PSP_PGO=generate runs into one
# profile for PSP_PGO=use: pgo/<name>.tgz.
#
#   tools/pgo_merge.sh <name> <gcda.tgz> [<gcda.tgz> ...]
#   tools/pgo_merge.sh campaign-b119 runs/*/gcda.tgz
#   tools/pgo_merge.sh campaign-rc1 a.tgz:0.5 b.tgz:2   (each run's counts scaled)
#
# Each counting run writes its counts (one .gcda file a unit: how often each
# branch went each way) at its gcovdumpN second; the lab returns them as
# gcda.tgz. psp-gcov-tool's merge adds two runs' counts, so the runs are
# folded in one at a time, and each stretch weighs in proportion to how long
# it ran, unless a weight follows its name (<gcda.tgz>:<w>): the recipe's
# fight windows are weighted by window, not by seconds, so a long window
# doesn't outweigh a short one. Count at the build's own commit: GCC matches a function's counts by
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

# Weights. psp-gcov-tool's merge -w takes whole numbers (w1,w2), and the first
# set's weight must stay 1: with w1 above 1 it first merges the first set into
# itself (w1-1 times), which turns its top-N value counters into the in-memory
# form, then reads them again as on-disk data, and stops in
# gcov_read_counter_mem (an internal error, GCC 15.2; pgo-merge 45463888645).
# Its rewrite -s fails on the same counters (__gcov_topn_counter_op). So each
# weight becomes a whole number of units (a tenth of the smallest weight), the
# lightest run goes first at 1 and is merged with a copy of itself for its
# other units, and every other run comes in as the second set with its units.
specs=() weights=() min=
for spec in "$@"; do
    archive=$spec weight=1
    if [[ $spec =~ ^(.*):([0-9]+(\.[0-9]+)?)$ ]]; then archive=${BASH_REMATCH[1]} weight=${BASH_REMATCH[2]}; fi
    awk -v w="$weight" 'BEGIN { exit !(w > 0) }' || { echo "pgo_merge: $archive: a weight must be above 0" >&2; exit 1; }
    specs+=("$archive") weights+=("$weight")
    if [[ -z $min ]] || awk -v a="$weight" -v b="$min" 'BEGIN { exit !(a < b) }'; then min=$weight; fi
done
weighted=0
for weight in "${weights[@]}"; do [[ $weight == "$min" ]] || weighted=1; done
units() {   # <weight>: whole units of a tenth of the smallest weight (10 for the smallest); 1 each when all weigh the same
    if (( weighted )); then awk -v w="$1" -v m="$min" 'BEGIN { n = int(w * 10 / m + 0.5); print (n < 1 ? 1 : n) }'; else echo 1; fi
}
first=0
for ((n = 1; n < ${#specs[@]}; n++)); do
    awk -v a="${weights[n]}" -v b="${weights[first]}" 'BEGIN { exit !(a < b) }' && first=$n
done
order=("$first")
for ((n = 0; n < ${#specs[@]}; n++)); do (( n == first )) || order+=("$n"); done
i=0
for n in "${order[@]}"; do
    i=$((i + 1))
    u=$(units "${weights[n]}")
    mkdir "$work/run$i"
    tar xzf "${specs[n]}" -C "$work/run$i"
    if (( i == 1 )); then
        mv "$work/run1" "$work/merged"
        if (( u > 1 )); then
            cp -r "$work/merged" "$work/self"
            psp-gcov-tool merge -w "1,$((u - 1))" "$work/merged" "$work/self" -o "$work/next"
            rm -rf "$work/merged" "$work/self"; mv "$work/next" "$work/merged"
        fi
    else
        psp-gcov-tool merge -w "1,$u" "$work/merged" "$work/run$i" -o "$work/next"
        rm -rf "$work/merged" "$work/run$i"; mv "$work/next" "$work/merged"
    fi
    echo "pgo_merge: $i: ${specs[n]} (weight ${weights[n]}, $u units)"
done

mkdir -p "$here/pgo"
(cd "$work/merged" && tar czf "$here/pgo/$name.tgz" -- *)   # names as the earlier profiles have them, no ./
echo "pgo_merge: pgo/$name.tgz from $i runs ($(tar tzf "$here/pgo/$name.tgz" | grep -c '\.gcda$') units)"
