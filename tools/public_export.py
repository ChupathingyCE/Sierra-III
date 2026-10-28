#!/usr/bin/env python3
"""Builds the public release's tree from one commit of this repository.

    tools/public_export.py <commit> <out folder> [--pgo NAME] [--gitleaks PATH]
    tools/public_export.py - x --report      (the work tree as it is on disk)
                           [--report] [--commit-message TEXT]

The public repository is a separate one with a history of its own: one commit
per release, each a copy of the tree this writes (docs/public_release.md).
From <commit> it takes only what a player or a porter needs (RULES below:
every tracked file must match one, so a new folder is a decision, not an
accident), then refuses the export if anything in it

  - matches the deny-list (DENY: personal names and addresses, the test
    hardware's and machines' names, the development infrastructure), in any
    text file, in any binary's readable strings, and in the names and contents
    of the archives inside it;
  - is a Markdown link to a file the export leaves out;
  - is a file type that has no business there (game data, firmware);
  - is a secret, by gitleaks (`gitleaks dir`; --gitleaks names the program,
    else it must be on PATH).

A refusal names every file and line. The fix goes into this repository's own
text (neutral wording), never into the exported copy, so the next export
passes by itself. --report lists every finding and writes nothing.

<out folder> must not exist and must not be inside a git work tree. With
--commit-message it is made a git repository holding one commit of the tree,
authored as PUBLIC_IDENTITY; nothing is pushed.
"""
import argparse
import fnmatch
import io
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

# the public history's author and committer (docs/public_release.md)
PUBLIC_IDENTITY = ('Sierra-III', 'noreply@sierra-iii.invalid')

# ---------------------------------------------------------------------------
# What goes out. First match wins; a tracked file matching none fails the
# export. Each rule: (glob, keep, why). Globs match the whole path; "**"
# matches across folders.
# ---------------------------------------------------------------------------
RULES = [
    # the development process's own files
    ('CLAUDE.md', False, 'working rules for the development sessions'),
    ('tools/public_export.py', False, 'this tool: its deny-list names what must not go out'),
    ('docs/public_release.md', False, 'how the export is made, for the maintainers'),

    # public/ holds the public tree's own versions of files the development
    # tree also has: public/README.md goes out as README.md
    ('public/**', True, 'the public versions (OVERLAY)'),
    ('README.md', False, 'the development README; public/README.md replaces it'),

    # the build: what makes the EBOOT
    ('UPSTREAM.md', True, ''),
    ('LICENSE', True, ''),
    ('.gitignore', True, ''),
    ('.gitattributes', True, ''),
    ('psp_build.py', True, 'the build'),
    ('psp_fixups.py', True, 'the build: every change to the engine'),
    ('psp_*.h', True, 'the build'),
    ('psp/**', True, 'the PSP platform layer, the helper modules, the XMB art and its scripts'),
    ('upstream/**', True, 'the pins and our patches to Invader'),
    ('pgo/*.tgz', None, 'the profile-guided build: only the profile the release names (--pgo)'),
    ('tools/build_release.sh', True, 'the release build'),

    # the bake: what a player runs on their own disc
    ('bake/**', True, 'the bake and its bundle'),
    ('.github/workflows/bake-bundle.yml', True, 'builds the bake bundles on GitHub\'s runners'),
    ('tools/texpack.py', True, 'the bake runs it'),
    ('tools/stencilbake.py', True, 'the bake runs it'),
    ('tools/dez.py', True, 'the bake runs it'),
    ('tools/lightmap_masks.py', True, 'the bake runs it'),
    ('tools/bspread.py', True, 'the bake runs it'),
    ('tools/psp_glyphs.py', True, 'draws psp/glyphs/'),
    ('tools/tag_groups_check.py', True, 'proves the bake\'s tag group table'),

    # testing in the emulator (docs/toolchain.md)
    ('tools/emu.sh', True, 'one emulator run'),
    ('tools/strictfilter.py', True, 'reads an emulator run'),
    ('tools/ppsspp_BASE_COMMIT.txt', True, 'the emulator\'s pin'),
    ('tools/ppsspp_local_patches.diff', True, 'the emulator\'s patches'),
    ('bench/**', True, 'the micro-benchmarks behind docs/psp_hardware_reference.md'),
    ('tools/**', False, 'analysis tools for the development runs'),

    # documents: the player's and the porter's
    ('docs/play.md', True, 'the player\'s guide'),
    ('docs/toolchain.md', True, 'building from a clean machine'),
    ('docs/for_other_ports/**', True, 'what other ports can take'),
    ('docs/port_overview.md', True, 'how the port is put together'),
    ('docs/asset_baking.md', True, 'how the bake works'),
    ('docs/bake_changes.md', True, 'the bake\'s changes, for anyone re-baking'),
    ('docs/psp_hardware_reference.md', True, 'the PSP-1000\'s measured costs'),
    ('docs/release_notes_b143.md', True, 'the release\'s notes'),
    ('docs/release_notes_b147h.md', True, 'the release\'s notes'),
    ('docs/research/research_vita.md', True, 'provenance: halo-ce-vita\'s grant (README Credits)'),
    ('docs/research/research_halo64.md', True, 'provenance: Halo 64 (README Credits)'),
    ('docs/research/chant_provenance.md', True, 'provenance: the loading chant'),
    ('docs/**', False, 'development history: build notes, hardware reports, reviews, plans, recon'),
]

