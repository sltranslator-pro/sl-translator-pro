; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 SL Translator Pro contributors

!include "MUI2.nsh"
!include "LogicLib.nsh"

; Version
!define VERSION "1.5.1"

; General
Name "SL Translator Pro v${VERSION}"
OutFile "..\dist\SLTranslatorPro_Setup_v${VERSION}.exe"
InstallDir ""
RequestExecutionLevel admin
Unicode True

; UI
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "SL Translator Pro v${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "This will install SL Translator Pro v${VERSION} for Firestorm Viewer.$\r$\n$\r$\nThe installer will automatically find your Firestorm installation.$\r$\n$\r$\nClick Next to continue."

; License page (Privacy Policy)
!define MUI_LICENSEPAGE_TEXT_TOP "Please review our Privacy Policy and Terms of Service."
!define MUI_LICENSEPAGE_TEXT_BOTTOM "Click I Agree to accept and continue with the installation."
!define MUI_LICENSEPAGE_BUTTON "I Agree"

!define MUI_FINISHPAGE_TITLE "Installation Complete - v${VERSION}"
!define MUI_FINISHPAGE_TEXT "SL Translator Pro v${VERSION} has been installed successfully.$\r$\n$\r$\nOpen Firestorm and use the SL Translator menu to configure.$\r$\n$\r$\n1. Click SL Translator > Settings (or Ctrl+Shift+T)$\r$\n2. Enter your License Key$\r$\n3. Click Verify$\r$\n4. Select your language$\r$\n5. Click OK and restart Firestorm"

; Pages
!define MUI_DIRECTORYPAGE_TEXT_TOP "Select your Firestorm installation folder (the folder that contains Firestorm-Releasex64.exe).$\r$\n$\r$\nIf the box is empty, Firestorm could not be found automatically: click Browse and choose it. Tip: right-click your Firestorm shortcut > Open file location."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\PRIVACY.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Language
!insertmacro MUI_LANGUAGE "English"

; Version info
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "SL Translator Pro"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "SL Translator Pro - Real-time chat translation for Second Life"
VIAddVersionKey "LegalCopyright" "Copyright (C) 2026 SL Translator Pro contributors. GPL-3.0-or-later."
VIAddVersionKey "CompanyName" "SL Translator"

; Find Firestorm
; Firestorm's own installer records its folder in the registry under
; SOFTWARE\The Phoenix Firestorm Project\<channel> (default value = folder).
; Reading it first finds custom locations (D:\, renamed folders, beta
; channels) that the hard-coded list below misses. Before this, a user
; with Firestorm outside the list got everything installed into an empty
; default folder and kept seeing Firestorm's stock translation window.
; Scans one root key in the current registry view. Result in $R9.
!macro ScanFirestormKey ROOT
    ${If} $R9 == ""
        StrCpy $R0 0
        ${Do}
            EnumRegKey $R1 ${ROOT} "SOFTWARE\The Phoenix Firestorm Project" $R0
            ${If} $R1 == ""
                ${Break}
            ${EndIf}
            ReadRegStr $R2 ${ROOT} "SOFTWARE\The Phoenix Firestorm Project\$R1" ""
            ReadRegStr $R3 ${ROOT} "SOFTWARE\The Phoenix Firestorm Project\$R1" "Exe"
            ${If} $R2 != ""
            ${AndIf} $R3 != ""
            ${AndIf} ${FileExists} "$R2\$R3"
            ${AndIf} ${FileExists} "$R2\skins\default\xui\en\*.*"
                StrCpy $R9 $R2
                ${Break}
            ${EndIf}
            IntOp $R0 $R0 + 1
        ${Loop}
    ${EndIf}
!macroend

; Firestorm writes its key from a 32-bit NSIS installer, so depending on the
; version it lands in the 64-bit view or in WOW6432Node. Check both views,
; machine-wide and per-user.
Function FindFirestormInRegistry
    StrCpy $R9 ""
    SetRegView 64
    !insertmacro ScanFirestormKey HKLM
    !insertmacro ScanFirestormKey HKCU
    SetRegView 32
    !insertmacro ScanFirestormKey HKLM
    !insertmacro ScanFirestormKey HKCU
    SetRegView default
FunctionEnd

