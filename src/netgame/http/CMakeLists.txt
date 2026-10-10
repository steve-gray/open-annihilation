# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Fetches catalogues and packages over plain HTTP.
add_library(oa-netgame-http
  src/url_request.cpp
  src/response_reader.cpp
  src/gzip.cpp
  src/lookup.cpp
  src/client.cpp)
add_library(oa::netgame::http ALIAS oa-netgame-http)
target_include_directories(oa-netgame-http PUBLIC include)
target_compile_features(oa-netgame-http PUBLIC cxx_std_20)
target_link_libraries(oa-netgame-http
  PUBLIC oa-options
  PRIVATE oa-netgame oa-formats-url oa-base-threads ZLIB::ZLIB)
set_property(SOURCE src/client.cpp APPEND
  PROPERTY COMPILE_DEFINITIONS "OA_ENGINE_VERSION=\"${PROJECT_VERSION}\"")

if(BUILD_TESTING)
  # The reader is compiled into this program so the test does not link the
  # client library, which would define the reader twice.
  add_executable(oa-netgame-http-response-test
    tests/response_reader_test.cpp
    src/response_reader.cpp)
  target_include_directories(oa-netgame-http-response-test PRIVATE src include)
  target_link_libraries(oa-netgame-http-response-test PRIVATE oa-test-support oa-options)
  target_compile_features(oa-netgame-http-response-test PRIVATE cxx_std_20)
  add_test(NAME net-http-response COMMAND oa-netgame-http-response-test)
  set_tests_properties(net-http-response PROPERTIES TIMEOUT 30)

  if(WIN32)
    add_custom_command(TARGET oa-netgame-http-response-test POST_BUILD
      COMMAND ${CMAKE_COMMAND} "-DDLLS=$<TARGET_RUNTIME_DLLS:oa-netgame-http-response-test>"
        "-DDEST=$<TARGET_FILE_DIR:oa-netgame-http-response-test>"
        -P "${PROJECT_SOURCE_DIR}/tools/copy_runtime.cmake")
  endif()
endif()
