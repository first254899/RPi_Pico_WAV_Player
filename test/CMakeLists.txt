# Host-side unit tests (no Pico SDK needed)
#   cmake -S test -B build-test && cmake --build build-test && ctest --test-dir build-test --output-on-failure
cmake_minimum_required(VERSION 3.13)
project(RPi_Pico_WAV_Player_test CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test_wav_parser
    test_wav_parser.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../lib/PlayAudio/WavParser.cpp
)
target_include_directories(test_wav_parser PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../lib/PlayAudio)
target_compile_options(test_wav_parser PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all)
target_link_options(test_wav_parser PRIVATE -fsanitize=address,undefined)

enable_testing()
add_test(NAME wav_parser COMMAND test_wav_parser)
