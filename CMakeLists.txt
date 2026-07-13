cmake_minimum_required(VERSION 3.16)
project(nearcade VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(NEARCADE_BUILD_SHARED "Build shared library" ON)
option(NEARCADE_BUILD_SIGNALING "Build embedded WebSocket signaling" ON)
option(NEARCADE_BUILD_WEBRTC   "Build embedded WebRTC via libdatachannel" ON)
option(NEARCADE_BUILD_GODOT    "Build Godot 4.6 GDExtension" OFF)
option(NEARCADE_BUILD_TESTS    "Build tests" ON)

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

# ── libdatachannel for WebRTC ──────────────────────────────────────────────────
if(NEARCADE_BUILD_WEBRTC)
    include(FetchContent)
    set(OPENSSL_USE_STATIC_LIBS OFF)

    FetchContent_Declare(
        libdatachannel
        GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
        GIT_TAG v0.20.0
    )

    set(USE_GNUTLS OFF CACHE BOOL "" FORCE)
    set(USE_NICE OFF CACHE BOOL "" FORCE)
    set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
    set(NO_MEDIA OFF CACHE BOOL "" FORCE)
    set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
    set(NO_TESTS ON CACHE BOOL "" FORCE)

    FetchContent_GetProperties(libdatachannel)
    if(NOT libdatachannel_POPULATED)
        FetchContent_Populate(libdatachannel)
        add_subdirectory(${libdatachannel_SOURCE_DIR} ${libdatachannel_BINARY_DIR})
    endif()

    set(LIBDATACHANNEL_FOUND TRUE)
    add_definitions(-DNEARCADE_HAS_WEBRTC)
    message(STATUS "libdatachannel: FetchContent from ${libdatachannel_SOURCE_DIR}")
endif()

# ── Source files ────────────────────────────────────────────────────────────────
set(NEARCADE_SOURCES
    src/nearcade.c
    src/capture.c
)

if(NEARCADE_BUILD_SIGNALING)
    list(APPEND NEARCADE_SOURCES src/signaling.c)
endif()

if(NEARCADE_BUILD_WEBRTC)
    list(APPEND NEARCADE_SOURCES src/webrtc.cpp)
endif()

# Platform-specific input backend
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
    target_include_directories(nearcade PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS})
    target_link_libraries(nearcade PRIVATE ${LIBWEBSOCKETS_LIBRARIES})
endif()

if(NEARCADE_BUILD_WEBRTC)
    target_link_libraries(nearcade PRIVATE datachannel)
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
        target_include_directories(nearcade_shared PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS})
        target_link_libraries(nearcade_shared PRIVATE ${LIBWEBSOCKETS_LIBRARIES})
    endif()

    if(NEARCADE_BUILD_WEBRTC)
        target_link_libraries(nearcade_shared PRIVATE datachannel)
    endif()

    install(TARGETS nearcade_shared
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
    )
endif()

# ── Godot GDExtension ───────────────────────────────────────────────────────────
if(NEARCADE_BUILD_GODOT AND NEARCADE_BUILD_SHARED)
    if(WIN32)
        set(_nearcade_lib "${CMAKE_BINARY_DIR}/nearcade.lib")
    else()
        set(_nearcade_lib "${CMAKE_BINARY_DIR}/libnearcade${CMAKE_SHARED_LIBRARY_SUFFIX}")
    endif()
    set(NEARCADE_LIBRARIES "${_nearcade_lib}")
    set(NEARCADE_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
    add_subdirectory(bindings/godot)
endif()

# ── Tests ───────────────────────────────────────────────────────────────────────
if(NEARCADE_BUILD_TESTS)
    enable_testing()
    add_executable(nearcade_test test/test_basic.c)
    target_include_directories(nearcade_test PRIVATE include)
    target_link_libraries(nearcade_test PRIVATE nearcade Threads::Threads)

    if(NEARCADE_BUILD_WEBRTC)
        target_link_libraries(nearcade_test PRIVATE datachannel)
    endif()

    add_test(NAME nearcade_basic COMMAND nearcade_test)
    set_tests_properties(nearcade_basic PROPERTIES
        ENVIRONMENT "NEARCADE_LOG_LEVEL=debug"
    )
endif()
