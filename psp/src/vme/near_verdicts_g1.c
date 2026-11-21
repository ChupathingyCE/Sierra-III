#line 1 "near_verdicts_g1.vmeg.c"
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
typedef struct { volatile u32* memory; u32 count; u32 gap; u32 step; u32 groupSize; u32 groupOuter; u32 groupInner; } VmegTransferWords;
#line 24 "near_verdicts_g1.vmeg.c"

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
#line 47 "near_verdicts_g1.vmeg.c"
void nearDotsBuild(void* vmegMemory, int row) {
  meCoreMemset(vmegMemory, 0, VME_CONTEXT_WORD_COUNT*4);
  volatile u32 VME_BASE_ADDR = (u32)vmegMemory;
  vme_pe0(agu_top(MODE), agu_mode(2), ((row * 4)) & 0xffff);
  vme_pe0(agu_top(COUNT), VME_DEF_STEP, 0x183);
  vme_pe0(agu_top(INNER_0), agu_step(1), 3);
  vme_pe0(agu_top(FORMAT_0), VME_RING_TOKEN);
  vme_pe0(agu_base(MODE), agu_mode(2));
  vme_pe0(agu_base(COUNT), VME_DEF_STEP, 0x18f);
  vme_pe0(agu_base(INNER_0), agu_step(1), 0x18f);
  vme_pe0(agu_write(MODE), VME_DEF_MODE);
  vme_pe0(agu_write(COUNT), VME_DEF_STEP, 0x18f);
  vme_pe0(agu_write(FORMAT_0), 5);
  vme_pe0(agu_write(FORMAT_1), VME_END_TOKEN);
  vme_pe0(vme_fu(PRIMARY), vme_mux(TOP_0, TOP_1), VME_FU_OPCODE_MAC_INNER_PRODUCT_BIAS, 8);
  vme_pe1(agu_top(MODE), agu_mode(2));
  vme_pe1(agu_top(COUNT), VME_DEF_STEP, 0x183);
  vme_pe1(agu_top(INNER_0), agu_step(1), 0x183);
  vme_pe1(vme_fu(PRIMARY), vme_mux(TOP_2, STAGING_0), fu_code(OP_AND));
  vme_pe2(agu_top(MODE), agu_mode(2));
  vme_pe2(agu_top(COUNT), VME_DEF_STEP, 0x183);
  vme_pe2(agu_top(INNER_0), agu_step(1), 3);
  vme_pe2(agu_top(FORMAT_0), VME_RING_TOKEN);
  vme_pe2(agu_write(MODE), VME_DEF_MODE);
  vme_pe2(agu_write(COUNT), VME_DEF_STEP, 0x18f);
  vme_pe2(agu_write(FORMAT_0), 1);
  vme_pe2(agu_write(FORMAT_1), VME_END_TOKEN);
  vme_pe2(vme_fu(PRIMARY), vme_mux(NONE, STAGING_1), VME_FU_OPCODE_MAC_ACC_SHIFT_BIAS);
  vme_pe3(agu_write(MODE), VME_DEF_MODE, 0x30, vme_cyc(0x11));
  vme_pe3(agu_write(COUNT), VME_DEF_STEP, 0x183);
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
#line 89 "near_verdicts_g1.vmeg.c"


/* What the ME hands over, a tile at a time: each vertex's (x, y, z, 1) in
fixed point and the slot's five plane rows. */
volatile u32 __attribute__((aligned(64))) nearVertex[4 * NEAR_VERTICES];
volatile u32 __attribute__((aligned(64))) nearRow[NEAR_ROWS * NEAR_RING];
volatile u32 __attribute__((aligned(64))) nearPlane0[NEAR_VERTICES];
volatile u32 __attribute__((aligned(64))) nearPlane1[NEAR_VERTICES];
volatile u32 __attribute__((aligned(64))) nearPlane2[NEAR_VERTICES];
volatile u32 __attribute__((aligned(64))) nearPlane3[NEAR_VERTICES];
volatile u32 __attribute__((aligned(64))) nearPlane4[NEAR_VERTICES];
volatile u32 __attribute__((aligned(64))) nearZero[32];
volatile u32 __attribute__((aligned(64))) nearMask[8] = { 0, 0, 0xffffffff, 0, 0, 0, 0, 0 };

/* the dot product on each group's last word, stage 1's own words */
#line 104 "near_verdicts_g1.vmeg.c"
VmegTransferWords vmeVarPlaneOut = { nearPlane0, 95, 3, 4, 0, 0, 0 };
#line 112 "near_verdicts_g1.vmeg.c"


/* the contexts, built once: a pass loads its own, so no register keeps
what the last pass wrote (a loaded context is all of them, 0 where the
kernel writes none) */
volatile u32 __attribute__((aligned(64))) nearRowContext[NEAR_ROWS][VME_CONTEXT_WORD_COUNT];
static inline void nearVerdictsTile6RowsInLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 19);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void nearVerdictsTile6MaskInLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 7);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void nearVerdictsTile6ZeroIn1Load(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 3);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void nearVerdictsTile6VertexInLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 383);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}
#line 117 "near_verdicts_g1.vmeg.c"

