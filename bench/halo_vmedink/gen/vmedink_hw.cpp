#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
#include <me-core-mapper/me-core.h>
#include <me-core-mapper/vme-fu-opcodes.h>
#include <vme-debug.h>
typedef struct { volatile u32* memory; u32 count; u32 gap; u32 step; u32 groupSize; u32 groupOuter; u32 groupInner; } VmegTransferWords;

PSP_MODULE_INFO("vmeh", 0, 1, 1);
PSP_HEAP_SIZE_KB(-1024);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_VFPU | PSP_THREAD_ATTR_USER);

ME_LIB_SETUP_SIMPLE_SUSPEND_HANDLER();
VME_DEBUG_SET_BUFFER_WORD_COUNT(512);

meLibSetSharedUncached32(4);
#define meFinish (meLibSharedMemory[0])
#define meCommand (meLibSharedMemory[1])
#define meStart (meLibSharedMemory[2])
#define meCount (meLibSharedMemory[3])

#define VECTOR_COUNT 16
#define VECTOR_BATCH_WORD_COUNT (VECTOR_COUNT * 4)
#define VECTOR_MAC_WORD_COUNT 16
#define NEAR_ROWS 5
#define NEAR_SLOTS 1
#define NEAR_VERTICES 96
#define NEAR_K1 8
#define NEAR_K2 7
#define NEAR_RING (4 * NEAR_SLOTS)
#define NEAR_LEAD1 NEAR_RING
#define NEAR_STREAM1 (NEAR_LEAD1 + NEAR_VERTICES * NEAR_RING)
#define NEAR_LOST 12
#define NEAR_DOTS 48

