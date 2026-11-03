#!/bin/bash
# Instala Relink Deck Tuner en la carpeta del juego (Steam Deck / Linux, o Git Bash en Windows).
# Uso: bash instalar.sh "/ruta/a/Granblue Fantasy Relink"
# Guarda lo que reemplaza en <juego>/relinkdecktuner_backup_<fecha>/ y comprueba cada archivo.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
game=${1:?indica la carpeta del juego}
[ -f "$game/granblue_fantasy_relink.exe" ] || { echo "no encuentro granblue_fantasy_relink.exe en $game"; exit 1; }
if pgrep -f granblue_fantasy_relink.exe >/dev/null 2>&1; then echo "cierra el juego antes de instalar"; exit 1; fi
bk="$game/relinkdecktuner_backup_$(date +%Y%m%d_%H%M)"
mkdir -p "$bk/data"
for f in data.i winmm.dll scripts; do [ -e "$game/$f" ] && cp -a "$game/$f" "$bk/"; done
for d in filter layout model system; do [ -e "$game/data/$d" ] && cp -a "$game/data/$d" "$bk/data/"; done
[ -e "$game/data.i.orig" ] || cp -p "$game/data.i" "$game/data.i.orig"   # data.i de fábrica, para desinstalar
rm -rf "$game/data/filter" "$game/data/layout" "$game/data/model" "$game/data/system" "$game/scripts"
cp -a "$here/juego/." "$game/"
(cd "$game" && sha256sum -c --quiet "$here/SHA256SUMS.txt") && echo "instalado y verificado ($(wc -l < "$here/SHA256SUMS.txt") archivos). Copia previa: $bk"
