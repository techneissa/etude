# The built-in piano engine. It has no SDL in it, so the unit tests link it too and render offline.

# TinySoundFont's implementation plus the few helpers that need its internal structs.
# Third-party code: built without our warning flags.
add_library(etude_tsf STATIC ${PROJECT_SOURCE_DIR}/app/tsf_glue.cpp)
target_include_directories(etude_tsf SYSTEM PUBLIC ${tinysoundfont_SOURCE_DIR})
target_compile_features(etude_tsf PUBLIC cxx_std_20)
set_target_properties(etude_tsf PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

add_library(etude_synth STATIC ${PROJECT_SOURCE_DIR}/app/piano_synth.cpp)
target_include_directories(etude_synth PUBLIC ${PROJECT_SOURCE_DIR}/app)
target_link_libraries(etude_synth PUBLIC etude::core PRIVATE etude_tsf etude_warnings)
