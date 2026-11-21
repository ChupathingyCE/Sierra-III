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
typedef struct { volatile u32* memory; u32 count; u32 gap; u32 step; u32 groupSize; u32 groupOuter; u32 groupInner; } VmegTransferWords;
#line 23 "near_verdicts.vmeg.c"

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
#line 55 "near_verdicts.vmeg.c"
void nearDotsBuild(void* vmegMemory, int row) {
  meCoreMemset(vmegMemory, 0, VME_CONTEXT_WORD_COUNT*4);
  volatile u32 VME_BASE_ADDR = (u32)vmegMemory;
  vme_pe0(agu_top(MODE), agu_mode(2), ((row * 16)) & 0xffff);
  vme_pe0(agu_top(COUNT), VME_DEF_STEP, 0x60f);
  vme_pe0(agu_top(INNER_0), agu_step(1), 0xf);
  vme_pe0(agu_top(FORMAT_0), VME_RING_TOKEN);
  vme_pe0(agu_base(MODE), agu_mode(2));
  vme_pe0(agu_base(COUNT), VME_DEF_STEP, 0x61b);
  vme_pe0(agu_base(INNER_0), agu_step(1), 0x61b);
  vme_pe0(agu_write(MODE), VME_DEF_MODE);
  vme_pe0(agu_write(COUNT), VME_DEF_STEP, 0x61b);
  vme_pe0(agu_write(FORMAT_0), 5);
  vme_pe0(agu_write(FORMAT_1), VME_END_TOKEN);
  vme_pe0(vme_fu(PRIMARY), vme_mux(TOP_0, TOP_1), VME_FU_OPCODE_MAC_INNER_PRODUCT_BIAS, 8);
  vme_pe1(agu_top(MODE), agu_mode(2));
  vme_pe1(agu_top(COUNT), VME_DEF_STEP, 0x60f);
  vme_pe1(agu_top(INNER_0), agu_step(1), 0x60f);
  vme_pe1(vme_fu(PRIMARY), vme_mux(TOP_2, STAGING_0), fu_code(OP_AND));
  vme_pe2(agu_top(MODE), agu_mode(2));
  vme_pe2(agu_top(COUNT), VME_DEF_STEP, 0x60f);
  vme_pe2(agu_top(INNER_0), agu_step(1), 3);
  vme_pe2(agu_top(FORMAT_0), VME_RING_TOKEN);
  vme_pe2(agu_write(MODE), VME_DEF_MODE);
  vme_pe2(agu_write(COUNT), VME_DEF_STEP, 0x61b);
  vme_pe2(agu_write(FORMAT_0), 1);
  vme_pe2(agu_write(FORMAT_1), VME_END_TOKEN);
  vme_pe2(vme_fu(PRIMARY), vme_mux(NONE, STAGING_1), VME_FU_OPCODE_MAC_ACC_SHIFT_BIAS);
  vme_pe3(agu_write(MODE), VME_DEF_MODE, 0x30, vme_cyc(0x11));
  vme_pe3(agu_write(COUNT), VME_DEF_STEP, 0x60f);
  vme_pe3(vme_fu(PRIMARY), vme_mux(STAGING_2, BASE_2), fu_code(OP_SUB_INV_OFA));
  vme_pe3(fu_reg(PRIMARY, A), 0);
  vme_pe3(vme_fu(SECONDARY), vme_mux(BASE_0, STAGING_3), fu_code(OP_SUB_INV_OFA));
  vme_pe3(fu_reg(SECONDARY, A), 0);
  vme_icn(AGU_TOP, 0x4210);
  vme_icn(AGU_BASE, 0x4040);
  vme_icn(AGU_WRITE, 0x3240);
  vme_icn(SWEN, fu_on(0b0001));
  meCoreDcacheWritebackRange(vmegMemory, VME_CONTEXT_WORD_COUNT*4);
}
void* nearDots(int row) {
  static u32 vmegContext[VME_CONTEXT_WORD_COUNT] __attribute__((aligned(64)));
  nearDotsBuild(vmegContext, row);
  return vmegContext;
}
void* nearDotsContext;
#line 97 "near_verdicts.vmeg.c"


