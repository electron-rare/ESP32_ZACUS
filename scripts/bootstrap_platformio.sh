#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"
VENV_DIR="${ZACUS_VENV_DIR:-$ROOT_DIR/.venv}"

if ! command -v "$PYTHON_BIN" >/dev/null 2>&1; then
  echo "[BOOTSTRAP] python3 not found" >&2
  exit 1
fi

if [ ! -d "$VENV_DIR" ]; then
  echo "[BOOTSTRAP] creating virtualenv at $VENV_DIR"
  "$PYTHON_BIN" -m venv "$VENV_DIR"
fi

# shellcheck disable=SC1091
source "$VENV_DIR/bin/activate"

python -m pip install --upgrade pip
python -m pip install platformio esptool pyserial

echo "[BOOTSTRAP] tool versions"
python -m platformio --version
python -m esptool version
python - <<'PY'
import serial
print(f"pyserial {serial.VERSION}")
PY

cat <<EOF

[BOOTSTRAP] next steps
source "$VENV_DIR/bin/activate"
python -m platformio run -e freenove_esp32s3_full_with_ui
python -m platformio run -e freenove_esp32s3_full_with_ui -t buildfs
python -m platformio run -e freenove_esp32s3_full_with_ui -t uploadfs --upload-port <PORT>
python -m platformio run -e freenove_esp32s3_full_with_ui -t upload --upload-port <PORT>
python -m platformio device monitor -b 115200 --port <PORT>
EOF
