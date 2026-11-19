; MirEfir Setup (NSIS). Собирается через native\windows\package.bat.
;
; Ставится поверх 1.0.24 (Electron):
;  - тот же GUID, что у electron-builder (appId com.mirefir.app) -> одна запись в «Программы и компоненты»;
;  - берёт папку из реестра Software\<GUID>\InstallLocation (HKCU, затем HKLM), т.е. туда же, где стояла старая;
;  - exe называется MirEfir.exe, как у 1.0.24: старый автоапдейтер после /S запускает именно его;
;  - старую папку программы чистит (Electron-файлы), профиль %AppData%\MirEfir НЕ трогает ни при установке,
;    ни при удалении;
;  - тихий режим /S (так зовёт автообновление), без запуска приложения: его запускает апдейтер.

Unicode true
SetCompressor /SOLID lzma

!ifndef VERSION
  !error "Укажите /DVERSION=x.y.z"
!endif
!ifndef STAGE
  !error "Укажите /DSTAGE=папка_с_MirEfir.exe_и_mpv"
!endif
!ifndef OUT
  !define OUT "MirEfir-Setup-${VERSION}.exe"
!endif

!define APP "MirEfir"
!define GUID "d3e2f0b6-17b5-58ab-9a7f-c8682488c7c4"
!define KEY_UNINST "Software\Microsoft\Windows\CurrentVersion\Uninstall\${GUID}"
!define KEY_APP "Software\${GUID}"

!include "MUI2.nsh"
!include "LogicLib.nsh"

!define MUI_ICON "..\src\mirefir.ico"
!define MUI_UNICON "..\src\mirefir.ico"

Name "${APP}"
OutFile "${OUT}"
InstallDir "$LOCALAPPDATA\Programs\${APP}"
RequestExecutionLevel highest
BrandingText "${APP} ${VERSION}"
ShowInstDetails nevershow
ShowUninstDetails nevershow

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APP}"
VIAddVersionKey "CompanyName" "${APP}"
VIAddVersionKey "LegalCopyright" "${APP}"
VIAddVersionKey "FileDescription" "${APP} Setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"

Var Mode      ; "all" (для всех, HKLM) или "user" (HKCU)
Var Existing  ; "1" если найдена прежняя установка

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN ""
!define MUI_FINISHPAGE_RUN_TEXT "$(LaunchText)"
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchApp
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipDirIfExisting
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Russian"
!insertmacro MUI_LANGUAGE "English"

LangString LaunchText ${LANG_RUSSIAN} "Запустить MirEfir"
LangString LaunchText ${LANG_ENGLISH} "Run MirEfir"
LangString NeedAdmin ${LANG_RUSSIAN} "MirEfir установлен для всех пользователей. Запустите установщик от имени администратора."
LangString NeedAdmin ${LANG_ENGLISH} "MirEfir is installed for all users. Run Setup as administrator."

; Удаляем папку программы, только если это правда папка MirEfir (защита от выбора чужой папки).
!macro CleanInstDir
  StrCpy $R2 0
  ${If} ${FileExists} "$INSTDIR\MirEfir.exe"
    StrCpy $R2 1
  ${EndIf}
  ${If} ${FileExists} "$INSTDIR\Uninstall MirEfir.exe"
    StrCpy $R2 1
  ${EndIf}
  StrLen $R1 "$INSTDIR"
  ${If} $R2 == 1
  ${AndIf} $R1 > 6
    SetOutPath "$TEMP"
    ClearErrors
    RMDir /r "$INSTDIR"
    ClearErrors
  ${EndIf}
!macroend

!macro KillApp
  nsExec::Exec '"$SYSDIR\taskkill.exe" /F /IM MirEfir.exe /T'
  Pop $R0
  Sleep 500
!macroend

!macro Firewall verb
  nsExec::Exec '"$SYSDIR\netsh.exe" advfirewall firewall delete rule name="MirEfir"'
  Pop $R0
  nsExec::Exec '"$SYSDIR\netsh.exe" advfirewall firewall delete rule name="MirEfir Out"'
  Pop $R0
  nsExec::Exec '"$SYSDIR\netsh.exe" advfirewall firewall delete rule name="MirEfir Player"'
  Pop $R0
  !if "${verb}" == "add"
    nsExec::Exec '"$SYSDIR\netsh.exe" advfirewall firewall add rule name="MirEfir" dir=in action=allow program="$INSTDIR\MirEfir.exe" enable=yes profile=any'
    Pop $R0
    nsExec::Exec '"$SYSDIR\netsh.exe" advfirewall firewall add rule name="MirEfir Out" dir=out action=allow program="$INSTDIR\MirEfir.exe" enable=yes profile=any'
    Pop $R0
    ; UDP/RTP-потоки принимает сам mpv
    nsExec::Exec '"$SYSDIR\netsh.exe" advfirewall firewall add rule name="MirEfir Player" dir=in action=allow program="$INSTDIR\mpv\mpv.exe" enable=yes profile=any'
    Pop $R0
  !endif
!macroend

