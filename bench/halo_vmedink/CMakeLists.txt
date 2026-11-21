# halo_vmedink: Th6uD1nk's batched mat4x4 sample and the near-vme kernel's
# context load on the PSP, against his emulator vmee (the lab's nearvme1 read,
# 2026-11-21). halo_vmekern's build (the one that ran his mat4x4 on this
# runner, 2026-11-10), unchanged but for the names. README.md says what each
# file is and where it comes from: mcidclan's me-core-mapper (0197c99, MIT; its
# sources are the same as vme2-next-level's submodule), Dink's vme-debug.h and
# the inputs (vme2-next-level, MIT), the program vmeh and vmegc generated from
# them (gen/), vmee's answers (expected.h), and vmedink.cpp, ours.
#
# mcidclan's libraries are built here as his own CMakeLists makes them (the
# kernel module uncompressed, his default), our copy's headers first on every
# include path, since the runners' pspdev has the port's older me-core.
cmake_minimum_required(VERSION 3.10)
if(NOT DEFINED PROJECT_NAME)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{PSPDEV}/psp/share/pspdev.cmake" CACHE PATH "toolchain file")
  project(halo_vmedink C CXX ASM)
endif()

set(CORE ${CMAKE_CURRENT_LIST_DIR}/mcidclan/me-core-mapper)
set(GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GEN})

# ---- the kernel module (his kernel/CMakeLists.txt, uncompressed: USE_PRX_COMPRESSION off)
add_custom_command(
  OUTPUT ${GEN}/exports.c
  COMMAND psp-build-exports -b ${CORE}/kernel/exports.exp > ${GEN}/exports.c
  DEPENDS ${CORE}/kernel/exports.exp
  WORKING_DIRECTORY ${GEN})
add_executable(kcall ${CORE}/kernel/main.c ${GEN}/exports.c)
target_compile_options(kcall PRIVATE -Wno-narrowing -fsingle-precision-constant -Os -G0 -Wall)
target_link_options(kcall PRIVATE -nostartfiles -Wl,-q -Wl,-T$ENV{PSPDEV}/psp/sdk/lib/linkfile.prx
  -Wl,-zmax-page-size=128 -Wl,--strip-all)
target_link_libraries(kcall PRIVATE -lpspkernel)
add_custom_command(
  OUTPUT ${GEN}/kcall.prx
  COMMAND psp-prxgen $<TARGET_FILE:kcall> ${GEN}/kcall.prx
  COMMAND psp-fixup-imports ${GEN}/kcall.prx
  DEPENDS kcall)

# ---- the module embedded as embedded_kcall[] (his xxd -i and two seds)
add_custom_command(
  OUTPUT ${GEN}/embedded.c
  COMMAND ${CMAKE_COMMAND} -DIN=${GEN}/kcall.prx -DOUT=${GEN}/embedded.c -P ${CMAKE_CURRENT_LIST_DIR}/embed.cmake
  DEPENDS ${GEN}/kcall.prx ${CMAKE_CURRENT_LIST_DIR}/embed.cmake)

# ---- the main program's stubs for the module's exports
add_custom_command(
  OUTPUT ${GEN}/kcall.S
  COMMAND psp-build-exports -s ${CORE}/kernel/exports.exp > ${GEN}/kcall.S
  DEPENDS ${CORE}/kernel/exports.exp ${GEN}/kcall.prx
  WORKING_DIRECTORY ${GEN})

# ---- his libraries, as his CMakeLists.txt makes them
add_library(me-core STATIC ${CORE}/me-core-custom.c ${CORE}/me-core-mapping.c ${CORE}/context.S)
add_library(me-core-lib STATIC ${CORE}/me-lib.c ${GEN}/embedded.c ${GEN}/kcall.S)
add_library(vme-lib STATIC ${CORE}/vme-lib.c)
foreach(target me-core me-core-lib vme-lib)
  target_compile_options(${target} PRIVATE -Os -G0 -Wall -fno-pic)
  target_include_directories(${target} BEFORE PUBLIC ${CORE})
endforeach()

# ---- the program: vmedink.cpp includes gen/vmedink_hw.cpp and runs its cells
add_executable(halo_vmedink ${CMAKE_CURRENT_LIST_DIR}/vmedink.cpp)
target_include_directories(halo_vmedink BEFORE PRIVATE ${CORE} ${CMAKE_CURRENT_LIST_DIR}/mcidclan
  ${CMAKE_CURRENT_LIST_DIR}/dink ${CMAKE_CURRENT_LIST_DIR})
target_compile_options(halo_vmedink PRIVATE -Os -G0 -Wall -fno-pic)
target_compile_options(halo_vmedink PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-std=c++11>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
target_link_libraries(halo_vmedink me-core me-core-lib vme-lib psppower pspge pspdisplay pspctrl pspdebug)
create_pbp_file(TARGET halo_vmedink TITLE "Halo PSP Bench - VME mat4x4 and near-vme" BUILD_PRX)
