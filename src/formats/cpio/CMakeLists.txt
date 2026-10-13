# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Portable ASCII cpio archives, odc and newc, read a piece at a time.
add_library(oa-formats-cpio src/cpio.cpp)
add_library(oa::formats::cpio ALIAS oa-formats-cpio)
target_include_directories(oa-formats-cpio PUBLIC include)
target_link_libraries(oa-formats-cpio PRIVATE oa-options)
target_compile_features(oa-formats-cpio PUBLIC cxx_std_20)

if(BUILD_TESTING)
  add_executable(oa-formats-cpio-test tests/cpio_test.cpp)
  target_link_libraries(oa-formats-cpio-test PRIVATE oa-formats-cpio oa-test-support oa-options)
  add_test(NAME formats-cpio COMMAND oa-formats-cpio-test)
endif()
