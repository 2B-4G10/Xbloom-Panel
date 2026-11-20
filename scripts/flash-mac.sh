#!/usr/bin/env bash
# xBloom Panel · Double Shot edition — one-command installer for macOS.
#
# Checks and installs everything it needs (Xcode command-line tools, Homebrew,
# Python 3.10–3.14, PlatformIO (pioarduino core), esptool, the WCH USB driver), finds the
# panel, backs up its current firmware, builds this repo and flashes it.
#
# Run:   bash <(curl -fsSL https://raw.githubusercontent.com/2B-4G10/Xbloom-Panel/main/scripts/flash-mac.sh)
#
# Options (environment variables):
#   XBP_PORT=/dev/cu.xxx   use this serial port instead of auto-detecting
#   XBP_BAUD=115200        serial speed (115200 is the reliable default)
#   XBP_NO_BACKUP=1        skip the backup step
#   XBP_BUILD_ONLY=1       build the update file but don't flash
#   XBP_DIR=~/Xbloom-Panel where the source is kept
set -euo pipefail

REPO_URL="https://github.com/2B-4G10/Xbloom-Panel"
DIR="${XBP_DIR:-$HOME/Xbloom-Panel}"
BAUD="${XBP_BAUD:-115200}"
PIO_PKG="pioarduino==6.2.0.post1"   # the PlatformIO Core the ESP32 platform itself uses (see requirements-build.txt)
ENV_NAME="crowpanel-advance-43"
PIO_HOME="$HOME/.platformio"
PENV="$PIO_HOME/penv"
ESPTOOL_VENV="$HOME/.esptool"
BACKUP_DIR="$HOME/Desktop/xBloom-backups"

bold=$'\033[1m'; dim=$'\033[2m'; red=$'\033[31m'; grn=$'\033[32m'; yel=$'\033[33m'; rst=$'\033[0m'
step() { printf '\n%s☕ %s%s\n' "$bold" "$*" "$rst"; }
ok()   { printf '   %s✓%s %s\n' "$grn" "$rst" "$*"; }
warn() { printf '   %s!%s %s\n' "$yel" "$rst" "$*"; }
die()  { printf '\n%s✗ %s%s\n' "$red" "$*" "$rst" >&2; exit 1; }
# Prompts read from the terminal, so they work even when the script is piped in.
ask()  { local a; read -r -p "   $1 " a </dev/tty || true; printf '%s' "$a"; }
yes_default() { local a; a="$(ask "$1 [Y/n]")"; [[ -z "$a" || "$a" =~ ^[Yy] ]]; }

py_ok() {  # $1 = python executable; succeeds if it is 3.10–3.14
  "$1" -c 'import sys; sys.exit(0 if (3,10) <= sys.version_info[:2] <= (3,14) else 1)' 2>/dev/null
}

# ---------------------------------------------------------------------------
step "Checking this Mac"
[[ "$(uname -s)" == "Darwin" ]] || die "This script is for macOS. On Windows use scripts/flash-windows.ps1."
ok "macOS $(sw_vers -productVersion) on $(uname -m)"

if ! xcode-select -p >/dev/null 2>&1; then
  warn "Apple's command-line tools (git, compilers) are missing. Opening the installer…"
  xcode-select --install || true
  die "Finish the 'command line developer tools' install, then run this script again."
fi
ok "Command-line tools"
command -v git >/dev/null || die "git not found even though the command-line tools are installed."
ok "git $(git --version | awk '{print $3}')"

# Homebrew (standard locations plus a home-folder install)
BREW=""
for b in "$(command -v brew 2>/dev/null || true)" /opt/homebrew/bin/brew /usr/local/bin/brew "$HOME/.homebrew/bin/brew" "$HOME/homebrew/bin/brew"; do
  [[ -n "$b" && -x "$b" ]] && { BREW="$b"; break; }
done
if [[ -z "$BREW" ]]; then
  warn "Homebrew isn't installed. It's needed for Python 3.13 and the USB driver."
  if yes_default "Install Homebrew now (official installer from brew.sh)?"; then
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)" </dev/tty
    for b in /opt/homebrew/bin/brew /usr/local/bin/brew; do [[ -x "$b" ]] && BREW="$b"; done
  fi
  [[ -n "$BREW" ]] || die "Homebrew is required. Install it from https://brew.sh and run this script again."
fi
eval "$("$BREW" shellenv)"
ok "Homebrew ($BREW)"

# ---------------------------------------------------------------------------
step "Python 3.10–3.14 (the ESP32 tools don't support newer versions yet)"
PY=""
for v in 3.13 3.12 3.14 3.11 3.10; do
  for c in "$("$BREW" --prefix 2>/dev/null)/opt/python@$v/bin/python$v" "$(command -v "python$v" 2>/dev/null || true)"; do
    [[ -n "$c" && -x "$c" ]] && py_ok "$c" && { PY="$c"; break 2; }
  done
