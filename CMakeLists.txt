# nbaunrivaled - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(nbaunrivaled LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(NBAUNRIVALED_SOURCES
    src/main.cpp
    src/gpu_poll_backoff.cpp
    src/host_timer_resolution.cpp
    src/host_overlay.cpp
    src/host_settings.cpp
    src/host_pause.cpp
    src/host_restart.cpp
    src/host_gpu_live.cpp
    src/frame_times.cpp
    src/license_unlock.cpp
    src/langoverride_log_filter.cpp
    src/live_menu.cpp
    src/gpu_preference.cpp
    src/frame_pacer.cpp
    src/perf_hud.cpp
)

if(WIN32)
    add_executable(nbaunrivaled WIN32 ${NBAUNRIVALED_SOURCES} resources/nba_unrivaled.rc)
else()
    add_executable(nbaunrivaled ${NBAUNRIVALED_SOURCES})
endif()

rexglue_setup_target(nbaunrivaled GPU_PLUGINS xenos)

# Keep local checkout and SDK paths out of release diagnostics and binaries.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(nba_release_path_maps
        "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.")
    if(REXSDK_DIR)
        file(REAL_PATH "${REXSDK_DIR}" nba_sdk_root)
    else()
        file(REAL_PATH "${rexglue_DIR}/../../.." nba_sdk_root)
    endif()
    list(APPEND nba_release_path_maps "-ffile-prefix-map=${nba_sdk_root}=rexglue")
    target_compile_options(nbaunrivaled PRIVATE
        "$<$<CONFIG:Release>:${nba_release_path_maps}>")
    if(TARGET nbaunrivaled_recomp)
        target_compile_options(nbaunrivaled_recomp PRIVATE
            "$<$<CONFIG:Release>:${nba_release_path_maps}>")
    endif()
endif()

# Build metadata is generated once per configure and compiled into the host only.
set(NBA_BUILD_VERSION "development" CACHE STRING "Version reported in game logs")
if(NOT NBA_BUILD_VERSION MATCHES "^[A-Za-z0-9._+-]+$")
    message(FATAL_ERROR "NBA_BUILD_VERSION must be a plain version or build label")
endif()
execute_process(COMMAND git rev-parse HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE NBA_BUILD_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NBA_BUILD_REVISION STREQUAL "")
    set(NBA_BUILD_REVISION "unknown")
else()
    execute_process(COMMAND git status --porcelain
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE NBA_BUILD_CHANGES OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT NBA_BUILD_CHANGES STREQUAL "")
        string(APPEND NBA_BUILD_REVISION "+working-tree")
    endif()
endif()
string(TIMESTAMP NBA_BUILD_UTC "%Y-%m-%dT%H:%M:%SZ" UTC)
string(SHA256 NBA_BUILD_TOKEN "${NBA_BUILD_VERSION}|${NBA_BUILD_REVISION}|${NBA_BUILD_UTC}")
string(SUBSTRING "${NBA_BUILD_TOKEN}" 0 10 NBA_BUILD_TOKEN)
configure_file(src/build_info.h.in "${CMAKE_CURRENT_BINARY_DIR}/nba_build_info.h" @ONLY)
target_include_directories(nbaunrivaled PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# Player settings app (config.exe), Hitz-style packaging next to the game.
if(WIN32)
    add_executable(nbaunrivaled_config WIN32
        src/config/config.c
        resources/config.rc
    )
    target_include_directories(nbaunrivaled_config PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(nbaunrivaled_config PRIVATE comctl32 user32 gdi32)
    set_target_properties(nbaunrivaled_config PROPERTIES OUTPUT_NAME config)
    if(MSVC)
        set_property(TARGET nbaunrivaled_config PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    # Installer import helper: ZIP/folder/LIVE STFS â†’ {app}\game (no ISO, no texture pack).
    add_executable(nba_import installer/nba_import.c installer/stfs_nba.c)
    target_include_directories(nba_import PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/installer)
    target_link_libraries(nba_import PRIVATE bcrypt shell32 ole32)
    target_compile_definitions(nba_import PRIVATE UNICODE _UNICODE)
    if(MSVC)
        set_property(TARGET nba_import PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# Optional Zen 1 instruction set tuning instead of the portable
# SSE4.1 floor. znver1 emits AVX; CPUs without AVX (e.g. Pentium Silver J5005 /
# Goldmont Plus) can fail with an illegal-instruction error.
# Build with this option off for the portable installer binary.
option(NBA_MARCH_ZNVER1 "Release -march=znver1 (AVX)" ON)
if(NBA_MARCH_ZNVER1 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(nbaunrivaled PRIVATE
    $<$<CONFIG:Release>:-march=znver1>)
  if(TARGET nbaunrivaled_recomp)
    target_compile_options(nbaunrivaled_recomp PRIVATE
      $<$<CONFIG:Release>:-march=znver1>)
  endif()
endif()
