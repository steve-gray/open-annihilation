# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The first-run setup: the gate, the three steps, the coach marks and the
# sample Library the online-features step draws (first_run.hpp).
add_library(oa-ui-first-run src/gate.cpp src/coaches.cpp src/screen.cpp src/sample.cpp)
add_library(oa::ui::first_run ALIAS oa-ui-first-run)
target_include_directories(oa-ui-first-run PUBLIC include)
target_compile_features(oa-ui-first-run PUBLIC cxx_std_20)
target_link_libraries(oa-ui-first-run PUBLIC oa-ui-kit oa-data-languages oa-ui-library)

if(BUILD_TESTING)
  # The gate's order, and the coach marks at Compact, Regular and Large.
  add_executable(oa-ui-first-run-test tests/first_run_test.cpp)
  target_link_libraries(oa-ui-first-run-test PRIVATE oa-ui-first-run oa-test-support)
  add_test(NAME ui-first-run COMMAND oa-ui-first-run-test)
  set_tests_properties(ui-first-run PROPERTIES TIMEOUT 30)

  # The setup on windows of 640x480, 1280x720, 1920x1080 and 1920x1117 at 2x:
  # the footer's buttons and the body text at least their stated sizes,
  # nothing overlapping in English, German and Chinese, the header without
  # the mark and the name in a window, and Locate another... holding its
  # words in every language. Drawn in the bundled fonts.
  add_executable(oa-ui-first-run-sizes-test tests/sizes_test.cpp)
  target_link_libraries(oa-ui-first-run-sizes-test PRIVATE oa-ui-first-run oa-test-support
    oa-platform-text-font oa-ui-paint)
  oa_copy_text_fonts(oa-ui-first-run-sizes-test
    "$<TARGET_FILE_DIR:oa-ui-first-run-sizes-test>")
  add_test(NAME ui-first-run-sizes COMMAND oa-ui-first-run-sizes-test
    "${PROJECT_SOURCE_DIR}/languages/interface.tdf"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/zh-Hans-setup.tdf")
  set_tests_properties(ui-first-run-sizes PROPERTIES TIMEOUT 120)

  # Pictures of the three steps for the manual. Not a test: run it with a
  # directory and it writes the four pictures there.
  add_executable(oa-ui-first-run-pictures tests/pictures.cpp)
  target_link_libraries(oa-ui-first-run-pictures PRIVATE oa-ui-first-run oa-formats-png
    oa-platform-text-font oa-ui-paint)
  oa_copy_text_fonts(oa-ui-first-run-pictures "$<TARGET_FILE_DIR:oa-ui-first-run-pictures>")
endif()
