# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# A Windows build of the engine: the same source, cross-compiled.
#
# The toolchain and the C libraries come from the cross set the caller passes,
# so a build for an older Windows can be given a set built against the C
# run-time library that Windows ships, and one for a current Windows the
# default. zlib and FreeType are that set's own; only SDL3 is built here
# (tools/nix/windows-sdl.nix), for its patch and its static library.
#
# What makes the build a Windows one is cmake/OaWindows95.cmake or
# cmake/OaWindowsXp.cmake, chosen by the definitions passed in; the toolchain
# file named below is the one that turns those on for the compiler.
#
# Takes the text fonts, because the engine reads them beside the executable the
# same way it does everywhere else.
#
# fullPackage is the Windows 95 release package. The other Windows packages
# are laid out by the suite; this output is what the suite copies for Windows
# 95, from $out/bin, so the files there are the package root. It adds the
# notices, the interface catalogue and the intro player to the executables,
# fonts and registries every Windows build here installs. oa-intro is the
# player the game already links, built with the same toolchain.
#
# @param toolchain the file in cmake/toolchains that names the target
# @param definitions the -D flags that pick the platform and the float mode
# @param pname the derivation's name, which says which Windows it is for
# @param description the same, in words
# @param fullPackage install the release package, not only the executables
{ lib, stdenv, cmake, ninja, zlib, freetype, sdl3, src, textFonts, version
, toolchain, definitions, pname, description, fullPackage ? false }:
{
  game = stdenv.mkDerivation {
    inherit pname version src;

    nativeBuildInputs = [
      cmake
      ninja
    ];

    # The cmake setup hook puts the host inputs' prefixes on CMAKE_PREFIX_PATH,
    # so nothing has to name them on the command line.
    buildInputs = [
      zlib
      freetype
      sdl3
    ];

    cmakeFlags = [
      "-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/${toolchain}"
      "-DOA_TEXT_FONTS_DIR=${textFonts}"
      # Nothing here runs on the build machine, so no test may be configured.
      "-DBUILD_TESTING=OFF"
    ] ++ definitions;

    buildFlags = [
      "oa-game"
      "oa-tool"
    ] ++ lib.optionals fullPackage [
      "oa-intro"
    ];

    # There is no CMake install configured for this target either.
    installPhase = ''
      runHook preInstall

      mkdir -p $out/bin/fonts
      install -Dm755 oa-tool.exe $out/bin/oa-tool.exe
      install -Dm755 open-annihilation.exe $out/bin/open-annihilation.exe
      # bundled_font_directory() reads the fonts from the executable's folder
      # (src/platform/text-font/src/font_directory.cpp).
      install -Dm644 -t $out/bin/fonts ${textFonts}/*.ttf ${textFonts}/*.otf
      # The registries the game ships with, read from the executable's folder.
      install -Dm644 -t $out/bin/registries ${src}/registries/*.yaml
    '' + lib.optionalString fullPackage ''
      install -Dm755 oa-intro.exe $out/bin/oa-intro.exe
      install -Dm644 -t $out/bin ${src}/LICENSE ${src}/ATTRIBUTIONS.md
      install -Dm644 -t $out/bin/licenses ${src}/licenses/*
      install -Dm644 ${src}/languages/interface.tdf $out/bin/languages/interface.tdf
    '' + ''
      runHook postInstall
    '';

    meta = with lib; {
      inherit description;
      homepage = "https://coreprime.net/";
      # nixpkgs compares this against the system the derivation is built for,
      # which is Windows: this is a cross build whose host platform is the
      # target, however much the build machine is a Linux one.
      platforms = platforms.windows;
      license = licenses.gpl3Plus;
      mainProgram = "open-annihilation.exe";
    };
  };
}
