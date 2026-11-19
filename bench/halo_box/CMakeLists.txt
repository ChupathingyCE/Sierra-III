cmake_minimum_required(VERSION 3.10)
set(CMAKE_TOOLCHAIN_FILE "$ENV{PSPDEV}/psp/share/pspdev.cmake" CACHE PATH "toolchain file")
project(halo_box C ASM)

set(GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GEN})

# ---- the kernel side (kernel/main.c: the CPU's counters behind one system
# call), built as a module and carried inside the EBOOT, which writes it to
# the stick and loads it when a run asks for the counters
add_custom_command(
  OUTPUT ${GEN}/exports.c
  COMMAND psp-build-exports -b ${CMAKE_CURRENT_SOURCE_DIR}/kernel/exports.exp > ${GEN}/exports.c
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/kernel/exports.exp
  WORKING_DIRECTORY ${GEN})
add_executable(halo_boxk kernel/main.c ${GEN}/exports.c)
target_compile_options(halo_boxk PRIVATE -Os -G0 -Wall)
target_link_options(halo_boxk PRIVATE -nostartfiles -Wl,-q -Wl,-T$ENV{PSPDEV}/psp/sdk/lib/linkfile.prx
  -Wl,-zmax-page-size=128 -Wl,--strip-all)
target_link_libraries(halo_boxk PRIVATE -lpspkernel)
add_custom_command(
  OUTPUT ${GEN}/halo_boxk.prx
  # (no psp-fixup-imports: the module imports nothing, so it has no stubs to fix up)
  COMMAND psp-prxgen $<TARGET_FILE:halo_boxk> ${GEN}/halo_boxk.prx
  DEPENDS halo_boxk)
add_custom_command(
  OUTPUT ${GEN}/embedded.c
  COMMAND ${CMAKE_COMMAND} -DIN=${GEN}/halo_boxk.prx -DOUT=${GEN}/embedded.c -P ${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake
  DEPENDS ${GEN}/halo_boxk.prx ${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake)
add_custom_command(
  OUTPUT ${GEN}/halo_boxk.S
  COMMAND psp-build-exports -s ${CMAKE_CURRENT_SOURCE_DIR}/kernel/exports.exp > ${GEN}/halo_boxk.S
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/kernel/exports.exp ${GEN}/halo_boxk.prx
  WORKING_DIRECTORY ${GEN})

# ---- the bench: every adapter, and every box unit of the port
# (psp/src/box_*.c); a run names its box with the keep word box<name>
file(GLOB ADAPTERS ${CMAKE_CURRENT_SOURCE_DIR}/adapters/*.c)
file(GLOB BOX_UNITS ${CMAKE_CURRENT_SOURCE_DIR}/../../psp/src/box_*.c)
add_executable(halo_box main.c harness.c me.c selftest_capture.c adapters.c ${ADAPTERS} ${BOX_UNITS} ${GEN}/embedded.c ${GEN}/halo_boxk.S)
# me.c's ME side: no loop of its may become a library call (halo_bench12's flags)
set_source_files_properties(me.c PROPERTIES COMPILE_OPTIONS "-fno-builtin;-fno-tree-loop-distribute-patterns")
# the flags psp_build.py gives psp/src's plain C units, so a box is priced as the port compiles it
target_compile_options(halo_box PRIVATE -O2 -G0 -Wall -ffp-contract=off -ffunction-sections -fdata-sections -fno-math-errno)
target_include_directories(halo_box PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../psp/src)
target_link_libraries(halo_box me-core pspgu pspge psppower pspdebug pspdisplay m)
# every relocation kept, so tools/mexec.py can make the eDRAM copy's table
target_link_options(halo_box PRIVATE -Wl,-q)
# each unit's size into the build's log: one program holds every box, so a new box moves the others
add_custom_command(TARGET halo_box POST_BUILD COMMAND psp-size $<TARGET_OBJECTS:halo_box> COMMAND_EXPAND_LISTS)
# the eDRAM copy's table (README, "On the Media Engine"), written into the
# linked program before create_pbp_file's own steps (fix-up, prxgen) run after
# it; its log names what the copy holds and the ME check (VFPU, system calls)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_command(TARGET halo_box POST_BUILD
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/mexec.py --entry box_me_pass $<TARGET_FILE:halo_box>
          ${CMAKE_CURRENT_SOURCE_DIR}/me/copy.txt)
create_pbp_file(TARGET halo_box TITLE "Halo PSP - the box harness" BUILD_PRX)