# ---------------------------------------------------------------------------
# The deny-list: (label, regex). Case-insensitive. Each finding fails the
# export, wherever it is, with one exception below (ALLOW).
# ---------------------------------------------------------------------------
DENY = [
    ('a person\'s name', r'\bsam\b|\buser\b|[name removed]'),
    ('a personal address', r'[address removed]|@gmail\.com|15330660|sheepdog-01@users'),
    ('a home path', r'/home/(?!claude/)\w+|/Users/\w+|~'),
    ('a place', r'\b[place removed]\b|[place removed]'),
    ('a machine', r'(?<![0-9a-z])x3d\b|9800x3d|sheepcube|cachyos|\bai[ -]?box\b|\bp14s\b|7840u|gpd win|steam ?deck'),
    ('a console\'s ID', r'[console id]|[console id]'),
    ('the development infrastructure',
     r'lab-repo|lab-repo|\blab\'s\b|\b(?:the|our|a) lab\b|\blab$|'
     r'\blab\b(?=[ ,(]*(?:run|batch|build|repo|runner|sierra-|pc-|heapladder|ld1why|p3walk|\d{6,}))|\brunners?\b|\bemupool\b|'
     r'x3d-psp|aibox-p|\bpsprig\b|sheep-psp-tools|\bcoord/|'
     r'\bsierra-(?:hw|batch|build|bake|test|netplay|tags)\b'),
    ('a USB port', r'\busb ?(?:port|socket|hub) ?\d|\b\d-\d(?:\.\d)+:1\.\d\b'),
    ('a network detail', r'\b10\.42\.0\.|\bpsp-(?:lan|open|ap)\b|\b192\.168\.\d+\.\d+|\b10\.\d+\.\d+\.\d+\b|'
                         r'\b(?!ff:ff:ff:ff:ff:ff)(?!12:34:56:78:9a:bc)(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b'),
    ('an Actions run', r'\b3[6-9]\d{9}\b'),
]

OVERLAY = 'public/'

# Markdown files whose links resolve from the repository's root, not their own
# folder: the release notes are the GitHub Release's text
ROOT_LINKS = ['docs/release_notes_*.md']

# third-party texts carried as they are: their authors' own addresses, and
# GitHub's own runners. (glob, label)
ALLOW = [
    ('bake/bundle/licenses/**', 'a personal address'),
    ('psp/xmb/fonts/OFL.txt', 'a personal address'),
    ('.github/workflows/bake-bundle.yml', 'the development infrastructure'),
]

# file types that never go out (game data, firmware, build output)
REFUSED_TYPES = ('.map', '.pgeo', '.ptex', '.pstn', '.adp', '.vram', '.xbe', '.iso', '.prx',
                 '.PBP', '.elf', '.bin', '.at3', '.AT3', '.bitmap', '.sound', '.gbxmodel')

TEXT_LIMIT = 16 << 20


def glob_match(path, pattern):
    rx = re.escape(pattern).replace(r'\*\*', '\0').replace(r'\*', '[^/]*').replace('\0', '.*')
    return re.fullmatch(rx, path) is not None


def rule_for(path):
    for pattern, keep, why in RULES:
        if glob_match(path, pattern):
            return pattern, keep, why
    return None


def git(*args, **kw):
    return subprocess.run(['git', '-C', REPO] + list(args), check=True, capture_output=True, **kw).stdout


