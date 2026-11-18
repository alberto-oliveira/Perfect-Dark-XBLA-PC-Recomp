@echo off
rem Copies the dev build's game files to F:\pd-client and F:\pd-client-2 (see tools\Copy-TestCopies.ps1).
rem Extra arguments go to the script, e.g.  "Copy Test Copies.bat" -Targets F:\pd-client
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Copy-TestCopies.ps1" %*
pause
