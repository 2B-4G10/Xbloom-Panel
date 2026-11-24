#!/usr/bin/env bash
# xBloom Panel · Double Shot edition — one-command installer for macOS.
#
# Downloads the latest released firmware and Espressif's esptool (a single program,
# no Python needed), finds the panel and flashes it. Your Wi-Fi and xBloom sign-in
# are kept. About 5–10 minutes. Nothing is compiled on this Mac.
#
# Run (README, Method 3). To update later, run the same command again.
#
# Options (environment variables, e.g.  XBP_BACKUP=1 bash …):
#   XBP_PORT=/dev/cu.xxx   use this serial port instead of finding the panel
#   XBP_BAUD=460800        faster flashing (default 115200, the speed that always works)
#   XBP_BACKUP=1           back up the panel first (~/Desktop/xBloom-backups)
#   XBP_FRESH=1            erase everything too (Wi-Fi and xBloom sign-in reset)
#   XBP_DOWNLOAD_ONLY=1    only download and check the firmware and esptool
#   XBP_BUILD=1            build the firmware from source instead (30–60 minutes; see README)
set -euo pipefail

REPO="2B-4G10/Xbloom-Panel"
REPO_URL="https://github.com/$REPO"
LATEST="$REPO_URL/releases/latest/download"
FULL_NAME="xbloom-panel-doubleshot-FULL-USB-ONLY.bin"
WORK="$HOME/Library/Application Support/xBloom-Panel"   # tools and downloads live here
BAUD="${XBP_BAUD:-115200}"
BACKUP_DIR="$HOME/Desktop/xBloom-backups"

bold=$'\033[1m'; dim=$'\033[2m'; red=$'\033[31m'; grn=$'\033[32m'; yel=$'\033[33m'; rst=$'\033[0m'
step() { printf '\n%s☕ %s%s\n' "$bold" "$*" "$rst"; }
ok()   { printf '   %s✓%s %s\n' "$grn" "$rst" "$*"; }
warn() { printf '   %s!%s %s\n' "$yel" "$rst" "$*"; }
die()  { printf '\n%s✗ %s%s\n   %sRun the same command again, or see Help in the README: %s%s\n' "$red" "$*" "$rst" "$dim" "$REPO_URL#-help" "$rst" >&2; exit 1; }
# Prompts read from the terminal, so they work even when the script is piped in.
ask()  { local a; read -r -p "   $1 " a </dev/tty || true; printf '%s' "$a"; }
fetch() {  # url → file, with retries
  curl -fL --retry 3 --retry-delay 3 -sS -o "$2" "$1" || die "Couldn't download $1. Check the internet connection."
}

mkdir -p "$WORK"

# ---------------------------------------------------------------------------
step "Checking this Mac"
[[ "$(uname -s)" == "Darwin" ]] || die "This script is for macOS. On Windows use scripts/flash-windows.ps1."
ok "macOS $(sw_vers -productVersion) on $(uname -m)"

if [[ -n "${XBP_BUILD:-}" ]]; then
  # Building from source is the slow, advanced path: hand over to the build script.
  fetch "https://raw.githubusercontent.com/$REPO/main/scripts/build-mac.sh" "$WORK/build-mac.sh"
  exec bash "$WORK/build-mac.sh"
fi

# ---------------------------------------------------------------------------
step "esptool (Espressif's flashing program)"
ESPTOOL="$(find "$WORK/esptool" -type f -name esptool -perm -u+x 2>/dev/null | head -1 || true)"
if [[ -z "$ESPTOOL" ]]; then
  arch="$( [[ "$(uname -m)" == "arm64" ]] && echo arm64 || echo amd64 )"
  url="$(curl -fsSL https://api.github.com/repos/espressif/esptool/releases/latest 2>/dev/null \
         | grep -Eo "https://[^\"]*macos-$arch\.tar\.gz" | head -1 || true)"
  url="${url:-https://github.com/espressif/esptool/releases/download/v5.7.0/esptool-v5.7.0-macos-$arch.tar.gz}"
  warn "Downloading esptool (about 60 MB, once)…"
  fetch "$url" "$WORK/esptool.tar.gz"
  rm -rf "$WORK/esptool"; mkdir -p "$WORK/esptool"
  tar -xzf "$WORK/esptool.tar.gz" -C "$WORK/esptool" || die "esptool download is damaged."
  rm -f "$WORK/esptool.tar.gz"
  xattr -dr com.apple.quarantine "$WORK/esptool" 2>/dev/null || true
  ESPTOOL="$(find "$WORK/esptool" -type f -name esptool | head -1)"
  chmod +x "$ESPTOOL"
fi
"$ESPTOOL" version >/dev/null 2>&1 || die "esptool won't start. Allow it in System Settings → Privacy & Security, then run again."
ok "esptool $("$ESPTOOL" version 2>/dev/null | tail -1)"

