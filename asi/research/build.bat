@echo off
setlocal enabledelayedexpansion
where cl.exe >nul 2>&1
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_DIR=%%i"
    )
    if defined VS_DIR (
        call "!VS_DIR!\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
    ) else (
        echo ERROR: cl.exe not found and Visual Studio x64 tools could not be located.
        exit /b 1
    )
)
cd /d "%~dp0"
if not exist ..\build mkdir ..\build
cl /nologo /O2 /MT /EHsc /W3 /LD lodtrace.cpp /Fo..\build\ /Fe..\build\lodtrace.asi /link /NOLOGO
cl /nologo /O2 /MT /EHsc /W3 /LD opttrace.cpp /Fo..\build\ /Fe..\build\opttrace.asi /link /NOLOGO
