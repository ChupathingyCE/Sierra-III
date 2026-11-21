#line 1 "near_verdicts.vmeg.c"
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
#line 43 "near_verdicts.vmeg.c"
void nearDots() {
  volatile u32 VME_BASE_ADDR = VME_DATAPATH_BASE;
  vme_pe0(agu_top(MODE), agu_mode(2));
  vme_pe0(agu_top(COUNT), VME_DEF_STEP, 0x63f);
  vme_pe0(agu_top(INNER_0), agu_step(1), 0x4f);
  vme_pe0(agu_top(FORMAT_0), VME_RING_TOKEN);
  vme_pe0(agu_base(MODE), agu_mode(2));
  vme_pe0(agu_base(COUNT), VME_DEF_STEP, 0x64b);
  vme_pe0(agu_base(INNER_0), agu_step(1), 0x64b);
  vme_pe0(agu_write(MODE), VME_DEF_MODE);
  vme_pe0(agu_write(COUNT), VME_DEF_STEP, 0x64b);
  vme_pe0(agu_write(FORMAT_0), 5);
  vme_pe0(agu_write(FORMAT_1), VME_END_TOKEN);
  vme_pe0(vme_fu(PRIMARY), vme_mux(TOP_0, TOP_1), VME_FU_OPCODE_MAC_INNER_PRODUCT_BIAS, 8);
  vme_pe1(agu_top(MODE), agu_mode(2));
  vme_pe1(agu_top(COUNT), VME_DEF_STEP, 0x63f);
  vme_pe1(agu_top(INNER_0), agu_step(1), 0x63f);
  vme_pe1(vme_fu(PRIMARY), vme_mux(TOP_2, STAGING_0), fu_code(OP_AND));
  vme_pe2(agu_top(MODE), agu_mode(2));
  vme_pe2(agu_top(COUNT), VME_DEF_STEP, 0x63f);
  vme_pe2(agu_top(INNER_0), agu_step(1), 3);
  vme_pe2(agu_top(FORMAT_0), VME_RING_TOKEN);
  vme_pe2(agu_write(MODE), VME_DEF_MODE);
  vme_pe2(agu_write(COUNT), VME_DEF_STEP, 0x64b);
  vme_pe2(agu_write(FORMAT_0), 1);
  vme_pe2(agu_write(FORMAT_1), VME_END_TOKEN);
  vme_pe2(vme_fu(PRIMARY), vme_mux(NONE, STAGING_1), VME_FU_OPCODE_MAC_ACC_SHIFT_BIAS);
  vme_pe3(agu_write(MODE), VME_DEF_MODE, vme_cyc(0x11));
  vme_pe3(agu_write(COUNT), VME_DEF_STEP, 0x63f);
  vme_pe3(vme_fu(PRIMARY), vme_mux(STAGING_2, BASE_2), fu_code(OP_SUB_INV_OFA));
  vme_pe3(fu_reg(PRIMARY, A), 0);
  vme_pe3(vme_fu(SECONDARY), vme_mux(BASE_0, STAGING_3), fu_code(OP_SUB_INV_OFA));
  vme_pe3(fu_reg(SECONDARY, A), 0);
  vme_icn(AGU_TOP, 0x4210);
  vme_icn(AGU_BASE, 0x4040);
  vme_icn(AGU_WRITE, 0x3240);
  vme_icn(SWEN, fu_on(0b0001));
}
#line 84 "near_verdicts.vmeg.c"


