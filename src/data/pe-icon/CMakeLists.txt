# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The icon a Windows program file carries, read from its resource section.
add_library(oa-data-pe-icon src/icon.cpp)
add_library(oa::data::pe_icon ALIAS oa-data-pe-icon)
target_include_directories(oa-data-pe-icon PUBLIC include)
target_compile_features(oa-data-pe-icon PUBLIC cxx_std_20)
target_link_libraries(oa-data-pe-icon PUBLIC oa-base-bytes PRIVATE oa-options)

if(BUILD_TESTING)
  add_executable(oa-data-pe-icon-test tests/icon_test.cpp)
  target_link_libraries(oa-data-pe-icon-test PRIVATE oa-data-pe-icon oa-test-support
    oa-test-game-data oa-options)
  add_test(NAME data-pe-icon COMMAND oa-data-pe-icon-test)
  oa_add_game_data_test(data-pe-icon-data oa-data-pe-icon-test --data)
endif()
