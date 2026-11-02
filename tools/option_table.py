"""Decode system/table/option_item.tbl (v2.0.x layout) and name rows via text ids.

Usage: python -I option_table.py <option_item.tbl> <text_en_flat.txt>
Layout (GBFRDataTools option_item.headers, >= 2.0.0.2): u64 row count, then
52-byte rows: 9 x u32 (Key, Name, Description hashes, ItemType, Min, Max,
Default, Unk7, Unk70) + 16 bytes of flags.
Text ids are hashed with GBFR's custom XXHash32.
"""
import struct
import sys

P1, P2, P3, P4, P5 = 0x9E3779B1, 0x85EBCA77, 0xC2B2AE3D, 0x27D4EB2F, 0x165667B1
M = 0xFFFFFFFF


def _rotl(x, r):
    return ((x << r) | (x >> (32 - r))) & M


def _round(seed, v):
    return (_rotl((seed + v * P2) & M, 13) * P1) & M


def hash32(s):
    b = s.encode("ascii")
    h = 0x178A54A4
    i = 0
    if len(b) >= 16:
        v = [0x2557311B, 0x871FB76A, 0x0133ECF3, 0x62FC7342]
        while True:
            for k in range(4):
                v[k] = _round(v[k], struct.unpack_from("<I", b, i + 4 * k)[0])
            i += 16
            if not len(b) - i > 16:
                break
        h = (_rotl(v[0], 1) + _rotl(v[1], 7) + _rotl(v[2], 12) + _rotl(v[3], 18)) & M
    h = (h + len(b)) & M
    while len(b) - i >= 4:
        h = (_rotl((h + struct.unpack_from("<I", b, i)[0] * P3) & M, 17) * P4) & M
        i += 4
    while i < len(b):
        h = (_rotl((h + b[i] * P5) & M, 11) * P1) & M
        i += 1
    h ^= h >> 15
    h = (h * P2) & M
    h ^= h >> 13
    h = (h * P3) & M
    h ^= h >> 16
    return h


FLAGS = ["Unk9", "Unk10", "Unk11", "Unk12", "OnlyMainMenu", "Unk14", "IsPC", "IsPS4",
         "IsEnabled", "IsPS5", "VisibleOnMainMenu", "Unk19", "IsSwitch2", "Unk21", "Unk22", "Unk23"]


def main():
    data = open(sys.argv[1], "rb").read()
    names = {}
    for line in open(sys.argv[2], encoding="utf-8"):
        parts = line.split(" | ")
        if len(parts) >= 3:
            names[hash32(parts[1].strip())] = (parts[1].strip(), parts[2].strip())
    n = struct.unpack_from("<Q", data, 0)[0]
    for r in range(n):
        off = 8 + r * 52
        key, name, desc, itype, mn, mx, dflt, u7, u70 = struct.unpack_from("<9I", data, off)
        flags = data[off + 36:off + 52]
        nm = names.get(name, (f"{name:08x}", ""))
        fl = " ".join(f"{FLAGS[i]}={flags[i]}" for i in (6, 7, 9, 12, 8, 10, 4))
        print(f"{r:3d} key={key:08x} {nm[0]:34s} '{nm[1][:34]}' type={itype} {mn}..{mx} def={dflt} | {fl}")


if __name__ == "__main__":
    main()