done
if [[ -z "$PY" ]]; then
  warn "No suitable Python found (your default python3 is $(python3 --version 2>&1 | awk '{print $2}')). Installing Python 3.13 with Homebrew…"
  "$BREW" install python@3.13
  PY="$("$BREW" --prefix python@3.13)/bin/python3.13"
  py_ok "$PY" || die "Python 3.13 install didn't work."
fi
ok "Using $("$PY" --version) at $PY"

# ---------------------------------------------------------------------------
step "PlatformIO (${PIO_PKG})"
# The ESP32 platform runs part of the build with the PlatformIO Core in ~/.platformio/penv,
# so the whole build uses that same core (mixing two cores breaks the last build step).
if [[ -x "$PENV/bin/python" ]] && ! py_ok "$PENV/bin/python"; then
  warn "PlatformIO's environment uses $("$PENV/bin/python" --version 2>&1). Rebuilding it with $("$PY" --version)…"
  rm -rf "$PENV"   # only PlatformIO's own Python environment; recreated just below
fi
[[ -x "$PENV/bin/python" ]] || "$PY" -m venv "$PENV"
want="${PIO_PKG#*==}"
have="$("$PENV/bin/python" -m pip show pioarduino 2>/dev/null | awk '/^Version:/{print $2}' || true)"
if [[ "$have" != "$want" ]]; then
  "$PENV/bin/python" -m pip install -q --upgrade pip
  "$PENV/bin/python" -m pip uninstall -y -q platformio 2>/dev/null || true   # stock core, if an older install left it
  "$PENV/bin/python" -m pip install -q --force-reinstall "$PIO_PKG"
fi
PIO="$PENV/bin/pio"
ok "$("$PIO" --version)"

step "esptool (for the backup and the flash)"
if [[ ! -x "$ESPTOOL_VENV/bin/esptool" ]] || ! py_ok "$ESPTOOL_VENV/bin/python"; then
  rm -rf "$ESPTOOL_VENV"
  "$PY" -m venv "$ESPTOOL_VENV"
fi
"$ESPTOOL_VENV/bin/python" -m pip install -q --upgrade esptool
ESPTOOL="$ESPTOOL_VENV/bin/esptool"
ok "esptool $("$ESPTOOL" version 2>/dev/null | tail -1)"

# ---------------------------------------------------------------------------
step "Getting the source"
if [[ -d "$DIR/.git" ]]; then
  if [[ -n "$(git -C "$DIR" status --porcelain --untracked-files=no)" ]]; then
    git -C "$DIR" stash push -q -m "xbp installer auto-stash $(date +%F-%H%M)"
    warn "You had local edits in $DIR — saved them with 'git stash' (git stash list to see them)."
  fi
  git -C "$DIR" checkout -q main
  git -C "$DIR" pull -q --ff-only
else
  git clone -q "$REPO_URL" "$DIR"
fi
ok "$DIR at $(git -C "$DIR" log -1 --format='%h %s')"
FW_VERSION="$(sed -n "s/.*FW_VERSION='\"\([^\"]*\)\"'.*/\1/p" "$DIR/platformio.ini")"

# ---------------------------------------------------------------------------
find_ports() { ls /dev/cu.wchusbserial* /dev/cu.usbserial* /dev/cu.usbmodem* /dev/cu.SLAB_USBtoUART* 2>/dev/null || true; }
wch_on_usb() { ioreg -p IOUSB -l -w0 2>/dev/null | grep -q '"idVendor" = 6790'; }
wch_driver_state() { systemextensionsctl list 2>/dev/null | grep -i 'cn.wch.CH34xVCPDriver' | grep -o '\[[^]]*\]' | tail -1 || true; }

