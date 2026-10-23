@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright (C) 2026 SL Translator Pro contributors
setlocal
cd /d "%~dp0"

call build.bat
if errorlevel 1 exit /b 1

set "MAKENSIS=%ProgramFiles(x86)%\NSIS\makensis.exe"
if not exist "%MAKENSIS%" set "MAKENSIS=C:\Program Files (x86)\NSIS\makensis.exe"
if not exist "%MAKENSIS%" (
    echo ERROR: makensis.exe not found. Install NSIS 3.x.
    exit /b 1
)

"%MAKENSIS%" installer\installer.nsi
if errorlevel 1 exit /b 1

echo === INSTALLER BUILD OK ===
exit /b 0