def tracked(commit):
    if commit is None:   # the work tree as it is on disk (--report only)
        return [p for p in git('ls-files', '-z').decode().split('\0') if p and os.path.exists(os.path.join(REPO, p))]
    return [p for p in git('ls-tree', '-r', '-z', '--name-only', commit).decode().split('\0') if p]


def read(commit, path):
    if commit is None:
        with open(os.path.join(REPO, path), 'rb') as f:
            return f.read()
    return git('show', '%s:%s' % (commit, path))


def newest_profile(paths):
    best = None
    for p in paths:
        m = re.fullmatch(r'pgo/campaign-b(\d+)\.tgz', p)
        if m and (best is None or int(m.group(1)) > best[0]):
            best = (int(m.group(1)), p)
    return best[1] if best else None


def select(commit, pgo):
    """(keep, out, unmatched, missing); keep is [(published path, source path)]"""
    paths = tracked(commit)
    keep, out, unmatched = [], [], []
    profiles = ['pgo/%s.tgz' % n for n in pgo] if pgo else [newest_profile(paths)]
    for p in paths:
        r = rule_for(p)
        if r is None:
            unmatched.append(p)
        elif r[1] is None:
            (keep if p in profiles else out).append(p)
        elif r[1]:
            keep.append(p)
        else:
            out.append(p)
    keep = [(p[len(OVERLAY):] if p.startswith(OVERLAY) else p, p) for p in keep]
    published = [d for d, _ in keep]
    dupes = sorted(set(d for d in published if published.count(d) > 1))
    if dupes:
        sys.exit('public_export: published twice (an OVERLAY file whose original also goes out): ' + ' '.join(dupes))
    missing = [p for p in profiles if p not in paths]
    return keep, out, unmatched, missing


def strings_of(data):
    """the readable parts of a binary file, as text lines"""
    return '\n'.join(m.decode('latin1') for m in re.findall(rb'[\x20-\x7e]{4,}', data))


def texts_of(path, data):
    """(where, text) pairs to scan: the file, or each member of an archive"""
    if path.endswith(('.tgz', '.tar.gz')):
        with tarfile.open(fileobj=io.BytesIO(data)) as t:
            for m in t.getmembers():
                yield '%s:%s (name)' % (path, m.name), m.name
                if m.isfile():
                    yield '%s:%s' % (path, m.name), strings_of(t.extractfile(m).read())
        return
    if b'\0' in data[:8192]:
        yield path, strings_of(data)
    else:
        yield path, data.decode('utf-8', 'replace')


def allowed(path, label):
    return any(glob_match(path, g) and label == l for g, l in ALLOW)


def deny_scan(files):
    """files: {path: bytes}. Returns [(where, line, label, text)]."""
    rx = [(label, re.compile(p, re.I)) for label, p in DENY]
    found = []
    for path, data in sorted(files.items()):
        for where, text in texts_of(path, data):
            for n, line in enumerate(text.splitlines(), 1):
                for label, r in rx:
                    if r.search(line) and not allowed(path, label):
                        found.append((where, n, label, line.strip()[:160]))
    return found


MD_LINK = re.compile(r'\[[^\]]*\]\(([^)\s]+)\)')


def link_scan(files):
    """Markdown links to files the export leaves out"""
    found = []
    for path, data in sorted(files.items()):
        if not path.endswith('.md'):
            continue
        for n, line in enumerate(data.decode('utf-8', 'replace').splitlines(), 1):
            for target in MD_LINK.findall(line):
                if re.match(r'[a-z]+:|#|mailto:', target):
                    continue
                base = '' if any(glob_match(path, g) for g in ROOT_LINKS) else os.path.dirname(path)
                t = os.path.normpath(os.path.join(base, target.split('#')[0]))
                if t not in files and not any(f.startswith(t + '/') for f in files):
                    found.append((path, n, 'a link to a file left out', target))
    return found


CITED = re.compile(r'`((?:docs|tools|bake|psp|bench|upstream|pgo)/[A-Za-z0-9_./-]+|[A-Za-z0-9_-]+\.md)`')


def cited_paths(files, tracked_paths):
    """files a kept Markdown file names in backticks that exist in the
    development tree but don't go out: reported, not refused (citations of the
    development notes are expected; README says so)"""
    names = {os.path.basename(p): p for p in tracked_paths}
    out = set()
    for path, data in files.items():
        if path.endswith('.md'):
            for c in CITED.findall(data.decode('utf-8', 'replace')):
                p = c if '/' in c else names.get(c)
                if p and p in tracked_paths and p not in files:
                    out.add(p)
    return sorted(out)


