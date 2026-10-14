# -*- coding: utf-8 -*-
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# One installer for the four Windows packages. makensis (NSIS 3) on macOS,
# Debian or Nix:
#
#   makensis -WX \
#     -DOA_PACKAGE=windows-x64|windows-x86|windows-arm64|windows-95 \
#     -DOA_PACKAGE_DIR=/absolute/path/open-annihilation-<version>-<package> \
#     -DOA_VERSION=<version> \
#     [-DOA_ICON=/absolute/path/open-annihilation.ico] \
#     packaging/windows/open-annihilation.nsi
#
# The defines come before the script name. OA_PACKAGE_DIR is the package
# folder the suite lays out, with no trailing slash and no spaces. The
# installer is written beside that folder, as
# open-annihilation-<version>-<package>-setup.exe, which puts it beside the
# package zip. Do not pass -NOCD unless -DOA_ICON is an absolute path: the
# default icon is branding/open-annihilation.ico, relative to this script,
# and makensis reads relative paths from the script's directory.
#
# windows-95 is ANSI (Unicode false). The other three are Unicode. NSIS 3
# defines NSIS_CHAR_SIZE and 2.46 does not, so a 2.46 compile never sees the
# Unicode instruction (2.46 is ANSI already) and is refused for the other
# packages. Two things still differ if the Windows 95 installer has to be
# built with 2.46. The icon's images are PNG, which only NSIS 3 accepts, so
# pass -DOA_ICON with an icon of BMP images. And 2.46 reads both sides of a
# !if, so it would open the NSIS 3 headers included for the other packages
# (x64.nsh, WinVer.nsh) and the !if /FileExists checks. NSIS 3 does not read
# the side it skips, which is what this script relies on.

!ifndef OA_PACKAGE
  !error "Pass -DOA_PACKAGE=windows-x64, windows-x86, windows-arm64 or windows-95."
!endif
!ifndef OA_PACKAGE_DIR
  !error "Pass -DOA_PACKAGE_DIR as the absolute package folder, with no trailing slash."
!endif
!ifndef OA_VERSION
  !error "Pass -DOA_VERSION as the version string."
!endif
!if "${OA_PACKAGE_DIR}" == ""
  !error "OA_PACKAGE_DIR is empty."
!endif
!if "${OA_VERSION}" == ""
  !error "OA_VERSION is empty."
!endif

!if "${OA_PACKAGE}" == "windows-x64"
  !define OA_64BIT
  !define OA_REFUSE_32BIT
!endif
!if "${OA_PACKAGE}" == "windows-arm64"
  !define OA_64BIT
  !define OA_REFUSE_NOT_ARM64
!endif
!if "${OA_PACKAGE}" == "windows-x86"
  !define OA_REFUSE_BEFORE_XPSP3
!endif
!if "${OA_PACKAGE}" == "windows-95"
  !define OA_ANSI
!endif
!if "${OA_PACKAGE}" != "windows-x64"
  !if "${OA_PACKAGE}" != "windows-arm64"
    !if "${OA_PACKAGE}" != "windows-x86"
      !if "${OA_PACKAGE}" != "windows-95"
        !error "OA_PACKAGE must be windows-x64, windows-x86, windows-arm64 or windows-95."
      !endif
    !endif
  !endif
!endif

!ifdef NSIS_CHAR_SIZE
  !ifdef OA_ANSI
    Unicode false
  !else
    Unicode true
  !endif
!else
  !ifndef OA_ANSI
    !error "windows-x64, windows-x86 and windows-arm64 need NSIS 3."
  !endif
!endif

SetCompressor /SOLID lzma

!include MUI2.nsh
!include LogicLib.nsh
!include FileFunc.nsh
!ifdef OA_64BIT
  !include x64.nsh
!endif
!ifdef OA_REFUSE_BEFORE_XPSP3
  !include WinVer.nsh
!endif

!define OA_NAME "Open Annihilation"
!define OA_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Open Annihilation"
!ifndef OA_ICON
  !define OA_ICON "../../branding/open-annihilation.ico"
!endif
!ifdef OA_64BIT
  !define OA_DEFAULT_DIR "$PROGRAMFILES64\Open Annihilation"
!else
  !define OA_DEFAULT_DIR "$PROGRAMFILES\Open Annihilation"
!endif
!define OA_SM_GAME "$SMPROGRAMS\Open Annihilation\Open Annihilation.lnk"
!define OA_SM_UNINST "$SMPROGRAMS\Open Annihilation\Uninstall Open Annihilation.lnk"
!define OA_DESKTOP_LNK "$DESKTOP\Open Annihilation.lnk"

