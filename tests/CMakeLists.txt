add_executable(etude_tests
  main.cpp
  ${PROJECT_SOURCE_DIR}/helper/layout.cpp
  ${PROJECT_SOURCE_DIR}/helper/state_store.cpp
  abc_test.cpp
  crash_test.cpp
  download_test.cpp
  hands_test.cpp
  helper_test.cpp
  history_store_test.cpp
  key_signature_test.cpp
  library_test.cpp
  midi_test.cpp
  musicxml_test.cpp
  pitch_test.cpp
  release_pack_mac_test.cpp
  repeats_test.cpp
  scoring_test.cpp
  settings_test.cpp
  share_setup_script_test.cpp
  timing_test.cpp
  transpose_test.cpp
  update_test.cpp
  upload_test.cpp)
# The app and helper code under test is built once, in etude_app_core and etude_helper_core; the two
# helper files with test hooks are built here.
target_link_libraries(etude_tests PRIVATE etude_app_core etude_helper_core etude::core
  doctest::doctest etude_warnings)
add_test(NAME etude_tests COMMAND etude_tests)
target_compile_definitions(etude_tests PRIVATE
  ETUDE_HELPER_TEST_HOOKS
  ETUDE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
  ETUDE_TEST_CATALOGUE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/catalogue"
  ETUDE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
