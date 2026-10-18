@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
if errorlevel 1 exit /b 1
cd /d "E:\soft\MouseInc-Rebuild\res"
rc.exe /nologo /fo MouseInc.res MouseInc.rc
if errorlevel 1 exit /b 1
cd /d "E:\soft\MouseInc-Rebuild"
cl.exe /nologo /O2 /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE /I"E:\soft\MouseInc-Rebuild\src" /I"E:\soft\MouseInc-Rebuild\res" "E:\soft\MouseInc-Rebuild\src\main.cpp" "E:\soft\MouseInc-Rebuild\res\MouseInc.res" /Fe:"E:\soft\MouseInc-Rebuild\MouseInc.exe" /link /SUBSYSTEM:WINDOWS
if errorlevel 1 exit /b 1
echo BUILD_SUCCESS
