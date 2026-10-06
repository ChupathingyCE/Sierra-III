#!/bin/bash
# runs the current EBOOT in PPSSPP's interpreter (which reports unaligned
# accesses the JIT forgives) and names the faulting functions
export PATH=/opt/psp/pspdev/bin:$PATH
cp /tmp/psp_halo/EBOOT.PBP /tmp/ms/PSP/GAME/halo/
rm -f /tmp/ms/halo/log.txt /tmp/ms/halo/debug.txt
cd /tmp/ppsspp/build && timeout ${2:-150} ./PPSSPPHeadless -i --memstick=/tmp/ms --timeout-wall=${1:-120} /tmp/ms/PSP/GAME/halo/EBOOT.PBP > /tmp/hl_i.out 2>&1
grep -E "SIGSEGV|Bad memory" /tmp/hl_i.out | head -3
grep -oE "\(pc: [0-9a-f]+" /tmp/hl_i.out | head -8 | awk '{print $2}' | while read pc; do
  off=$(printf '0x%x' $((0x$pc - 0x08804000)))
  echo "  $(psp-addr2line -f -e /tmp/psp_halo/halo.elf $off | paste -sd' ')"
done
echo "-- debug.txt:"; tail -3 /tmp/ms/halo/debug.txt
