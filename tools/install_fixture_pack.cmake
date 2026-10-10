# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Installs the Simplified Chinese test fixture into each player folder a
# native check uses. SOURCE is the fixture pack, FONT is its face, and
# DESTINATIONS is a ;-list of player folders. A missing source or font
# fails and names the bootstrap that cuts the face. The engine never
# ships this pack.

if(NOT DEFINED SOURCE OR SOURCE STREQUAL "" OR NOT DEFINED FONT OR FONT STREQUAL "" OR
   NOT DEFINED DESTINATIONS OR DESTINATIONS STREQUAL "")
  message(FATAL_ERROR
    "install_fixture_pack: -DSOURCE=, -DFONT= and -DDESTINATIONS= are required")
endif()
if(NOT EXISTS "${SOURCE}" OR NOT EXISTS "${FONT}")
  message(FATAL_ERROR
    "run python3 tools/bootstrap_text_fonts.py, which cuts the test fixture's font")
endif()

get_filename_component(oa_fixture_font_name "${FONT}" NAME)
foreach(destination IN LISTS DESTINATIONS)
  set(pack "${destination}/Languages/zh-Hans")
  file(REMOVE_RECURSE "${pack}")
  file(MAKE_DIRECTORY "${destination}/Languages")
  file(COPY "${SOURCE}" DESTINATION "${destination}/Languages")
  file(MAKE_DIRECTORY "${pack}/fonts")
  file(COPY_FILE "${FONT}" "${pack}/fonts/${oa_fixture_font_name}")
endforeach()
