# xBloom Panel - Double Shot edition - one-command installer for Windows 10/11.
#
# Downloads the latest released firmware and Espressif's esptool (a single program,
# no Python needed), finds the panel and flashes it. Your Wi-Fi and xBloom sign-in
# are kept. A few minutes (about 1 to flash). Nothing is compiled on this PC.
#
# Run in PowerShell (README, Method 3). To update later, run the same command again.
#
# Options (set before running, e.g.  $env:XBP_BACKUP = "1"):
#   XBP_PORT=COM5          use this port instead of finding the panel
#   XBP_BAUD=115200        use one flashing speed (default: fastest that works, 921600 -> 460800 -> 115200)
#   XBP_BACKUP=1           back up the panel first (Desktop\xBloom-backups, about 5-25 minutes)
#   XBP_FRESH=1            erase everything too (Wi-Fi and xBloom sign-in reset)
#   XBP_DOWNLOAD_ONLY=1    only download and check the firmware and esptool
#   XBP_BUILD=1            build the firmware from source instead of downloading it (30-60 minutes,
#                          needs Git and Python; see README)

# Native tools write harmless warnings to stderr, which 'Stop' would treat as fatal in
# Windows PowerShell 5.1, so failures are checked explicitly with $LASTEXITCODE.
$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'   # makes downloads much faster in Windows PowerShell 5.1
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Repo       = '2B-4G10/Xbloom-Panel'
$RepoUrl    = "https://github.com/$Repo"
$Latest     = "$RepoUrl/releases/latest/download"
$FullName   = 'xbloom-panel-doubleshot-FULL-USB-ONLY.bin'
$Home2      = Join-Path $env:LOCALAPPDATA 'xBloom-Panel'      # tools and downloads live here
$Speeds     = if ($env:XBP_BAUD) { @($env:XBP_BAUD) } else { @('921600','460800','115200') }   # fastest first
$Baud       = $Speeds[-1]   # reading (backup) uses the speed that always works
$Desktop    = [Environment]::GetFolderPath('Desktop')
$BackupDir  = Join-Path $Desktop 'xBloom-backups'

function Step($m) { Write-Host "`n==> $m" -ForegroundColor Cyan }
function Ok($m)   { Write-Host "    [ok] $m" -ForegroundColor Green }
function Warn($m) { Write-Host "    [!]  $m" -ForegroundColor Yellow }
function Die($m)  { throw $m }
function YesDefault($q) { $a = Read-Host "   $q [Y/n]"; return (-not $a) -or ($a -match '^[Yy]') }
function Download($url, $out) {
  for ($try = 1; $try -le 3; $try++) {
    try { Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $out -ErrorAction Stop; return }
    catch { if ($try -eq 3) { Die "Couldn't download $url ($($_.Exception.Message)). Check the internet connection and run again." }; Start-Sleep 3 }
  }
}

