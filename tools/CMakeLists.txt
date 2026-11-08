add_executable(midi-ports midi_ports.cpp)
target_link_libraries(midi-ports PRIVATE etude::core libremidi etude_warnings)

add_executable(midi-demo midi_demo.cpp)
target_link_libraries(midi-demo PRIVATE libremidi etude_warnings)

if(ETUDE_BUILD_APP)
  # Needs SDL3, which only the app option fetches.
  add_executable(display-spike display_spike.cpp)
  target_link_libraries(display-spike PRIVATE SDL3::SDL3 etude_warnings)
endif()

# Pitch detector evaluation and CPU benchmark: core only, no SDL or MIDI.
add_executable(pitch-eval pitch_eval.cpp)
target_link_libraries(pitch-eval PRIVATE etude::core etude_warnings)

# Scores the hand split against the reference hands of a corpus of song files.
add_executable(hand-eval hand_eval.cpp)
target_link_libraries(hand-eval PRIVATE etude::core etude_warnings)

# Writes the medals (eight materials, large and small) as one PNG, to look at them without the app.
add_executable(medal-sheet medal_sheet.cpp)
target_link_libraries(medal-sheet PRIVATE etude::core etude_warnings)
