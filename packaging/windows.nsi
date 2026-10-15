Unicode true
!include "MUI2.nsh"
Name "Reword"
OutFile "${ROOT}\dist\Reword-Setup-0.1.0.exe"
InstallDir "$LOCALAPPDATA\Programs\Reword"
RequestExecutionLevel user
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
Section "Reword"
 SetOutPath "$INSTDIR"
 File /r "${ROOT}\dist\windows\*"
 WriteUninstaller "$INSTDIR\Uninstall.exe"
 CreateDirectory "$SMPROGRAMS\Reword"
 CreateShortcut "$SMPROGRAMS\Reword\Reword.lnk" "$INSTDIR\reword.exe"
 CreateShortcut "$SMPROGRAMS\Reword\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Reword" "DisplayName" "Reword"
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Reword" "UninstallString" '"$INSTDIR\Uninstall.exe"'
SectionEnd
Section "Uninstall"
 RMDir /r "$INSTDIR"
 RMDir /r "$SMPROGRAMS\Reword"
 DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Reword"
 ; User history is intentionally retained; deletion is available inside Privacy.
SectionEnd
