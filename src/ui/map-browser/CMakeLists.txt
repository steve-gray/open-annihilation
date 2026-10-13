# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The map browser: its model (the cards from the four sources, the filters,
# the order and the primary button; model.hpp), and its screen made of the UI
# kit's components (the layout at the three size classes, the drawing and
# the input; screen.hpp). It draws nothing of its own, reads no file and
# fetches nothing.
add_library(oa-ui-map-browser src/model.cpp src/layout.cpp src/draw.cpp src/input.cpp)
add_library(oa::ui::map_browser ALIAS oa-ui-map-browser)
target_include_directories(oa-ui-map-browser PUBLIC include)
target_compile_features(oa-ui-map-browser PUBLIC cxx_std_20)
target_link_libraries(oa-ui-map-browser PUBLIC oa-ui-kit oa-ui-map-previews
  PRIVATE oa-data-languages oa-options)

if(BUILD_TESTING)
  # The model (merging, every filter and their combination, the search, the
  # order and the primary button) and the screen (each size class's
  # columns, nothing outside the window, the footer's buttons, the names,
  # the pictures asked for, focus by geometry, Tab's order and the input).
  add_executable(oa-ui-map-browser-test tests/model_test.cpp tests/layout_test.cpp)
  target_link_libraries(oa-ui-map-browser-test PRIVATE oa-ui-map-browser oa-ui-kit
    oa-formats-png oa-formats-hpi oa-platform-text-font oa-present oa-test-game-data
    oa-test-support oa-options)
  oa_copy_text_fonts(oa-ui-map-browser-test "$<TARGET_FILE_DIR:oa-ui-map-browser-test>")
  add_test(NAME ui-map-browser COMMAND oa-ui-map-browser-test)
  set_tests_properties(ui-map-browser PROPERTIES TIMEOUT 30)
endif()
