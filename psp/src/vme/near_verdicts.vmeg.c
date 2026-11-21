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
 * The rest (two stages, the scales, the rings, the strided read, the zeroed
 * lead-ins) is Sierra-III's.
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
#define NEAR_VERTICES 19
#define NEAR_K1 8
#define NEAR_K2 7
/* NEAR_CONFIG_END */

#define NEAR_RING (4 * NEAR_ROWS * NEAR_SLOTS)
#define NEAR_LEAD1 NEAR_RING
#define NEAR_STREAM1 (NEAR_LEAD1 + NEAR_VERTICES * NEAR_RING)
#define NEAR_LEAD2 16
#define NEAR_STREAM2 (NEAR_LEAD2 + NEAR_VERTICES * NEAR_ROWS * NEAR_SLOTS)
#define NEAR_LOST (12 + NEAR_SLOTS - 4)
#define NEAR_OUT2 NEAR_STREAM1

/* Stage 1: each vertex's dot product with each slot's plane rows, stream order
(vertex, row, slot, component), so stage 2 finds a (vertex, row)'s slots side
by side. Groups of 4 words; one result on each group's last word. */
.kernel nearDots direct {
  Int count = NEAR_STREAM1 - 1;
  Int lost = NEAR_LOST;

  /* mode 2 for every read: the PSP-1000 matched vmee in all of the lab's
  mode-2 kernels, and mode 4's rings didn't (vme_kernels.md section 2) */
  Agu(2) rows = { count: count, inner0: { count: NEAR_RING - 1, step: 1 }, repeat: true };
  Agu(2) mask = { count: count, inner0: { count: 3, step: 1 }, repeat: true };
  Agu(2) vertices = { count: count, inner0: { count: count, step: 1 } };
  Agu(2) inter = { count: count + lost, inner0: { count: count + lost, step: 1 } };
  /* writes stay mode 4: mode 2's write AGU is unmeasured on the PSP */
  Agu write0 = { count: count + lost, bubbles: 5, sync: true };
  Agu write2 = { count: count + lost, bubbles: 1, sync: true };
  Agu write3 = { count, cyc: auto - 1 };

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

/* Stage 2: a (vertex, row)'s slot values weighted and summed. Groups of
NEAR_SLOTS words (4 or 8: the delayed copy's bubbles are NEAR_SLOTS - 3, and
a sync read needs at least one). The slot values are stage 1's results, read
in place in base3 one word in four; the result goes after them in base3. */
.kernel nearSum direct {
  Int count = NEAR_STREAM2 - 1;
  Int lost = NEAR_LOST;

  Agu(2) dots = { count: count, offset: NEAR_LEAD1 + 3 - 4 * NEAR_LEAD2, inner0: { count: 4 * count, step: 4 }, repeat: true };
  Agu(2) mask = { count: count, inner0: { count: NEAR_SLOTS - 1, step: 1 }, repeat: true };
  Agu(2) weights = { count: count, inner0: { count: count, step: 1 } };
  Agu(2) inter = { count: count + lost, inner0: { count: count + lost, step: 1 } };
  Agu write0 = { count: count + lost, bubbles: 5, sync: true };
  Agu write2 = { count: count + lost, bubbles: NEAR_SLOTS - 3, sync: true };
  Agu write3 = { count, offset: NEAR_OUT2, cyc: auto - 1 };

  Staging s0, s1, s2, s3;

  .pe0 {
    .base = inter;
    .write = write0;
    s0 = mac(base3, top1) >> NEAR_K2;
  }

  .pe1 {
    .top = weights;
    s1 = top2 & s0;
  }

  .pe2 {
    .top = mask;
    .base = inter;
    .write = write2;
    s2 = acc(s1);
  }

  .pe3 {
    .base = dots;
    .write = write3;
    s3 = subacst(s2, base2, 0);
    .spad = subacst(base0, s3, 0);
  }
}

#define NEAR_COPIES1 (NEAR_ROWS * NEAR_SLOTS - 1)
#define NEAR_COPIES2 (NEAR_ROWS - 1)

/* What the ME hands over, a tile at a time: each vertex's (x, y, z, 1) in
fixed point; each slot's plane rows in stream order (row, slot); each vertex's
byte weights, a slot past the batch's own weighing 0. */
volatile Buffer nearVertex[4 * NEAR_VERTICES];
volatile Buffer nearRow[NEAR_RING];
volatile Buffer nearWeight[NEAR_SLOTS * NEAR_VERTICES];
volatile Buffer nearPlane[NEAR_ROWS * NEAR_VERTICES];
volatile Buffer nearZero[NEAR_LEAD1];
/* the group's last product, three words ahead of the running sum's lag;
its first 4 words are stage 1's mask, its first NEAR_SLOTS stage 2's */
volatile Buffer nearMask[8] = { 0, 0, 0xffffffff, 0, 0, 0, 0, 0 };

void nearVerdictsTile(void) {
  @emulate "tile" {
    @kernel nearDots config {
      Transfer rowsIn = { memory: nearRow, count: NEAR_RING - 1 };
      Transfer maskIn = { memory: nearMask, count: 3 };
      /* the lead-in reads zeros, not whatever the last tile left */
      Transfer zeroIn = { memory: nearZero, count: NEAR_LEAD1 - 1 };
      /* each vertex once for every (row, slot) word group of the ring */
      Transfer(Words) vertexIn = {
        memory: nearVertex,
        count: 4 * NEAR_VERTICES - 1,
        gap: 0,
        step: 4,
        group: { size: 3, outer: NEAR_RING, inner: 1 }
      };

      .load { .transfer = rowsIn; .scope = global; .offsets { .top0 = 0; } }
      .load { .transfer = maskIn; .scope = global; .offsets { .top2 = 0; } }
      .load { .transfer = zeroIn; .scope = global; .offsets { .top1 = 0; } }
      for i in 0..NEAR_COPIES1 {
        .load { .transfer = vertexIn; .scope = global; .offsets { top[1] = NEAR_LEAD1 + i * 4; } }
      }
    }
    @kernel nearDots exec {
    }
    @kernel nearSum config {
      Transfer maskIn = { memory: nearMask, count: NEAR_SLOTS - 1 };
      Transfer zeroIn = { memory: nearZero, count: NEAR_LEAD2 - 1 };
      /* each vertex's weights once for every row */
      Transfer(Words) weightIn = {
        memory: nearWeight,
        count: NEAR_SLOTS * NEAR_VERTICES - 1,
        gap: 0,
        step: 4,
        group: { size: NEAR_SLOTS - 1, outer: NEAR_ROWS * NEAR_SLOTS, inner: 1 }
      };

      .load { .transfer = maskIn; .scope = global; .offsets { .top2 = 0; } }
      .load { .transfer = zeroIn; .scope = global; .offsets { .top1 = 0; } }
      for i in 0..NEAR_COPIES2 {
        .load { .transfer = weightIn; .scope = global; .offsets { top[1] = NEAR_LEAD2 + i * NEAR_SLOTS; } }
      }
    }
    @kernel nearSum exec {
      /* one plane value on each group's last word */
      Transfer(Words) planeOut = {
        memory: nearPlane,
        count: NEAR_ROWS * NEAR_VERTICES - 1,
        gap: NEAR_SLOTS - 1,
        step: 4,
        /* a store keeps the last load's group unless it sets its own */
        group: { size: 0, outer: 0, inner: 0 }
      };

      .store { .transfer = planeOut; .scope = global; .offsets { .base3 = NEAR_OUT2 + NEAR_LEAD2 + NEAR_SLOTS - 1; } }
    }
  }
}
