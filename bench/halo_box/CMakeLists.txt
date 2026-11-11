cmake_minimum_required(VERSION 3.10)
set(CMAKE_TOOLCHAIN_FILE "$ENV{PSPDEV}/psp/share/pspdev.cmake" CACHE PATH "toolchain file")
project(halo_box C)
# every adapter, and every box unit of the port (psp/src/box_*.c): a run names its box with the keep word box<name>
file(GLOB ADAPTERS ${CMAKE_CURRENT_SOURCE_DIR}/adapters/*.c)
file(GLOB BOX_UNITS ${CMAKE_CURRENT_SOURCE_DIR}/../../psp/src/box_*.c)
add_executable(halo_box main.c harness.c selftest_capture.c adapters.c ${ADAPTERS} ${BOX_UNITS})
# the flags psp_build.py gives psp/src's plain C units, so a box is priced as the port compiles it
target_compile_options(halo_box PRIVATE -O2 -G0 -Wall -ffunction-sections -fdata-sections -fno-math-errno)
target_include_directories(halo_box PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../psp/src)
target_link_libraries(halo_box pspgu pspge psppower pspdebug pspdisplay m)
create_pbp_file(TARGET halo_box TITLE "Halo PSP - the box harness" BUILD_PRX)
