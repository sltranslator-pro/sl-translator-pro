; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 SL Translator Pro contributors

!include "MUI2.nsh"

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
Function .onInit
    StrCpy $INSTDIR ""

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
        StrCpy $INSTDIR "$PROGRAMFILES64\Firestorm-Releasex64"
    done:
FunctionEnd

Section "Install"
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
