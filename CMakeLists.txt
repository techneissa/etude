cmake_minimum_required(VERSION 3.25)
project(etude VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ETUDE_BUILD_APP "Build the SDL3 app" ON)
option(ETUDE_BUILD_TOOLS "Build developer tools" ON)
option(ETUDE_BUILD_TESTS "Build unit tests" ON)
# The root side of app updates (helper/): built wherever there is a POSIX shell, installed by
# deploy/pi/setup.sh. The helper-only configure (its container test) builds nothing else of ours.
option(ETUDE_BUILD_UPDATE_HELPER "Build etude-update-helper" ON)
option(ETUDE_UPDATE_HELPER_ONLY "Skip core: only the update helper (pass the BUILD_APP, TOOLS and TESTS switches OFF too)" OFF)

# ccache, when it is installed (`brew install ccache`; `sudo apt install ccache`): every checkout and
# checkout of this machine shares the compiled objects of the dependencies and of unchanged code, so
# a new checkout's first build takes a fraction of the time. Off with -DETUDE_CCACHE=OFF. The
# cache is keyed by the preprocessed source, so a changed file always recompiles. CCACHE_BASEDIR
# makes the paths in that key relative to the main checkout (a linked checkout lives in a subfolder of it),
# and CCACHE_NOHASHDIR keeps the build folder out of the key of a -g object; without both, another
# checkout's objects would never match. The sloppiness lets ccache use files that were just
# unpacked or checked out (it refuses a header whose time is too recent to trust). Precompiled
# headers are off while ccache is in use: a precompiled header holds absolute paths, so the
# objects built with one never matched another checkout's, which left our own code, the bulk of a
# build, uncached.
option(ETUDE_CCACHE "Use ccache for compiles when it is installed" ON)
if(ETUDE_CCACHE AND NOT CMAKE_C_COMPILER_LAUNCHER AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  find_program(ETUDE_CCACHE_EXE ccache)
  if(ETUDE_CCACHE_EXE)
    set(etude_checkout_root "${CMAKE_CURRENT_SOURCE_DIR}")
    string(REGEX REPLACE "/\\.worktrees/[^/]+$" "" etude_checkout_root "${etude_checkout_root}")
    set(etude_ccache_launcher ${CMAKE_COMMAND} -E env "CCACHE_BASEDIR=${etude_checkout_root}"
        CCACHE_NOHASHDIR=1 CCACHE_SLOPPINESS=include_file_mtime,include_file_ctime
        ${ETUDE_CCACHE_EXE})
    set(CMAKE_C_COMPILER_LAUNCHER ${etude_ccache_launcher})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${etude_ccache_launcher})
    if(NOT DEFINED CMAKE_DISABLE_PRECOMPILE_HEADERS)
      set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
    endif()
    message(STATUS "ccache: ${ETUDE_CCACHE_EXE} (base ${etude_checkout_root})")
  endif()
endif()

# Warnings for our own targets only, not for fetched dependencies.
add_library(etude_warnings INTERFACE)
if(MSVC)
  target_compile_options(etude_warnings INTERFACE /W4)
else()
  target_compile_options(etude_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

include(cmake/Dependencies.cmake)

if(NOT ETUDE_UPDATE_HELPER_ONLY)
  add_subdirectory(core)
endif()
if(ETUDE_BUILD_UPDATE_HELPER AND UNIX)
  add_subdirectory(helper)
endif()
if(ETUDE_BUILD_APP OR ETUDE_BUILD_TESTS)
  include(cmake/PianoAsset.cmake)
  include(cmake/Synth.cmake)
endif()
if(ETUDE_BUILD_APP)
  add_subdirectory(app)
endif()
if(ETUDE_BUILD_TOOLS)
  add_subdirectory(tools)
endif()
if(ETUDE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
