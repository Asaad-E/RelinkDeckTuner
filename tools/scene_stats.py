"""Count scene objects (by *SceneObject type) in layout .scene.msg files.

Usage:
  python -I scene_stats.py <game_dir> <paths.txt> <out.tsv>   per-file type counts
  python -I scene_stats.py <game_dir> fields <path> <TypeName> dump one object's fields
Only objects with mActive true (and all ancestors active) are counted in the
"active" columns; "all" counts every object.
"""
import collections
import json
import os
import sys

import msgpack

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbfr_archive import Archive  # noqa: E402


def walk(node, active, out, typename=None):
    if isinstance(node, dict):
        for k, v in node.items():
            if k.endswith("SceneObject") and isinstance(v, dict):
                is_active = active and v.get("mActive", True)
                out.append((k, is_active, v))
                for child in v.get("mpChild", []) or []:
                    walk(child, is_active, out)
            elif k == "SceneHierarchy" and isinstance(v, dict):
                walk(v.get("mRootSceneObject", {}), active, out)


def objects(arc, path):
    d = msgpack.unpackb(arc.read(path), raw=False, strict_map_key=False)
    out = []
    walk(d, True, out)
    return out


def main():
    arc = Archive(sys.argv[1])
    if sys.argv[2] == "fields":
        for k, act, v in objects(arc, sys.argv[3]):
            if k == sys.argv[4]:
                print(json.dumps({kk: vv for kk, vv in v.items() if kk != "mpChild"}, indent=1, default=str)[:6000])
                return
        print("not found")
        return
    paths = [l.strip() for l in open(sys.argv[2], encoding="utf-8") if l.strip()]
    rows = []
    types = set()
    for p in paths:
        c_all, c_act = collections.Counter(), collections.Counter()
        for k, act, _ in objects(arc, p):
            c_all[k] += 1
            if act:
                c_act[k] += 1
        types |= set(c_all)
        rows.append((p, c_all, c_act))
    types = sorted(types)
    with open(sys.argv[3], "w", encoding="utf-8") as f:
        f.write("path\t" + "\t".join(f"{t}:all\t{t}:active" for t in types) + "\n")
        for p, ca, cc in rows:
            f.write(p + "\t" + "\t".join(f"{ca[t]}\t{cc[t]}" for t in types) + "\n")
    print(f"{len(rows)} files, {len(types)} types")


if __name__ == "__main__":
    main()
