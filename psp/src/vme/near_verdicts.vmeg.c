/*
 * near_verdicts.vmeg.c: the near loop's plane values on the VME (box
 * near-vme, docs/box_near_vme.md section 6).
 *
 * The accumulate form (pe0 multiply-accumulate, pe1 a cancel mask, pe2 the
 * running sum of the masked values, pe3 subtracting its delayed copy, the
 * write AGUs' bubbles and sync, and the lead-in) is taken from Th6uD1nk's
 * vmegc-mat4x4-mul-batch sample in vme2-next-level:
 *
 *   Copyright (c) 2026 m-c/d, Th6uD1nk
 *   MIT License: psp/src/vme/LICENSE_vme2-next-level.md
 *
 * The rest (two stages, the scales, the stream layout, the rings, the strided
 * read, the zeroed lead-ins) is Sierra-III's.
 *
 * Five plane rows a slot (NEAR_ROWS): z, and the four sides normalized. The
 * near pair shares the z row, as the weights sum to 128 the ME compares one
 * value with both limits; deriving the sides on the ME instead would cost it
 * a multiply-add each and multiply z's error by the guard's slope.
 */
#include <me-core-mapper/me-core.h>
#include <me-core-mapper/vme-fu-opcodes.h>

/* NEAR_CONFIG_BEGIN */
#define NEAR_ROWS 5
#define NEAR_SLOTS 4
#define NEAR_VERTICES 96
#define NEAR_K1 8
#define NEAR_K2 7
/* NEAR_CONFIG_END */

/* The stream layout (docs/box_near_vme.md section 8): a pass per row, so a
row's plane rows are a ring of one row's slots and the tile's vertices go in
once for each slot, not once for each (row, slot). The vertex stream, the
weights and the rows stay in the buffers for the tile's ten passes, and each
row's plane words go out after its pass, so base3 holds one row's stage 1 and
stage 2 words and a tile takes 96 vertices (48 at 8 slots). The ME's cost is
ten transfers and ten context switches a tile, not a transfer for every copy
of the vertices. */
#define NEAR_RING (4 * NEAR_SLOTS)
#define NEAR_LEAD1 NEAR_RING
#define NEAR_STREAM1 (NEAR_LEAD1 + NEAR_VERTICES * NEAR_RING)
#define NEAR_LEAD2 16
#define NEAR_STREAM2 (NEAR_LEAD2 + NEAR_VERTICES * NEAR_SLOTS)
#define NEAR_LOST (12 + NEAR_SLOTS - 4)
/* stage 1's words in base3; low enough that stage 2's strided read, which
starts 4 * NEAR_LEAD2 words early under its zero weights, stays in base3 */
#define NEAR_DOTS 48
/* a row's plane words, sent out before the next row's stage 2 */
#define NEAR_PLANES (NEAR_DOTS + NEAR_STREAM1 + 16)

/* Stage 1, one row: each vertex's dot product with each slot's plane row,
stream order (vertex, slot, component), so stage 2 finds a vertex's slots side
by side. Groups of 4 words; one result on each group's last word. */
.kernel nearDots direct {
  .params { int row; }
  Int count = NEAR_STREAM1 - 1;
  Int lost = NEAR_LOST;

  /* mode 2 for every read: the PSP-1000 matched vmee in all of the lab's
  mode-2 kernels, and mode 4's rings didn't (vme_kernels.md section 2) */
  Agu(2) rows = { count: count, offset: row * NEAR_RING, inner0: { count: NEAR_RING - 1, step: 1 }, repeat: true };
  Agu(2) mask = { count: count, inner0: { count: 3, step: 1 }, repeat: true };
  Agu(2) vertices = { count: count, inner0: { count: count, step: 1 } };
  Agu(2) inter = { count: count + lost, inner0: { count: count + lost, step: 1 } };
  /* writes stay mode 4: mode 2's write AGU is unmeasured on the PSP */
  Agu write0 = { count: count + lost, bubbles: 5, sync: true };
  Agu write2 = { count: count + lost, bubbles: 1, sync: true };
  Agu write3 = { count, offset: NEAR_DOTS, cyc: auto - 1 };

  Staging s0, s1, s2, s3;

  .pe0 {
    .top = rows;
    .base = inter;
    .write = write0;
    s0 = mac(top0, top1) >> NEAR_K1;
  }

  .pe1 {
    .top = vertices;
    s1 = top2 & s0;
  }

  .pe2 {
    .top = mask;
    .base = inter;
    .write = write2;
    s2 = acc(s1);
  }

  .pe3 {
    .write = write3;
    s3 = subacst(s2, base2, 0);
    .spad = subacst(base0, s3, 0);
  }
}

