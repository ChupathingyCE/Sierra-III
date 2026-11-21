/* halo_vmedink: is the near-vme kernel's wrong answer on the PSP-1000 vmee's
 * fault or our setup's? (the lab's nearvme1 read, 2026-11-21: the kernel was
 * exact word for word in Th6uD1nk's emulator vmee, but on the PSP-1000 its
 * words were wrong; Sierra-III docs/box_near_vme.md section 9.)
 *
 * One program, the shape Dink's toolkit ships: gen/vmedink_hw.cpp is the
 * program his vmeh writes for src/mat4x4.vmeg.cpp and src/near_tile.vmeg.c,
 * compiled by his vmegc with --compat fat, included here unchanged except that
 * its main and its ME loop are renamed out (README.md says how it was made).
 * His runner's steps are kept as he has them (Sheepdog01, 2026-11-21 08:09:
 * in VME bench code, follow his toolkit exactly): the clock call
 * scePowerSetClockFrequency(333, 333, 166), meLibDefaultInit, and before every
 * kernel his ME loop's wipe (vmeLibEnable, vmeLibWipe, vmeLibDisable).
 *
 * The cells, in this order (safest first: each writes a "calling" line before
 * it starts, so a hang leaves the most behind it):
 *   1  his batched mat4x4 sample (vmehRun1) as vmeh generates it. Passes when
 *      all 64 results, in BASE 3 words 19 + 4j and in his stored sharedRes,
 *      equal M v worked out here on the main CPU. halo_vmekern ran the same
 *      kernel on this runner on 2026-11-10 (16 of 16 vectors right on both
 *      PSPs): this is the same-run control that his toolkit works today.
 *   2a our near-vme kernel (one-slot form, near_verdicts_g1) on one tile, as
 *      vmeh generates it (vmehRun2): his context loads (the ME firmware's
 *      preload), his waits.
 *   2b the same tile through the box's own sequence (box_near_vme.c's
 *      tile_program and op_issue for one slot, builder/nearvme-me 1d5b9434):
 *      the hand-written 256-word context load (0x440ff014/18/10/08), the
 *      trigger as one store, polled bounded waits, the DMAC's fast mode on as
 *      the box ran it.
 *   2c 2b with the fast mode off ("nearvmerest").
 *   2d 2c with each context loaded his way (his vmeLibLoadCustomContext on
 *      the 112-word images) in place of the hand-written load.
 *   1  the mat4x4 cell again: the VME still right after our cells.
 *   Each tile cell passes when all 480 plane words equal vmee's for the same
 *   inputs (expected.h; vmee matched the lab's C reference on all 480).
 *   So: 2a right and 2b wrong is our setup; 2a wrong with cell 1 right is
 *   vmee's model of this kernel; 2c and 2d say which part of the setup.
 *
 * Waits: the main CPU waits 2 s at most for each cell's answer, then stops
 * starting cells. Our ME code's waits are bounded in the ME's cycles, as the
 * box's are. Dink's generated code waits through the ME firmware's own
 * functions, which have no bound on the ME: only the main CPU's bound covers
 * those. Every address the VME's DMAC is given is a 0x4xxxxxxx user form
 * (useg_mem), never a kernel-segment one.
 *
 * Output: ms0:/halo/log/vmedink.txt, a line at a time (opened and closed per
 * line, so a hang keeps what came before), with each cell's buffers in
 * vmeh's dump format. When it's done it waits for the run's wall rather than
 * quitting (the rig's rule); by hand, X exits.
 *
 * mcidclan's me-core-mapper (MIT, (c) m-c/d) and Th6uD1nk's vme2-next-level
 * (MIT, (c) m-c/d, Th6uD1nk): see README.md and the LICENSE.md files beside
 * them. This file is Sierra-III's. */
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
#include <pspiofilemgr.h>
#include <me-core-mapper/me-core.h>
#include <me-core-mapper/vme-fu-opcodes.h>
#include <vme-debug.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* the headers above are already in, so these rename only the generated
 * program's own two definitions */
#define main vmedinkGeneratedMain
#define meLibOnProcess vmedinkGeneratedOnProcess
#include "gen/vmedink_hw.cpp"
#undef meLibOnProcess
#undef main

#include "expected.h"