; Last-resort detection for portable / copied installs that never wrote a
; registry key: the folder of a running Firestorm, then the target of any
; Firestorm shortcut on the desktops or in the Start menus. Result in $R9.
Function FindFirestormFromShortcuts
    StrCpy $R9 ""
    nsExec::ExecToStack `powershell -NoProfile -ExecutionPolicy Bypass -Command "$$ok={param($$d) $$d -and (Test-Path (Join-Path $$d 'skins\default\xui\en'))}; $$p=Get-Process Firestorm* -EA SilentlyContinue | ?{ $$_.Path } | %{ Split-Path $$_.Path } | ?{ & $$ok $$_ } | select -First 1; if(-not $$p){ $$sh=New-Object -ComObject WScript.Shell; $$dirs=@([Environment]::GetFolderPath('Desktop'),[Environment]::GetFolderPath('CommonDesktopDirectory'),[Environment]::GetFolderPath('StartMenu'),[Environment]::GetFolderPath('CommonStartMenu')); $$p=Get-ChildItem $$dirs -Recurse -Filter *.lnk -EA SilentlyContinue | %{ $$sh.CreateShortcut($$_.FullName).TargetPath } | ?{ $$_ -match 'Firestorm[^\\]*\.exe$$' } | %{ Split-Path $$_ } | ?{ & $$ok $$_ } | select -First 1 }; [Console]::Write($$p)"`
    Pop $0
    Pop $1
    ${If} $0 == 0
    ${AndIf} $1 != ""
    ${AndIf} ${FileExists} "$1\skins\default\xui\en\*.*"
        StrCpy $R9 $1
    ${EndIf}
FunctionEnd

; Final fallback: bounded file search for dinput8.dll / Firestorm*.exe in
; the places a viewer realistically lives (drive roots, Program Files,
; Games, user profile), shallow depth, hard 15 s cap. A full-disk search
; was tried and takes minutes, so it is deliberately not used.
; Prefers a folder that already holds our dinput8.dll (previous install).
Function FindFirestormByScan
    StrCpy $R9 ""
    InitPluginsDir
    File "/oname=$PLUGINSDIR\find_firestorm.ps1" "find_firestorm.ps1"
    nsExec::ExecToStack 'powershell -NoProfile -ExecutionPolicy Bypass -File "$PLUGINSDIR\find_firestorm.ps1"'
    Pop $0
    Pop $1
    ${If} $0 == 0
    ${AndIf} $1 != ""
    ${AndIf} ${FileExists} "$1\skins\default\xui\en\*.*"
        StrCpy $R9 $1
    ${EndIf}
FunctionEnd

Function .onInit
    StrCpy $INSTDIR ""

    Call FindFirestormInRegistry
    ${If} $R9 == ""
        Call FindFirestormFromShortcuts
    ${EndIf}
    ${If} $R9 == ""
        Call FindFirestormByScan
    ${EndIf}
    ${If} $R9 != ""
        StrCpy $INSTDIR $R9
        Goto done
    ${EndIf}

    IfFileExists "$PROGRAMFILES64\Firestorm-Releasex64\*.exe" 0 +2
        StrCpy $INSTDIR "$PROGRAMFILES64\Firestorm-Releasex64"
    IfFileExists "$PROGRAMFILES64\Firestorm-Release\*.exe" 0 +2
        StrCpy $INSTDIR "$PROGRAMFILES64\Firestorm-Release"
    IfFileExists "$PROGRAMFILES64\Firestorm\*.exe" 0 +2
        StrCpy $INSTDIR "$PROGRAMFILES64\Firestorm"
    IfFileExists "$PROGRAMFILES64\FirestormOS-Releasex64\*.exe" 0 +2
        StrCpy $INSTDIR "$PROGRAMFILES64\FirestormOS-Releasex64"

    IfFileExists "$PROGRAMFILES\Firestorm-Releasex64\*.exe" 0 +2
        StrCpy $INSTDIR "$PROGRAMFILES\Firestorm-Releasex64"
    IfFileExists "$PROGRAMFILES\Firestorm-Release\*.exe" 0 +2
        StrCpy $INSTDIR "$PROGRAMFILES\Firestorm-Release"
    IfFileExists "$PROGRAMFILES\Firestorm\*.exe" 0 +2
        StrCpy $INSTDIR "$PROGRAMFILES\Firestorm"

    IfFileExists "C:\Firestorm-Releasex64\*.exe" 0 +2
        StrCpy $INSTDIR "C:\Firestorm-Releasex64"
    IfFileExists "C:\Firestorm-Release\*.exe" 0 +2
        StrCpy $INSTDIR "C:\Firestorm-Release"
    IfFileExists "C:\Firestorm\*.exe" 0 +2
        StrCpy $INSTDIR "C:\Firestorm"

    StrCmp $INSTDIR "" 0 done
        ; Nothing found: leave empty so the user must browse to the folder.
        ; .onVerifyInstDir keeps Next disabled until a real Firestorm
        ; folder is chosen.
        StrCpy $INSTDIR ""
    done:
FunctionEnd

; Refuse to install into a folder that is not a Firestorm installation.
; The old behaviour silently created an empty folder and reported success.
Function .onVerifyInstDir
    IfFileExists "$INSTDIR\skins\default\xui\en\*.*" +2 0
        Abort
FunctionEnd