/* Stage 2, one row: a vertex's slot values weighted and summed. Groups of
NEAR_SLOTS words (4 or 8: the delayed copy's bubbles are NEAR_SLOTS - 3, and
a sync read needs at least one). The slot values are stage 1's results, read
in place in base3 one word in four; the weights are in top3, as top1 keeps
the vertices for the next row. */
.kernel nearSum direct {
  Int count = NEAR_STREAM2 - 1;
  Int lost = NEAR_LOST;

  Agu(2) dots = { count: count, offset: NEAR_DOTS + NEAR_LEAD1 + 3 - 4 * NEAR_LEAD2, inner0: { count: 4 * count, step: 4 }, repeat: true };
  Agu(2) mask = { count: count, inner0: { count: NEAR_SLOTS - 1, step: 1 }, repeat: true };
  Agu(2) weights = { count: count, inner0: { count: count, step: 1 } };
  Agu(2) inter = { count: count + lost, inner0: { count: count + lost, step: 1 } };
  Agu write0 = { count: count + lost, bubbles: 5, sync: true };
  Agu write2 = { count: count + lost, bubbles: NEAR_SLOTS - 3, sync: true };
  Agu write3 = { count, offset: NEAR_PLANES, cyc: auto - 1 };

  Staging s0, s1, s2, s3;

  .pe0 {
    .base = inter;
    .write = write0;
    s0 = mac(base3, top3) >> NEAR_K2;
  }

  .pe1 {
    s1 = top2 & s0;
  }

  .pe2 {
    .top = mask;
    .base = inter;
    .write = write2;
    s2 = acc(s1);
  }

  .pe3 {
    .top = weights;
    .base = dots;
    .write = write3;
    s3 = subacst(s2, base2, 0);
    .spad = subacst(base0, s3, 0);
  }
}

#define NEAR_COPIES (NEAR_SLOTS - 1)

/* What the ME hands over, a tile at a time: each vertex's (x, y, z, 1) in
fixed point; each slot's plane rows in order (row, slot, component); each
vertex's byte weights, a slot past the batch's own weighing 0. */
volatile Buffer nearVertex[4 * NEAR_VERTICES];
volatile Buffer nearRow[NEAR_ROWS * NEAR_RING];
volatile Buffer nearWeight[NEAR_SLOTS * NEAR_VERTICES];
/* each row's plane words, one array a row: a store names its array */
volatile Buffer nearPlane0[NEAR_VERTICES];
volatile Buffer nearPlane1[NEAR_VERTICES];
volatile Buffer nearPlane2[NEAR_VERTICES];
volatile Buffer nearPlane3[NEAR_VERTICES];
volatile Buffer nearPlane4[NEAR_VERTICES];
volatile Buffer nearZero[32];
/* the group's last product, three words ahead of the running sum's lag;
its first 4 words are stage 1's mask, its first NEAR_SLOTS stage 2's */
volatile Buffer nearMask[8] = { 0, 0, 0xffffffff, 0, 0, 0, 0, 0 };

