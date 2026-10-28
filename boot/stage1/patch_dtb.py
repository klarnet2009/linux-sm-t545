#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""patch_dtb.py <dtb> <group-mask> <out> : enable the groups of groups.py in the DTB (status "disabled" -> "okay") and clear t545,bisect"""
import importlib.util
import struct
import sys

spec = importlib.util.spec_from_file_location("groups", __file__.replace("patch_dtb.py", "groups.py"))
src = open(spec.origin).read().replace("\nmain()\n", "\n")
ns = {"__file__": spec.origin}
exec(compile(src, spec.origin, "exec"), ns)
b = bytearray(open(sys.argv[1], "rb").read())
mask = int(sys.argv[2], 0)
props = ns["walk"](bytes(b))
for gi, (name, nodes) in enumerate(ns["GROUPS"]):
    if not mask >> gi & 1:
        continue
    for n in nodes:
        cand = [k for k in props if k[1] == "status" and k[0].endswith("soc@0/" + n)]
        assert len(cand) == 1, (n, cand)
        off = props[cand[0]][0]
        assert bytes(b[off:off + 9]) == b"disabled\0"
        b[off:off + 9] = b"okay\0\0\0\0\0"
bis = [k for k in props if k[1] == "t545,bisect"][0]
off = props[bis][0]
b[off:off + 4] = struct.pack(">I", 0)
open(sys.argv[3], "wb").write(b)
print("patched, mask", hex(mask))