volatile u32 __attribute__((aligned(64))) sharedMat[16] = {
  
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

volatile u32 __attribute__((aligned(64))) cancelMask[4] = {
  0, 0, 0xffffffff, 0
};

volatile u32 __attribute__((aligned(64))) sharedVec[VECTOR_BATCH_WORD_COUNT] = {

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

volatile u32 __attribute__((aligned(64))) sharedRes[VECTOR_BATCH_WORD_COUNT] = {0};

volatile u32 __attribute__((aligned(64))) nearRow[NEAR_ROWS * NEAR_RING] = {
  0xffffc8be, 0xffffc4d5, 0x00006742, 0x0009bde5,
  0xffffb441, 0xffffdb6a, 0x000064b3, 0x000c60db,
  0xffffe03d, 0xffffb177, 0x00006433, 0x0006934d,
  0xffffb166, 0xffffacd2, 0x0000402e, 0x000a574a,
  0xffffe897, 0xffffe5f2, 0x00007e73, 0x0007a4d3
};

volatile u32 __attribute__((aligned(64))) nearMask[8] = { 0, 0, 0xffffffff, 0, 0, 0, 0, 0 };

volatile u32 __attribute__((aligned(64))) nearZero[32];

volatile u32 __attribute__((aligned(64))) nearVertex[4 * NEAR_VERTICES] = {
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

volatile u32 __attribute__((aligned(64))) nearPlane0[NEAR_VERTICES];

volatile u32 __attribute__((aligned(64))) nearPlane1[NEAR_VERTICES];

volatile u32 __attribute__((aligned(64))) nearPlane2[NEAR_VERTICES];

volatile u32 __attribute__((aligned(64))) nearPlane3[NEAR_VERTICES];

volatile u32 __attribute__((aligned(64))) nearPlane4[NEAR_VERTICES];

volatile u32 __attribute__((aligned(64))) nearRowContext[NEAR_ROWS][VME_CONTEXT_WORD_COUNT];

VmegTransferWords vmeVarPlaneOut = { nearPlane0, 95, 3, 4, 0, 0, 0 };

void mulMat() {
  volatile u32 VME_BASE_ADDR = VME_DATAPATH_BASE;
  vme_pe0(agu_top(MODE), VME_DEF_MODE);
  vme_pe0(agu_top(COUNT), VME_DEF_STEP, 0xf);
  vme_pe0(agu_top(INNER_0), 0xf << 16, 1);
  vme_pe0(agu_top(FORMAT_0), VME_RING_TOKEN);
  vme_pe0(agu_base(MODE), VME_DEF_MODE);
  vme_pe0(agu_base(COUNT), VME_DEF_STEP, 0x11b);
  vme_pe0(agu_write(MODE), VME_DEF_MODE);
  vme_pe0(agu_write(COUNT), VME_DEF_STEP, 0x11b);
  vme_pe0(agu_write(FORMAT_0), 5);
  vme_pe0(agu_write(FORMAT_1), VME_END_TOKEN);
  vme_pe0(vme_fu(PRIMARY), vme_mux(TOP_0, TOP_1), VME_FU_OPCODE_MAC_INNER_PRODUCT_BIAS);
  vme_pe1(agu_top(MODE), VME_DEF_MODE, 0xfff0);
  vme_pe1(agu_top(COUNT), VME_DEF_STEP, 0x10f);
  vme_pe1(vme_fu(PRIMARY), vme_mux(TOP_2, STAGING_0), fu_code(OP_AND));
  vme_pe2(agu_top(MODE), VME_DEF_MODE);
  vme_pe2(agu_top(COUNT), VME_DEF_STEP, 3);
  vme_pe2(agu_top(INNER_0), 3 << 16, 1);
  vme_pe2(agu_top(FORMAT_0), VME_RING_TOKEN);
  vme_pe2(agu_write(MODE), VME_DEF_MODE);
  vme_pe2(agu_write(COUNT), VME_DEF_STEP, 0x11b);
  vme_pe2(agu_write(FORMAT_0), 1);
  vme_pe2(agu_write(FORMAT_1), VME_END_TOKEN);
  vme_pe2(vme_fu(PRIMARY), vme_mux(NONE, STAGING_1), VME_FU_OPCODE_MAC_ACC_SHIFT_BIAS);
  vme_pe3(agu_write(MODE), VME_DEF_MODE, vme_cyc(0x11));
  vme_pe3(agu_write(COUNT), VME_DEF_STEP, 0x10f);
  vme_pe3(vme_fu(PRIMARY), vme_mux(STAGING_2, BASE_2), fu_code(OP_SUB_INV_OFA));
  vme_pe3(fu_reg(PRIMARY, A), 0);
  vme_pe3(vme_fu(SECONDARY), vme_mux(BASE_0, STAGING_3), fu_code(OP_SUB_INV_OFA));
  vme_pe3(fu_reg(SECONDARY, A), 0);
  vme_icn(AGU_TOP, 0x4210);
  vme_icn(AGU_BASE, 0x4040);
  vme_icn(AGU_WRITE, 0x3240);
  vme_icn(SWEN, fu_on(0b0001));
}

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

static inline void vmehRun1MatrixTransferLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 15);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void vmehRun1MaskTransferLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 3);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void vmehRun1VectorTransferLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 63, 0);
  vme_dma(ITERATION, STEP, 4);
  vme_dma(GROUP, SIZE, 3);
  vme_dma(GROUP, STEPS, 16, 1);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x50);
}

static inline void vmehRun12ResultTransferStore(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 63, 3);
  vme_dma(ITERATION, STEP, 4);
  vme_dma(GROUP, SIZE, 0);
  vme_dma(GROUP, STEPS, 0, 0);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x58);
}