Function .onInit
  SetRegView 64
  StrCpy $Mode "user"
  StrCpy $Existing "0"
  UserInfo::GetAccountType
  Pop $1
  ReadRegStr $0 HKCU "${KEY_APP}" "InstallLocation"
  ${If} $0 != ""
    StrCpy $Existing "1"
    StrCpy $INSTDIR $0
  ${Else}
    ReadRegStr $0 HKLM "${KEY_APP}" "InstallLocation"
    ${If} $0 != ""
      StrCpy $Existing "1"
      StrCpy $Mode "all"
      StrCpy $INSTDIR $0
      ${If} $1 != "Admin"
        MessageBox MB_OK|MB_ICONSTOP "$(NeedAdmin)" /SD IDOK
        Abort
      ${EndIf}
    ${ElseIf} $1 == "Admin"
      StrCpy $Mode "all"
      StrCpy $INSTDIR "$PROGRAMFILES64\${APP}"
    ${EndIf}
  ${EndIf}
  ${If} $Mode == "all"
    SetShellVarContext all
  ${Else}
    SetShellVarContext current
  ${EndIf}
FunctionEnd

Function SkipDirIfExisting
  ${If} $Existing == "1"
    Abort
  ${EndIf}
FunctionEnd

Function LaunchApp
  ; через проводник: запуск без прав администратора
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\MirEfir.exe"'
FunctionEnd

Section "Install"
  SetRegView 64
  !insertmacro KillApp
  !insertmacro CleanInstDir
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*.*"
  WriteUninstaller "$INSTDIR\Uninstall MirEfir.exe"

  ${If} $Mode == "all"
    WriteRegStr HKLM "${KEY_APP}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${KEY_APP}" "KeepShortcuts" "true"
    WriteRegStr HKLM "${KEY_APP}" "ShortcutName" "${APP}"
    WriteRegStr HKLM "${KEY_UNINST}" "DisplayName" "${APP} ${VERSION}"
    WriteRegStr HKLM "${KEY_UNINST}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${KEY_UNINST}" "DisplayIcon" "$INSTDIR\MirEfir.exe,0"
    WriteRegStr HKLM "${KEY_UNINST}" "Publisher" "${APP}"
    WriteRegStr HKLM "${KEY_UNINST}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${KEY_UNINST}" "UninstallString" '"$INSTDIR\Uninstall MirEfir.exe" /allusers'
    WriteRegStr HKLM "${KEY_UNINST}" "QuietUninstallString" '"$INSTDIR\Uninstall MirEfir.exe" /allusers /S'
    WriteRegDWORD HKLM "${KEY_UNINST}" "NoModify" 1
    WriteRegDWORD HKLM "${KEY_UNINST}" "NoRepair" 1
    SectionGetSize 0 $0
    WriteRegDWORD HKLM "${KEY_UNINST}" "EstimatedSize" $0
    !insertmacro Firewall add
  ${Else}
    WriteRegStr HKCU "${KEY_APP}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${KEY_APP}" "KeepShortcuts" "true"
    WriteRegStr HKCU "${KEY_APP}" "ShortcutName" "${APP}"
    WriteRegStr HKCU "${KEY_UNINST}" "DisplayName" "${APP} ${VERSION}"
    WriteRegStr HKCU "${KEY_UNINST}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "${KEY_UNINST}" "DisplayIcon" "$INSTDIR\MirEfir.exe,0"
    WriteRegStr HKCU "${KEY_UNINST}" "Publisher" "${APP}"
    WriteRegStr HKCU "${KEY_UNINST}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${KEY_UNINST}" "UninstallString" '"$INSTDIR\Uninstall MirEfir.exe" /currentuser'
    WriteRegStr HKCU "${KEY_UNINST}" "QuietUninstallString" '"$INSTDIR\Uninstall MirEfir.exe" /currentuser /S'
    WriteRegDWORD HKCU "${KEY_UNINST}" "NoModify" 1
    WriteRegDWORD HKCU "${KEY_UNINST}" "NoRepair" 1
    SectionGetSize 0 $0
    WriteRegDWORD HKCU "${KEY_UNINST}" "EstimatedSize" $0
  ${EndIf}

  CreateShortCut "$SMPROGRAMS\${APP}.lnk" "$INSTDIR\MirEfir.exe" "" "$INSTDIR\MirEfir.exe" 0
  ; на обновлении ярлык на рабочем столе не пересоздаём (мог быть удалён намеренно)
  ${If} $Existing == "0"
    CreateShortCut "$DESKTOP\${APP}.lnk" "$INSTDIR\MirEfir.exe" "" "$INSTDIR\MirEfir.exe" 0
  ${EndIf}
SectionEnd

Function un.onInit
  SetRegView 64
  StrCpy $Mode "all"
  ReadRegStr $0 HKCU "${KEY_APP}" "InstallLocation"
  ${If} $0 != ""
  ${AndIf} $0 == $INSTDIR
    StrCpy $Mode "user"
  ${EndIf}
  ${If} $Mode == "all"
    SetShellVarContext all
  ${Else}
    SetShellVarContext current
  ${EndIf}
FunctionEnd

Section "Uninstall"
  SetRegView 64
  !insertmacro KillApp
  Delete "$SMPROGRAMS\${APP}.lnk"
  Delete "$DESKTOP\${APP}.lnk"
  ${If} $Mode == "all"
    !insertmacro Firewall remove
    DeleteRegKey HKLM "${KEY_UNINST}"
    DeleteRegKey HKLM "${KEY_APP}"
  ${Else}
    DeleteRegKey HKCU "${KEY_UNINST}"
    DeleteRegKey HKCU "${KEY_APP}"
  ${EndIf}
  ; Только папка программы. %AppData%\MirEfir (плейлисты, гид, настройки, записи) остаётся.
  !insertmacro CleanInstDir
SectionEnd
