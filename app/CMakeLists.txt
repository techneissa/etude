include(${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/EmbedMelodies.cmake)
set(font_source ${CMAKE_CURRENT_BINARY_DIR}/embedded_font.cpp)
etude_embed_file(${ETUDE_FONT_FILE} embedded_font ${font_source})
set(font_regular_source ${CMAKE_CURRENT_BINARY_DIR}/embedded_font_regular.cpp)
etude_embed_file(${ETUDE_FONT_REGULAR_FILE} embedded_font_regular ${font_regular_source})
set(music_font_source ${CMAKE_CURRENT_BINARY_DIR}/embedded_font_music.cpp)
etude_embed_file(${ETUDE_FONT_MUSIC_FILE} embedded_font_music ${music_font_source})
set(icons_source ${CMAKE_CURRENT_BINARY_DIR}/embedded_icons.cpp)
etude_embed_file(${ETUDE_ICON_FONT_FILE} embedded_icons ${icons_source})
# The app icon in each colour, 256 px and 64 px (icon_assets.cpp; the colours are etude::kIconColours).
set(icon_sources)
set(icon_inputs)
foreach(colour wine teal petrol cobalt midnight aubergine graphite black)
  foreach(px 256 64)
    set(icon_source ${CMAKE_CURRENT_BINARY_DIR}/embedded_icon_${colour}_${px}.cpp)
    set(icon_input ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/${colour}/etude-${px}.png)
    etude_embed_file(${icon_input} embedded_icon_${colour}_${px} ${icon_source})
    list(APPEND icon_sources ${icon_source})
    list(APPEND icon_inputs ${icon_input})
  endforeach()
endforeach()
set(brand_github_source ${CMAKE_CURRENT_BINARY_DIR}/embedded_brand_github.cpp)
etude_embed_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/brands/github-mark.svg embedded_brand_github
                   ${brand_github_source})
set(brand_coffee_source ${CMAKE_CURRENT_BINARY_DIR}/embedded_brand_coffee.cpp)
etude_embed_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/brands/bmc-logo-yellow.png embedded_brand_coffee
                   ${brand_coffee_source})
set(upload_page_source ${CMAKE_CURRENT_BINARY_DIR}/embedded_upload_page.cpp)
etude_embed_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/upload/index.html embedded_upload_page
                   ${upload_page_source})
set(melodies_source ${CMAKE_CURRENT_BINARY_DIR}/embedded_melodies.cpp)
etude_embed_melodies(${CMAKE_CURRENT_SOURCE_DIR}/assets/melodies ${melodies_source})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ETUDE_FONT_FILE}
                                       ${CMAKE_CURRENT_SOURCE_DIR}/assets/upload/index.html
                                       ${CMAKE_CURRENT_SOURCE_DIR}/assets/brands/github-mark.svg
                                       ${CMAKE_CURRENT_SOURCE_DIR}/assets/brands/bmc-logo-yellow.png
                                       ${icon_inputs}
                                                               ${ETUDE_FONT_REGULAR_FILE}
                                                               ${ETUDE_FONT_MUSIC_FILE}
                                                               ${ETUDE_ICON_FONT_FILE})

# Dear ImGui with its SDL3 and SDLRenderer3 backends. Third-party code: no warnings of ours.
add_library(etude_imgui STATIC
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(etude_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR}
                                                       ${imgui_SOURCE_DIR}/backends)
