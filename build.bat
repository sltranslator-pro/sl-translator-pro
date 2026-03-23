@echo off
set "MSVC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.43.34808"
set "WINSDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.22621.0"
set "PATH=%MSVC%\bin\Hostx64\x64;%PATH%"
set "INCLUDE=%MSVC%\include;%WINSDK%\Include\%SDKVER%\ucrt;%WINSDK%\Include\%SDKVER%\um;%WINSDK%\Include\%SDKVER%\shared"
set "LIB=%MSVC%\lib\x64;%WINSDK%\Lib\%SDKVER%\ucrt\x64;%WINSDK%\Lib\%SDKVER%\um\x64"

cd /d "%~dp0"

echo Compiling MinHook...
cl.exe /c /EHsc /O2 /MT /DNDEBUG /I"minhook\include" minhook\src\hook.c minhook\src\buffer.c minhook\src\trampoline.c minhook\src\hde\hde64.c

echo.
echo Compiling dinput8.dll...
cl.exe /EHsc /O2 /MT /DNDEBUG /I"minhook\include" /LD /Fe:dinput8.dll version.cpp hook.obj buffer.obj trampoline.obj hde64.obj /link /DLL user32.lib winhttp.lib shell32.lib kernel32.lib gdi32.lib

echo.
if exist dinput8.dll (
    echo === BUILD OK ===
    dir dinput8.dll
) else (
    echo === BUILD FAILED ===
)

del *.obj 2>nul
pause
