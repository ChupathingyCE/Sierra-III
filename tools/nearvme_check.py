#!/usr/bin/env python3
"""nearvme_check.py: the near-vme kernel against its C reference, in vmee.

Runs psp/src/vme/near_verdicts.vmeg.c in Th6uD1nk's vmee (vme2-next-level,
MIT) on generated tiles, compares every result word of both stages with
tools/nearvme_ref.c, and measures the plane values' error against a
double-precision skinning of the same unrounded inputs
(docs/box_near_vme.md section 6).

  python3 -I tools/nearvme_check.py --toolkit <vme2-next-level> --work <dir> \
      [--scale safe32|wide] [--seed N] [--synthetic N] [--realistic]

vmee is outside code: run it from a copy of the toolkit in a folder of its
own, with python3 -I (the lab's rule). --python lets the caller give the
command that starts the toolkit's scripts (default: python3 -I and a runner
that puts only the toolkit's folder on sys.path). Exit 0 when every word
matched.
"""
import argparse
import math
import os
import random
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
KERNEL = os.path.join(REPO, "psp", "src", "vme", "near_verdicts.vmeg.c")
REFERENCE = os.path.join(HERE, "nearvme_ref.c")

ROWS = 5
# The scales (docs/box_near_vme.md 6.2). p: the positions' fraction bits;
# q: the rotation coefficients'; the translations are at 2^-17 and the
# vertex's w word is 2^(p + q - 17), so every product is at 2^-(p + q) and
# stage 1's shift k1 brings the dot product to 2^-18. Stage 2's products are
# at 2^-25 (the byte weights are 128ths) and k2 = 7 brings them back.
SCALES = {
  # sums kept right modulo 2^32: needs an accumulator of 24 + k bits = 32
  "safe32": {"p": 11, "q": 15, "k1": 8, "k2": 7},
  # needs an accumulator of 24 + 21 = 45 bits: unmeasured past 32 on the PSP
  "wide": {"p": 18, "q": 21, "k1": 21, "k2": 7},
}
T_BITS = 17
OUT_BITS = 18
# a tile's vertices for each group size: what fits base3 (both stages' streams)
TILE = {4: 19, 8: 9}


def rotation(rng, spread=math.pi):
  """A random rotation (rows), by a random axis and angle up to spread."""
  axis = [rng.gauss(0, 1) for _ in range(3)]
  n = math.sqrt(sum(a * a for a in axis)) or 1.0
  x, y, z = (a / n for a in axis)
  a = rng.uniform(-spread, spread)
  c, s, t = math.cos(a), math.sin(a), 1 - math.cos(a)
  return [[t * x * x + c, t * x * y - s * z, t * x * z + s * y],
          [t * x * y + s * z, t * y * y + c, t * y * z - s * x],
          [t * x * z - s * y, t * y * z + s * x, t * z * z + c]]


def matmul(a, b):
  return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def plane_rows(gx, gy):
  """The five tested planes as camera-space rows (a, b, c, d): value = a x + b y
  + c z + d. The side planes normalised, as the near loop's distances are."""
  lx, ly = math.sqrt(1 + gx * gx), math.sqrt(1 + gy * gy)
  return [(0.0, 0.0, 1.0, 0.0),
          (-1 / lx, 0.0, gx / lx, 0.0), (1 / lx, 0.0, gx / lx, 0.0),
          (0.0, -1 / ly, gy / ly, 0.0), (0.0, 1 / ly, gy / ly, 0.0)]


def slot_rows(planes, rot, scale, trans):
  """P M_s for each plane: the slot matrix is scale * rot plus trans."""
  out = []
  for a, b, c, d in planes:
    n = (a, b, c)
    coef = [scale * sum(n[i] * rot[i][j] for i in range(3)) for j in range(3)]
    out.append(coef + [sum(n[i] * trans[i] for i in range(3)) + d])
  return out


def weights_for(rng, slots, realistic):
  """Byte weights summing to 128 over the batch's slots, some 0."""
  if slots == 1:
    return [128]
  if realistic:
    live = rng.randint(1, min(slots, 3))
    picks = rng.sample(range(slots), live)
  else:
    picks = [s for s in range(slots) if rng.random() < 0.8] or [rng.randrange(slots)]
  cuts = sorted(rng.randint(0, 128) for _ in range(len(picks) - 1))
  parts = [b - a for a, b in zip([0] + cuts, cuts + [128])]
  w = [0] * slots
  for s, part in zip(picks, parts):
    w[s] = part
  return w


