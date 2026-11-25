<!--
Notes for the next release, written for the people using the panel. The weekly
release puts them on the release page and in CHANGELOG.md, then empties this file.
Keep it friendly and short: an emoji heading per change, a line or two each.
Example:

### 🍓 New starter recipe
Berry Bomb, for fruity natural coffees, under Recipes → Starter.
-->

> [!IMPORTANT]
> **Fix release: opening a recipe no longer restarts the panel.** Please update.

### 🐞 Fixed
- ☕ **Opening any recipe restarted the panel.** The crash report pinned it down: the compiler kept a colour value among the program's instructions and read it in a way the ESP32-S3 doesn't allow there. The colours are now built a different way.
- 🛡️ **It can't slip back in:** every build now scans the whole firmware for that kind of read and refuses to publish if it finds one.

### 🔄 Updating
- **Panel restarting when you open a recipe?** Use **Settings → Software update** with the new Wi-Fi update file, or paste the README's one-line USB command.
