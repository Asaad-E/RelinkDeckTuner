"""Count Timeline filter (post-process / lighting) tracks used by event cutscenes.

Schema: GBFRDataTools Evtb_EventCutsceneBinary.fbs / FilterData.fbs.
Usage: python -I evtb_filters.py <game_dir> <out.tsv>
Writes one row per event: path, cut count, and per-filter-type track counts.
"""
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbfr_archive import Archive  # noqa: E402

FILTER_TYPES = [
    "Bloom", "Dof", "DropShadow", "EffectColor", "MotionBlur", "CameraBlur",
    "AmbientAreaLight", "HueSaturation", "TemporalAntiAliasing", "DeferredLight",
    "ImageOverlay", "PlanarCloud", "Fog", "UNUSED_BrightnessContrast",
    "UNUSED_SymmetricNearestNeighbor", "ColorShadow", "ColorMatrixFilter", "LocalIBL",
    "IBL", "RadialBlur", "Bokeh", "VolumetricLight", "DiffusionFilter", "SkyCloudSdf",
    "VolumeCloud", "CharacterShaderParam", "Plant", "SkyCube", "DeferredPointLight",
    "Tonemap", "Global", "UNUSED_ColorCorrection", "GrandCypherWing", "HeightFog",
    "BahamutAlter",
]


class Table:
    def __init__(self, buf, pos):
        self.buf, self.pos = buf, pos
        self.vt = pos - struct.unpack_from("<i", buf, pos)[0]
        self.nfields = (struct.unpack_from("<H", buf, self.vt)[0] - 4) // 2

    def field(self, i):
        if i >= self.nfields:
            return None
        off = struct.unpack_from("<H", self.buf, self.vt + 4 + 2 * i)[0]
        return self.pos + off if off else None

    def u8(self, i):
        p = self.field(i)
        return self.buf[p] if p else 0

    def table(self, i):
        p = self.field(i)
        return Table(self.buf, p + struct.unpack_from("<I", self.buf, p)[0]) if p else None

    def tables(self, i):
        p = self.field(i)
        if not p:
            return []
        v = p + struct.unpack_from("<I", self.buf, p)[0]
        n = struct.unpack_from("<I", self.buf, v)[0]
        out = []
        for k in range(n):
            e = v + 4 + 4 * k
            out.append(Table(self.buf, e + struct.unpack_from("<I", self.buf, e)[0]))
        return out


def analyze(buf):
    root = Table(buf, struct.unpack_from("<I", buf, 0)[0])
    counts = collections.Counter()
    cuts = root.tables(14)
    for cut in cuts:
        for seq in cut.tables(7):
            for sub in seq.tables(1):
                t = sub.u8(4)  # union type slot (1-based)
                name = FILTER_TYPES[t - 1] if 1 <= t <= len(FILTER_TYPES) else f"type{t}"
                counts[name] += 1
    return len(cuts), counts


def main():
    game_dir, out = sys.argv[1], sys.argv[2]
    arc = Archive(game_dir)
    paths = [l.split("\t")[0] for l in open(sys.argv[3] if len(sys.argv) > 3 else "src/dumps/resolved.tsv", encoding="utf-8")
             if l.startswith("system/event/") and l.split("\t")[0].endswith("master.evtb")]
    with open(out, "w", encoding="utf-8") as f:
        f.write("path\tcuts\t" + "\t".join(FILTER_TYPES) + "\n")
        for p in sorted(paths):
            try:
                ncuts, c = analyze(arc.read(p))
            except Exception as e:  # report and continue: schema may not cover every file
                print(f"FAIL {p}: {e}")
                continue
            f.write(f"{p}\t{ncuts}\t" + "\t".join(str(c.get(n, 0)) for n in FILTER_TYPES) + "\n")
    print(f"done {len(paths)}")


if __name__ == "__main__":
    main()