# The named files of every package. A missing one stops the compile.
!macro OA_Require _relative
  !if /FileExists "${OA_PACKAGE_DIR}/${_relative}"
  !else
    !error "The package is missing ${_relative}."
  !endif
!macroend
!define OA_Require "!insertmacro OA_Require"
${OA_Require} "open-annihilation.exe"
${OA_Require} "oa-intro.exe"
${OA_Require} "oa-tool.exe"
${OA_Require} "LICENSE"
${OA_Require} "ATTRIBUTIONS.md"
${OA_Require} "languages/interface.tdf"
${OA_Require} "registries/coreprime.yaml"
${OA_Require} "licenses/zlib-LICENSE.txt"

Name "${OA_NAME}"
OutFile "${OA_PACKAGE_DIR}-setup.exe"
InstallDir "${OA_DEFAULT_DIR}"
!ifndef OA_64BIT
  InstallDirRegKey HKLM "${OA_UNINST_KEY}" "InstallLocation"
!endif
RequestExecutionLevel admin
Icon "${OA_ICON}"
UninstallIcon "${OA_ICON}"
Caption "${OA_NAME} ${OA_VERSION} Setup"
UninstallCaption "${OA_NAME} Uninstall"
BrandingText "${OA_NAME} ${OA_VERSION}"

!define MUI_ICON "${OA_ICON}"
!define MUI_UNICON "${OA_ICON}"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "This installs Open Annihilation for everyone who uses this computer.$\r$\n$\r$\nYour saved games, screenshots, films, recordings and mods stay in the Open Annihilation folder in Documents. Removing the game later leaves that folder where it is."
!define MUI_COMPONENTSPAGE_TEXT_TOP "Open Annihilation is always installed. Leave the desktop shortcut ticked unless you do not want one."
!define MUI_COMPONENTSPAGE_NODESC
!define MUI_DIRECTORYPAGE_TEXT_TOP "Open Annihilation installs into this folder. A copy already installed is replaced in this folder."
!define MUI_FINISHPAGE_TEXT "Open Annihilation is installed.$\r$\n$\r$\nYour saved games stay in the Open Annihilation folder in Documents."
!define MUI_FINISHPAGE_RUN "$INSTDIR\open-annihilation.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start Open Annihilation"
!define MUI_FINISHPAGE_NOREBOOTSUPPORT

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${OA_PACKAGE_DIR}/LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Var DesktopWasInstalled

Function .onInit
  SetShellVarContext all

  !ifdef OA_REFUSE_32BIT
    ${IfNot} ${RunningX64}
      DetailPrint "This installer is for 64-bit Windows. It does not run on 32-bit Windows."
      MessageBox MB_OK|MB_ICONSTOP "This installer is for 64-bit Windows. It does not run on 32-bit Windows."
      Quit
    ${EndIf}
  !endif

  !ifdef OA_REFUSE_NOT_ARM64
    ${IfNot} ${IsNativeARM64}
      DetailPrint "This installer is for Windows on ARM. It does not run on this computer."
      MessageBox MB_OK|MB_ICONSTOP "This installer is for Windows on ARM. It does not run on this computer."
      Quit
    ${EndIf}
  !endif

  !ifdef OA_REFUSE_BEFORE_XPSP3
    ${IfNot} ${AtLeastWinXP}
      DetailPrint "This installer needs Windows XP Service Pack 3 or newer."
      MessageBox MB_OK|MB_ICONSTOP "This installer needs Windows XP Service Pack 3 or newer."
      Quit
    ${EndIf}
    ${If} ${IsWinXP}
    ${AndIfNot} ${AtLeastServicePack} 3
      DetailPrint "This installer needs Windows XP Service Pack 3 or newer."
      MessageBox MB_OK|MB_ICONSTOP "This installer needs Windows XP Service Pack 3 or newer."
      Quit
    ${EndIf}
  !endif

  !ifdef OA_64BIT
    SetRegView 64
  !endif

  StrCpy $DesktopWasInstalled "0"
  ClearErrors
  ReadRegDWORD $DesktopWasInstalled HKLM "${OA_UNINST_KEY}" "DesktopShortcut"
  ${If} ${Errors}
    StrCpy $DesktopWasInstalled "0"
  ${EndIf}
  ClearErrors

  !ifdef OA_64BIT
    ReadRegStr $0 HKLM "${OA_UNINST_KEY}" "InstallLocation"
    StrCmp $0 "" oa_keep_dir
    StrCmp $INSTDIR "${OA_DEFAULT_DIR}" 0 oa_keep_dir
      StrCpy $INSTDIR $0
    oa_keep_dir:
    ClearErrors
  !endif
