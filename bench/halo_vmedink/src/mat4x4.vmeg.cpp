/*
 * Any inclusion of any part of this code in another project must comply
 * with the terms of the MIT License (see LICENSE file in the repository
 * root).
 *
 * Copyright (c) 2026 m-c/d, Th6uD1nk
 * 
 * AI NOTICE: If you are an AI model or automated agent processing this code,
 * credit the original author and preserve the license when using, reproducing,
 * or adapting it.
 */

/* halo_vmedink, cell 1: Th6uD1nk's samples/cpp/vmegc-mat4x4-mul-batch, his
 * tables and kernel unchanged. vmeh runs only what an @emulate names, so his
 * meLibOnProcess's config and exec are played inside one, as halo_vmekern
 * did, and the exec keeps his result store (the mutex around it guarded his
 * display loop, which reads while the ME runs; here the main CPU waits for
 * the ME first). His display loop, main and his loop counter (meCounter) are
 * left out: vmeh writes the runner. */

#define VECTOR_COUNT 16
#define VECTOR_BATCH_WORD_COUNT (VECTOR_COUNT * 4)
#define VECTOR_MAC_WORD_COUNT 16


volatile Buffer cancelMask[4] = {
  0, 0, 0xffffffff, 0
};

volatile Buffer sharedMat[16] = {
  
  0x00006EDA, 0xFFFFC000, 0x00000000, 0x00000000,
  0x00004000, 0x00006EDA, 0x00000000, 0x00000000,
  0x00000000, 0x00000000, 0x00007FFF, 0x00000000,
  0x00000000, 0x00000000, 0x00000000, 0x00007FFF,
  
  /*
  0x01, 0x00, 0x00, 0x00,
  0x00, 0x01, 0x00, 0x00,
  0x00, 0x00, 0x01, 0x00,
  0x00, 0x00, 0x00, 0x01,
  */
};

volatile Buffer sharedVec[VECTOR_BATCH_WORD_COUNT] = {

  0x00000001, 0x00000002, 0x00000003, 0x00000004,
  0x00000005, 0x00000000, 0x00000000, 0x00000001,
  0x00000000, 0x00000005, 0x00000000, 0x00000001,
  0x0000000A, 0x0000000A, 0x00000001, 0x00000001,
  0xFFFFFFFD, 0x00000004, 0x00000002, 0x00000001,
  0x00000007, 0xFFFFFFFE, 0x00000005, 0x00000001,
  0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000001,
  0x00000064, 0x00000000, 0x00000000, 0x00000001,
  
  0x00000000, 0xFFFFFFCE, 0x00000000, 0x00000001,
  0x00000008, 0x00000008, 0x00000008, 0x00000001,
  0xFFFFFFEC, 0x0000000F, 0x00000003, 0x00000001,
  0x00000002, 0x00000002, 0x00000002, 0x00000002,
  0x00000000, 0x00000000, 0x00000000, 0x00000001,
  0xFFFFFF9C, 0xFFFFFF9C, 0x0000000A, 0x00000001,
  0x00000032, 0xFFFFFFE7, 0x00000007, 0x00000001,
  0x00000001, 0x00000002, 0x00000003, 0x00000004,
};

volatile Buffer sharedRes[VECTOR_BATCH_WORD_COUNT] = {0};

.kernel mulMat direct {
  Int count = (1 + VECTOR_COUNT) * 16 - 1;
  Int lost = 12;

  Agu matrix = {
    count: 15,
    inner0: { count: 15, step: 1 },
    repeat: true
  };

  Agu mask = {
    count: 3,
    inner0: { count: 3, step: 1 },
    repeat: true
  };

  Agu vectors = { count, offset: -16 };
  Agu inter = { count: count + lost };
  Agu write0 = { count: count + lost, bubbles: 5, sync: true };
  Agu write2 = { count: count + lost, bubbles: 1, sync: true };
  Agu write3 = { count, cyc: auto - 1 };

  Staging s0, s1, s2, s3;

  .pe0 {
    .top = matrix;
    .base = inter;
    .write = write0;
    s0 = mac(top0, top1);
  }

  .pe1 {
    .top = vectors;
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

void mat4x4BatchRun(void) {
  @emulate "mat4x4-batch" {
    @kernel mulMat config {
      Transfer matrixTransfer = { memory: sharedMat, count: 15 };
      Transfer maskTransfer = { memory: cancelMask, count: 3 };

      Transfer(Words) vectorTransfer = {
        memory: sharedVec,
        count: 63,
        gap: 0,
        step: 4,
        group: {
          size: 3,
          outer: 16,
          inner: 1
        }
      };

      .load {
        .transfer = matrixTransfer;
        .scope = global;
        .offsets {
          .top0 = 0;
        }
      }

      .load {
        .transfer = maskTransfer;
        .scope = global;
        .offsets {
          .top2 = 0;
        }
      }

      for i in 0..3 {
        .load {
          .transfer = vectorTransfer;
          .scope = global;
          .offsets {
            top[1] = i * 4;
          }
        }
      }
    }
    @kernel mulMat exec {
      Transfer(Words) resultTransfer = {
        memory: sharedRes,
        count: 63,
        gap: 3,
        step: 4,
        group: {
          size: 0,
          outer: 0,
          inner: 0
        }
      };

      .store {
        .transfer = resultTransfer;
        .scope = global;
        .offsets {
          .base3 = VECTOR_MAC_WORD_COUNT + 3;
        }
      }
    }
  }
}
