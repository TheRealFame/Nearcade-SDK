cmake_minimum_required(VERSION 3.22)
project(nearcade_godot VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── godot-cpp (auto-fetch) ─────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    godot-cpp
    GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
    GIT_TAG godot-4.6
)
set(godot-cpp_SRC_DIR "${FETCHCONTENT_BASE_DIR}/godot-cpp-src")
FetchContent_GetProperties(godot-cpp)
if(NOT godot-cpp_POPULATED)
    FetchContent_Populate(godot-cpp)
    add_subdirectory(${godot-cpp_SOURCE_DIR} ${godot-cpp_BINARY_DIR})
endif()

# ── libnearcade (shared library) ───────────────────────────────────────────
# The GDExtension links against libnearcade.so. At runtime, Godot's dynamic
# linker resolves libnearcade + its transitive deps (libdatachannel, etc.).
find_library(NEARCADE_LIBRARIES
    NAMES nearcade
    PATHS ${CMAKE_PREFIX_PATH} ${CMAKE_SOURCE_DIR}/../../build
    NO_DEFAULT_PATH
)
find_path(NEARCADE_INCLUDE_DIR
    NAMES nearcade.h
    PATHS ${CMAKE_SOURCE_DIR}/../../include
    NO_DEFAULT_PATH
)

if(NOT NEARCADE_LIBRARIES OR NOT NEARCADE_INCLUDE_DIR)
    message(FATAL_ERROR "libnearcade not found. Build the SDK first: "
                        "mkdir build && cd build && cmake .. && make")
endif()

message(STATUS "libnearcade: ${NEARCADE_LIBRARIES}")
message(STATUS "nearcade.h:   ${NEARCADE_INCLUDE_DIR}")

# ── Extension shared library ───────────────────────────────────────────────
set(SOURCES
    src/nearcade_godot.cpp
    src/register_types.cpp
)

add_library(nearcade_godot MODULE ${SOURCES})
target_include_directories(nearcade_godot PRIVATE
    ${NEARCADE_INCLUDE_DIR}
    ${godot-cpp_SOURCE_DIR}/include
    ${godot-cpp_SOURCE_DIR}/gen/include
)
target_link_libraries(nearcade_godot PRIVATE
    ${NEARCADE_LIBRARIES}
    godot-cpp
    pthread
)

# Platform-specific output names for Godot GDExtension
# (default "lib" prefix kept for Linux/macOS; Windows has no prefix)
if(WIN32)
    set_target_properties(nearcade_godot PROPERTIES
        PREFIX ""
        SUFFIX ".windows.x86_64.dll"
    )
elseif(APPLE)
    set_target_properties(nearcade_godot PROPERTIES
        SUFFIX ".macos.dylib"
    )
else()
    set_target_properties(nearcade_godot PROPERTIES
        SUFFIX ".linux.x86_64.so"
    )
endif()

set_target_properties(nearcade_godot PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/demo/bin"
)