# ---------------------------------------------------------------------------
step "Downloading the latest firmware"
FULL="$WORK/$FULL_NAME"
fetch "$LATEST/$FULL_NAME" "$FULL"
fetch "$LATEST/SHA256SUMS.txt" "$WORK/SHA256SUMS.txt"
want="$(grep "$FULL_NAME" "$WORK/SHA256SUMS.txt" | awk '{print $1}' | head -1)"
have="$(shasum -a 256 "$FULL" | awk '{print $1}')"
[[ -n "$want" && "$have" == "$want" ]] || die "The download is damaged (checksum doesn't match)."
tag="$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" 2>/dev/null | grep -Eo '"tag_name": *"[^"]+"' | cut -d'"' -f4 || true)"
ok "${tag:-latest} ($(( $(stat -f%z "$FULL") / 1048576 )) MB, checksum verified)"
if [[ -n "${XBP_DOWNLOAD_ONLY:-}" ]]; then printf '\n%sDownloaded and checked: %s%s\n' "$grn" "$FULL" "$rst"; exit 0; fi

# ---------------------------------------------------------------------------
find_ports() { ls /dev/cu.wchusbserial* /dev/cu.usbserial* /dev/cu.usbmodem* /dev/cu.SLAB_USBtoUART* 2>/dev/null || true; }
wch_on_usb() { ioreg -p IOUSB -l -w0 2>/dev/null | grep -q '"idVendor" = 6790'; }
wch_driver_state() { systemextensionsctl list 2>/dev/null | grep -i 'cn.wch.CH34xVCPDriver' | grep -o '\[[^]]*\]' | tail -1 || true; }

PORT="${XBP_PORT:-}"
if [[ -z "$PORT" ]]; then
  step "Finding the panel (USB-C, data cable)"
  for attempt in 1 2 3 4 5 6 7 8 9 10; do
    PORTS=(); while IFS= read -r p; do [[ -z "$p" ]] || PORTS+=("$p"); done < <(find_ports)
    [[ ${#PORTS[@]} -gt 0 ]] && break
    if wch_on_usb; then
      state="$(wch_driver_state)"
      if [[ -z "$state" ]]; then
        warn "The panel's USB chip (WCH CH34x) is connected but has no driver."
        warn "Opening WCH's driver page: download, open the installer, then click Install."
        open "https://www.wch-ic.com/downloads/CH34XSER_MAC_ZIP.html" 2>/dev/null || true
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
    [[ $attempt -eq 10 ]] && die "Still no panel port."
    ask "Press Enter to look again…" >/dev/null
  done
  if [[ ${#PORTS[@]} -eq 1 ]]; then
    PORT="${PORTS[0]}"
  else
    echo "   Found several ports:"; for i in "${!PORTS[@]}"; do echo "     $((i+1))) ${PORTS[$i]}"; done
    n="$(ask "Which one is the panel? [1]")"; PORT="${PORTS[$(( ${n:-1} - 1 ))]}"
  fi
  ok "Panel port: $PORT"
fi

step "Talking to the panel"
"$ESPTOOL" --chip esp32s3 -p "$PORT" -b 115200 chip-id >"$WORK/chip.txt" 2>&1 \
  || { cat "$WORK/chip.txt"; die "Couldn't talk to the panel. Close other programs using it, or hold BOOT, tap RESET, release BOOT."; }
MAC="$(grep -Eo '([0-9a-f]{2}:){5}[0-9a-f]{2}' "$WORK/chip.txt" | head -1 | tr -d ':' || true)"
ok "ESP32-S3 found${MAC:+ (MAC $MAC)}"

if [[ -n "${XBP_BACKUP:-}" ]]; then
  step "Backing up the panel's current firmware"
  mkdir -p "$BACKUP_DIR"
  BK="$BACKUP_DIR/xbloom-panel-${MAC:-unknown}-$(date +%Y%m%d-%H%M).bin"
  "$ESPTOOL" --chip esp32s3 -p "$PORT" -b "$BAUD" read-flash 0 ALL "$BK" \
    || die "Backup failed, so nothing was written to the panel. Try a shorter data cable."
  ok "Saved $BK. Keep a copy somewhere safe."
fi

# ---------------------------------------------------------------------------
step "Flashing the panel (about 3–5 minutes)"
if [[ -n "${XBP_FRESH:-}" ]]; then
  warn "Fresh install: Wi-Fi and the xBloom sign-in will be reset."
  "$ESPTOOL" --chip esp32s3 -p "$PORT" -b "$BAUD" write-flash 0x0 "$FULL" \
    || die "Flash failed. Use a shorter data cable plugged straight into the Mac."
else
  # The image minus the settings area (0x9000–0xE000), so Wi-Fi and sign-in stay.
  dd if="$FULL" of="$WORK/part-boot.bin" bs=4096 count=9 2>/dev/null
  dd if="$FULL" of="$WORK/part-app.bin" bs=4096 skip=14 2>/dev/null
  "$ESPTOOL" --chip esp32s3 -p "$PORT" -b "$BAUD" write-flash 0x0 "$WORK/part-boot.bin" 0xe000 "$WORK/part-app.bin" \
    || die "Flash failed. Use a shorter data cable plugged straight into the Mac."
fi

printf '\n%s🎉 Done! The panel is restarting with the latest xBloom Panel by Double Shot.%s\n' "$grn$bold" "$rst"
printf '%s   To update later, run the same command again.%s\n' "$dim" "$rst"
