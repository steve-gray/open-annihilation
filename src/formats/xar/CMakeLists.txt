# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Apple xar archives: the header, the zlib table of contents and gzip members.
# The deflate stream is the zip module's inflater.
add_library(oa-formats-xar src/xar.cpp)
add_library(oa::formats::xar ALIAS oa-formats-xar)
target_include_directories(oa-formats-xar PUBLIC include)
target_link_libraries(oa-formats-xar PRIVATE oa-formats-zip ZLIB::ZLIB oa-options)
target_compile_features(oa-formats-xar PUBLIC cxx_std_20)

if(BUILD_TESTING)
  add_executable(oa-formats-xar-test tests/xar_test.cpp)
  target_link_libraries(oa-formats-xar-test PRIVATE oa-formats-xar ZLIB::ZLIB oa-test-support
    oa-options)
  add_test(NAME formats-xar COMMAND oa-formats-xar-test)
endif()
