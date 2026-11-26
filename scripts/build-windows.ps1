# xBloom Panel - Double Shot edition - build from source and flash, for Windows 10/11.
#
# The advanced path (flash-windows.ps1 downloads the ready-made firmware instead).
# Installs Git, Python 3.10-3.14, PlatformIO (pioarduino core) and esptool, gets the
# source, builds it (30-60 minutes the first time) and flashes the panel.
#
# Run:  $env:XBP_BUILD = "1"  then the README's Windows command,  or directly:
#   powershell -ExecutionPolicy Bypass -File "$HOME\Xbloom-Panel\scripts\build-windows.ps1"
#
# Options (set before running, e.g.  $env:XBP_BACKUP = "1"):
#   XBP_PORT=COM5          use this port instead of auto-detecting
#   XBP_BAUD=115200        serial speed (115200 is the reliable default)
#   XBP_BACKUP=1           also back up the panel first (about 25 minutes; off by default)
#   XBP_BUILD_ONLY=1       build the update file but don't flash
#   XBP_DIR=...            where the source is kept (default: %USERPROFILE%\Xbloom-Panel)

# Native tools write harmless warnings to stderr, which 'Stop' would treat as fatal in
# Windows PowerShell 5.1, so failures are checked explicitly with `$LASTEXITCODE / -ErrorAction Stop.
$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'

$RepoUrl     = 'https://github.com/2B-4G10/Xbloom-Panel'
$Dir         = if ($env:XBP_DIR) { $env:XBP_DIR } else { Join-Path $HOME 'Xbloom-Panel' }
$Baud        = if ($env:XBP_BAUD) { $env:XBP_BAUD } else { '115200' }
$PioPkg      = 'pioarduino==6.1.19'   # the PlatformIO Core the ESP32 platform itself uses (see requirements-build.txt)
$EnvName     = 'crowpanel-advance-43'
$PioHome     = Join-Path $HOME '.platformio'
$Penv        = Join-Path $PioHome 'penv'
$EsptoolVenv = Join-Path $HOME '.esptool'
$Desktop     = [Environment]::GetFolderPath('Desktop')
$BackupDir   = Join-Path $Desktop 'xBloom-backups'

function Step($m) { Write-Host "`n==> $m" -ForegroundColor Cyan }
function Ok($m)   { Write-Host "    [ok] $m" -ForegroundColor Green }
function Warn($m) { Write-Host "    [!]  $m" -ForegroundColor Yellow }
function Die($m)  { throw $m }
function YesDefault($q) { $a = Read-Host "   $q [Y/n]"; return (-not $a) -or ($a -match '^[Yy]') }
function Run($exe, [string[]]$argv) {   # run a program and stop on failure
  & $exe @argv
  if ($LASTEXITCODE -ne 0) { Die "Command failed ($LASTEXITCODE): $exe $($argv -join ' ')" }
}
function Refresh-Path {
  $env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
}
function Py-Ok($py) {   # true if $py is Python 3.10-3.14
  if (-not $py -or -not (Test-Path $py)) { return $false }
  & $py -c 'import sys; sys.exit(0 if (3,10) <= sys.version_info[:2] <= (3,14) else 1)' 2>$null
  return ($LASTEXITCODE -eq 0)
}
function Winget-Install($id) {
  Run 'winget' @('install', '-e', '--id', $id, '--accept-source-agreements', '--accept-package-agreements', '--silent')
  Refresh-Path
}

try {
# ---------------------------------------------------------------------------
Step 'Checking this PC'
if ($env:OS -ne 'Windows_NT') { Die 'This script is for Windows. On a Mac use scripts/flash-mac.sh.' }
Ok ("Windows {0}" -f [Environment]::OSVersion.Version)
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
  Die "winget is missing. Install 'App Installer' from the Microsoft Store, then run this again."
}
Ok 'winget'

if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Warn 'Installing Git...'; Winget-Install 'Git.Git' }
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Die 'Git install did not finish. Close PowerShell, open a new one and run again.' }
Ok (git --version)

# ---------------------------------------------------------------------------
Step 'Python 3.10-3.14 (the ESP32 tools do not support newer versions yet)'
$Py = $null
foreach ($v in '3.13','3.12','3.14','3.11','3.10') {
  $cand = $null
  if (Get-Command py -ErrorAction SilentlyContinue) {
    $cand = (& py "-$v" -c 'import sys; print(sys.executable)' 2>$null)
  }
  if (-not $cand) {
    $guess = Join-Path $env:LOCALAPPDATA ("Programs\Python\Python{0}\python.exe" -f $v.Replace('.',''))
    if (Test-Path $guess) { $cand = $guess }
  }
  if ($cand -and (Py-Ok $cand)) { $Py = $cand; break }
}
if (-not $Py) {
  Warn 'No suitable Python found. Installing Python 3.13...'
  Winget-Install 'Python.Python.3.13'
  $Py = @((Join-Path $env:LOCALAPPDATA 'Programs\Python\Python313\python.exe'), (Join-Path $env:ProgramFiles 'Python313\python.exe')) | Where-Object { Test-Path $_ } | Select-Object -First 1
  if (-not (Py-Ok $Py)) { Die 'Python 3.13 install did not finish. Close PowerShell, open a new one and run again.' }
}
Ok ("Using {0} at {1}" -f (& $Py --version), $Py)

