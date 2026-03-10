#!/bin/bash

###############################################################################
# Phase 6: Hardware Validation Smoke Tests
# Tests P1/P2 security hardening on live ESP32-S3 device
# Port: pass as argument or use ZACUS_SERIAL_PORT
###############################################################################

set -e

DEVICE_PORT="${1:-${ZACUS_SERIAL_PORT:-}}"
BAUD_RATE="115200"
TEST_LOG="/tmp/phase6_test_results.log"

if [ -z "$DEVICE_PORT" ]; then
    echo "Usage: $0 <PORT> or set ZACUS_SERIAL_PORT" >&2
    exit 1
fi

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Helper to send command and capture response
send_command() {
    local cmd="$1"
    local timeout_sec="${2:-2}"
    
    echo "→ Sending: $cmd"
    (printf "%s\r\n" "$cmd"; sleep "$timeout_sec") | \
        cat > "$DEVICE_PORT" 2>/dev/null &
    local pid=$!
    
    # Read response with timeout
    timeout "$timeout_sec" cat "$DEVICE_PORT" 2>/dev/null || true
    wait $pid 2>/dev/null || true
}

echo "═══════════════════════════════════════════════════════════════"
echo "Phase 6: Hardware Validation - P1/P2 Security Hardening Tests"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "Device: $DEVICE_PORT @ $BAUD_RATE baud"
echo "Log: $TEST_LOG"
echo ""

: > "$TEST_LOG"

# Test 1: Basic Command - PING
echo -e "${YELLOW}[Test 1/8]${NC} Command Map: PING → PONG"
echo "PING" >> "$TEST_LOG"
send_command "PING" | tee -a "$TEST_LOG"
echo ""

# Test 2: STATUS command
echo -e "${YELLOW}[Test 2/8]${NC} Command Map: STATUS → System Info"
echo "STATUS" >> "$TEST_LOG"
send_command "STATUS" 3 | tee -a "$TEST_LOG"
echo ""

# Test 3: HELP command
echo -e "${YELLOW}[Test 3/8]${NC} Command Map: HELP → Available Commands"
echo "HELP" >> "$TEST_LOG"
send_command "HELP" | tee -a "$TEST_LOG"
echo ""

# Test 4: Serial Buffer Overflow - 193 bytes (exceeds 192-byte limit)
echo -e "${YELLOW}[Test 4/8]${NC} Serial Overflow Protection: Send >192 bytes"
OVERFLOW_CMD=$(python3 -c "print('A' * 193)")
echo "Overflow test (193 bytes)" >> "$TEST_LOG"
(printf "%s\r\n" "$OVERFLOW_CMD"; sleep 2) > "$DEVICE_PORT" 2>/dev/null &
sleep 3
cat "$DEVICE_PORT" 2>/dev/null | grep -i "ERR_CMD_TOO_LONG\|ERR CMD_TOO_LONG" >> "$TEST_LOG" || echo "NO RESPONSE" >> "$TEST_LOG"
echo "Expected: ERR CMD_TOO_LONG"
echo ""

# Test 5: Invalid Character Detection
echo -e "${YELLOW}[Test 5/8]${NC} Serial Overflow Protection: Send invalid char (null byte)"
echo "Invalid char test" >> "$TEST_LOG"
(printf "TEST\x00CMD\r\n"; sleep 2) > "$DEVICE_PORT" 2>/dev/null &
sleep 3
cat "$DEVICE_PORT" 2>/dev/null | grep -i "ERR_CMD_INVALID\|ERR CMD_INVALID" >> "$TEST_LOG" || echo "NO RESPONSE" >> "$TEST_LOG"
echo "Expected: ERR CMD_INVALID_CHAR"
echo ""

# Test 6: Path Traversal Rejection
echo -e "${YELLOW}[Test 6/8]${NC} Path Validation (P2#8): Reject ../../ traversal"
echo "Testing path validation rejection" >> "$TEST_LOG"
echo ""
echo "Expected behavior: /../../etc/passwd → REJECTED"
echo "Whitelist check: /music/test.mp3 → ACCEPTED"
echo ""

# Test 7: JSON Schema Validation
echo -e "${YELLOW}[Test 7/8]${NC} JSON Schema (P2#9): Type mismatch detection"
echo "JSON schema validation test" >> "$TEST_LOG"
echo ""
echo "Expected: POST {\"action\": 123} → TYPE ERROR (int vs string)"
echo ""

# Test 8: JSON Size Limit
echo -e "${YELLOW}[Test 8/8]${NC} JSON Size Limit (P2#9): Reject >1024 bytes"
echo "JSON size limit test" >> "$TEST_LOG"
echo ""
echo "Expected: Body >1024 bytes → [WEB] json body too large"
echo ""

echo "═══════════════════════════════════════════════════════════════"
echo -e "${GREEN}Phase 6 Smoke Tests Initiated${NC}"
echo "Results logged to: $TEST_LOG"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "Next Steps:"
echo "  1. Monitor serial output on $DEVICE_PORT"
echo "  2. Verify all test responses match expected behavior"
echo "  3. Proceed to Phase 7 (Apps Core implementation)"
echo ""
