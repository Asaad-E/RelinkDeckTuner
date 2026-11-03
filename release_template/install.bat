@echo off
setlocal
rem Install Relink Deck Tuner into the game folder (Windows).
rem Usage: install.bat "C:\path\to\Granblue Fantasy Relink"   (or run it and type the folder)
rem Backs up what it replaces to <game>\relinkdecktuner_backup_<date>\ and verifies every file.
set "HERE=%~dp0"
set "GAME=%~1"
if "%GAME%"=="" set /p "GAME=Game folder: "
if not exist "%GAME%\granblue_fantasy_relink.exe" (echo granblue_fantasy_relink.exe not found in "%GAME%" & goto :fail)
tasklist /fi "imagename eq granblue_fantasy_relink.exe" | "%SystemRoot%\System32\find.exe" /i "granblue_fantasy_relink.exe" >nul && (echo Close the game before installing. & goto :fail)
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop'; $g=$env:GAME; $h=$env:HERE;" ^
  "$bk=Join-Path $g ('relinkdecktuner_backup_' + (Get-Date -Format yyyyMMdd_HHmm)); New-Item -ItemType Directory -Force (Join-Path $bk 'data') | Out-Null;" ^
  "foreach($f in 'data.i','winmm.dll','scripts'){ $p=Join-Path $g $f; if(Test-Path $p){ Copy-Item $p $bk -Recurse -Force } }" ^
  "foreach($d in 'filter','layout','model','system'){ $p=Join-Path $g ('data\' + $d); if(Test-Path $p){ Copy-Item $p (Join-Path $bk 'data') -Recurse -Force } }" ^
  "if(-not (Test-Path (Join-Path $g 'data.i.orig'))){ Copy-Item (Join-Path $g 'data.i') (Join-Path $g 'data.i.orig') }" ^
  "foreach($p in 'data\filter','data\layout','data\model','data\system','scripts'){ $q=Join-Path $g $p; if(Test-Path $q){ Remove-Item $q -Recurse -Force } }" ^
  "Copy-Item (Join-Path $h 'game\*') $g -Recurse -Force;" ^
  "$bad=0; Get-Content (Join-Path $h 'SHA256SUMS.txt') | ForEach-Object { $hash,$rel=$_ -split '\s+\*?',2; $f=Join-Path $g $rel; if(-not (Test-Path $f) -or (Get-FileHash $f -Algorithm SHA256).Hash -ne $hash.ToUpper()){ Write-Host ('MISMATCH ' + $rel); $bad++ } };" ^
  "if($bad){ exit 1 } else { Write-Host ('Installed and verified. Backup: ' + $bk) }"
if errorlevel 1 goto :fail
pause
exit /b 0
:fail
echo Installation failed.
pause
exit /b 1