def make_batch(rng, vertices, slots, realistic, guard):
  """A batch: its vertices (model space), its slots' plane rows (5 x 4 each,
  doubles), its byte weights."""
  planes = plane_rows(*guard)
  if realistic:
    # a limb or a weapon a few units across, its bones near each other, in
    # front of the camera out to the census's deepest corners (~15.5 units)
    base = rotation(rng)
    centre = [rng.uniform(-6, 6), rng.uniform(-4, 4), rng.uniform(0.5, 14)]
    rows = []
    for _ in range(slots):
      rot = matmul(rotation(rng, 0.4), base)
      trans = [centre[i] + rng.uniform(-1, 1) for i in range(3)]
      rows.append(slot_rows(planes, rot, rng.uniform(0.97, 1.03), trans))
    verts = [[rng.uniform(-3, 3) for _ in range(3)] for _ in range(vertices)]
  else:
    # the brief's synthetic set: positions anywhere within +-16 units, any
    # rotation; the translations small so every value stays inside +-32
    rows = [slot_rows(planes, rotation(rng), rng.uniform(0.9, 1.05), [rng.uniform(-2, 2) for _ in range(3)])
            for _ in range(slots)]
    verts = [[rng.uniform(-16, 16) for _ in range(3)] for _ in range(vertices)]
  wts = [weights_for(rng, slots, realistic) for _ in range(vertices)]
  return verts, rows, wts


def fixed(x, bits):
  return int(round(x * (1 << bits)))


def quantise(verts, rows, scale):
  p, q = scale["p"], scale["q"]
  vw = 1 << (p + q - T_BITS)
  qv = [[fixed(c, p) for c in v] + [vw] for v in verts]
  qr = [[[fixed(c, q) for c in row[:3]] + [fixed(row[3], T_BITS)] for row in slot] for slot in rows]
  for word in [w for v in qv for w in v] + [w for s in qr for r in s for w in r]:
    if not -(1 << 23) <= word < (1 << 23):
      raise ValueError(f"a word {word} doesn't fit 24 bits")
  return qv, qr


def tiles(qv, qr, wts, group):
  """The batch cut into tiles of TILE[group] vertices, the last padded with
  vertices of weight 0, the slots padded to the group with rows of 0."""
  n = TILE[group]
  slots = len(qr)
  zero_row = [0, 0, 0, 0]
  out = []
  for start in range(0, len(qv), n):
    tv, tw = qv[start:start + n], wts[start:start + n]
    pad = n - len(tv)
    tv = tv + [[0, 0, 0, qv[0][3]]] * pad
    tw = [w + [0] * (group - slots) for w in tw] + [[0] * group] * pad
    tr = [[(qr[s][r] if s < slots else zero_row) for s in range(group)] for r in range(ROWS)]
    out.append((start, len(qv[start:start + n]), tv, tr, tw))
  return out


def c_array(name, words):
  body = ", ".join(str(w & 0xffffffff) for w in words)
  return f"volatile Buffer {name}[{len(words)}] = {{ {body} }};\n"


def kernel_for(group, scale, folder):
  text = open(KERNEL).read()
  block = (f"/* NEAR_CONFIG_BEGIN */\n#define NEAR_ROWS {ROWS}\n#define NEAR_SLOTS {group}\n"
           f"#define NEAR_VERTICES {TILE[group]}\n#define NEAR_K1 {scale['k1']}\n#define NEAR_K2 {scale['k2']}\n"
           "/* NEAR_CONFIG_END */")
  text = re.sub(r"/\* NEAR_CONFIG_BEGIN \*/.*?/\* NEAR_CONFIG_END \*/", block, text, flags=re.S)
  path = os.path.join(folder, "kernel.vmeg.c")
  open(path, "w").write(text)
  return path


