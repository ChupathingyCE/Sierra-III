cmake_minimum_required(VERSION 3.10)
set(CMAKE_TOOLCHAIN_FILE "$ENV{PSPDEV}/psp/share/pspdev.cmake" CACHE PATH "toolchain file")
project(halo_dmacbench C CXX)
add_executable(halo_dmacbench main.cpp)
# no library calls in code the ME runs: no loops turned into memcpy or memset
target_compile_options(halo_dmacbench PRIVATE -O2 -G0 -Wall -fno-pic -fno-builtin -fno-tree-loop-distribute-patterns)
target_compile_options(halo_dmacbench PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-std=c++11>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
)
# me-core: the ME's start and the kernel calls, as halo_bench12 links it. psppower
# only because me-core's own objects may name it: the bench makes no power call
target_link_libraries(halo_dmacbench me-core psppower pspctrl pspdebug pspdisplay pspge m)
create_pbp_file(TARGET halo_dmacbench TITLE "Halo PSP dmacbench - DMACplus start cost, chains, bursts" BUILD_PRX)
