# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The Library's model: every package the player can get or has installed,
# built from plain inputs, with its tabs, filters, search, selection, details
# and download queue lines, and its words (library.hpp, text.hpp). It draws
# nothing and reads no file, clock or network.
add_library(oa-ui-library src/library.cpp src/search.cpp src/text.cpp)
add_library(oa::ui::library ALIAS oa-ui-library)
target_include_directories(oa-ui-library PUBLIC include)
target_compile_features(oa-ui-library PUBLIC cxx_std_20)
target_link_libraries(oa-ui-library PUBLIC oa-formats-oamod oa-base-sha256 PRIVATE oa-data-mod-profile
  oa-options)

