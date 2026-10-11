# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The map browser: its model (the cards from the three sources, the filters,
# the order and the primary button; model.hpp). It reads no file and fetches
# nothing.
add_library(oa-ui-map-browser src/model.cpp)
add_library(oa::ui::map_browser ALIAS oa-ui-map-browser)
target_include_directories(oa-ui-map-browser PUBLIC include)
target_compile_features(oa-ui-map-browser PUBLIC cxx_std_20)
target_link_libraries(oa-ui-map-browser PRIVATE oa-options)

if(BUILD_TESTING)
  # The model: merging, every filter and their combination, the search, the
  # order and the primary button.
  add_executable(oa-ui-map-browser-test tests/model_test.cpp)
  target_link_libraries(oa-ui-map-browser-test PRIVATE oa-ui-map-browser oa-test-support
    oa-options)
  add_test(NAME ui-map-browser COMMAND oa-ui-map-browser-test)
  set_tests_properties(ui-map-browser PROPERTIES TIMEOUT 30)
endif()
