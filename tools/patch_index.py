"""Make the game load mod files from disk, without Reloaded-II.

Usage: python -I patch_index.py <original data.i> <out_dir> <mod_dir> [<mod_dir> ...]

Each <mod_dir> is a Reloaded mod from build_mod.py (files under GBFR/data/<path>).
If two mods contain the same file, the one given later wins.
Writes <out_dir>/data.i and <out_dir>/data/<path>: every mod file is removed from
the archive tables and listed as an external file, which is how the game already
loads data/sound and data/ui (same approach as GBFRDataTools' mod manager).
"""
import bisect
import os
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbfr_archive import Index, path_hash  # noqa: E402


def raw_vector(idx, field, size):
    """Element bytes of a vector field (struct vectors are copied verbatim)."""
    p = idx._field(field)
    v = p + struct.unpack_from("<I", idx.buf, p)[0]
    n = struct.unpack_from("<I", idx.buf, v)[0]
    return [idx.buf[v + 4 + i * size:v + 4 + (i + 1) * size] for i in range(n)]


def build(codename, num_archives, vectors):
    """Serialize IndexFile. vectors: {field: (list of element bytes, alignment)}."""
    vt_fields = 9
    vtable_len = 4 + 2 * vt_fields
    ref_fields = [0, 3, 4, 5, 6, 7, 8]  # offset fields, in table order; field 1 is a ushort
    table_len = 4 + 4 * len(ref_fields) + 4  # soffset + uoffsets + ushort (+2 pad)
    vt_pos = 4
    tbl_pos = (vt_pos + vtable_len + 3) & ~3
    out = bytearray(tbl_pos + table_len)
    struct.pack_into("<I", out, 0, tbl_pos)
    field_off = {f: 4 + 4 * i for i, f in enumerate(ref_fields)}
    field_off[1] = 4 + 4 * len(ref_fields)
    struct.pack_into("<HH", out, vt_pos, vtable_len, table_len)
    for f in range(vt_fields):
        struct.pack_into("<H", out, vt_pos + 4 + 2 * f, field_off.get(f, 0))
    struct.pack_into("<i", out, tbl_pos, tbl_pos - vt_pos)
    struct.pack_into("<H", out, tbl_pos + field_off[1], num_archives)

    def place(field, payload_len, align, write):
        start = len(out) + 4
        start += (-start) % align          # element data aligned; length prefix just before
        out.extend(b"\0" * (start - 4 - len(out)))
        slot = tbl_pos + field_off[field]
        struct.pack_into("<I", out, slot, len(out) - slot)
        write()

    for field in (8, 7, 6, 5, 4, 3):
        elems, align = vectors[field]
        def write(elems=elems):
            out.extend(struct.pack("<I", len(elems)))
            for e in elems:
                out.extend(e)
        place(field, 0, align, write)

    name = codename.encode()
    def write_str():
        out.extend(struct.pack("<I", len(name)) + name + b"\0")
    place(0, 0, 4, write_str)
    out.extend(b"\0" * ((-len(out)) % 4))
    return bytes(out)


def main():
    src, out_dir, mod_dirs = sys.argv[1], sys.argv[2], sys.argv[3:]
    idx = Index(open(src, "rb").read())

    files = {}  # game path -> local file
    for m in mod_dirs:
        root = os.path.join(m, "GBFR", "data")
        for dp, _, names in os.walk(root):
            for n in names:
                p = os.path.join(dp, n)
                rel = os.path.relpath(p, root).replace(os.sep, "/")
                if rel in files:
                    print(f"  {rel}: taken from {m} (later mod wins)")
                files[rel] = p

    arc_hashes = list(idx.archive_hashes)
    arc_entries = raw_vector(idx, 4, 12)
    ext_hashes = list(idx.external_hashes)
    ext_sizes = list(idx.external_sizes)
    removed = 0
    for rel, p in sorted(files.items()):
        h = path_hash(rel)
        i = bisect.bisect_left(arc_hashes, h)
        if i < len(arc_hashes) and arc_hashes[i] == h:
            del arc_hashes[i], arc_entries[i]
            removed += 1
        size = os.path.getsize(p)
        j = bisect.bisect_left(ext_hashes, h)
        if j < len(ext_hashes) and ext_hashes[j] == h:
            ext_sizes[j] = size
        else:
            ext_hashes.insert(j, h)
            ext_sizes.insert(j, size)

    data = build(idx.codename, idx.num_archives, {
        3: ([struct.pack("<Q", h) for h in arc_hashes], 8),
        4: (arc_entries, 4),
        5: (raw_vector(idx, 5, 24), 8),
        6: ([struct.pack("<Q", h) for h in ext_hashes], 8),
        7: ([struct.pack("<Q", s) for s in ext_sizes], 8),
        8: (raw_vector(idx, 8, 4), 4),
    })
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "data.i"), "wb") as f:
        f.write(data)
    for rel, p in files.items():
        dst = os.path.join(out_dir, "data", *rel.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(p, dst)
    print(f"{len(files)} files external ({removed} moved out of the archive), data.i {len(data)} bytes")


if __name__ == "__main__":
    main()
