# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# HFS+ volumes: the header, the catalogue and the extents overflow tree.
add_library(oa-formats-hfsplus src/volume.cpp)
add_library(oa::formats::hfsplus ALIAS oa-formats-hfsplus)
target_include_directories(oa-formats-hfsplus PUBLIC include)
target_link_libraries(oa-formats-hfsplus PRIVATE oa-options)
target_compile_features(oa-formats-hfsplus PUBLIC cxx_std_20)

if(BUILD_TESTING)
  add_executable(oa-formats-hfsplus-test tests/hfsplus_test.cpp)
  target_link_libraries(oa-formats-hfsplus-test PRIVATE oa-formats-hfsplus oa-formats-udif
    oa-test-support oa-options)
  target_compile_definitions(oa-formats-hfsplus-test PRIVATE
    OA_UDIF_FIXTURE="${CMAKE_SOURCE_DIR}/src/formats/udif/tests/data/fixture.dmg")
  add_test(NAME formats-hfsplus COMMAND oa-formats-hfsplus-test)
endif()
