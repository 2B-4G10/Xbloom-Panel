<!--
Notes for the next release, written for the people using the panel. The weekly
release puts them on the release page and in CHANGELOG.md, then empties this file.
Keep it friendly and short: an emoji heading per change, a line or two each.
Example:

### 🍓 New starter recipe
Berry Bomb, for fruity natural coffees, under Recipes → Starter.
-->

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
