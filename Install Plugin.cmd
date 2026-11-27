@echo off
rem Copies the built Stilletchaser plugins into Arena's Extra Effects. Close Arena first.
tasklist /FI "IMAGENAME eq Arena.exe" | find /I "Arena.exe" >nul && (
  echo Chiudi Resolume Arena e rilancia questo file.
  pause
  exit /b 1
)
rem Developer shortcut: the released editor installs the plugins itself (File > Install / update plugins).
set "DEST=%USERPROFILE%\Documents\Resolume Arena\Extra Effects\STLT-CHASER"
if not exist "%DEST%" mkdir "%DEST%"
rem Earlier builds went into "Extra Effects\Stilletchaser"; two copies would clash in Arena.
set "OLD=%USERPROFILE%\Documents\Resolume Arena\Extra Effects\Stilletchaser"
for %%F in (Stilletchaser STLT-CHASER STLT-WIPER STLT-OUTLINER) do if exist "%OLD%\%%F.dll" del "%OLD%\%%F.dll"
for %%P in (STLT-CHASER STLT-WIPER STLT-OUTLINER STLT-SAMPLER STLT-TILER) do (
  copy /Y "%~dp0plugin\build\Release\%%P.dll" "%DEST%\%%P.dll" >nul && echo %%P installato.
)
echo Riapri Arena.
pause
