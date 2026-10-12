#!/bin/bash
# Assembles halo-psp-bake/ (and a .tar.gz): the bake's scripts from this checkout,
# Invader's binaries from a built fork.
# usage: make_bundle.sh [OUT]
# environment:
#   INVADER    the Invader fork checkout, built in INVADER/build (default /home/claude/invader);
#              upstream d43ce55 + upstream/invader/psp-fork.patch
#   HALO_REPO  the engine checkout, for its licence (default /tmp/halo-sp)
#   ATRACDENC  an atracdenc checkout built in ATRACDENC/build (the menu sound's encoder;
#              github.com/dcherednik/atracdenc, cb17070). Unset: the bundle has none, and
#              the bake uses atracdenc from PATH or skips the menu sound
set -e
OUT=${1:-/tmp/claude-0/halo-psp-bake}
P=$(cd "$(dirname "$0")/.." && pwd)
INVADER=${INVADER:-/home/claude/invader}
HALO_REPO=${HALO_REPO:-/tmp/halo-sp}
rm -rf $OUT; mkdir -p $OUT/lib $OUT/invader/bin $OUT/invader/lib $OUT/invader/definitions
cp $P/bake/bake.py $P/bake/xiso.py $P/bake/halo-psp-bake $P/bake/README.md $P/bake/INVADER.md $P/bake/hashes.json $OUT/
cp $P/tools/texpack.py $P/tools/stencilbake.py $P/tools/dez.py $P/tools/lightmap_masks.py $P/tools/bspread.py $OUT/lib/
cp $P/bake/lib/snd0.py $P/bake/lib/halomap.py $P/bake/lib/assets.py $P/bake/lib/refs.py $P/bake/lib/bitmap_bake.py $OUT/lib/
# the bundle's own lib first: no paths into this machine
sed -i "/'bake', 'lib'))  # the bake's readers/d" $OUT/lib/*.py
B=$INVADER/build
cp $B/invader-build $B/invader-extract $B/invader-info $OUT/invader/bin/
cp $B/libinvader.so $OUT/invader/lib/
for lib in libFLAC.so.12 libvorbisenc.so.2 libvorbisfile.so.3 libvorbis.so.0 libogg.so.0 libsquish.so.0 libgomp.so.1; do
  src=$(ldd $B/invader-build | awk -v l=$lib '$1==l {print $3}'); [ -n "$src" ] && cp -L "$src" $OUT/invader/lib/
done
cp $INVADER/src/tag/hek/definition/*.json $OUT/invader/definitions/
cp $INVADER/include/invader/hek/fourcc.hpp $OUT/invader/definitions/
(cd $(dirname $INVADER) && tar czf $OUT/invader-src.tar.gz --exclude=$(basename $INVADER)/build $(basename $INVADER))
# the working tree against upstream, so changes not yet committed are in the patch too
(cd $INVADER && git diff d43ce55 > $OUT/invader-psp-fork.patch) || true
if [ -n "$ATRACDENC" ]; then
  mkdir -p $OUT/atracdenc/bin $OUT/atracdenc/lib
  A=$ATRACDENC/build/src/atracdenc
  cp $A $OUT/atracdenc/bin/
  # libsndfile and its codecs, beside it; the C and C++ runtimes come from the system
  ldd $A | awk '$3 ~ /^\// {print $3}' | grep -v -E '/(libc|libm|libstdc\+\+|libgcc_s|ld-linux[^/]*)\.so' |
    while read -r lib; do cp -L "$lib" $OUT/atracdenc/lib/; done
  (cd $(dirname $ATRACDENC) && tar czf $OUT/atracdenc-src.tar.gz --exclude=$(basename $ATRACDENC)/build $(basename $ATRACDENC))
fi
mkdir -p $OUT/LICENSES; [ -n "$ATRACDENC" ] && cp $ATRACDENC/LICENSE $OUT/LICENSES/atracdenc-LICENSE.txt; cp $INVADER/LICENSE.md $OUT/LICENSES/invader-LICENSE.md; cp $INVADER/gplv3.txt $OUT/LICENSES/ 2>/dev/null || true; cp $HALO_REPO/LICENSE.md $OUT/LICENSES/engine-LICENSE.md
(cd $(dirname $OUT) && tar czf $(basename $OUT).tar.gz $(basename $OUT))
du -sh $OUT $OUT.tar.gz
