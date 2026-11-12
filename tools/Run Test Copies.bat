@echo off
rem Starts the host build and its test copies (see tools\Run-TestCopies.ps1).
rem Examples:  "Run Test Copies.bat"                          host + client
rem            "Run Test Copies.bat" -Copies 3 -Monitors 1,2,2 -Mute -FreeMouse
rem -Mute and -FreeMouse apply to the client copies only; the host keeps sound and mouse aiming.
rem -MuteAll mutes every copy, the host included.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-TestCopies.ps1" %*