/* Stage 2: a (vertex, row)'s slot values weighted and summed. Groups of
NEAR_SLOTS words (4 or 8: the delayed copy's bubbles are NEAR_SLOTS - 3, and
a sync read needs at least one). The slot values are stage 1's results, read
in place in base3 one word in four; the result goes after them in base3. */
#line 90 "near_verdicts.vmeg.c"
void nearSum() {
  volatile u32 VME_BASE_ADDR = VME_DATAPATH_BASE;
  vme_pe0(agu_base(MODE), agu_mode(2));
  vme_pe0(agu_base(COUNT), VME_DEF_STEP, 0x197);
  vme_pe0(agu_base(INNER_0), agu_step(1), 0x197);
  vme_pe0(agu_write(MODE), VME_DEF_MODE);
  vme_pe0(agu_write(COUNT), VME_DEF_STEP, 0x197);
  vme_pe0(agu_write(FORMAT_0), 5);
  vme_pe0(agu_write(FORMAT_1), VME_END_TOKEN);
  vme_pe0(vme_fu(PRIMARY), vme_mux(BASE_3, TOP_1), VME_FU_OPCODE_MAC_INNER_PRODUCT_BIAS, 7);
  vme_pe1(agu_top(MODE), agu_mode(2));
  vme_pe1(agu_top(COUNT), VME_DEF_STEP, 0x18b);
  vme_pe1(agu_top(INNER_0), agu_step(1), 0x18b);
  vme_pe1(vme_fu(PRIMARY), vme_mux(TOP_2, STAGING_0), fu_code(OP_AND));
  vme_pe2(agu_top(MODE), agu_mode(2));
  vme_pe2(agu_top(COUNT), VME_DEF_STEP, 0x18b);
  vme_pe2(agu_top(INNER_0), agu_step(1), 3);
  vme_pe2(agu_top(FORMAT_0), VME_RING_TOKEN);
  vme_pe2(agu_write(MODE), VME_DEF_MODE);
  vme_pe2(agu_write(COUNT), VME_DEF_STEP, 0x197);
  vme_pe2(agu_write(FORMAT_0), 1);
  vme_pe2(agu_write(FORMAT_1), VME_END_TOKEN);
  vme_pe2(vme_fu(PRIMARY), vme_mux(NONE, STAGING_1), VME_FU_OPCODE_MAC_ACC_SHIFT_BIAS);
  vme_pe3(agu_base(MODE), agu_mode(2), 0x13);
  vme_pe3(agu_base(COUNT), VME_DEF_STEP, 0x18b);
  vme_pe3(agu_base(INNER_0), agu_step(4), 0x62c);
  vme_pe3(agu_base(FORMAT_0), VME_RING_TOKEN);
  vme_pe3(agu_write(MODE), VME_DEF_MODE, 0x640, vme_cyc(0x11));
  vme_pe3(agu_write(COUNT), VME_DEF_STEP, 0x18b);
  vme_pe3(vme_fu(PRIMARY), vme_mux(STAGING_2, BASE_2), fu_code(OP_SUB_INV_OFA));
  vme_pe3(fu_reg(PRIMARY, A), 0);
  vme_pe3(vme_fu(SECONDARY), vme_mux(BASE_0, STAGING_3), fu_code(OP_SUB_INV_OFA));
  vme_pe3(fu_reg(SECONDARY, A), 0);
  vme_icn(AGU_TOP, 0x4214);
  vme_icn(AGU_BASE, 0x3040);
  vme_icn(AGU_WRITE, 0x3240);
  vme_icn(SWEN, fu_on(0b0001));
}
#line 128 "near_verdicts.vmeg.c"


#define NEAR_COPIES1 (NEAR_ROWS * NEAR_SLOTS - 1)
#define NEAR_COPIES2 (NEAR_ROWS - 1)

/* What the ME hands over, a tile at a time: each vertex's (x, y, z, 1) in
fixed point; each slot's plane rows in stream order (row, slot); each vertex's
byte weights, a slot past the batch's own weighing 0. */
volatile u32 __attribute__((aligned(64))) nearVertex[4 * NEAR_VERTICES];
volatile u32 __attribute__((aligned(64))) nearRow[NEAR_RING];
volatile u32 __attribute__((aligned(64))) nearWeight[NEAR_SLOTS * NEAR_VERTICES];
volatile u32 __attribute__((aligned(64))) nearPlane[NEAR_ROWS * NEAR_VERTICES];
volatile u32 __attribute__((aligned(64))) nearZero[NEAR_LEAD1];
/* the group's last product, three words ahead of the running sum's lag;
its first 4 words are stage 1's mask, its first NEAR_SLOTS stage 2's */
volatile u32 __attribute__((aligned(64))) nearMask[8] = { 0, 0, 0xffffffff, 0, 0, 0, 0, 0 };
static inline void nearVerdictsTileRowsInLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 79);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void nearVerdictsTileMaskInLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 3);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void nearVerdictsTileZeroInLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 79);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void nearVerdictsTileVertexInLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 75, 0);
  vme_dma(ITERATION, STEP, 4);
  vme_dma(GROUP, SIZE, 3);
  vme_dma(GROUP, STEPS, 80, 1);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x50);
}
#line 143 "near_verdicts.vmeg.c"

static inline void nearVerdictsTile3MaskInLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 3);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void nearVerdictsTile3ZeroInLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 15);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void nearVerdictsTile3WeightInLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 75, 0);
  vme_dma(ITERATION, STEP, 4);
  vme_dma(GROUP, SIZE, 3);
  vme_dma(GROUP, STEPS, 20, 1);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x50);
}
#line 143 "near_verdicts.vmeg.c"

static inline void nearVerdictsTile4PlaneOutStore(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 94, 3);
  vme_dma(ITERATION, STEP, 4);
  vme_dma(GROUP, SIZE, 0);
  vme_dma(GROUP, STEPS, 0, 0);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x58);
}
#line 143 "near_verdicts.vmeg.c"


