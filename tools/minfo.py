"""Minimal reader for model .minfo (FlatBuffer ModelInfo, schema: GBFRDataTools MInfo_ModelInfo.fbs).

Only what the LOD tools need: per-LOD vertex/index counts and lod_screen_size_thresholds (field 3),
with the byte offset of each threshold float so it can be patched in place.
"""
import struct


class MInfo:
    def __init__(self, buf):
        self.buf = buf
        root = struct.unpack_from("<I", buf, 0)[0]
        vt = root - struct.unpack_from("<i", buf, root)[0]
        self.root, self.vt = root, vt
        self.nfields = (struct.unpack_from("<H", buf, vt)[0] - 4) // 2

    def field(self, table, vt, idx):
        nfields = (struct.unpack_from("<H", self.buf, vt)[0] - 4) // 2
        if idx >= nfields:
            return None
        off = struct.unpack_from("<H", self.buf, vt + 4 + idx * 2)[0]
        return table + off if off else None

    def vector(self, pos):
        v = pos + struct.unpack_from("<I", self.buf, pos)[0]
        return v + 4, struct.unpack_from("<I", self.buf, v)[0]

    def lods(self):
        p = self.field(self.root, self.vt, 1)
        if p is None:
            return []
        start, n = self.vector(p)
        out = []
        for i in range(n):
            e = start + 4 * i
            t = e + struct.unpack_from("<I", self.buf, e)[0]
            tvt = t - struct.unpack_from("<i", self.buf, t)[0]
            vc = self.field(t, tvt, 2)
            ic = self.field(t, tvt, 3)
            out.append((struct.unpack_from("<i", self.buf, vc)[0] if vc else 0,
                        struct.unpack_from("<i", self.buf, ic)[0] if ic else 0))
        return out

    def thresholds(self):
        """[(byte_offset, value)] of lod_screen_size_thresholds."""
        p = self.field(self.root, self.vt, 3)
        if p is None:
            return []
        start, n = self.vector(p)
        return [(start + 4 * i, struct.unpack_from("<f", self.buf, start + 4 * i)[0]) for i in range(n)]
