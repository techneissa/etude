# Every third-party library is a release tarball pinned by SHA-256, so a fresh clone builds the same
# everywhere. To upgrade one, change the URL and replace the hash with `shasum -a 256` of the new file.
include(FetchContent)
set(CMAKE_POLICY_DEFAULT_CMP0135 NEW) # extracted files get the extraction time, so rebuilds work

# The release archives are downloaded once per machine, into ~/.cache/etude/downloads/<name>/
# (-DETUDE_DEP_DOWNLOADS=DIR moves it, an empty value turns it off): a new checkout finds the
# archive there, checks its SHA-256 and unpacks it, instead of downloading about 100 MB again. A
# build folder that already has its sources keeps downloading where it did (the choice is made when
# the folder is first configured and kept in its cache), since a changed download folder would
# unpack every source again and so rebuild every dependency.
if(NOT DEFINED ETUDE_DEP_DOWNLOADS AND DEFINED ENV{HOME})
  set(ETUDE_DEP_DOWNLOADS "$ENV{HOME}/.cache/etude/downloads")
endif()
if(NOT DEFINED ETUDE_DEP_DOWNLOADS_IN_USE)
  if(FETCHCONTENT_BASE_DIR)
    set(etude_deps_dir "${FETCHCONTENT_BASE_DIR}")
  else()
    set(etude_deps_dir "${CMAKE_BINARY_DIR}/_deps")
  endif()
  if(ETUDE_DEP_DOWNLOADS AND NOT EXISTS "${etude_deps_dir}/pugixml-src")
    set(ETUDE_DEP_DOWNLOADS_IN_USE ON CACHE INTERNAL "")
  else()
    set(ETUDE_DEP_DOWNLOADS_IN_USE OFF CACHE INTERNAL "")
  endif()
endif()
# Two configures at once (two new checkouts) would write one archive at the same time and one of
# them would fail its hash check. The lock is held while this file runs, which is when every
# dependency is downloaded; a second configure waits for it (up to 15 minutes), then finds the
# archives in place. It is released at the end of this file, or when the process ends.
if(ETUDE_DEP_DOWNLOADS_IN_USE)
  file(MAKE_DIRECTORY "${ETUDE_DEP_DOWNLOADS}")
  file(LOCK "${ETUDE_DEP_DOWNLOADS}/.lock" GUARD FILE TIMEOUT 900 RESULT_VARIABLE etude_download_lock)
  if(NOT etude_download_lock EQUAL 0)
    message(FATAL_ERROR "another configure holds ${ETUDE_DEP_DOWNLOADS}/.lock for over 15 minutes: ${etude_download_lock}")
  endif()
endif()

# Every dependency below is declared through this, which adds the download folder. (Wrapping
# FetchContent_Declare itself does not hold: libremidi's CMake includes FetchContent again, which
# puts the original back.)
function(etude_declare name)
  if(ETUDE_DEP_DOWNLOADS_IN_USE AND NOT "DOWNLOAD_NO_EXTRACT" IN_LIST ARGN) # a single file stays in its source folder
    FetchContent_Declare(${name} DOWNLOAD_DIR "${ETUDE_DEP_DOWNLOADS}/${name}" ${ARGN})
  else()
    FetchContent_Declare(${name} ${ARGN})
  endif()
endfunction()

