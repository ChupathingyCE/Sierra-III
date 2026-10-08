#!/bin/bash
# One emulator run of the PSP build (PPSSPP headless, local patches).
# usage: emu.sh LABEL
# environment:
#   EB      EBOOT.PBP to run (default /tmp/claude-0/EBOOT101.PBP)
#   MAP     init.txt map line (default levels\c20\c20)
#   KEEP    keep words, space separated (default "nearauto nomeaudio")
#   TP      teleport.txt line (default none)
#   INPUT   input script file (default: none pressed)
#   WALL    wall seconds (default 200)
#   STRICT  1: the strict interpreter (-i): range, unaligned and FPU checks
#   FAT     1 (default): PSP-1000 memory (PPSSPP_FAT); 0: a Slim
#   MS      memory stick directory (default /tmp/ms)
#   EXTRA   more PPSSPPHeadless arguments
#   ROUTE   route.txt to replay (build 102)
#   PSP_MAXFD  descriptor limit of the patched emulator (13: a PSP-1000's; tools/ppsspp_local_patches.diff)
# results: /tmp/claude-0/r_LABEL.txt (emulator output), /tmp/claude-0/r_LABEL/ (log, profile, the
# animation trace if "animtrace" was kept, shots), and a one-line summary with the strict findings
# (tools/strictfilter.py)
L=$1
MS=${MS:-/tmp/ms}
EB=${EB:-/tmp/claude-0/EBOOT101.PBP}
cp "$EB" $MS/PSP/GAME/halo/EBOOT.PBP
printf "map_name %s\n" "${MAP:-levels\\c20\\c20}" > $MS/halo/init.txt
: > $MS/halo/keep.txt
for w in ${KEEP-nearauto nomeaudio}; do echo "$w" >> $MS/halo/keep.txt; done
if [ -n "$ROUTE" ]; then cp "$ROUTE" $MS/halo/route.txt; else rm -f $MS/halo/route.txt; fi
if [ -n "$TP" ]; then echo "$TP" > $MS/halo/teleport.txt; else rm -f $MS/halo/teleport.txt; fi
if [ -n "$INPUT" ]; then cp "$INPUT" $MS/halo/input.txt; else echo "0 0000 128 128" > $MS/halo/input.txt; fi
rm -f $MS/halo/log/log.txt $MS/halo/log/profile.txt $MS/halo/log/animtrace.bin $MS/halo/crash.txt; mkdir -p $MS/halo/shots /tmp/ms_old_shots; mv $MS/halo/shots/*.bmp /tmp/ms_old_shots/ 2>/dev/null
ARGS="--memstick=$MS --timeout-wall=${WALL:-200}"
[ "$STRICT" = 1 ] && ARGS="-i $ARGS"
FATV=1; [ "$FAT" = 0 ] && FATV=
(cd /tmp/ppsspp/build && env ${FATV:+PPSSPP_FAT=1} timeout $(( ${WALL:-200} + 120 )) ./PPSSPPHeadless $ARGS $EXTRA $MS/PSP/GAME/halo/EBOOT.PBP > /tmp/claude-0/r_$L.txt 2>&1)
rm -rf /tmp/claude-0/r_$L; mkdir -p /tmp/claude-0/r_$L
cp $MS/halo/log/log.txt $MS/halo/log/profile.txt $MS/halo/log/animtrace.bin $MS/halo/shots/*.bmp /tmp/claude-0/r_$L/ 2>/dev/null
[ -f $MS/halo/crash.txt ] && cp $MS/halo/crash.txt /tmp/claude-0/r_$L/
S=$(python3 /home/claude/tools/strictfilter.py /tmp/claude-0/r_$L.txt 2>&1 | tail -1)
T=$(grep -o "game time [-0-9]*" /tmp/claude-0/r_$L/log.txt 2>/dev/null | tail -1)
echo "$L: $(grep -o 'bring-up build [0-9]*' /tmp/claude-0/r_$L/log.txt 2>/dev/null | head -1); segv $(grep -ic sigsegv /tmp/claude-0/r_$L.txt); shots $(ls /tmp/claude-0/r_$L/*.bmp 2>/dev/null | wc -l); $T; strict: $S; crash.txt $( [ -f /tmp/claude-0/r_$L/crash.txt ] && echo yes || echo no)"
