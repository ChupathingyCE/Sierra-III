/*
 * near_verdicts_g1.vmeg.c: the near loop's plane values on the VME for a
 * batch of one bone slot (box near-vme, docs/box_near_vme.md section 9).
 *
 * The accumulate form (pe0 multiply-accumulate, pe1 a cancel mask, pe2 the
 * running sum of the masked values, pe3 subtracting its delayed copy, the
 * write AGUs' bubbles and sync, and the lead-in) is taken from Th6uD1nk's
 * vmegc-mat4x4-mul-batch sample in vme2-next-level:
 *
 *   Copyright (c) 2026 m-c/d, Th6uD1nk
 *   MIT License: psp/src/vme/LICENSE_vme2-next-level.md
 *
 * The rest is Sierra-III's, and it is near_verdicts.vmeg.c's stage 1 with one
 * slot. A one-slot corner's byte weight is 128 (the census found every
 * corner's weights summing to 128), so its plane value is its dot product:
 * stage 2 would multiply by 128 and shift by 7, the same word. So this kernel
 * has no stage 2, no weights and five passes a tile where the 2-4 slot kernel
 * has ten. Stage 2 can't run at fewer than 4 slots anyway (its delayed
 * copy's sync read needs NEAR_SLOTS - 3 bubbles, at least one), which is why
 * a 2- or 3-slot batch runs on the 4-slot kernel with its spare slots zero.
 */
#include <me-core-mapper/me-core.h>
#include <me-core-mapper/vme-fu-opcodes.h>

/* NEAR_CONFIG_BEGIN */
#define NEAR_ROWS 5
#define NEAR_SLOTS 1
#define NEAR_VERTICES 96
#define NEAR_K1 8
#define NEAR_K2 7
/* NEAR_CONFIG_END */

/* A pass per row, as the 4-slot kernel's: the row's plane row is a ring of
4 words, the tile's vertices go in once as one flat run, and each row's plane
words go out after its pass. */
#define NEAR_RING (4 * NEAR_SLOTS)
#define NEAR_LEAD1 NEAR_RING
#define NEAR_STREAM1 (NEAR_LEAD1 + NEAR_VERTICES * NEAR_RING)
/* stage 1's delay, as the 4-slot kernel's stage 1 has it (proven there; it
doesn't depend on the slots, only on stage 1's groups of 4) */
#define NEAR_LOST 12
#define NEAR_DOTS 48

/* Stage 1, one row: each vertex's dot product with each slot's plane row,
stream order (vertex, slot, component), so stage 2 finds a vertex's slots side
by side. Groups of 4 words; one result on each group's last word. */
.kernel nearDots memory {
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

/* What the ME hands over, a tile at a time: each vertex's (x, y, z, 1) in
fixed point and the slot's five plane rows. */
volatile Buffer nearVertex[4 * NEAR_VERTICES];
volatile Buffer nearRow[NEAR_ROWS * NEAR_RING];
volatile Buffer nearPlane0[NEAR_VERTICES];
volatile Buffer nearPlane1[NEAR_VERTICES];
volatile Buffer nearPlane2[NEAR_VERTICES];
volatile Buffer nearPlane3[NEAR_VERTICES];
volatile Buffer nearPlane4[NEAR_VERTICES];
volatile Buffer nearZero[32];
volatile Buffer nearMask[8] = { 0, 0, 0xffffffff, 0, 0, 0, 0, 0 };

/* the dot product on each group's last word, stage 1's own words */
@declare {
  Transfer(Words) planeOut = {
    memory: nearPlane0,
    count: NEAR_VERTICES - 1,
    gap: 3,
    step: 4,
    group: { size: 0, outer: 0, inner: 0 }
  };
}

/* the contexts, built once: a pass loads its own, so no register keeps
what the last pass wrote (a loaded context is all of them, 0 where the
kernel writes none) */
volatile Memory nearRowContext[NEAR_ROWS];

void nearVerdictsTile(void) {
  @emulate "tile" {
    @kernel nearDots build {
      .params = { row: 0 };
      .memory = nearRowContext[0];
    }
    @kernel nearDots build {
      .params = { row: 1 };
      .memory = nearRowContext[1];
    }
    @kernel nearDots build {
      .params = { row: 2 };
      .memory = nearRowContext[2];
    }
    @kernel nearDots build {
      .params = { row: 3 };
      .memory = nearRowContext[3];
    }
    @kernel nearDots build {
      .params = { row: 4 };
      .memory = nearRowContext[4];
    }
    /* a memory kernel's config loads its context and triggers it (vme2-next-level's
    vmeLibLoadCustomContext, vmeLibFinish), so this call's pass runs before its loads
    and its words are dropped: the box loads first and then triggers, which the
    call below is */
    @kernel nearDots config | exec {
      .memory = nearRowContext[0];
      Transfer rowsIn = { memory: nearRow, count: NEAR_ROWS * NEAR_RING - 1 };
      Transfer maskIn = { memory: nearMask, count: 7 };
      /* the lead-in reads zeros, not whatever the last tile left */
      Transfer zeroIn1 = { memory: nearZero, count: NEAR_LEAD1 - 1 };
      Transfer vertexIn = { memory: nearVertex, count: 4 * NEAR_VERTICES - 1 };

      .load { .transfer = rowsIn; .scope = global; .offsets { .top0 = 0; } }
      .load { .transfer = maskIn; .scope = global; .offsets { .top2 = 0; } }
      .load { .transfer = zeroIn1; .scope = global; .offsets { .top1 = 0; } }
      .load { .transfer = vertexIn; .scope = global; .offsets { .top1 = NEAR_LEAD1; } }
    }
    @kernel nearDots config | exec {
      .memory = nearRowContext[0];
      .store { .transfer = { ...planeOut, memory: nearPlane0 }; .scope = global; .offsets { .base3 = NEAR_DOTS + NEAR_LEAD1 + 3; } }
    }
    @kernel nearDots config | exec {
      .memory = nearRowContext[1];
      .store { .transfer = { ...planeOut, memory: nearPlane1 }; .scope = global; .offsets { .base3 = NEAR_DOTS + NEAR_LEAD1 + 3; } }
    }
    @kernel nearDots config | exec {
      .memory = nearRowContext[2];
      .store { .transfer = { ...planeOut, memory: nearPlane2 }; .scope = global; .offsets { .base3 = NEAR_DOTS + NEAR_LEAD1 + 3; } }
    }
    @kernel nearDots config | exec {
      .memory = nearRowContext[3];
      .store { .transfer = { ...planeOut, memory: nearPlane3 }; .scope = global; .offsets { .base3 = NEAR_DOTS + NEAR_LEAD1 + 3; } }
    }
    @kernel nearDots config | exec {
      .memory = nearRowContext[4];
      .store { .transfer = { ...planeOut, memory: nearPlane4 }; .scope = global; .offsets { .base3 = NEAR_DOTS + NEAR_LEAD1 + 3; } }
    }
  }
}
