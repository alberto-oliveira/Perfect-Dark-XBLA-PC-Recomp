<#
.SYNOPSIS
  Builds the dev build, copies it to the test copies, and starts them: one command
  for a test round.

.DESCRIPTION
  Runs, in order, stopping at the first failure:
    1. configure  (cmake --preset win-amd64-release) - only when patches\rexglue.patch
       is newer than the build's CMakeCache.txt (or there is no build yet)
    2. codegen    (--target perfect_dark_codegen) - only when perfect_dark_config.toml
       (the hooks) is newer than the generated code
    3. build      (cmake --build --preset win-amd64-release)
    4. copy       tools\Copy-TestCopies.ps1
    5. run        tools\Run-TestCopies.ps1, with every argument this script does not
                  know passed on to it (-Copies, -Monitors, -Mute, -Scenario, ...)
  It says which steps ran and why.

  The build commands need the Visual Studio environment, so they run inside one
  cmd.exe that calls vcvars64.bat first (a temporary .cmd file, because quoting a
  cmd chain from PowerShell 5.1 is fragile).

.PARAMETER Configure
  Run the configure step even if nothing seems to need it.

.PARAMETER Codegen
  Run the codegen step even if nothing seems to need it.

.PARAMETER NoRun
  Stop after the copy: don't start the game.

.EXAMPLE
  .\tools\Build-AndTest.ps1 -Copies 3 -Monitors 1,2,2 -Mute
.EXAMPLE
  .\tools\Build-AndTest.ps1 -Codegen -NoRun
#>
# No [CmdletBinding()] here: without it, arguments this script doesn't declare
# (such as -Copies 3) land in $RunArgs instead of being rejected.
param(
    [switch]$Configure,
    [switch]$Codegen,
    [switch]$NoRun,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$RunArgs = @()
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$vcvars = 'F:\Workspace\VisualStudio\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$buildDir = Join-Path $repo 'out\build\win-amd64-release'

if (-not (Test-Path -LiteralPath $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

# True when $source changed after $target was made (or $target doesn't exist).
function Test-Newer([string]$source, [string]$target) {
    if (-not (Test-Path -LiteralPath $target)) { return $true }
    return (Get-Item -LiteralPath $source).LastWriteTime -gt (Get-Item -LiteralPath $target).LastWriteTime
}

# Decide which steps are needed, and say why.
$commands = @()
$cache = Join-Path $buildDir 'CMakeCache.txt'
if ($Configure) {
    Write-Host 'configure: forced (-Configure)'
    $commands += 'cmake --preset win-amd64-release'
} elseif (Test-Newer (Join-Path $repo 'patches\rexglue.patch') $cache) {
    Write-Host 'configure: patches\rexglue.patch changed (or no build yet)'
    $commands += 'cmake --preset win-amd64-release'
} else {
    Write-Host 'configure: skipped (rexglue.patch unchanged)'
}

$generated = Join-Path $repo 'generated\default\perfect_dark_init.cpp'
if ($Codegen) {
    Write-Host 'codegen:   forced (-Codegen)'
    $commands += 'cmake --build --preset win-amd64-release --target perfect_dark_codegen'
} elseif (Test-Newer (Join-Path $repo 'perfect_dark_config.toml') $generated) {
    Write-Host 'codegen:   perfect_dark_config.toml changed (hooks)'
    $commands += 'cmake --build --preset win-amd64-release --target perfect_dark_codegen'
} else {
    Write-Host 'codegen:   skipped (perfect_dark_config.toml unchanged)'
}

Write-Host 'build:     always'
$commands += 'cmake --build --preset win-amd64-release'

# One cmd.exe for all the build steps: vcvars64 sets up the compiler's
# environment, then each command runs only if the previous one succeeded (&&).
$cmdFile = Join-Path $env:TEMP 'pd-build-and-test.cmd'
$lines = @(
    '@echo off',
    "call `"$vcvars`" >nul || exit /b 1",
    "cd /d `"$repo`" || exit /b 1"
)
foreach ($c in $commands) {
    $lines += "echo."
    $lines += "echo ^> $c"
    $lines += "$c || exit /b 1"
}
Set-Content -LiteralPath $cmdFile -Value $lines -Encoding Ascii
& cmd.exe /c $cmdFile
$buildExit = $LASTEXITCODE
Remove-Item -LiteralPath $cmdFile -ErrorAction SilentlyContinue
if ($buildExit -ne 0) { throw "Build failed (exit code $buildExit); not copying or starting anything." }

Write-Host ''
Write-Host '> copy to the test copies'
& (Join-Path $PSScriptRoot 'Copy-TestCopies.ps1')

if ($NoRun) {
    Write-Host 'Not starting the game (-NoRun).'
    return
}

# Run-TestCopies in its own PowerShell, the way its .bat starts it, so the
# passed-on arguments (strings such as "-Copies" "3") are read as parameters.
Write-Host ''
Write-Host "> start: Run-TestCopies.ps1 $($RunArgs -join ' ')"
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'Run-TestCopies.ps1') @RunArgs
if ($LASTEXITCODE -ne 0) { throw "Run-TestCopies failed (exit code $LASTEXITCODE)" }
