#!/bin/bash
# Build release/RelinkDeckTuner_<version>/ (+ .zip) from deck_final/ and release_template/.
# Usage (from src/): bash tools/make_deck_pkg.sh && bash tools/make_release.sh v0.11
set -euo pipefail
cd "$(dirname "$0")/.."
V=${1:?version, e.g. v0.11}
R=release/RelinkDeckTuner_$V
rm -rf "$R" "$R.zip"
mkdir -p "$R/game" "$R/licenses"
cp -r deck_final/. "$R/game/"
sed "s/{VERSION}/$V/" release_template/README.md > "$R/README.md"
cp release_template/install.sh release_template/uninstall.sh release_template/install.bat release_template/uninstall.bat "$R/"
cp asi/third_party/fsr1/license.txt "$R/licenses/AMD_FidelityFX_FSR1_LICENSE.txt"
cp release_template/GBFRelinkFix_LICENSE.md "$R/licenses/"
(cd "$R/game" && find . -type f | sort | sed 's#^\./##' | tr '\n' '\0' | xargs -0 sha256sum > ../SHA256SUMS.txt)
(cd release && powershell -NoProfile -Command "Compress-Archive -Path 'RelinkDeckTuner_$V' -DestinationPath 'RelinkDeckTuner_$V.zip' -Force")
echo "$R: $(wc -l < "$R/SHA256SUMS.txt") files; $(du -h "$R.zip" | cut -f1) zip"
