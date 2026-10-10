# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The OA UI kit: one look, one text measure and one layout for every OA screen
# (include/oa/ui/kit/theme.hpp, include/oa/ui/kit/text.hpp,
# include/oa/ui/kit/layout.hpp, include/oa/ui/kit/input.hpp). Provisional until
# the kit is declared stable.
add_library(oa-ui-kit src/text.cpp src/wrap.cpp src/layout.cpp src/input.cpp)
add_library(oa::ui::kit ALIAS oa-ui-kit)
target_include_directories(oa-ui-kit PUBLIC include)
target_compile_features(oa-ui-kit PUBLIC cxx_std_20)
target_link_libraries(oa-ui-kit PUBLIC oa-ui-paint oa-ui-frontend-renderer oa-present oa-formats-fnt
  oa-formats-hpi PRIVATE oa-base-text oa-ui-decoded oa-options)

if(BUILD_TESTING)
  add_executable(oa-ui-kit-theme-test tests/theme_test.cpp)
  target_link_libraries(oa-ui-kit-theme-test PRIVATE oa-ui-kit oa-test-support oa-options)
  add_test(NAME ui-kit-theme COMMAND oa-ui-kit-theme-test)
  set_tests_properties(ui-kit-theme PROPERTIES TIMEOUT 30)

  add_executable(oa-ui-kit-text-test tests/text_test.cpp)
  target_link_libraries(oa-ui-kit-text-test PRIVATE oa-ui-kit oa-test-support oa-options oa-ui-paint
    oa-platform-text-font oa-base-text)
  # The moved paint wrap checks read the bundled fonts beside the test.
  oa_copy_text_fonts(oa-ui-kit-text-test "$<TARGET_FILE_DIR:oa-ui-kit-text-test>")
  add_test(NAME ui-kit-text COMMAND oa-ui-kit-text-test)
  set_tests_properties(ui-kit-text PROPERTIES TIMEOUT 30)

  add_executable(oa-ui-kit-layout-test tests/layout_test.cpp)
  target_link_libraries(oa-ui-kit-layout-test PRIVATE oa-ui-kit oa-test-support oa-options)
  add_test(NAME ui-kit-layout COMMAND oa-ui-kit-layout-test)
  set_tests_properties(ui-kit-layout PROPERTIES TIMEOUT 30)

  add_executable(oa-ui-kit-input-test tests/input_test.cpp)
  target_link_libraries(oa-ui-kit-input-test PRIVATE oa-ui-kit oa-test-support oa-options)
  add_test(NAME ui-kit-input COMMAND oa-ui-kit-input-test)
  set_tests_properties(ui-kit-input PROPERTIES TIMEOUT 30)
endif()
