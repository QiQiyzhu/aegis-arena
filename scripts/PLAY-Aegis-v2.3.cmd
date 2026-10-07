@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0AegisArena.exe" (
  echo AegisArena.exe is missing. Extract the complete Windows game ZIP first.
  echo Keep this launcher beside AegisArena.exe and the Engine and AegisArena folders.
  pause
  exit /b 1
)
start "Aegis Arena" "%~dp0AegisArena.exe" -AegisRelease
endlocal