Section "Install"
    ; Firestorm keeps the XUI files open while running; replacing them
    ; then appears to work but the running viewer never reloads them.
    ${Do}
        nsExec::ExecToStack 'cmd /c tasklist /FI "IMAGENAME eq Firestorm*" | find /I "Firestorm"'
        Pop $0
        Pop $1
        ${If} $0 != 0
            ${Break}
        ${EndIf}
        MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Firestorm is running. Please close Firestorm completely, then click Retry." IDRETRY retry_fs
        Abort
        retry_fs:
    ${Loop}

    SetOutPath $INSTDIR

    ; Backup existing dinput8.dll if it exists (and not our own)
    IfFileExists "$INSTDIR\dinput8.dll" 0 +3
    IfFileExists "$INSTDIR\dinput8.dll.backup" +2 0
        Rename "$INSTDIR\dinput8.dll" "$INSTDIR\dinput8.dll.backup"

    ; Install DLL
    File "..\dist\dinput8.dll"

    ; Install floater XML (complete replacement) into every Firestorm
    ; localization folder that exists on this machine, not just "en".
    ; Firestorm layers xui/<lang>/<file>.xml on top of xui/en/<file>.xml
    ; (LLXMLNode::getLayeredXMLNode -> updateNode) when the viewer's UI
    ; language isn't English, so a non-English user (e.g. German) was
    ; getting our English override silently overwritten by Firestorm's
    ; own untouched xui/de/floater_translation_settings.xml: "License Key"
    ; label became "Azure-Key" and the field stayed locked/tentative.
    ; Fixed by writing our file into every language folder we find so
    ; there is nothing left for Firestorm to layer on top of (10 Eki).
    SetOutPath "$INSTDIR\skins\default\xui\en"
    File "skins\floater_translation_settings.xml"
    File "skins\panel_preferences_sltranslator.xml"

    !macro InstallXuiLang LANG
        IfFileExists "$INSTDIR\skins\default\xui\${LANG}" 0 +4
            SetOutPath "$INSTDIR\skins\default\xui\${LANG}"
            File "skins\floater_translation_settings.xml"
            File "skins\panel_preferences_sltranslator.xml"
    !macroend
    !insertmacro InstallXuiLang "de"
    !insertmacro InstallXuiLang "fr"
    !insertmacro InstallXuiLang "es"
    !insertmacro InstallXuiLang "it"
    !insertmacro InstallXuiLang "pt"
    !insertmacro InstallXuiLang "ru"
    !insertmacro InstallXuiLang "tr"
    !insertmacro InstallXuiLang "pl"
    !insertmacro InstallXuiLang "ja"
    !insertmacro InstallXuiLang "zh"
    !insertmacro InstallXuiLang "az"
    !insertmacro InstallXuiLang "da"

    ; Install license, privacy and source-code notices with the binary.
    SetOutPath "$INSTDIR\SLTranslatorPro"
    File "..\LICENSE"
    File "..\PRIVACY.md"
    File "..\THIRD_PARTY_NOTICES.md"
    File "..\SOURCE.txt"

    ; Install and run patch script
    SetOutPath "$INSTDIR"
    File "patch.ps1"
    nsExec::ExecToStack 'powershell -ExecutionPolicy Bypass -File "$INSTDIR\patch.ps1" -InstDir "$INSTDIR"'
    Pop $0
    Delete "$INSTDIR\patch.ps1"

    ; Verify the translation window really is ours now.
    nsExec::ExecToStack 'cmd /c findstr /C:"SL Translator Pro" "$INSTDIR\skins\default\xui\en\floater_translation_settings.xml"'
    Pop $0
    ${If} $0 != 0
        MessageBox MB_OK|MB_ICONSTOP "SL Translator Pro could not replace the Firestorm translation window in:$\r$\n$INSTDIR$\r$\n$\r$\nPlease close Firestorm and run the installer again as administrator."
    ${EndIf}

    ; Create uninstaller
    WriteUninstaller "$INSTDIR\SLTranslator_Uninstall.exe"

    ; Add to Add/Remove Programs
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLTranslatorPro" "DisplayName" "SL Translator Pro"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLTranslatorPro" "UninstallString" "$INSTDIR\SLTranslator_Uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLTranslatorPro" "Publisher" "SL Translator"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLTranslatorPro" "DisplayVersion" "${VERSION}"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLTranslatorPro" "EstimatedSize" 400
SectionEnd

Section "Uninstall"
    ; Remove DLL
    Delete "$INSTDIR\dinput8.dll"
    Delete "$INSTDIR\SLTranslator_Uninstall.exe"
    Delete "$INSTDIR\SLTranslatorPro\LICENSE"
    Delete "$INSTDIR\SLTranslatorPro\PRIVACY.md"
    Delete "$INSTDIR\SLTranslatorPro\THIRD_PARTY_NOTICES.md"
    Delete "$INSTDIR\SLTranslatorPro\SOURCE.txt"
    RMDir "$INSTDIR\SLTranslatorPro"

    ; Restore backup if exists
    IfFileExists "$INSTDIR\dinput8.dll.backup" 0 +2
        Rename "$INSTDIR\dinput8.dll.backup" "$INSTDIR\dinput8.dll"

    ; Remove from Add/Remove Programs
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLTranslatorPro"
SectionEnd