void vmehRun1(void) {
  {
    vmeLibEnable();
    vmeLibSetFlow(VME_DEFAULT);
    mulMat();



    vmehRun1MatrixTransferLoad(sharedMat, VME_TOP_BUFF0_WOFF);
    meCoreDMACPrimWaitTransferFinish();

    vmehRun1MaskTransferLoad(cancelMask, VME_TOP_BUFF2_WOFF);
    meCoreDMACPrimWaitTransferFinish();

    vmehRun1VectorTransferLoad(sharedVec, VME_TOP_BUFF1_WOFF);
    vmehRun1VectorTransferLoad(sharedVec, VME_TOP_BUFF1_WOFF + 4);
    meCoreDMACPrimWaitTransferFinish();
    vmehRun1VectorTransferLoad(sharedVec, VME_TOP_BUFF1_WOFF + 8);
    vmehRun1VectorTransferLoad(sharedVec, VME_TOP_BUFF1_WOFF + 0xc);
    meCoreDMACPrimWaitTransferFinish();

  }
  {
    vmeLibTrigger();
    meCoreDMACPrimWaitVMEFinish();

    vmehRun12ResultTransferStore(sharedRes, VME_BASE_BUFF3_WOFF + 0x13);
    meCoreDMACPrimWaitTransferFinish();

    vmeDebugFillWithCount(VME_BASE_BUFFERS + meStart * 4, meCount);
    vmeLibDisable();
  }
}
static inline void vmehRun26RowsInLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 19);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void vmehRun26MaskInLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 7);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void vmehRun26ZeroIn1Load(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 3);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void vmehRun26VertexInLoad(volatile u32* memory, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, 383);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x40);
}

static inline void vmeTransferStoreWords(const VmegTransferWords* transfer, u32 spadOffset) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)transfer->memory));
  vme_dma(ITERATION, DIMS, transfer->count, transfer->gap);
  vme_dma(ITERATION, STEP, transfer->step);
  vme_dma(GROUP, SIZE, transfer->groupSize);
  vme_dma(GROUP, STEPS, transfer->groupOuter, transfer->groupInner);
  vme_dma(SPAD, OFFSET, spadOffset);
  vme_dma(CTRL, VALUE, 0x58);
}


void vmehRun2(void) {
  {
    nearDotsBuild((void*)nearRowContext[0], 0);


  }
  {
    nearDotsBuild((void*)nearRowContext[1], 1);


  }
  {
    nearDotsBuild((void*)nearRowContext[2], 2);


  }
  {
    nearDotsBuild((void*)nearRowContext[3], 3);


  }
  {
    nearDotsBuild((void*)nearRowContext[4], 4);


  }
  {
    vmeLibEnable();
    vmeLibStart();
    vmeLibLoadCustomContext((void*)nearRowContext[0]);
    vmeLibFinish();





    vmehRun26RowsInLoad(nearRow, VME_TOP_BUFF0_WOFF);
    meCoreDMACPrimWaitTransferFinish();

    vmehRun26MaskInLoad(nearMask, VME_TOP_BUFF2_WOFF);
    meCoreDMACPrimWaitTransferFinish();

    vmehRun26ZeroIn1Load(nearZero, VME_TOP_BUFF1_WOFF);
    meCoreDMACPrimWaitTransferFinish();

    vmehRun26VertexInLoad(nearVertex, VME_TOP_BUFF1_WOFF + 4);
    meCoreDMACPrimWaitTransferFinish();

  }
  {
    vmeLibStart();
    vmeLibLoadCustomContext((void*)nearRowContext[0]);
    vmeLibFinish();

    {
      VmegTransferWords vmeCopyVar = vmeVarPlaneOut;
      vmeCopyVar.memory = nearPlane0;
      vmeTransferStoreWords(&vmeCopyVar, VME_BASE_BUFF3_WOFF + 0x37);
      meCoreDMACPrimWaitTransferFinish();
    }

  }
  {
    vmeLibStart();
    vmeLibLoadCustomContext((void*)nearRowContext[1]);
    vmeLibFinish();

    {
      VmegTransferWords vmeCopyVar = vmeVarPlaneOut;
      vmeCopyVar.memory = nearPlane1;
      vmeTransferStoreWords(&vmeCopyVar, VME_BASE_BUFF3_WOFF + 0x37);
      meCoreDMACPrimWaitTransferFinish();
    }

  }
  {
    vmeLibStart();
    vmeLibLoadCustomContext((void*)nearRowContext[2]);
    vmeLibFinish();

    {
      VmegTransferWords vmeCopyVar = vmeVarPlaneOut;
      vmeCopyVar.memory = nearPlane2;
      vmeTransferStoreWords(&vmeCopyVar, VME_BASE_BUFF3_WOFF + 0x37);
      meCoreDMACPrimWaitTransferFinish();
    }

  }
  {
    vmeLibStart();
    vmeLibLoadCustomContext((void*)nearRowContext[3]);
    vmeLibFinish();

    {
      VmegTransferWords vmeCopyVar = vmeVarPlaneOut;
      vmeCopyVar.memory = nearPlane3;
      vmeTransferStoreWords(&vmeCopyVar, VME_BASE_BUFF3_WOFF + 0x37);
      meCoreDMACPrimWaitTransferFinish();
    }

  }
  {
    vmeLibStart();
    vmeLibLoadCustomContext((void*)nearRowContext[4]);
    vmeLibFinish();

    {
      VmegTransferWords vmeCopyVar = vmeVarPlaneOut;
      vmeCopyVar.memory = nearPlane4;
      vmeTransferStoreWords(&vmeCopyVar, VME_BASE_BUFF3_WOFF + 0x37);
      meCoreDMACPrimWaitTransferFinish();
    }

    vmeDebugFillWithCount(VME_BASE_BUFFERS + meStart * 4, meCount);
    vmeLibDisable();
  }
}

