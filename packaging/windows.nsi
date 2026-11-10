Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "Sections.nsh"
!define REWORD_UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Reword"
Name "Reword"
OutFile "${ROOT}\dist\Reword-Setup-0.1.0.exe"
InstallDir "$LOCALAPPDATA\Programs\Reword"
InstallDirRegKey HKCU "${REWORD_UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel user
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
Section "Reword" REWORD_CORE
 SectionIn RO
 SetOutPath "$INSTDIR"
 File /r "${ROOT}\dist\windows\*"
 WriteUninstaller "$INSTDIR\Uninstall.exe"
 CreateDirectory "$SMPROGRAMS\Reword"
 CreateShortcut "$SMPROGRAMS\Reword\Reword.lnk" "$INSTDIR\reword.exe"
 CreateShortcut "$SMPROGRAMS\Reword\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
 WriteRegStr HKCU "${REWORD_UNINSTALL_KEY}" "DisplayName" "Reword"
 WriteRegStr HKCU "${REWORD_UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
 WriteRegStr HKCU "${REWORD_UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
SectionEnd

!macro RegisterBrowser browser label
 nsExec::Exec /TIMEOUT=5000 '"$INSTDIR\reword-browser-host.exe" --register-browser ${browser}'
 Pop $0
 ${If} $0 != "0"
  SetErrorLevel 2
  MessageBox MB_ICONEXCLAMATION|MB_OK "Reword was installed. The ${label} connection could not be registered." /SD IDOK
 ${EndIf}
!macroend

Section /o "Register Chrome connection" REWORD_CHROME
 !insertmacro RegisterBrowser chrome Chrome
SectionEnd

Section /o "Register Chromium connection" REWORD_CHROMIUM
 !insertmacro RegisterBrowser chromium Chromium
SectionEnd

Function .onInit
 ${GetParameters} $R0
 ${GetOptions} $R0 "/ConnectChrome" $R1
 ${IfNot} ${Errors}
  !insertmacro SelectSection ${REWORD_CHROME}
 ${EndIf}
 ${GetOptions} $R0 "/ConnectChromium" $R1
 ${IfNot} ${Errors}
  !insertmacro SelectSection ${REWORD_CHROMIUM}
 ${EndIf}
 ClearErrors
FunctionEnd

Section "Uninstall"
 IfFileExists "$INSTDIR\reword-browser-host.exe" 0 browser_cleanup_done
 nsExec::Exec /TIMEOUT=5000 '"$INSTDIR\reword-browser-host.exe" --unregister-installed-browser chrome'
 Pop $0
 nsExec::Exec /TIMEOUT=5000 '"$INSTDIR\reword-browser-host.exe" --unregister-installed-browser chromium'
 Pop $0
 browser_cleanup_done:
 !include "${ROOT}\dist\uninstall-files.nsh"
 Delete "$INSTDIR\Uninstall.exe"
 RMDir "$INSTDIR"
 ReadRegStr $0 HKCU "${REWORD_UNINSTALL_KEY}" "InstallLocation"
 StrCmp $0 "$INSTDIR" 0 shared_cleanup_done
 Delete "$SMPROGRAMS\Reword\Reword.lnk"
 Delete "$SMPROGRAMS\Reword\Uninstall.lnk"
 RMDir "$SMPROGRAMS\Reword"
 DeleteRegKey HKCU "${REWORD_UNINSTALL_KEY}"
 shared_cleanup_done:
 ; User history is intentionally retained; deletion is available inside Privacy.
SectionEnd