static inline void vmeTransferStoreWords(const VmegTransferWords* transfer, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)transfer->memory));
  vme_dma(ITERATION, DIMS, transfer->count, transfer->gap);
  vme_dma(ITERATION, STEP, transfer->step);
  vme_dma(GROUP, SIZE, transfer->groupSize);
  vme_dma(GROUP, STEPS, transfer->groupOuter, transfer->groupInner);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x58);
}
#line 117 "near_verdicts_g1.vmeg.c"


void nearVerdictsTile(void) {
#line 120 "near_verdicts_g1.vmeg.c"

#line 121 "near_verdicts_g1.vmeg.c"
    {
      nearDotsBuild((void*)nearRowContext[0], 0);
#line 122 "near_verdicts_g1.vmeg.c"

#line 123 "near_verdicts_g1.vmeg.c"

#line 124 "near_verdicts_g1.vmeg.c"
    }
#line 125 "near_verdicts_g1.vmeg.c"
    {
      nearDotsBuild((void*)nearRowContext[1], 1);
#line 126 "near_verdicts_g1.vmeg.c"

#line 127 "near_verdicts_g1.vmeg.c"

#line 128 "near_verdicts_g1.vmeg.c"
    }
#line 129 "near_verdicts_g1.vmeg.c"
    {
      nearDotsBuild((void*)nearRowContext[2], 2);
#line 130 "near_verdicts_g1.vmeg.c"

#line 131 "near_verdicts_g1.vmeg.c"

#line 132 "near_verdicts_g1.vmeg.c"
    }
#line 133 "near_verdicts_g1.vmeg.c"
    {
      nearDotsBuild((void*)nearRowContext[3], 3);
#line 134 "near_verdicts_g1.vmeg.c"

#line 135 "near_verdicts_g1.vmeg.c"

#line 136 "near_verdicts_g1.vmeg.c"
    }
#line 137 "near_verdicts_g1.vmeg.c"
    {
      nearDotsBuild((void*)nearRowContext[4], 4);
#line 138 "near_verdicts_g1.vmeg.c"

#line 139 "near_verdicts_g1.vmeg.c"

#line 140 "near_verdicts_g1.vmeg.c"
    }
    /* a memory kernel's config loads its context and triggers it (vme2-next-level's
    vmeLibLoadCustomContext, vmeLibFinish), so this call's pass runs before its loads
    and its words are dropped: the box loads first and then triggers, which the
    call below is */
#line 145 "near_verdicts_g1.vmeg.c"
    {
      vmeLibEnable();
      vmeLibStart();
      vmeLibLoadCustomContext((void*)nearRowContext[0]);
      vmeLibFinish();
#line 146 "near_verdicts_g1.vmeg.c"

#line 147 "near_verdicts_g1.vmeg.c"

#line 148 "near_verdicts_g1.vmeg.c"

      /* the lead-in reads zeros, not whatever the last tile left */
#line 150 "near_verdicts_g1.vmeg.c"

#line 151 "near_verdicts_g1.vmeg.c"

#line 153 "near_verdicts_g1.vmeg.c"
      nearVerdictsTile6RowsInLoad(nearRow, VME_TOP_BUFF0_WOFF);
      meCoreDMACPrimWaitTransferFinish();
#line 153 "near_verdicts_g1.vmeg.c"

#line 154 "near_verdicts_g1.vmeg.c"
      nearVerdictsTile6MaskInLoad(nearMask, VME_TOP_BUFF2_WOFF);
      meCoreDMACPrimWaitTransferFinish();
#line 154 "near_verdicts_g1.vmeg.c"

#line 155 "near_verdicts_g1.vmeg.c"
      nearVerdictsTile6ZeroIn1Load(nearZero, VME_TOP_BUFF1_WOFF);
      meCoreDMACPrimWaitTransferFinish();
#line 155 "near_verdicts_g1.vmeg.c"

#line 156 "near_verdicts_g1.vmeg.c"
      nearVerdictsTile6VertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 4);
      meCoreDMACPrimWaitTransferFinish();
#line 156 "near_verdicts_g1.vmeg.c"

#line 157 "near_verdicts_g1.vmeg.c"
#line 157 "near_verdicts_g1.vmeg.c"
    }
