#!/usr/bin/env python3
"""hek_census.py - where a tag's bytes are, block by block (2026-10-09).

Reads a tag file as invader-extract writes it (the HEK format: a 64-byte
header, then the main struct big-endian, then each struct's children after
its whole array: dependency paths, data blobs, then child arrays, in field
order, element by element) with Invader's own tag definitions, and prints
the bytes under every block path: the elements (count x struct size) and the
data blobs, summed over every occurrence.

    python3 hek_census.py DEFINITIONS_DIR TAG_FILE [--top N] [--depth D]

DEFINITIONS_DIR: invader's src/tag/hek/definition. The struct sizes the
definitions give are checked against the fields' sum, so a misread type
shows up as an error rather than as wrong numbers.

The sizes are the tag file's, which match the cache's for the structs (same
layout in Halo CE). What the PSP bake changes on top (invader-build's -G
moves the BSP's material vertices and the models' geometry out, packed
collision halves most collision tables) is for the reader to apply; the
report marks the blocks it touches."""
import glob
import json
import os
import struct
import sys
from collections import defaultdict

PRIMITIVES = {
    'int8': 1, 'uint8': 1, 'int16': 2, 'uint16': 2, 'int32': 4, 'uint32': 4,
    'float': 4, 'Angle': 4, 'Fraction': 4, 'Index': 2, 'Pointer': 4, 'TagID': 4,
    'TagFourCC': 4, 'ColorARGBInt': 4, 'Point2DInt': 4, 'Rectangle2D': 8,
    'Point2D': 8, 'Vector2D': 8, 'Euler2D': 8, 'Plane2D': 12,
    'Point3D': 12, 'Vector3D': 12, 'Euler3D': 12, 'ColorRGB': 12,
    'Plane3D': 16, 'Quaternion': 16, 'ColorARGB': 16, 'Matrix': 36,
    'TagString': 32, 'TagDependency': 16, 'TagReflexive': 12, 'TagDataOffset': 20,
    'ScenarioScriptNodeValue': 4,
}

# blocks the PSP bake changes (for the report's marks)
BAKE_NOTES = {
    'collision_bsp': 'packed (16-bit tables, about -40%)',
    'uncompressed_vertices': 'moved out by -G (.pgeo)',
    'compressed_vertices': 'moved out by -G (.pgeo)',
}


def snake(name):
    return name.replace(' ', '_')


class Definitions:
    def __init__(self, folder):
        self.structs, self.enums, self.bitfields = {}, {}, {}
        for path in glob.glob(os.path.join(folder, '*.json')):
            for item in json.load(open(path)):
                if item['type'] == 'struct':
                    self.structs[item['name']] = item
                elif item['type'] == 'enum':
                    self.enums[item['name']] = item
                elif item['type'] == 'bitfield':
                    self.bitfields[item['name']] = item
        self.layouts = {}

    def field_size(self, field):
        kind = field['type']
        if kind == 'pad':
            return field['size']
        if kind in PRIMITIVES:
            size = PRIMITIVES[kind]
        elif kind in self.enums:
            size = 2
        elif kind in self.bitfields:
            size = self.bitfields[kind]['width'] // 8
        elif kind in self.structs:
            size = self.size(kind)
        else:
            raise KeyError('unknown field type %s' % kind)
        if field.get('bounds'):
            size *= 2
        return size * field.get('count', 1)

    def layout(self, name):
        """the struct's fields with their offsets, inherited ones first"""
        if name in self.layouts:
            return self.layouts[name]
        struct_def = self.structs[name]
        fields, offset = [], 0
        if 'inherits' in struct_def:
            fields = list(self.layout(struct_def['inherits']))
            offset = self.size(struct_def['inherits'])
        for field in struct_def['fields']:
            size = self.field_size(field)
            fields.append((offset, field))
            offset += size
        if 'size' in struct_def and struct_def['size'] != offset:
            raise ValueError('%s: fields sum to %d, the definition says %d' % (name, offset, struct_def['size']))
        self.layouts[name] = fields
        self.layouts[name + '#size'] = offset
        return fields

    def size(self, name):
        if name + '#size' not in self.layouts:
            self.layout(name)
        return self.layouts[name + '#size']


