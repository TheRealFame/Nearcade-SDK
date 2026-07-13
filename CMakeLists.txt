cmake_minimum_required(VERSION 3.16)
project(nearcade VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(NEARCADE_BUILD_SHARED "Build shared library" ON)
option(NEARCADE_BUILD_SIGNALING "Build embedded WebSocket signaling" ON)
option(NEARCADE_BUILD_TESTS "Build tests" ON)

find_package(Threads REQUIRED)

# ── libwebsockets for signaling ─────────────────────────────────────────────────
if(NEARCADE_BUILD_SIGNALING)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBWEBSOCKETS libwebsockets)
    endif()
    if(NOT LIBWEBSOCKETS_FOUND)
        find_library(LIBWEBSOCKETS_LIBRARIES websockets)
        find_path(LIBWEBSOCKETS_INCLUDE_DIRS libwebsockets.h
            PATHS /usr/include /usr/local/include
        )
        if(LIBWEBSOCKETS_LIBRARIES AND LIBWEBSOCKETS_INCLUDE_DIRS)
            set(LIBWEBSOCKETS_FOUND TRUE)
        endif()
    endif()
    if(LIBWEBSOCKETS_FOUND)
        add_definitions(-DNEARCADE_HAS_SIGNALING)
    else()
        message(STATUS "libwebsockets not found — signaling server disabled")
        set(NEARCADE_BUILD_SIGNALING OFF)
    endif()
endif()

# ── Source files ────────────────────────────────────────────────────────────────
set(NEARCADE_SOURCES
    src/nearcade.c
    src/capture.c
)

if(NEARCADE_BUILD_SIGNALING)
    list(APPEND NEARCADE_SOURCES src/signaling.c)
endif()

# Platform-specific input backend + generic input dispatcher
if(WIN32)
    list(APPEND NEARCADE_SOURCES src/input_win.c)
elseif(APPLE)
    list(APPEND NEARCADE_SOURCES src/input_mac.c)
else()
    list(APPEND NEARCADE_SOURCES src/input_linux.c)
endif()

# ── Static library ──────────────────────────────────────────────────────────────
add_library(nearcade STATIC ${NEARCADE_SOURCES})
target_include_directories(nearcade PUBLIC include PRIVATE src)
target_link_libraries(nearcade PRIVATE Threads::Threads)

if(NEARCADE_BUILD_SIGNALING)
    target_link_libraries(nearcade PRIVATE ${LIBWEBSOCKETS_LIBRARIES})
    target_include_directories(nearcade PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS})
endif()

install(TARGETS nearcade ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

# ── Shared library ──────────────────────────────────────────────────────────────
if(NEARCADE_BUILD_SHARED)
    add_library(nearcade_shared SHARED ${NEARCADE_SOURCES})
    set_target_properties(nearcade_shared PROPERTIES
        OUTPUT_NAME nearcade
        SOVERSION ${PROJECT_VERSION_MAJOR}
        VERSION ${PROJECT_VERSION}
    )
    target_compile_definitions(nearcade_shared PRIVATE NEARCADE_BUILD_SHARED)
    target_include_directories(nearcade_shared PUBLIC include PRIVATE src)
    target_link_libraries(nearcade_shared PRIVATE Threads::Threads)

    if(NEARCADE_BUILD_SIGNALING)
        target_link_libraries(nearcade_shared PRIVATE ${LIBWEBSOCKETS_LIBRARIES})
        target_include_directories(nearcade_shared PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS})
    endif()

    install(TARGETS nearcade_shared
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
    )
endif()

# ── Tests ───────────────────────────────────────────────────────────────────────
if(NEARCADE_BUILD_TESTS)
    enable_testing()
    add_executable(nearcade_test test/test_basic.c)
    target_include_directories(nearcade_test PRIVATE include)
    target_link_libraries(nearcade_test PRIVATE nearcade Threads::Threads)
    add_test(NAME nearcade_basic COMMAND nearcade_test)
    set_tests_properties(nearcade_basic PROPERTIES
        ENVIRONMENT "NEARCADE_LOG_LEVEL=debug"
    )
endif()
