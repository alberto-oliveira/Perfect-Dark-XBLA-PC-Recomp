@echo off
rem Builds pdunpack.exe next to this file with the LLVM clang (no Visual Studio prompt needed
rem beyond what clang finds by itself). The LZX decoder comes from the SDK's copy of libmspack.
setlocal
set CLANG=F:\Workspace\LLVM\bin\clang.exe
set MSPACK=%~dp0..\..\..\rexglue-sdk\thirdparty\libmspack\libmspack\mspack
"%CLANG%" -O2 -w -UHAVE_CONFIG_H -I"%MSPACK%" "%~dp0pdunpack.c" "%MSPACK%\lzxd.c" "%MSPACK%\system.c" -o "%~dp0pdunpack.exe"
if errorlevel 1 (echo build failed & exit /b 1)
echo built %~dp0pdunpack.exe
