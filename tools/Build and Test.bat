@echo off
rem Builds (configure/codegen only when needed), copies to the test copies, and starts them.
rem See tools\Build-AndTest.ps1. Any argument it doesn't know goes to Run-TestCopies.ps1.
rem Examples:  "Build and Test.bat" -Copies 3 -Monitors 1,2,2 -Mute -FreeMouse
rem            "Build and Test.bat" -Codegen -NoRun
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-AndTest.ps1" %*
if errorlevel 1 pause
