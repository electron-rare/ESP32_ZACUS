#!/usr/bin/env python3
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import serial


BOOT_BANNER = "[MAIN] Freenove all-in-one boot"


def host_line(text: str) -> str:
    stamp = time.strftime("%Y-%m-%d %H:%M:%S")
    return f"[HOST] {stamp} {text}"


def open_serial(port: str, baud: int, timeout: float) -> serial.Serial:
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = timeout
    ser.write_timeout = 1.0
    ser.dtr = False
    ser.rts = False
    ser.open()
    time.sleep(0.25)
    return ser


def flush_lines(ser: serial.Serial, log_handle, deadline: float) -> list[str]:
    lines: list[str] = []
    pending = ""
    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if not chunk:
            continue
        pending += chunk.decode("utf-8", errors="ignore")
        while "\n" in pending:
            raw_line, pending = pending.split("\n", 1)
            line = raw_line.rstrip("\r")
            log_handle.write(line + "\n")
            log_handle.flush()
            lines.append(line)
    if pending:
        line = pending.rstrip("\r")
        log_handle.write(line + "\n")
        log_handle.flush()
        lines.append(line)
    return lines


def send_command(ser: serial.Serial, log_handle, command: str, wait_s: float) -> list[str]:
    log_handle.write(host_line(f"SENT {command}") + "\n")
    log_handle.flush()
    ser.write((command + "\n").encode("utf-8"))
    ser.flush()
    return flush_lines(ser, log_handle, time.time() + wait_s)


def wait_for_command_channel(ser: serial.Serial, log_handle, timeout_s: float) -> bool:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        flush_lines(ser, log_handle, time.time() + 0.35)
        response = send_command(ser, log_handle, "PING", wait_s=1.2)
        if any("PONG" in line or "UNKNOWN PING" in line for line in response):
            log_handle.write(host_line("COMMAND CHANNEL READY") + "\n")
            log_handle.flush()
            return True
        time.sleep(0.4)
    log_handle.write(host_line("COMMAND CHANNEL NOT READY") + "\n")
    log_handle.flush()
    return False


def wait_for_quiet_period(
    ser: serial.Serial,
    log_handle,
    timeout_s: float,
    quiet_s: float,
) -> tuple[bool, float]:
    deadline = time.time() + timeout_s
    last_rx_at = time.time()
    while time.time() < deadline:
        lines = flush_lines(ser, log_handle, time.time() + 0.25)
        if lines:
            last_rx_at = time.time()
        elif (time.time() - last_rx_at) >= quiet_s:
            return True, last_rx_at
    return False, last_rx_at


def main() -> int:
    parser = argparse.ArgumentParser(description="Capture a rebooted ESP32 boot log over serial.")
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=0.2)
    parser.add_argument("--seconds", type=float, default=90.0)
    parser.add_argument("--log", required=True)
    args = parser.parse_args()

    log_path = Path(args.log)
    log_path.parent.mkdir(parents=True, exist_ok=True)

    try:
        with log_path.open("w", encoding="utf-8") as log_handle:
            log_handle.write(host_line(f"OPEN port={args.port} baud={args.baud}") + "\n")
            log_handle.flush()

            ser = open_serial(args.port, args.baud, args.timeout)
            try:
                ready = wait_for_command_channel(ser, log_handle, timeout_s=20.0)
                if ready:
                    ser.close()
                    time.sleep(1.2)
                    ser = open_serial(args.port, args.baud, args.timeout)
                    log_handle.write(host_line("REOPEN FOR BOOT CAPTURE") + "\n")
                    log_handle.flush()

                capture_deadline = time.time() + args.seconds
                boot_seen = False
                post_boot_commands_sent = False
                boot_seen_at = 0.0
                last_rx_at = time.time()
                phase_start = time.time()

                while time.time() < capture_deadline:
                    lines = flush_lines(ser, log_handle, time.time() + 0.25)
                    if lines:
                        last_rx_at = time.time()
                    for line in lines:
                        if BOOT_BANNER in line:
                            boot_seen = True
                            boot_seen_at = time.time()

                    should_probe = False
                    if (
                        boot_seen
                        and not post_boot_commands_sent
                        and (
                            (time.time() - last_rx_at) >= 1.8
                            or (time.time() - boot_seen_at) >= 18.0
                        )
                    ):
                        should_probe = True
                    if (
                        not boot_seen
                        and not post_boot_commands_sent
                        and (time.time() - phase_start) >= 18.0
                    ):
                        should_probe = True

                    if should_probe:
                        send_command(ser, log_handle, "PING", wait_s=2.0)
                        send_command(ser, log_handle, "STATUS", wait_s=2.4)
                        post_boot_commands_sent = True

                log_handle.write(host_line("CAPTURE COMPLETE") + "\n")
                log_handle.flush()
            finally:
                try:
                    ser.close()
                except Exception:
                    pass
    except serial.SerialException as exc:
        print(f"serial capture failed: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
