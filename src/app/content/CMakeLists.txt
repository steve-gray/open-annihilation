# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Registries in effect, their cached catalogues and the worker that refreshes
# them, and the download settings kept in preferences. No SDL.
add_library(oa-app-content src/service.cpp src/worker.cpp src/refresh.cpp src/cache.cpp
  src/settings.cpp)
add_library(oa::app::content ALIAS oa-app-content)
target_include_directories(oa-app-content PUBLIC include)
target_include_directories(oa-app-content PRIVATE src)
target_compile_features(oa-app-content PUBLIC cxx_std_20)
target_link_libraries(oa-app-content
  PUBLIC oa-data-registry oa-data-catalogue oa-netgame-http oa-formats-url oa-options
    oa-platform-preferences
  PRIVATE oa-formats-oamod oa-platform-shims oa-base-sha256 oa-base-threads
    oa-platform-random)

if(BUILD_TESTING)
  add_executable(oa-app-content-refresh-rules-test tests/refresh_rules_test.cpp)
  target_link_libraries(oa-app-content-refresh-rules-test PRIVATE oa-app-content oa-test-support)
  add_test(NAME app-content-refresh-rules COMMAND oa-app-content-refresh-rules-test)
  set_tests_properties(app-content-refresh-rules PROPERTIES TIMEOUT 30)

  add_executable(oa-app-content-settings-test tests/settings_test.cpp)
  target_link_libraries(oa-app-content-settings-test PRIVATE oa-app-content oa-test-support)
  add_test(NAME app-content-settings COMMAND oa-app-content-settings-test)
  set_tests_properties(app-content-settings PROPERTIES TIMEOUT 30)

  add_executable(oa-app-content-test tests/service_test.cpp)
  target_link_libraries(oa-app-content-test
    PRIVATE oa-app-content oa-netgame-http-fixture oa-base-signing oa-base-sha256 oa-base-threads
      oa-test-support)
  add_test(NAME app-content COMMAND oa-app-content-test)
  set_tests_properties(app-content PROPERTIES TIMEOUT 120)

  if(WIN32)
    foreach(target oa-app-content-refresh-rules-test oa-app-content-settings-test
        oa-app-content-test)
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} "-DDLLS=$<TARGET_RUNTIME_DLLS:${target}>"
          "-DDEST=$<TARGET_FILE_DIR:${target}>" -P "${PROJECT_SOURCE_DIR}/tools/copy_runtime.cmake")
    endforeach()
  endif()
endif()
