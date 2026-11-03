"""Build a data mod that makes vegetation models use a lighter LOD even close to the camera.

Usage:
  python -I build_foliage.py <game_dir> <out_dir> [--level 1] [--mod-id relinkdecktuner.foliage]

Vegetation = bg/ba models with more than one LOD whose material file (vars/*.mmat) names leaf,
bush, grass, tree-crown style textures. For each, lod_screen_size_thresholds[0..level] are raised
to 1000. Whichever way the engine compares screen size against the thresholds (size < thr[i] picks
the last such LOD, or size >= thr[i] picks the first), LOD 0..level-1 can then no longer be chosen.
Profiling (report 7.9): alpha-tested vegetation overdraw is ~half of a heavy dialogue frame.
"""
import argparse
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_mod import ModWriter  # noqa: E402
from gbfr_archive import Archive  # noqa: E402
from minfo import MInfo  # noqa: E402

VEGETATION = re.compile(rb"(leaf|leaves|bush|grass|hedge|ivy|flower|fern|shrub|vine|plant|foliage|crown|needle)",
                        re.IGNORECASE)
FORCED = 1000.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game_dir")
    ap.add_argument("out_dir")
    ap.add_argument("--level", type=int, default=1, help="first LOD allowed (1 = skip LOD0)")
    ap.add_argument("--mod-id", default="relinkdecktuner.foliage")
    args = ap.parse_args()

    arc = Archive(args.game_dir)
    paths = [l.split("\t")[0] for l in open(os.path.join(args.game_dir, "src/dumps/resolved.tsv"), encoding="utf-8")]
    mats = {}
    for p in paths:
        m = re.match(r"model/(b[ag]/b[ag][0-9a-f]{4})/vars/\d+\.mmat$", p)
        if m:
            mats.setdefault(m.group(1), []).append(p)

    mod = ModWriter(args.out_dir, args.mod_id, f"Relink Deck Tuner - lighter vegetation (LOD{args.level}+)",
                    "Vegetation models skip their most detailed LODs to reduce alpha-tested overdraw.")
    count = 0
    for model, mmats in sorted(mats.items()):
        if not any(VEGETATION.search(arc.read(mp)) for mp in mmats):
            continue
        mpath = f"model/{model}/{model.split('/')[1]}.minfo"
        try:
            buf = bytearray(arc.read(mpath))
        except KeyError:
            continue
        info = MInfo(bytes(buf))
        thr = info.thresholds()
        if len(info.lods()) <= args.level + 1 or len(thr) <= args.level + 1:  # keep the last LOD reachable
            continue
        before = [round(v, 4) for _, v in thr]
        for off, _ in thr[:args.level + 1]:
            struct.pack_into("<f", buf, off, FORCED)
        after = [round(v, 4) for _, v in MInfo(bytes(buf)).thresholds()]
        changed = sum(1 for a, b in zip(bytes(buf), arc.read(mpath)) if a != b)
        if changed > 4 * (args.level + 1):
            raise AssertionError(f"{mpath}: {changed} bytes changed")
        mod.write(mpath, bytes(buf))
        mod.changes.append(f"{mpath}: lod thresholds {before} -> {after}")
        count += 1
    print(f"  {count} vegetation models")
    mod.finish()


if __name__ == "__main__":
    main()
