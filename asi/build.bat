@echo off
rem Builds RelinkDeckTuner.asi with MSVC (x64). Shaders are compiled with fxc and embedded.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist build mkdir build
set FXC="C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe"
%FXC% /nologo /T ps_5_0 /E main /O3 /Fo build\easu_ps.cso easu_ps.hlsl || exit /b 1
%FXC% /nologo /T ps_5_0 /E main /O3 /Fo build\upscale_ps.cso upscale_ps.hlsl || exit /b 1
python -I embed_shaders.py || exit /b 1
cl /nologo /O2 /MT /EHsc /W4 /std:c++17 /LD RelinkDeckTuner.cpp /Fobuild\ /Febuild\RelinkDeckTuner.asi /link /NOLOGO kernel32.lib user32.lib
