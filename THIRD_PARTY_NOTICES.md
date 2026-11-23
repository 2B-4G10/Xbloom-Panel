# Credits and third-party notices

## xBloom Panel — https://github.com/Mesh-one/Xbloom-panel
The idea for this firmware, and the look of its screens, come from Mansour Alshekhi's xBloom Panel (MIT, same licence text as below). The code here is a new implementation by Double Shot.

## xbloom-py — https://github.com/Alshekhi/xbloom-py
The xBloom BLE protocol (frame layout, command codes, recipe encoding) as used in src/xbloom_protocol.cpp was learned from xbloom-py; the code is our own.

```
MIT License

Copyright (c) 2026 Mansour Alshekhi

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Elecrow CrowPanel examples
Display/touch bring-up (src/main.cpp, include/LovyanGFX_Driver.h, boards/) follows Elecrow's CrowPanel Advance 4.3 example: https://github.com/Elecrow-RD/CrowPanel-Advance-4.3-HMI-ESP32-S3-AI-Powered-IPS-Touch-Screen-800x480

## Libraries
LVGL (MIT), LovyanGFX (FreeBSD), NimBLE-Arduino (Apache-2.0), Arduino-ESP32 (LGPL-2.1).

ArduinoJson (MIT). Inter font (SIL Open Font License 1.1), converted to LVGL fonts in src/fonts/.
IBM Plex Sans Arabic (Copyright © 2017 IBM Corp., SIL Open Font License 1.1, full text in src/fonts/licenses/IBMPlexSansArabic-OFL.txt), converted to LVGL fonts in src/fonts/ar_*.c.
