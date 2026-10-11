# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The Library's model: every package the player can get or has installed,
# built from plain inputs, with its tabs, filters, search, selection, details
# and download queue lines, and its words (library.hpp, text.hpp). It draws
# nothing and reads no file, clock or network. The update question asked
# before an update that may stop saved games loading, and Settings › Mods'
# lines on updates (update_question.hpp). UNINSTALL's rules, its question
# and its notices (uninstall.hpp).
add_library(oa-ui-library src/library.cpp src/search.cpp src/text.cpp src/counts.cpp
  src/update_question.cpp src/uninstall.cpp)
# The Library's screens, made of the UI kit's components: their layout at
# the three size classes, their drawing and their input (screen.hpp).
target_sources(oa-ui-library PRIVATE src/layout.cpp src/draw.cpp src/input.cpp)
add_library(oa::ui::library ALIAS oa-ui-library)
target_include_directories(oa-ui-library PUBLIC include)
target_compile_features(oa-ui-library PUBLIC cxx_std_20)
target_link_libraries(oa-ui-library PUBLIC oa-formats-oamod oa-base-sha256 oa-ui-kit PRIVATE
  oa-data-mod-profile oa-options)

if(BUILD_TESTING)
  # Entries and their states, blocking, registries, tabs, each tab's filter,
  # New, the filters' counts, tags, the Active tab, the order, selection,
  # buttons, facts, status lines, notes, queue lines and every text.
  add_executable(oa-ui-library-test tests/library_test.cpp)
  target_link_libraries(oa-ui-library-test PRIVATE oa-ui-library oa-data-mod-profile oa-test-support
    oa-options)
  add_test(NAME ui-library COMMAND oa-ui-library-test)
  set_tests_properties(ui-library PROPERTIES TIMEOUT 30)

  # The update question: when it is asked (never for a built-in registry's
  # update, one that keeps the rules, or a language or map pack), its words
  # for a new revision and a new version, UPDATE ALL's, its buttons, and
  # Settings › Mods' lines on updates and on the check for updates.
  add_executable(oa-ui-library-update-question-test tests/update_question_test.cpp)
  target_link_libraries(oa-ui-library-update-question-test PRIVATE oa-ui-library oa-ui-kit
    oa-test-support oa-options)
  add_test(NAME ui-library-update-question COMMAND oa-ui-library-update-question-test)
  set_tests_properties(ui-library-update-question PROPERTIES TIMEOUT 30)

  # UNINSTALL: on for every installed package, off for a map pack in a game
  # and while queued, what it leads to for the mod being played, the
  # language pack in use and any other package, its question, its notices
  # and the note that says why it is off.
  add_executable(oa-ui-library-uninstall-test tests/uninstall_test.cpp)
  target_link_libraries(oa-ui-library-uninstall-test PRIVATE oa-ui-library oa-ui-kit
    oa-test-support oa-options)
  add_test(NAME ui-library-uninstall COMMAND oa-ui-library-uninstall-test)
  set_tests_properties(ui-library-uninstall PROPERTIES TIMEOUT 30)

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

  # Each class drawn twice in the installed game's fonts, and the Active and
  # Updates tabs, byte for byte the same and nothing outside the window;
  # with --frames DIR it writes the pictures for review.
  add_executable(oa-ui-library-draw-test tests/draw_test.cpp)
  target_link_libraries(oa-ui-library-draw-test PRIVATE oa-ui-library oa-ui-kit oa-formats-png
    oa-formats-hpi oa-platform-text-font oa-present oa-test-game-data oa-test-support oa-options)
  # The modern fonts draw the characters the game's fonts lack, as beside the game.
  oa_copy_text_fonts(oa-ui-library-draw-test "$<TARGET_FILE_DIR:oa-ui-library-draw-test>")
  oa_add_game_data_test(ui-library-draw-data oa-ui-library-draw-test --data)
  set_tests_properties(ui-library-draw-data PROPERTIES TIMEOUT 60)
endif()
