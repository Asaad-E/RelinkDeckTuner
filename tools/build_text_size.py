"""Build the "larger text" data mod: scales the font size of chosen UI text components.

Usage: python -I build_text_size.py <game_dir> <out_dir> [--scale 1.2]

UI prefabs (.prfb) store each component as its type name, a list of property hashes and the
values inline; in a Text component the font size is a float32 in 1920x1080 layout units. The
offsets below were found by hand for GBFR v2.0.5 and are checked against the expected sizes, so
the build fails instead of writing garbage if the game files change. Only those floats change:
the files keep their size and every internal offset. The text boxes keep their size and wrap
the text, so only the glyphs grow. The mod is a Reloaded-style folder (GBFR/data/<path>) for
patch_index.py, like the build_mod.py mods.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbfr_archive import Archive  # noqa: E402

# prefab -> [(offset of the font-size float, original size, what it is)]
TARGETS = {
    "ui/layouts/common/win_cut/prefabs/win_cut01.prfb": [
        (0x100C, 50.0, "cutscene dialogue: speaker name"),
        (0x1374, 60.0, "cutscene dialogue: text (box 1500x300)"),
    ],
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game_dir")
    ap.add_argument("out_dir")
    ap.add_argument("--scale", type=float, default=1.2)
    args = ap.parse_args()
    arc = Archive(args.game_dir)
    manifest = []
    for path, fields in TARGETS.items():
        buf = bytearray(arc.read(path))
        for off, size, what in fields:
            found = struct.unpack_from("<f", buf, off)[0]
            if found != size:
                sys.exit(f"{path} +{off:#x}: font size {found:g}, expected {size:g} (game files changed?)")
            new = round(size * args.scale)
            struct.pack_into("<f", buf, off, new)
            manifest.append(f"{path} +{off:#x}: {what}: font size {size:g} -> {new}")
        dst = os.path.join(args.out_dir, "GBFR", "data", path)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(buf)
    with open(os.path.join(args.out_dir, "manifest.txt"), "w", newline="\n") as f:
        f.write("\n".join(manifest) + "\n")
    print("\n".join(manifest))


if __name__ == "__main__":
    main()
