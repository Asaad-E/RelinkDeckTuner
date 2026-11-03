@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist ..\build mkdir ..\build
cl /nologo /O2 /EHsc /W3 upscale_test.cpp /Fo..\build\ /Fe..\build\upscale_test.exe
