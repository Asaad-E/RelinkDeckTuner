@echo off
setlocal enabledelayedexpansion
rem Builds RelinkDeckTuner.asi with MSVC (x64). Shaders are compiled with fxc and embedded.
cd /d "%~dp0"

where cl.exe >nul 2>&1
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            set "VS_DIR=%%i"
        )
    )
    if defined VS_DIR (
        call "!VS_DIR!\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
    ) else (
        echo ERROR: cl.exe not found and Visual Studio with C++ x64 tools could not be located via vswhere.exe.
        exit /b 1
    )
)

set "FXC="
where fxc.exe >nul 2>&1
if not errorlevel 1 (
    for /f "delims=" %%f in ('where fxc.exe') do set "FXC=%%f"
)
if not defined FXC if defined WindowsSdkDir if defined WindowsSDKVersion (
    set "SDK_VER=%WindowsSDKVersion:\=%"
    if exist "%WindowsSdkDir%bin\%SDK_VER%\x64\fxc.exe" set "FXC=%WindowsSdkDir%bin\%SDK_VER%\x64\fxc.exe"
)
if not defined FXC (
    for /f "delims=" %%d in ('dir /b /ad /o-n "%ProgramFiles(x86)%\Windows Kits\10\bin\10.*" 2^>nul') do (
        if not defined FXC if exist "%ProgramFiles(x86)%\Windows Kits\10\bin\%%d\x64\fxc.exe" (
            set "FXC=%ProgramFiles(x86)%\Windows Kits\10\bin\%%d\x64\fxc.exe"
        )
    )
)
if not defined FXC (
    echo ERROR: fxc.exe not found. Install the Windows 10/11 SDK or run from a Visual Studio Developer Command Prompt.
    exit /b 1
)

if not exist build mkdir build
"%FXC%" /nologo /T ps_5_0 /E main /O3 /Fo build\upscale_ps.cso upscale_ps.hlsl || exit /b 1
"%FXC%" /nologo /T ps_5_0 /E main /O3 /Fo build\fsr_ps.cso fsr_ps.hlsl || exit /b 1
"%FXC%" /nologo /T ps_5_0 /E main /O3 /Fo build\easu_ps.cso easu_ps.hlsl || exit /b 1
"%FXC%" /nologo /T ps_5_0 /E main /O3 /Fo build\rcas_ps.cso rcas_ps.hlsl || exit /b 1
"%FXC%" /nologo /T vs_5_0 /E main /O3 /Fo build\fs_vs.cso fs_vs.hlsl || exit /b 1
python -I embed_shaders.py || exit /b 1
cl /nologo /O2 /MT /EHsc /W4 /std:c++17 /LD RelinkDeckTuner.cpp /Fobuild\ /Febuild\RelinkDeckTuner.asi /link /NOLOGO kernel32.lib user32.lib
