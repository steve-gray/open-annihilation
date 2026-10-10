# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Random bytes from the system's generator. No SDL.
add_library(oa-platform-random src/random.cpp)
add_library(oa::platform::random ALIAS oa-platform-random)
target_include_directories(oa-platform-random PUBLIC include)
target_compile_features(oa-platform-random PUBLIC cxx_std_20)
target_link_libraries(oa-platform-random PUBLIC oa-options)

if(BUILD_TESTING)
  add_executable(oa-platform-random-test tests/random_test.cpp)
  target_link_libraries(oa-platform-random-test PRIVATE oa-platform-random oa-test-support)
  add_test(NAME platform-random COMMAND oa-platform-random-test)
  set_tests_properties(platform-random PROPERTIES TIMEOUT 30)
endif()