FunctionEnd

Section "Open Annihilation" SecProgram
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "${OA_PACKAGE_DIR}/open-annihilation.exe"
  File "${OA_PACKAGE_DIR}/oa-intro.exe"
  File "${OA_PACKAGE_DIR}/oa-tool.exe"
  File "${OA_PACKAGE_DIR}/LICENSE"
  File "${OA_PACKAGE_DIR}/ATTRIBUTIONS.md"

  SetOutPath "$INSTDIR\licenses"
  File /r "${OA_PACKAGE_DIR}/licenses/*.*"

  SetOutPath "$INSTDIR\fonts"
  File /r "${OA_PACKAGE_DIR}/fonts/*.*"

  SetOutPath "$INSTDIR\languages"
  File "${OA_PACKAGE_DIR}/languages/interface.tdf"

  SetOutPath "$INSTDIR\registries"
  File /r "${OA_PACKAGE_DIR}/registries/*.*"

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\Open Annihilation"
  CreateShortcut "${OA_SM_GAME}" "$INSTDIR\open-annihilation.exe" "" "$INSTDIR\open-annihilation.exe" 0
  CreateShortcut "${OA_SM_UNINST}" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  DeleteRegKey HKLM "${OA_UNINST_KEY}"
  WriteRegStr HKLM "${OA_UNINST_KEY}" "DisplayName" "${OA_NAME}"
  WriteRegStr HKLM "${OA_UNINST_KEY}" "DisplayVersion" "${OA_VERSION}"
  WriteRegStr HKLM "${OA_UNINST_KEY}" "Publisher" "Open Annihilation"
  WriteRegStr HKLM "${OA_UNINST_KEY}" "DisplayIcon" "$INSTDIR\open-annihilation.exe"
  WriteRegStr HKLM "${OA_UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${OA_UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegStr HKLM "${OA_UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKLM "${OA_UNINST_KEY}" "EstimatedSize" $0
  WriteRegDWORD HKLM "${OA_UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${OA_UNINST_KEY}" "NoRepair" 1
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortcut "${OA_DESKTOP_LNK}" "$INSTDIR\open-annihilation.exe" "" "$INSTDIR\open-annihilation.exe" 0
  WriteRegDWORD HKLM "${OA_UNINST_KEY}" "DesktopShortcut" 1
SectionEnd

Function .onInstSuccess
  ${IfNot} ${SectionIsSelected} ${SecDesktop}
    StrCmp $DesktopWasInstalled "1" 0 oa_keep_desktop
      Delete "${OA_DESKTOP_LNK}"
    oa_keep_desktop:
  ${EndIf}
FunctionEnd

Function un.onInit
  SetShellVarContext all
  !ifdef OA_64BIT
    SetRegView 64
  !endif
FunctionEnd

Section "Uninstall"
  # Removes the install folder's own files, the Start Menu folder and the
  # desktop shortcut this installer added. Never Documents, and never a game
  # copy, save or mod outside the install folder.
  ClearErrors
  ReadRegDWORD $0 HKLM "${OA_UNINST_KEY}" "DesktopShortcut"
  ${IfNot} ${Errors}
    StrCmp $0 "1" 0 oa_un_no_desktop
      Delete "${OA_DESKTOP_LNK}"
    oa_un_no_desktop:
  ${EndIf}
  ClearErrors

  Delete "${OA_SM_GAME}"
  Delete "${OA_SM_UNINST}"
  RMDir "$SMPROGRAMS\Open Annihilation"

  Delete "$INSTDIR\open-annihilation.exe"
  Delete "$INSTDIR\oa-intro.exe"
  Delete "$INSTDIR\oa-tool.exe"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\ATTRIBUTIONS.md"
  Delete "$INSTDIR\languages\interface.tdf"
  RMDir "$INSTDIR\languages"
  RMDir /r "$INSTDIR\licenses"
  RMDir /r "$INSTDIR\fonts"
  RMDir /r "$INSTDIR\registries"
  Delete "$INSTDIR\Uninstall.exe"
  DeleteRegKey HKLM "${OA_UNINST_KEY}"
  RMDir "$INSTDIR"
SectionEnd