def type_scan(files):
    return [(p, 0, 'a refused file type', '') for p in files if p.endswith(REFUSED_TYPES)]


def secret_scan(root, gitleaks):
    exe = gitleaks or shutil.which('gitleaks')
    if not exe:
        sys.exit('public_export: gitleaks not found (--gitleaks PATH, or on PATH)')
    report = os.path.join(tempfile.mkdtemp(), 'gitleaks.json')
    r = subprocess.run([exe, 'dir', root, '--no-banner', '--redact', '--exit-code', '3',
                        '--report-format', 'json', '--report-path', report],
                       capture_output=True, text=True)
    if r.returncode not in (0, 3):
        sys.exit('public_export: gitleaks failed:\n' + r.stderr)
    version = subprocess.run([exe, 'version'], capture_output=True, text=True).stdout.strip()
    return r.returncode == 0, version, (r.stdout + r.stderr).strip()


def inside_work_tree(path):
    p = os.path.abspath(path)
    while True:
        p = os.path.dirname(p)
        if os.path.exists(os.path.join(p, '.git')):
            return p
        if p == os.path.dirname(p):
            return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('commit')
    ap.add_argument('out')
    ap.add_argument('--pgo', action='append', default=[], metavar='NAME',
                    help='the profile(s) the release is built with (pgo/NAME.tgz); default the newest campaign-bNNN')
    ap.add_argument('--gitleaks', help='the gitleaks program')
    ap.add_argument('--report', action='store_true', help='list every finding; write nothing')
    ap.add_argument('--commit-message', help='make <out> a git repository with this one commit')
    a = ap.parse_args()

    if a.commit == '-':
        if not a.report:
            sys.exit('public_export: the work tree (-) is for --report only; export a commit')
        commit = None
    else:
        commit = git('rev-parse', '--verify', a.commit + '^{commit}').decode().strip()
    keep, out, unmatched, missing = select(commit, a.pgo)
    if unmatched:
        sys.exit('public_export: no rule says whether these go out (RULES):\n  ' + '\n  '.join(unmatched))
    if missing:
        sys.exit('public_export: no such profile: %s' % ' '.join(missing))
    files = {dest: read(commit, src) for dest, src in keep}

    findings = deny_scan(files) + link_scan(files) + type_scan(files)
    cited = cited_paths(files, set(tracked(commit)))
    if cited:
        print('cited but left out (%d, not refused): %s' % (len(cited), ' '.join(cited)))
    for where, n, label, text in findings:
        print('%s:%s: %s: %s' % (where, n, label, text))
    print('%d files out of %d tracked; %d findings' % (len(keep), len(keep) + len(out), len(findings)))
    if a.report:
        return
    if findings:
        sys.exit('public_export: refused (fix the text in the repository, then export again)')

    if os.path.exists(a.out):
        sys.exit('public_export: %s already exists' % a.out)
    tree = inside_work_tree(a.out)
    if tree:
        sys.exit('public_export: %s is inside the work tree %s' % (a.out, tree))
    os.makedirs(a.out)
    modes = {}
    for line in git('ls-tree', '-r', '-z', commit).decode().split('\0'):
        if line:
            meta, path = line.split('\t', 1)
            modes[path] = meta.split()[0]
    for p, src in keep:
        dest = os.path.join(a.out, p)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, 'wb') as f:
            f.write(files[p])
        os.chmod(dest, 0o755 if modes.get(src) == '100755' else 0o644)

    ok, version, text = secret_scan(a.out, a.gitleaks)
    print('gitleaks %s: %s' % (version, 'no leaks found' if ok else 'LEAKS FOUND'))
    if not ok:
        print(text)
        shutil.rmtree(a.out)
        sys.exit('public_export: refused (secrets); nothing written')

    if a.commit_message:
        env = dict(os.environ, GIT_AUTHOR_NAME=PUBLIC_IDENTITY[0], GIT_AUTHOR_EMAIL=PUBLIC_IDENTITY[1],
                   GIT_COMMITTER_NAME=PUBLIC_IDENTITY[0], GIT_COMMITTER_EMAIL=PUBLIC_IDENTITY[1])
        for args in (['init', '-q', '-b', 'main'], ['add', '-A'],
                     ['-c', 'commit.gpgsign=false', 'commit', '-q', '-m', a.commit_message]):
            subprocess.run(['git', '-C', a.out] + args, check=True, env=env)
    print('written: %s' % a.out)


if __name__ == '__main__':
    main()
