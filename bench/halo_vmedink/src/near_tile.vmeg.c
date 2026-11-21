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
volatile Buffer nearVertex[4 * NEAR_VERTICES] = {
  0xfffffe81, 0xfffff7c0, 0xffffec6a, 0x00000200,
  0x00000899, 0xfffff9c9, 0x000013ab, 0x00000200,
  0x00000102, 0x00001161, 0x000006f0, 0x00000200,
  0xffffe936, 0xfffff162, 0x00000599, 0x00000200,
  0xffffee5b, 0x000005c4, 0x00001039, 0x00000200,
  0xfffff9cb, 0xfffff9f0, 0xfffffcd8, 0x00000200,
  0x000003fd, 0x0000129b, 0xfffffcb9, 0x00000200,
  0x000000ea, 0xfffff286, 0x00000927, 0x00000200,
  0xfffff3b8, 0xfffffe76, 0x0000014c, 0x00000200,
  0xfffffb01, 0xfffffe26, 0x00001135, 0x00000200,
  0xfffffffc, 0x00000b93, 0x00000ed7, 0x00000200,
  0x0000094b, 0xfffffad5, 0xfffffe25, 0x00000200,
  0x0000179d, 0x00001772, 0xffffec9c, 0x00000200,
  0x000015a6, 0x0000145d, 0xfffff4b4, 0x00000200,
  0x000003b0, 0xfffff9f7, 0x00000ed0, 0x00000200,
  0xfffff6c8, 0xffffe978, 0x000014b5, 0x00000200,
  0x00001365, 0xffffee7a, 0x00000ca8, 0x00000200,
  0x00000372, 0x000002be, 0x000000e5, 0x00000200,
  0x000008f9, 0x000001b4, 0xfffffed2, 0x00000200,
  0x00000fbd, 0xffffeccf, 0x00000c83, 0x00000200,
  0xffffeb10, 0xfffff69d, 0x00000bb2, 0x00000200,
  0xfffff409, 0xffffe923, 0xffffe9f4, 0x00000200,
  0x000004b5, 0xfffff030, 0x000005e5, 0x00000200,
  0x00001296, 0x0000128d, 0x00000e05, 0x00000200,
  0x000012ab, 0x00000307, 0xffffed79, 0x00000200,
  0x00000b00, 0x0000051b, 0x00000a2d, 0x00000200,
  0x00000880, 0x00000860, 0x000017a7, 0x00000200,
  0xfffff685, 0x00000775, 0xffffffbf, 0x00000200,
  0x0000150e, 0xffffea38, 0x00000994, 0x00000200,
  0x00000692, 0x00000981, 0xffffea82, 0x00000200,
  0x0000016f, 0xffffea1b, 0xfffff1a3, 0x00000200,
  0x00000ebb, 0xfffff26c, 0x00000712, 0x00000200,
  0x0000005c, 0x0000021a, 0xffffefea, 0x00000200,
  0x00001345, 0xfffff517, 0x000016c7, 0x00000200,
  0x0000011e, 0x00000c3d, 0x00000cfc, 0x00000200,
  0xffffed7d, 0x000002be, 0x000005f3, 0x00000200,
  0xfffffeda, 0x000010f9, 0xfffff4f1, 0x00000200,
  0xfffff708, 0x000001d2, 0x00000171, 0x00000200,
  0x00000ff4, 0x00000da9, 0x00001626, 0x00000200,
  0xfffffc4b, 0x00001200, 0x000006fb, 0x00000200,
  0x000016ee, 0xfffff324, 0x0000036e, 0x00000200,
  0xfffff077, 0x00001768, 0x00000e4b, 0x00000200,
  0xfffff2c1, 0xfffff788, 0xfffff1d5, 0x00000200,
  0x00000c1d, 0x000010f6, 0xffffeac4, 0x00000200,
  0xffffe966, 0x000000af, 0x00000c97, 0x00000200,
  0xfffffca5, 0x000000b5, 0xfffff9cb, 0x00000200,
  0x00000425, 0xfffff57b, 0x000014aa, 0x00000200,
  0x0000029a, 0x00000d76, 0xfffff2c2, 0x00000200,
  0x00001256, 0xfffff734, 0xfffff63f, 0x00000200,
  0x0000052e, 0x000001ff, 0xfffffcba, 0x00000200,
  0xfffffe63, 0xfffff4ee, 0x00001491, 0x00000200,
  0x000004aa, 0xfffffc64, 0xffffedd9, 0x00000200,
  0xfffff9e1, 0xffffedfb, 0x00000590, 0x00000200,
  0x00000ee0, 0xffffeef2, 0x00000f74, 0x00000200,
  0xffffe8c6, 0xfffff9c8, 0xfffff36b, 0x00000200,
  0x0000003e, 0xffffecfe, 0x000009c2, 0x00000200,
  0xfffff5d9, 0x00001145, 0xffffed4c, 0x00000200,
  0x000007e1, 0xfffff601, 0x000017d6, 0x00000200,
  0xffffea8b, 0x00000237, 0xfffff487, 0x00000200,
  0xfffff60d, 0xfffff39f, 0xfffffeeb, 0x00000200,
  0x00000588, 0xfffffae3, 0x00000cd4, 0x00000200,
  0x00000425, 0x00000ff5, 0xfffff8dc, 0x00000200,
  0xfffff0f9, 0x000012fd, 0xfffff812, 0x00000200,
  0x00001613, 0xfffff6de, 0x00000db4, 0x00000200,
  0xfffff1c6, 0xfffff5ef, 0x00000199, 0x00000200,
  0xffffe829, 0xfffff2ea, 0xffffeda7, 0x00000200,
  0x00000636, 0xfffffdfe, 0xfffff87e, 0x00000200,
  0xfffff163, 0x00001219, 0x000009df, 0x00000200,
  0x00000c2b, 0xfffff5cc, 0xfffffa9b, 0x00000200,
  0xffffeb1b, 0xfffffdb3, 0xffffef02, 0x00000200,
  0xffffe828, 0xffffeecb, 0xfffffaee, 0x00000200,
  0x00000c23, 0x000010de, 0x00001104, 0x00000200,
  0x00000e14, 0xffffff27, 0x00001235, 0x00000200,
  0xfffff82e, 0xfffff380, 0x0000105b, 0x00000200,
  0x00000b35, 0xfffff56c, 0xfffff5ff, 0x00000200,
  0x0000177c, 0xfffffa82, 0xfffffbc2, 0x00000200,
  0xffffeef3, 0xfffffd05, 0x00000087, 0x00000200,
  0xfffff0f5, 0xffffe997, 0x0000060f, 0x00000200,
  0x0000148a, 0xffffe8c3, 0x000013e9, 0x00000200,
  0x00000e81, 0xfffffb40, 0xfffff036, 0x00000200,
  0xfffff7fb, 0xffffe9eb, 0x00000cc3, 0x00000200,
  0x000001f7, 0xfffff468, 0xfffffd8b, 0x00000200,
  0xfffff057, 0x00000508, 0xffffec40, 0x00000200,
  0xfffff802, 0x00000418, 0xfffffbb3, 0x00000200,
  0x000009f9, 0xfffff661, 0x00000590, 0x00000200,
  0xfffff0ae, 0xffffee64, 0x00000292, 0x00000200,
  0xfffff77f, 0xfffffef1, 0xfffffdbc, 0x00000200,
  0xffffeede, 0x0000069b, 0x000016d3, 0x00000200,
  0xfffffdbe, 0xffffe8bf, 0xffffe9d4, 0x00000200,
  0xfffff721, 0xfffffceb, 0xfffff3b7, 0x00000200,
  0xffffe80d, 0xfffff00b, 0x0000004b, 0x00000200,
  0xffffe973, 0x0000163c, 0xffffea08, 0x00000200,
  0x00001406, 0x000016a1, 0xfffff340, 0x00000200,
  0xffffe876, 0xffffec41, 0x000013cc, 0x00000200,
  0x00000652, 0xfffff30e, 0x000002e3, 0x00000200,
  0x000003a6, 0xffffff94, 0xffffef32, 0x00000200
};
volatile Buffer nearRow[NEAR_ROWS * NEAR_RING] = {
  0xffffc8be, 0xffffc4d5, 0x00006742, 0x0009bde5,
  0xffffb441, 0xffffdb6a, 0x000064b3, 0x000c60db,
  0xffffe03d, 0xffffb177, 0x00006433, 0x0006934d,
  0xffffb166, 0xffffacd2, 0x0000402e, 0x000a574a,
  0xffffe897, 0xffffe5f2, 0x00007e73, 0x0007a4d3
};
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
