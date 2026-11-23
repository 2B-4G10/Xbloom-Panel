<div align="center">

<img alt="By: Double Shot — Speciality Coffee / Bareista Tools" src="docs/images/logo.png" width="520">

# ☕ xBloom Panel by Double Shot

**A touchscreen for your xBloom Studio: recipes, brewing, grinder and scale at your fingertips, in English or العربية.**

</div>

---

## 👋 What is it?

Firmware for an **Elecrow CrowPanel Advance 4.3″** touchscreen that sits next to your **xBloom Studio** and controls it over Bluetooth.

| | |
|---|---|
| ☕ **Brew your recipes** | Your xBloom account's recipes (Created and Shared) on the panel. Load, start, pause and stop, with a live dot-matrix brewing scene. |
| 🍓 **Eight starter recipes** | Daily Pour Over, Light and Dark Roast, plus 🍓 Berry Bomb, 🍫 Chocolate Hug, 🍯 Honey Morning, 🧊 Iced Sunrise and 😋 Sweet Spot, always under Recipes → Starter. |
| 😋 **Emoji** | Every emoji in colour: skin tones, families, professions, keycaps and flags, in recipe names and everywhere else. |
| ✏️ **Edit on the panel** | Change or create recipes right on the screen. Edits sync back to your account. |
| ⚙️ **Grinder, Brewer, Scale** | The machine's manual modes, with drag-to-set controls, a timer and tare. |
| 🌐 **English or العربية** | Full Arabic with right-to-left screens. Choose Arabic numbers and units (٨٠ مل) or English ones (80 ml). |
| 🕰️ **Idle clock** | A dot-matrix clock instead of a dark screen (white, blue or brown), off at night if you like. |
| 🛜 **Wi-Fi updates** | Install new versions from your phone. A version that fails to start rolls back by itself. |
| 🛍️ **Double Shot tab** | The store page, with QR codes for [doubleshot-sa.com](https://doubleshot-sa.com) and WhatsApp. |

<div align="center">
  <img alt="Boot screen" src="docs/images/boot-screen.png" width="400">
  <img alt="Double Shot tab" src="docs/images/store-page.png" width="400">
</div>

> Unofficial: not affiliated with xBloom. The idea comes from [xBloom Panel by Mansour Alshekhi](https://github.com/Mesh-one/Xbloom-panel); this firmware is Double Shot's own code.

---

## 🧺 What you need

- 🖥️ **Elecrow CrowPanel Advance 4.3″** (the **Advance** 800×480 model): [AliExpress](https://a.aliexpress.com/_c3srIHtH) · [Elecrow](https://www.elecrow.com/crowpanel-advance-4-3-hmi-esp32-800x480-ai-display-ips-touch-artificial-intelligent-screen.html)
- 🔌 **A USB-C cable that carries data.** Charge-only cables won't work.
- 🔑 **Access to this repository.** It is private, so sign in to GitHub with an account that has been given access.

---

## 📦 The two firmware files

Download them from the [**latest release**](https://github.com/2B-4G10/Xbloom-Panel/releases/latest). Each name tells you how to install it:

| File | Install it with | Your settings |
|---|---|---|
| 🛜 `xbloom-panel-doubleshot-UPDATE-WiFi-or-USB.bin` | **Wi-Fi** from the panel's settings (Method 1) | ✅ Kept |
| 🔌 `xbloom-panel-doubleshot-FULL-USB-ONLY.bin` | **USB cable only** (Method 2) | ⚠️ Wi-Fi and xBloom login reset |

> ⛔ **Never upload the FULL-USB-ONLY file on the Wi-Fi update page.** It only works over USB.

---

## 🚀 How to install (pick one)

### 1️⃣ Wi-Fi update: easiest, if the panel already runs xBloom Panel

1. Download **`xbloom-panel-doubleshot-UPDATE-WiFi-or-USB.bin`** from the [latest release](https://github.com/2B-4G10/Xbloom-Panel/releases/latest) to your phone or computer.
2. On the panel, go to **Settings → Software update**.
3. Open the address the panel shows (on a phone or computer on the same Wi-Fi), choose the file, and upload it.
4. The panel restarts with the new firmware. Your settings stay. 🎉

### 2️⃣ USB with the `.bin` file: no software to install

Use this for a new panel, or to recover one.

1. Download **`xbloom-panel-doubleshot-FULL-USB-ONLY.bin`** from the [latest release](https://github.com/2B-4G10/Xbloom-Panel/releases/latest).
2. Connect the panel to your computer with the USB-C cable.
3. In **Chrome** or **Edge**, open Espressif's web flasher: **https://espressif.github.io/esptool-js/**
4. Set **Baudrate** to `115200`, click **Connect**, and pick the panel's port. On a Mac it's named `wchusbserial…`.
5. Set **Flash Address** to `0x0`, choose the **FULL-USB-ONLY** file, and click **Program**.
6. When it finishes, press the panel's **RESET** button, then set up Wi-Fi and your xBloom account again.

> 💡 **No port shows up?** The panel's USB chip (WCH CH34x) needs a driver on some computers. **Mac:** install the [WCH driver](https://www.wch-ic.com/downloads/CH34XSER_MAC_ZIP.html), then turn it on in **System Settings → General → Login Items & Extensions → Driver Extensions**. **Windows:** install [CH341SER](https://www.wch-ic.com/downloads/CH341SER_EXE.html). Then replug the panel.

### 3️⃣ One command: builds from source

> [!IMPORTANT]
> The project is private. You need a free [GitHub account](https://github.com/signup) that **Double Shot has invited** to `2B-4G10/Xbloom-Panel` (accept the invite from your email first). Without it, the download step stops with a message saying so.

This method installs everything it needs, builds the latest code and flashes it. Your settings are kept. The first run takes 30–60 minutes, mostly waiting for the build ☕.

**🍎 Mac.** Open **Terminal** and paste:

```bash
brew install gh && { gh auth status >/dev/null 2>&1 || gh auth login -h github.com -p https -w; } && { [ -d ~/Xbloom-Panel/.git ] || gh repo clone 2B-4G10/Xbloom-Panel ~/Xbloom-Panel || { echo "❌ This GitHub account has no access to Xbloom-Panel yet: ask Double Shot to invite it."; false; }; } && bash ~/Xbloom-Panel/scripts/flash-mac.sh
```

**🪟 Windows.** Open **PowerShell** and paste:

```powershell
winget install -e --id GitHub.cli --accept-source-agreements --accept-package-agreements; winget install -e --id Git.Git --accept-source-agreements --accept-package-agreements; $env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User'); gh auth status *> $null; if ($LASTEXITCODE -ne 0) { gh auth login -h github.com -p https -w }; if (-not (Test-Path "$HOME\Xbloom-Panel\.git")) { gh repo clone 2B-4G10/Xbloom-Panel "$HOME\Xbloom-Panel" }; if (Test-Path "$HOME\Xbloom-Panel\scripts\flash-windows.ps1") { powershell -ExecutionPolicy Bypass -File "$HOME\Xbloom-Panel\scripts\flash-windows.ps1" } else { Write-Host 'This GitHub account has no access to Xbloom-Panel yet: ask Double Shot to invite it.' -ForegroundColor Red }
```

What happens:
- 🔑 **Sign in to GitHub (first time only).** It shows a one-time code like `ABCD-1234`: press **Enter**, a browser opens, paste the code and approve. If it asks *Authenticate Git with your GitHub credentials?*, answer **Y**. Next time this step is skipped.
- The script then checks and installs everything it needs: Python, the build tools, esptool and the USB driver. It explains each step as it goes.
- Want a copy of the panel's current firmware first? Add the `XBP_BACKUP=1` option below (about 25 minutes more). You can always go back with the FULL-USB-ONLY file anyway.
- It flashes the new firmware, and also leaves the Wi-Fi update file on your Desktop.

On a Mac, if `brew` isn't found, install [Homebrew](https://brew.sh) first.

**To run it again later:** Mac: `bash ~/Xbloom-Panel/scripts/flash-mac.sh` · Windows: `powershell -ExecutionPolicy Bypass -File "$HOME\Xbloom-Panel\scripts\flash-windows.ps1"`

<details>
<summary>⚙️ Options for the command method</summary>

Put these before the command. On a Mac: `XBP_BACKUP=1 bash …`. On Windows, run e.g. `$env:XBP_BACKUP="1"` first.

| Option | Effect |
|---|---|
| `XBP_BUILD_ONLY=1` | Only build the Wi-Fi update file; don't touch the panel |
| `XBP_BACKUP=1` | Back up the panel to `Desktop/xBloom-backups` before flashing (about 25 minutes) |
| `XBP_PORT=…` | Use a specific port (`/dev/cu.wchusbserial110`, `COM5`) |
| `XBP_BAUD=…` | Serial speed (default `115200`; try `57600` with a long cable) |

</details>

---

## ⚙️ After installing

1. **Settings → Wi-Fi:** pick your network.
2. **Settings → xBloom account:** sign in with your xBloom app email and password.
3. Turn on the xBloom Studio. The panel connects by itself over Bluetooth.
4. **Settings → Language:** English or العربية (Arabic, right-to-left). The panel restarts in the new language.

---

## 🧯 Help

<details>
<summary><b>The computer doesn't see the panel</b></summary>

Use a data cable, plug straight into the computer (no hub), and install the USB driver (see the 💡 tip in Method 2). On a Mac, `ls /dev/cu.*` should list a `cu.wchusbserial…` port.
</details>

<details>
<summary><b>"Invalid head of packet … serial noise"</b></summary>

The serial speed is too high. Use `115200`, or `57600` with a shorter cable.
</details>

<details>
<summary><b>Stuck at "Connecting…"</b></summary>

Hold **BOOT**, tap **RESET**, release **BOOT**, and try again. Press **RESET** once after flashing.
</details>

<details>
<summary><b>↩️ Restore your backup</b></summary>

Use **Method 2**, but choose your backup file (in `Desktop/xBloom-backups`) instead of the FULL-USB-ONLY file, at address `0x0`. The panel returns exactly to how it was, settings included.
</details>

---

## 🤖 How it stays up to date

| When | What happens automatically |
|---|---|
| **Sunday** | The [original project](https://github.com/Mesh-one/Xbloom-panel) is checked for new changes. Nothing is merged: if there's something new, GitHub emails you a summary to review, with the full changes attached to the run. After review, run **Watch the original repo** with **Mark the current upstream as reviewed** ticked. |
| **Monday** | Dependabot updates the build automation. Each update is test-built and merged automatically if it passes. |
| **Tuesday** | The ESP32 platform is updated with its matching build tool, test-built first and added only if it passes. A platform version that doesn't build yet is skipped and noted in `.github/essentials-hold.txt`. |
| **Wednesday** | If anything changed that week, a new [release](https://github.com/2B-4G10/Xbloom-Panel/releases) is published with both `.bin` files and a [changelog](CHANGELOG.md). |

<details>
<summary>🧑‍💻 Building by hand</summary>

```bash
python3 -m venv ~/.platformio/penv                              # Python 3.10–3.14
~/.platformio/penv/bin/pip install -r requirements-build.txt   # build tools
~/.platformio/penv/bin/pio run                                 # first build takes a while
```

Where things are:

| | |
|---|---|
| `src/ui.cpp` | every screen (the store page is `build_store()`) |
| `src/dotfx.cpp` | the dot-matrix clock and brewing scenes |
| `src/i18n.cpp` | the Arabic translations |
| `src/emoji.cpp` | colour emoji (images in `src/emoji_data.c`, made by `tools/emoji/make_emoji.py`) |
| `include/recipes.h` | the starter recipes |
| `src/xbloom_protocol.cpp` | the machine's Bluetooth protocol |
| `src/library.cpp`, `src/cloud.cpp` | recipes and the xBloom account |
| `src/boot_logo.c` | the boot logo |
| `sim/` | previews every screen on a computer (`SIM_LANG=ar` for Arabic, `sim emoji` for the emoji screens) |
| `test_native/run.sh` | checks the protocol, recipe and encryption code against an earlier version, and every emoji |
</details>

---

<details>
<summary>🇸🇦 <b>بالعربي باختصار</b></summary>

**ملفات البرنامج** (من صفحة [آخر إصدار](https://github.com/2B-4G10/Xbloom-Panel/releases/latest)):
- **`UPDATE-WiFi-or-USB`**: للتحديث عن طريق الواي فاي من **الإعدادات ← تحديث البرنامج**. إعداداتك تبقى.
- **`FULL-USB-ONLY`**: بالكيبل فقط (أول تثبيت أو إصلاح). ⛔ لا ترفعه من صفحة تحديث الواي فاي. يمسح إعدادات الواي فاي والحساب.

**التثبيت بالكيبل بدون برامج:** افتح https://espressif.github.io/esptool-js/ في كروم، اختر السرعة `115200` ثم **Connect**، والعنوان `0x0`، وملف **FULL-USB-ONLY**، ثم **Program**.

**أو بأمر واحد** (يسوي نسخة احتياطية ويبني ويثبّت): انسخ أمر الماك أو الويندوز من الطريقة 3 فوق.
</details>

<div align="center">

---

Made with ☕ by **Double Shot** · MIT licensed (`LICENSE`) · credits and third-party licences: `THIRD_PARTY_NOTICES.md`

</div>
