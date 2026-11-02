"""Flatten filter/*.ccomp.msg (post-process component curves) into rows.

Usage: python -I ccomp_dump.py <game_dir> <out.tsv>
Row: file, ObjectType, Attribute, key count, first key value.
"""
import json
import os
import sys

import msgpack

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbfr_archive import Archive  # noqa: E402


def attrs(node, out):
    if isinstance(node, dict):
        if node.get("NodeType") == "Attribute":
            keys = node.get("Keys", {}).get("Array", [])
            first = keys[0][2] if keys and len(keys[0]) > 2 else None
            out.append((node.get("ObjectType"), node.get("Attribute"), len(keys), first))
        for v in node.values():
            attrs(v, out)
    elif isinstance(node, list):
        for x in node:
            attrs(x, out)


def main():
    game_dir, out = sys.argv[1], sys.argv[2]
    arc = Archive(game_dir)
    paths = [l.split("\t")[0] for l in open(os.path.join(game_dir, "src/dumps/resolved.tsv"), encoding="utf-8")
             if l.startswith("filter/") and l.split("\t")[0].endswith(".ccomp.msg")]
    with open(out, "w", encoding="utf-8") as f:
        f.write("file\tobject\tattribute\tnkeys\tvalue\n")
        for p in sorted(paths):
            rows = []
            attrs(msgpack.unpackb(arc.read(p), raw=False, strict_map_key=False), rows)
            for o, a, n, v in rows:
                f.write(f"{p}\t{o}\t{a}\t{n}\t{json.dumps(v)}\n")
    print(f"{len(paths)} files")


if __name__ == "__main__":
    main()
