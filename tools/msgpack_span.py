"""Lossless MessagePack editing: parse to a tree that keeps byte spans.

Re-serialising a .msg with a generic library can change scalar encodings
(float32 -> float64, int widths). Instead we parse into Node objects that
remember where each value sits, then splice new bytes in place. MessagePack
containers store element counts, not byte lengths, so a replacement of a
different size keeps the document valid.
"""
import struct

import msgpack


class Node:
    __slots__ = ("start", "end", "kind", "value", "items")

    def __init__(self, start, end, kind, value=None, items=None):
        self.start, self.end, self.kind, self.value, self.items = start, end, kind, value, items

    def get(self, key, default=None):
        """Map lookup by key (maps only)."""
        if self.kind != "map":
            return default
        for k, v in self.items:
            if k.value == key:
                return v
        return default

    def __iter__(self):
        if self.kind == "array":
            return iter(self.items)
        if self.kind == "map":
            return iter(self.items)
        return iter(())


def parse(buf, pos=0):
    """Parse one value at pos. Returns (Node, next_pos)."""
    b = buf[pos]
    if b <= 0x7F:
        return Node(pos, pos + 1, "scalar", b), pos + 1
    if 0x80 <= b <= 0x8F:
        return _map(buf, pos, pos + 1, b & 0x0F)
    if 0x90 <= b <= 0x9F:
        return _array(buf, pos, pos + 1, b & 0x0F)
    if 0xA0 <= b <= 0xBF:
        n = b & 0x1F
        return Node(pos, pos + 1 + n, "scalar", buf[pos + 1:pos + 1 + n].decode("utf-8")), pos + 1 + n
    if b >= 0xE0:
        return Node(pos, pos + 1, "scalar", b - 0x100), pos + 1
    if b == 0xC0:
        return Node(pos, pos + 1, "scalar", None), pos + 1
    if b in (0xC2, 0xC3):
        return Node(pos, pos + 1, "scalar", b == 0xC3), pos + 1
    fixed = {0xCA: ">f", 0xCB: ">d", 0xCC: ">B", 0xCD: ">H", 0xCE: ">I", 0xCF: ">Q",
             0xD0: ">b", 0xD1: ">h", 0xD2: ">i", 0xD3: ">q"}
    if b in fixed:
        fmt = fixed[b]
        size = struct.calcsize(fmt)
        return Node(pos, pos + 1 + size, "scalar", struct.unpack_from(fmt, buf, pos + 1)[0]), pos + 1 + size
    if b in (0xD9, 0xDA, 0xDB, 0xC4, 0xC5, 0xC6):
        lf = {0xD9: ">B", 0xDA: ">H", 0xDB: ">I", 0xC4: ">B", 0xC5: ">H", 0xC6: ">I"}[b]
        ls = struct.calcsize(lf)
        n = struct.unpack_from(lf, buf, pos + 1)[0]
        s = pos + 1 + ls
        raw = buf[s:s + n]
        val = raw.decode("utf-8") if b in (0xD9, 0xDA, 0xDB) else bytes(raw)
        return Node(pos, s + n, "scalar", val), s + n
    if b in (0xDC, 0xDD):
        lf = ">H" if b == 0xDC else ">I"
        return _array(buf, pos, pos + 1 + struct.calcsize(lf), struct.unpack_from(lf, buf, pos + 1)[0])
    if b in (0xDE, 0xDF):
        lf = ">H" if b == 0xDE else ">I"
        return _map(buf, pos, pos + 1 + struct.calcsize(lf), struct.unpack_from(lf, buf, pos + 1)[0])
    raise ValueError(f"unsupported msgpack type 0x{b:02x} at {pos}")


def _array(buf, start, pos, n):
    items = []
    for _ in range(n):
        node, pos = parse(buf, pos)
        items.append(node)
    return Node(start, pos, "array", items=items), pos


def _map(buf, start, pos, n):
    items = []
    for _ in range(n):
        k, pos = parse(buf, pos)
        v, pos = parse(buf, pos)
        items.append((k, v))
    return Node(start, pos, "map", items=items), pos


def walk_maps(node):
    """Yield every map node in the tree (depth first)."""
    if node.kind == "map":
        yield node
        for _, v in node.items:
            yield from walk_maps(v)
    elif node.kind == "array":
        for v in node.items:
            yield from walk_maps(v)


def encode_like(old_node, buf, new_value):
    """Encode new_value keeping the original float width when possible."""
    tag = buf[old_node.start]
    if isinstance(new_value, float) and tag == 0xCA:
        return b"\xca" + struct.pack(">f", new_value)
    return msgpack.packb(new_value, use_bin_type=True)


def apply(buf, patches):
    """patches: list of (Node, new_value). Returns new bytes."""
    out = bytearray(buf)
    for node, val in sorted(patches, key=lambda p: p[0].start, reverse=True):
        out[node.start:node.end] = encode_like(node, buf, val)
    return bytes(out)