/* Stage 2, one row: a vertex's slot values weighted and summed. Groups of
NEAR_SLOTS words (4 or 8: the delayed copy's bubbles are NEAR_SLOTS - 3, and
a sync read needs at least one). The slot values are stage 1's results, read
in place in base3 one word in four; the weights are in top3, as top1 keeps
the vertices for the next row. */
#line 104 "near_verdicts.vmeg.c"
void nearSumBuild(void* vmegMemory) {
  meCoreMemset(vmegMemory, 0, VME_CONTEXT_WORD_COUNT*4);
  volatile u32 VME_BASE_ADDR = (u32)vmegMemory;
  vme_pe0(agu_base(MODE), agu_mode(2));
  vme_pe0(agu_base(COUNT), VME_DEF_STEP, 0x19b);
  vme_pe0(agu_base(INNER_0), agu_step(1), 0x19b);
  vme_pe0(agu_write(MODE), VME_DEF_MODE);
  vme_pe0(agu_write(COUNT), VME_DEF_STEP, 0x19b);
  vme_pe0(agu_write(FORMAT_0), 5);
  vme_pe0(agu_write(FORMAT_1), VME_END_TOKEN);
  vme_pe0(vme_fu(PRIMARY), vme_mux(BASE_3, TOP_3), VME_FU_OPCODE_MAC_INNER_PRODUCT_BIAS, 7);
  vme_pe1(vme_fu(PRIMARY), vme_mux(TOP_2, STAGING_0), fu_code(OP_AND));
  vme_pe2(agu_top(MODE), agu_mode(2));
  vme_pe2(agu_top(COUNT), VME_DEF_STEP, 0x18f);
  vme_pe2(agu_top(INNER_0), agu_step(1), 3);
  vme_pe2(agu_top(FORMAT_0), VME_RING_TOKEN);
  vme_pe2(agu_write(MODE), VME_DEF_MODE);
  vme_pe2(agu_write(COUNT), VME_DEF_STEP, 0x19b);
  vme_pe2(agu_write(FORMAT_0), 1);
  vme_pe2(agu_write(FORMAT_1), VME_END_TOKEN);
  vme_pe2(vme_fu(PRIMARY), vme_mux(NONE, STAGING_1), VME_FU_OPCODE_MAC_ACC_SHIFT_BIAS);
  vme_pe3(agu_top(MODE), agu_mode(2));
  vme_pe3(agu_top(COUNT), VME_DEF_STEP, 0x18f);
  vme_pe3(agu_top(INNER_0), agu_step(1), 0x18f);
  vme_pe3(agu_base(MODE), agu_mode(2), 3);
  vme_pe3(agu_base(COUNT), VME_DEF_STEP, 0x18f);
  vme_pe3(agu_base(INNER_0), agu_step(4), 0x63c);
  vme_pe3(agu_base(FORMAT_0), VME_RING_TOKEN);
  vme_pe3(agu_write(MODE), VME_DEF_MODE, 0x650, vme_cyc(0x11));
  vme_pe3(agu_write(COUNT), VME_DEF_STEP, 0x18f);
  vme_pe3(vme_fu(PRIMARY), vme_mux(STAGING_2, BASE_2), fu_code(OP_SUB_INV_OFA));
  vme_pe3(fu_reg(PRIMARY, A), 0);
  vme_pe3(vme_fu(SECONDARY), vme_mux(BASE_0, STAGING_3), fu_code(OP_SUB_INV_OFA));
  vme_pe3(fu_reg(SECONDARY, A), 0);
  vme_icn(AGU_TOP, 0x3244);
  vme_icn(AGU_BASE, 0x3040);
  vme_icn(AGU_WRITE, 0x3240);
  vme_icn(SWEN, fu_on(0b0001));
  meCoreDcacheWritebackRange(vmegMemory, VME_CONTEXT_WORD_COUNT*4);
}
void* nearSum(void* param) {
  static u32 vmegContext[VME_CONTEXT_WORD_COUNT] __attribute__((aligned(64)));
  nearSumBuild(vmegContext);
  return vmegContext;
}
void* nearSumContext;
#line 142 "near_verdicts.vmeg.c"


#define NEAR_COPIES (NEAR_SLOTS - 1)

