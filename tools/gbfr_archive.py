"""Minimal reader for the Granblue Fantasy: Relink data.i / data.N archives.

Format reference: Nenkai/GBFRDataTools (IndexFile.fbs, Archive.cs).
  - data.i is a FlatBuffer (IndexFile). Files are identified by
    XXH64(lowercase path, seed 0); ArchiveFileHashes is sorted.
  - Each file maps to a chunk (LZ4 block, possibly uncompressed) in data.<N>.

Usage:
  python -I gbfr_archive.py info <game_dir>
  python -I gbfr_archive.py resolve <game_dir> <filelist.txt> <out.tsv>
  python -I gbfr_archive.py extract <game_dir> <out_dir> <path> [<path> ...]
  python -I gbfr_archive.py extract-list <game_dir> <out_dir> <paths.txt>
"""
import bisect
import os
import struct
import sys

import lz4.block

M64 = 0xFFFFFFFFFFFFFFFF
P1, P2, P3, P4, P5 = (0x9E3779B185EBCA87, 0xC2B2AE3D27D4EB4F, 0x165667B19E3779F9,
                      0x85EBCA77C2B2AE63, 0x27D4EB2F165667C5)


def _rotl(x, r):
    return ((x << r) | (x >> (64 - r))) & M64


def _round(acc, lane):
    acc = (acc + lane * P2) & M64
    return (_rotl(acc, 31) * P1) & M64


def _merge(acc, val):
    acc ^= _round(0, val)
    return (acc * P1 + P4) & M64


def xxh64(data, seed=0):
    n = len(data)
    i = 0
    if n >= 32:
        v1 = (seed + P1 + P2) & M64
        v2 = (seed + P2) & M64
        v3 = seed
        v4 = (seed - P1) & M64
        while i + 32 <= n:
            a, b, c, d = struct.unpack_from("<4Q", data, i)
            v1, v2, v3, v4 = _round(v1, a), _round(v2, b), _round(v3, c), _round(v4, d)
            i += 32
        h = (_rotl(v1, 1) + _rotl(v2, 7) + _rotl(v3, 12) + _rotl(v4, 18)) & M64
        for v in (v1, v2, v3, v4):
            h = _merge(h, v)
    else:
        h = (seed + P5) & M64
    h = (h + n) & M64
    while i + 8 <= n:
        (k,) = struct.unpack_from("<Q", data, i)
        h ^= _round(0, k)
        h = (_rotl(h, 27) * P1 + P4) & M64
        i += 8
    if i + 4 <= n:
        (k,) = struct.unpack_from("<I", data, i)
        h ^= (k * P1) & M64
        h = (_rotl(h, 23) * P2 + P3) & M64
        i += 4
    while i < n:
        h ^= (data[i] * P5) & M64
        h = (_rotl(h, 11) * P1) & M64
        i += 1
    h ^= h >> 33
    h = (h * P2) & M64
    h ^= h >> 29
    h = (h * P3) & M64
    h ^= h >> 32
    return h


def path_hash(path):
    return xxh64(path.replace("\\", "/").lower().encode("utf-8"))


