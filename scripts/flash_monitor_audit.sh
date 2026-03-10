#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_NAME="${ZACUS_ENV:-freenove_esp32s3_full_with_ui}"
MONITOR_SECONDS="${ZACUS_MONITOR_SECONDS:-90}"
DEFAULT_PORT="/dev/cu.usbmodem5AB90753301"

if [ -x "$ROOT_DIR/.venv/bin/python" ]; then
  PYTHON_BIN="$ROOT_DIR/.venv/bin/python"
else
  PYTHON_BIN="${PYTHON_BIN:-python3}"
fi

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
BUILD_ARTIFACT_DIR="$ROOT_DIR/artifacts/build"
MONITOR_ARTIFACT_DIR="$ROOT_DIR/artifacts/monitor"
AUDIT_ARTIFACT_DIR="$ROOT_DIR/artifacts/audit"
TEST_ARTIFACT_DIR="$ROOT_DIR/artifacts/tests"
SUMMARY_FILE="$AUDIT_ARTIFACT_DIR/${TIMESTAMP}_summary.txt"
MONITOR_LOG="$MONITOR_ARTIFACT_DIR/${TIMESTAMP}_boot.log"
ESPTOOL_LOG="$AUDIT_ARTIFACT_DIR/${TIMESTAMP}_esptool.log"

mkdir -p "$BUILD_ARTIFACT_DIR" "$MONITOR_ARTIFACT_DIR" "$AUDIT_ARTIFACT_DIR" "$TEST_ARTIFACT_DIR"

log() {
  printf '[CHAIN] %s\n' "$1"
}

detect_port() {
  if [ -n "${ZACUS_SERIAL_PORT:-}" ] && [ -e "${ZACUS_SERIAL_PORT}" ]; then
    printf '%s\n' "${ZACUS_SERIAL_PORT}"
    return 0
  fi
  if [ -e "$DEFAULT_PORT" ]; then
    printf '%s\n' "$DEFAULT_PORT"
    return 0
  fi
  mapfile -t ports < <(find /dev -maxdepth 1 -name 'cu.usbmodem*' | sort)
  if [ "${#ports[@]}" -eq 1 ]; then
    printf '%s\n' "${ports[0]}"
    return 0
  fi
  return 1
}

run_stage() {
  local stage="$1"
  shift
  local logfile="$BUILD_ARTIFACT_DIR/${TIMESTAMP}_${stage}.log"
  log "stage=$stage"
  if "$@" 2>&1 | tee "$logfile"; then
    return 0
  fi
  return 1
}

write_blocked_summary() {
  local build_ok="$1"
  local upload_ok="$2"
  "$PYTHON_BIN" "$ROOT_DIR/scripts/audit_boot_log.py" \
    --summary "$SUMMARY_FILE" \
    --build-ok "$build_ok" \
    --upload-ok "$upload_ok" >/dev/null
}

run_upload_target() {
  local target="$1"
  local port="$2"
  run_stage "$target" "$PYTHON_BIN" -m platformio run -e "$ENV_NAME" -t "$target" --upload-port "$port"
}

run_upload_with_retry() {
  local result_var="$1"
  local target="$2"
  local port="$3"
  if run_upload_target "$target" "$port"; then
    printf -v "$result_var" '%s' "$port"
    return 0
  fi
  log "retry stage=$target port=$port"
  if [ -e "$port" ] && run_upload_target "$target" "$port"; then
    printf -v "$result_var" '%s' "$port"
    return 0
  fi
  local fallback_port
  fallback_port="$(detect_port)" || return 1
  if [ "$fallback_port" != "$port" ] && run_upload_target "$target" "$fallback_port"; then
    printf -v "$result_var" '%s' "$fallback_port"
    return 0
  fi
  return 1
}

run_test() {
  local name="$1"
  shift
  local logfile="$TEST_ARTIFACT_DIR/${TIMESTAMP}_${name}.log"
  log "test=$name"
  "$@" 2>&1 | tee "$logfile"
}

main() {
  cd "$ROOT_DIR"
  local port
  port="$(detect_port)" || {
    printf 'state=hardware_unreachable\nreason=no_serial_port\n' >"$SUMMARY_FILE"
    log "no serial port detected"
    return 40
  }

  export ZACUS_SERIAL_PORT="$port"

  if ! run_stage build "$PYTHON_BIN" -m platformio run -e "$ENV_NAME"; then
    write_blocked_summary 0 0
    return 20
  fi
  if ! run_stage buildfs "$PYTHON_BIN" -m platformio run -e "$ENV_NAME" -t buildfs; then
    write_blocked_summary 0 0
    return 20
  fi

  if ! run_upload_with_retry port uploadfs "$port"; then
    write_blocked_summary 1 0
    return 20
  fi
  export ZACUS_SERIAL_PORT="$port"

  if ! run_upload_with_retry port upload "$port"; then
    write_blocked_summary 1 0
    return 20
  fi
  export ZACUS_SERIAL_PORT="$port"

  if ! "$PYTHON_BIN" "$ROOT_DIR/scripts/serial_boot_capture.py" \
      --port "$port" \
      --baud 115200 \
      --seconds "$MONITOR_SECONDS" \
      --log "$MONITOR_LOG"; then
    printf 'state=hardware_unreachable\nreason=serial_capture_failed\nport=%s\nlog=%s\n' "$port" "$MONITOR_LOG" >"$SUMMARY_FILE"
    return 40
  fi

  local audit_state
  set +e
  audit_state="$("$PYTHON_BIN" "$ROOT_DIR/scripts/audit_boot_log.py" \
    --log "$MONITOR_LOG" \
    --summary "$SUMMARY_FILE" \
    --build-ok 1 \
    --upload-ok 1)"
  local audit_rc=$?
  set -e

  if [ "$audit_state" = "safe_diagnostic_ok" ]; then
    {
      printf '[ESPTOOL] flash_id\n'
      "$PYTHON_BIN" -m esptool --chip esp32s3 --port "$port" flash_id
      printf '\n[ESPTOOL] read_mac\n'
      "$PYTHON_BIN" -m esptool --chip esp32s3 --port "$port" read_mac
    } 2>&1 | tee "$ESPTOOL_LOG"
    {
      printf 'finding=containment_logic_ok_root_cause_probable_hardware_or_memory_config\n'
      printf 'expected_board=FNK0102H/ESP32-S3-WROOM-1-N16R8\n'
      printf 'esptool_log=%s\n' "$ESPTOOL_LOG"
    } >>"$SUMMARY_FILE"
    return "$audit_rc"
  fi

  if [ "$audit_state" = "normal_boot_ok" ]; then
    run_test sprint1 "$PYTHON_BIN" "$ROOT_DIR/tests/sprint1_utility_contract.py" --mode serial --cycles 1 --port "$port"
    run_test sprint2 "$PYTHON_BIN" "$ROOT_DIR/tests/sprint2_capture_contract.py" --mode serial --cycles 1 --port "$port"
    run_test sprint3 "$PYTHON_BIN" "$ROOT_DIR/tests/sprint3_audio_contract.py" --mode serial --cycles 1 --port "$port"
    run_test phase9 "$PYTHON_BIN" "$ROOT_DIR/tests/phase9_ui_validation.py" --port "$port"
    printf 'tests=serial_smoke_pass\n' >>"$SUMMARY_FILE"
    return 0
  fi

  return "$audit_rc"
}

main "$@"
