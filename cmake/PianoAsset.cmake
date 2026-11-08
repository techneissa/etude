# The built-in piano's samples: etude-piano.sf2, the acoustic piano presets extracted from
# FluidR3_GM (MIT, Frank Wen) by tools/extract_piano_sf2.py. The file is 10.8 MB, so it lives
# outside git. By default the build makes it: the tool downloads FluidR3_GM once (148 MB, pinned
# by SHA-256 inside the tool) and extracts the piano into a per-user cache shared by every
# checkout. The result is pinned by SHA-256 too, since the tool is reproducible.
#
#   ETUDE_PIANO_SF2_FILE         a local copy; wins over everything else
#   ETUDE_PIANO_SF2_URL          download a prebuilt copy instead (empty by default)
#   ETUDE_PIANO_FROM_SOURCE      build it from FluidR3_GM when neither is set (default ON; CI
#                                  turns it off to skip the 148 MB download)
#   ETUDE_PIANO_SF2_SHA256       the extracted file's hash
#
# To download from a release that needs authentication, export ETUDE_ASSET_TOKEN and point the
# URL at the API asset (https://api.github.com/repos/OWNER/REPO/releases/assets/ID), or download
# it with `gh release download` and set ETUDE_PIANO_SF2_FILE.
#
# Without the file the build still succeeds and the app plays no sound for taps. Sets
# ETUDE_PIANO_SF2 to the file's path, or to "" when there is none.
set(ETUDE_PIANO_SF2_URL "" CACHE STRING "URL of etude-piano.sf2")
set(ETUDE_PIANO_SF2_SHA256 "0c8cc866ead4bf6b0274cfa338a3bcb1cfedaa21e6f6ad9d3a8fda21c57f3c97"
    CACHE STRING "SHA-256 of etude-piano.sf2")
set(ETUDE_PIANO_SF2_FILE "" CACHE FILEPATH "Local etude-piano.sf2, used instead of the URL")
option(ETUDE_PIANO_FROM_SOURCE "Extract etude-piano.sf2 from FluidR3_GM at configure time" ON)

set(ETUDE_PIANO_SF2 "")
if(ETUDE_PIANO_SF2_FILE)
  if(EXISTS "${ETUDE_PIANO_SF2_FILE}")
    file(SHA256 "${ETUDE_PIANO_SF2_FILE}" have)
    if(NOT have STREQUAL ETUDE_PIANO_SF2_SHA256)
      message(WARNING "${ETUDE_PIANO_SF2_FILE} has SHA-256 ${have}, not the pinned "
                      "${ETUDE_PIANO_SF2_SHA256}. Using it anyway.")
    endif()
    set(ETUDE_PIANO_SF2 "${ETUDE_PIANO_SF2_FILE}")
  else()
    message(WARNING "ETUDE_PIANO_SF2_FILE does not exist: ${ETUDE_PIANO_SF2_FILE}")
  endif()
elseif(ETUDE_PIANO_SF2_URL)
  set(dest "${CMAKE_BINARY_DIR}/_deps/piano/etude-piano.sf2")
  set(have "")
  if(EXISTS "${dest}")
    file(SHA256 "${dest}" have)
  endif()
  if(NOT have STREQUAL ETUDE_PIANO_SF2_SHA256)
    set(headers "")
    if(DEFINED ENV{ETUDE_ASSET_TOKEN})
      set(headers HTTPHEADER "Authorization: Bearer $ENV{ETUDE_ASSET_TOKEN}"
                  HTTPHEADER "Accept: application/octet-stream")
    endif()
    message(STATUS "Downloading the piano samples from ${ETUDE_PIANO_SF2_URL}")
    # The hash is checked after the download: with EXPECTED_HASH, CMake stops on any failed
    # download, and no network must only mean no piano sound. A wrong file still stops the build.
    file(DOWNLOAD "${ETUDE_PIANO_SF2_URL}" "${dest}.part" ${headers} TLS_VERIFY ON
         STATUS status)
    list(GET status 0 code)
    if(code EQUAL 0)
      file(SHA256 "${dest}.part" got)
      if(NOT got STREQUAL ETUDE_PIANO_SF2_SHA256)
        file(REMOVE "${dest}.part")
        message(FATAL_ERROR "The piano samples from ${ETUDE_PIANO_SF2_URL} have SHA-256 "
                            "${got}, expected ${ETUDE_PIANO_SF2_SHA256}.")
      endif()
      file(RENAME "${dest}.part" "${dest}")
      set(have "${got}")
    else()
      file(REMOVE "${dest}.part")
      message(WARNING "Piano samples download failed (${status}); building without them.")
    endif()
  endif()
  # An older file left from a different pin is not used.
  if(have STREQUAL ETUDE_PIANO_SF2_SHA256)
    set(ETUDE_PIANO_SF2 "${dest}")
  endif()
elseif(ETUDE_PIANO_FROM_SOURCE)
  if(DEFINED ENV{XDG_CACHE_HOME} AND NOT "$ENV{XDG_CACHE_HOME}" STREQUAL "")
    set(cache "$ENV{XDG_CACHE_HOME}/etude/piano")
  else()
    set(cache "$ENV{HOME}/.cache/etude/piano")
  endif()
  set(dest "${cache}/etude-piano-${ETUDE_PIANO_SF2_SHA256}.sf2")
  set(have "")
  if(EXISTS "${dest}")
    file(SHA256 "${dest}" have)
  endif()
  if(NOT have STREQUAL ETUDE_PIANO_SF2_SHA256)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
      message(STATUS "Building the piano samples from FluidR3_GM (first time only, 148 MB "
                     "download) into ${cache}")
      execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/extract_piano_sf2.py"
                --cache-dir "${cache}/source" --out "${dest}.part"
        RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
      if(rc EQUAL 0 AND EXISTS "${dest}.part")
        file(SHA256 "${dest}.part" got)
        if(NOT got STREQUAL ETUDE_PIANO_SF2_SHA256)
          file(REMOVE "${dest}.part")
          message(FATAL_ERROR "The extracted piano samples have SHA-256 ${got}, expected "
                              "${ETUDE_PIANO_SF2_SHA256}. If tools/extract_piano_sf2.py "
                              "changed on purpose, update ETUDE_PIANO_SF2_SHA256.")
        endif()
        file(RENAME "${dest}.part" "${dest}")
        # The 148 MB source isn't needed again; the extraction is reproducible from it.
        file(REMOVE_RECURSE "${cache}/source")
        set(have "${got}")
      else()
        file(REMOVE "${dest}.part")
        message(WARNING "Could not build the piano samples (offline?): ${err}"
                        "Building without them.")
      endif()
    else()
      message(WARNING "Python 3 not found, so the piano samples can't be built.")
    endif()
  endif()
  if(have STREQUAL ETUDE_PIANO_SF2_SHA256)
    set(ETUDE_PIANO_SF2 "${dest}")
  endif()
endif()

if(ETUDE_PIANO_SF2)
  message(STATUS "Built-in piano samples: ${ETUDE_PIANO_SF2}")
else()
  message(STATUS "Built-in piano samples: none. Taps and typing make no sound. See "
                 "cmake/PianoAsset.cmake.")
endif()
