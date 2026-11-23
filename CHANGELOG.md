# Changelog

## [v4.1.0-ds.17](https://github.com/2B-4G10/Xbloom-Panel/releases/tag/v4.1.0-ds.17) · 2026-11-23

- Emulator build: QEMU's Ethernet card for real internet, so sign-in talks to xBloom (c806a81)
- Emulator test: match Arabic labels in their joined forms, close the finished scene and the store page, tap list rows (250292f)
- Emulator test: list what the screen shows when a step fails, and print every step (620aa05)
- Emulator build: larger loop stack, stack use in the start-up log, keep the ELF for crash reports (7b42651)
- Emulator build: declare the fonts outside the file's namespace (85dd4a7)
- Emulator test: run the firmware in QEMU and walk through every screen (054fd7b)
- Colour emoji and five new starter recipes (cdce6c7)

## [v4.0.0-ds.16](https://github.com/2B-4G10/Xbloom-Panel/releases/tag/v4.0.0-ds.16) · 2026-11-23

- xBloom Panel by Double Shot 4.0.0 (a7f0dfe)

## [v3.3.1-ds.15](https://github.com/2B-4G10/Xbloom-Panel/releases/tag/v3.3.1-ds.15) · 2026-11-23

- Arabic numbers and units, whole names, no overlaps (3d225ee)
- Arabic language and right-to-left layout (2392f75)
- Watch the original repo instead of importing it (bf04e6a)
- Run all automation weekly, one job per day (27a987b)
- Hold ESP32 platform at 55.03.39: 61.04.00 doesn't build yet (99aebe8)
- Report sync and platform problems without GitHub Issues (dd99435)

## [v3.3.1-ds.12](https://github.com/2B-4G10/Xbloom-Panel/releases/tag/v3.3.1-ds.12) · 2026-11-21

- Automate upstream sync, weekly releases and dependency updates (11b5ed3)
- Name release files by install method; simpler README (e86e97a)
- deps(actions): bump the github-actions group with 5 updates (3a6d589)
- Pin pioarduino 6.1.19 to match the platform's SCons 4.8.1 (9ffa408)
- Add a Double Shot tab: full-screen store page with QR codes (6145d65)
- Build with the pioarduino core in ~/.platformio/penv (bad962f)
- Automate firmware releases, upstream sync and Dependabot (ec27fc6)
- Add Mac/Windows one-command installers, fix esp-modbus build, new README (66a2b87)
- Add Double Shot boot logo (1cf2b68)
- Installer page: cable link under step 1 (629d9dc)
- Installer page: link to the case print file (98fe20f)
- Case v1 (STL) and the 90-degree USB-C cable link (17f019f)
- Add the 3.3.1 firmware images and source (8b8b791)
- xBloom Panel 3.3.1: steadier clock, settings drop-downs, Sync button (d5b22a0)
- Add the AliExpress buy link (a0449fb)
- xBloom Panel 3.3.0: firmware, one-click installer, case (d36c8bf)

Release notes are added here automatically by the weekly release.