class Census:
    def __init__(self, definitions, data):
        self.d = definitions
        self.data = data
        self.at = 64
        self.elements = defaultdict(int)   # path -> element bytes
        self.counts = defaultdict(int)     # path -> elements
        self.blobs = defaultdict(int)      # path -> data blob bytes

    def walk_children(self, name, base, path, embedded=False):
        """the children of one element whose struct starts at base, in the
        order the HEK format writes them; returns nothing, advances self.at"""
        for offset, field in self.d.layout(name):
            kind = field['type']
            at = base + offset
            field_path = path + '.' + snake(field.get('name', 'pad'))
            if kind == 'TagDependency':
                length = struct.unpack_from('>I', self.data, at + 8)[0]
                if length:
                    self.at += length + 1
            elif kind == 'TagDataOffset':
                size = struct.unpack_from('>I', self.data, at)[0]
                self.blobs[field_path] += size
                self.at += size
            elif kind == 'TagReflexive':
                count = struct.unpack_from('>I', self.data, at)[0]
                child = field['struct']
                child_size = self.d.size(child)
                start = self.at
                self.at += count * child_size
                self.elements[field_path] += count * child_size
                self.counts[field_path] += count
                for i in range(count):
                    self.walk_children(child, start + i * child_size, field_path)
            elif kind in self.d.structs:
                # an embedded struct: its children come in line
                for i in range(field.get('count', 1)):
                    self.walk_children(kind, at + i * self.d.size(kind), field_path, True)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    definitions = Definitions(sys.argv[1])
    data = open(sys.argv[2], 'rb').read()
    top = int(sys.argv[sys.argv.index('--top') + 1]) if '--top' in sys.argv else 60
    depth = int(sys.argv[sys.argv.index('--depth') + 1]) if '--depth' in sys.argv else 99
    fourcc = data[0x24:0x28].decode('latin-1')
    # the main struct: the definitions name it by the class; find the one whose
    # name matches the file's extension
    extension = os.path.splitext(sys.argv[2])[1][1:]
    candidates = [n for n in definitions.structs
                  if n.lower() == extension.replace('_', '').lower()]
    if not candidates:
        sys.exit('no struct for %s (%s)' % (extension, fourcc))
    main_name = candidates[0]
    census = Census(definitions, data)
    main_size = definitions.size(main_name)
    census.at = 64 + main_size
    census.walk_children(main_name, 64, '')
    total = len(data) - 64
    walked = census.at - 64
    print('%s (%s): %d bytes after the header; walked %d (%s)' % (os.path.basename(sys.argv[2]), main_name, total,
          walked, 'all of it' if walked == total else 'MISMATCH: %+d' % (walked - total)))
    rows = defaultdict(lambda: [0, 0, 0])
    for path, value in census.elements.items():
        rows[path][0] += value
        rows[path][2] += census.counts[path]
    for path, value in census.blobs.items():
        rows[path][1] += value
    # roll children up into their top-level block for a summary
    summary = defaultdict(int)
    for path, (elements, blobs, count) in rows.items():
        summary[path.split('.')[1]] += elements + blobs
    print('\nby top-level block (everything under it), bytes:')
    for name, value in sorted(summary.items(), key=lambda x: -x[1])[:top]:
        note = next((v for k, v in BAKE_NOTES.items() if k in name), '')
        print('  %10d  %5.1f%%  %s  %s' % (value, 100.0 * value / total, name, note))
    print('\nby block path (its own elements and blobs), bytes:')
    for path, (elements, blobs, count) in sorted(rows.items(), key=lambda x: -(x[1][0] + x[1][1]))[:top]:
        if path.count('.') > depth:
            continue
        note = next((v for k, v in BAKE_NOTES.items() if k in path), '')
        print('  %10d  elements %9d (%7d)  data %9d  %s  %s' % (elements + blobs, elements, count, blobs, path, note))


if __name__ == '__main__':
    main()
