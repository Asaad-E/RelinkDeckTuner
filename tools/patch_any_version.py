"""Install or build Relink Deck Tuner for Granblue Fantasy: Relink >= 2.0.5 (e.g. 2.0.6+).

Unlike the prebuilt v2.0.5 Steam Deck package (make_deck_pkg.sh), this script
reads the target game folder's own data.i / data.N archives so it works on
newer game versions without shipping a v2.0.5 data.i.

Usage:
  python -I patch_any_version.py <game_dir> [--mode direct|reloaded] [--out <reloaded_mods_dir>]
                                 [--npc-density 0.8] [--foliage-level 1] [--force-ini]
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
SRC_DIR = os.path.dirname(TOOLS_DIR)


def run(cmd):
    res = subprocess.run(cmd, check=False)
    if res.returncode != 0:
        raise SystemExit(f"command failed ({res.returncode}): {' '.join(cmd)}")


def copy_if_exists(src, dst):
    if os.path.isfile(src):
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
        return True
    return False


def install_scripts(game_dir, force_ini=False):
    scripts_dir = os.path.join(game_dir, "scripts")
    os.makedirs(scripts_dir, exist_ok=True)

    asi_src = os.path.join(SRC_DIR, "asi", "build", "RelinkDeckTuner.asi")
    ini_src = os.path.join(SRC_DIR, "asi", "RelinkDeckTuner.ini")
    if not os.path.isfile(asi_src):
        raise SystemExit(f"RelinkDeckTuner.asi not found at {asi_src}; run asi/build.bat first")
    shutil.copy2(asi_src, os.path.join(scripts_dir, "RelinkDeckTuner.asi"))

    ini_dst = os.path.join(scripts_dir, "RelinkDeckTuner.ini")
    if force_ini or not os.path.isfile(ini_dst):
        copy_if_exists(ini_src, ini_dst)

    # Copy ASI loader and GBFRelinkFix if present in deck_pkg/ and not already installed.
    deck_pkg = os.path.join(SRC_DIR, "deck_pkg")
    winmm_dst = os.path.join(game_dir, "winmm.dll")
    if not os.path.isfile(winmm_dst):
        copy_if_exists(os.path.join(deck_pkg, "winmm.dll"), winmm_dst)
    fix_asi_dst = os.path.join(scripts_dir, "GBFRelinkFix.asi")
    if not os.path.isfile(fix_asi_dst):
        copy_if_exists(os.path.join(deck_pkg, "scripts", "GBFRelinkFix.asi"), fix_asi_dst)
    fix_ini_dst = os.path.join(scripts_dir, "GBFRelinkFix.ini")
    if not os.path.isfile(fix_ini_dst):
        copy_if_exists(os.path.join(deck_pkg, "scripts", "GBFRelinkFix.ini"), fix_ini_dst)


def build_mods(game_dir, index_path, mod_out_dir, npc_density, foliage_level):
    run([
        sys.executable, "-I", os.path.join(TOOLS_DIR, "build_mod.py"),
        game_dir, mod_out_dir,
        "--npc-density", str(npc_density),
        "--skip-options",
        "--allow-menu-skip",
        "--index", index_path,
    ])
    run([
        sys.executable, "-I", os.path.join(TOOLS_DIR, "build_foliage.py"),
        game_dir, mod_out_dir,
        "--level", str(foliage_level),
        "--index", index_path,
    ])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game_dir", help="folder containing granblue_fantasy_relink.exe and data.i")
    ap.add_argument("--mode", choices=("direct", "reloaded"), default="direct",
                    help="direct patches data.i + data/ in place; reloaded outputs Reloaded-II mod folders")
    ap.add_argument("--out", help="Reloaded-II Mods output directory (required for --mode reloaded)")
    ap.add_argument("--npc-density", type=float, default=0.8)
    ap.add_argument("--foliage-level", type=int, default=1)
    ap.add_argument("--force-ini", action="store_true", help="overwrite existing scripts/RelinkDeckTuner.ini")
    ap.add_argument("--skip-scripts", action="store_true", help="only build/patch data mods")
    args = ap.parse_args()

    game_dir = os.path.abspath(args.game_dir)
    exe_path = os.path.join(game_dir, "granblue_fantasy_relink.exe")
    data_i = os.path.join(game_dir, "data.i")
    data_i_orig = os.path.join(game_dir, "data.i.orig")
    if not os.path.isfile(exe_path) or not os.path.isfile(data_i):
        raise SystemExit(f"granblue_fantasy_relink.exe or data.i not found in {game_dir}")

    if args.mode == "reloaded":
        if not args.out:
            raise SystemExit("--out <reloaded_mods_dir> is required with --mode reloaded")
        os.makedirs(args.out, exist_ok=True)
        index_src = data_i_orig if os.path.isfile(data_i_orig) else data_i
        build_mods(game_dir, index_src, args.out, args.npc_density, args.foliage_level)
        if not args.skip_scripts:
            install_scripts(game_dir, force_ini=args.force_ini)
        print(f"Reloaded-II mods built in {args.out}")
        return

    # Direct mode: keep factory data.i in data.i.orig so re-runs stay idempotent across updates.
    if not os.path.isfile(data_i_orig):
        shutil.copy2(data_i, data_i_orig)
        print(f"Backed up factory index to {data_i_orig}")

    with tempfile.TemporaryDirectory() as tmp:
        mods_dir = os.path.join(tmp, "mods")
        staged = os.path.join(tmp, "staged")
        os.makedirs(mods_dir, exist_ok=True)
        build_mods(game_dir, data_i_orig, mods_dir, args.npc_density, args.foliage_level)

        mod_order = ["relinkdecktuner.dialogue", "relinkdecktuner.towns",
                     "relinkdecktuner.foliage", "relinkdecktuner.menu"]
        present = [os.path.join(mods_dir, m) for m in mod_order
                   if os.path.isdir(os.path.join(mods_dir, m, "GBFR", "data"))]
        if not present:
            raise SystemExit("no data mods produced")

        run([sys.executable, "-I", os.path.join(TOOLS_DIR, "patch_index.py"),
             data_i_orig, staged, *present])

        # Copy staged external files and atomically swap data.i.
        staged_data = os.path.join(staged, "data")
        if os.path.isdir(staged_data):
            shutil.copytree(staged_data, os.path.join(game_dir, "data"), dirs_exist_ok=True)
        tmp_idx = os.path.join(game_dir, "data.i.rdt_new")
        shutil.copy2(os.path.join(staged, "data.i"), tmp_idx)
        os.replace(tmp_idx, data_i)

    if not args.skip_scripts:
        install_scripts(game_dir, force_ini=args.force_ini)
    print(f"Patched {game_dir} successfully (universal >=2.0.5)")


if __name__ == "__main__":
    main()
