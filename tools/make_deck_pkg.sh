#!/bin/bash
# Assemble the Steam Deck package in deck_final/ from the built mods and binaries.
#   data.i + data/   : dialogue + towns + foliage_lod1 (patch_index.py, external files)
#   winmm.dll        : Ultimate ASI Loader (from GBFRelinkFix 1.1.5, already in deck_pkg/)
#   scripts/         : GBFRelinkFix.asi/.ini, RelinkDeckTuner.asi/.ini
# Usage (from src/): bash tools/make_deck_pkg.sh
set -euo pipefail
cd "$(dirname "$0")/.."
rm -rf deck_final
python -I tools/patch_index.py backup/data.i deck_final \
  dist/relinkdecktuner.dialogue dist/relinkdecktuner.towns dist/relinkdecktuner.foliage_lod1
mkdir -p deck_final/scripts
cp deck_pkg/winmm.dll deck_final/
cp deck_pkg/scripts/GBFRelinkFix.asi deck_pkg/scripts/GBFRelinkFix.ini deck_final/scripts/
cp asi/build/RelinkDeckTuner.asi asi/RelinkDeckTuner.ini deck_final/scripts/
(cd deck_final && find . -type f | sort | sed 's#^\./##' > ../deck_final_files.txt)
echo "deck_final: $(wc -l < deck_final_files.txt) files"