try {
New-Item -ItemType Directory -Force $Home2 -ErrorAction Stop | Out-Null

# ---------------------------------------------------------------------------
Step 'Checking this PC'
if ($env:OS -ne 'Windows_NT') { Die 'This script is for Windows. On a Mac use scripts/flash-mac.sh.' }
Ok ("Windows {0}" -f [Environment]::OSVersion.Version)

if ($env:XBP_BUILD) {
  # Building from source is the slow, advanced path: hand over to the build script.
  $Build = Join-Path $Home2 'build-windows.ps1'
  Download "https://raw.githubusercontent.com/$Repo/main/scripts/build-windows.ps1" $Build
  & powershell -ExecutionPolicy Bypass -File $Build
  return
}

# ---------------------------------------------------------------------------
Step 'esptool (Espressif''s flashing program)'
$EsptoolDir = Join-Path $Home2 'esptool'
$Esptool = Get-ChildItem $EsptoolDir -Recurse -Filter 'esptool.exe' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
if (-not $Esptool) {
  $rel = Invoke-RestMethod -UseBasicParsing 'https://api.github.com/repos/espressif/esptool/releases/latest' -ErrorAction SilentlyContinue
  $url = $rel.assets | Where-Object { $_.name -like '*windows-amd64.zip' } | Select-Object -First 1 -ExpandProperty browser_download_url
  if (-not $url) { $url = 'https://github.com/espressif/esptool/releases/download/v5.7.0/esptool-v5.7.0-windows-amd64.zip' }
  Warn 'Downloading esptool (about 60 MB, once)...'
  $zip = Join-Path $Home2 'esptool.zip'
  Download $url $zip
  if (Test-Path $EsptoolDir) { Remove-Item -Recurse -Force $EsptoolDir }
  Expand-Archive -Force $zip $EsptoolDir -ErrorAction Stop
  Remove-Item $zip -ErrorAction SilentlyContinue
  $Esptool = Get-ChildItem $EsptoolDir -Recurse -Filter 'esptool.exe' | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Esptool) { Die 'esptool could not be set up. Your antivirus may have removed it: allow it and run again.' }
Ok ("esptool " + ((& $Esptool version 2>$null) | Select-Object -Last 1))

# ---------------------------------------------------------------------------
Step 'Downloading the latest firmware'
$Full = Join-Path $Home2 $FullName
$Sums = Join-Path $Home2 'SHA256SUMS.txt'
Download "$Latest/$FullName" $Full
Download "$Latest/SHA256SUMS.txt" $Sums
$want = (Get-Content $Sums | Where-Object { $_ -match [regex]::Escape($FullName) } | ForEach-Object { ($_ -split '\s+')[0] }) | Select-Object -First 1
$have = (Get-FileHash $Full -Algorithm SHA256).Hash.ToLower()
if (-not $want -or $have -ne $want.ToLower()) { Die 'The download is damaged (checksum does not match). Run again.' }
$tag = try { (Invoke-RestMethod -UseBasicParsing "https://api.github.com/repos/$Repo/releases/latest").tag_name } catch { 'latest' }
Ok ("{0} ({1:N1} MB, checksum verified)" -f $tag, ((Get-Item $Full).Length / 1MB))
if ($env:XBP_DOWNLOAD_ONLY) { Write-Host "`nDownloaded and checked: $Full" -ForegroundColor Green; return }

# ---------------------------------------------------------------------------
function Find-Panel {
  # The CrowPanel's USB chip is a WCH CH34x (USB vendor ID 1A86)
  Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -match 'VID_1A86' }
}
$Port = $env:XBP_PORT
if (-not $Port) {
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

Step 'Talking to the panel'
$chip = & $Esptool --chip esp32s3 -p $Port -b 115200 chip-id 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { Write-Host $chip; Die "Couldn't talk to the panel. Close other programs using it (Arduino, serial monitors), or hold BOOT, tap RESET, release BOOT, then run again." }
$Mac = ([regex]::Match($chip, '([0-9a-f]{2}:){5}[0-9a-f]{2}')).Value -replace ':',''
Ok "ESP32-S3 found $(if ($Mac) { "(MAC $Mac)" })"

if ($env:XBP_BACKUP) {
  Step "Backing up the panel's current firmware"
  New-Item -ItemType Directory -Force $BackupDir -ErrorAction Stop | Out-Null
  $bk = Join-Path $BackupDir ("xbloom-panel-{0}-{1}.bin" -f $(if ($Mac) { $Mac } else { 'unknown' }), (Get-Date -Format 'yyyyMMdd-HHmm'))
  & $Esptool --chip esp32s3 -p $Port -b $Baud read-flash 0 ALL $bk
  if ($LASTEXITCODE -ne 0) { Die 'Backup failed, so nothing was written to the panel. Try a shorter data cable.' }
  Ok ("Saved {0} ({1} MB). Keep a copy somewhere safe." -f $bk, [math]::Round((Get-Item $bk).Length/1MB))
}

# ---------------------------------------------------------------------------
# Already up to date? (compares the start of the installed firmware: a few seconds)
$fullBytes = [IO.File]::ReadAllBytes($Full)
if (-not $env:XBP_FRESH) {
  $cur = Join-Path $Home2 'installed-head.bin'
  & $Esptool --chip esp32s3 -p $Port -b 115200 read-flash 0xE000 0x2100 $cur *> $null
  if ($LASTEXITCODE -eq 0 -and (Test-Path $cur)) {
    $want = New-Object byte[] 0x2100; [Array]::Copy($fullBytes, 0xE000, $want, 0, 0x2100)
    $wantFile = Join-Path $Home2 'release-head.bin'; [IO.File]::WriteAllBytes($wantFile, $want)
    if ((Get-FileHash $cur).Hash -eq (Get-FileHash $wantFile).Hash) {
      Write-Host "`nThe panel already runs $tag. Nothing to do." -ForegroundColor Green
      Write-Host "    (Screen blank? Run with `$env:XBP_FRESH=`"1`" to reinstall from scratch.)" -ForegroundColor DarkGray
      return
    }
  }
}

Step 'Flashing the panel (about 1 minute)'
if ($env:XBP_FRESH) {
  Warn 'Fresh install: Wi-Fi and the xBloom sign-in will be reset.'
  $parts = @('0x0', $Full)
} else {
  # The image minus the settings area (0x9000-0xE000), so Wi-Fi and sign-in stay.
  $head = New-Object byte[] 0x9000;  [Array]::Copy($fullBytes, 0, $head, 0, 0x9000)
  $tailLen = $fullBytes.Length - 0xE000
  $tail = New-Object byte[] $tailLen; [Array]::Copy($fullBytes, 0xE000, $tail, 0, $tailLen)
  $HeadBin = Join-Path $Home2 'part-boot.bin';  [IO.File]::WriteAllBytes($HeadBin, $head)
  $TailBin = Join-Path $Home2 'part-app.bin';   [IO.File]::WriteAllBytes($TailBin, $tail)
  $parts = @('0x0', $HeadBin, '0xE000', $TailBin)
}
$flashed = $false
foreach ($speed in $Speeds) {
  Write-Host "    writing at $speed baud..." -ForegroundColor DarkGray
  & $Esptool --chip esp32s3 -p $Port -b $speed write-flash @parts
  if ($LASTEXITCODE -eq 0) { $flashed = $true; break }
  Warn "That speed didn't work with this cable. Trying slower..."
  Start-Sleep 2
}
if (-not $flashed) {
  Die 'Flash failed at every speed. Use a shorter data cable plugged straight into the PC, then run again.'
}

# ---------------------------------------------------------------------------
Step 'Checking that the panel starts'
$log = ''
try {
  $sp = New-Object System.IO.Ports.SerialPort $Port, 115200
  $sp.DtrEnable = $false; $sp.RtsEnable = $false
  $sp.Open()
  $sp.RtsEnable = $true; Start-Sleep -Milliseconds 150; $sp.RtsEnable = $false   # restart it, as RESET does
  $end = (Get-Date).AddSeconds(25)
  while ((Get-Date) -lt $end -and $log -notmatch "`nready" -and $log -notmatch 'Guru Meditation|abort\(\)|Backtrace:') {
    $log += $sp.ReadExisting(); Start-Sleep -Milliseconds 200
  }
  Start-Sleep -Milliseconds 800; $log += $sp.ReadExisting()
  $sp.Close()
} catch { Warn "Couldn't watch the panel start ($($_.Exception.Message)). Look at its screen." }
$logFile = Join-Path $Home2 'panel-start.log'; Set-Content -Path $logFile -Value $log
if ($log -match 'Guru Meditation|abort\(\)|Backtrace:') {
  Write-Host ($log -split "`n" | Select-String -Pattern 'Guru|Backtrace|reason|xBloom Panel|abort' | Out-String) -ForegroundColor Yellow
  Die "The panel crashed while starting. Send a photo of the lines above (the full log is in $logFile)."
} elseif ($log -match "`nready") {
  $ver = ([regex]::Match($log, 'xBloom Panel (\S+) starting')).Groups[1].Value
  Ok "Panel started$(if ($ver) { " (version $ver)" })"
} else {
  Warn "Couldn't confirm the start from here. If the screen stays blank, send $logFile."
}

Write-Host "`nDone! The panel is restarting with the latest xBloom Panel by Double Shot." -ForegroundColor Green
Write-Host "    To update later, run the same command again." -ForegroundColor DarkGray
}
catch {
  Write-Host "`n[x] Stopped: $($_.Exception.Message)" -ForegroundColor Red
  Write-Host "    Run the same command again, or see Help in the README: $RepoUrl#-help" -ForegroundColor DarkGray
}
