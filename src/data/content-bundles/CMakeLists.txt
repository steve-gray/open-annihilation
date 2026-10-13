# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-data-content-bundles src/content_bundles.cpp src/content_choice.cpp src/content_needs.cpp)
add_library(oa::data::content_bundles ALIAS oa-data-content-bundles)
target_include_directories(oa-data-content-bundles PUBLIC include)
target_compile_features(oa-data-content-bundles PUBLIC cxx_std_20)
target_link_libraries(oa-data-content-bundles PUBLIC oa-base-sha256 oa-formats-oamod PRIVATE oa-options)
if(BUILD_TESTING)
  add_executable(oa-data-content-bundles-test tests/content_bundles_test.cpp)
  target_link_libraries(oa-data-content-bundles-test PRIVATE oa-data-content-bundles oa-test-support
    oa-app-headers)
  add_test(NAME data-content-bundles COMMAND oa-data-content-bundles-test)
  add_executable(oa-data-content-choice-test tests/content_choice_test.cpp)
  target_link_libraries(oa-data-content-choice-test PRIVATE oa-data-content-bundles oa-test-support)
  add_test(NAME data-content-choice COMMAND oa-data-content-choice-test)
endif()