# ---------------------------------------------------------------------------
Step "PlatformIO ($PioPkg)"
# The ESP32 platform runs part of the build with the PlatformIO Core in .platformio\penv,
# so the whole build uses that same core (mixing two cores breaks the last build step).
$PenvPy  = Join-Path $Penv 'Scripts\python.exe'
$Pio     = Join-Path $Penv 'Scripts\pio.exe'
if ((Test-Path $PenvPy) -and -not (Py-Ok $PenvPy)) {
  Warn 'PlatformIO uses an unsupported Python. Rebuilding its environment...'
  Remove-Item -Recurse -Force $Penv -ErrorAction Stop   # only PlatformIO's own Python environment; recreated below
}
if (-not (Test-Path $PenvPy)) { Run $Py @('-m','venv',$Penv) }
$want = ($PioPkg -split '==')[1]
$have = ((& $PenvPy -m pip show pioarduino 2>$null) | Select-String '^Version:\s*(.+)$' | ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() })
if ($have -ne $want) {
  Run $PenvPy @('-m','pip','install','-q','--upgrade','pip')
  & $PenvPy -m pip uninstall -y -q platformio 2>$null | Out-Null   # stock core, if an older install left it
  Run $PenvPy @('-m','pip','install','-q','--force-reinstall',$PioPkg)
}
# starlette newer than pioarduino asks for: security fixes, used only by `pio home` (requirements-build-overrides.txt)
& $PenvPy -m pip install -q --upgrade 'starlette>=1.3.1' 2>$null | Out-Null
Ok (& $Pio --version)

Step 'esptool (for the backup and the flash)'
$EsptoolPy = Join-Path $EsptoolVenv 'Scripts\python.exe'
if (-not (Py-Ok $EsptoolPy)) {
  if (Test-Path $EsptoolVenv) { Remove-Item -Recurse -Force $EsptoolVenv -ErrorAction Stop }
  Run $Py @('-m','venv',$EsptoolVenv)
}
Run $EsptoolPy @('-m','pip','install','-q','--upgrade','esptool')
$Esptool = Join-Path $EsptoolVenv 'Scripts\esptool.exe'
Ok ("esptool " + ((& $Esptool version 2>$null) | Select-Object -Last 1))

