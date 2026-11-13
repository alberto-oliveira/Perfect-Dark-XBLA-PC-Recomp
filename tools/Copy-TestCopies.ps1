<#
.SYNOPSIS
  Copies the dev build's game files to the extra game folders used for network testing.

.DESCRIPTION
  Copies only what the game needs to run (the same files tools\Make-Release.ps1
  ships) from out\build\win-amd64-release to F:\pd-client and F:\pd-client-2 (or
  the folders given), plus the native_shaders cache so the copies start quickly.
  Build leftovers, userdata (saves, profiles), logs and each copy's own
  perfect_dark.toml are never copied. Nothing in a target folder is deleted. A
  copy that has no assets junction yet gets one pointing to the repo's game folder.

.PARAMETER Targets
  Folders to copy to. Default: F:\pd-client, F:\pd-client-2.

.EXAMPLE
  .\tools\Copy-TestCopies.ps1
.EXAMPLE
  .\tools\Copy-TestCopies.ps1 -Targets F:\pd-client
#>
param(
    [string[]]$Targets = @('F:\pd-client', 'F:\pd-client-2')
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repo 'out\build\win-amd64-release'
$game = Join-Path $repo 'game'

# The game and its DLLs, as in Make-Release.ps1's $runtimeFiles (the Visual C++
# runtime DLLs it also ships are already installed on a PC that builds the game).
$runtimeFiles = @('perfect_dark.exe', 'rexruntime.dll', 'rexgpu-xenos.dll', 'dxcompiler.dll', 'dxil.dll',
    'native_pipelines.bin')
# Optional: a shader cache the game rebuilds when it is missing.
$cacheDirs = @('native_shaders')

foreach ($file in $runtimeFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $file))) { throw "No $file in $source - build first." }
}

function Invoke-Robocopy([string[]]$RoboArgs, [string]$What) {
    robocopy @RoboArgs /NFL /NDL /NJH /NJS /NP | Out-Null
    # robocopy: 0-7 = success (1 = files copied), 8 and up = failure.
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed copying $What (exit code $LASTEXITCODE)" }
}

foreach ($target in $Targets) {
    Write-Host "== $source -> $target"
    Invoke-Robocopy (@($source, $target) + $runtimeFiles) 'the game files'
    foreach ($dir in $cacheDirs) {
        $from = Join-Path $source $dir
        if (Test-Path -LiteralPath $from) { Invoke-Robocopy @($from, (Join-Path $target $dir), '/E') $dir }
    }
    Write-Host "   copied $($runtimeFiles -join ', ') and $($cacheDirs -join ', ')"

    $assets = Join-Path $target 'assets'
    if (-not (Test-Path -LiteralPath $assets)) {
        New-Item -ItemType Junction -Path $assets -Target $game | Out-Null
        Write-Host "   created assets junction -> $game"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $target 'perfect_dark.toml'))) {
        Write-Host "   note: $target has no perfect_dark.toml yet; give it its own (net_address etc.)"
    }
}
$global:LASTEXITCODE = 0
Write-Host 'Done.'