void meLibOnProcess(void) {
  meCoreDcacheWritebackInvalidateAll();
  meLibExceptionHandlerInit(0);
  vmeLibEnable();
  vmeLibWipe();
  vmeLibDisable();

  while (1) {
    while (!meCommand) {
    }

    const int command = meCommand;
    meCommand = 0;

    vmeLibEnable();
    vmeLibWipe();
    vmeLibDisable();

    switch (command) {
      case 1:
        vmehRun1();
        break;
      case 2:
        vmehRun2();
        break;
    }

    meFinish = 1;
  }
}

static const char* const vmehNames[] = {"mat4x4-batch", "tile"};

#define vmehCount ((int)(sizeof(vmehNames) / sizeof(vmehNames[0])))

static int vmehWords = 512;

static void vmehPlay(const int index) {
  pspDebugScreenClear();
  pspDebugScreenSetXY(0, 0);
  pspDebugScreenPrintf("%03d - %s", index + 1, vmehNames[index]);

  meFinish = 0;
  meCommand = index + 1;
  while (!meFinish) {
    sceKernelDelayThread(100);
  }

  vmeDebugTouch();
  FILE* const log = fopen("./log.txt", "a");
  if (log) {
    fprintf(log, "Kernel: %03d - %s\n\n", index + 1, vmehNames[index]);
    fclose(log);
  }
  vmeDebugDumpWindowCount(vmehWords);

  sceKernelDelayThread(500000);
}

static void vmehReadConfig(int* const start, int* const count) {
  FILE* const config = fopen("./config.txt", "r");
  if (!config) {
    return;
  }

  char line[64];
  while (fgets(line, sizeof(line), config)) {
    int value;
    if (sscanf(line, "start: %d", &value) == 1) {
      *start = value;
    } else if (sscanf(line, "count: %d", &value) == 1) {
      *count = value;
    }
  }
  fclose(config);
}

int main() {
  scePowerSetClockFrequency(333, 333, 166);
  pspDebugScreenInit();

  int start = 0;
  vmehReadConfig(&start, &vmehWords);
  vmeDebugSetupBuffersCount(vmehWords);

  meLibDefaultInit();

  meStart = start;
  meCount = vmehWords;

  int index = 0;
  vmehPlay(index);

  SceCtrlData pad;
  while (1) {
    sceCtrlPeekBufferPositive(&pad, 1);
    if ((pad.Buttons & PSP_CTRL_LEFT) && index > 0) {
      vmehPlay(--index);
    } else if ((pad.Buttons & PSP_CTRL_RIGHT) && index < vmehCount - 1) {
      vmehPlay(++index);
    }
    sceKernelDelayThread(10000);
  }
}
