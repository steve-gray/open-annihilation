# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The in-engine locator: places, what a folder or installer is, the layout in
# the Game files screen's look, the hit test, the focus order and what
# presses and keys ask of the app, with no SDL and no app header
# (folder_chooser.hpp).
add_library(oa-ui-folder-chooser src/folder_chooser.cpp src/folder_chooser_layout.cpp
  src/source_account.cpp)
add_library(oa::ui::folder_chooser ALIAS oa-ui-folder-chooser)
target_include_directories(oa-ui-folder-chooser PUBLIC include)
target_compile_features(oa-ui-folder-chooser PUBLIC cxx_std_20)
target_link_libraries(oa-ui-folder-chooser PUBLIC oa-ui-game-files
  PRIVATE oa-data-languages oa-data-content-bundles oa-options oa-ui-kit)

if(BUILD_TESTING)
  # Places, what a folder or installer is recognised as, and choosing it with
  # keys and presses, on the desktop and on a Steam Deck. Every layout case at
  # the Deck and phone sizes is compared with tests/layout-digests.txt.
  add_executable(oa-ui-folder-chooser-test tests/folder_chooser_test.cpp)
  target_link_libraries(oa-ui-folder-chooser-test PRIVATE oa-ui-folder-chooser oa-base-sha256
    oa-test-support oa-options)
  add_test(NAME ui-folder-chooser COMMAND oa-ui-folder-chooser-test
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/layout-digests.txt)
endif()
