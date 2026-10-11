# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The Library's model: every package the player can get or has installed,
# built from plain inputs, with its tabs, filters, search, selection, details
# and download queue lines, and its words (library.hpp, text.hpp). It draws
# nothing and reads no file, clock or network.
add_library(oa-ui-library src/library.cpp src/search.cpp src/text.cpp)
# The Library's screens, made of the UI kit's components: their layout at
# the three size classes, their drawing and their input (screen.hpp).
target_sources(oa-ui-library PRIVATE src/layout.cpp src/draw.cpp src/input.cpp)
add_library(oa::ui::library ALIAS oa-ui-library)
target_include_directories(oa-ui-library PUBLIC include)
target_compile_features(oa-ui-library PUBLIC cxx_std_20)
target_link_libraries(oa-ui-library PUBLIC oa-formats-oamod oa-base-sha256 oa-ui-kit PRIVATE
  oa-data-mod-profile oa-options)

if(BUILD_TESTING)
  # Entries and their states, blocking, registries, tabs, filters, tags, the
  # order, selection, buttons, facts, status lines, notes, queue lines and
  # every text.
  add_executable(oa-ui-library-test tests/library_test.cpp)
  target_link_libraries(oa-ui-library-test PRIVATE oa-ui-library oa-data-mod-profile oa-test-support
    oa-options)
  add_test(NAME ui-library COMMAND oa-ui-library-test)
  set_tests_properties(ui-library PROPERTIES TIMEOUT 30)

  # The search: what ranks first, several words, a missing word, folding,
  # tag prefixes, the cap and ties.
  add_executable(oa-ui-library-search-test tests/search_test.cpp)
  target_link_libraries(oa-ui-library-search-test PRIVATE oa-ui-library oa-test-support oa-options)
  add_test(NAME ui-library-search COMMAND oa-ui-library-search-test)
  set_tests_properties(ui-library-search PROPERTIES TIMEOUT 30)

  # The screens' layout at Compact, Regular and Large and at three windows:
  # parts inside the frame, no two controls meeting, every action a control,
  # Tab's order and the arrows, the names, the texts fitting and the Rules
  # fact whole, and what the keys, the controller and the pointer change.
  add_executable(oa-ui-library-layout-test tests/layout_test.cpp)
  target_link_libraries(oa-ui-library-layout-test PRIVATE oa-ui-library oa-ui-kit oa-test-support
    oa-options)
  add_test(NAME ui-library-layout COMMAND oa-ui-library-layout-test)
  set_tests_properties(ui-library-layout PROPERTIES TIMEOUT 60)

  # Each class drawn twice in the installed game's fonts, byte for byte the
  # same and nothing outside the window; with --frames DIR it writes the
  # pictures for review.
  add_executable(oa-ui-library-draw-test tests/draw_test.cpp)
  target_link_libraries(oa-ui-library-draw-test PRIVATE oa-ui-library oa-ui-kit oa-formats-png
    oa-formats-hpi oa-platform-text-font oa-present oa-test-game-data oa-test-support oa-options)
  # The modern fonts draw the characters the game's fonts lack, as beside the game.
  oa_copy_text_fonts(oa-ui-library-draw-test "$<TARGET_FILE_DIR:oa-ui-library-draw-test>")
  oa_add_game_data_test(ui-library-draw-data oa-ui-library-draw-test --data)
  set_tests_properties(ui-library-draw-data PROPERTIES TIMEOUT 60)
endif()
