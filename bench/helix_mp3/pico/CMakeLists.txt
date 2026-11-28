# Standalone Helix MP3 decode benchmark firmware (does not touch the player build)
#   rp2040: cmake -S bench/helix_mp3/pico -B build-bench-rp2040 -G Ninja
#   rp2350: cmake -S bench/helix_mp3/pico -B build-bench-rp2350 -G Ninja -DPICO_PLATFORM=rp2350 -DPICO_BOARD=pico2
cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(helix_mp3_bench C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

include(${CMAKE_CURRENT_LIST_DIR}/../helix.cmake)

set(CLIP_DIR ${CMAKE_CURRENT_LIST_DIR}/../clips)
configure_file(${CMAKE_CURRENT_LIST_DIR}/clips.S.in ${CMAKE_CURRENT_BINARY_DIR}/clips.S @ONLY)
set_property(SOURCE ${CMAKE_CURRENT_BINARY_DIR}/clips.S APPEND PROPERTY OBJECT_DEPENDS
    ${CLIP_DIR}/clip_128k.mp3 ${CLIP_DIR}/clip_320k.mp3)

if (PICO_PLATFORM MATCHES "rp2350")
    set(NATIVE_MODE HELIX_PORT_DSP)  # Cortex-M33: SMMUL/SMLAL
    set(CHIP rp2350)
else()
    set(NATIVE_MODE HELIX_PORT_C64)  # Cortex-M0+: generic C, 64-bit multiply via __aeabi_lmul (fastest in rp2040js)
    set(CHIP rp2040)
endif()

# add_bench(<target> <port mode> <binary type: default|copy_to_ram> <emu: 0|1>)
function(add_bench target mode bintype emu)
    add_helix_library(${target}_helix ${mode})
    add_executable(${target}
        main.c
        ${CMAKE_CURRENT_LIST_DIR}/../core/bench_decode.c
        ${CMAKE_CURRENT_BINARY_DIR}/clips.S
    )
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../core)
    target_compile_options(${target} PRIVATE -Wall -Wextra)
    target_compile_definitions(${target} PRIVATE
        BENCH_NAME="${target}" BENCH_PORT_MODE="${mode}" BENCH_BINTYPE="${bintype}" BENCH_EMU=${emu})
    target_link_libraries(${target} PRIVATE ${target}_helix pico_stdlib hardware_clocks hardware_vreg)
    if (emu)
        pico_enable_stdio_usb(${target} 0)
    else()
        pico_enable_stdio_usb(${target} 1)
    endif()
    pico_enable_stdio_uart(${target} 1)
    if (NOT bintype STREQUAL "default")
        pico_set_binary_type(${target} ${bintype})
    endif()
    pico_add_extra_outputs(${target})
endfunction()

add_bench(helix_bench_${CHIP}       ${NATIVE_MODE} default 0)
add_bench(helix_bench_${CHIP}_ram   ${NATIVE_MODE} copy_to_ram 0)
if (CHIP STREQUAL "rp2040")
    # 16x16 partial products in C instead of __aeabi_lmul, for comparison (slower in rp2040js)
    add_bench(helix_bench_${CHIP}_m0 HELIX_PORT_M0 default 0)
    # for rp2040js (UART only, default clock, single pass)
    add_bench(helix_bench_${CHIP}_emu    ${NATIVE_MODE} default 1)
    add_bench(helix_bench_${CHIP}_m0_emu HELIX_PORT_M0 default 1)
endif()
