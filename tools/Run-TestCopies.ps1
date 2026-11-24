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

  The game centres its window on its monitor, so copies sharing a monitor would
  open on top of each other. Those are placed side by side once their windows
  appear, each taking an equal share of the monitor's width at 16:9 (the game
  follows a resize as it does when the Resolution menu changes the size).

.PARAMETER Mute
  Start the client copies with the sound off (audio_mute). The host keeps sound.

.PARAMETER MuteAll
  Start every copy with the sound off, the host included.

.PARAMETER FreeMouse
  The client copies never capture the mouse (mnk_mouse off), so the pointer moves
  freely in and out of their windows; the mouse does not aim in them. The host
  keeps mouse aiming.

.PARAMETER TestPipe
  Start every copy with the test pipe on (pd_test_pipe): each reads commands
  from test_cmd.txt beside its exe, and logs every stage, menu and highlighted
  row ([test] lines), which is how a menu path is recorded for a test script.

.PARAMETER Scenario
  Run a test script in every copy (tools\test-scripts, see its README.md): the
  host gets lobby-host.txt then <scenario>\host.txt; client N gets
  lobby-client.txt then <scenario>\client<N>.txt, or <scenario>\client.txt when
  there is none. In the scripts, {players} becomes the number of copies,
  {clients} that minus one, and {copy} the copy's number (0 = host). Turns on
  the test pipe, and gives every copy the same empty signal folder,
  out\test-sync (pd_test_sync_dir).

.EXAMPLE
  .\tools\Run-TestCopies.ps1 -Copies 3 -Scenario basic
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
    [switch]$TestPipe,
    [string]$Scenario = '',
    [string[]]$Folders = @(),
    [int]$DelaySeconds = 2
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
# Through powershell -File a list arrives as one string, as with -Monitors: split it.
$Folders = @(($Folders -join ',') -split ',' | Where-Object { $_ -ne '' })
if ($Folders.Count -eq 0) {
    $Folders = @((Join-Path $repo 'out\build\win-amd64-release'), 'F:\pd-client', 'F:\pd-client-2')
}

# "1,2,2", "1 2 2" or 1,2,2 -> 1, 2, 2
$monitorList = @(($Monitors -join ',') -split '[,;\s]+' | Where-Object { $_ -ne '' } | ForEach-Object {
    $n = 0
    if (-not [int]::TryParse($_, [ref]$n) -or $n -lt 0) { throw "Not a monitor number: '$_'" }
    $n
})

# A scenario: check its files before starting anything, and empty the signal folder.
$scripts = Join-Path $PSScriptRoot 'test-scripts'
$syncDir = Join-Path $repo 'out\test-sync'
if ($Scenario) {
    $TestPipe = $true
    $scenarioDir = Join-Path $scripts $Scenario
    if (-not (Test-Path (Join-Path $scenarioDir 'host.txt'))) { throw "No scenario '$Scenario' (no $scenarioDir\host.txt)" }
    if (Test-Path $syncDir) { Remove-Item -LiteralPath $syncDir -Recurse -Force }
    New-Item -ItemType Directory -Path $syncDir | Out-Null
}

# The script for copy $i: its role's lobby part, then the scenario's part, with
# the placeholders filled in.
function Get-CopyScript([int]$i) {
    if ($i -eq 0) {
        $parts = @((Join-Path $scripts 'lobby-host.txt'), (Join-Path $scenarioDir 'host.txt'))
    } else {
        $own = Join-Path $scenarioDir "client$i.txt"
        if (-not (Test-Path $own)) { $own = Join-Path $scenarioDir 'client.txt' }
        if (-not (Test-Path $own)) { throw "No $own for client $i" }
        $parts = @((Join-Path $scripts 'lobby-client.txt'), $own)
    }
    $text = ($parts | ForEach-Object { Get-Content -LiteralPath $_ -Raw }) -join "`r`n"
    return $text.Replace('{players}', "$Copies").Replace('{clients}', "$($Copies - 1)").Replace('{copy}', "$i")
}

