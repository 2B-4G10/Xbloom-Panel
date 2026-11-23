# Changelog

All the good stuff, newest first. ☕ Every version is on the [releases page](https://github.com/2B-4G10/Xbloom-Panel/releases).

## ☕ [4.1.0 · build 18](https://github.com/2B-4G10/Xbloom-Panel/releases/tag/v4.1.0-ds.18) · 2026-11-23

> [!TIP]
> **Highlights:** 💻 an easier, faster one-command install on Windows and Mac · 🔄 same 4.1.0 panel firmware

### 🛠️ Improved
#### 💻 One-command install, smoother
- 🔑 **Signing in to GitHub is one step:** no more menus. It shows a code, you press Enter and approve it in the browser. If you're already signed in, this step is skipped.
- 💬 **Clear messages:** if your GitHub account hasn't been invited yet, it tells you so in red instead of failing silently.
- ⚡ **About 25 minutes faster:** the panel backup is now off by default. Want one anyway? Set `XBP_BACKUP=1` first (see the README).
- 🧹 **Picks up where it left off:** a half-finished earlier download no longer blocks the next try.

#### 📖 README
- 🖼️ The Double Shot logo now shows in the GitHub mobile app and in both light and dark themes.

### 🧪 Tested before release
- ✅ The README's install commands run as written on fresh Windows and Mac machines, with sign-in, download, every tool and the full firmware build all passing.
- ✅ The emulator walk-through of the panel still passes: 70 of 70 steps.

### 🔄 Updating
The panel firmware is the same as 4.1.0, so a panel that's already on 4.1.0 doesn't need this update. It's here for the easier install.


## ☕ [4.1.0 · build 17](https://github.com/2B-4G10/Xbloom-Panel/releases/tag/v4.1.0-ds.17) · 2026-11-23

> [!TIP]
> **Highlights:** 😋 colour emoji everywhere · ☕ five new starter recipes · 🧪 every release now tested in an emulator

### ✨ New
#### 😋 Emoji, everywhere
Every emoji shows in full colour, on every screen, just like on your phone:
- 😀 smileys and ❤️ hearts
- 👍🏽 skin tones and 👨‍👩‍👧‍👦 families
- 👩🏽‍💻 jobs, 1️⃣ keycaps and 🇸🇦 flags (4,012 in all)

Give a recipe an emoji name and the panel saves it exactly as you typed it.

#### ☕ Five new starter recipes
Under **Recipes → Starter**, next to Daily Pour Over, Light Roast and Dark Roast:

| | Recipe | Best for | Dose · Water |
|---|---|---|---|
| 🍓 | **Berry Bomb** | Fruity naturals: hot, fine, small pours to lift the berries | 15 g · 240 ml |
| 🍫 | **Chocolate Hug** | Chocolatey medium-dark beans: a round, heavy cup | 16 g · 240 ml |
| 🍯 | **Honey Morning** | Washed medium roasts: long bloom, slow middle pour | 15 g · 230 ml |
| 🧊 | **Iced Sunrise** | Iced pour over: brew strong onto about 120 g of ice | 20 g · 180 ml |
| 😋 | **Sweet Spot** | Any beans, any day: balanced 1:16 in four even pours | 15 g · 240 ml |

### 🐞 Fixed
- 🌐 **Arabic:** the temperature and pour pattern under each pour on the recipe page were cut off. They now show in full.

### 🧪 Tested before release
The real firmware now runs in an emulator on every change and taps through the whole panel:
- ✅ Start-up, Grinder, Brewer and Scale
- ✅ A full brew with pause and resume
- ✅ The recipe editor: an emoji name, then saving it
- ✅ Wi-Fi: a wrong password, then the right one
- ✅ Signing in, software update, the Double Shot page and the clock
- ✅ Restarts into Arabic and back to English

**70 of 70 steps passed.**

### 🔧 Under the hood
- 📦 Firmware is about 1.5 MB bigger (the emoji pictures). It still fits the update slot easily, so Wi-Fi updates work as before.


## ☕ [4.0.0 · build 16](https://github.com/2B-4G10/Xbloom-Panel/releases/tag/v4.0.0-ds.16) · 2026-11-23

> [!IMPORTANT]
> **A fresh start.** xBloom Panel by Double Shot is rebuilt from the ground up as Double Shot's own firmware. It's the same panel you know, now cleaner, faster to improve and ready for what's next.

### ✨ Everything the panel does
- ☕ **Your recipes:** Created and Shared, straight from your xBloom account. Load, start, pause and stop, with the dot-matrix brewing scene.
- ✏️ **Edit on the panel:** change or create recipes on the screen. Edits sync back to your account.
- ⚙️ **Grinder, Brewer and Scale:** the machine's manual modes, with drag-to-set controls, a timer and tare.
- 🌐 **English or العربية:** full Arabic with right-to-left screens.
- 🕰️ **Idle clock:** a dot-matrix clock instead of a dark screen, off at night if you like.
- 🛜 **Wi-Fi updates:** install new versions from your phone. A version that fails to start rolls back by itself.
- 🛍️ **Double Shot tab:** the store page, with QR codes for the shop and WhatsApp.

### 🔄 Updating
Coming from 3.3.1? Use **Settings → Software update** as usual. Your Wi-Fi, xBloom sign-in and screen settings are kept.


## ☕ [3.3.1 · build 15](https://github.com/2B-4G10/Xbloom-Panel/releases/tag/v3.3.1-ds.15) · 2026-11-23

> [!TIP]
> **Highlight:** 🇸🇦 the panel now speaks **العربية**

### ✨ New
- 🌐 **Arabic language:** every screen translated, laid out right to left.
- 🔢 **Your choice of numbers** under **Settings → Language**:
  - **العربية · وحدات عربية:** Arabic numbers and units too (٨٠ مل, ٩٣°م)
  - **العربية · وحدات إنجليزية:** Arabic text, with numbers and units kept as 80 ml and 93°C
- 🔤 **Arabic fonts:** IBM Plex Sans Arabic, with letters joined properly.

### 🛠️ Improved
- 📏 **Whole names:** recipe names and labels always fit, with no more "…".
- 🧩 **No overlaps:** text and buttons rearrange so nothing collides, on every tab.

### 🔧 Under the hood
- 🗓️ Weekly automation, one job a day: watch the original project (Sun), dependency updates (Mon), platform check (Tue), release (Wed).


## ☕ [3.3.1 · build 12](https://github.com/2B-4G10/Xbloom-Panel/releases/tag/v3.3.1-ds.12) · 2026-11-21

> [!TIP]
> **Highlights:** 🛍️ the Double Shot tab · 📦 clearer download names

### ✨ New
- 🛍️ **Double Shot tab:** a full-screen store page with QR codes for the shop and WhatsApp.

### 🛠️ Improved
- 📦 **Clearer files:** each download is named after how you install it:
  - 🛜 `UPDATE-WiFi-or-USB` for the panel's update page
  - 🔌 `FULL-USB-ONLY` for a first install over USB
- 📖 **Simpler README:** what the panel does and how to install it, in a few steps.

### 🔧 Under the hood
- 🤖 Automatic releases, dependency updates and a watch on the original project.


## ☕ [3.3.1 · build 10](https://github.com/2B-4G10/Xbloom-Panel/releases/tag/v3.3.1-ds.10) · 2026-11-21

> [!TIP]
> **Highlight:** 🛍️ meet the new Double Shot tab

### ✨ New
- 🛍️ **Double Shot tab:** a full-screen store page with QR codes for the shop and WhatsApp, one tap from Home.

### 🔧 Under the hood
- 🔄 Build tools updated.


## ☕ [3.3.1 · build 7](https://github.com/2B-4G10/Xbloom-Panel/releases/tag/v3.3.1-ds.7) · 2026-11-21

> [!TIP]
> **Highlights:** 🖼️ Double Shot boot logo · 💻 one-command installers · 🧰 printable case

### ✨ New
- 🖼️ **Double Shot boot logo:** the panel starts with "By: DOUBLE SHOT".
- 💻 **One-command installers** for Mac and Windows: plug in the panel, paste one line, done.
- 🧰 **3D-printable case (STL)** and a link to the 90° USB-C cable.

### 📦 Based on xBloom Panel 3.3.1 by Mansour Alshekhi
- 🕰️ A steadier clock
- 🎛️ Drop-downs in Settings
- 🔄 A Sync button for your recipes

### 🔧 Under the hood
- 🤖 Automatic firmware builds and releases, and dependency updates.


## 🌱 Where it started

The original xBloom Panel by Mansour Alshekhi:
- **3.3.1**: a steadier clock, drop-downs in Settings and a Sync button
- **3.3.0**: the first firmware, a one-click installer and the case
