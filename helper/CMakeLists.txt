# The root side of app updates: etude-update-helper (helper/main.cpp). Built from the pure parts of
# core that decide what is trusted (the signature check, the tarball reading, the version, settings
# and state texts) and its own file handling; no SDL and no app code. deploy/pi/setup.sh builds it
# and installs it root-owned; the release workflow does not ship it (a release only changes the app).
set(ETUDE_UPDATE_KEY_SOURCE "${PROJECT_SOURCE_DIR}/core/src/update_key.cpp" CACHE FILEPATH
  "The file that holds the public key the helper verifies updates against. Only the helper's own container test changes it (scripts/update-helper-test.sh), with a key it makes.")
if(NOT ETUDE_UPDATE_KEY_SOURCE STREQUAL "${PROJECT_SOURCE_DIR}/core/src/update_key.cpp")
  message(WARNING "etude-update-helper is built with another update key: ${ETUDE_UPDATE_KEY_SOURCE}. Never ship this build.")
endif()

# The helper's own code except the two files that change under ETUDE_HELPER_TEST_HOOKS
# (layout.cpp, state_store.cpp): built once, linked by the helper and by the unit tests.
add_library(etude_helper_core STATIC
  apply.cpp
  backup.cpp
  count_in_place.cpp
  handoff_files.cpp
  install.cpp
  interrupt.cpp
  process.cpp
  safe_io.cpp
  sandbox.cpp
  service.cpp
  settle.cpp
  stream.cpp
  tree_check.cpp
  unpack.cpp)
target_include_directories(etude_helper_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                                                    ${PROJECT_SOURCE_DIR}/core/include)
target_link_libraries(etude_helper_core PRIVATE etude_warnings)
if(NOT (ETUDE_BUILD_UPDATE_HELPER AND UNIX))
  return()
endif()

set(core_src ${PROJECT_SOURCE_DIR}/core/src)
add_library(etude_update_pure STATIC
  ${core_src}/base64.cpp
  ${core_src}/data_folder.cpp
  ${core_src}/gunzip.cpp
  ${core_src}/gunzip_stream.cpp
  ${core_src}/machine_os.cpp
  ${core_src}/minisign.cpp
  ${core_src}/release_info.cpp
  ${core_src}/semver.cpp
  ${core_src}/tar_archive.cpp
  ${core_src}/update_conf.cpp
  ${core_src}/update_handoff.cpp
  ${core_src}/update_state.cpp
  ${ETUDE_UPDATE_KEY_SOURCE})
target_include_directories(etude_update_pure PUBLIC ${PROJECT_SOURCE_DIR}/core/include)
target_link_libraries(etude_update_pure PRIVATE etude_warnings etude_miniz etude_monocypher)

add_executable(etude-update-helper
  main.cpp
  layout.cpp
  state_store.cpp)
target_include_directories(etude-update-helper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(etude-update-helper PRIVATE etude_helper_core etude_warnings etude_update_pure)
