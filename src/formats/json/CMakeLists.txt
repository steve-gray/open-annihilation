# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The engine's strict JSON reader and writer (RFC 8259). First used by the
# automation protocol; any layer that needs JSON links it.
add_library(oa-formats-json src/json.cpp)
add_library(oa::formats::json ALIAS oa-formats-json)
target_include_directories(oa-formats-json PUBLIC include)
target_link_libraries(oa-formats-json PRIVATE oa-options)
target_compile_features(oa-formats-json PUBLIC cxx_std_20)

if(BUILD_TESTING)
  add_executable(oa-formats-json-test tests/json_test.cpp)
  target_link_libraries(oa-formats-json-test PRIVATE oa-formats-json oa-test-support oa-options)
  add_test(NAME formats-json COMMAND oa-formats-json-test)
endif()
