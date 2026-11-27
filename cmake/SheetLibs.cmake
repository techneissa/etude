# Verovio, lunasvg and plutovg as static libraries; included by Dependencies.cmake when they are
# built here. With -DETUDE_DEPS_INSTALL=ON they are also installed, with a CMake file that gives
# them back as the same targets (cmake/DepsPrefix.cmake reads it).
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
# The include folders, relative to the Verovio sources; a prefix has the same tree under
# include/verovio.
set(etude_verovio_includes include include/vrv include/crc include/midi include/hum include/json
  include/pugi include/zip include/tuning-library libmei/dist libmei/addons)
foreach(dir ${etude_verovio_includes})
  target_include_directories(etude_verovio SYSTEM PUBLIC
    $<BUILD_INTERFACE:${vrv}/${dir}> $<INSTALL_INTERFACE:include/verovio/${dir}>)
endforeach()
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
target_include_directories(etude_plutovg SYSTEM PUBLIC
  $<BUILD_INTERFACE:${luna}/plutovg/include> $<INSTALL_INTERFACE:include/lunasvg/plutovg>)
# Fonts come from the app's own memory, never from the system's folders.
target_compile_definitions(etude_plutovg PRIVATE PLUTOVG_BUILD PLUTOVG_DISABLE_FONT_FACE_CACHE_LOAD
  PUBLIC PLUTOVG_BUILD_STATIC)
add_library(etude_lunasvg STATIC ${luna}/source/lunasvg.cpp ${luna}/source/graphics.cpp
  ${luna}/source/svgelement.cpp ${luna}/source/svggeometryelement.cpp
  ${luna}/source/svglayoutstate.cpp ${luna}/source/svgpaintelement.cpp
  ${luna}/source/svgparser.cpp ${luna}/source/svgproperty.cpp
  ${luna}/source/svgrenderstate.cpp ${luna}/source/svgtextelement.cpp)
target_include_directories(etude_lunasvg SYSTEM PUBLIC
  $<BUILD_INTERFACE:${luna}/include> $<INSTALL_INTERFACE:include/lunasvg> PRIVATE ${luna}/source)
target_compile_definitions(etude_lunasvg PRIVATE LUNASVG_BUILD LUNASVG_DISABLE_LOAD_SYSTEM_FONTS
  PUBLIC LUNASVG_BUILD_STATIC)
target_link_libraries(etude_lunasvg PUBLIC etude_plutovg)
if(UNIX AND NOT APPLE)
  target_link_libraries(etude_plutovg PUBLIC m)
endif()
foreach(target etude_verovio etude_plutovg etude_lunasvg)
  target_compile_options(${target} PRIVATE -w)
  set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
endforeach()

if(ETUDE_DEPS_INSTALL)
  install(TARGETS etude_verovio etude_plutovg etude_lunasvg EXPORT etude-deps ARCHIVE DESTINATION lib)
  install(EXPORT etude-deps FILE etude-deps-config.cmake DESTINATION lib/cmake/etude-deps)
  install(DIRECTORY ${vrv}/include DESTINATION include/verovio)
  install(DIRECTORY ${vrv}/libmei/dist ${vrv}/libmei/addons DESTINATION include/verovio/libmei
    FILES_MATCHING PATTERN "*.h")
  install(FILES ${luna}/include/lunasvg.h DESTINATION include/lunasvg)
  install(FILES ${luna}/plutovg/include/plutovg.h DESTINATION include/lunasvg/plutovg)
  install(DIRECTORY ${vrv}/data DESTINATION share/verovio)
endif()
