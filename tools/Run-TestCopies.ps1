<#
.SYNOPSIS
  Starts the dev build and its network-test copies, each on a chosen monitor.

.DESCRIPTION
  Starts perfect_dark.exe from out\build\win-amd64-release (the host), then from
  F:\pd-client and, with -Copies 3, F:\pd-client-2. -Monitors gives each copy's
  monitor in that order (1 = primary, 2 = second monitor, ...; 0 = Windows decides).

  -Monitors takes a list such as 1,2,2 or "1 2 2"; it is split here, because a
  list given through powershell -File (as the .bat does) arrives as one string.

  Everything is passed as command-line flags (--monitor=N, --audio_mute,
  --no-mnk_mouse), which apply to this run only and are not saved. A
  perfect_dark.toml line for the same setting wins over its flag, so leave
  monitor, audio_mute and mnk_mouse out of the configs.

.PARAMETER Mute
  Start the client copies with the sound off (audio_mute). The host keeps sound.

.PARAMETER MuteAll
  Start every copy with the sound off, the host included.

.PARAMETER FreeMouse
  The client copies never capture the mouse (mnk_mouse off), so the pointer moves
  freely in and out of their windows; the mouse does not aim in them. The host
  keeps mouse aiming.

.EXAMPLE
  .\tools\Run-TestCopies.ps1                       # host + client, both on the default monitor
.EXAMPLE
  .\tools\Run-TestCopies.ps1 -Copies 3 -Monitors 1,2,2 -Mute -FreeMouse
.EXAMPLE
  .\tools\Run-TestCopies.ps1 -Copies 3 -Monitors 1,2,2 -MuteAll
#>
param(
    [ValidateRange(1, 3)][int]$Copies = 2,
    [string[]]$Monitors = @(),
    [switch]$Mute,
    [switch]$MuteAll,
    [switch]$FreeMouse,
    [string[]]$Folders = @(),
    [int]$DelaySeconds = 2
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if ($Folders.Count -eq 0) {
    $Folders = @((Join-Path $repo 'out\build\win-amd64-release'), 'F:\pd-client', 'F:\pd-client-2')
}

# "1,2,2", "1 2 2" or 1,2,2 -> 1, 2, 2
$monitorList = @(($Monitors -join ',') -split '[,;\s]+' | Where-Object { $_ -ne '' } | ForEach-Object {
    $n = 0
    if (-not [int]::TryParse($_, [ref]$n) -or $n -lt 0) { throw "Not a monitor number: '$_'" }
    $n
})

for ($i = 0; $i -lt $Copies; $i++) {
    $folder = $Folders[$i]
    $exe = Join-Path $folder 'perfect_dark.exe'
    if (-not (Test-Path $exe)) { throw "No perfect_dark.exe in $folder" }

    $gameArgs = @()
    $monitor = if ($i -lt $monitorList.Count) { $monitorList[$i] } else { 0 }
    if ($monitor -gt 0) { $gameArgs += "--monitor=$monitor" }
    $isClient = $i -gt 0   # copy 0 is the host
    if ($MuteAll -or ($isClient -and $Mute)) { $gameArgs += '--audio_mute' }
    if ($isClient -and $FreeMouse) { $gameArgs += '--no-mnk_mouse' }

    Write-Host "Starting $exe $($gameArgs -join ' ')"
    if ($gameArgs.Count -gt 0) {
        Start-Process -FilePath $exe -WorkingDirectory $folder -ArgumentList $gameArgs
    } else {
        Start-Process -FilePath $exe -WorkingDirectory $folder
    }
    # A short gap so the copies don't all load the game data at the same moment.
    if ($i -lt $Copies - 1) { Start-Sleep -Seconds $DelaySeconds }
}
