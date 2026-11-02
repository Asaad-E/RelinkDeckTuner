"""Dump bool-key values of selected filter params across events (per category).

Usage: python -I evtb_bools.py <game_dir>
For each (filter, field) prints how many keys are True/False per event category.
Field indices follow GBFRDataTools FilterData.fbs (0-based vtable slots).
"""
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbfr_archive import Archive  # noqa: E402
from evtb_filters import FILTER_TYPES, Table  # noqa: E402

# filter name -> list of (slot, label) for TimelineKey_Bool fields
BOOL_FIELDS = {
    "Dof": [(5, "field_6"), (6, "field_7"), (11, "field_12")],
    "Bokeh": [(0, "field_1")],
    "DeferredPointLight": [(4, "field_5"), (25, "field_26")],
    "Bloom": [(5, "field_6")],
    "HeightFog": [(0, "field_1"), (1, "field_2")],
}


def key_values(key_table):
    """TimelineKey_Bool: slot0 = bool, slot1 = [KeyParamData_Bool{key_data, value}]."""
    vals = []
    if key_table is None:
        return vals
    for kp in key_table.tables(1):
        p = kp.field(1)
        vals.append(bool(kp.buf[p]) if p else False)
    return vals


def main():
    arc = Archive(sys.argv[1])
    paths = [l.split("\t")[0] for l in open("src/dumps/resolved.tsv", encoding="utf-8")
             if l.startswith("system/event/") and l.split("\t")[0].endswith("master.evtb")]
    stats = collections.defaultdict(collections.Counter)
    for p in paths:
        cat = p.split("/")[2]
        buf = arc.read(p)
        root = Table(buf, struct.unpack_from("<I", buf, 0)[0])
        for cut in root.tables(14):
            for seq in cut.tables(7):
                for sub in seq.tables(1):
                    t = sub.u8(4)
                    name = FILTER_TYPES[t - 1] if 1 <= t <= len(FILTER_TYPES) else None
                    if name not in BOOL_FIELDS:
                        continue
                    data = sub.table(5)
                    if data is None:
                        continue
                    for slot, label in BOOL_FIELDS[name]:
                        for v in key_values(data.table(slot)):
                            stats[(name, label)][(cat, v)] += 1
    for (name, label), c in sorted(stats.items()):
        row = "  ".join(f"{cat}:T={c[(cat, True)]}/F={c[(cat, False)]}" for cat in ("ci", "ct", "cw"))
        print(f"{name}.{label:9s} {row}")


if __name__ == "__main__":
    main()