# ---------------------------------------------------------------------------
Step 'Getting the source'
if (Test-Path (Join-Path $Dir '.git')) {
  if (git -C $Dir status --porcelain --untracked-files=no) {
    Run 'git' @('-C',$Dir,'stash','push','-q','-m',"xbp installer auto-stash $(Get-Date -Format yyyy-MM-dd-HHmm)")
    Warn "You had local edits in $Dir - saved them with 'git stash'."
  }
  # Always the exact published code, even if this copy's history no longer matches
  # (local edits were stashed just above).
  Run 'git' @('-C',$Dir,'remote','set-url','origin',$RepoUrl)
  Run 'git' @('-C',$Dir,'fetch','-q','origin','main')
  Run 'git' @('-C',$Dir,'checkout','-q','-B','main','origin/main')
  Run 'git' @('-C',$Dir,'reset','-q','--hard','origin/main')
} else {
  Run 'git' @('clone','-q',$RepoUrl,$Dir)
}
Ok ("$Dir at " + (git -C $Dir log -1 --format='%h %s'))
$FwVersion = ([regex]::Match((Get-Content (Join-Path $Dir 'platformio.ini') -Raw), "FW_VERSION='\""([^""]*)\""'")).Groups[1].Value
if (-not $FwVersion) { $FwVersion = 'dev' }

# ---------------------------------------------------------------------------
function Find-Panel {
  # The CrowPanel's USB chip is a WCH CH34x (USB vendor ID 1A86)
  Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -match 'VID_1A86' }
}
$Port = $env:XBP_PORT
if (-not $env:XBP_BUILD_ONLY -and -not $Port) {
  Step 'Finding the panel (USB-C, data cable)'
  for ($attempt = 1; $attempt -le 10; $attempt++) {
    $devs = @(Find-Panel)
    $withCom = $devs | Where-Object { $_.Name -match '\((COM\d+)\)' } | Select-Object -First 1
    if ($withCom) { [void]($withCom.Name -match '\((COM\d+)\)'); $Port = $Matches[1]; break }
    $dev = $devs | Select-Object -First 1
    if ($dev) {
      $driverPage = if ($dev.PNPDeviceID -match 'PID_55D') { 'https://www.wch-ic.com/downloads/CH343SER_EXE.html' } else { 'https://www.wch-ic.com/downloads/CH341SER_EXE.html' }
      Warn "The panel's USB chip ($($dev.Name)) is connected but has no working driver."
      Warn "Opening WCH's driver page: download the EXE, run it and click INSTALL."
      Start-Process $driverPage
      Warn 'When that is done, unplug the panel and plug it back in.'
    } else {
      Warn 'No panel found on USB. Check: the panel screen is on, the cable carries data (not charge-only),'
      Warn 'and it is plugged straight into the PC (no hub).'
    }
    if ($attempt -eq 10) { Die 'Still no panel port. See Help in the README.' }
    Read-Host '   Press Enter to look again' | Out-Null
  }
  Ok "Panel port: $Port"
}

if (-not $env:XBP_BUILD_ONLY) {
  Step 'Talking to the panel'
  $chip = & $Esptool --chip esp32s3 -p $Port -b $Baud chip-id 2>&1 | Out-String
  if ($LASTEXITCODE -ne 0) { Write-Host $chip; Die "Couldn't talk to the panel. Hold BOOT, tap RESET, release BOOT, then run again." }
  $Mac = ([regex]::Match($chip, '([0-9a-f]{2}:){5}[0-9a-f]{2}')).Value -replace ':',''
  Ok "ESP32-S3 found $(if ($Mac) { "(MAC $Mac)" })"

  if ($env:XBP_BACKUP -and -not $env:XBP_NO_BACKUP) {
    Step "Backing up the panel's current firmware (about 25 minutes at $Baud baud)"
    New-Item -ItemType Directory -Force $BackupDir -ErrorAction Stop | Out-Null
    $existing = Get-ChildItem $BackupDir -Filter "*$Mac*.bin" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($existing -and -not (YesDefault "A backup already exists ($($existing.Name)). Make a new one anyway?")) {
      Ok "Keeping $($existing.FullName)"
    } else {
      $bk = Join-Path $BackupDir ("xbloom-panel-{0}-{1}.bin" -f $(if ($Mac) { $Mac } else { 'unknown' }), (Get-Date -Format 'yyyyMMdd-HHmm'))
      & $Esptool --chip esp32s3 -p $Port -b $Baud read-flash 0 ALL $bk
      if ($LASTEXITCODE -ne 0) { Die 'Backup failed, so nothing was written to the panel. Try a shorter cable or $env:XBP_BAUD="57600".' }
      $sz = (Get-Item $bk).Length
      if ($sz -lt 4194304) { Die "Backup is only $sz bytes - not flashing." }
      Ok ("Saved {0} ({1} MB). Keep a copy somewhere safe." -f $bk, [math]::Round($sz/1MB))
    }
  }
}

# ---------------------------------------------------------------------------
Step 'Building the firmware (first build: 10-30 minutes)'
Push-Location $Dir
try { & $Pio run -e $EnvName; if ($LASTEXITCODE -ne 0) { Die "Build failed. If it says 'Permission denied', your antivirus blocked the compiler: allow $Dir and $PioHome in Windows Security (or simply use the README command without XBP_BUILD, which downloads the ready-made firmware)." } }
finally { Pop-Location }
$B = Join-Path $Dir ".pio\build\$EnvName"
$BootApp0 = Join-Path $PioHome 'packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin'
foreach ($f in "$B\bootloader.bin","$B\partitions.bin","$B\firmware.bin",$BootApp0) {
  if (-not (Test-Path $f)) { Die "Expected build output is missing: $f" }
}
$UpdateBin = Join-Path $Desktop "xbloom-panel-$FwVersion-doubleshot-UPDATE-WiFi-or-USB.bin"
Copy-Item "$B\firmware.bin" $UpdateBin -Force -ErrorAction Stop
Ok "Wi-Fi update file: $UpdateBin"

if ($env:XBP_BUILD_ONLY) {
  Write-Host "`nBuilt! Upload $(Split-Path $UpdateBin -Leaf) from the panel: Settings > Software update." -ForegroundColor Green
  return
}

# ---------------------------------------------------------------------------
Step 'Flashing the panel (a few minutes)'
# Same four pieces 'pio run -t upload' writes. Wi-Fi and xBloom login (NVS) are left alone.
& $Esptool --chip esp32s3 -p $Port -b $Baud write-flash 0x0 "$B\bootloader.bin" 0x8000 "$B\partitions.bin" 0xe000 $BootApp0 0x10000 "$B\firmware.bin"
if ($LASTEXITCODE -ne 0) { Die 'Flash failed. The panel may be half-written: just run this again (see Help in the README).' }

Write-Host "`nDone! The panel is restarting with the Double Shot boot logo." -ForegroundColor Green
Write-Host "    Update file: $UpdateBin$(if ($env:XBP_BACKUP) { "  |  Backup: $BackupDir" })" -ForegroundColor DarkGray
}
catch {
  Write-Host "`n[x] Stopped: $($_.Exception.Message)" -ForegroundColor Red
}
