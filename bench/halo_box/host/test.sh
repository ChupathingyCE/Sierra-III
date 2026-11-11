#!/bin/bash
# The harness's desktop test: builds the portable part natively and runs the
# self-test box over a good capture (must pass) and a spoiled one (must be
# caught), and the null box with no capture. bash bench/halo_box/host/test.sh
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -O2 -Wall -Wextra -fsanitize=address,undefined -I"$here/../../psp/src" -o "$out/host" "$here/host/host.c" "$here/harness.c" "$here/selftest_capture.c" "$here/adapters.c" "$here"/adapters/*.c "$here"/../../psp/src/box_*.c -lm
python3 "$here/../../tools/cap1.py" selftest "$out/good.cap" > /dev/null
python3 "$here/../../tools/cap1.py" selftest "$out/bad.cap" --wrong 7:13 > /dev/null
"$out/host" selftest "$out/good.cap" > "$out/good.txt" || { cat "$out/good.txt"; echo "FAILED: the good capture didn't pass"; exit 1; }
grep -q "1280 outputs compared, 0 outside" "$out/good.txt" || { cat "$out/good.txt"; echo "FAILED: the good capture's check line"; exit 1; }
if "$out/host" selftest "$out/bad.cap" > "$out/bad.txt"; then cat "$out/bad.txt"; echo "FAILED: the spoiled capture passed"; exit 1; fi
grep -q "the first is tick 210, entity 1013, element 3" "$out/bad.txt" || { cat "$out/bad.txt"; echo "FAILED: the spoiled output wasn't named"; exit 1; }
"$out/host" null > "$out/null.txt" || { cat "$out/null.txt"; echo "FAILED: the null box"; exit 1; }
# the capture the bench writes for itself on the PSP: read back, and its spoiled twin caught; cap1.py reads it too
"$out/host" --write "$out/own.cap" good && "$out/host" --write "$out/ownbad.cap" spoiled
"$out/host" selftest "$out/own.cap" > "$out/own.txt" || { cat "$out/own.txt"; echo "FAILED: the bench's own capture didn't pass"; exit 1; }
if "$out/host" selftest "$out/ownbad.cap" > "$out/ownbad.txt"; then cat "$out/ownbad.txt"; echo "FAILED: the bench's own spoiled capture passed"; exit 1; fi
grep -q "the first is tick 210, entity 1013, element 3" "$out/ownbad.txt" || { cat "$out/ownbad.txt"; echo "FAILED: the bench's own spoiled output wasn't named"; exit 1; }
python3 "$here/../../tools/cap1.py" info "$out/own.cap" > "$out/info.txt"; grep -q "32 ticks, 1280 entities" "$out/info.txt" || { echo "FAILED: cap1.py and the bench disagree on the format"; exit 1; }
head -c 200 "$out/good.cap" > "$out/cut.cap"
"$out/host" selftest "$out/cut.cap" > "$out/cut.txt" && { cat "$out/cut.txt"; echo "FAILED: a cut capture passed"; exit 1; }
# the posing box over a made-up capture whose outputs are the engine's walk restated (cap1.py posing): passes, and a spoiled one is caught
python3 "$here/../../tools/cap1.py" posing "$out/pose.cap" > /dev/null
python3 "$here/../../tools/cap1.py" posing "$out/posebad.cap" --wrong 5:17 > /dev/null
"$out/host" posing "$out/pose.cap" > "$out/pose.txt" || { cat "$out/pose.txt"; echo "FAILED: the posing box's made-up capture"; exit 1; }
grep -q "1456 outputs compared, 0 outside" "$out/pose.txt" || { cat "$out/pose.txt"; echo "FAILED: the posing box's check line"; exit 1; }
"$out/host" posing "$out/posebad.cap" > "$out/posebad.txt" || true
grep -q "the first is tick 150, entity 5017, element 17" "$out/posebad.txt" || { cat "$out/posebad.txt"; echo "FAILED: the posing box's spoiled output wasn't named"; exit 1; }
[[ ${1:-} == -v ]] && cat "$out/good.txt" "$out/bad.txt" "$out/null.txt" "$out/cut.txt"
echo "halo_box host test: passed (good capture, spoiled capture caught, null box, cut capture refused, the posing box over a made-up capture and its spoiled twin)"
