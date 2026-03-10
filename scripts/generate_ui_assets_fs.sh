#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA_DIR="${ROOT_DIR}/data"

APPS=(
  audio_player
  camera_video
  dictaphone
  timer_tools
  flashlight
  calculator
  qr_scanner
  audiobook_player
  kids_webradio
  kids_podcast
  kids_drawing
  kids_coloring
  kids_music
  kids_yoga
  kids_meditation
  kids_languages
  kids_math
  kids_science
  kids_geography
  nes_emulator
)

palette_bg=("#0F2A5C" "#2A1E5C" "#1E4A50" "#49331A" "#213059")
palette_ac=("#FFD166" "#FF8ED8" "#7CFFEA" "#8BC6FF" "#FFB870")

mkdir -p "${DATA_DIR}/apps/shared/audio"
mkdir -p "${DATA_DIR}/webui/assets"
mkdir -p "${DATA_DIR}/webui/assets/fonts"

fetch_font() {
  local url="$1"
  local output="$2"
  if [[ -s "${output}" ]]; then
    return 0
  fi
  curl -fsSL "${url}" -o "${output}"
}

fetch_font "https://raw.githubusercontent.com/google/fonts/main/ofl/pressstart2p/PressStart2P-Regular.ttf" \
  "${DATA_DIR}/webui/assets/fonts/PressStart2P-Regular.ttf"
fetch_font "https://raw.githubusercontent.com/google/fonts/main/ofl/comicneue/ComicNeue-Regular.ttf" \
  "${DATA_DIR}/webui/assets/fonts/ComicNeue-Regular.ttf"
fetch_font "https://raw.githubusercontent.com/google/fonts/main/ofl/comicneue/ComicNeue-Bold.ttf" \
  "${DATA_DIR}/webui/assets/fonts/ComicNeue-Bold.ttf"

has_cmd() {
  command -v "$1" >/dev/null 2>&1
}

HAS_MAGICK=0
if has_cmd magick; then
  HAS_MAGICK=1
fi

# Global UI SFX
sox -n -r 16000 -c 1 "${DATA_DIR}/apps/shared/audio/ui_click.wav" synth 0.07 sine 1300 vol 0.35 fade q 0.001 0.07 0.01
sox -n -r 16000 -c 1 "${DATA_DIR}/apps/shared/audio/ui_back.wav" synth 0.08 sine 760  vol 0.35 fade q 0.001 0.08 0.02
sox -n -r 16000 -c 1 "${DATA_DIR}/apps/shared/audio/ui_success.wav" synth 0.12 sine 980 vol 0.35 fade q 0.001 0.12 0.02
sox -n -r 16000 -c 1 "${DATA_DIR}/apps/shared/audio/ui_error.wav" synth 0.13 sine 240 vol 0.35 fade q 0.001 0.13 0.03
sox -n -r 16000 -c 1 "${DATA_DIR}/apps/shared/audio/app_open.wav" synth 0.10 sine 660 vol 0.35 fade q 0.001 0.10 0.02

# WebUI assets
if [[ "${HAS_MAGICK}" == "1" ]]; then
  magick -size 640x220 "xc:#0B1E4A" \
    -fill "#FFD166" -draw "roundrectangle 8,8 632,212 28,28" \
    -fill "#0B1E4A" -draw "rectangle 24,24 616,196" \
    -fill "#6FE7FF" -draw "roundrectangle 56,56 584,164 20,20" \
    -fill "#0B1E4A" -draw "rectangle 80,80 560,140" \
    -fill "#FFD166" -draw "rectangle 108,90 140,130" \
    -fill "#FFD166" -draw "rectangle 156,90 188,130" \
    -fill "#FFD166" -draw "rectangle 204,90 236,130" \
    -fill "#FFD166" -draw "rectangle 252,90 284,130" \
    -fill "#FFD166" -draw "rectangle 300,90 332,130" \
    -fill "#FFD166" -draw "rectangle 348,90 380,130" \
    -fill "#FFD166" -draw "rectangle 396,90 428,130" \
    -fill "#FFD166" -draw "rectangle 444,90 476,130" \
    -fill "#FFD166" -draw "rectangle 492,90 524,130" \
    "${DATA_DIR}/webui/assets/header.png"

  magick -size 96x96 "xc:#172A56" \
    -fill "#6FE7FF" -draw "roundrectangle 4,4 92,92 14,14" \
    -fill "#172A56" -draw "rectangle 14,14 82,82" \
    -fill "#FFD166" -draw "rectangle 24,24 72,36" \
    -fill "#FFD166" -draw "rectangle 24,44 72,56" \
    -fill "#FFD166" -draw "rectangle 24,64 72,76" \
    "${DATA_DIR}/webui/assets/favicon.png"