/* ---------- shared with the ME: our cells' counters */

meLibMakeSharedUncachedMem(boxShared, 16, u32);
enum {
  SH_WAITS, SH_NEVER_SEEN, SH_BOUNDED, SH_BOUNDED_OP, SH_OPS, SH_CYCLES,
  SH_STATUS, SH_CONFIG, SH_COUNT
};

/* ---------- the box's tile on the ME (box_near_vme.c, one slot). Nothing
 * here may reach a system call or the VFPU: plain stores, the VME's
 * registers, cache operations and the ME firmware's own functions */

#define TILE NEAR_VERTICES
#define CONTEXT_LOAD_WORDS 256
/* 6 ms of the ME's clock: far over any pass or transfer here */
#define WAIT_BOUND 2000000u
/* where one row's plane word sits on each group's last word, as the box's
 * K1_PLANE_AT and the generated store have it */
#define PLANE_AT (NEAR_DOTS + NEAR_LEAD1 + 3)

static u32 boxImage[NEAR_ROWS][CONTEXT_LOAD_WORDS] __attribute__((aligned(64)));
volatile u32 boxPlane[NEAR_ROWS][TILE] __attribute__((aligned(64)));

static inline u32 meCycles(void) {
  u32 count;
  asm volatile("mfc0 %0, $9" : "=r"(count));
  return count;
}

/* the box's box_writeback: what the DMAC reads leaves the ME's cache first */
static void boxWriteback(const void* start, u32 bytes) {
  u32 line = (u32)start & ~63u;
  const u32 end = (u32)start + bytes;
  for (; line < end; line += 64) {
    asm volatile("cache 0x1b, 0(%0)" : : "r"(line) : "memory");
  }
  asm volatile("sync" : : : "memory");
}

/* the box's vmeLibEnable/Disable are psp_vme.c's, register writes by
 * read-modify-write (0xbc100050 is shared: bit 0 is the ME's own bus clock) */
static void boxClocks(const int on) {
  if (on) {
    hw(0xbc100050) |= 0x20;
    asm volatile("sync");
    hw(0xbc100054) |= 1;
  } else {
    hw(0xbc100050) &= ~0x20u;
    asm volatile("sync");
    hw(0xbc100054) &= ~1u;
  }
  asm volatile("sync");
}

/* the box's op_done, run to its end: 0 done, -1 past its bound. The busy bit
 * may come and go before the first look, so a wait that never saw it ends
 * only once the step's least time has passed */
static int boxWait(const u32 mask, const u32 floor) {
  const u32 start = meCycles();
  int seen = 0;
  for (;;) {
    const u32 status = hw(0x440ff000);
    const u32 now = meCycles();
    if (status & mask) {
      seen = 1;
      if (now - start > WAIT_BOUND) {
        boxShared[SH_BOUNDED]++;
        return -1;
      }
      continue;
    }
    if (!seen && now - start < floor) {
      continue;
    }
    break;
  }
  boxShared[SH_WAITS]++;
  if (!seen) {
    boxShared[SH_NEVER_SEEN]++;
  }
  /* the firmware's closing write, as psp_vme.c's vme_wait makes it */
  hw(0x440ff000) = (mask >> 8) | 0x80;
  asm volatile("sync" : : : "memory");
  return 0;
}

static int boxLoad(const volatile u32* memory, const u32 spad, const u32 dims) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, dims);
  vme_dma(SPAD, OFFSET, spad);
  vme_dma(CTRL, VALUE, 0x40);
  boxShared[SH_OPS]++;
  return boxWait(0x200, 2 * (dims + 1));
}

/* the box's OP_CONTEXT: his vmeLibSendCustomContext without its trigger, the
 * length (256 words) and the buffer word set first */
static int boxContextHand(const u32* image) {
  vmeLibSetFlow(VME_DEFAULT);
  asm volatile("sync" : : : "memory");
  vmeLibConfigTransfer(0x1c);
  hw(0x440ff014) = CONTEXT_LOAD_WORDS - 1;
  hw(0x440ff018) = 0;
  hw(0x440ff010) = useg_mem((u32)image);
  hw(0x440ff008) = 0x0c;
  asm volatile("sync" : : : "memory");
  boxShared[SH_OPS]++;
  return boxWait(0x200, 2 * VME_CONTEXT_WORD_COUNT);
}