$started = @()   # the copies started: process and monitor
for ($i = 0; $i -lt $Copies; $i++) {
    $folder = $Folders[$i]
    $exe = Join-Path $folder 'perfect_dark.exe'
    if (-not (Test-Path $exe)) { throw "No perfect_dark.exe in $folder" }

    # The game reads test_cmd.txt beside its exe (and deletes it once read).
    if ($Scenario) {
        Set-Content -LiteralPath (Join-Path $folder 'test_cmd.txt') -Value (Get-CopyScript $i) -Encoding Ascii
    }

    $gameArgs = @()
    $monitor = if ($i -lt $monitorList.Count) { $monitorList[$i] } else { 0 }
    if ($monitor -gt 0) { $gameArgs += "--monitor=$monitor" }
    $isClient = $i -gt 0   # copy 0 is the host
    if ($MuteAll -or ($isClient -and $Mute)) { $gameArgs += '--audio_mute' }
    if ($isClient -and $FreeMouse) { $gameArgs += '--no-mnk_mouse' }
    if ($TestPipe) { $gameArgs += '--pd_test_pipe' }
    if ($Scenario) { $gameArgs += "--pd_test_sync_dir=$syncDir" }

    Write-Host "Starting $exe $($gameArgs -join ' ')"
    if ($gameArgs.Count -gt 0) {
        $process = Start-Process -FilePath $exe -WorkingDirectory $folder -ArgumentList $gameArgs -PassThru
    } else {
        $process = Start-Process -FilePath $exe -WorkingDirectory $folder -PassThru
    }
    $started += [pscustomobject]@{ Process = $process; Monitor = $monitor }
    # A short gap so the copies don't all load the game data at the same moment.
    if ($i -lt $Copies - 1) { Start-Sleep -Seconds $DelaySeconds }
}

# ---- Side by side: copies that share a monitor --------------------------------
$shared = @($started | Where-Object { $_.Monitor -gt 0 } | Group-Object Monitor | Where-Object { $_.Count -gt 1 })
if ($shared.Count -eq 0) { return }

# MoveWindow and SetProcessDPIAware from the Windows API. DPI-aware, so the
# monitor's size and the window's position are both in real pixels.
Add-Type -Namespace PdTest -Name Win32 -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int x, int y, int w, int h, bool repaint);
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
'@
Add-Type -AssemblyName System.Windows.Forms
[void][PdTest.Win32]::SetProcessDPIAware()

# The window of a started copy, once it is shown (0 if it never appears).
function Wait-Window($process) {
    for ($t = 0; $t -lt 60; $t++) {   # up to 30 seconds
        $process.Refresh()
        if ($process.HasExited) { return [IntPtr]::Zero }
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) { return $process.MainWindowHandle }
        Start-Sleep -Milliseconds 500
    }
    return [IntPtr]::Zero
}

foreach ($group in $shared) {
    $members = @($group.Group)
    $windows = @($members | ForEach-Object { Wait-Window $_.Process })
    if ($windows -contains [IntPtr]::Zero) { Write-Warning "Monitor $($group.Name): a window didn't appear; not arranging"; continue }
    # The game fits its window to its saved size just after showing it: wait
    # for that, so it doesn't undo the move.
    Start-Sleep -Seconds 1
    # The monitor the game centred them on, minus the taskbar.
    $area = [System.Windows.Forms.Screen]::FromHandle($windows[0]).WorkingArea
    $width = [int][math]::Floor($area.Width / $members.Count)
    $height = [math]::Min($area.Height, [int]($width * 9 / 16) + 40)   # + about a title bar
    $top = $area.Top + [int](($area.Height - $height) / 2)
    for ($k = 0; $k -lt $members.Count; $k++) {
        [void][PdTest.Win32]::MoveWindow($windows[$k], $area.Left + $k * $width, $top, $width, $height, $true)
    }
    Write-Host "Monitor $($group.Name): $($members.Count) windows side by side, $width x $height each"
}