else
  ffmpeg -loglevel error -y -f lavfi -i "color=c=#0B1E4A:s=640x220:d=0.1" \
    -vf "drawbox=x=8:y=8:w=624:h=204:color=#FFD166:t=fill,drawbox=x=24:y=24:w=592:h=172:color=#0B1E4A:t=fill,drawbox=x=56:y=56:w=528:h=108:color=#6FE7FF:t=fill,drawbox=x=80:y=80:w=480:h=60:color=#0B1E4A:t=fill,drawbox=x=108:y=90:w=32:h=40:color=#FFD166:t=fill,drawbox=x=156:y=90:w=32:h=40:color=#FFD166:t=fill,drawbox=x=204:y=90:w=32:h=40:color=#FFD166:t=fill,drawbox=x=252:y=90:w=32:h=40:color=#FFD166:t=fill,drawbox=x=300:y=90:w=32:h=40:color=#FFD166:t=fill,drawbox=x=348:y=90:w=32:h=40:color=#FFD166:t=fill,drawbox=x=396:y=90:w=32:h=40:color=#FFD166:t=fill,drawbox=x=444:y=90:w=32:h=40:color=#FFD166:t=fill,drawbox=x=492:y=90:w=32:h=40:color=#FFD166:t=fill" \
    -frames:v 1 "${DATA_DIR}/webui/assets/header.png"

  ffmpeg -loglevel error -y -f lavfi -i "color=c=#172A56:s=96x96:d=0.1" \
    -vf "drawbox=x=4:y=4:w=88:h=88:color=#6FE7FF:t=fill,drawbox=x=14:y=14:w=68:h=68:color=#172A56:t=fill,drawbox=x=24:y=24:w=48:h=12:color=#FFD166:t=fill,drawbox=x=24:y=44:w=48:h=12:color=#FFD166:t=fill,drawbox=x=24:y=64:w=48:h=12:color=#FFD166:t=fill" \
    -frames:v 1 "${DATA_DIR}/webui/assets/favicon.png"
fi

sox -n -r 16000 -c 1 "${DATA_DIR}/webui/assets/sfx_click.wav" synth 0.07 sine 1260 vol 0.30 fade q 0.001 0.07 0.01
sox -n -r 16000 -c 1 "${DATA_DIR}/webui/assets/sfx_ok.wav" synth 0.11 sine 960 vol 0.32 fade q 0.001 0.11 0.02
sox -n -r 16000 -c 1 "${DATA_DIR}/webui/assets/sfx_error.wav" synth 0.12 sine 240 vol 0.32 fade q 0.001 0.12 0.02

idx=0
for app in "${APPS[@]}"; do
  app_dir="${DATA_DIR}/apps/${app}"
  audio_dir="${app_dir}/audio"
  content_dir="${app_dir}/content"
  cache_dir="${app_dir}/cache"
  mkdir -p "${app_dir}" "${audio_dir}" "${content_dir}" "${cache_dir}"

  bg="${palette_bg[$((idx % ${#palette_bg[@]}))]}"
  ac="${palette_ac[$((idx % ${#palette_ac[@]}))]}"
  radius=$((18 + (idx % 12)))

  if [[ "${HAS_MAGICK}" == "1" ]]; then
    magick -size 96x96 "xc:${bg}" \
      -fill "${ac}" -draw "roundrectangle 4,4 92,92 14,14" \
      -fill "${bg}" -draw "rectangle 14,14 82,82" \
      -fill "${ac}" -draw "circle 48,48 48,$((48 - radius))" \
      -fill "${bg}" -draw "circle 48,48 48,$((48 - radius + 8))" \
      -fill "${ac}" -draw "rectangle 30,44 66,52" \
      "${app_dir}/icon.png"
  else
    ffmpeg -loglevel error -y -f lavfi -i "color=c=${bg}:s=96x96:d=0.1" \
      -vf "drawbox=x=4:y=4:w=88:h=88:color=${ac}:t=fill,drawbox=x=14:y=14:w=68:h=68:color=${bg}:t=fill,drawbox=x=30:y=44:w=36:h=8:color=${ac}:t=fill" \
      -frames:v 1 "${app_dir}/icon.png"
  fi

  # Per-app short SFX
  sox -n -r 16000 -c 1 "${audio_dir}/open.wav" synth 0.09 sine $((520 + (idx * 17 % 220))) vol 0.3 fade q 0.001 0.09 0.02
  sox -n -r 16000 -c 1 "${audio_dir}/action.wav" synth 0.08 sine $((880 + (idx * 11 % 260))) vol 0.3 fade q 0.001 0.08 0.02

  # Offline/session fallback for streaming kids apps and audiobook.
  if [[ "${app}" == kids_* || "${app}" == "audiobook_player" || "${app}" == "audio_player" ]]; then
    ffmpeg -loglevel error -y -f lavfi \
      -i "sine=frequency=$((380 + idx * 13)):duration=1.6" \
      -ar 22050 -ac 1 -b:a 64k "${audio_dir}/offline.mp3"
    ffmpeg -loglevel error -y -f lavfi \
      -i "sine=frequency=$((300 + idx * 9)):duration=1.2" \
      -ar 22050 -ac 1 -b:a 64k "${audio_dir}/session.mp3"
    cp -f "${audio_dir}/offline.mp3" "${audio_dir}/default.mp3"
  fi

  idx=$((idx + 1))
done

echo "UI assets generated in ${DATA_DIR}/apps"
