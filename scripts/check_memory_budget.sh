#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  check_memory_budget.sh [--env <name>] [--max-ram <pct>] [--max-flash <pct>] [--yes]

Options:
  --env <name>         PlatformIO env (default: freenove_esp32s3_full_with_ui)
  --max-ram <pct>      RAM usage threshold in percent (default: 75)
  --max-flash <pct>    Flash usage threshold in percent (default: 80)
  --yes                Non-interactive mode (skip optional TUI prompts)
  -h, --help

Examples:
  ./scripts/check_memory_budget.sh
  ./scripts/check_memory_budget.sh --max-ram 70 --max-flash 78
  ./scripts/check_memory_budget.sh --env freenove_esp32s3_full_with_ui --yes
EOF
}

env_name="freenove_esp32s3_full_with_ui"
max_ram="75"
max_flash="80"
non_interactive=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --env)
      env_name="${2:-}"
      shift 2
      ;;
    --max-ram)
      max_ram="${2:-}"
      shift 2
      ;;
    --max-flash)
      max_flash="${2:-}"
      shift 2
      ;;
    --yes|--non-interactive)
      non_interactive=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if [[ "$non_interactive" -eq 0 && -t 0 && -t 1 && "$(command -v gum >/dev/null 2>&1; echo $?)" -eq 0 ]]; then
  env_name="$(gum input --value "$env_name" --placeholder "PlatformIO env")"
  max_ram="$(gum input --value "$max_ram" --placeholder "RAM threshold %")"
  max_flash="$(gum input --value "$max_flash" --placeholder "Flash threshold %")"
fi

tmp_log="$(mktemp -t zacus-mem-budget.XXXXXX.log)"
cleanup() { rm -f "$tmp_log"; }
trap cleanup EXIT

echo "[RUN] python3 -m platformio run -e $env_name"
python3 -m platformio run -e "$env_name" | tee "$tmp_log"

ram_line="$(grep -E '^RAM:' "$tmp_log" | tail -n1 || true)"
flash_line="$(grep -E '^Flash:' "$tmp_log" | tail -n1 || true)"

if [[ -z "$ram_line" || -z "$flash_line" ]]; then
  echo "[ERR] cannot parse RAM/Flash lines from PlatformIO output" >&2
  exit 1
fi

ram_pct="$(echo "$ram_line" | sed -E 's/.* ([0-9]+([.][0-9]+)?)% .*/\1/')"
flash_pct="$(echo "$flash_line" | sed -E 's/.* ([0-9]+([.][0-9]+)?)% .*/\1/')"
ram_used="$(echo "$ram_line" | sed -E 's/.*[(]used ([0-9]+) bytes from ([0-9]+) bytes[)].*/\1\/\2/')"
flash_used="$(echo "$flash_line" | sed -E 's/.*[(]used ([0-9]+) bytes from ([0-9]+) bytes[)].*/\1\/\2/')"

echo "[INFO] env=$env_name"
echo "[INFO] RAM   : ${ram_pct}% ($ram_used), threshold=${max_ram}%"
echo "[INFO] Flash : ${flash_pct}% ($flash_used), threshold=${max_flash}%"

ram_over="$(awk "BEGIN {print ($ram_pct > $max_ram) ? 1 : 0}")"
flash_over="$(awk "BEGIN {print ($flash_pct > $max_flash) ? 1 : 0}")"

status=0
if [[ "$ram_over" -eq 1 ]]; then
  echo "[FAIL] RAM usage is above threshold"
  status=1
fi
if [[ "$flash_over" -eq 1 ]]; then
  echo "[FAIL] Flash usage is above threshold"
  status=1
fi

if [[ "$status" -eq 0 ]]; then
  echo "[PASS] memory budget is within thresholds"
fi

exit "$status"
