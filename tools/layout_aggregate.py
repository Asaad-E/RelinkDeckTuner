"""Aggregate scene-object statistics per stage folder across layout/*.scene.msg.

Usage: python -I layout_aggregate.py <game_dir> <out.tsv>
Columns per stage: total objects by type (active only) plus point-light details:
  pl_active, pl_dynshadow (bCastDynamicShadow_ != 0), pl_eventchara (useEventCharaLight_).
"""
import collections
import os
import sys

import msgpack

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbfr_archive import Archive  # noqa: E402
from scene_stats import walk  # noqa: E402


def main():
    game_dir, out = sys.argv[1], sys.argv[2]
    arc = Archive(game_dir)
    paths = [l.split("\t")[0] for l in open(os.path.join(game_dir, "src/dumps/resolved.tsv"), encoding="utf-8")
             if l.startswith("layout/") and l.split("\t")[0].endswith(".scene.msg")]
    per_stage = collections.defaultdict(collections.Counter)
    for p in sorted(paths):
        stage = p.split("/")[1]
        try:
            d = msgpack.unpackb(arc.read(p), raw=False, strict_map_key=False)
        except Exception as e:  # keep going; report undecodable files
            print(f"FAIL {p}: {e}")
            continue
        objs = []
        walk(d, True, objs)
        c = per_stage[stage]
        c["files"] += 1
        for k, act, v in objs:
            if not act:
                continue
            c[k] += 1
            if k in ("PointLightSceneObject", "SpotLightSceneObject"):
                data = v.get("data_", {})
                if data.get("bCastDynamicShadow_"):
                    c[k + ":dynshadow"] += 1
                if v.get("useEventCharaLight_"):
                    c[k + ":eventchara"] += 1
    keys = sorted({k for c in per_stage.values() for k in c})
    with open(out, "w", encoding="utf-8") as f:
        f.write("stage\t" + "\t".join(keys) + "\n")
        for s in sorted(per_stage):
            f.write(s + "\t" + "\t".join(str(per_stage[s][k]) for k in keys) + "\n")
    print(f"stages={len(per_stage)} types={len(keys)}")


if __name__ == "__main__":
    main()
