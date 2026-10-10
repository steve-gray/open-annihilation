# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# A test-only HTTP/1.1 server on 127.0.0.1, and the oa-http-fixture tool that
# serves one folder with it. Built only when the tests are built.
add_library(oa-netgame-http-fixture src/server.cpp)
add_library(oa::netgame::http_fixture ALIAS oa-netgame-http-fixture)
target_include_directories(oa-netgame-http-fixture PUBLIC include)
target_compile_features(oa-netgame-http-fixture PUBLIC cxx_std_20)
target_link_libraries(oa-netgame-http-fixture PUBLIC oa-options PRIVATE oa-netgame oa-base-threads ZLIB::ZLIB)

add_executable(oa-http-fixture src/main.cpp)
target_link_libraries(oa-http-fixture PRIVATE oa-netgame-http-fixture oa-base-threads oa-platform-shims)

add_executable(oa-netgame-http-fixture-test tests/server_test.cpp)
target_link_libraries(oa-netgame-http-fixture-test PRIVATE oa-netgame-http-fixture oa-netgame ZLIB::ZLIB)
add_test(NAME net-http-fixture COMMAND oa-netgame-http-fixture-test)
set_tests_properties(net-http-fixture PROPERTIES TIMEOUT 60)

if(WIN32)
  foreach(target oa-http-fixture oa-netgame-http-fixture-test)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} "-DDLLS=$<TARGET_RUNTIME_DLLS:${target}>"
        "-DDEST=$<TARGET_FILE_DIR:${target}>" -P "${PROJECT_SOURCE_DIR}/tools/copy_runtime.cmake")
  endforeach()
endif()
