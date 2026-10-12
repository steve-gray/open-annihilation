# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# LZMA and LZMA2. The decode itself is the public-domain LZMA SDK, kept
# unchanged under vendor/ and compiled without the engine's warnings.
add_library(oa-formats-lzma src/decoder.cpp vendor/LzmaDec.c vendor/Lzma2Dec.c)
add_library(oa::formats::lzma ALIAS oa-formats-lzma)
target_include_directories(oa-formats-lzma PUBLIC include PRIVATE vendor)
target_link_libraries(oa-formats-lzma PRIVATE oa-options)
target_compile_features(oa-formats-lzma PUBLIC cxx_std_20)
if(MSVC)
  set_source_files_properties(vendor/LzmaDec.c vendor/Lzma2Dec.c PROPERTIES COMPILE_OPTIONS /W0)
else()
  set_source_files_properties(vendor/LzmaDec.c vendor/Lzma2Dec.c PROPERTIES COMPILE_OPTIONS -w)
endif()

if(BUILD_TESTING)
  add_executable(oa-formats-lzma-test tests/lzma_test.cpp)
  target_link_libraries(oa-formats-lzma-test PRIVATE oa-formats-lzma oa-test-support oa-options)
  target_compile_definitions(oa-formats-lzma-test PRIVATE
    OA_LZMA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  add_test(NAME formats-lzma COMMAND oa-formats-lzma-test)
endif()
