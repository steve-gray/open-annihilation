# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Map packs: the manifest oamap.yaml, the index of each map, and the name a
# map carries in the battle room and in a saved game. It takes bytes and
# plain values and never opens a file.
add_library(oa-data-map-pack src/manifest.cpp src/write.cpp src/map_name.cpp)
add_library(oa::data::map_pack ALIAS oa-data-map-pack)
target_include_directories(oa-data-map-pack PUBLIC include)
target_compile_features(oa-data-map-pack PUBLIC cxx_std_20)
target_link_libraries(oa-data-map-pack PRIVATE oa-formats-oamod oa-options)

if(BUILD_TESTING)
  # Manifests read and refused by rule, names split and fitted, and a
  # manifest written and read back.
  add_executable(oa-data-map-pack-test tests/manifest_test.cpp)
  target_link_libraries(oa-data-map-pack-test PRIVATE oa-data-map-pack oa-test-support oa-options)
  add_test(NAME data-map-pack COMMAND oa-data-map-pack-test)
  set_tests_properties(data-map-pack PROPERTIES TIMEOUT 30)
endif()
