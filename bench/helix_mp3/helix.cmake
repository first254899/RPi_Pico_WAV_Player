# Helix MP3 decoder sources (lib/libhelix-mp3, unmodified) built with port/helix_port.h force-included
set(HELIX_DIR ${CMAKE_CURRENT_LIST_DIR}/../../lib/libhelix-mp3)
set(HELIX_SOURCES
    ${HELIX_DIR}/mp3dec.c
    ${HELIX_DIR}/mp3tabs.c
    ${HELIX_DIR}/real/bitstream.c
    ${HELIX_DIR}/real/buffers.c
    ${HELIX_DIR}/real/dct32.c
    ${HELIX_DIR}/real/dequant.c
    ${HELIX_DIR}/real/dqchan.c
    ${HELIX_DIR}/real/huffman.c
    ${HELIX_DIR}/real/hufftabs.c
    ${HELIX_DIR}/real/imdct.c
    ${HELIX_DIR}/real/polyphase.c
    ${HELIX_DIR}/real/scalfact.c
    ${HELIX_DIR}/real/stproc.c
    ${HELIX_DIR}/real/subband.c
    ${HELIX_DIR}/real/trigtabs.c
)
set(HELIX_INCLUDES ${HELIX_DIR}/pub ${HELIX_DIR}/real ${CMAKE_CURRENT_LIST_DIR}/port)
# third-party code: its own warnings are not ours to fix
set(HELIX_OPTIONS -include ${CMAKE_CURRENT_LIST_DIR}/port/helix_port.h -w)

# add_helix_library(<name> <port mode>): static library
function(add_helix_library name mode)
    add_library(${name} STATIC ${HELIX_SOURCES})
    target_include_directories(${name} PUBLIC ${HELIX_INCLUDES})
    target_compile_options(${name} PRIVATE $<$<COMPILE_LANGUAGE:C>:${HELIX_OPTIONS}>)
    target_compile_definitions(${name} PRIVATE HELIX_PORT_MODE=${mode} HELIX_BENCH_MALLOC)
endfunction()
