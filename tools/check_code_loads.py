#!/usr/bin/env python3
"""Fails if the firmware reads data from its code area with 8- or 16-bit loads.

    python3 tools/check_code_loads.py firmware.elf [objdump]

On the ESP32-S3 the code area (instruction RAM 0x40370000-0x403E0000, and flash or
PSRAM mapped for instructions at 0x42000000-0x44000000) only allows 32-bit reads; an l8ui/l16ui/l16si there crashes with
LoadStoreError. The compiler can place small constants among the code (literal
pools) and read them that way. This finds every l32r that loads an address
inside the code area which is not the start of a function, followed within a
few instructions by a narrow load through that register.
"""
import re
import subprocess
import sys

elf = sys.argv[1]
objdump = sys.argv[2] if len(sys.argv) > 2 else 'xtensa-esp32s3-elf-objdump'
out = subprocess.run([objdump, '-d', '--no-show-raw-insn', elf], capture_output=True, text=True, check=True).stdout

l32r = re.compile(r'^\s*([0-9a-f]+):\s+l32r\s+(a\d+),.*\(([0-9a-f]+) <([^>]+)>\)')
narrow = re.compile(r'^\s*([0-9a-f]+):\s+(l8ui|l16ui|l16si)\s+a\d+,\s*(a\d+),')
func = re.compile(r'^[0-9a-f]+ <(.+)>:$')

bad, current, pending = [], '?', {}
for line in out.splitlines():
    m = func.match(line)
    if m:
        current, pending = m.group(1), {}
        continue
    m = l32r.match(line)
    if m:
        target, sym = int(m.group(3), 16), m.group(4)
        if (0x40370000 <= target < 0x403E0000 or 0x42000000 <= target < 0x44000000) and '+0x' in sym:
            pending[m.group(2)] = (target, sym, 8)
        else:
            pending.pop(m.group(2), None)
        continue
    m = narrow.match(line)
    if m and m.group(3) in pending:
        target, sym, _ = pending[m.group(3)]
        bad.append(f'{m.group(1)} in {current}: {m.group(2)} from 0x{target:08x} <{sym}>')
    # forget registers after a few instructions
    for r in list(pending):
        t, s, n = pending[r]
        if n <= 1:
            del pending[r]
        else:
            pending[r] = (t, s, n - 1)

if bad:
    print(f'{len(bad)} narrow read(s) from the code area (would crash with LoadStoreError):')
    print('\n'.join(bad[:50]))
    sys.exit(1)
print('No narrow reads from the code area.')
