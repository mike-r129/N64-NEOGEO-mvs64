#!/usr/bin/env python3
# Record-type histogram of an MVS64_Z80TRACE file, plus how many IRQ/BANK
# effects sit inside callbacks (right after an IN/OUT record).
import sys
SIZES = {0x01: 4, 0x02: 10, 0x03: 0, 0x04: 4, 0x10: 7, 0x11: 7, 0x20: 1, 0x21: 1,
         0x22: 0, 0x23: 4, 0x24: 1, 0x25: 5, 0x30: 4}
NAMES = {0x01: 'RUN', 0x02: 'RUN_END', 0x03: 'STEP', 0x04: 'STEP_END', 0x10: 'IN',
         0x11: 'OUT', 0x20: 'IRQ', 0x21: 'GENINT', 0x22: 'NMI', 0x23: 'SETCYC',
         0x24: 'SETR', 0x25: 'BANK', 0x30: 'RAMCRC', 0x7F: 'END'}
for fn in sys.argv[1:]:
    b = open(fn, 'rb').read()
    i = 12 + 43 + 2048 + 16
    rom = int.from_bytes(b[i:i + 4], 'little'); i += 4 + rom
    cnt = {}; incb = {'IRQ': 0, 'BANK': 0}; prev = None; inrun = False
    while i < len(b):
        t = b[i]; i += 1
        name = NAMES.get(t, '?%02x' % t)
        cnt[name] = cnt.get(name, 0) + 1
        if t == 0x7F: break
        if t in (0x20, 0x25) and prev in (0x10, 0x11, 0x20, 0x25) and inrun:
            incb[NAMES[t]] += 1
        if t in (0x01, 0x03): inrun = True
        if t in (0x02, 0x04): inrun = False
        if t not in (0x20, 0x25) or not inrun: prev = t
        if t in (0x20, 0x25) and inrun: prev = prev if prev in (0x10, 0x11) else t
        i += SIZES[t]
    print(fn.split('/')[-1], ' '.join('%s=%d' % kv for kv in sorted(cnt.items())),
          '| inside callbacks: IRQ=%d BANK=%d' % (incb['IRQ'], incb['BANK']))