target_compile_features(etude_imgui PUBLIC cxx_std_20)
target_link_libraries(etude_imgui PUBLIC SDL3::SDL3)
set_target_properties(etude_imgui PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
# ImGui's own hooks that list every item a frame laid out (ui/item_probe.cpp, for the settings check
# the UI audit's hit areas, ui/item_hook.hpp).
# Characters of any plane: the music font's treble clef (U+1D11E) is outside the first one (the title
# medals' emblems, ui/medal_emblem.cpp).
target_compile_definitions(etude_imgui PUBLIC IMGUI_ENABLE_TEST_ENGINE IMGUI_USE_WCHAR32)

add_executable(etude
  alloc_probe.cpp
  audio_out.cpp
  bench.cpp
  catalogue_service.cpp
  chord_label_view.cpp
  cli_parse.cpp
  cli_parse_dev.cpp
  cli_run.cpp
  cli_upload.cpp
  cli_usage.cpp
  dev_script.cpp
  crash_handler.cpp
  crash_notice.cpp
  crash_screen.cpp
  crash_screen_run.cpp
  crash_session.cpp
  device_check_host.cpp
  display_panel.cpp
  effects.cpp
  folder_events_fsevents.cpp
  folder_events_inotify.cpp
  folder_watcher.cpp
  frame_log.cpp
  gfx.cpp
  glow_shader.cpp
  hand_chords_job.cpp
  hand_split_job.cpp
  history_journal.cpp
  history_schema.cpp
  history_store.cpp
  history_store_journal.cpp
  history_store_open.cpp
  history_store_prefs.cpp
  history_store_profiles.cpp
  history_store_progress.cpp
  history_store_runs.cpp
  history_store_size.cpp
  history_store_songs.cpp
  history_store_thread.cpp
  library_bench.cpp
  learn_sound.cpp
  ear_session.cpp
  library_binding.cpp
  library_binding_progress.cpp
  library_catalogue.cpp
  library_files.cpp
  library_picks.cpp
  library_row.cpp
  log_ring.cpp
  loop_wake.cpp
  mac_relaunch_script.cpp
  mac_update.cpp
  main.cpp
  melody_book.cpp
  mesh.cpp
  mic_capture.cpp
  mic_inbox.cpp
  mic_pipeline.cpp
  mic_source.cpp
  midi_input.cpp
  midi_log_mode.cpp
  midi_output.cpp
  music_font.cpp
  net_fetch.cpp
  panel_drm.cpp
  panel_probe.cpp
  panel_scale.cpp
  persistence.cpp
  port_watcher.cpp
  practice_tables_job.cpp
  profile_looks.cpp
  qr_code.cpp
  replay_demo.cpp
  replay_session.cpp
  resume_keeper.cpp
  run_program.cpp
  runs_file.cpp
  scene.cpp
  scene_block_names.cpp
  scene_combo.cpp
  scene_grades.cpp
  scene_keyboard.cpp
  scene_labels.cpp
  scene_notes.cpp
  scene_replay.cpp
  scene_fingers.cpp
  scene_scale.cpp
  sections_controller.cpp
  settings_check.cpp
  settings_check_taps.cpp
  settings_store.cpp
  share_probe.cpp
  song_board_cache.cpp
  song_controller.cpp
  song_controller_load.cpp
  song_controller_octave.cpp
  song_controller_practice.cpp
  song_controller_run.cpp
  song_controller_session.cpp
  song_controller_sound.cpp
  song_controller_status.cpp
  song_controller_transport.cpp
  song_lengths.cpp
  song_library.cpp
  song_library_file.cpp
  song_library_worker.cpp
  song_opener.cpp
  song_practice_sync.cpp
  song_timing_test.cpp
  song_transport.cpp
  sources_controller.cpp
  spike_bench.cpp
  sqlite_db.cpp
  synth_test.cpp
  text.cpp
  timed_sender.cpp
  ui/accompaniment.cpp
  ui/bar_dock.cpp
  ui/bar_groups.cpp
  ui/bar_lineup.cpp
  ui/bar_pin.cpp
  ui/bar_strip.cpp
  ui/brand_marks.cpp
  ui/card_kit.cpp
  ui/drill_card.cpp
  ui/clef_icon.cpp
  ui/colour_widgets.cpp
  ui/colour_widgets_effects.cpp
  ui/colour_widgets_menu.cpp
  ui/colour_widgets_palette.cpp
  ui/colour_widgets_picker.cpp
  ui/colour_widgets_swatches.cpp
  ui/crash_card.cpp
  ui/crash_screen.cpp
  ui/device_check_card.cpp
  ui/editor.cpp
  ui/editor_gestures.cpp
  ui/editor_internal.cpp
  ui/editor_loupe.cpp
  ui/editor_roll.cpp
  ui/font_ink.cpp
  ui/hit_check.cpp
  ui/host.cpp
  ui/host_band.cpp
  ui/host_bar_reveal.cpp
  ui/host_crash.cpp
  ui/host_fonts.cpp
  ui/host_learn.cpp
  ui/host_menu.cpp
  ui/host_open_panel.cpp
  ui/host_overlays.cpp
  ui/host_panels.cpp
  ui/host_profile.cpp
  ui/host_resume.cpp
  ui/host_status.cpp
  ui/icon_audit.cpp
  ui/icon_audit_fit.cpp
  ui/icon_audit_write.cpp
  ui/icon_audit_tap.cpp
  ui/ink_measure.cpp
  ui/item_probe.cpp
  ui/key_ring_demo.cpp
  ui/kit.cpp
  ui/kit_actions.cpp
  ui/kit_controls.cpp
  ui/kit_internal.cpp
  ui/kit_key_ring.cpp
  ui/kit_note_name.cpp
  ui/kit_reset.cpp
  ui/kit_rows.cpp
  ui/kit_segmented.cpp
  ui/kit_sliders.cpp
  ui/kit_state.cpp
  ui/kit_text.cpp
  ui/kit_toast.cpp
  ui/learn_chord_card.cpp
  ui/learn_drill_ui.cpp
  ui/learn_ear_card.cpp
  ui/learn_keys.cpp
  ui/learn_melodies.cpp
  ui/learn_melodies_art.cpp
  ui/learn_melodies_library.cpp
  ui/learn_scale_card.cpp
  ui/learn_screen.cpp
  ui/library_screen.cpp
  ui/library_screen_cards.cpp
  ui/library_screen_details.cpp
  ui/library_screen_details_rows.cpp
  ui/library_screen_get.cpp
  ui/library_screen_get_details.cpp
  ui/library_screen_melody.cpp
  ui/library_screen_menus.cpp
  ui/library_screen_places.cpp
  ui/library_screen_results.cpp
  ui/library_screen_search.cpp
  ui/library_screen_selection.cpp
  ui/library_screen_shared.cpp
  ui/medal_atlas.cpp
  ui/medal_demo.cpp
  ui/medal_draw.cpp
  ui/medal_emblem.cpp
  ui/medal_wall.cpp
  ui/morph.cpp
  ui/morph_glyphs.cpp
  ui/music_glyph.cpp
  ui/osk.cpp
  ui/page_ids.cpp
  ui/pages.cpp
  ui/pages_about.cpp
  ui/pages_about_icons.cpp
  ui/pages_about_link.cpp
  ui/pages_add_songs.cpp
  ui/pages_add_songs_sources.cpp
  ui/pages_configs.cpp
  ui/pages_configs_confirm.cpp
  ui/pages_input.cpp
  ui/pages_learn_colours.cpp
  ui/pages_midi_ports.cpp
  ui/panel_names.cpp
  ui/preview.cpp
  ui/profile_confirm.cpp
  ui/profile_ui.cpp
  ui/progress_charts.cpp
  ui/progress_page.cpp
  ui/qr_draw.cpp
  ui/replay_bar.cpp
  ui/results_card.cpp
  ui/run_card.cpp
  ui/scale_pill.cpp
  ui/section_band.cpp
  ui/sections_ui.cpp
  ui/settings_catalogue.cpp
  ui/sheet.cpp
  ui/summary_board.cpp
  ui/summary_chart.cpp
  ui/summary_page.cpp
  ui/tap.cpp
  ui/tempo.cpp
  ui/top_menu.cpp
  ui/transport.cpp
  ui/transpose.cpp
  update_files.cpp
  update_install.cpp
  update_install_mac.cpp
  update_service.cpp
  update_status.cpp
  upload_api.cpp
  upload_controller.cpp
  upload_connections.cpp
  upload_page.cpp
  upload_server.cpp
  upload_service.cpp
  usb_import.cpp
  usb_scan.cpp
  version_grouper.cpp
  virtual_input.cpp
  watchdog.cpp
  wav_feeder.cpp
  wav_file.cpp
  window_crash.cpp
  window_device_check.cpp
  window_draw.cpp
  window_drill.cpp
  window_events.cpp
  window_frame.cpp
  window_icon.cpp
  icon_assets.cpp
  icon_service.cpp
  desktop_entry.cpp
  window_midi_ports.cpp
  window_options.cpp
  window_profiles.cpp
  window_recover.cpp
  window_replay.cpp
  window_resume.cpp
  window_run.cpp
  window_runs.cpp
  window_shutdown.cpp
  window_songs.cpp
  window_startup.cpp)
target_sources(etude PRIVATE 
  ui/column_check.cpp
  ui/row_layout.cpp)
# Sheet music: the engraver (the one file that sees Verovio and lunasvg), its worker thread and the
# view that draws its pictures.
target_sources(etude PRIVATE 
  sheet/sheet_dump.cpp
  sheet/sheet_engraver.cpp
  sheet/sheet_job.cpp
  sheet/sheet_name_fit.cpp
  sheet/sheet_preview.cpp
  sheet/sheet_view.cpp
  sheet/sheet_view_input.cpp
  sheet/sheet_view_names.cpp
  sheet/sheet_view_pictures.cpp
  sheet/sheet_worker.cpp)
target_link_libraries(etude PRIVATE etude::core etude_synth SDL3::SDL3 SDL3_ttf::SDL3_ttf
  libremidi etude_imgui etude_sqlite etude_verovio etude_lunasvg etude_warnings)
# "Get songs" downloads over HTTPS with the system's libcurl (curl licence, GPL-compatible) and its
# certificates: the macOS SDK's, libcurl4-openssl-dev on Linux.
target_link_libraries(etude PRIVATE etude_curl)
# The phone upload page's HTTP server (cpp-httplib, MIT), used in app/upload_server.cpp.
target_link_libraries(etude PRIVATE etude_httplib)
# The QR code on the "Add songs" page (Nayuki's QR Code generator, MIT), used in app/qr_code.cpp.
target_link_libraries(etude PRIVATE etude_qrcodegen)
if(UNIX AND NOT APPLE)
  # The crash report's stack trace names its functions from the dynamic symbol table.
  set_target_properties(etude PROPERTIES ENABLE_EXPORTS ON)
endif()
# The fonts Verovio reads at run time go beside the app: Bravura (the music font), Leipzig (which
# it loads at start as its fallback) and the text metrics.
foreach(item Bravura.xml Bravura Leipzig.xml Leipzig text)
  file(COPY ${ETUDE_VEROVIO_DATA}/${item} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/verovio-data)
endforeach()
# The repository's test songs join the library in a dev build from a checkout. The option is on
# for every build type but Release, so a Release binary does not carry the checkout path.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(_etude_dev_songs_default OFF)
else()
  set(_etude_dev_songs_default ON)
endif()
option(ETUDE_DEV_SONGS "Add the repository's tests/data songs to the library" ${_etude_dev_songs_default})
if(ETUDE_DEV_SONGS)
  target_compile_definitions(etude PRIVATE ETUDE_DEV_SONGS_DIR="${PROJECT_SOURCE_DIR}/tests/data")
endif()
if(APPLE)
  target_link_libraries(etude PRIVATE "-framework CoreAudio")
  # FSEvents, which tells the library when files arrive in the Songs folder.
  target_link_libraries(etude PRIVATE "-framework CoreServices")
  # The binary carries its own Info.plist, so the system has a microphone usage text for it when
  # the app itself is the process asking (launched from Finder or, later, from an app bundle). Run
  # from a terminal, the terminal is the one asking, and its own Info.plist counts.
  target_link_options(etude PRIVATE
    "LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist")
  set_property(TARGET etude APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist)
endif()
# The piano samples go beside the executable, where AudioOut looks for them.
if(ETUDE_PIANO_SF2)
  configure_file(${ETUDE_PIANO_SF2} ${CMAKE_CURRENT_BINARY_DIR}/etude-piano.sf2 COPYONLY)
endif()
# Generated, so kept out of the warning flags.
add_library(etude_font OBJECT ${font_source} ${font_regular_source} ${music_font_source}
  ${icons_source}
  ${icon_sources} ${brand_github_source} ${brand_coffee_source}
  ${upload_page_source} ${melodies_source})
target_compile_features(etude_font PRIVATE cxx_std_20)
target_sources(etude PRIVATE $<TARGET_OBJECTS:etude_font>)
