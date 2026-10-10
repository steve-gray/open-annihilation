# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Registries in effect, their cached catalogues and the worker that refreshes
# them, which installed packages a catalogue has a newer release of, and
# the player's edits to the registries. No SDL.
add_library(oa-app-content src/service.cpp src/worker.cpp src/refresh.cpp src/cache.cpp
  src/updates.cpp src/registries_edit.cpp)
add_library(oa::app::content ALIAS oa-app-content)
target_include_directories(oa-app-content PUBLIC include)
target_include_directories(oa-app-content PRIVATE src)
target_compile_features(oa-app-content PUBLIC cxx_std_20)
target_link_libraries(oa-app-content
  PUBLIC oa-data-registry oa-data-catalogue oa-netgame-http oa-formats-url oa-options
    oa-app-package-install
  PRIVATE oa-formats-oamod oa-platform-shims oa-base-sha256 oa-base-threads oa-app-user-folder
    oa-data-mod-profile)
set_property(SOURCE src/updates.cpp APPEND PROPERTY COMPILE_DEFINITIONS
  "OA_ENGINE_VERSION=\"${PROJECT_VERSION}\"")

if(BUILD_TESTING)
  add_executable(oa-app-content-refresh-rules-test tests/refresh_rules_test.cpp)
  target_link_libraries(oa-app-content-refresh-rules-test PRIVATE oa-app-content oa-test-support)
  add_test(NAME app-content-refresh-rules COMMAND oa-app-content-refresh-rules-test)
  set_tests_properties(app-content-refresh-rules PROPERTIES TIMEOUT 30)

  add_executable(oa-app-content-test tests/service_test.cpp)
  target_link_libraries(oa-app-content-test
    PRIVATE oa-app-content oa-netgame-http-fixture oa-base-signing oa-base-sha256 oa-base-threads
      oa-test-support)
  add_test(NAME app-content COMMAND oa-app-content-test)
  set_tests_properties(app-content PROPERTIES TIMEOUT 120)

  # Installed packages matched to catalogue releases, the updates that follow,
  # and a roll back that brings the older origin record back. No network.
  add_executable(oa-app-content-updates-test tests/updates_test.cpp)
  target_link_libraries(oa-app-content-updates-test
    PRIVATE oa-app-content oa-app-package-install oa-formats-zip oa-base-sha256
      oa-data-mod-profile oa-platform-shims ZLIB::ZLIB oa-test-support oa-options)
  add_test(NAME app-content-updates COMMAND oa-app-content-updates-test)
  set_tests_properties(app-content-updates PROPERTIES TIMEOUT 60)

  add_executable(oa-app-content-registries-test tests/registries_edit_test.cpp)
  target_link_libraries(oa-app-content-registries-test
    PRIVATE oa-app-content oa-netgame-http-fixture oa-base-signing oa-base-sha256 oa-base-threads
      oa-test-support)
  add_test(NAME app-content-registries COMMAND oa-app-content-registries-test)
  set_tests_properties(app-content-registries PROPERTIES TIMEOUT 120)

  if(WIN32)
    foreach(target oa-app-content-refresh-rules-test oa-app-content-test
        oa-app-content-updates-test oa-app-content-registries-test)
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} "-DDLLS=$<TARGET_RUNTIME_DLLS:${target}>"
          "-DDEST=$<TARGET_FILE_DIR:${target}>" -P "${PROJECT_SOURCE_DIR}/tools/copy_runtime.cmake")
    endforeach()
  endif()
endif()
