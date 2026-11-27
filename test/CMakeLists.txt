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

# ReadBuffer core0/core1 protocol: copy the sources next to nothing else so that
# their quoted includes ("PlayAudio.h", "ff.h", pico/...) resolve to test/stubs
find_package(Threads REQUIRED)
configure_file(${CMAKE_CURRENT_LIST_DIR}/../lib/PlayAudio/ReadBuffer.h ${CMAKE_CURRENT_BINARY_DIR}/rb/ReadBuffer.h COPYONLY)
configure_file(${CMAKE_CURRENT_LIST_DIR}/../lib/PlayAudio/ReadBuffer.cpp ${CMAKE_CURRENT_BINARY_DIR}/rb/ReadBuffer.cpp COPYONLY)
add_executable(test_read_buffer
    test_read_buffer.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/rb/ReadBuffer.cpp
)
target_include_directories(test_read_buffer PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/rb ${CMAKE_CURRENT_LIST_DIR}/stubs)
target_compile_options(test_read_buffer PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all)
target_link_options(test_read_buffer PRIVATE -fsanitize=address,undefined)
target_link_libraries(test_read_buffer PRIVATE Threads::Threads)

enable_testing()
add_test(NAME wav_parser COMMAND test_wav_parser)
add_test(NAME read_buffer COMMAND test_read_buffer)
