#!/bin/bash
# Install Relink Deck Tuner into the game folder (Steam Deck / Linux).
# Usage: bash install.sh "/path/to/Granblue Fantasy Relink"
# Backs up what it replaces to <game>/relinkdecktuner_backup_<date>/ and verifies every file.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
game=${1:?give the game folder}
[ -f "$game/granblue_fantasy_relink.exe" ] || { echo "granblue_fantasy_relink.exe not found in $game"; exit 1; }
if pgrep -f granblue_fantasy_relink.exe >/dev/null 2>&1; then echo "close the game before installing"; exit 1; fi
bk="$game/relinkdecktuner_backup_$(date +%Y%m%d_%H%M)"
mkdir -p "$bk/data"
for f in data.i winmm.dll scripts; do [ -e "$game/$f" ] && cp -a "$game/$f" "$bk/"; done
for d in filter layout model system; do [ -e "$game/data/$d" ] && cp -a "$game/data/$d" "$bk/data/"; done
[ -e "$game/data.i.orig" ] || cp -p "$game/data.i" "$game/data.i.orig"   # factory data.i, used by uninstall
rm -rf "$game/data/filter" "$game/data/layout" "$game/data/model" "$game/data/system" "$game/scripts"
cp -a "$here/game/." "$game/"
(cd "$game" && sha256sum -c --quiet "$here/SHA256SUMS.txt") && echo "installed and verified ($(wc -l < "$here/SHA256SUMS.txt") files). Backup: $bk"
