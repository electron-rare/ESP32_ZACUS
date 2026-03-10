#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/data/apps/nes_emulator/roms"
mkdir -p "${OUT_DIR}"

declare -a URLS=(
  "https://raw.githubusercontent.com/retrobrews/nes-games/master/ambushed.nes"
  "https://raw.githubusercontent.com/retrobrews/nes-games/master/croom.nes"
  "https://raw.githubusercontent.com/retrobrews/nes-games/master/debrisdodger.nes"
  "https://raw.githubusercontent.com/retrobrews/nes-games/master/driar.nes"
  "https://raw.githubusercontent.com/retrobrews/nes-games/master/flappyjack.nes"
)

echo "Downloading NES homebrew ROMs into ${OUT_DIR}"
for url in "${URLS[@]}"; do
  file_name="$(basename "${url}")"
  out_path="${OUT_DIR}/${file_name}"
  echo " - ${file_name}"
  curl -fL --retry 2 --connect-timeout 10 --max-time 60 -o "${out_path}" "${url}"
done

echo
echo "Done. ROMs ready for LittleFS upload at:"
echo "  ${OUT_DIR}"
echo
echo "Next steps:"
echo "  1) pio run -e freenove_esp32s3_full_with_ui -t buildfs"
echo "  2) pio run -e freenove_esp32s3_full_with_ui -t uploadfs"
