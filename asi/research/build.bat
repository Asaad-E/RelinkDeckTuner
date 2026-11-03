@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist ..\build mkdir ..\build
cl /nologo /O2 /MT /EHsc /W3 /LD lodtrace.cpp /Fo..\build\ /Fe..\build\lodtrace.asi /link /NOLOGO
cl /nologo /O2 /MT /EHsc /W3 /LD opttrace.cpp /Fo..\build\ /Fe..\build\opttrace.asi /link /NOLOGO
