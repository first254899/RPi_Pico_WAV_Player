if (NOT TARGET fatfs_owner)
    add_library(fatfs_owner INTERFACE)

    target_sources(fatfs_owner INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/fatfs_owner.c
    )

    target_link_libraries(fatfs_owner INTERFACE
        pico_stdlib
    )
    target_include_directories(fatfs_owner INTERFACE ${CMAKE_CURRENT_LIST_DIR})

    # FATFS_ASSERT_OWNER() is on when NDEBUG is not defined; -DFATFS_OWNER_CHECK=ON enables it in Release builds too
    option(FATFS_OWNER_CHECK "check the FatFs cross-core ownership rule (see fatfs_owner.h)" OFF)
    if (FATFS_OWNER_CHECK)
        target_compile_definitions(fatfs_owner INTERFACE FATFS_OWNER_CHECK=1)
    endif()
endif()
