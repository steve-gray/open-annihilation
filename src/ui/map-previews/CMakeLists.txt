# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Thumbnails for the map rows on screen.
add_library(oa-ui-map-previews src/map_previews.cpp)
add_library(oa::ui::map_previews ALIAS oa-ui-map-previews)
target_include_directories(oa-ui-map-previews PUBLIC include)
target_link_libraries(oa-ui-map-previews
  PUBLIC oa-formats-hpi
  PRIVATE oa-ui-frontend-multiplayer oa-formats-tnt oa-formats-png)

if(BUILD_TESTING)
  add_executable(oa-ui-map-previews-test tests/map_previews_test.cpp)
  target_link_libraries(oa-ui-map-previews-test PRIVATE oa-ui-map-previews oa-formats-png
    oa-formats-tnt oa-formats-hpi oa-data-campaign oa-data-campaign-assets oa-test-game-data)
  add_test(NAME ui-map-previews COMMAND oa-ui-map-previews-test)
  oa_add_game_data_test(ui-map-previews-data oa-ui-map-previews-test --data)
  set_tests_properties(ui-map-previews ui-map-previews-data PROPERTIES TIMEOUT 120)
endif()
