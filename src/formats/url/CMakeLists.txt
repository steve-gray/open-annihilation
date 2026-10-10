# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Web addresses: the strict reader the HTTP client, the registry rules and
# the catalogue share.
add_library(oa-formats-url src/url.cpp)
add_library(oa::formats::url ALIAS oa-formats-url)
target_include_directories(oa-formats-url PUBLIC include)
target_link_libraries(oa-formats-url PRIVATE oa-options)
target_compile_features(oa-formats-url PUBLIC cxx_std_20)

if(BUILD_TESTING)
  add_executable(oa-formats-url-test tests/url_test.cpp)
  target_link_libraries(oa-formats-url-test PRIVATE oa-formats-url oa-test-support oa-options)
  add_test(NAME formats-url COMMAND oa-formats-url-test)
endif()