/* What the ME hands over, a tile at a time: each vertex's (x, y, z, 1) in
fixed point; each slot's plane rows in order (row, slot, component); each
vertex's byte weights, a slot past the batch's own weighing 0. */
volatile u32 __attribute__((aligned(64))) nearVertex[4 * NEAR_VERTICES];
volatile u32 __attribute__((aligned(64))) nearRow[NEAR_ROWS * NEAR_RING];
volatile u32 __attribute__((aligned(64))) nearWeight[NEAR_SLOTS * NEAR_VERTICES];
/* each row's plane words, one array a row: a store names its array */
volatile u32 __attribute__((aligned(64))) nearPlane0[NEAR_VERTICES];
volatile u32 __attribute__((aligned(64))) nearPlane1[NEAR_VERTICES];
volatile u32 __attribute__((aligned(64))) nearPlane2[NEAR_VERTICES];
volatile u32 __attribute__((aligned(64))) nearPlane3[NEAR_VERTICES];
volatile u32 __attribute__((aligned(64))) nearPlane4[NEAR_VERTICES];
volatile u32 __attribute__((aligned(64))) nearZero[32];
/* the group's last product, three words ahead of the running sum's lag;
its first 4 words are stage 1's mask, its first NEAR_SLOTS stage 2's */
volatile u32 __attribute__((aligned(64))) nearMask[8] = { 0, 0, 0xffffffff, 0, 0, 0, 0, 0 };

/* one plane value on each group's last word; a store keeps the last load's
group unless it sets its own */
#line 165 "near_verdicts.vmeg.c"
VmegTransferWords vmeVarPlaneOut = { nearPlane0, 95, 3, 4, 0, 0, 0 };
#line 173 "near_verdicts.vmeg.c"


/* the contexts, built once: a pass loads its own, so no register keeps
what the last pass wrote (a loaded context is all of them, 0 where the
kernel writes none) */
volatile u32 __attribute__((aligned(64))) nearRowContext[NEAR_ROWS][VME_CONTEXT_WORD_COUNT];
static inline void nearVerdictsTile7RowsInLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 79);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void nearVerdictsTile7MaskInLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 7);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void nearVerdictsTile7ZeroIn1Load(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 15);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void nearVerdictsTile7ZeroIn2Load(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 15);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void nearVerdictsTile7WeightInLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 383);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void nearVerdictsTile7VertexInLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 383, 0);
  vme_dma(ITERATION, STEP, 4);
  vme_dma(GROUP, SIZE, 3);
  vme_dma(GROUP, STEPS, 16, 1);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x50);
}
#line 178 "near_verdicts.vmeg.c"

static inline void vmeTransferStoreWords(const VmegTransferWords* transfer, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)transfer->memory));
  vme_dma(ITERATION, DIMS, transfer->count, transfer->gap);
  vme_dma(ITERATION, STEP, transfer->step);
  vme_dma(GROUP, SIZE, transfer->groupSize);
  vme_dma(GROUP, STEPS, transfer->groupOuter, transfer->groupInner);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x58);
}
#line 178 "near_verdicts.vmeg.c"


