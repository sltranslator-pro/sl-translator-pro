!include "MUI2.nsh"

; General
Name "SL Translator Pro"
OutFile "..\SLTranslatorPro_Setup.exe"
InstallDir ""
RequestExecutionLevel admin
Unicode True

; UI
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "SL Translator Pro Setup"
!define MUI_WELCOMEPAGE_TEXT "This will install SL Translator Pro for Firestorm Viewer.$\r$\n$\r$\nThe installer will automatically find your Firestorm installation.$\r$\n$\r$\nClick Next to continue."
!define MUI_FINISHPAGE_TITLE "Installation Complete"
!define MUI_FINISHPAGE_TEXT "SL Translator Pro has been installed successfully.$\r$\n$\r$\nStart Firestorm to activate your license.$\r$\n$\r$\nChat commands:$\r$\n  /tr en - Translate to English$\r$\n  /tr de - Translate to German$\r$\n  Ctrl+Shift+T - Settings"

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Language
!insertmacro MUI_LANGUAGE "English"

; Find Firestorm
Function .onInit
    ; Try common paths
    StrCpy $INSTDIR ""

    ; Check Program Files
    IfFileExists "$PROGRAMFILES64\Firestorm-Releasex64\firestorm-bin.exe" 0 +2
        StrCpy $INSTDIR "$PROGRAMFILES64\Firestorm-Releasex64"

    IfFileExists "$PROGRAMFILES\Firestorm-Releasex64\firestorm-bin.exe" 0 +2
        StrCpy $INSTDIR "$PROGRAMFILES\Firestorm-Releasex64"

    IfFileExists "$PROGRAMFILES64\Firestorm\firestorm-bin.exe" 0 +2
        StrCpy $INSTDIR "$PROGRAMFILES64\Firestorm"

    IfFileExists "$PROGRAMFILES\Firestorm\firestorm-bin.exe" 0 +2
        StrCpy $INSTDIR "$PROGRAMFILES\Firestorm"

    ; Check C:\Firestorm
    IfFileExists "C:\Firestorm-Releasex64\firestorm-bin.exe" 0 +2
        StrCpy $INSTDIR "C:\Firestorm-Releasex64"

    StrCmp $INSTDIR "" 0 done
        StrCpy $INSTDIR "$PROGRAMFILES64\Firestorm-Releasex64"
    done:
FunctionEnd

Section "Install"
    SetOutPath $INSTDIR

    ; Check Firestorm exists
    IfFileExists "$INSTDIR\firestorm-bin.exe" found notfound
    notfound:
        MessageBox MB_YESNO|MB_ICONQUESTION "Firestorm was not found in:$\r$\n$INSTDIR$\r$\n$\r$\nContinue anyway?" IDYES found
        Abort
    found:

    ; Backup existing dinput8.dll if it exists
    IfFileExists "$INSTDIR\dinput8.dll" 0 +2
        Rename "$INSTDIR\dinput8.dll" "$INSTDIR\dinput8.dll.backup"

    ; Install DLL
    File "dinput8.dll"

    ; Create AppData folder
    CreateDirectory "$APPDATA\SLTranslator"

    ; Create uninstaller
    WriteUninstaller "$INSTDIR\SLTranslator_Uninstall.exe"

    ; Add to Add/Remove Programs
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLTranslatorPro" "DisplayName" "SL Translator Pro"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLTranslatorPro" "UninstallString" "$INSTDIR\SLTranslator_Uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLTranslatorPro" "Publisher" "SL Translator"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLTranslatorPro" "DisplayVersion" "1.0.0"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLTranslatorPro" "EstimatedSize" 200
SectionEnd

Section "Uninstall"
    ; Remove DLL
    Delete "$INSTDIR\dinput8.dll"
    Delete "$INSTDIR\SLTranslator_Uninstall.exe"

    ; Restore backup if exists
    IfFileExists "$INSTDIR\dinput8.dll.backup" 0 +2
        Rename "$INSTDIR\dinput8.dll.backup" "$INSTDIR\dinput8.dll"

    ; Remove config
    RMDir /r "$APPDATA\SLTranslator"

    ; Remove from Add/Remove Programs
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLTranslatorPro"
SectionEnd