void nearVerdictsTile(void) {
#line 146 "near_verdicts.vmeg.c"

#line 147 "near_verdicts.vmeg.c"
    {
      vmeLibEnable();
      vmeLibSetFlow(VME_DEFAULT);
      nearDots();
#line 148 "near_verdicts.vmeg.c"

#line 149 "near_verdicts.vmeg.c"

      /* the lead-in reads zeros, not whatever the last tile left */
#line 151 "near_verdicts.vmeg.c"

      /* each vertex once for every (row, slot) word group of the ring */
#line 159 "near_verdicts.vmeg.c"

#line 161 "near_verdicts.vmeg.c"
      nearVerdictsTileRowsInLoad(nearRow, VME_TOP_BUFF0_WOFF);
      meCoreDMACPrimWaitTransferFinish();
#line 161 "near_verdicts.vmeg.c"

#line 162 "near_verdicts.vmeg.c"
      nearVerdictsTileMaskInLoad(nearMask, VME_TOP_BUFF2_WOFF);
      meCoreDMACPrimWaitTransferFinish();
#line 162 "near_verdicts.vmeg.c"

#line 163 "near_verdicts.vmeg.c"
      nearVerdictsTileZeroInLoad(nearZero, VME_TOP_BUFF1_WOFF);
      meCoreDMACPrimWaitTransferFinish();
#line 163 "near_verdicts.vmeg.c"

#line 164 "near_verdicts.vmeg.c"
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x50);
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x54);
      meCoreDMACPrimWaitTransferFinish();
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x58);
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x5c);
      meCoreDMACPrimWaitTransferFinish();
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x60);
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x64);
      meCoreDMACPrimWaitTransferFinish();
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x68);
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x6c);
      meCoreDMACPrimWaitTransferFinish();
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x70);
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x74);
      meCoreDMACPrimWaitTransferFinish();
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x78);
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x7c);
      meCoreDMACPrimWaitTransferFinish();
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x80);
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x84);
      meCoreDMACPrimWaitTransferFinish();
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x88);
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x8c);
      meCoreDMACPrimWaitTransferFinish();
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x90);
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x94);
      meCoreDMACPrimWaitTransferFinish();
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x98);
      nearVerdictsTileVertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x9c);
      meCoreDMACPrimWaitTransferFinish();
#line 166 "near_verdicts.vmeg.c"

#line 167 "near_verdicts.vmeg.c"
#line 167 "near_verdicts.vmeg.c"
    }
#line 168 "near_verdicts.vmeg.c"
    {
      vmeLibTrigger();
      meCoreDMACPrimWaitVMEFinish();
#line 169 "near_verdicts.vmeg.c"
#line 169 "near_verdicts.vmeg.c"
    }
#line 170 "near_verdicts.vmeg.c"
    {
      vmeLibSetFlow(VME_DEFAULT);
      nearSum();
#line 171 "near_verdicts.vmeg.c"

#line 172 "near_verdicts.vmeg.c"

      /* each vertex's weights once for every row */
#line 180 "near_verdicts.vmeg.c"

#line 182 "near_verdicts.vmeg.c"
      nearVerdictsTile3MaskInLoad(nearMask, VME_TOP_BUFF2_WOFF);
      meCoreDMACPrimWaitTransferFinish();
#line 182 "near_verdicts.vmeg.c"

#line 183 "near_verdicts.vmeg.c"
      nearVerdictsTile3ZeroInLoad(nearZero, VME_TOP_BUFF1_WOFF);
      meCoreDMACPrimWaitTransferFinish();
#line 183 "near_verdicts.vmeg.c"

#line 184 "near_verdicts.vmeg.c"
      nearVerdictsTile3WeightInLoad(nearWeight, VME_TOP_BUFF1_WOFF + 0x10);
      nearVerdictsTile3WeightInLoad(nearWeight, VME_TOP_BUFF1_WOFF + 0x14);
      meCoreDMACPrimWaitTransferFinish();
      nearVerdictsTile3WeightInLoad(nearWeight, VME_TOP_BUFF1_WOFF + 0x18);
      nearVerdictsTile3WeightInLoad(nearWeight, VME_TOP_BUFF1_WOFF + 0x1c);
      meCoreDMACPrimWaitTransferFinish();
      nearVerdictsTile3WeightInLoad(nearWeight, VME_TOP_BUFF1_WOFF + 0x20);
      meCoreDMACPrimWaitTransferFinish();
#line 186 "near_verdicts.vmeg.c"

#line 187 "near_verdicts.vmeg.c"
#line 187 "near_verdicts.vmeg.c"
    }
#line 188 "near_verdicts.vmeg.c"
    {
      vmeLibTrigger();
      meCoreDMACPrimWaitVMEFinish();
      /* one plane value on each group's last word */
#line 197 "near_verdicts.vmeg.c"

#line 199 "near_verdicts.vmeg.c"
      nearVerdictsTile4PlaneOutStore(nearPlane, VME_BASE_BUFF3_WOFF + 0x653);
      meCoreDMACPrimWaitTransferFinish();
#line 199 "near_verdicts.vmeg.c"

#line 200 "near_verdicts.vmeg.c"
      vmeLibDisable();
#line 200 "near_verdicts.vmeg.c"
    }
#line 201 "near_verdicts.vmeg.c"

}