void nearVerdictsTile(void) {
#line 181 "near_verdicts.vmeg.c"

#line 182 "near_verdicts.vmeg.c"
    {
      nearDotsBuild((void*)nearRowContext[0], 0);
#line 183 "near_verdicts.vmeg.c"

#line 184 "near_verdicts.vmeg.c"

#line 185 "near_verdicts.vmeg.c"
    }
#line 186 "near_verdicts.vmeg.c"
    {
      nearDotsBuild((void*)nearRowContext[1], 1);
#line 187 "near_verdicts.vmeg.c"

#line 188 "near_verdicts.vmeg.c"

#line 189 "near_verdicts.vmeg.c"
    }
#line 190 "near_verdicts.vmeg.c"
    {
      nearDotsBuild((void*)nearRowContext[2], 2);
#line 191 "near_verdicts.vmeg.c"

#line 192 "near_verdicts.vmeg.c"

#line 193 "near_verdicts.vmeg.c"
    }
#line 194 "near_verdicts.vmeg.c"
    {
      nearDotsBuild((void*)nearRowContext[3], 3);
#line 195 "near_verdicts.vmeg.c"

#line 196 "near_verdicts.vmeg.c"

#line 197 "near_verdicts.vmeg.c"
    }
#line 198 "near_verdicts.vmeg.c"
    {
      nearDotsBuild((void*)nearRowContext[4], 4);
#line 199 "near_verdicts.vmeg.c"

#line 200 "near_verdicts.vmeg.c"

#line 201 "near_verdicts.vmeg.c"
    }
#line 202 "near_verdicts.vmeg.c"
    {
      nearSumContext = nearSum(0);
#line 203 "near_verdicts.vmeg.c"
    }
    /* a memory kernel's config loads its context and triggers it (vme2-next-level's
    vmeLibLoadCustomContext, vmeLibFinish), so this call's pass runs before its loads
    and its words are dropped: the box loads first and then triggers, which the
    call below is */
#line 208 "near_verdicts.vmeg.c"
    {
      vmeLibEnable();
      vmeLibStart();
      vmeLibLoadCustomContext((void*)nearRowContext[0]);
      vmeLibFinish();
#line 209 "near_verdicts.vmeg.c"

#line 210 "near_verdicts.vmeg.c"

#line 211 "near_verdicts.vmeg.c"

      /* the lead-ins read zeros, not whatever the last tile left */
#line 213 "near_verdicts.vmeg.c"

#line 214 "near_verdicts.vmeg.c"

#line 215 "near_verdicts.vmeg.c"

      /* each vertex once for each slot: the copies side by side */
#line 223 "near_verdicts.vmeg.c"

#line 225 "near_verdicts.vmeg.c"
      nearVerdictsTile7RowsInLoad(nearRow, VME_TOP_BUFF0_WOFF);
      meCoreDMACPrimWaitTransferFinish();
#line 225 "near_verdicts.vmeg.c"

#line 226 "near_verdicts.vmeg.c"
      nearVerdictsTile7MaskInLoad(nearMask, VME_TOP_BUFF2_WOFF);
      meCoreDMACPrimWaitTransferFinish();
#line 226 "near_verdicts.vmeg.c"

#line 227 "near_verdicts.vmeg.c"
      nearVerdictsTile7ZeroIn1Load(nearZero, VME_TOP_BUFF1_WOFF);
      meCoreDMACPrimWaitTransferFinish();
#line 227 "near_verdicts.vmeg.c"

#line 228 "near_verdicts.vmeg.c"
      nearVerdictsTile7ZeroIn2Load(nearZero, VME_TOP_BUFF3_WOFF);
      meCoreDMACPrimWaitTransferFinish();
#line 228 "near_verdicts.vmeg.c"

#line 229 "near_verdicts.vmeg.c"
      nearVerdictsTile7WeightInLoad(nearWeight, VME_TOP_BUFF3_WOFF + 0x10);
      meCoreDMACPrimWaitTransferFinish();
#line 229 "near_verdicts.vmeg.c"

#line 230 "near_verdicts.vmeg.c"
      nearVerdictsTile7VertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x10);
      nearVerdictsTile7VertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x14);
      meCoreDMACPrimWaitTransferFinish();
      nearVerdictsTile7VertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x18);
      nearVerdictsTile7VertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 0x1c);
      meCoreDMACPrimWaitTransferFinish();
#line 232 "near_verdicts.vmeg.c"

#line 233 "near_verdicts.vmeg.c"
#line 233 "near_verdicts.vmeg.c"
    }
#line 234 "near_verdicts.vmeg.c"
    {
      vmeLibStart();
      vmeLibLoadCustomContext((void*)nearRowContext[0]);
      vmeLibFinish();
#line 235 "near_verdicts.vmeg.c"

#line 236 "near_verdicts.vmeg.c"
#line 236 "near_verdicts.vmeg.c"
    }
#line 237 "near_verdicts.vmeg.c"
    {
      vmeLibStart();
      vmeLibLoadCustomContext(nearSumContext);
      vmeLibFinish();
#line 238 "near_verdicts.vmeg.c"
      {
        VmegTransferWords vmeCopyVar = vmeVarPlaneOut;
        vmeCopyVar.memory = nearPlane0;
        vmeTransferStoreWords(&vmeCopyVar, VME_BASE_BUFF3_WOFF + 0x663);
        meCoreDMACPrimWaitTransferFinish();
      }
#line 238 "near_verdicts.vmeg.c"

#line 239 "near_verdicts.vmeg.c"
#line 239 "near_verdicts.vmeg.c"
    }
