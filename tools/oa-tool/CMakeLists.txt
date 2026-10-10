# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# oa-tool's commands. The executable stays in the root CMakeLists.txt, where
# the Windows DLL copy and the packaged notices attach to it.
add_library(oa-tool-commands STATIC commands.cpp arguments.cpp files.cpp archive_commands.cpp)
add_library(oa::tool::commands ALIAS oa-tool-commands)
target_include_directories(oa-tool-commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(oa-tool-commands PUBLIC cxx_std_20)
target_link_libraries(oa-tool-commands PRIVATE oa-options PUBLIC oa-formats-hpi oa-formats-png oa-ui-decoded)

if(BUILD_TESTING)
  add_executable(oa-tool-test tests/oa_tool_test.cpp)
  target_link_libraries(oa-tool-test PRIVATE oa-tool-commands oa-test-support oa-options)
  add_test(NAME tools-oa-tool COMMAND oa-tool-test)
  set_tests_properties(tools-oa-tool PROPERTIES TIMEOUT 60)
endif()
