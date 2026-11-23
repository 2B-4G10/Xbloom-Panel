#!/usr/bin/env python3
"""Reads the serial log of the emulator build and writes the screen pictures and a report.

    python3 tools/emulator/report.py serial.log OUT_DIR

Writes OUT_DIR/<step>.png for every picture, OUT_DIR/contact-sheet.png with all of
them, and OUT_DIR/report.md. Exits 1 if a step failed, the firmware crashed or the
script did not finish.
"""
import base64
import os
import re
import struct
import sys

from PIL import Image, ImageDraw

log_path, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
lines = open(log_path, encoding='utf-8', errors='replace').read().splitlines()


def decode(name, w, h, data):
    px = bytearray()
    for i in range(0, len(data) - 3, 4):
        run, c = struct.unpack_from('<HH', data, i)
        r, g, b = (c >> 11) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3
        px += bytes((r | r >> 5, g | g >> 6, b | b >> 5)) * run
    if len(px) != w * h * 3:
        return None
    return Image.frombytes('RGB', (w, h), bytes(px))


shots, events, i = [], [], 0
while i < len(lines):
    line = lines[i]
    m = re.match(r'@@SHOT (\S+) (\d+) (\d+) (\d+)', line)
    if m:
        name, w, h = m.group(1), int(m.group(2)), int(m.group(3))
        chunks = []
        i += 1
        while i < len(lines) and lines[i] != '@@END':
            if lines[i].startswith('@@'):
                chunks.append(lines[i][2:])
            i += 1
        img = decode(name, w, h, base64.b64decode(''.join(chunks)))
        n = sum(1 for s in shots if s[0].startswith(name))
        file = f'{name}.png' if n == 0 else f'{name}_{n + 1}.png'
        if img:
            img.save(os.path.join(out, file))
            shots.append((file[:-4], img))
        else:
            events.append(f'picture {name} is damaged')
    elif not line.startswith('@@'):
        events.append(line)
    i += 1

text = '\n'.join(events)
failed_steps = [e for e in events if '[emu] FAIL' in e]
crashed = [e for e in events if re.search(r'Guru Meditation|abort\(\) was called|Backtrace:|stack overflow|assert failed', e)]
result = next((e for e in events if '[emu] RESULT' in e), None)
restarts = sum(1 for e in events if 'xBloom Panel' in e and 'starting' in e)

# All pictures on one sheet, four across, with their names.
if shots:
    cols, tw, th = 4, 400, 240
    rows = (len(shots) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * tw, rows * (th + 22)), (40, 40, 40))
    d = ImageDraw.Draw(sheet)
    for k, (name, img) in enumerate(shots):
        x, y = (k % cols) * tw, (k // cols) * (th + 22)
        sheet.paste(img.resize((tw, th), Image.LANCZOS), (x, y + 22))
        d.text((x + 6, y + 5), name, fill=(255, 255, 255))
    sheet.save(os.path.join(out, 'contact-sheet.png'))

ok = result is not None and not failed_steps and not crashed
with open(os.path.join(out, 'report.md'), 'w') as f:
    f.write('## Emulator test: ' + ('passed ✅' if ok else 'failed ❌') + '\n\n')
    f.write(f'- {result or "The script did not finish."}\n')
    f.write(f'- Start-ups: {restarts} (English, Arabic, English)\n')
    f.write(f'- Screen pictures: {len(shots)}\n')
    for e in failed_steps:
        f.write(f'- {e}\n')
    for e in crashed[:5]:
        f.write(f'- crash: `{e}`\n')
    f.write('\n<details><summary>Steps</summary>\n\n```\n')
    f.write('\n'.join(e for e in events if e.startswith(('[emu]', '[machine]', '[wifi]', '[mem]', 'xBloom Panel'))))
    f.write('\n```\n</details>\n')
print(open(os.path.join(out, 'report.md')).read())
sys.exit(0 if ok else 1)
