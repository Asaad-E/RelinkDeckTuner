#!/bin/bash
# Deja el juego de fábrica: restaura data.i.orig y quita los archivos del mod.
# Uso: bash desinstalar.sh "/ruta/a/Granblue Fantasy Relink"
set -euo pipefail
game=${1:?indica la carpeta del juego}
[ -f "$game/data.i.orig" ] || { echo "no hay data.i.orig: no puedo restaurar el índice de fábrica"; exit 1; }
if pgrep -f granblue_fantasy_relink.exe >/dev/null 2>&1; then echo "cierra el juego antes"; exit 1; fi
cp -p "$game/data.i.orig" "$game/data.i"
rm -rf "$game/winmm.dll" "$game/scripts" "$game/data/filter" "$game/data/layout" "$game/data/model" "$game/data/system"
echo "juego de fábrica (data/sound y data/ui son del juego y se conservan)"
