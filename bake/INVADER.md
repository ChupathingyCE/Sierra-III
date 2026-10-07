# Building the port's Invader from source

The bundle's Invader binaries need glibc 2.39 or later. On an older system,
build them from `invader-src.tar.gz` (the fork's full source, with its git
history):

```
tar xzf invader-src.tar.gz
cd invader && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4 invader-build invader-extract invader-info
cp invader-build invader-extract invader-info ../../invader/bin/
cp libinvader.so ../../invader/lib/
```

Upstream Invader's build wants (Debian/Ubuntu names): cmake, g++ (C++20),
python3, zlib1g-dev, libflac-dev, libvorbis-dev, libogg-dev, libsamplerate0-dev,
libsquish-dev, qt6-base-dev, libsdl2-dev, and a Rust toolchain (cargo) for its
"Rat In a Tube" part. libtiff, freetype and libarchive are optional.

The fork adds to upstream Invader (SnowyMouse/invader, commit d43ce55):
`-X` (tag address), `-G` (model and level geometry to a separate pack), `-K`
(keyframed animations), packed collision, and Windows-1252 script sources.
`invader-psp-fork.patch` is the whole change.
