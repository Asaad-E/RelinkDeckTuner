@echo off
setlocal
rem Restore the factory game (Windows): put back data.i.orig and remove the mod files.
rem Usage: uninstall.bat "C:\path\to\Granblue Fantasy Relink"   (or run it and type the folder)
set "GAME=%~1"
if "%GAME%"=="" set /p "GAME=Game folder: "
if not exist "%GAME%\data.i.orig" (echo data.i.orig not found: cannot restore the factory index. & goto :fail)
tasklist /fi "imagename eq granblue_fantasy_relink.exe" | "%SystemRoot%\System32\find.exe" /i "granblue_fantasy_relink.exe" >nul && (echo Close the game first. & goto :fail)
copy /y "%GAME%\data.i.orig" "%GAME%\data.i" >nul || goto :fail
if exist "%GAME%\winmm.dll" del /q "%GAME%\winmm.dll"
for %%d in (scripts data\filter data\layout data\model data\system) do if exist "%GAME%\%%d" rmdir /s /q "%GAME%\%%d"
echo Factory game restored (data\sound and data\ui belong to the game and are kept).
pause
exit /b 0
:fail
echo Uninstall failed.
pause
exit /b 1
