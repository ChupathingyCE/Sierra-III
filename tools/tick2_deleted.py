#!/usr/bin/env python3
"""tick2_deleted.py: is slice 1's deleted engine code absent from a build?

usage: tick2_deleted.py <link map> [--list tick2_deleted.txt] [--sha <build's sha>] [--quiet]

Reads the linker map psp_build.py writes (-Wl,-Map=halo.map; a batch's
linkmap = true returns it as build-N.linkmap.txt.gz, read gzipped or not) and
reports each name in tools/tick2_deleted.txt that the program still holds:
an input section .text[.hot|.unlikely|.startup].<name>[.isra.N|.constprop.N|
.part.N|.cold|.lto_priv.N] with a nonzero size, or a symbol line naming it.
The map's "Discarded input sections" part, before "Linker script and memory
map", is what --gc-sections dropped, so it isn't read.

Prints one line a present name (its group and task), then
    tick2_deleted.py <sha> <N> present of <M>
(the form ops/runs.py check reads in a kill-test line's deleted= field;
<sha> is --sha's, or "-" without it). Exit 0 if none is present, 1 if any
is, 2 if the map can't be read or holds no .text sections at all (so an
empty or wrong file is never "0 present").

A name absent from the map is deleted or inlined into a caller; the map
can't tell which. The new-only build poisons each name (done-when 2), which
the inlined case can't pass, so the two checks together are the proof."""
import gzip, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SUFFIX = r'(?:\.(?:isra|constprop|part|lto_priv|localalias)\.\d+|\.cold(?:\.\d+)?)*'
PREFIX = r'\.text(?:\.(?:hot|unlikely|startup|exit))?\.'


def read_list(path):
    entries = []
    with open(path) as f:
        for line in f:
            line = line.rstrip('\n')
            if not line.strip() or line.startswith('#'):
                continue
            parts = line.split('\t')
            if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', parts[0]):
                sys.exit('%s: not a symbol name: %r' % (path, parts[0]))
            entries.append((parts[0], parts[1] if len(parts) > 1 else '', parts[3] if len(parts) > 3 else ''))
    if not entries:
        sys.exit('%s: no names' % path)
    return entries


def open_map(path):
    with open(path, 'rb') as f:
        gz = f.read(2) == b'\x1f\x8b'
    return gzip.open(path, 'rt', errors='replace') if gz else open(path, errors='replace')


def present_names(path, wanted):
    """the wanted names the map's kept sections or symbols hold"""
    section = re.compile(r'^ ' + PREFIX + r'([A-Za-z_][A-Za-z0-9_]*?)' + SUFFIX + r'(?:\s|$)(.*)')
    sized = re.compile(r'\s*0x[0-9a-fA-F]+\s+(0x[0-9a-fA-F]+)\s+\S')
    symbol = re.compile(r'^\s+0x[0-9a-fA-F]+\s+([A-Za-z_][A-Za-z0-9_]*?)' + SUFFIX + r'\s*$')
    found, text_sections, kept, pending = set(), 0, False, None
    with open_map(path) as f:
        for line in f:
            if not kept:
                kept = line.startswith('Linker script and memory map')
                continue
            m = section.match(line)
            if m:
                text_sections += 1
                rest = m.group(2)
                if rest.strip():
                    n = sized.match(rest)
                    if n and int(n.group(1), 16) and m.group(1) in wanted:
                        found.add(m.group(1))
                    pending = None
                else:
                    pending = m.group(1)   # a long name wraps: its address and size are on the next line
                continue
            if pending:
                n = sized.match(line)
                if n and int(n.group(1), 16) and pending in wanted:
                    found.add(pending)
                pending = None
                continue
            s = symbol.match(line)
            if s and s.group(1) in wanted:
                found.add(s.group(1))
    if not kept:
        raise ValueError('no "Linker script and memory map" part: not a GNU ld map')
    if not text_sections:
        raise ValueError('no .text.<name> sections: not a -ffunction-sections build\'s map')
    return found


def main(argv):
    args = argv[1:]
    def option(name, default=None):
        if name in args:
            i = args.index(name)
            if i + 1 >= len(args):
                sys.exit('%s needs a value' % name)
            value = args[i + 1]
            del args[i:i + 2]
            return value
        return default
    list_path = option('--list', os.path.join(HERE, 'tick2_deleted.txt'))
    sha = option('--sha', '-')
    quiet = '--quiet' in args
    if quiet:
        args.remove('--quiet')
    if len(args) != 1 or args[0].startswith('-'):
        sys.stderr.write(__doc__)
        return 2
    if sha != '-' and not re.fullmatch(r'[0-9a-f]{40}', sha):
        sys.exit('--sha wants the build\'s full 40-hex sha')
    entries = read_list(list_path)
    try:
        found = present_names(args[0], {name for name, _, _ in entries})
    except (OSError, ValueError, EOFError) as e:
        print('tick2_deleted.py: %s: %s' % (args[0], e), file=sys.stderr)
        return 2
    if not quiet:
        for name, group, task in entries:
            if name in found:
                print('present\t%s\t%s\ttask %s' % (name, group, task))
    print('tick2_deleted.py %s %d present of %d' % (sha, len(found), len(entries)))
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