#line 240 "near_verdicts.vmeg.c"
    {
      vmeLibStart();
      vmeLibLoadCustomContext((void*)nearRowContext[1]);
      vmeLibFinish();
#line 241 "near_verdicts.vmeg.c"

#line 242 "near_verdicts.vmeg.c"
#line 242 "near_verdicts.vmeg.c"
    }
#line 243 "near_verdicts.vmeg.c"
    {
      vmeLibStart();
      vmeLibLoadCustomContext(nearSumContext);
      vmeLibFinish();
#line 244 "near_verdicts.vmeg.c"
      {
        VmegTransferWords vmeCopyVar = vmeVarPlaneOut;
        vmeCopyVar.memory = nearPlane1;
        vmeTransferStoreWords(&vmeCopyVar, VME_BASE_BUFF3_WOFF + 0x663);
        meCoreDMACPrimWaitTransferFinish();
      }
#line 244 "near_verdicts.vmeg.c"

#line 245 "near_verdicts.vmeg.c"
#line 245 "near_verdicts.vmeg.c"
    }
#line 246 "near_verdicts.vmeg.c"
    {
      vmeLibStart();
      vmeLibLoadCustomContext((void*)nearRowContext[2]);
      vmeLibFinish();
#line 247 "near_verdicts.vmeg.c"

#line 248 "near_verdicts.vmeg.c"
#line 248 "near_verdicts.vmeg.c"
    }
#line 249 "near_verdicts.vmeg.c"
    {
      vmeLibStart();
      vmeLibLoadCustomContext(nearSumContext);
      vmeLibFinish();
#line 250 "near_verdicts.vmeg.c"
      {
        VmegTransferWords vmeCopyVar = vmeVarPlaneOut;
        vmeCopyVar.memory = nearPlane2;
        vmeTransferStoreWords(&vmeCopyVar, VME_BASE_BUFF3_WOFF + 0x663);
        meCoreDMACPrimWaitTransferFinish();
      }
#line 250 "near_verdicts.vmeg.c"

#line 251 "near_verdicts.vmeg.c"
#line 251 "near_verdicts.vmeg.c"
    }
#line 252 "near_verdicts.vmeg.c"
    {
      vmeLibStart();
      vmeLibLoadCustomContext((void*)nearRowContext[3]);
      vmeLibFinish();
#line 253 "near_verdicts.vmeg.c"

#line 254 "near_verdicts.vmeg.c"
#line 254 "near_verdicts.vmeg.c"
    }
#line 255 "near_verdicts.vmeg.c"
    {
      vmeLibStart();
      vmeLibLoadCustomContext(nearSumContext);
      vmeLibFinish();
#line 256 "near_verdicts.vmeg.c"
      {
        VmegTransferWords vmeCopyVar = vmeVarPlaneOut;
        vmeCopyVar.memory = nearPlane3;
        vmeTransferStoreWords(&vmeCopyVar, VME_BASE_BUFF3_WOFF + 0x663);
        meCoreDMACPrimWaitTransferFinish();
      }
#line 256 "near_verdicts.vmeg.c"

#line 257 "near_verdicts.vmeg.c"
#line 257 "near_verdicts.vmeg.c"
    }
#line 258 "near_verdicts.vmeg.c"
    {
      vmeLibStart();
      vmeLibLoadCustomContext((void*)nearRowContext[4]);
      vmeLibFinish();
#line 259 "near_verdicts.vmeg.c"

#line 260 "near_verdicts.vmeg.c"
#line 260 "near_verdicts.vmeg.c"
    }
#line 261 "near_verdicts.vmeg.c"
    {
      vmeLibStart();
      vmeLibLoadCustomContext(nearSumContext);
      vmeLibFinish();
#line 262 "near_verdicts.vmeg.c"
      {
        VmegTransferWords vmeCopyVar = vmeVarPlaneOut;
        vmeCopyVar.memory = nearPlane4;
        vmeTransferStoreWords(&vmeCopyVar, VME_BASE_BUFF3_WOFF + 0x663);
        meCoreDMACPrimWaitTransferFinish();
      }
#line 262 "near_verdicts.vmeg.c"

#line 263 "near_verdicts.vmeg.c"
      vmeLibDisable();
#line 263 "near_verdicts.vmeg.c"
    }
#line 264 "near_verdicts.vmeg.c"

}
