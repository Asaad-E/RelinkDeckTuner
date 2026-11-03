#!/bin/bash
# Restore the factory game: put back data.i.orig and remove the mod files.
# Usage: bash uninstall.sh "/path/to/Granblue Fantasy Relink"
set -euo pipefail
game=${1:?give the game folder}
[ -f "$game/data.i.orig" ] || { echo "data.i.orig not found: cannot restore the factory index"; exit 1; }
if pgrep -f granblue_fantasy_relink.exe >/dev/null 2>&1; then echo "close the game first"; exit 1; fi
cp -p "$game/data.i.orig" "$game/data.i"
rm -rf "$game/winmm.dll" "$game/scripts" "$game/data/filter" "$game/data/layout" "$game/data/model" "$game/data/system"
echo "factory game restored (data/sound and data/ui belong to the game and are kept)"
