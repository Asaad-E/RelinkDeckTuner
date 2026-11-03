"""Build the "Relink Deck Tuner" data mods (layer A) from the installed game.

Usage:
  python -I build_mod.py <game_dir> <out_dir> [--npc-density 1.0] [--fog-shadow-mask 511]

Produces three independent Reloaded-II mods (each depends on the GBFR Mod Manager):
  RelinkDeckTuner.Options   A1  show hidden PC graphics options in the in-game menu
  RelinkDeckTuner.Dialogue  A2  no dynamic shadows on event (cutscene/dialogue) point lights
  RelinkDeckTuner.Towns     A3  simple-shadow / fog-downscale quality flags in town filters
                            A4  no volumetric light in pc00 church
                            A5  (optional) scale NPC crowd density
  RelinkDeckTuner.Menu      A6  RelinkDeckTuner settings as Graphics options (--menu-only builds just this)
Every output file is re-parsed and diffed against the original; only the intended
fields may differ, otherwise the build fails. A manifest.txt lists each change.
"""
import argparse
import json
import os
import struct
import sys

import msgpack

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import msgpack_span as ms  # noqa: E402
from gbfr_archive import Archive  # noqa: E402
from option_table import hash32  # noqa: E402

VERSION = "0.1.0"
TOWN_STAGES = ("pc00", "pd00")
EMPTY_HASH = hash32("")

# A1: hidden options to expose -> template row whose PC visibility flags are copied.
HIDDEN_OPTIONS = {
    "TXT_PAU_OPT_GRA_SHADOW": "TXT_PAU_OPT_GRA_TEXTURE",
    "TXT_PAU_OPT_GRA_CHARA": "TXT_PAU_OPT_GRA_TEXTURE",
    "TXT_PAU_OPT_GRA_BG": "TXT_PAU_OPT_GRA_TEXTURE",
    "TXT_PAU_OPT_GRA_EFFECT": "TXT_PAU_OPT_GRA_TEXTURE",
    "TXT_PAU_OPT_GRA_DECAL": "TXT_PAU_OPT_GRA_TEXTURE",
    "TXT_PAU_OPT_GRA_DEPTH": "TXT_PAU_OPT_GRA_VSYNC",
}
# Flag bytes that differ between PC-visible and PC-hidden rows (see report §5.3).
VISIBILITY_FLAG_BYTES = (5, 9, 11, 13)
INSERT_AFTER = "TXT_PAU_OPT_GRA_SIMULATION"
# Labels that are empty in the shipped text tables.
MISSING_LABELS = {
    "TXT_PAU_OPT_GRA_DECAL": {"default": "Decals", "es": "Calcomanías", "jp": "デカール"},
    "TXT_PAU_OPT_GRA_DEPTH": {"default": "Depth of Field", "es": "Profundidad de campo", "jp": "被写界深度"},
}
TEXT_REGIONS = ("bp", "cs", "ct", "en", "es", "fr", "ge", "it", "jp", "ko")

ITEM_ROW = 52
MENU_ROW = 180
MENU_ITEMS = 38


