"""Build an "effects off" data mod: switch off whole post-process components in every filter.

Usage:
  python -I build_effects.py <game_dir> <out_dir> <mod_id> GROUP[,GROUP...] [--base <mod_dir> ...]

Groups (component names as they appear in filter/*.ccomp.msg):
  snn    SymmetricNearestNeighborParameter (painterly filter)
  dof    DofParameter
  aal    AmbientAreaLightParameter
  sky    SkyCloudSdfParameter
  post   BloomParameter, MotionBlurParameter, ColorShadowParameter, DropShadowParameter
  plant  PlantParameter (vegetation wind)

Every keyframe of <Component>.active_ that is true becomes false. --base mods are read first, so a
filter already edited by another mod (e.g. relinkdecktuner.towns) is patched on top of that edit;
give this mod after the base mods to patch_index.py so its copy wins.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import msgpack_span as ms  # noqa: E402
from build_mod import ModWriter, keyframe_values, verify_msg  # noqa: E402
from gbfr_archive import Archive  # noqa: E402

GROUPS = {
    "snn": ("SymmetricNearestNeighborParameter",),
    "dof": ("DofParameter",),
    "aal": ("AmbientAreaLightParameter",),
    "sky": ("SkyCloudSdfParameter",),
    "post": ("BloomParameter", "MotionBlurParameter", "ColorShadowParameter", "DropShadowParameter"),
    "plant": ("PlantParameter",),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game_dir")
    ap.add_argument("out_dir")
    ap.add_argument("mod_id")
    ap.add_argument("groups")
    ap.add_argument("--base", nargs="*", default=[])
    args = ap.parse_args()

    comps = set()
    for g in args.groups.split(","):
        comps.update(GROUPS[g])

    arc = Archive(args.game_dir)
    paths = [l.split("\t")[0] for l in open(os.path.join(args.game_dir, "src/dumps/resolved.tsv"), encoding="utf-8")]
    mod = ModWriter(args.out_dir, args.mod_id, f"Relink Deck Tuner - effects off ({args.groups})",
                    "Switches off post-process components in every area filter (performance test).")
    per_comp = {c: 0 for c in comps}
    for p in paths:
        if not (p.startswith("filter/") and p.endswith(".ccomp.msg")):
            continue
        buf = None
        for b in args.base:
            local = os.path.join(b, "GBFR", "data", *p.split("/"))
            if os.path.exists(local):
                buf = open(local, "rb").read()
        if buf is None:
            buf = arc.read(p)
        root, _ = ms.parse(buf)
        patches = []
        for node in ms.walk_maps(root):
            attr = node.get("Attribute")
            if attr is None or node.get("NodeType") is None or node.get("NodeType").value != "Attribute":
                continue
            obj = node.get("ObjectType").value
            if obj in comps and attr.value == "active_":
                for v in keyframe_values(node):
                    if v.value is True:
                        patches.append((v, False))
                        per_comp[obj] += 1
        if patches:
            out = ms.apply(buf, patches)
            verify_msg(buf, out, len(patches))
            mod.write(p, out)
            mod.changes.append(f"{p}: {len(patches)} active_ keyframes -> false")
    for c, n in sorted(per_comp.items()):
        print(f"  {c}: {n} keyframes off")
    mod.finish()


if __name__ == "__main__":
    main()
