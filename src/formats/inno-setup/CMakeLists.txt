# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# A Windows installer made by Inno Setup 5.5.7, read as a file. The setup
# header is LZMA. A file's pieces are stored or LZMA2, then checked.
add_library(oa-formats-inno-setup src/installer.cpp)
add_library(oa::formats::inno_setup ALIAS oa-formats-inno-setup)
target_include_directories(oa-formats-inno-setup PUBLIC include)
target_link_libraries(oa-formats-inno-setup PRIVATE oa-formats-pe oa-formats-lzma ZLIB::ZLIB oa-options)
target_compile_features(oa-formats-inno-setup PUBLIC cxx_std_20)

if(BUILD_TESTING)
  add_executable(oa-formats-inno-setup-test tests/inno_setup_test.cpp)
  target_link_libraries(oa-formats-inno-setup-test PRIVATE oa-formats-inno-setup
    oa-formats-pe-test-image oa-test-support ZLIB::ZLIB oa-options)
  target_compile_definitions(oa-formats-inno-setup-test PRIVATE
    OA_INNO_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  add_test(NAME formats-inno-setup COMMAND oa-formats-inno-setup-test)
endif()