class ModWriter:
    def __init__(self, out_dir, mod_id, name, description):
        self.root = os.path.join(out_dir, mod_id)
        self.mod_id, self.name, self.description = mod_id, name, description
        self.changes = []

    def write(self, game_path, data):
        dst = os.path.join(self.root, "GBFR", "data", *game_path.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(data)

    def finish(self):
        if not self.changes:
            print(f"[{self.mod_id}] no changes, skipped")
            return
        cfg = {
            "ModId": self.mod_id, "ModName": self.name, "ModAuthor": "Relink Deck Tuner",
            "ModVersion": VERSION, "ModDescription": self.description, "ModDll": "",
            "ModIcon": "", "ModR2RManagedDll32": "", "ModR2RManagedDll64": "",
            "ModNativeDll32": "", "ModNativeDll64": "", "Tags": [], "CanUnload": False,
            "HasExports": False, "IsLibrary": False, "ReleaseMetadataFileName": "",
            "PluginData": {}, "IsUniversalMod": False,
            "ModDependencies": ["gbfrelink.utility.manager"], "OptionalDependencies": [],
            "SupportedAppId": ["granblue_fantasy_relink.exe"],
        }
        with open(os.path.join(self.root, "ModConfig.json"), "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=2)
        with open(os.path.join(self.root, "manifest.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(self.changes) + "\n")
        print(f"[{self.mod_id}] {len(self.changes)} changes")


# ---------- verification ----------

def flatten(obj, prefix="", out=None):
    out = {} if out is None else out
    if isinstance(obj, dict):
        for k, v in obj.items():
            flatten(v, f"{prefix}/{k}", out)
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            flatten(v, f"{prefix}[{i}]", out)
    else:
        out[prefix] = obj
    return out


def verify_msg(orig, patched, expected_changes):
    """Both must decode; the number of differing leaves must equal expected_changes."""
    a = flatten(msgpack.unpackb(orig, raw=False, strict_map_key=False))
    b = flatten(msgpack.unpackb(patched, raw=False, strict_map_key=False))
    if a.keys() != b.keys():
        raise AssertionError("structure changed")
    diff = [k for k in a if a[k] != b[k]]
    if len(diff) != expected_changes:
        raise AssertionError(f"expected {expected_changes} changed leaves, got {len(diff)}")


# ---------- A1: options ----------

def build_options(arc, mod):
    item_path, menu_path = "system/table/option_item.tbl", "system/table/option_menu.tbl"
    items = bytearray(arc.read(item_path))
    n = struct.unpack_from("<Q", items, 0)[0]
    if 8 + n * ITEM_ROW != len(items):
        raise SystemExit(f"{item_path}: unexpected layout ({len(items)} bytes, {n} rows); game version changed?")
    by_name = {}
    for r in range(n):
        key, name = struct.unpack_from("<2I", items, 8 + r * ITEM_ROW)
        by_name.setdefault(name, []).append((r, key))

    def row_of(txt):
        rows = by_name.get(hash32(txt))
        if not rows or len(rows) != 1:
            raise SystemExit(f"option row for {txt} not found uniquely")
        return rows[0]

    orig_items = bytes(items)
    for txt, template in HIDDEN_OPTIONS.items():
        r, _ = row_of(txt)
        tr, _ = row_of(template)
        f_off, t_off = 8 + r * ITEM_ROW + 36, 8 + tr * ITEM_ROW + 36
        for i in VISIBILITY_FLAG_BYTES:
            if items[f_off + i] != items[t_off + i]:
                items[f_off + i] = items[t_off + i]
                mod.changes.append(f"{item_path}: row {r} ({txt}) flag[{i}] -> {items[t_off + i]}")
    if items != orig_items:
        mod.write(item_path, bytes(items))

    menu = bytearray(arc.read(menu_path))
    m = struct.unpack_from("<Q", menu, 0)[0]
    if 8 + m * MENU_ROW != len(menu):
        raise SystemExit(f"{menu_path}: unexpected layout; game version changed?")
    anchor_key = row_of(INSERT_AFTER)[1]
    new_keys = [row_of(t)[1] for t in HIDDEN_OPTIONS]
    for r in range(m):
        off = 8 + r * MENU_ROW
        slots = list(struct.unpack_from(f"<{MENU_ITEMS}I", menu, off))
        if anchor_key not in slots:
            continue
        used = [s for s in slots if s != EMPTY_HASH]
        missing = [k for k in new_keys if k not in used]
        if not missing:
            continue
        if len(used) + len(missing) > MENU_ITEMS:
            raise SystemExit("graphics menu has no free slots")
        i = used.index(anchor_key) + 1
        used[i:i] = missing
        struct.pack_into(f"<{MENU_ITEMS}I", menu, off, *(used + [EMPTY_HASH] * (MENU_ITEMS - len(used))))
        mod.changes.append(f"{menu_path}: row {r} inserted {len(missing)} options after {INSERT_AFTER}")
    mod.write(menu_path, bytes(menu))

    for region in TEXT_REGIONS:
        path = f"system/table/text/{region}/text_ui.msg"
        buf = arc.read(path)
        root, _ = ms.parse(buf)
        patches = []
        for node in ms.walk_maps(root):
            col = node.get("column_")
            if col is None:
                continue
            tid = col.get("id_hash_")
            text = col.get("text_")
            if tid is None or text is None or tid.value not in MISSING_LABELS or text.value:
                continue
            labels = MISSING_LABELS[tid.value]
            patches.append((text, labels.get(region, labels["default"])))
        if patches:
            out = ms.apply(buf, patches)
            verify_msg(buf, out, len(patches))
            mod.write(path, out)
            mod.changes.append(f"{path}: filled {len(patches)} empty option labels")


# ---------- A6: Deck Tuner menu ----------
# Five hidden drop-down rows of the Graphics page are repurposed as RelinkDeckTuner settings: the
# .asi answers the game's option getter for these keys and receives the value on Apply. Only new text
# ids are added (descriptions wrap by hand, ~36 characters per line like the game's own); the rows get
# new names, descriptions and labels. Must match kMenu in RelinkDeckTuner.cpp.
PCT_LABELS = [(f"TXT_RDT_PCT_{p}", f"{p}%") for p in (100, 95, 90, 85, 80, 75, 70)]
LEVEL_LABELS = [("TXT_RDT_LVL_NORMAL", "Normal"), ("TXT_RDT_LVL_REDUCED", "Reduced"), ("TXT_RDT_LVL_MINIMUM", "Minimum")]
MENU_OPTIONS = [  # (key of the reused hidden option, name, description, labels, default label index)
    (0x2f6f51d1, ("TXT_RDT_OPT_BASE_SCALE", "Resolution Scale"),
     ("TXT_RDT_OPT_BASE_SCALE_INFO", "3D render scale outside dialogues.\nLower values raise the frame rate.\n(Relink Deck Tuner)"),
     PCT_LABELS, 0),
    (0xfbce1e5b, ("TXT_RDT_OPT_DLG_SCALE", "Dialogue Resolution"),
     ("TXT_RDT_OPT_DLG_SCALE_INFO", "3D render scale during dialogues\nand cutscenes.\n(Relink Deck Tuner)"),
     PCT_LABELS, 4),
    (0x4c1d6fce, ("TXT_RDT_OPT_UPSCALER", "Upscaler"),
     ("TXT_RDT_OPT_UPSCALER_INFO",
      "Filter that enlarges the reduced\n3D image. Takes effect after\nrestarting the game.\n(Relink Deck Tuner)"),
     [("TXT_RDT_UPS_FSR", "FSR 1.0"), ("TXT_RDT_UPS_BICUBIC", "Bicubic")], 0),
    (0x4c37cdf9, ("TXT_RDT_OPT_DLG_VEG", "Dialogue Vegetation"),
     ("TXT_RDT_OPT_DLG_VEG_INFO",
      "Vegetation detail during dialogues.\nReduced looks almost the same\nand is much faster.\n(Relink Deck Tuner)"),
     LEVEL_LABELS, 1),
    (0xffd48260, ("TXT_RDT_OPT_DLG_LOD", "Dialogue Scenery Detail"),
     ("TXT_RDT_OPT_DLG_LOD_INFO", "Detail of buildings and props\nduring dialogues.\n(Relink Deck Tuner)"),
     LEVEL_LABELS, 0),
]
SUBMENU_ROW = 44
SUBMENU_LABELS = 10


def menu_texts():
    texts = {}
    for _, name, info, labels, _ in MENU_OPTIONS:
        texts.update([name, info] + labels)
    return texts


def append_text_rows(buf, texts):
    """Append {id: text} rows to a text_*.msg ({rows_: [{column_: {...}}]}), keeping every existing byte."""
    root, _ = ms.parse(buf)
    rows = root.get("rows_")
    if rows is None or rows.kind != "array" or buf[rows.start] not in (0xDC, 0xDD):
        raise SystemExit("text table layout changed")
    n = len(rows.items) + len(texts)
    head, hlen = (b"\xdc" + struct.pack(">H", n), 3) if buf[rows.start] == 0xDC else (b"\xdd" + struct.pack(">I", n), 5)
    new = b"".join(msgpack.packb({"column_": {"id_hash_": i, "subid_hash_": "", "text_": t}}) for i, t in texts.items())
    out = buf[:rows.start] + head + buf[rows.start + hlen:rows.end] + new + buf[rows.end:]
    a = msgpack.unpackb(buf, raw=False)["rows_"]
    b = msgpack.unpackb(out, raw=False)["rows_"]
    if b[:len(a)] != a or [r["column_"]["id_hash_"] for r in b[len(a):]] != list(texts):
        raise AssertionError("text rows not appended cleanly")
    return out


def build_menu(arc, mod):
    item_path, menu_path = "system/table/option_item.tbl", "system/table/option_menu.tbl"
    sub_path = "system/table/option_submenu.tbl"
    texts = menu_texts()

    en = msgpack.unpackb(arc.read("system/table/text/en/text_ui.msg"), raw=False)["rows_"]
    taken = {hash32(r["column_"]["id_hash_"]) for r in en}
    for tid in texts:
        if tid in {r["column_"]["id_hash_"] for r in en} or hash32(tid) in taken:
            raise SystemExit(f"{tid} collides with an existing text id")

    items = bytearray(arc.read(item_path))
    n = struct.unpack_from("<Q", items, 0)[0]
    if 8 + n * ITEM_ROW != len(items):
        raise SystemExit(f"{item_path}: unexpected layout; game version changed?")
    key_row = {struct.unpack_from("<I", items, 8 + r * ITEM_ROW)[0]: r for r in range(n)}
    # Player Silhouette: a PC-visible drop-down row that can also be changed in the pause menu
    # (Textures-like rows are title-screen only).
    template = key_row[0x485fcc79]
    t_flags = 8 + template * ITEM_ROW + 36
    for key, name, info, labels, default in MENU_OPTIONS:
        r = key_row[key]
        off = 8 + r * ITEM_ROW
        struct.pack_into("<2I", items, off + 4, hash32(name[0]), hash32(info[0]))  # Name, Description
        struct.pack_into("<I", items, off + 16, 0)                                # Min
        struct.pack_into("<I", items, off + 24, default)                          # Default
        items[off + 37:off + 52] = items[t_flags + 1:t_flags + 16]  # flag 0 is per-row
        mod.changes.append(f"{item_path}: row {r} key {key:08x} -> {name[1]} (default {labels[default][1]}, visible on PC)")
    mod.write(item_path, bytes(items))

    sub = bytearray(arc.read(sub_path))
    m = struct.unpack_from("<Q", sub, 0)[0]
    if 8 + m * SUBMENU_ROW != len(sub):
        raise SystemExit(f"{sub_path}: unexpected layout; game version changed?")
    sub_row = {struct.unpack_from("<I", sub, 8 + r * SUBMENU_ROW + 40)[0]: r for r in range(m)}
    for key, name, _, labels, _ in MENU_OPTIONS:
        r = sub_row[key]  # KeyError = not a drop-down row
        hashes = [hash32(i) for i, _ in labels] + [EMPTY_HASH] * (SUBMENU_LABELS - len(labels))
        struct.pack_into(f"<{SUBMENU_LABELS}I", sub, 8 + r * SUBMENU_ROW, *hashes)
        mod.changes.append(f"{sub_path}: row {r} labels -> " + " / ".join(t for _, t in labels))
    mod.write(sub_path, bytes(sub))

    menu = bytearray(arc.read(menu_path))
    m = struct.unpack_from("<Q", menu, 0)[0]
    if 8 + m * MENU_ROW != len(menu):
        raise SystemExit(f"{menu_path}: unexpected layout; game version changed?")
    anchor = 0x9a9f05f8  # Rendering: marks the Graphics page
    keys = [k for k, *_ in MENU_OPTIONS]
    pages = 0
    for r in range(m):
        off = 8 + r * MENU_ROW
        slots = list(struct.unpack_from(f"<{MENU_ITEMS}I", menu, off))
        if anchor not in slots:
            continue
        used = [s for s in slots if s != EMPTY_HASH and s not in keys] + keys  # our rows at the end
        if len(used) > MENU_ITEMS:
            raise SystemExit("graphics menu has no free slots")
        struct.pack_into(f"<{MENU_ITEMS}I", menu, off, *(used + [EMPTY_HASH] * (MENU_ITEMS - len(used))))
        mod.changes.append(f"{menu_path}: row {r} appended {len(keys)} Deck Tuner options")
        pages += 1
    if pages != 1:
        raise SystemExit(f"expected 1 graphics page, found {pages}")
    mod.write(menu_path, bytes(menu))

    for region in TEXT_REGIONS:
        path = f"system/table/text/{region}/text_ui.msg"
        mod.write(path, append_text_rows(arc.read(path), texts))
        mod.changes.append(f"{path}: added {len(texts)} text rows (English)")


# ---------- A2: dialogue lights ----------

def build_dialogue(arc, mod, paths):
    total = 0
    for p in paths:
        if not (p.startswith("layout/timeline/") and p.endswith("_light.scene.msg")):
            continue
        buf = arc.read(p)
        root, _ = ms.parse(buf)
        patches = []
        for node in ms.walk_maps(root):
            light = node.get("PointLightSceneObject")
            if light is None:
                continue
            data = light.get("data_")
            flag = data.get("bCastDynamicShadow_") if data else None
            if flag is not None and flag.value:
                patches.append((flag, 0))
        if patches:
            out = ms.apply(buf, patches)
            verify_msg(buf, out, len(patches))
            mod.write(p, out)
            mod.changes.append(f"{p}: {len(patches)} point lights bCastDynamicShadow_ -> 0")
            total += len(patches)
    print(f"  dialogue: {total} lights patched")


# ---------- A3/A4: town filters ----------

def keyframe_values(attr_node):
    """Attribute node -> value nodes of its keyframes (Keys.Array[i][2])."""
    keys = attr_node.get("Keys")
    arr = keys.get("Array") if keys else None
    return [k.items[2] for k in (arr or []) if k.kind == "array" and len(k.items) > 2]


def build_town_filters(arc, mod, paths, mask):
    for p in paths:
        if not p.startswith("filter/") or p.split("/")[1] not in TOWN_STAGES or "/cutscene/" in p:
            continue
        buf = arc.read(p)
        root, _ = ms.parse(buf)
        patches, notes = [], []
        for node in ms.walk_maps(root):
            attr = node.get("Attribute")
            if attr is None or node.get("NodeType") is None or node.get("NodeType").value != "Attribute":
                continue
            obj = node.get("ObjectType").value
            if attr.value in ("useSimpleShadowQualityFlag_", "useFogDownScaleQualityFlag_"):
                for v in keyframe_values(node):
                    if v.value != mask:
                        notes.append(f"{obj}.{attr.value} {v.value} -> {mask}")
                        patches.append((v, mask))
            elif obj == "VolumetricLightParameter" and attr.value == "active_" and p.split("/")[-1].startswith("church_"):
                for v in keyframe_values(node):
                    if v.value is True:
                        notes.append("VolumetricLightParameter.active_ true -> false")
                        patches.append((v, False))
        if patches:
            out = ms.apply(buf, patches)
            verify_msg(buf, out, len(patches))
            mod.write(p, out)
            mod.changes += [f"{p}: {n}" for n in notes]


# ---------- A5: NPC density ----------

def build_npc_density(arc, mod, scale):
    if abs(scale - 1.0) < 1e-6:
        return
    for p in ("system/NPC/c00_npcConfig.msg", "system/NPC/d00_npcConfig.msg"):
        buf = arc.read(p)
        root, _ = ms.parse(buf)
        patches = []
        for node in ms.walk_maps(root):
            d = node.get("density_")
            if d is not None and isinstance(d.value, str):
                new = f"{float(d.value) * scale:.4g}"
                patches.append((d, new))
                mod.changes.append(f"{p}: density_ {d.value} -> {new}")
        if patches:
            out = ms.apply(buf, patches)
            verify_msg(buf, out, len(patches))
            mod.write(p, out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game_dir")
    ap.add_argument("out_dir")
    ap.add_argument("--npc-density", type=float, default=1.0, help="0.5 = half density (A5, experimental)")
    ap.add_argument("--fog-shadow-mask", type=int, default=511, help="value for the town quality-flag masks (A3)")
    ap.add_argument("--menu-only", action="store_true", help="only build the Deck Tuner menu mod (A6)")
    args = ap.parse_args()

    arc = Archive(args.game_dir)
    menu = ModWriter(args.out_dir, "relinkdecktuner.menu", "Relink Deck Tuner - Menu Options",
                     "Adds the Relink Deck Tuner settings to the Graphics options (needs RelinkDeckTuner.asi 0.13+).")
    build_menu(arc, menu)
    menu.finish()
    if args.menu_only:
        return
    paths = [l.split("\t")[0] for l in open(os.path.join(args.game_dir, "src/dumps/resolved.tsv"), encoding="utf-8")]

    opts = ModWriter(args.out_dir, "relinkdecktuner.options", "Relink Deck Tuner - Hidden Graphics Options",
                     "Shows Shadow Quality, Character/Background Models, Effects, Decals and Depth of Field in the PC graphics menu.")
    build_options(arc, opts)
    opts.finish()

    dlg = ModWriter(args.out_dir, "relinkdecktuner.dialogue", "Relink Deck Tuner - Lighter Dialogue Scenes",
                    "Disables per-frame dynamic shadows of cutscene/dialogue point lights.")
    build_dialogue(arc, dlg, paths)
    dlg.finish()

    town = ModWriter(args.out_dir, "relinkdecktuner.towns", "Relink Deck Tuner - Lighter Towns",
                     "Forces simple shadows and downscaled fog in town areas, disables church volumetric light.")
    build_town_filters(arc, town, paths, args.fog_shadow_mask)
    build_npc_density(arc, town, args.npc_density)
    town.finish()


if __name__ == "__main__":
    main()
