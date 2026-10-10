# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Painting for the touch controls' layer: RGBA rectangles, the controls'
# icon marks and their text (painter.hpp, docs/touch-controls.md), and
# the gamepad's button glyphs (pad_glyphs.hpp, docs/controllers.md).
add_library(oa-ui-paint src/painter.cpp src/pad_glyphs.cpp)
add_library(oa::ui::paint ALIAS oa-ui-paint)
target_include_directories(oa-ui-paint PUBLIC include)
target_compile_features(oa-ui-paint PUBLIC cxx_std_20)
# The text is drawn with the fonts that travel with the game.
target_link_libraries(oa-ui-paint PUBLIC oa-platform-text-font oa-ui-pad-controls PRIVATE oa-options)

if(BUILD_TESTING)
  # Rectangles clipped to the canvas, the icon marks and the text.
  add_executable(oa-ui-paint-test tests/paint_test.cpp)
  target_link_libraries(oa-ui-paint-test PRIVATE oa-ui-paint oa-test-support oa-options)
  # The text checks read the bundled fonts beside the test, as the game does.
  oa_copy_text_fonts(oa-ui-paint-test "$<TARGET_FILE_DIR:oa-ui-paint-test>")
  add_test(NAME ui-paint COMMAND oa-ui-paint-test)
endif()