def run_vmee(args, folder, group, scale, tv, tr, tw):
  kernel = kernel_for(group, scale, folder)
  ring = [w for r in range(ROWS) for s in range(group) for w in tr[r][s]]
  data = ("#include <me-core-mapper/me-core.h>\n" + c_array("caseVertex", [w for v in tv for w in v])
          + c_array("caseRow", ring) + c_array("caseWeight", [w for ws in tw for w in ws]))
  data_path = os.path.join(folder, "data.vmeg.c")
  open(data_path, "w").write(data)
  cmd = args.python + [os.path.join(args.toolkit, "vmeemu.py"), "--run", "tile", kernel, data_path,
                       "--compat", "fat", "--buffers-words", "2048",
                       "--use", "nearVertex=caseVertex", "--use", "nearRow=caseRow", "--use", "nearWeight=caseWeight"]
  done = subprocess.run(cmd, cwd=folder, capture_output=True, text=True, env=args.env)
  if done.returncode:
    raise RuntimeError(f"vmee failed ({done.returncode}):\n{done.stdout[-2000:]}\n{done.stderr[-2000:]}")
  bufs = {}
  for m in re.finditer(r"Buffer: BASE (\d)\n((?:0x[0-9a-f]{8},? ?\n?)+)", done.stdout):
    bufs[int(m.group(1))] = [int(x, 16) for x in re.findall(r"0x[0-9a-f]{8}", m.group(2))]
  return bufs[3]


def signed24(x):
  x &= 0xffffff
  return x - (1 << 24) if x & 0x800000 else x


def run_reference(ref, group, scale, tv, tr, tw):
  words = [ROWS, group, len(tv), scale["k1"], scale["k2"]]
  words += [w for v in tv for w in v] + [w for r in range(ROWS) for s in range(group) for w in tr[r][s]]
  words += [w for ws in tw for w in ws]
  done = subprocess.run([ref], input=" ".join(map(str, words)), capture_output=True, text=True, check=True)
  lines = dict((l.split()[0], [int(x) for x in l.split()[1:]]) for l in done.stdout.splitlines())
  return lines["d"], lines["p"]


def check_tile(args, ref, folder, group, scale, tv, tr, tw):
  base3 = run_vmee(args, folder, group, scale, tv, tr, tw)
  ref_d, ref_p = run_reference(ref, group, scale, tv, tr, tw)
  ring = 4 * ROWS * group
  lead2 = 16
  stream1 = ring + TILE[group] * ring
  emu_d = [signed24(base3[ring + 3 + 4 * j]) for j in range(len(ref_d))]
  emu_p = [signed24(base3[stream1 + lead2 + group - 1 + group * j]) for j in range(len(ref_p))]
  diff = sum(a != b for a, b in zip(emu_d, ref_d)) + sum(a != b for a, b in zip(emu_p, ref_p))
  return len(ref_d) + len(ref_p), diff, emu_p


def double_planes(verts, rows, wts):
  """Each vertex's five plane values in doubles, and its largest slot term."""
  out, terms = [], []
  for v, w in zip(verts, wts):
    vals, big = [], 0.0
    for r in range(ROWS):
      total = 0.0
      for s, slot in enumerate(rows):
        term = w[s] / 128.0 * (slot[r][0] * v[0] + slot[r][1] * v[1] + slot[r][2] * v[2] + slot[r][3])
        total += term
        big = max(big, abs(term))
      vals.append(total)
    out.append(vals)
    terms.append(big)
  return out, terms


