<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/logo-dark.png">
  <img alt="By: Double Shot — Speciality Coffee / Bareista Tools" src="docs/images/logo-light.png" width="560">
</picture>

# ☕ xBloom Panel · Double Shot Edition

**A touchscreen for your xBloom Studio, now with a little Double Shot love on every boot ✨**

![Chip](https://img.shields.io/badge/ESP32--S3-800×480-c4976e?style=for-the-badge)
![macOS](https://img.shields.io/badge/macOS-one_command-7887c8?style=for-the-badge&logo=apple&logoColor=white)
![Windows](https://img.shields.io/badge/Windows-one_command-7cc4ae?style=for-the-badge&logo=windows&logoColor=white)
![License](https://img.shields.io/badge/license-MIT-e4a3a4?style=for-the-badge)

</div>

---

## 👋 What is this?

It's the [xBloom Panel](https://github.com/Mesh-one/Xbloom-panel) firmware by **Mansour Alshekhi**: an Elecrow touchscreen that sits next to your xBloom Studio and drives it over Bluetooth.

This fork adds a **Double Shot boot logo**, shown from the moment the panel powers on until it's ready. It also adds **one-command installers** for Mac and Windows that set everything up for you.

<div align="center">
  <img alt="The Double Shot boot screen on the panel" src="docs/images/boot-screen.png" width="520"><br>
  <sub>👆 What you'll see on the panel at power-on (800×480)</sub>
</div>

> 🫶 Unofficial personal project, not affiliated with xBloom. It uses xBloom's unofficial API, which may change at any time.

---

## 🧺 What you need

| | |
|---|---|
| 🖥️ **The panel** | **Elecrow CrowPanel Advance 4.3″ HMI** (ESP32-S3, 800×480 IPS). It must be the **Advance** model, not the 480×272 one. Buy from [AliExpress](https://a.aliexpress.com/_c3srIHtH) or the [Elecrow store](https://www.elecrow.com/crowpanel-advance-4-3-hmi-esp32-800x480-ai-display-ips-touch-artificial-intelligent-screen.html). |
| 🔌 **A USB-C *data* cable** | Charge-only cables won't work. A [90° one](https://amzn.eu/d/0hJzCoap) hides nicely behind the screen. |
| 💻 **A Mac or a Windows PC** | Internet connection, about 30–60 minutes the first time (mostly waiting ☕). |
| 🧩 **Case (optional)** | `case/Xbloom_screen_case_v1.stl`, all parts on one plate. Print in ABS, ASA or PETG. |

---

## 🚀 Install in one command

Plug the panel into your computer with the USB-C cable, then pick your computer below and paste **one line**. The installer explains every step as it goes and stops safely if something's missing.

### 🍎 Mac

Open **Terminal** (⌘ Space, type `Terminal`, press Enter) and paste:

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/2B-4G10/Xbloom-Panel/main/scripts/flash-mac.sh)
```

<details>
<summary>🔍 <b>What the Mac installer checks and does</b></summary>

| Step | What happens |
|---|---|
| 🛠️ Apple command-line tools | Needed for `git`. If missing, the installer opens Apple's installer and asks you to run it again afterwards. |
| 🍺 Homebrew | Finds it, or offers to install it from [brew.sh](https://brew.sh). |
| 🐍 Python 3.10–3.14 | The ESP32 tools don't support newer Python yet. If your Mac only has a newer one, it installs Python 3.13 alongside it. |
| ⚙️ PlatformIO 6.1.19 | The build tool, pinned to a version known to work with this board. It also repairs an existing install that uses the wrong Python. |
| 📟 esptool | Used to back up and flash the panel. |
| 🔌 USB driver | Detects the panel's **WCH CH34x** USB chip. If the driver is missing it installs it. If macOS is waiting for your approval, it opens the right Settings page: **General → Login Items & Extensions → Driver Extensions → CH34xVCPDriver**. |
| 🔎 Port | Finds the panel automatically and ignores Bluetooth headphones and similar ports. |
| 💾 Backup | Saves the panel's full 16 MB flash to `Desktop/xBloom-backups/` before touching anything (about 25 min). |
| 🏗️ Build | Compiles the firmware. The first build takes 10–30 min. |
| ⚡ Flash | Writes the new firmware at a safe 115200 baud. **Your Wi-Fi and xBloom login are kept.** |
| 📦 Update file | Copies `xbloom-panel-…-doubleshot-update.bin` to your Desktop for Wi-Fi updates. |

</details>

### 🪟 Windows

Open **PowerShell** (Start menu, type `PowerShell`, press Enter) and paste:

```powershell
irm https://raw.githubusercontent.com/2B-4G10/Xbloom-Panel/main/scripts/flash-windows.ps1 | iex
```

<details>
<summary>🔍 <b>What the Windows installer checks and does</b></summary>

| Step | What happens |
|---|---|
| 📦 winget | Windows' built-in app installer, used for everything below. If it's missing, install **App Installer** from the Microsoft Store. |
| 🌿 Git | Installed with winget if missing. |
| 🐍 Python 3.10–3.14 | Finds a suitable one, or installs Python 3.13. |
| ⚙️ PlatformIO 6.1.19 | Installed and pinned to the version that works with this board. |
| 📟 esptool | Used to back up and flash the panel. |
| 🔌 USB driver | Looks for the panel's **WCH** chip (USB ID `1A86`). If it has no COM port, the installer opens WCH's official driver page (CH341SER or CH343SER, whichever matches your chip). Run the EXE, click **INSTALL**, then replug the panel. |
| 🔎 Port | Finds the panel's `COM` port automatically. |
| 💾 Backup | Saves the full flash to `Desktop\xBloom-backups\` before touching anything. |
| 🏗️ Build · ⚡ Flash · 📦 Update file | Same as on the Mac. Your Wi-Fi and xBloom login are kept. |

</details>

### 🎛️ Options (both installers)

Set these before the command if you need them. On a Mac, put them in front, e.g. `XBP_NO_BACKUP=1 bash <(curl …)`. On Windows, run e.g. `$env:XBP_NO_BACKUP="1"` first.

| Option | What it does |
|---|---|
| `XBP_BUILD_ONLY=1` | Only build the Wi-Fi update file; don't touch the panel. |
| `XBP_NO_BACKUP=1` | Skip the backup (e.g. you already have one). |
| `XBP_PORT=…` | Use a specific port, e.g. `/dev/cu.wchusbserial110` or `COM5`. |
| `XBP_BAUD=…` | Serial speed. `115200` is the reliable default; try `57600` with a long or flaky cable. |

---

## 🛜 Updating later over Wi-Fi

No cable needed once the panel runs this firmware:

1. Get the update file: `xbloom-panel-…-doubleshot-update.bin`. The installer puts it on your Desktop, or run it with `XBP_BUILD_ONLY=1`.
2. On the panel: **Settings → Software update**. Open the link it shows on your phone and upload the file.

> ⚠️ The files in `firmware/` and the one-click web installer at mesh-one.github.io are the **original** builds **without** the Double Shot logo. Use the installers above to get the logo.

---

## ⚙️ First-time setup on the panel

1. **Settings → Wi-Fi**: pick your network and enter the password.
2. **Settings → xBloom account**: sign in with your xBloom app email and password. Your recipes sync down.
3. Turn on the machine. The panel connects over Bluetooth by itself 🎉

## 🌟 Features

- 📖 Your recipes (created and saved) from your xBloom account, with edits synced back to the app
- 💧 Live animated brewing screen: temperature, pour type, water, weight and time for every pour
- 😋 An "Enjoy" screen with the result when your coffee is ready
- ⚖️ Grinder, manual pour and scale controls with a stopwatch
- ✏️ Edit recipes or create new ones on the panel
- 🕰️ Dot-matrix idle clock (white, blue or brown) and adjustable sleep time
- 🔄 Recipe sync button and over-the-air updates

---

## 🧯 Troubleshooting

<details>
<summary><b>"Could not connect to an Espressif device" / it only finds <code>soundcoreLiberty…</code> or Bluetooth ports</b></summary>

The computer can't see the panel. Use a cable you know carries data (one you've used for file transfer), plug straight into the computer with no hub, and check the panel's screen is on. On a Mac, run `ls /dev/cu.*` before and after plugging in. A new `cu.wchusbserial…` should appear.
</details>

<details>
<summary><b>Mac: the panel shows up as "USB Serial" but there's no port</b></summary>

The WCH driver is missing or not approved. The installer handles this. To do it by hand:

```bash
brew install --cask wch-ch34x-usb-serial-driver
```

Open the **CH34xVCPDriver** app and click **Install**. Then go to **System Settings → General → Login Items & Extensions → Driver Extensions**, switch on **CH34xVCPDriver**, and replug the panel.
</details>

<details>
<summary><b>"Invalid head of packet … Possible serial noise or corruption"</b></summary>

The serial speed is too high for the cable or driver. The installers use 115200, which works in practice. If it still happens, try `XBP_BAUD=57600` and a shorter cable.
</details>

<details>
<summary><b>"Python version must be 3.10 … 3.14"</b></summary>

PlatformIO was installed with a Python that's too new. Run the installer again: it rebuilds PlatformIO's environment with a supported Python.
</details>

<details>
<summary><b>"No module named 'SCons.Tool.FortranCommon'"</b></summary>

The PlatformIO version is too new for this board's tools. Run the installer again: it pins PlatformIO to 6.1.19.
</details>

<details>
<summary><b>Stuck at <code>Connecting....</code></b></summary>

Put the panel in download mode: hold **BOOT**, tap **RESET**, release **BOOT**, then run the installer again. Press **RESET** once after flashing.
</details>

<details>
<summary><b>↩️ Restoring your backup</b></summary>

Mac (replace the port and file name with yours):

```bash
~/.esptool/bin/esptool --chip esp32s3 -p /dev/cu.wchusbserial110 -b 115200 write-flash 0 ~/Desktop/xBloom-backups/xbloom-panel-XXXX.bin
```

Windows:

```powershell
& "$HOME\.esptool\Scripts\esptool.exe" --chip esp32s3 -p COM5 -b 115200 write-flash 0 "$([Environment]::GetFolderPath('Desktop'))\xBloom-backups\xbloom-panel-XXXX.bin"
```

This puts the panel back exactly as it was when you made the backup, settings included.
</details>

---

## 🧑‍💻 Building by hand

```bash
pip install "platformio==6.1.19"   # in a Python 3.10–3.14 environment
pio run                            # first build compiles ESP-IDF with the settings in platformio.ini
```

The boot logo is `src/boot_logo.c`, a 720×189 8-bit alpha map that the firmware draws in white on black. `sim/` renders the UI on a desktop for layout checks.

## 📁 What's in here

| Folder | Contents |
|---|---|
| `src/`, `include/` | Firmware source (boot logo in `src/boot_logo.c`) |
| `scripts/` | The Mac and Windows one-command installers |
| `sim/` | Desktop preview of the UI |
| `case/` | The 3D-printable case (STL) |
| `docs/` | Original one-click web installer page, and images for this README |
| `firmware/` | Original prebuilt images (without the Double Shot logo) |

---

<details>
<summary>🇸🇦 <b>بالعربي: التثبيت بأمر واحد</b></summary>

وصّل الشاشة بالكمبيوتر بكيبل USB-C يدعم نقل البيانات، وبعدين:

**ماك:** افتح Terminal والصق:

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/2B-4G10/Xbloom-Panel/main/scripts/flash-mac.sh)
```

**ويندوز:** افتح PowerShell والصق:

```powershell
irm https://raw.githubusercontent.com/2B-4G10/Xbloom-Panel/main/scripts/flash-windows.ps1 | iex
```

البرنامج يتأكد من كل شي لحاله (بايثون، PlatformIO، تعريف الـ USB)، ويسوي نسخة احتياطية للشاشة على سطح المكتب قبل أي تغيير، وبعدين يبني البرنامج ويثبّته. إعدادات الواي فاي وحساب xBloom تبقى زي ما هي.
</details>

<div align="center">

---

Made with ☕ by **Double Shot** · firmware by [Mansour Alshekhi](https://github.com/Mesh-one/Xbloom-panel)

MIT licensed (see `LICENSE`) · third-party code: `THIRD_PARTY_NOTICES.md`

</div>
