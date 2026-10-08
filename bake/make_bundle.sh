#!/bin/bash
# assembles halo-psp-bake/ (and a .tar.gz) from this machine's pieces
set -e
OUT=${1:-/tmp/claude-0/halo-psp-bake}
P=/home/claude/psp_port
rm -rf $OUT; mkdir -p $OUT/lib $OUT/invader/bin $OUT/invader/lib $OUT/invader/definitions
cp $P/bake/bake.py $P/bake/xiso.py $P/bake/halo-psp-bake $P/bake/README.md $P/bake/INVADER.md $P/bake/hashes.json $OUT/
cp $P/tools/texpack.py $P/tools/stencilbake.py $P/tools/dez.py $P/tools/lightmap_masks.py $P/tools/bspread.py $OUT/lib/
for f in halomap.py assets.py refs.py bitmap_bake.py; do cp /home/claude/research/baker/$f $OUT/lib/; done
# the bundle's own lib first: no paths into this machine
sed -i "/sys.path.insert(0, '\/home\/claude\/research\/baker')/d" $OUT/lib/*.py
B=/home/claude/invader/build
cp $B/invader-build $B/invader-extract $B/invader-info $OUT/invader/bin/
cp $B/libinvader.so $OUT/invader/lib/
for lib in libFLAC.so.12 libvorbisenc.so.2 libvorbisfile.so.3 libvorbis.so.0 libogg.so.0 libsquish.so.0 libgomp.so.1; do
  src=$(ldd $B/invader-build | awk -v l=$lib '$1==l {print $3}'); [ -n "$src" ] && cp -L "$src" $OUT/invader/lib/
done
cp /home/claude/invader/src/tag/hek/definition/*.json $OUT/invader/definitions/
cp /home/claude/invader/include/invader/hek/fourcc.hpp $OUT/invader/definitions/
(cd /home/claude && tar czf $OUT/invader-src.tar.gz --exclude=invader/build invader)
# the working tree against upstream, so changes not yet committed are in the patch too
(cd /home/claude/invader && git diff d43ce55 > $OUT/invader-psp-fork.patch) || true
mkdir -p $OUT/LICENSES; cp /home/claude/invader/LICENSE.md $OUT/LICENSES/invader-LICENSE.md; cp /home/claude/invader/gplv3.txt $OUT/LICENSES/ 2>/dev/null || true; cp /tmp/halo-sp/LICENSE.md $OUT/LICENSES/halo-ce-universal-CC0.md
(cd $(dirname $OUT) && tar czf $(basename $OUT).tar.gz $(basename $OUT))
du -sh $OUT $OUT.tar.gz