class Index:
    """Hand-rolled FlatBuffer parse of IndexFile."""

    def __init__(self, buf):
        self.buf = buf
        root = struct.unpack_from("<I", buf, 0)[0]
        vt = root - struct.unpack_from("<i", buf, root)[0]
        vt_len = struct.unpack_from("<H", buf, vt)[0]
        self._root, self._vt, self._nfields = root, vt, (vt_len - 4) // 2
        self.codename = self._string(0)
        self.num_archives = self._scalar(1, "<H")
        self.seed = self._scalar(2, "<H")
        self.archive_hashes = self._vector(3, "<Q", 8)
        self.file_chunks = self._vector(4, "<iII", 12)
        self.chunks = self._vector(5, "<QIIIBBBB", 24)
        self.external_hashes = self._vector(6, "<Q", 8)
        self.external_sizes = self._vector(7, "<Q", 8)

    def _field(self, idx):
        if idx >= self._nfields:
            return None
        off = struct.unpack_from("<H", self.buf, self._vt + 4 + idx * 2)[0]
        return self._root + off if off else None

    def _scalar(self, idx, fmt):
        p = self._field(idx)
        return struct.unpack_from(fmt, self.buf, p)[0] if p else 0

    def _string(self, idx):
        p = self._field(idx)
        if not p:
            return ""
        s = p + struct.unpack_from("<I", self.buf, p)[0]
        n = struct.unpack_from("<I", self.buf, s)[0]
        return self.buf[s + 4:s + 4 + n].decode()

    def _vector(self, idx, fmt, size):
        p = self._field(idx)
        if not p:
            return []
        v = p + struct.unpack_from("<I", self.buf, p)[0]
        n = struct.unpack_from("<I", self.buf, v)[0]
        single = len(fmt.strip("<")) == 1
        out = []
        for i in range(n):
            t = struct.unpack_from(fmt, self.buf, v + 4 + i * size)
            out.append(t[0] if single else t)
        return out


class Archive:
    def __init__(self, game_dir):
        self.dir = game_dir
        self.index = Index(open(os.path.join(game_dir, "data.i"), "rb").read())
        self._chunk_cache = {}

    def find(self, path):
        h = path_hash(path)
        hashes = self.index.archive_hashes
        i = bisect.bisect_left(hashes, h)
        if i < len(hashes) and hashes[i] == h:
            return ("archive", i)
        hashes = self.index.external_hashes
        i = bisect.bisect_left(hashes, h)
        if i < len(hashes) and hashes[i] == h:
            return ("external", i)
        return None

    def read(self, path):
        hit = self.find(path)
        if hit is None:
            raise KeyError(path)
        kind, i = hit
        if kind == "external":
            return open(os.path.join(self.dir, "data", path), "rb").read()
        chunk_idx, size, dec_off = self.index.file_chunks[i]
        if chunk_idx not in self._chunk_cache:
            off, zsize, usize, _align, _flag, _pad, num, _pad2 = self.index.chunks[chunk_idx]
            with open(os.path.join(self.dir, f"data.{num}"), "rb") as f:
                f.seek(off)
                raw = f.read(zsize)
            data = raw if zsize == usize else lz4.block.decompress(raw, uncompressed_size=usize)
            self._chunk_cache = {chunk_idx: data}
        return self._chunk_cache[chunk_idx][dec_off:dec_off + size]


def main():
    cmd, game_dir = sys.argv[1], sys.argv[2]
    arc = Archive(game_dir)
    if cmd == "info":
        ix = arc.index
        print(f"codename={ix.codename} archives={ix.num_archives} seed={ix.seed}")
        print(f"archive files={len(ix.archive_hashes)} external={len(ix.external_hashes)} chunks={len(ix.chunks)}")
    elif cmd == "resolve":
        found = 0
        with open(sys.argv[4], "w", encoding="utf-8") as out:
            for line in open(sys.argv[3], encoding="utf-8"):
                p = line.strip()
                if not p:
                    continue
                hit = arc.find(p)
                if hit:
                    found += 1
                    size = arc.index.file_chunks[hit[1]][1] if hit[0] == "archive" else arc.index.external_sizes[hit[1]]
                    out.write(f"{p}\t{hit[0]}\t{size}\n")
        print(f"resolved {found}")
    elif cmd in ("extract", "extract-list"):
        out_dir = sys.argv[3]
        paths = sys.argv[4:] if cmd == "extract" else [l.strip() for l in open(sys.argv[4], encoding="utf-8") if l.strip()]
        for p in paths:
            try:
                data = arc.read(p)
            except KeyError:
                print(f"missing {p}")
                continue
            dst = os.path.join(out_dir, p)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            open(dst, "wb").write(data)
        print(f"extracted {len(paths)}")


if __name__ == "__main__":
    main()
