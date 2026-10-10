# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# oa-tool's commands. The executable stays in the root CMakeLists.txt, where
# the Windows DLL copy and the packaged notices attach to it.
add_library(oa-tool-commands STATIC commands.cpp arguments.cpp files.cpp archive_commands.cpp
  pack.cpp pack_map.cpp)
add_library(oa::tool::commands ALIAS oa-tool-commands)
target_include_directories(oa-tool-commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(oa-tool-commands PUBLIC cxx_std_20)
target_link_libraries(oa-tool-commands
  PRIVATE oa-options oa-app-package-install oa-data-languages oa-data-map-pack oa-data-map-fit
    oa-formats-oamod oa-formats-ota oa-formats-tnt oa-formats-zip oa-base-sha256 oa-base-text
    oa-platform-shims
  PUBLIC oa-formats-hpi oa-formats-png oa-ui-decoded)

if(BUILD_TESTING)
  add_executable(oa-tool-test tests/oa_tool_test.cpp)
  target_link_libraries(oa-tool-test PRIVATE oa-tool-commands oa-test-support oa-options)
  add_test(NAME tools-oa-tool COMMAND oa-tool-test)
  set_tests_properties(tools-oa-tool PROPERTIES TIMEOUT 60)

  # pack: a made-up mod and the pseudo language pack, the fixed order and
  # methods, identical bytes after the times change, and the refusals.
  add_executable(oa-tool-pack-test tests/pack_test.cpp)
  target_link_libraries(oa-tool-pack-test PRIVATE oa-tool-commands oa-app-package-install
    oa-data-languages oa-formats-zip oa-base-sha256 oa-platform-shims oa-test-support oa-options)
  add_test(NAME tools-oa-tool-pack COMMAND oa-tool-pack-test
    ${CMAKE_SOURCE_DIR}/src/data/languages/tests/pseudo-pack)
  set_tests_properties(tools-oa-tool-pack PROPERTIES TIMEOUT 60)

  # pack-map: two maps, a shared feature, a preview and the refusals. The
  # source and stand-in game folders are left in the test's working folder.
  add_executable(oa-tool-pack-map-test tests/pack_map_test.cpp)
  target_link_libraries(oa-tool-pack-map-test PRIVATE oa-tool-commands oa-data-map-pack
    oa-formats-zip oa-formats-png oa-formats-tnt oa-base-sha256 oa-platform-shims oa-test-support
    oa-options)
  add_test(NAME tools-oa-tool-pack-map COMMAND oa-tool-pack-map-test)
  set_tests_properties(tools-oa-tool-pack-map PROPERTIES TIMEOUT 60)
endif()
