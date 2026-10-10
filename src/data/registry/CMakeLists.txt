# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Registry descriptors, the player's registry file and the rules for which
# registries a player may add. Nothing here fetches.
add_library(oa-data-registry src/descriptor.cpp src/yaml_text.cpp)
add_library(oa::data::registry ALIAS oa-data-registry)
target_include_directories(oa-data-registry PUBLIC include)
target_compile_features(oa-data-registry PUBLIC cxx_std_20)
target_link_libraries(oa-data-registry PUBLIC oa-base-signing oa-formats-url oa-options
  PRIVATE oa-formats-oamod oa-platform-shims)