/* cell 2d's load: his own (the generated program's vmeLibStart and
 * vmeLibLoadCustomContext), the firmware's wait in it */
static int boxContextHis(void* image) {
  _vmeLibStart();
  vmeLibLoadCustomContext(image);
  boxShared[SH_OPS]++;
  return 0;
}

/* the box's OP_RUN: psp_vme.c's trigger, one store */
static int boxRun(void) {
  hw(0x440ff008) = 0x18;
  asm volatile("sync" : : : "memory");
  boxShared[SH_OPS]++;
  return boxWait(0x800, 72 + 2 * NEAR_STREAM1);
}

/* the box's OP_STORE: a row's plane value on each group's last word */
static int boxStore(volatile u32* memory) {
  vme_dma(MEMORY, ADDR, useg_mem((u32)memory));
  vme_dma(ITERATION, DIMS, TILE - 1, 3);
  vme_dma(ITERATION, STEP, 4);
  vme_dma(GROUP, SIZE, 0);
  vme_dma(GROUP, STEPS, 0, 0);
  vme_dma(SPAD, OFFSET, VME_BASE_BUFF3_WOFF + PLANE_AT);
  vme_dma(CTRL, VALUE, 0x58);
  boxShared[SH_OPS]++;
  return boxWait(0x200, 2 * TILE);
}

/* one tile, the box's steps in the box's order: the rows, the mask, the
 * lead-in's zeros and the vertices, then a context, a pass and a store a row */
static void boxTile(const int fast, const int hisContext) {
  const u32 start = meCycles();
  int r, k, failed = 0;

  for (k = 0; k < SH_COUNT; k++) {
    boxShared[k] = 0;
  }
  boxClocks(1);
  /* the images as nvme_job_begin makes them: a whole load long, zero past
   * the context's 112 words, built on the ME and written back */
  for (r = 0; r < NEAR_ROWS; r++) {
    volatile u32* const image = boxImage[r];
    for (k = 0; k < CONTEXT_LOAD_WORDS; k++) {
      image[k] = 0;
    }
    nearDotsBuild(boxImage[r], r);
    if (hisContext) {
      nearDotsBuild((void*)nearRowContext[r], r);
    }
  }
  boxWriteback(boxImage, sizeof(boxImage));
  if (fast) {
    hw(0x440ff004) |= 0x300;
  }
  failed = boxLoad(nearRow, VME_TOP_BUFF0_WOFF, NEAR_ROWS * 4 - 1) < 0;
  if (!failed) failed = boxLoad(nearMask, VME_TOP_BUFF2_WOFF, 7) < 0;
  if (!failed) failed = boxLoad(nearZero, VME_TOP_BUFF1_WOFF, 3) < 0;
  if (!failed) failed = boxLoad(nearVertex, VME_TOP_BUFF1_WOFF + 4, 4 * TILE - 1) < 0;
  for (r = 0; r < NEAR_ROWS && !failed; r++) {
    failed = (hisContext ? boxContextHis((void*)nearRowContext[r]) : boxContextHand(boxImage[r])) < 0;
    if (!failed) failed = boxRun() < 0;
    if (!failed) failed = boxStore(boxPlane[r]) < 0;
  }
  if (failed) {
    boxShared[SH_BOUNDED_OP] = boxShared[SH_OPS];
  }
  boxShared[SH_STATUS] = hw(0x440ff000);
  boxShared[SH_CONFIG] = hw(0x440ff004);
  if (fast) {
    hw(0x440ff004) &= ~0x300u;
  }
  vmeDebugFillWithCount(VME_BASE_BUFFERS + meStart * 4, meCount);
  boxClocks(0);
  boxShared[SH_CYCLES] = meCycles() - start;
}

/* the generated program's ME loop (vmeh's on_process_loop), with our cells
 * added as commands 3 to 5 */
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
      case 3:
        boxTile(1, 0);
        break;
      case 4:
        boxTile(0, 0);
        break;
      case 5:
        boxTile(0, 1);
        break;
    }

    meFinish = 1;
  }
}

/* ---------- the main CPU: output */

static const char* const logPath = "ms0:/halo/log/vmedink.txt";