/* one plane value on each group's last word; a store keeps the last load's
group unless it sets its own */
@declare {
  Transfer(Words) planeOut = {
    memory: nearPlane0,
    count: NEAR_VERTICES - 1,
    gap: NEAR_SLOTS - 1,
    step: 4,
    group: { size: 0, outer: 0, inner: 0 }
  };
}

void nearVerdictsTile(void) {
  @emulate "tile" {
    @kernel nearDots config {
      .params = { row: 0 };
      Transfer rowsIn = { memory: nearRow, count: NEAR_ROWS * NEAR_RING - 1 };
      Transfer maskIn = { memory: nearMask, count: 7 };
      /* the lead-ins read zeros, not whatever the last tile left */
      Transfer zeroIn1 = { memory: nearZero, count: NEAR_LEAD1 - 1 };
      Transfer zeroIn2 = { memory: nearZero, count: NEAR_LEAD2 - 1 };
      Transfer weightIn = { memory: nearWeight, count: NEAR_SLOTS * NEAR_VERTICES - 1 };
      /* each vertex once for each slot: the copies side by side */
      Transfer(Words) vertexIn = {
        memory: nearVertex,
        count: 4 * NEAR_VERTICES - 1,
        gap: 0,
        step: 4,
        group: { size: 3, outer: NEAR_RING, inner: 1 }
      };

      .load { .transfer = rowsIn; .scope = global; .offsets { .top0 = 0; } }
      .load { .transfer = maskIn; .scope = global; .offsets { .top2 = 0; } }
      .load { .transfer = zeroIn1; .scope = global; .offsets { .top1 = 0; } }
      .load { .transfer = zeroIn2; .scope = global; .offsets { .top3 = 0; } }
      .load { .transfer = weightIn; .scope = global; .offsets { .top3 = NEAR_LEAD2; } }
      for i in 0..NEAR_COPIES {
        .load { .transfer = vertexIn; .scope = global; .offsets { top[1] = NEAR_LEAD1 + i * 4; } }
      }
    }
    @kernel nearDots exec {
    }
    @kernel nearSum config {
    }
    @kernel nearSum exec {
      .store { .transfer = { ...planeOut, memory: nearPlane0 }; .scope = global; .offsets { .base3 = NEAR_PLANES + NEAR_LEAD2 + NEAR_SLOTS - 1; } }
    }
    @kernel nearDots config {
      .params = { row: 1 };
    }
    @kernel nearDots exec {
    }
    @kernel nearSum config {
    }
    @kernel nearSum exec {
      .store { .transfer = { ...planeOut, memory: nearPlane1 }; .scope = global; .offsets { .base3 = NEAR_PLANES + NEAR_LEAD2 + NEAR_SLOTS - 1; } }
    }
    @kernel nearDots config {
      .params = { row: 2 };
    }
    @kernel nearDots exec {
    }
    @kernel nearSum config {
    }
    @kernel nearSum exec {
      .store { .transfer = { ...planeOut, memory: nearPlane2 }; .scope = global; .offsets { .base3 = NEAR_PLANES + NEAR_LEAD2 + NEAR_SLOTS - 1; } }
    }
    @kernel nearDots config {
      .params = { row: 3 };
    }
    @kernel nearDots exec {
    }
    @kernel nearSum config {
    }
    @kernel nearSum exec {
      .store { .transfer = { ...planeOut, memory: nearPlane3 }; .scope = global; .offsets { .base3 = NEAR_PLANES + NEAR_LEAD2 + NEAR_SLOTS - 1; } }
    }
    @kernel nearDots config {
      .params = { row: 4 };
    }
    @kernel nearDots exec {
    }
    @kernel nearSum config {
    }
    @kernel nearSum exec {
      .store { .transfer = { ...planeOut, memory: nearPlane4 }; .scope = global; .offsets { .base3 = NEAR_PLANES + NEAR_LEAD2 + NEAR_SLOTS - 1; } }
    }
  }
}
