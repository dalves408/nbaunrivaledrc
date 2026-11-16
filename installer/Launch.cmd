@echo off
setlocal
rem Installed next to nbaunrivaled.exe. Points the recomp at the imported game
rem folder and applies sticky player settings via Launch-Game-compatible cvars.
rem ISA (portable vs AVX/znver1) is chosen once at Setup time: primary is
rem nbaunrivaled.exe; the other build is also installed as nbaunrivaled-AVX.exe
rem or nbaunrivaled-NonAVX.exe. Shortcuts / this helper use the primary only.
set "APP=%~dp0"
set "GAME=%APP%game"
if not exist "%GAME%\default.xex" (
  echo Game files are missing. Run Setup again and select your ZIP or extracted folder.
  pause
  exit /b 1
)
if not exist "%APP%nbaunrivaled.exe" (
  echo nbaunrivaled.exe is missing. Re-run Setup.
  pause
  exit /b 1
)
rem nbaunrivaled.exe now carries these launch defaults itself (game folder next to the exe,
rem saves under %LOCALAPPDATA%\NBAUnrivaled, 60 fps, settings.ini from config.exe / F1). No flags needed;
rem any flag passed here still overrides.
start "" "%APP%nbaunrivaled.exe" %*
endlocal
