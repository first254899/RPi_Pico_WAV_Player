# Host-side unit tests (no Pico SDK needed)
#   cmake -S test -B build-test && cmake --build build-test && ctest --test-dir build-test --output-on-failure
cmake_minimum_required(VERSION 3.13)
project(RPi_Pico_WAV_Player_test C CXX)
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

# UTF-8 to LCD text (file names, tags)
add_executable(test_lcd_text
    test_lcd_text.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../lib/LcdElementBox/lcd_text.c
)
target_include_directories(test_lcd_text PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../lib/LcdElementBox)
target_compile_options(test_lcd_text PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all)
target_link_options(test_lcd_text PRIVATE -fsanitize=address,undefined)

# gain ramp (fades) of the decoder
add_executable(test_gain_ramp test_gain_ramp.cpp)
target_include_directories(test_gain_ramp PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../lib/PlayAudio)
target_compile_options(test_gain_ramp PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all)
target_link_options(test_gain_ramp PRIVATE -fsanitize=address,undefined)

# TagRead error paths (leaks are reported by LeakSanitizer); TagRead itself is built without -Werror
add_executable(test_tag_read
    test_tag_read.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/TagRead.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/utf_conv.cpp
)
target_include_directories(test_tag_read PRIVATE ${CMAKE_CURRENT_LIST_DIR}/stubs ${CMAKE_CURRENT_LIST_DIR}/../src)
target_compile_options(test_tag_read PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all)
set_source_files_properties(test_tag_read.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
target_link_options(test_tag_read PRIVATE -fsanitize=address,undefined)

# file menu (C, unmodified) against an in-memory FatFs (test/stubs_c) with fault injection;
# its allocations go through test_malloc/test_calloc to inject out-of-memory
add_executable(test_file_menu
    test_file_menu.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../lib/file_menu/file_menu_FatFs.c
)
target_include_directories(test_file_menu PRIVATE ${CMAKE_CURRENT_LIST_DIR}/stubs_c ${CMAKE_CURRENT_LIST_DIR}/../lib/file_menu)
target_compile_options(test_file_menu PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all)
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../lib/file_menu/file_menu_FatFs.c PROPERTIES
    COMPILE_DEFINITIONS "malloc=test_malloc;calloc=test_calloc"
    COMPILE_OPTIONS "-Wno-discarded-qualifiers")
target_link_options(test_file_menu PRIVATE -fsanitize=address,undefined)

enable_testing()
add_test(NAME wav_parser COMMAND test_wav_parser)
add_test(NAME read_buffer COMMAND test_read_buffer)
add_test(NAME lcd_text COMMAND test_lcd_text)
add_test(NAME gain_ramp COMMAND test_gain_ramp)
add_test(NAME tag_read COMMAND test_tag_read)
add_test(NAME file_menu COMMAND test_file_menu)
