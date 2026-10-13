# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The content a save, a recording or a film notes, and what loading one that
# needs other content asks.
add_library(oa-data-saves src/content_record.cpp)
add_library(oa::data::saves ALIAS oa-data-saves)
target_include_directories(oa-data-saves PUBLIC include)
target_compile_features(oa-data-saves PUBLIC cxx_std_20)
target_link_libraries(oa-data-saves PUBLIC oa-data-content-bundles)

if(BUILD_TESTING)
  add_executable(oa-data-saves-test tests/content_record_test.cpp)
  target_link_libraries(oa-data-saves-test PRIVATE oa-data-saves oa-test-support)
  add_test(NAME data-saves COMMAND oa-data-saves-test)
endif()
