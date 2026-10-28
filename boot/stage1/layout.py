#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""layout.py <Image> <dtb> <initramfs>  -> prints the -D flags the stage1 stub is compiled with.
   assemble.py-style mode:  layout.py --assemble <stub> <Image> <dtb> <initramfs> <out>"""
import struct
import sys

STUB_AREA = 0x20000


def pad(n, a=4096):
    return (n + a - 1) // a * a


def sizes(kimg, dtb, rd):
    k = open(kimg, 'rb').read()
    kimgsz = struct.unpack_from('<Q', k, 16)[0]
    ksz = pad(len(k))
    dsz = pad(len(open(dtb, 'rb').read()))
    rsz = pad(len(open(rd, 'rb').read()))
    koff = STUB_AREA
    doff = koff + ksz
    roff = doff + dsz
    total = roff + rsz
    return dict(STUB_AREA=STUB_AREA, KOFF=koff, KSZ=ksz, KIMGSZ=pad(kimgsz), DOFF=doff, DSZ=dsz, ROFF=roff, RSZ=rsz,
                IMG_SIZE=total), k


if sys.argv[1] == '--assemble':
    stub, kimg, dtb, rd, out = sys.argv[2:7]
    s, k = sizes(kimg, dtb, rd)
    st = open(stub, 'rb').read()
    assert len(st) <= STUB_AREA, 'stub too big: %d' % len(st)
    blob = st + b'\0' * (STUB_AREA - len(st))
    for data, size in ((k, s['KSZ']), (open(dtb, 'rb').read(), s['DSZ']), (open(rd, 'rb').read(), s['RSZ'])):
        blob += data + b'\0' * (size - len(data))
    assert len(blob) == s['IMG_SIZE'], (len(blob), s['IMG_SIZE'])
    open(out, 'wb').write(blob)
    print('payload', len(blob), 'bytes;', ' '.join('%s=%#x' % kv for kv in s.items()))
else:
    s, _ = sizes(*sys.argv[1:4])
    print(' '.join('-D%s=%#xUL' % kv for kv in s.items()))
