# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Copies the registries the game ships with into the registries folder of a
# directory after a target is built. The folder is the one
# read_builtin_registries reads beside the game.
#   oa_copy_registries(<target> <directory>)
# The directory may be a generator expression, such as the game's
# OA_GAME_FILES_DIR.

function(oa_copy_registries target directory)
  file(GLOB sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/registries/*.yaml")
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${directory}/registries"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${sources} "${directory}/registries"
    VERBATIM)
endfunction()
