@echo off
rem Builds RelinkDeckTuner.asi with MSVC (x64).
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /O2 /MT /EHsc /W4 /std:c++17 /LD RelinkDeckTuner.cpp /Fobuild\ /Febuild\RelinkDeckTuner.asi /link /NOLOGO kernel32.lib