PORT="${XBP_PORT:-}"
if [[ -z "${XBP_BUILD_ONLY:-}" && -z "$PORT" ]]; then
  step "Finding the panel (USB-C, data cable)"
  for attempt in 1 2 3 4 5 6 7 8 9 10; do
    PORTS=(); while IFS= read -r p; do [[ -z "$p" ]] || PORTS+=("$p"); done < <(find_ports)
    [[ ${#PORTS[@]} -gt 0 ]] && break

    if wch_on_usb; then
      state="$(wch_driver_state)"
      if [[ -z "$state" ]]; then
        warn "The panel's USB chip (WCH CH34x) is connected but has no driver. Installing it…"
        "$BREW" install --cask wch-ch34x-usb-serial-driver || true
        open -a CH34xVCPDriver 2>/dev/null || true
        warn "In the CH34xVCPDriver window that opened, click Install."
      elif [[ "$state" != "[activated enabled]" ]]; then
        warn "WCH driver is installed but not switched on ($state)."
        warn "System Settings → General → Login Items & Extensions → Driver Extensions → turn on CH34xVCPDriver."
        open "x-apple.systempreferences:com.apple.LoginItems-Settings.extension" 2>/dev/null || true
      fi
      warn "When that's done, unplug the panel and plug it back in."
    else
      warn "No panel found on USB. Check: the panel's screen is on, the cable carries data (not charge-only),"
      warn "and it's plugged straight into the Mac (no hub)."
    fi
    [[ $attempt -eq 10 ]] && die "Still no panel port. See the Troubleshooting section of the README."
    ask "Press Enter to look again…" >/dev/null
  done
  if [[ ${#PORTS[@]} -eq 1 ]]; then
    PORT="${PORTS[0]}"
  else
    echo "   Found several ports:"; for i in "${!PORTS[@]}"; do echo "     $((i+1))) ${PORTS[$i]}"; done
    n="$(ask "Which one is the panel? [1]")"; PORT="${PORTS[$(( ${n:-1} - 1 ))]}"
  fi
  ok "Panel port: $PORT"

  step "Talking to the panel"
  "$ESPTOOL" --chip esp32s3 -p "$PORT" -b "$BAUD" chip-id >/tmp/xbp-chip.txt 2>&1 \
    || { cat /tmp/xbp-chip.txt; die "Couldn't talk to the panel. Hold BOOT, tap RESET, release BOOT, then run again."; }
  MAC="$(grep -Eo '([0-9a-f]{2}:){5}[0-9a-f]{2}' /tmp/xbp-chip.txt | head -1 | tr -d ':' || true)"
  ok "ESP32-S3 found${MAC:+ (MAC $MAC)}"

  # ---------------------------------------------------------------------------
  if [[ -z "${XBP_NO_BACKUP:-}" ]]; then
    step "Backing up the panel's current firmware (about 25 minutes at $BAUD baud)"
    mkdir -p "$BACKUP_DIR"
    EXISTING="$(ls -t "$BACKUP_DIR"/*"${MAC:-}"*.bin "$HOME"/Desktop/xbloom-backup-*.bin 2>/dev/null | head -1 || true)"
    if [[ -n "$EXISTING" ]] && ! yes_default "A backup already exists ($(basename "$EXISTING")). Make a new one anyway?"; then
      ok "Keeping $EXISTING"
    else
      BK="$BACKUP_DIR/xbloom-panel-${MAC:-unknown}-$(date +%Y%m%d-%H%M).bin"
      "$ESPTOOL" --chip esp32s3 -p "$PORT" -b "$BAUD" read-flash 0 ALL "$BK" \
        || die "Backup failed, so nothing was written to the panel. Try a shorter cable or XBP_BAUD=57600."
      sz=$(stat -f%z "$BK"); [[ $sz -ge 4194304 ]] || die "Backup is only $sz bytes — not flashing."
      ok "Saved $BK ($((sz/1048576)) MB). Keep a copy somewhere safe."
    fi
  fi
fi

# ---------------------------------------------------------------------------
step "Building the firmware (first build: 10–30 minutes)"
( cd "$DIR" && "$PIO" run -e "$ENV_NAME" ) || die "Build failed. Copy the last 30 lines and open an issue on $REPO_URL/issues."
B="$DIR/.pio/build/$ENV_NAME"
BOOT_APP0="$PIO_HOME/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
for f in "$B/bootloader.bin" "$B/partitions.bin" "$B/firmware.bin" "$BOOT_APP0"; do
  [[ -s "$f" ]] || die "Expected build output is missing: $f"
done
UPDATE_BIN="$HOME/Desktop/xbloom-panel-${FW_VERSION:-dev}-doubleshot-update.bin"
cp "$B/firmware.bin" "$UPDATE_BIN"
ok "Wi-Fi update file: $UPDATE_BIN"

if [[ -n "${XBP_BUILD_ONLY:-}" ]]; then
  printf '\n%s✨ Built. Upload %s from the panel: Settings → Software update.%s\n' "$grn" "$(basename "$UPDATE_BIN")" "$rst"
  exit 0
fi

# ---------------------------------------------------------------------------
step "Flashing the panel (a few minutes)"
# Same four pieces 'pio run -t upload' writes. Wi-Fi and xBloom login (NVS) are left alone.
"$ESPTOOL" --chip esp32s3 -p "$PORT" -b "$BAUD" write-flash \
  0x0 "$B/bootloader.bin" 0x8000 "$B/partitions.bin" 0xe000 "$BOOT_APP0" 0x10000 "$B/firmware.bin" \
  || die "Flash failed. The panel may be half-written: run this script again, or restore your backup (see README)."

printf '\n%s🎉 Done! The panel is restarting with the Double Shot boot logo.%s\n' "$grn$bold" "$rst"
printf '%s   Backup: %s   ·   Update file: %s%s\n' "$dim" "$BACKUP_DIR" "$UPDATE_BIN" "$rst"
