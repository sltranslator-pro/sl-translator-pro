@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright (C) 2026 SL Translator Pro contributors
rem SL Translator Pro - build script
rem Builds dist\dinput8.dll with the MSVC x64 toolchain.
rem
rem Run from a "x64 Native Tools Command Prompt for VS 2022", or let this
rem script locate Visual Studio via vswhere.
setlocal EnableDelayedExpansion
cd /d "%~dp0"

where cl.exe >nul 2>nul
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo ERROR: cl.exe not found and vswhere.exe is missing. Install Visual Studio 2022 with C++ tools.
        exit /b 1
    )
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
    if not defined VSDIR (
        echo ERROR: Visual Studio C++ x64 tools not found.
        exit /b 1
    )
    call "!VSDIR!\VC\Auxiliary\Build\vcvars64.bat" >nul
)

if not exist build mkdir build
if not exist dist mkdir dist

echo Compiling MinHook...
cl.exe /nologo /c /O2 /MT /DNDEBUG /I"third_party\minhook\include" /Fo"build\\" ^
    third_party\minhook\src\hook.c ^
    third_party\minhook\src\buffer.c ^
    third_party\minhook\src\trampoline.c ^
    third_party\minhook\src\hde\hde64.c
if errorlevel 1 goto :fail

echo Compiling version resource...
rc.exe /nologo /fo"build\version.res" src\version.rc
if errorlevel 1 goto :fail

echo Compiling dinput8.dll...
cl.exe /nologo /EHsc /O2 /MT /DNDEBUG /I"third_party\minhook\include" /LD /Fo"build\\" /Fe"dist\dinput8.dll" ^
    src\dinput8.cpp build\hook.obj build\buffer.obj build\trampoline.obj build\hde64.obj build\version.res ^
    /link /DLL /IMPLIB:build\dinput8.lib user32.lib winhttp.lib shell32.lib kernel32.lib gdi32.lib ws2_32.lib advapi32.lib ole32.lib
if errorlevel 1 goto :fail

echo === BUILD OK: dist\dinput8.dll ===
exit /b 0

:fail
echo === BUILD FAILED ===
exit /b 1
