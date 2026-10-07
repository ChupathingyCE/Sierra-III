"""xiso.py - read files out of an Xbox disc image (XDVDFS), pure Python.

Takes either a bare game partition ("XISO", what extract-xiso and most
dumping tools make) or a full Redump-style image, whose game partition starts
0x18300000 bytes in. Only reading: list the tree, copy files out.

usage (library):
    with XIso(path) as iso:
        for path, size in iso.files():     # 'maps/c20.map', ...
            ...
        iso.extract('maps/c20.map', 'out/c20.map')"""
import os, struct

SECTOR = 2048
MAGIC = b'MICROSOFT*XBOX*MEDIA'
PARTITION_OFFSETS = (0, 0x18300000, 0xFD90000, 0x89D80000, 0x2080000)


class XIso:
    def __init__(self, path):
        self.f = open(path, 'rb')
        self.base = None
        for offset in PARTITION_OFFSETS:
            self.f.seek(offset + 32 * SECTOR)
            if self.f.read(len(MAGIC)) == MAGIC:
                self.base = offset
                break
        if self.base is None:
            self.f.close()
            raise ValueError('%s: not an Xbox disc image (no XDVDFS volume found)' % path)
        self.f.seek(self.base + 32 * SECTOR + 20)
        self.root_sector, self.root_size = struct.unpack('<II', self.f.read(8))

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.f.close()

    def _read(self, sector, size):
        self.f.seek(self.base + sector * SECTOR)
        return self.f.read(size)

    def _walk(self, sector, size, prefix):
        table = self._read(sector, size)
        out = []
        stack = [0]
        seen = set()
        while stack:
            offset = stack.pop()
            if offset in seen or offset + 14 > len(table):
                continue
            seen.add(offset)
            left, right, start, length, attributes, name_length = struct.unpack_from('<HHIIBB', table, offset)
            if left == 0xFFFF and right == 0xFFFF:
                continue
            name = table[offset + 14: offset + 14 + name_length].decode('latin-1')
            path = prefix + name
            if attributes & 0x10:
                if length:
                    out += self._walk(start, length, path + '/')
            else:
                out.append((path, start, length))
            if left and left != 0xFFFF:
                stack.append(left * 4)
            if right and right != 0xFFFF:
                stack.append(right * 4)
        return out

    def files(self):
        """[(path, size)] with '/' separators"""
        return [(p, n) for p, s, n in self._walk(self.root_sector, self.root_size, '')]

    def extract(self, path, destination):
        for p, start, length in self._walk(self.root_sector, self.root_size, ''):
            if p.lower() == path.lower():
                os.makedirs(os.path.dirname(os.path.abspath(destination)), exist_ok=True)
                with open(destination, 'wb') as out:
                    self.f.seek(self.base + start * SECTOR)
                    left = length
                    while left:
                        chunk = self.f.read(min(left, 4 << 20))
                        if not chunk:
                            raise IOError('%s: image ends inside %s' % (destination, path))
                        out.write(chunk)
                        left -= len(chunk)
                return length
        raise FileNotFoundError(path)


def make_test_image(path, files):
    """a minimal XDVDFS image of {name: bytes} in one folder 'maps' (for
    testing the reader; directory entries in a degenerate right-leaning tree)"""
    names = sorted(files)
    data_sector = 40
    entries = []
    layout = []
    sector = data_sector
    for n in names:
        layout.append((n, sector, len(files[n])))
        sector += (len(files[n]) + SECTOR - 1) // SECTOR
    def table(items):
        blob = bytearray()
        offsets = []
        for i, (n, s, l, attr) in enumerate(items):
            offsets.append(len(blob))
            e = struct.pack('<HHIIBB', 0, 0, s, l, attr, len(n)) + n.encode()
            while len(e) % 4:
                e += b'\xff'
            blob += e
        for i in range(len(items) - 1):
            struct.pack_into('<H', blob, offsets[i] + 2, offsets[i + 1] // 4)
        while len(blob) % SECTOR:
            blob += b'\xff'
        return bytes(blob)
    maps_table = table([(n, s, l, 0x20) for n, s, l in layout])
    maps_sector = 36
    root_table = table([('maps', maps_sector, len(maps_table), 0x10)])
    root_sector = 34
    image = bytearray(sector * SECTOR)
    vd = MAGIC + struct.pack('<II', root_sector, len(root_table))
    image[32 * SECTOR: 32 * SECTOR + len(vd)] = vd
    image[32 * SECTOR + 0x7EC: 32 * SECTOR + 0x7EC + len(MAGIC)] = MAGIC
    image[root_sector * SECTOR: root_sector * SECTOR + len(root_table)] = root_table
    image[maps_sector * SECTOR: maps_sector * SECTOR + len(maps_table)] = maps_table
    for n, s, l in layout:
        image[s * SECTOR: s * SECTOR + l] = files[n]
    open(path, 'wb').write(bytes(image))


if __name__ == '__main__':
    import sys, tempfile
    if len(sys.argv) > 1:
        with XIso(sys.argv[1]) as iso:
            for p, n in sorted(iso.files()):
                print('%12d  %s' % (n, p))
    else:
        d = tempfile.mkdtemp()
        files = {'c20.map': os.urandom(5000), 'ui.map': b'hello' * 1000, 'a10.map': b''}
        make_test_image(os.path.join(d, 't.iso'), files)
        with XIso(os.path.join(d, 't.iso')) as iso:
            listed = dict(iso.files())
            assert set(listed) == {'maps/' + n for n in files}, listed
            for n, b in files.items():
                iso.extract('maps/' + n, os.path.join(d, n))
                assert open(os.path.join(d, n), 'rb').read() == b
        print('xiso self-test passed')