#line 158 "near_verdicts_g1.vmeg.c"
    {
      vmeLibStart();
      vmeLibLoadCustomContext((void*)nearRowContext[0]);
      vmeLibFinish();
#line 159 "near_verdicts_g1.vmeg.c"

#line 160 "near_verdicts_g1.vmeg.c"
      {
        VmegTransferWords vmeCopyVar = vmeVarPlaneOut;
        vmeCopyVar.memory = nearPlane0;
        vmeTransferStoreWords(&vmeCopyVar, VME_BASE_BUFF3_WOFF + 0x37);
        meCoreDMACPrimWaitTransferFinish();
      }
#line 160 "near_verdicts_g1.vmeg.c"

#line 161 "near_verdicts_g1.vmeg.c"
#line 161 "near_verdicts_g1.vmeg.c"
    }
#line 162 "near_verdicts_g1.vmeg.c"
    {
      vmeLibStart();
      vmeLibLoadCustomContext((void*)nearRowContext[1]);
      vmeLibFinish();
#line 163 "near_verdicts_g1.vmeg.c"

#line 164 "near_verdicts_g1.vmeg.c"
      {
        VmegTransferWords vmeCopyVar = vmeVarPlaneOut;
        vmeCopyVar.memory = nearPlane1;
        vmeTransferStoreWords(&vmeCopyVar, VME_BASE_BUFF3_WOFF + 0x37);
        meCoreDMACPrimWaitTransferFinish();
      }
#line 164 "near_verdicts_g1.vmeg.c"

#line 165 "near_verdicts_g1.vmeg.c"
#line 165 "near_verdicts_g1.vmeg.c"
    }
#line 166 "near_verdicts_g1.vmeg.c"
    {
      vmeLibStart();
      vmeLibLoadCustomContext((void*)nearRowContext[2]);
      vmeLibFinish();
#line 167 "near_verdicts_g1.vmeg.c"

#line 168 "near_verdicts_g1.vmeg.c"
      {
        VmegTransferWords vmeCopyVar = vmeVarPlaneOut;
        vmeCopyVar.memory = nearPlane2;
        vmeTransferStoreWords(&vmeCopyVar, VME_BASE_BUFF3_WOFF + 0x37);
        meCoreDMACPrimWaitTransferFinish();
      }
#line 168 "near_verdicts_g1.vmeg.c"

#line 169 "near_verdicts_g1.vmeg.c"
#line 169 "near_verdicts_g1.vmeg.c"
    }
#line 170 "near_verdicts_g1.vmeg.c"
    {
      vmeLibStart();
      vmeLibLoadCustomContext((void*)nearRowContext[3]);
      vmeLibFinish();
#line 171 "near_verdicts_g1.vmeg.c"

#line 172 "near_verdicts_g1.vmeg.c"
      {
        VmegTransferWords vmeCopyVar = vmeVarPlaneOut;
        vmeCopyVar.memory = nearPlane3;
        vmeTransferStoreWords(&vmeCopyVar, VME_BASE_BUFF3_WOFF + 0x37);
        meCoreDMACPrimWaitTransferFinish();
      }
#line 172 "near_verdicts_g1.vmeg.c"

#line 173 "near_verdicts_g1.vmeg.c"
#line 173 "near_verdicts_g1.vmeg.c"
    }
#line 174 "near_verdicts_g1.vmeg.c"
    {
      vmeLibStart();
      vmeLibLoadCustomContext((void*)nearRowContext[4]);
      vmeLibFinish();
#line 175 "near_verdicts_g1.vmeg.c"

#line 176 "near_verdicts_g1.vmeg.c"
      {
        VmegTransferWords vmeCopyVar = vmeVarPlaneOut;
        vmeCopyVar.memory = nearPlane4;
        vmeTransferStoreWords(&vmeCopyVar, VME_BASE_BUFF3_WOFF + 0x37);
        meCoreDMACPrimWaitTransferFinish();
      }
#line 176 "near_verdicts_g1.vmeg.c"

#line 177 "near_verdicts_g1.vmeg.c"
      vmeLibDisable();
#line 177 "near_verdicts_g1.vmeg.c"
    }
#line 178 "near_verdicts_g1.vmeg.c"

}