def main():
  ap = argparse.ArgumentParser()
  ap.add_argument("--toolkit", required=True, help="a copy of vme2-next-level, with me-core-mapper filled")
  ap.add_argument("--work", required=True)
  ap.add_argument("--scale", default="safe32", choices=sorted(SCALES))
  ap.add_argument("--seed", type=int, default=20261121)
  ap.add_argument("--synthetic", type=int, default=3, help="tiles for each slot count 1-8")
  ap.add_argument("--realistic", action="store_true", help="also the realistic batches (60-120 vertices, 1-4 slots)")
  ap.add_argument("--pq", default=None, help="p,q to try in place of the scale's (k1 = p + q - 18)")
  ap.add_argument("--python", default=None, help="the command that runs the toolkit's scripts")
  ap.add_argument("--path", default=None, help="a folder put first on PATH for vmee (its g++)")
  args = ap.parse_args()
  args.toolkit = os.path.abspath(args.toolkit)
  runner = os.path.join(HERE, "nearvme_dink.py")
  args.python = args.python.split() if args.python else [sys.executable, "-I", runner]
  args.env = dict(os.environ)
  if args.path:
    args.env["PATH"] = os.path.abspath(args.path) + os.pathsep + args.env["PATH"]
  scale = dict(SCALES[args.scale])
  if args.pq:
    scale["p"], scale["q"] = (int(x) for x in args.pq.split(","))
    scale["k1"] = scale["p"] + scale["q"] - OUT_BITS
  os.makedirs(args.work, exist_ok=True)
  ref = os.path.join(args.work, "nearvme_ref")
  subprocess.run(["cc", "-O2", "-Wall", "-o", ref, REFERENCE], check=True)

  rng = random.Random(args.seed)
  batches = []
  for slots in range(1, 9):
    group = 4 if slots <= 4 else 8
    for n in range(args.synthetic):
      batches.append(("synthetic", slots, TILE[group], (4.2, 2.4) if n % 2 == 0 else (10.0, 10.0)))
  if args.realistic:
    for slots, vertices in ((1, 60), (2, 84), (3, 100), (4, 120)):
      batches.append(("realistic", slots, vertices, (4.2, 2.4)))

  words = diffs = tile_count = 0
  worst = {"units": 0.0, "rel": 0.0, "arith": 0.0}
  by_kind = {}
  for kind, slots, vertices, guard in batches:
    group = 4 if slots <= 4 else 8
    verts, rows, wts = make_batch(rng, vertices, slots, kind == "realistic", guard)
    qv, qr = quantise(verts, rows, scale)
    exact, terms = double_planes(verts, rows, wts)
    # the same skinning on the rounded inputs: what the arithmetic alone errs by
    dv = [[c / (1 << scale["p"]) for c in v[:3]] for v in qv]
    dr = [[[c / (1 << scale["q"]) for c in r[:3]] + [r[3] / (1 << T_BITS)] for r in s] for s in qr]
    rounded, _ = double_planes(dv, dr, wts)
    batch_words = batch_diffs = 0
    for start, real, tv, tr, tw in tiles(qv, qr, wts, group):
      folder = os.path.join(args.work, f"t{tile_count:03d}")
      os.makedirs(folder, exist_ok=True)
      n, diff, emu_p = check_tile(args, ref, folder, group, scale, tv, tr, tw)
      batch_words += n
      batch_diffs += diff
      tile_count += 1
      for i in range(real):
        for r in range(ROWS):
          got = emu_p[i * ROWS + r] / (1 << OUT_BITS)
          err = abs(got - exact[start + i][r])
          kind_worst = by_kind.setdefault(kind, [0.0, 0.0])
          kind_worst[0] = max(kind_worst[0], err)
          kind_worst[1] = max(kind_worst[1], err / terms[start + i] if terms[start + i] else 0.0)
          worst["units"] = max(worst["units"], err)
          worst["rel"] = max(worst["rel"], err / terms[start + i] if terms[start + i] else 0.0)
          worst["arith"] = max(worst["arith"], abs(got - rounded[start + i][r]))
    words += batch_words
    diffs += batch_diffs
    print(f"{kind:9s} slots {slots} vertices {vertices:3d} group {group}: {batch_words} words, {batch_diffs} differ", flush=True)
  print(f"scale {args.scale} {scale}: {tile_count} tiles, {words} words compared, {diffs} differ")
  print(f"largest error against doubles: {worst['units']:.3e} units (2^{math.log2(worst['units']):.2f}); "
        f"relative to the vertex's largest slot term {worst['rel']:.3e} (2^{math.log2(worst['rel']):.2f}); "
        f"the arithmetic alone (rounded inputs) {worst['arith']:.3e} units")
  for kind, (units, rel) in sorted(by_kind.items()):
    print(f"  {kind}: largest error 2^{math.log2(units):.2f} units, 2^{math.log2(rel):.2f} of the largest slot term")
  return 0 if diffs == 0 else 1


if __name__ == "__main__":
  sys.exit(main())
