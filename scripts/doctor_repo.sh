#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"
STATUS=0

ok() {
  printf '[OK] %s\n' "$1"
}

warn() {
  printf '[WARN] %s\n' "$1"
}

err() {
  printf '[ERR] %s\n' "$1" >&2
  STATUS=1
}

cd "$ROOT_DIR"

for cmd in bash rg "$PYTHON_BIN"; do
  if command -v "$cmd" >/dev/null 2>&1; then
    ok "tool available: $cmd"
  else
    err "missing tool: $cmd"
  fi
done

if command -v pio >/dev/null 2>&1; then
  ok "pio available: $(pio --version | head -n 1)"
elif [ -x "$ROOT_DIR/.venv/bin/python" ] && "$ROOT_DIR/.venv/bin/python" -m platformio --version >/dev/null 2>&1; then
  ok "platformio available in .venv: $("$ROOT_DIR/.venv/bin/python" -m platformio --version | head -n 1)"
else
  warn "PlatformIO not found in PATH or .venv"
fi

platform_pin="$(rg -n '^platform = ' platformio.ini | sed 's/^[0-9]*://')"
ok "platform pin: ${platform_pin:-missing}"

echo "[INFO] supported envs"
rg '^\[env:' platformio.ini | sed 's/^\[env://; s/\]$//'

for path in \
  README.md \
  README_ESP32_ZACUS.md \
  ui_freenove_allinone/README.md \
  docs/QUICKSTART.md \
  docs/FNK0102H_SOURCE_OF_TRUTH.md \
  boards/freenove_esp32_s3_wroom.json \
  scripts/bootstrap_platformio.sh \
  scripts/doctor_repo.sh \
  scripts/flash_monitor_audit.sh \
  scripts/serial_boot_capture.py \
  scripts/audit_boot_log.py \
  tests/sprint1_utility_contract.py \
  tests/sprint2_capture_contract.py \
  tests/sprint3_audio_contract.py \
  tests/phase9_ui_validation.py; do
  if [ -e "$path" ]; then
    ok "path present: $path"
  else
    err "path missing: $path"
  fi
done

"$PYTHON_BIN" - <<'PY' || STATUS=1
from pathlib import Path
import re
import sys

root = Path.cwd()
files = [
    root / "README.md",
    root / "README_ESP32_ZACUS.md",
    root / "ui_freenove_allinone" / "README.md",
    root / "docs" / "QUICKSTART.md",
    root / "docs" / "FNK0102H_SOURCE_OF_TRUTH.md",
]
legacy_needles = ("hardware/firmware", "tools/dev/", "protocol/", "../hardware/")
status = 0

for path in files:
    text = path.read_text(encoding="utf-8")
    for match in re.finditer(r"\[[^\]]+\]\(([^)]+)\)", text):
        target = match.group(1).strip()
        if not target or target.startswith(("http://", "https://", "#", "mailto:")):
            continue
        target = target.split("#", 1)[0]
        if not (path.parent / target).resolve().exists():
            print(f"[ERR] broken markdown link in {path.relative_to(root)} -> {target}")
            status = 1
    for needle in legacy_needles:
        if needle in text:
            print(f"[ERR] stale legacy reference in {path.relative_to(root)} -> {needle}")
            status = 1

if status == 0:
    print("[OK] markdown links and legacy path scan clean")
sys.exit(status)
PY

if [ "$STATUS" -ne 0 ]; then
  err "doctor detected blocking issues"
else
  ok "doctor completed without blocking issues"
fi

exit "$STATUS"
