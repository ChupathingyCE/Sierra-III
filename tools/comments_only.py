#!/usr/bin/env python3
"""comments_only.py - proves a change touched only comments.

For each file changed since a git ref, strips the comments from both
versions and compares what's left (whitespace folded). C files go through
the compiler's own preprocessor in comment-stripping mode
(`psp-gcc -fpreprocessed -dD -E -P`: comments go, nothing is expanded);
Python files through `tokenize` (comments go; docstrings stay, as they are
strings the program can read). A file whose code differs is named and the
run fails, so a comment pass can't slip in a code change.

usage: python3 tools/comments_only.py BASE_REF [FILE ...]
  BASE_REF  the version to compare against (a branch, tag or commit)
  FILE      the files to check (default: every .c, .h and .py changed since BASE_REF)

  python3 tools/comments_only.py origin/w119
Needs psp-gcc on PATH for C files (pspdev's bin).

Line numbers move when a comment grows or shrinks, so __LINE__ values and a
PGO profile's line checksums change with them: count the build's profile
after the pass, as every build's is counted at its own commit anyway.
"""
import io
import subprocess
import sys
import tokenize


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, check=True).stdout


def code_of_c(text):
    out = subprocess.run(["psp-gcc", "-fpreprocessed", "-dD", "-E", "-P", "-x", "c", "-"],
                         input=text, capture_output=True, text=True)
    if out.returncode:
        sys.exit("comments_only: psp-gcc failed: " + out.stderr.strip())
    return " ".join(out.stdout.split())


def code_of_py(text):
    kept = [tok.string for tok in tokenize.generate_tokens(io.StringIO(text).readline)
            if tok.type not in (tokenize.COMMENT, tokenize.NL, tokenize.NEWLINE, tokenize.INDENT, tokenize.DEDENT)]
    return " ".join(kept)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    base, files = sys.argv[1], sys.argv[2:]
    if not files:
        files = [f for f in git("diff", "--name-only", base, "--").split() if f.endswith((".c", ".h", ".py"))]
    if not files:
        sys.exit("comments_only: no .c, .h or .py file changed since " + base)
    bad = []
    for f in files:
        before = git("show", f"{base}:{f}")
        after = open(f).read()
        code = code_of_py if f.endswith(".py") else code_of_c
        same = code(before) == code(after)
        print(("only comments  " if same else "CODE CHANGED   ") + f)
        if not same:
            bad.append(f)
    if bad:
        sys.exit(f"comments_only: {len(bad)} file(s) changed code, not just comments")


if __name__ == "__main__":
    main()
