<!--
Notes for the next release, written for the people using the panel. The weekly
release puts them on the release page and in CHANGELOG.md, then empties this file.
Keep it friendly and short: an emoji heading per change, a line or two each.
Example:

### 🍓 New starter recipe
Berry Bomb, for fruity natural coffees, under Recipes → Starter.
-->

> [!NOTE]
> **Diagnostics release.** It helps track down a restart some panels have when opening a recipe.

### 🔧 Under the hood
- 🩺 **Crash reports that survive a restart:** if the panel ever crashes, it saves where, and prints it the next time it starts. It also prints why it last restarted: crash, watchdog, or a dip in USB power.
- 💻 **Watch the panel from the installer:** set `XBP_MONITOR=1` before the README command to see and save everything the panel prints, for example to report a problem.

### 🔄 Updating
Use the README's one-line USB command this time, so the panel also gets room for the crash reports. A Wi-Fi update works too, just without the saved crash reports.
