# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# UDIF disk images: the koly trailer, mish block tables and zlib chunks.
add_library(oa-formats-udif src/image.cpp)
add_library(oa::formats::udif ALIAS oa-formats-udif)
target_include_directories(oa-formats-udif PUBLIC include)
target_link_libraries(oa-formats-udif PRIVATE ZLIB::ZLIB oa-options)
target_compile_features(oa-formats-udif PUBLIC cxx_std_20)

if(BUILD_TESTING)
  add_executable(oa-formats-udif-test tests/udif_test.cpp)
  target_link_libraries(oa-formats-udif-test PRIVATE oa-formats-udif oa-test-support ZLIB::ZLIB
    oa-options)
  target_compile_definitions(oa-formats-udif-test PRIVATE
    OA_UDIF_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/fixture.dmg")
  add_test(NAME formats-udif COMMAND oa-formats-udif-test)
endif()
