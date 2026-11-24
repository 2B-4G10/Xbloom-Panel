<!--
Notes for the next release, written for the people using the panel. The weekly
release puts them on the release page and in CHANGELOG.md, then empties this file.
Keep it friendly and short: an emoji heading per change, a line or two each.
Example:

### 🍓 New starter recipe
Berry Bomb, for fruity natural coffees, under Recipes → Starter.
-->

> [!IMPORTANT]
> **Fix release.** 4.0 and 4.1 could stop at start-up on a real panel, leaving a lit but blank screen. **4.1.1 fixes it.** Please update.

### 🐞 Fixed
- 🖥️ **Blank screen at start-up:** the panel's main program ran out of working memory (its stack) while drawing the first screen and restarted over and over. It now has three times the room, measured to leave plenty spare.

### 🛠️ Improved
#### 💻 One-command install: minutes, not an hour
- ⚡ **No building:** the command now downloads the ready-made firmware and Espressif's flashing tool. Nothing is compiled and nothing else is installed, so antivirus blocks and long builds are gone.
- 🚀 **Fastest safe speed:** it tries 921600, then 460800, then 115200 baud, whichever your cable handles. Flashing takes about a minute.
- ✅ **Checks its work:** every download is checksum-verified. After flashing it watches the panel start up and tells you if anything goes wrong, instead of just saying "Done".
- ⏭️ **Already up to date?** It notices in seconds and stops.
- 🔐 **Settings kept:** Wi-Fi and your xBloom sign-in stay. Use `XBP_FRESH=1` to start from scratch.
- 🧰 Building from source is still there for those who want it: `XBP_BUILD=1`.

### 🔄 Updating
- **Panel showing a blank screen?** Plug it in by USB and paste the README's one-line command. It installs 4.1.1 in a few minutes.
- **Otherwise:** use **Settings → Software update** as usual.