if(ETUDE_BUILD_APP)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  etude_declare(SDL3
    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.16.tar.gz
    URL_HASH SHA256=c2ee715e42ec520c4d11fd8d249ef1d2b2baf4ad31148b72b3b000276c0b3633
    SYSTEM)
  FetchContent_MakeAvailable(SDL3)

  # Text for the chord name and status lines. SDL3_ttf (zlib) needs FreeType, which its release
  # tarball leaves out, so FreeType's own release tarball is unpacked into SDL3_ttf's external/
  # folder and built as SDL3_ttf's vendored copy. FreeType is dual-licensed FTL / GPL-2.0-or-later;
  # the FTL is GPL-3.0-compatible. HarfBuzz and plutosvg are off: chord names are plain Latin text.
  # SOURCE_SUBDIR points at a missing folder so MakeAvailable only downloads, and the
  # add_subdirectory below runs once FreeType is in place.
  etude_declare(SDL3_ttf
    URL https://github.com/libsdl-org/SDL_ttf/releases/download/release-3.2.2/SDL3_ttf-3.2.2.tar.gz
    URL_HASH SHA256=63547d58d0185c833213885b635a2c0548201cc8f301e6587c0be1a67e1e045d
    SOURCE_SUBDIR no-add-subdirectory
    SYSTEM)
  FetchContent_MakeAvailable(SDL3_ttf)
  etude_declare(freetype
    URL https://download.savannah.gnu.org/releases/freetype/freetype-2.14.1.tar.xz
        https://downloads.sourceforge.net/project/freetype/freetype2/2.14.1/freetype-2.14.1.tar.xz
    URL_HASH SHA256=32427e8c471ac095853212a37aef816c60b42052d4d9e48230bab3bdf2936ccc
    SOURCE_DIR ${sdl3_ttf_SOURCE_DIR}/external/freetype
    SOURCE_SUBDIR no-add-subdirectory)
  FetchContent_MakeAvailable(freetype)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
  set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
  set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
  set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
  set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory(${sdl3_ttf_SOURCE_DIR} ${sdl3_ttf_BINARY_DIR} EXCLUDE_FROM_ALL SYSTEM)
  # Third-party warnings are not ours to fix.
  foreach(target freetype SDL3_ttf-static)
    set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
  endforeach()

  # The chord-name font: Atkinson Hyperlegible Bold (SIL OFL 1.1, Braille Institute), designed to
  # stay readable at a distance. One file from the google/fonts repository at a fixed commit. It is
  # compiled into the app (app/CMakeLists.txt), so nothing has to be installed next to the binary.
  # A binary release must ship the OFL text with it.
  etude_declare(font_atkinson
    URL https://raw.githubusercontent.com/google/fonts/95f4904fc8bcf26d3420fe315560c96417c6dec7/ofl/atkinsonhyperlegible/AtkinsonHyperlegible-Bold.ttf
    URL_HASH SHA256=5a3b0c8cc8ca545155150b4512a1fa248298df121c50d6557e651e61fbdab92f
    DOWNLOAD_NO_EXTRACT TRUE)
  FetchContent_MakeAvailable(font_atkinson)
  set(ETUDE_FONT_FILE ${font_atkinson_SOURCE_DIR}/AtkinsonHyperlegible-Bold.ttf)

  # The settings screens use the regular weight of the same family for body text (same commit,
  # same licence as above).
  etude_declare(font_atkinson_regular
    URL https://raw.githubusercontent.com/google/fonts/95f4904fc8bcf26d3420fe315560c96417c6dec7/ofl/atkinsonhyperlegible/AtkinsonHyperlegible-Regular.ttf
    URL_HASH SHA256=7fb917c89019896d0b52ee84b7cbb3304c18cb90b19a62f5e32712bd23e97669
    DOWNLOAD_NO_EXTRACT TRUE)
  FetchContent_MakeAvailable(font_atkinson_regular)
  set(ETUDE_FONT_REGULAR_FILE
      ${font_atkinson_regular_SOURCE_DIR}/AtkinsonHyperlegible-Regular.ttf)

  # The flat and sharp signs (U+266D, U+266F) of every name the app writes: Atkinson has neither, so
  # Noto Music (SIL OFL 1.1, Google) fills them in behind it, merged into the ImGui font atlas and
  # added as a fallback to the scene's SDL_ttf font. Same repository and commit as Atkinson. A
  # binary release must ship the OFL text with it.
  etude_declare(font_noto_music
    URL https://raw.githubusercontent.com/google/fonts/95f4904fc8bcf26d3420fe315560c96417c6dec7/ofl/notomusic/NotoMusic-Regular.ttf
    URL_HASH SHA256=e913be269fe16723d1dea0afc3c31a28be6958f6a7f0e6d5be6e98506c4022bd
    DOWNLOAD_NO_EXTRACT TRUE)
  FetchContent_MakeAvailable(font_noto_music)
  set(ETUDE_FONT_MUSIC_FILE ${font_noto_music_SOURCE_DIR}/NotoMusic-Regular.ttf)

  # Icons for the touch UI: the Lucide icon font (ISC; the icons derived from Feather are MIT), from
  # the lucide-font release archive. Merged into the ImGui font atlas, so a control can draw an icon
  # by its codepoint (app/ui/icons.hpp lists the ones used). A binary release must ship
  # Lucide's ISC licence text with it.
  etude_declare(font_lucide
    URL https://github.com/lucide-icons/lucide/releases/download/1.55.0/lucide-font-1.55.0.zip
    URL_HASH SHA256=f5f44ae0e910259ed1b832e370b3bb96405bb88abef6658b1166fc7053b108c9
    SOURCE_SUBDIR no-add-subdirectory)
  FetchContent_MakeAvailable(font_lucide)
  set(ETUDE_ICON_FONT_FILE ${font_lucide_SOURCE_DIR}/lucide.ttf)

  # Dear ImGui (MIT), the settings and menu screens. The release tarball has no CMake project, so
  # it is only downloaded here and app/CMakeLists.txt builds its core files plus the SDL3 platform
  # and SDLRenderer3 renderer backends into a static library.
  etude_declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
    URL_HASH SHA256=21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99
    SOURCE_SUBDIR no-add-subdirectory
    SYSTEM)
  FetchContent_MakeAvailable(imgui)

  # Sheet music: Verovio (LGPL-3.0, RISM Digital Center) engraves MusicXML into SVG, and lunasvg
  # with its plutovg (both MIT; plutovg carries FreeType-derived rasteriser code under the FTL and
  # stb_truetype/stb_image, public domain or MIT) draws the SVG into bitmaps. LGPL-3.0 lets the
  # combined work be conveyed under GPL-3.0, so the linked binary is GPL-3.0; a binary release must
  # ship Verovio's COPYING and COPYING.LESSER, the MIT texts of lunasvg, plutovg, libmei, pugixml
  # and jsonxx, the FTL and the Bravura font's OFL. Verovio's own CMake only builds an executable or
  # a shared library, so both are only downloaded and built here as static libraries private to
  # app/, with every warning off (not ours to fix).
  etude_declare(verovio
    URL https://github.com/rism-digital/verovio/archive/refs/tags/version-6.3.0.tar.gz
    URL_HASH SHA256=095a561e9bd26c97787ad49e01009576d1ca234feae3995948c5aede97a40598
    SOURCE_SUBDIR no-add-subdirectory
    SYSTEM)
  FetchContent_MakeAvailable(verovio)
  etude_declare(lunasvg
    URL https://github.com/sammycage/lunasvg/archive/refs/tags/v3.5.0.tar.gz
    URL_HASH SHA256=1abf1472ee6c4d19797916e8cc3c2e4b628e0d81178ffac60bdb0d457e32c690
    SOURCE_SUBDIR no-add-subdirectory
    SYSTEM)
  FetchContent_MakeAvailable(lunasvg)

  # Verovio trimmed to what the app reads: MusicXML (core writes MusicXML for ABC, MuseScore and
  # MIDI songs itself), so the Humdrum, PAE, DARMS, GABC, ABC and compressed-MusicXML readers are
  # left out. Its zip reader (filereader.cpp) embeds an older miniz whose global mz_* symbols clash
  # with core's, so app/sheet/verovio_zip_stub.cpp stands in for it: core unzips and hands Verovio
  # plain XML. Its bundled pugixml is renamed (pugi=vrv_pugi), since core links its own built with
  # other options.
  set(vrv ${verovio_SOURCE_DIR})
  file(GLOB etude_verovio_sources ${vrv}/src/*.cpp ${vrv}/libmei/dist/*.cpp
       ${vrv}/libmei/addons/*.cpp ${vrv}/src/midi/*.cpp ${vrv}/src/crc/*.cpp)
  list(APPEND etude_verovio_sources ${vrv}/src/json/jsonxx.cc ${vrv}/src/pugi/pugixml.cpp
       ${PROJECT_SOURCE_DIR}/app/sheet/verovio_zip_stub.cpp)
  list(FILTER etude_verovio_sources EXCLUDE REGEX "/src/filereader\\.cpp$")
  add_library(etude_verovio STATIC ${etude_verovio_sources})
  target_include_directories(etude_verovio SYSTEM PUBLIC ${vrv}/include ${vrv}/include/vrv
    ${vrv}/include/crc ${vrv}/include/midi ${vrv}/include/hum ${vrv}/include/json
    ${vrv}/include/pugi ${vrv}/include/zip ${vrv}/include/tuning-library ${vrv}/libmei/dist
    ${vrv}/libmei/addons)
  target_compile_definitions(etude_verovio PUBLIC pugi=vrv_pugi NO_DARMS_SUPPORT NO_RUNTIME
    NO_HUMDRUM_SUPPORT NO_PAE_SUPPORT NO_ABC_SUPPORT NO_GABC_SUPPORT NO_MXL_SUPPORT
    RESOURCE_DIR="")
  target_compile_features(etude_verovio PUBLIC cxx_std_20)
  # The fonts Verovio reads at run time: the Bravura glyphs and metrics (its fallback Leipzig too,
  # which it loads at start) and the text metrics. Copied beside the app, where it looks for them.
  set(ETUDE_VEROVIO_DATA ${vrv}/data)

  set(luna ${lunasvg_SOURCE_DIR})
  add_library(etude_plutovg STATIC ${luna}/plutovg/source/plutovg-blend.c
    ${luna}/plutovg/source/plutovg-canvas.c ${luna}/plutovg/source/plutovg-font.c
    ${luna}/plutovg/source/plutovg-matrix.c ${luna}/plutovg/source/plutovg-paint.c
    ${luna}/plutovg/source/plutovg-path.c ${luna}/plutovg/source/plutovg-rasterize.c
    ${luna}/plutovg/source/plutovg-surface.c ${luna}/plutovg/source/plutovg-ft-math.c
    ${luna}/plutovg/source/plutovg-ft-raster.c ${luna}/plutovg/source/plutovg-ft-stroker.c)
  target_include_directories(etude_plutovg SYSTEM PUBLIC ${luna}/plutovg/include)
  # Fonts come from the app's own memory, never from the system's folders.
  target_compile_definitions(etude_plutovg PRIVATE PLUTOVG_BUILD PLUTOVG_DISABLE_FONT_FACE_CACHE_LOAD
    PUBLIC PLUTOVG_BUILD_STATIC)
  add_library(etude_lunasvg STATIC ${luna}/source/lunasvg.cpp ${luna}/source/graphics.cpp
    ${luna}/source/svgelement.cpp ${luna}/source/svggeometryelement.cpp
    ${luna}/source/svglayoutstate.cpp ${luna}/source/svgpaintelement.cpp
    ${luna}/source/svgparser.cpp ${luna}/source/svgproperty.cpp
    ${luna}/source/svgrenderstate.cpp ${luna}/source/svgtextelement.cpp)
  target_include_directories(etude_lunasvg SYSTEM PUBLIC ${luna}/include PRIVATE ${luna}/source)
  target_compile_definitions(etude_lunasvg PRIVATE LUNASVG_BUILD LUNASVG_DISABLE_LOAD_SYSTEM_FONTS
    PUBLIC LUNASVG_BUILD_STATIC)
  target_link_libraries(etude_lunasvg PUBLIC etude_plutovg)
  if(UNIX AND NOT APPLE)
    target_link_libraries(etude_plutovg PUBLIC m)
  endif()
  foreach(target etude_verovio etude_plutovg etude_lunasvg)
    if(MSVC)
      target_compile_options(${target} PRIVATE /w)
    else()
      target_compile_options(${target} PRIVATE -w)
    endif()
    set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
  endforeach()
endif()

# The app reads live MIDI and the tools list ports and play test patterns.
if(ETUDE_BUILD_APP OR ETUDE_BUILD_TOOLS)
  set(LIBREMIDI_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(LIBREMIDI_TESTS OFF CACHE BOOL "" FORCE)
  set(LIBREMIDI_NO_BOOST ON CACHE BOOL "" FORCE)
  set(LIBREMIDI_NO_JACK ON CACHE BOOL "" FORCE)
  set(LIBREMIDI_NO_NETWORK ON CACHE BOOL "" FORCE)
  etude_declare(libremidi
    URL https://github.com/celtera/libremidi/archive/refs/tags/v5.4.3.tar.gz
    URL_HASH SHA256=a1c369c6aab01c80f1506345d1c099a167b330d1d59159aa7583f7b0b9f966b9
    SYSTEM)
  FetchContent_MakeAvailable(libremidi)
endif()

# TinySoundFont (MIT), the built-in piano's SoundFont player: one header, tsf.h. Upstream has no
# release tags, so this is the archive of a fixed commit (v0.9, 2026-07-19). It is only
# downloaded here; cmake/Synth.cmake compiles the implementation into etude_tsf.
if(ETUDE_BUILD_APP OR ETUDE_BUILD_TESTS)
  etude_declare(tinysoundfont
    URL https://github.com/schellingb/TinySoundFont/archive/853a0a171759f1ddba0de1442133a75912bbeffa.tar.gz
    URL_HASH SHA256=76c356df524f71f2a34f67fa0cb2cb946036fe7a65d2930a849569c5e742fa57
    SOURCE_SUBDIR no-add-subdirectory
    SYSTEM)
  FetchContent_MakeAvailable(tinysoundfont)
endif()

# Score files (MusicXML, .mxl, .mscz) are read inside core, from bytes the caller hands over.
# pugixml (MIT) parses the XML and miniz (MIT) unzips .mxl and .mscz. Neither release ships a build
# we want: pugixml's is a project with options and miniz's needs a generated export header. So both
# are only downloaded (SOURCE_SUBDIR points at a missing folder) and built here as static libraries
# private to core. Their warnings are not ours to fix: the include folders are SYSTEM and the
# compile flags drop all warnings.
etude_declare(pugixml
  URL https://github.com/zeux/pugixml/releases/download/v1.16/pugixml-1.16.tar.gz
  URL_HASH SHA256=4cee1ca4aad395170f4c7a07824f3bdd41f28316c6e1e1090a1425b278ec0b4b
  SOURCE_SUBDIR no-add-subdirectory
  SYSTEM)
FetchContent_MakeAvailable(pugixml)
add_library(etude_pugixml STATIC ${pugixml_SOURCE_DIR}/src/pugixml.cpp)
target_include_directories(etude_pugixml SYSTEM PUBLIC ${pugixml_SOURCE_DIR}/src)
# No exceptions across core's API, no XPath (unused), no file or stream access.
target_compile_definitions(etude_pugixml PUBLIC
  PUGIXML_NO_EXCEPTIONS PUGIXML_NO_XPATH PUGIXML_NO_STL)

etude_declare(miniz
  URL https://github.com/richgel999/miniz/archive/refs/tags/3.1.2.tar.gz
  URL_HASH SHA256=98468f8924934b723276680f85238b6c78bf1f8b49b4459cc9b7214a20e2e9fb
  SOURCE_SUBDIR no-add-subdirectory
  SYSTEM)
FetchContent_MakeAvailable(miniz)
# miniz.h includes a header CMake generates in its own build; an empty export macro is all we need
# for a static library.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/miniz_generated/miniz_export.h
  "#pragma once\n#define MINIZ_EXPORT\n")
add_library(etude_miniz STATIC
  ${miniz_SOURCE_DIR}/miniz.c ${miniz_SOURCE_DIR}/miniz_tdef.c ${miniz_SOURCE_DIR}/miniz_tinfl.c
  ${miniz_SOURCE_DIR}/miniz_zip.c)
target_include_directories(etude_miniz SYSTEM PUBLIC ${miniz_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/miniz_generated)
# core reads zips from memory only: no stdio, so no file system access, and no writing.
target_compile_definitions(etude_miniz PUBLIC MINIZ_NO_STDIO MINIZ_NO_ARCHIVE_WRITING_APIS)
if(MSVC)
  target_compile_options(etude_miniz PRIVATE /w)
  target_compile_options(etude_pugixml PRIVATE /w)
else()
  target_compile_options(etude_miniz PRIVATE -w)
  target_compile_options(etude_pugixml PRIVATE -w)
endif()
set_target_properties(etude_miniz etude_pugixml PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

# KISS FFT (BSD-3-Clause), the FFT behind core's pitch detector. Only kiss_fft.c and kiss_fftr.c are
# built, as a static library private to core, with float samples. The detector only uses
# power-of-two sizes and out-of-place real transforms, where KISS FFT never allocates after
# kiss_fftr_alloc, so a transform is safe on the real-time analysis thread.
etude_declare(kissfft
  URL https://github.com/mborgerding/kissfft/archive/refs/tags/131.2.0.tar.gz
  URL_HASH SHA256=205a8f6a448ef12b091f8ac6a514b5091bb5f6b0b543431ed75f673116cf5cbf
  SOURCE_SUBDIR no-add-subdirectory
  SYSTEM)
FetchContent_MakeAvailable(kissfft)
add_library(etude_kissfft STATIC ${kissfft_SOURCE_DIR}/kiss_fft.c ${kissfft_SOURCE_DIR}/kiss_fftr.c)
target_include_directories(etude_kissfft SYSTEM PUBLIC ${kissfft_SOURCE_DIR})
target_compile_definitions(etude_kissfft PUBLIC kiss_fft_scalar=float)
if(MSVC)
  target_compile_options(etude_kissfft PRIVATE /w)
else()
  target_compile_options(etude_kissfft PRIVATE -w)
endif()
set_target_properties(etude_kissfft PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

# Monocypher (BSD-2-Clause or CC0, either is GPL-3.0-compatible), the ed25519 and BLAKE2b behind the
# check of a downloaded release's minisign signature (core/src/minisign.cpp). Four unmodified files
# of version 4.0.2 are kept in third_party/monocypher (see its README for the source and checksums)
# rather than fetched, since a GitHub-generated tag archive can change its bytes. They are built as
# a static library private to core: monocypher.c (BLAKE2b) and the optional monocypher-ed25519.c
# (standard ed25519 with SHA-512, which is what minisign signs; the core file's own EdDSA uses
# BLAKE2b and would not verify a minisign signature). The signature tests (tests/minisign_test.cpp)
# must pass after an upgrade.
set(monocypher_dir ${PROJECT_SOURCE_DIR}/third_party/monocypher)
add_library(etude_monocypher STATIC
  ${monocypher_dir}/monocypher.c
  ${monocypher_dir}/monocypher-ed25519.c)
target_include_directories(etude_monocypher SYSTEM PUBLIC ${monocypher_dir})
if(MSVC)
  target_compile_options(etude_monocypher PRIVATE /w)
else()
  target_compile_options(etude_monocypher PRIVATE -w)
endif()
set_target_properties(etude_monocypher PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

# SQLite (public domain), the practice history database. The amalgamation is one C file, built here
# as a static library private to app/ (core never sees it). Version 3.54.0, pinned by the SHA-256 of
# the zip (sqlite.org lists SHA3-256 7b670a62fdfbd672b75fef004cb703c8a3e87d3a5cc7d675b4a08337004a2d93).
# To upgrade, take the URL from https://sqlite.org/download.html and hash the new file. The options
# drop what the app does not use: loadable extensions (so no libdl on the Pi), the deprecated
# interfaces and double-quoted strings; threads are serialised because the history writer runs on
# its own thread.
if(ETUDE_BUILD_APP OR ETUDE_BUILD_TESTS)
  etude_declare(sqlite3
    URL https://sqlite.org/2026/sqlite-amalgamation-3540000.zip
    URL_HASH SHA256=68e913b0fe8ec6b4e5f391c594a28872884e80d6cd042a1e50c957be46b5aa4d
    SOURCE_SUBDIR no-add-subdirectory
    SYSTEM)
  FetchContent_MakeAvailable(sqlite3)
  find_package(Threads REQUIRED)
  add_library(etude_sqlite STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
  target_include_directories(etude_sqlite SYSTEM PUBLIC ${sqlite3_SOURCE_DIR})
  target_compile_definitions(etude_sqlite PRIVATE
    SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_DEPRECATED SQLITE_DQS=0
    SQLITE_DEFAULT_MEMSTATUS=0 SQLITE_DEFAULT_FOREIGN_KEYS=1 SQLITE_LIKE_DOESNT_MATCH_BLOBS)
  target_link_libraries(etude_sqlite PUBLIC Threads::Threads)
  if(UNIX AND NOT APPLE)
    target_link_libraries(etude_sqlite PUBLIC m)
  endif()
  if(MSVC)
    target_compile_options(etude_sqlite PRIVATE /w)
  else()
    target_compile_options(etude_sqlite PRIVATE -w)
  endif()
  set_target_properties(etude_sqlite PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
endif()

# cpp-httplib (MIT), the HTTP server of the phone upload page ("Add songs from phone"). One header,
# httplib.h, used in app/upload_server.cpp alone; TLS, compression and the other optional support
# are all off, so it needs nothing but the system's sockets. It is only downloaded (SOURCE_SUBDIR
# points at a missing folder) and its folder is an include path, SYSTEM so its warnings are not ours.
# Version 0.61.0, pinned by the SHA-256 of the GitHub tag archive. To upgrade, change the URL and
# hash the new file; the upload tests (tests/upload_http_test.cpp) must pass.
if(ETUDE_BUILD_APP OR ETUDE_BUILD_TESTS)
  etude_declare(httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.61.0.tar.gz
    URL_HASH SHA256=f03bdb0bee4911d6db0335bc103f0f441ae70c527e993c1f4b3f177586b042d3
    SOURCE_SUBDIR no-add-subdirectory
    SYSTEM)
  FetchContent_MakeAvailable(httplib)
  add_library(etude_httplib INTERFACE)
  target_include_directories(etude_httplib SYSTEM INTERFACE ${httplib_SOURCE_DIR})
  find_package(Threads REQUIRED)
  target_link_libraries(etude_httplib INTERFACE Threads::Threads)
endif()

# QR Code generator library (Project Nayuki, MIT), the QR code of the "Add songs" page: it draws
# the address of the upload page so a phone can open it with its camera. Two files, qrcodegen.cpp
# and qrcodegen.hpp, built as a static library of its own (no warnings of ours) and used through
# app/qr_code.* alone. Version 1.8.0, pinned by the SHA-256 of the GitHub tag archive.
if(ETUDE_BUILD_APP OR ETUDE_BUILD_TESTS)
  etude_declare(qrcodegen
    URL https://github.com/nayuki/QR-Code-generator/archive/refs/tags/v1.8.0.tar.gz
    URL_HASH SHA256=2ec0a4d33d6f521c942eeaf473d42d5fe139abcfa57d2beffe10c5cf7d34ae60
    SOURCE_SUBDIR no-add-subdirectory
    SYSTEM)
  FetchContent_MakeAvailable(qrcodegen)
  add_library(etude_qrcodegen STATIC ${qrcodegen_SOURCE_DIR}/cpp/qrcodegen.cpp)
  target_include_directories(etude_qrcodegen SYSTEM PUBLIC ${qrcodegen_SOURCE_DIR}/cpp)
  target_compile_features(etude_qrcodegen PUBLIC cxx_std_20)
  if(MSVC)
    target_compile_options(etude_qrcodegen PRIVATE /w)
  else()
    target_compile_options(etude_qrcodegen PRIVATE -w)
  endif()
  set_target_properties(etude_qrcodegen PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
endif()

if(ETUDE_BUILD_TESTS)
  etude_declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz
    URL_HASH SHA256=174ebc4e769928959614789c5b4e9c3d0a0f81a62bb608756b127bfebfb21331
    SYSTEM)
  FetchContent_MakeAvailable(doctest)
endif()

# libcurl comes from the system (an exception to the pinned-tarball rule): the
# macOS SDK's, libcurl4-openssl-dev on Linux. On macOS the SDK's curl headers are on the compiler's
# default path, and CURL::libcurl would add the SDK's usr/include as -isystem, which puts the C
# headers ahead of libc++'s whenever the build was configured against another Xcode (the libc++
# <cstdlib> error), so there the library alone is linked.
# (The update helper's own configure, which builds nothing that fetches, does not need it.)
if(NOT ETUDE_UPDATE_HELPER_ONLY)
  find_package(CURL 7.85 REQUIRED)
  add_library(etude_curl INTERFACE)
  if(APPLE)
    target_link_libraries(etude_curl INTERFACE ${CURL_LIBRARIES})
  else()
    target_link_libraries(etude_curl INTERFACE CURL::libcurl)
  endif()
endif()
