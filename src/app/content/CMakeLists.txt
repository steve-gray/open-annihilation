# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Registries in effect, their cached catalogues and the worker that refreshes
# them. No SDL.
add_library(oa-app-content src/service.cpp src/worker.cpp src/refresh.cpp src/cache.cpp)
add_library(oa::app::content ALIAS oa-app-content)
target_include_directories(oa-app-content PUBLIC include)
target_include_directories(oa-app-content PRIVATE src)
target_compile_features(oa-app-content PUBLIC cxx_std_20)
target_link_libraries(oa-app-content
  PUBLIC oa-data-registry oa-data-catalogue oa-netgame-http oa-formats-url oa-options
  PRIVATE oa-formats-oamod oa-platform-shims oa-base-sha256 oa-base-threads)
