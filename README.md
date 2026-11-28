<p align="center">
  <a href="README.ar.md">العربية</a> · <b>English</b> · <a href="README.zh-CN.md">简体中文</a>
</p>

<div align="center">

<img alt="By: Double Shot — Speciality Coffee / Bareista Tools" src="docs/images/logo.png" width="520">

# ☕ xBloom Panel by Double Shot

**A touchscreen for your xBloom Studio: recipes, brewing, grinder and scale at your fingertips, in English or العربية.**

[![Downloads](https://img.shields.io/github/downloads/2B-4G10/Xbloom-Panel/total?label=%E2%AC%87%20downloads&color=34c759&style=for-the-badge)](https://github.com/2B-4G10/Xbloom-Panel/releases)
[![Latest release](https://img.shields.io/github/v/release/2B-4G10/Xbloom-Panel?label=%E2%98%95%20latest&color=c8a27e&style=for-the-badge)](https://github.com/2B-4G10/Xbloom-Panel/releases/latest)
[![This version's downloads](https://img.shields.io/github/downloads/2B-4G10/Xbloom-Panel/latest/total?label=%F0%9F%9A%80%20this%20version&color=0a84ff&style=for-the-badge)](https://github.com/2B-4G10/Xbloom-Panel/releases/latest)
[![Support on Patreon](https://img.shields.io/badge/Support-Patreon-F96854?style=for-the-badge&logo=patreon&logoColor=white)](https://www.patreon.com/c/Dossary)

</div>

---

## 👋 What is it?

A touchscreen that sits next to your **xBloom Studio** and runs it over Bluetooth. It's an **Elecrow CrowPanel Advance 4.3″** with this firmware on it.

- ☕ **Your recipes**, from your xBloom account, with a live dot-matrix brewing scene
- 🍓 **8 starter recipes**, including 🍓 Berry Bomb, 🍫 Chocolate Hug and 🧊 Iced Sunrise
- ✏️ **Edit recipes on the screen**, synced back to your account
- ⚙️ **Grinder, Brewer and Scale** controls
- 🌐 **English or العربية**, with Arabic or English numbers
- 😋 **Every emoji** in colour
- 🔄 **Updates itself over Wi-Fi**: one tap, or fully automatic
- 🕰️ **Idle clock**, and a silent dimmed standby
- 🛍️ **Double Shot tab** with QR codes for the shop

<div align="center">
  <img alt="Boot screen" src="docs/images/boot-screen.png" width="400">
  <img alt="Double Shot tab" src="docs/images/store-page.png" width="400">
</div>

> Unofficial: not affiliated with xBloom.

**You need:** a CrowPanel **Advance** 4.3″ (800×480) from [AliExpress](https://a.aliexpress.com/_c3srIHtH) or [Elecrow](https://www.elecrow.com/crowpanel-advance-4-3-hmi-esp32-800x480-ai-display-ips-touch-artificial-intelligent-screen.html), and a USB-C cable that carries data.

---

## 🚀 Install

**Flash it once by cable. After that it updates itself over Wi-Fi.** Your recipes and settings always stay.

Plug the panel into your computer and paste one line:

**🍎 Mac** (Terminal):

```bash
curl -fsSL https://raw.githubusercontent.com/2B-4G10/Xbloom-Panel/main/scripts/flash-mac.sh -o /tmp/xbp.sh && bash /tmp/xbp.sh
```

**🪟 Windows** (PowerShell):

```powershell
irm https://raw.githubusercontent.com/2B-4G10/Xbloom-Panel/main/scripts/flash-windows.ps1 -OutFile "$env:TEMP\xbp.ps1"; powershell -ExecutionPolicy Bypass -File "$env:TEMP\xbp.ps1"
```

<details>
<summary>⚙️ Options</summary>

Set before the command: Mac `XBP_BACKUP=1 bash …` · Windows `$env:XBP_BACKUP="1"`

| Option | Effect |
|---|---|
| `XBP_BACKUP=1` | Back up the panel to `Desktop/xBloom-backups` first |
| `XBP_FRESH=1` | Erase everything too (Wi-Fi and xBloom sign-in reset) |
| `XBP_PORT=…` | Use this port (`/dev/cu.wchusbserial110`, `COM5`) |
| `XBP_BAUD=115200` | Use one flashing speed (normally it tries 921600, then 460800, then 115200) |
| `XBP_BUILD=1` | Build the firmware from source instead of downloading it (30–60 minutes, installs Python and the build tools) |

</details>

<details>
<summary>🌐 No terminal? Flash from the browser</summary>

1. Download [**the USB file**](https://github.com/2B-4G10/Xbloom-Panel/releases/latest/download/xbloom-panel-doubleshot-FULL-USB-ONLY.bin) and plug in the panel.
2. In **Chrome** or **Edge**, open **https://espressif.github.io/esptool-js/**
3. Baudrate `115200` → **Connect** → pick the panel's port.
4. Flash Address `0x0` → choose the file → **Program**, then press **RESET**.

⛔ This file is for USB only.
</details>

---

## ⚙️ After installing

1. **Settings → Wi-Fi:** pick your network.
2. **Settings → xBloom account:** sign in with your xBloom app email and password.
3. Turn on the xBloom Studio. The panel connects by itself.
4. **Settings → Language:** English or العربية.

---

## 🔄 Updates

When a new version is out, the panel asks. Tap **Ok** to install, or pick **Always Update Automatically** and forget about it. Each download is verified, and if an update ever fails to start, the panel goes back to the last version by itself.

Change this anytime in **Settings → Software update**.

---

## 🧯 Help

<details>
<summary><b>The computer doesn't see the panel</b></summary>

Use a data cable plugged straight into the computer (no hub), and install the USB driver:
- **Mac:** the [WCH driver](https://www.wch-ic.com/downloads/CH34XSER_MAC_ZIP.html), then allow it in **System Settings → General → Login Items & Extensions → Driver Extensions**.
- **Windows:** [CH341SER](https://www.wch-ic.com/downloads/CH341SER_EXE.html).

Then unplug and replug the panel.
</details>

<details>
<summary><b>Stuck at "Connecting…" or "serial noise"</b></summary>

Hold **BOOT**, tap **RESET**, release **BOOT**, and try again. With a long cable, use speed `57600`.
</details>

<details>
<summary><b>The one-command install stopped</b></summary>

Paste the same command again. It checks every download and starts clean each time. If it can't talk to the panel, close other programs using it (Arduino IDE, serial monitors), or hold **BOOT**, tap **RESET**, release **BOOT**, and try again.
</details>

<details>
<summary><b>↩️ Restore a backup</b></summary>

Use **Flash from the browser** with your backup file (from `Desktop/xBloom-backups`) at address `0x0`.
</details>

<details>
<summary><b>🤖 How it stays up to date</b></summary>

Every week: Sunday checks the [original project](https://github.com/Mesh-one/Xbloom-panel) for changes to review, Monday and Tuesday test and apply tool updates, and Wednesday publishes a [release](https://github.com/2B-4G10/Xbloom-Panel/releases) if anything changed (see the [changelog](CHANGELOG.md)).
</details>

---

<details>
<summary>🇸🇦 <b>بالعربي باختصار</b></summary>

- **التثبيت مرة وحدة بالكيبل، وبعدها التحديثات بالواي فاي تلقائياً.** وصفاتك وإعداداتك تبقى دائماً.
- **بأمر واحد:** وصّل اللوحة بالكمبيوتر وانسخ أمر الماك أو الويندوز من فوق.
- **أو من المتصفح بدون برامج:** نزّل [ملف الكيبل](https://github.com/2B-4G10/Xbloom-Panel/releases/latest/download/xbloom-panel-doubleshot-FULL-USB-ONLY.bin)، افتح https://espressif.github.io/esptool-js/ في كروم، السرعة `115200` ثم **Connect**، العنوان `0x0`، ثم **Program**. ⛔ لا ترفعه من صفحة الواي فاي.
- **بعد التثبيت:** لما يطلع إصدار جديد تسألك اللوحة. اضغط **موافق**، أو اختر **حدّث تلقائياً دائماً** وارتاح.
</details>

<div align="center">

---

Made with ☕ by **Double Shot** · © Double Shot, all rights reserved ([`LICENSE`](LICENSE)) · credits and third-party licences: `THIRD_PARTY_NOTICES.md`

<br>

### 🇸🇦 ❤️ ☕

**Made By: [@i_2B_4G10](https://t.me/i_2B_4G10)**

[![Support on Patreon](https://img.shields.io/badge/Support_me_on-Patreon-F96854?style=for-the-badge&logo=patreon&logoColor=white)](https://www.patreon.com/c/Dossary)

</div>