static void out(const char* format, ...) {
  char line[400];
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(line, sizeof(line) - 2, format, arguments);
  va_end(arguments);
  pspDebugScreenPrintf("%s\n", line);
  const SceUID file = sceIoOpen(logPath, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
  if (file >= 0) {
    strcat(line, "\n");
    sceIoWrite(file, line, strlen(line));
    sceIoClose(file);
  }
}

/* the dump vmeh's runner writes (./log.txt, his format), appended to ours */
static void keepDump(const char* const name, const int command) {
  vmeDebugTouch();
  FILE* const log = fopen("./log.txt", "a");
  if (log) {
    fprintf(log, "Kernel: %03d - %s\n\n", command, name);
    fclose(log);
  }
  vmeDebugDumpWindowCount(vmehWords);
  FILE* const in = fopen("./log.txt", "r");
  FILE* const to = fopen(logPath, "a");
  char line[256];
  if (in && to) {
    while (fgets(line, sizeof(line), in)) {
      fputs(line, to);
    }
    fputs("\n", to);
  }
  if (in) {
    fclose(in);
  }
  if (to) {
    fclose(to);
  }
}

/* ---------- the main CPU: the ME's answer, bounded */

/* a cell takes well under a millisecond; 2 s means the ME never answered */
#define ANSWER_WAITS 20000 /* of 100 us */

static int meAlive = 1;

static int play(const int command, const char* const cell) {
  if (!meAlive) {
    out("cell %s: not run (the ME stopped answering earlier)", cell);
    return 0;
  }
  out("cell %s: calling (ME command %d)", cell, command);
  meFinish = 0;
  meCommand = command;
  for (int waited = 0; !meFinish; waited++) {
    if (waited >= ANSWER_WAITS) {
      out("cell %s: NO ANSWER from the ME in 2 s; no more cells run", cell);
      meAlive = 0;
      return 0;
    }
    sceKernelDelayThread(100);
  }
  return 1;
}

/* the main CPU reads what the DMAC wrote through the uncached alias: its own
 * cache never held these lines (meLibDefaultInit wrote everything back and
 * dropped it, and nothing here reads them cached) */
static inline volatile u32* uncached(const volatile void* p) {
  return (volatile u32*)(UNCACHED_USER_MASK | (u32)p);
}

/* a word the DMAC didn't write keeps this */
#define POISON 0x5a5a5a5au

static void poison(const volatile void* p, const int words) {
  volatile u32* const w = uncached(p);
  for (int k = 0; k < words; k++) {
    w[k] = POISON;
  }
}

static inline int signed24(const u32 x) {
  return (int)(x << 8) >> 8;
}

/* each base buffer's window against vmee's (BASE 0-3, vmehWords words each):
 * intermediates may differ on the PSP without the results doing so
 * (halo_vmekern: 11 of BASE 0's and 2 of BASE 2/3's in his mat4x4) */
static void compareWindow(const char* const cell, const u32* const expect) {
  for (int b = 0; b < 4; b++) {
    const volatile u32* const got = &vmeDebugBuffers[vmehWords * b];
    int differ = 0, shown = 0;
    char first[200] = "";
    for (int k = 0; k < vmehWords && k < EXPECT_WINDOW; k++) {
      if (got[k] != expect[EXPECT_WINDOW * b + k]) {
        differ++;
        if (shown < 4) {
          const int at = strlen(first);
          snprintf(first + at, sizeof(first) - at, "%s%d: %08lx not %08lx", shown ? ", " : "", k,
            (unsigned long)got[k], (unsigned long)expect[EXPECT_WINDOW * b + k]);
          shown++;
        }
      }
    }
    out("cell %s: BASE %d window: %d of %d words as vmee%s%s", cell, b, vmehWords - differ, vmehWords,
      differ ? "; first differing (word: PSP not vmee) " : "", first);
  }
}

/* ---------- the cells' checks */

static int cellMat(const char* const cell) {
  u32 mv[64];
  const volatile u32* const res = uncached(sharedRes);
  int resRight = 0, bufRight = 0, refRight = 0;

  for (int j = 0; j < 16; j++) {
    for (int r = 0; r < 4; r++) {
      int sum = 0;
      for (int c = 0; c < 4; c++) {
        sum += (int)sharedMat[4 * r + c] * (int)sharedVec[4 * j + c];
      }
      mv[4 * j + r] = (u32)sum;
    }
  }
  for (int k = 0; k < 64; k++) {
    refRight += mv[k] == expectMatRes[k];
  }
  out("cell %s: M v on the main CPU against vmee's sharedRes: %d of 64 the same", cell, refRight);
  int shown = 0;
  for (int k = 0; k < 64; k++) {
    const u32 inBuffer = vmeDebugBuffers[vmehWords * 3 + 19 + 4 * k];
    resRight += res[k] == mv[k];
    bufRight += inBuffer == mv[k];
    if ((res[k] != mv[k] || inBuffer != mv[k]) && shown < 6) {
      out("cell %s:   result %d (vector %d, row %d): sharedRes %08lx, BASE 3 word %d %08lx, M v %08lx", cell, k,
        k / 4, k % 4, (unsigned long)res[k], 19 + 4 * k, (unsigned long)inBuffer, (unsigned long)mv[k]);
      shown++;
    }
  }
  out("cell %s: results: BASE 3 words 19 + 4j %d of 64 equal M v; his sharedRes store %d of 64", cell, bufRight,
    resRight);
  for (int k = 0; k < 64; k += 8) {
    out("cell %s:   sharedRes[%2d..]: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx", cell, k,
      (unsigned long)res[k], (unsigned long)res[k + 1], (unsigned long)res[k + 2], (unsigned long)res[k + 3],
      (unsigned long)res[k + 4], (unsigned long)res[k + 5], (unsigned long)res[k + 6], (unsigned long)res[k + 7]);
  }
  compareWindow(cell, expectMatBase);
  const int pass = bufRight == 64 && resRight == 64;
  out("cell %s: %s", cell, pass ? "PASS (his kernel right on this PSP)" : "FAIL");
  return pass;
}

static int cellTile(const char* const cell, const volatile u32* const* const planes) {
  int same = 0, same24 = 0, offOne = 0, offMore = 0, unwritten = 0, shown = 0;

  for (int r = 0; r < NEAR_ROWS; r++) {
    const volatile u32* const p = uncached(planes[r]);
    int rowDiffer = 0;
    for (int j = 0; j < TILE; j++) {
      const u32 got = p[j], want = expectTilePlane[TILE * r + j];
      if (got == want) {
        same++;
        same24++;
        continue;
      }
      rowDiffer++;
      if (got == POISON) {
        unwritten++;
      } else if ((got & 0xffffff) == (want & 0xffffff)) {
        same24++;
      } else {
        const int d = signed24(got) - signed24(want);
        if (d == 1 || d == -1) {
          offOne++;
        } else {
          offMore++;
        }
      }
      if (shown < 8) {
        out("cell %s:   row %d vertex %2d: PSP %08lx, vmee %08lx", cell, r, j, (unsigned long)got,
          (unsigned long)want);
        shown++;
      }
    }
    out("cell %s: row %d: %d of %d plane words as vmee", cell, r, TILE - rowDiffer, TILE);
  }
  out("cell %s: plane words: %d of %d as vmee (low 24 bits: %d); differ by one %d, by more %d, unwritten %d", cell,
    same, NEAR_ROWS * TILE, same24, offOne, offMore, unwritten);
  if (same != NEAR_ROWS * TILE) {
    for (int r = 0; r < NEAR_ROWS; r++) {
      const volatile u32* const p = uncached(planes[r]);
      for (int j = 0; j < TILE; j += 8) {
        out("cell %s:   row %d [%2d..]: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx", cell, r, j,
          (unsigned long)p[j], (unsigned long)p[j + 1], (unsigned long)p[j + 2], (unsigned long)p[j + 3],
          (unsigned long)p[j + 4], (unsigned long)p[j + 5], (unsigned long)p[j + 6], (unsigned long)p[j + 7]);
      }
    }
  }
  compareWindow(cell, expectTileBase);
  const int pass = same == NEAR_ROWS * TILE;
  out("cell %s: %s", cell, pass ? "PASS (the tile's words are vmee's)" : "FAIL");
  return pass;
}

static void boxCounters(const char* const cell) {
  const volatile u32* const s = boxShared;
  out("cell %s: the box's steps on the ME: %lu ops, %lu waits (busy never seen %lu, past their bound %lu%s), "
    "%lu ME cycles; DMAC status %08lx, config %08lx at the end",
    cell, (unsigned long)s[SH_OPS], (unsigned long)s[SH_WAITS], (unsigned long)s[SH_NEVER_SEEN],
    (unsigned long)s[SH_BOUNDED], s[SH_BOUNDED] ? ", the tile stopped there" : "", (unsigned long)s[SH_CYCLES],
    (unsigned long)s[SH_STATUS], (unsigned long)s[SH_CONFIG]);
  if (s[SH_BOUNDED]) {
    out("cell %s: the step past its bound was op %lu (1 rows, 2 mask, 3 zeros, 4 vertices, then a context, a "
      "pass and a store a row)", cell, (unsigned long)s[SH_BOUNDED_OP]);
  }
}

static const volatile u32* const generatedPlanes[NEAR_ROWS] = { nearPlane0, nearPlane1, nearPlane2, nearPlane3,
  nearPlane4 };
static const volatile u32* const boxPlanes[NEAR_ROWS] = { boxPlane[0], boxPlane[1], boxPlane[2], boxPlane[3],
  boxPlane[4] };

static int runMat(const char* const cell) {
  poison(sharedRes, 64);
  if (!play(1, cell)) {
    return -1;
  }
  keepDump(vmehNames[0], 1);
  return cellMat(cell);
}

static int runTile(const int command, const char* const cell) {
  const volatile u32* const* planes = command == 2 ? generatedPlanes : boxPlanes;
  for (int r = 0; r < NEAR_ROWS; r++) {
    poison(planes[r], TILE);
  }
  if (!play(command, cell)) {
    return -1;
  }
  keepDump(command == 2 ? vmehNames[1] : cell, command);
  if (command != 2) {
    boxCounters(cell);
  }
  return cellTile(cell, planes);
}

int main() {
  scePowerSetClockFrequency(333, 333, 166);
  pspDebugScreenInit();
  sceIoMkdir("ms0:/halo", 0777);
  sceIoMkdir("ms0:/halo/log", 0777);
  /* me-lib writes its kernel module as ./kcall.prx and loads it, so the ME
   * starts in the EBOOT's folder: a .prx in ms0:/halo/log makes the results'
   * firmware check refuse the run's results */
  sceIoRemove("ms0:/halo/log/kcall.prx");
  sceIoRemove(logPath);

  out("vmedink: Dink's mat4x4 sample and the near-vme kernel's context load (halo_vmedink)");
  out("vmedink: clocks %d/%d MHz (CPU/bus), %d buffers words a window", scePowerGetCpuClockFrequency(),
    scePowerGetBusClockFrequency(), vmehWords);
  vmeDebugSetupBuffersCount(vmehWords);
  out("vmedink: calling meLibDefaultInit");
  const int started = meLibDefaultInit();
  /* vmeh's dump writes ./log.txt: kept out of ms0:/halo/log, where the game's
   * own log.txt lives */
  chdir("ms0:/halo");
  out("vmedink: meLibDefaultInit %d (3: the PSP-1000's image table, 2: the PSP-3000's)", started);
  if (started < 0) {
    out("vmedink: the ME didn't start; no cells run");
  } else {
    meStart = 0;
    meCount = vmehWords;
    const int c1 = runMat("1");
    const int c2a = runTile(2, "2a");
    const int c2b = runTile(3, "2b");
    const int c2c = runTile(4, "2c");
    const int c2d = runTile(5, "2d");
    const int c1again = runMat("1 again");
    out("vmedink: summary (1 pass, 0 fail, -1 not run or no answer): cell 1 %d, 2a %d, 2b %d, 2c %d, 2d %d, "
      "1 again %d", c1, c2a, c2b, c2c, c2d, c1again);
  }
  sceIoRemove("ms0:/halo/log.txt");
  out("vmedink: done; waiting for the run's wall (X exits)");

  SceCtrlData pad;
  while (1) {
    sceCtrlPeekBufferPositive(&pad, 1);
    if (pad.Buttons & PSP_CTRL_CROSS) {
      break;
    }
    sceKernelDelayThread(100000);
  }
  sceKernelExitGame();
  return 0;
}
