cmake_minimum_required(VERSION 3.10)
set(CMAKE_TOOLCHAIN_FILE "$ENV{PSPDEV}/psp/share/pspdev.cmake" CACHE PATH "toolchain file")
project(halo_bench12 C CXX)
add_executable(halo_bench12 main.cpp)
# no library calls in code the ME runs: no loops turned into memcpy
target_compile_options(halo_bench12 PRIVATE -O2 -G0 -Wall -fno-pic -fno-builtin -fno-tree-loop-distribute-patterns)
target_compile_options(halo_bench12 PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-std=c++11>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
)
target_link_libraries(halo_bench12 me-core pspgu pspge pspdmac psppower pspctrl pspdebug pspdisplay m)
create_pbp_file(TARGET halo_bench12 TITLE "Halo PSP Bench 12 - the bus under DMA" BUILD_PRX)
