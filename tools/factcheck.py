#!/usr/bin/env python3
"""Check that docs/hardware_facts.md's citations still say what the page says.

Each citation on the page is `file:LINE` followed by a fragment of that line
in backticks. Line numbers drift as docs are edited, so the fragment is what
proves the source: this finds it at its line, near it, elsewhere in the file,
or nowhere. A fragment found nowhere means the source changed under the page:
the bullet needs re-reading, and possibly its figure updating.

Usage: tools/factcheck.py [--lab <lab checkout>] [--fix-lines] [page]
  --lab        also check `lab:` citations against that checkout (skipped otherwise)
  --fix-lines  rewrite moved citations' line numbers in place
Exit status: 1 if any fragment is missing.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CITE = re.compile(r"`((?:lab:)?[\w./-]+\.(?:md|tsv|txt|h|c|py)):(\d+)`\s*(?:[—-]\s*)?`([^`]+)`")
NEAR = 15


def norm(s):
    # The page quotes plain text; sources carry emphasis and either dash.
    s = s.replace("**", "").replace("`", "").replace("_", "")
    s = re.sub(r"[–—]", "-", s).replace("×", "x").replace("−", "-")
    return re.sub(r"\s+", " ", s).strip().lower()


def find(lines, line, frag):
    f = norm(frag)
    if 0 < line <= len(lines) and f in norm(lines[line - 1]):
        return "ok", line
    lo, hi = max(1, line - NEAR), min(len(lines), line + NEAR)
    for n in range(lo, hi + 1):
        if f in norm(lines[n - 1]):
            return "moved", n
    hits = [n for n, l in enumerate(lines, 1) if f in norm(l)]
    if hits:
        return "moved", hits[0]
    return "missing", None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("page", nargs="?", default=str(ROOT / "docs/hardware_facts.md"))
    ap.add_argument("--lab")
    ap.add_argument("--fix-lines", action="store_true")
    a = ap.parse_args()

    page = Path(a.page)
    text = page.read_text()
    cache, counts, fixes = {}, {"ok": 0, "moved": 0, "missing": 0, "skipped": 0, "nofile": 0}, []
    for m in CITE.finditer(text):
        path, line, frag = m.group(1), int(m.group(2)), m.group(3)
        if path.startswith("lab:"):
            if not a.lab:
                counts["skipped"] += 1
                continue
            f = Path(a.lab) / path[4:]
        else:
            f = page.parent / path
        if f not in cache:
            body = f.read_text(errors="replace") if f.exists() else None
            if body is not None and f.suffix == ".tsv":
                body = body.replace("\t", " | ")  # a TSV row is quoted with its fields joined by " | "
            cache[f] = body.split("\n") if body is not None else None
        if cache[f] is None:
            counts["nofile"] += 1
            print(f"NO FILE  {path}:{line}  `{frag}`")
            continue
        state, at = find(cache[f], line, frag)
        counts[state] += 1
        if state == "moved":
            print(f"moved    {path}:{line} -> {at}  `{frag}`")
            fixes.append((m.group(0), m.group(0).replace(f"{path}:{line}`", f"{path}:{at}`", 1)))
        elif state == "missing":
            print(f"MISSING  {path}:{line}  `{frag}`  (re-read the bullet: its source changed)")
    if a.fix_lines and fixes:
        for old, new in fixes:
            text = text.replace(old, new, 1)
        page.write_text(text)
        print(f"{len(fixes)} line numbers updated")
    print(" ".join(f"{k} {v}" for k, v in counts.items()))
    return 1 if counts["missing"] or counts["nofile"] else 0


if __name__ == "__main__":
    sys.exit(main())
