# Host build of the benchmark core: checks the port (bit-exact across port modes, vs ffmpeg),
# gives the expected PCM hashes for the firmware, and host timings (not representative of the Pico)
#   cmake -S bench/helix_mp3/host -B /tmp/hb && cmake --build /tmp/hb && /tmp/hb/helix_host_c64 bench/helix_mp3/clips/clip_128k.mp3
cmake_minimum_required(VERSION 3.13)
project(helix_bench_host C)
set(CMAKE_C_STANDARD 11)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
include(${CMAKE_CURRENT_LIST_DIR}/../helix.cmake)

foreach(mode C64 M0)
    string(TOLOWER ${mode} m)
    add_helix_library(helix_${m} HELIX_PORT_${mode})
    add_executable(helix_host_${m} host_bench.c ${CMAKE_CURRENT_LIST_DIR}/../core/bench_decode.c)
    target_include_directories(helix_host_${m} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../core)
    target_compile_options(helix_host_${m} PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(helix_host_${m} PRIVATE helix_${m})
endforeach()

# MULSHIFT32/MADD64 of the M0 port against the 64-bit reference
add_executable(test_port test_port.c)
target_include_directories(test_port PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../port)
target_compile_options(test_port PRIVATE -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all)
target_link_options(test_port PRIVATE -fsanitize=undefined)
