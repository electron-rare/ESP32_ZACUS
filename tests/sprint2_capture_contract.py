#!/usr/bin/env python3
"""
Sprint 2 capture contract + endurance checks (camera_video, qr_scanner, dictaphone).

Modes:
- Serial: APP_OPEN / APP_ACTION / APP_STATUS / APP_CLOSE
- HTTP:   /api/apps/open|action|status|close
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence, Tuple

import requests

try:
    import serial
except Exception:  # pragma: no cover - optional until serial mode is used
    serial = None


APPS: Sequence[str] = ("camera_video", "qr_scanner", "dictaphone")

# (action_name, payload)
APP_ACTIONS: Dict[str, Sequence[Tuple[str, str]]] = {
    "camera_video": (
        ("status", ""),
        ("snapshot", ""),
        ("list_media", ""),
        ("clip_start", ""),
        ("clip_stop", ""),
        ("status", ""),
    ),
    "qr_scanner": (
        ("scan_start", ""),
        ("status", ""),
        ("scan_payload", "https://example.com"),
        ("scan_payload", "app:timer_tools"),
        ("scan_payload", "hello-zacus"),
        ("scan_stop", ""),
        ("status", ""),
    ),
    "dictaphone": (
        ("record_start", "1"),
        ("record_stop", ""),
        ("list_records", ""),
        ("status", ""),
    ),
}


@dataclass
class Status:
    app_id: str
    state: str
    mode: str
    source: str
    last_error: str
    last_event: str


APP_STATUS_RE = re.compile(
    r"APP_STATUS id=(?P<id>\S*) state=(?P<state>\S+) mode=(?P<mode>\S+) "
    r"source=(?P<source>\S+) err=(?P<err>.*?) event=(?P<event>.*?) tick=(?P<tick>\d+) "
    r"missing=(?P<missing>0x[0-9A-Fa-f]+)"
)


def parse_serial_status(raw: str) -> Optional[Status]:
    for line in reversed(raw.splitlines()):
        if "APP_STATUS " not in line:
            continue
        match = APP_STATUS_RE.search(line.strip())
        if match is None:
            continue
        return Status(
            app_id=match.group("id"),
            state=match.group("state"),
            mode=match.group("mode"),
            source=match.group("source"),
            last_error=match.group("err"),
            last_event=match.group("event"),
        )
    return None


class SerialRunner:
    def __init__(self, port: str, baud: int, timeout: float) -> None:
        if serial is None:
            raise RuntimeError("pyserial is required for serial mode")
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self.ser: Optional[serial.Serial] = None

    def __enter__(self) -> "SerialRunner":
        self.ser = serial.Serial(self.port, self.baud, timeout=self.timeout)
        self.ser.dtr = False
        self.ser.rts = False
        time.sleep(0.8)
        self.ser.reset_input_buffer()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if self.ser is not None:
            self.ser.close()
            self.ser = None

    def send(self, command: str, wait_s: float = 1.2) -> str:
        if self.ser is None:
            raise RuntimeError("serial connection closed")
        self.ser.reset_input_buffer()
        self.ser.write((command + "\r\n").encode("utf-8"))
        self.ser.flush()
        deadline = time.time() + wait_s
        chunks: List[str] = []
        while time.time() < deadline:
            if self.ser.in_waiting > 0:
                data = self.ser.read(self.ser.in_waiting).decode("utf-8", errors="ignore")
                chunks.append(data)
            time.sleep(0.03)
        return "".join(chunks)

    def wait_for_boot_settle(self, timeout_s: float = 60.0, quiet_s: float = 3.5) -> None:
        if self.ser is None:
            raise RuntimeError("serial connection closed")
        deadline = time.time() + timeout_s
        last_rx = time.time()
        while time.time() < deadline:
            if self.ser.in_waiting > 0:
                _ = self.ser.read(self.ser.in_waiting)
                last_rx = time.time()
            elif (time.time() - last_rx) >= quiet_s:
                return
            time.sleep(0.04)
        # Do not hard-fail on noisy boot; handshake may still succeed.

    def wait_for_command_channel(self, retries: int = 8) -> None:
        self.wait_for_boot_settle()
        last_raw = ""
        for _ in range(retries):
            raw = self.send("PING", wait_s=1.6)
            if "PONG" in raw:
                return
            if "UNKNOWN PING" in raw:
                # Command path is active even if PING isn't mapped as expected.
                return
            last_raw = raw
            time.sleep(0.25)
        raise RuntimeError(
            "serial command channel unavailable after PING retries "
            f"(port={self.port}). Last output:\n{last_raw}"
        )

    def status(self) -> Status:
        last_raw = ""
        for _ in range(4):
            raw = self.send("APP_STATUS", wait_s=1.4)
            parsed = parse_serial_status(raw)
            if parsed is not None:
                return parsed
            last_raw = raw
            time.sleep(0.25)
        raise RuntimeError(f"APP_STATUS parse failed. Raw output:\n{last_raw}")


class ApiRunner:
    def __init__(self, base_url: str, token: str) -> None:
        self.base_url = base_url.rstrip("/")
        self.session = requests.Session()
        if token:
            self.session.headers["Authorization"] = f"Bearer {token}"
        self.session.headers["Content-Type"] = "application/json"

    def post(self, path: str, payload: dict) -> dict:
        resp = self.session.post(f"{self.base_url}{path}", data=json.dumps(payload), timeout=8)
        resp.raise_for_status()
        return resp.json()

    def get(self, path: str) -> dict:
        resp = self.session.get(f"{self.base_url}{path}", timeout=8)
        resp.raise_for_status()
        return resp.json()

    def status(self) -> Status:
        body = self.get("/api/apps/status")
        runtime = body.get("runtime", {})
        return Status(
            app_id=str(runtime.get("running_id", "")),
            state=str(runtime.get("state", "")),
            mode=str(runtime.get("mode", "")),
            source=str(runtime.get("source", "")),
            last_error=str(runtime.get("last_error", "")),
            last_event=str(runtime.get("last_event", "")),
        )

    def wait_until_ready(self, timeout_s: float = 45.0) -> None:
        deadline = time.time() + timeout_s
        last_error = ""
        while time.time() < deadline:
            try:
                self.get("/api/apps/status")
                return
            except Exception as exc:  # pragma: no cover - network/device dependent
                last_error = str(exc)
                time.sleep(1.0)
        raise RuntimeError(
            f"API unreachable at {self.base_url} after {timeout_s:.0f}s (last_error={last_error})"
        )


def assert_status_running(status: Status, app_id: str) -> None:
    if status.state not in ("running", "starting"):
        raise RuntimeError(f"{app_id}: unexpected state after open: {status.state}")
    if status.app_id and status.app_id != app_id:
        raise RuntimeError(f"{app_id}: running_id mismatch: got={status.app_id}")


def assert_status_idle(status: Status) -> None:
    if status.state != "idle":
        raise RuntimeError(f"expected idle after close, got={status.state}")


def run_serial_cycles(port: str, baud: int, timeout: float, cycles: int) -> None:
    with SerialRunner(port=port, baud=baud, timeout=timeout) as runner:
        runner.wait_for_command_channel()
        for app_id in APPS:
            for cycle in range(1, cycles + 1):
                open_out = runner.send(f"APP_OPEN {app_id} sprint2", wait_s=0.9)
                if "ACK APP_OPEN ok=1" not in open_out:
                    fallback = runner.status()
                    if fallback.state not in ("running", "starting") or (
                        fallback.app_id and fallback.app_id != app_id
                    ):
                        raise RuntimeError(f"{app_id} cycle#{cycle}: APP_OPEN failed\n{open_out}")

                st = runner.status()
                assert_status_running(st, app_id)

                for action, payload in APP_ACTIONS[app_id]:
                    cmd = f"APP_ACTION {action}" + (f" {payload}" if payload else "")
                    action_out = runner.send(cmd, wait_s=0.9)
                    if "ACK APP_ACTION ok=1" not in action_out:
                        st_mid = runner.status()
                        if st_mid.state not in ("running", "starting"):
                            raise RuntimeError(
                                f"{app_id} cycle#{cycle}: APP_ACTION failed ({cmd})\n{action_out}"
                            )
                    if action == "record_start":
                        time.sleep(1.2)
                    elif action == "clip_start":
                        time.sleep(1.1)
                    else:
                        time.sleep(0.05)

                st_after = runner.status()
                if st_after.last_error not in ("", "none"):
                    raise RuntimeError(
                        f"{app_id} cycle#{cycle}: runtime error={st_after.last_error} "
                        f"event={st_after.last_event}"
                    )

                close_out = runner.send("APP_CLOSE sprint2_cycle", wait_s=0.9)
                if "ACK APP_CLOSE ok=1" not in close_out:
                    st_close = runner.status()
                    if st_close.state != "idle":
                        raise RuntimeError(f"{app_id} cycle#{cycle}: APP_CLOSE failed\n{close_out}")
                st_idle = runner.status()
                assert_status_idle(st_idle)

                print(f"[SERIAL] {app_id} cycle {cycle}/{cycles} OK")


def run_api_cycles(base_url: str, token: str, cycles: int) -> None:
    runner = ApiRunner(base_url=base_url, token=token)
    runner.wait_until_ready()
    for app_id in APPS:
        for cycle in range(1, cycles + 1):
            opened = runner.post(
                "/api/apps/open",
                {"id": app_id, "mode": "sprint2", "source": "sprint2_test"},
            )
            if not opened.get("ok", False):
                raise RuntimeError(f"{app_id} cycle#{cycle}: /open failed {opened}")

            st = runner.status()
            assert_status_running(st, app_id)

            for action, payload in APP_ACTIONS[app_id]:
                payload_json = {
                    "id": app_id,
                    "action": action,
                    "payload": payload,
                    "content_type": "text/plain",
                }
                acted = runner.post("/api/apps/action", payload_json)
                if not acted.get("ok", False):
                    raise RuntimeError(f"{app_id} cycle#{cycle}: /action failed {acted}")
                if action == "record_start":
                    time.sleep(1.2)
                elif action == "clip_start":
                    time.sleep(1.1)
                else:
                    time.sleep(0.05)

            st_after = runner.status()
            if st_after.last_error not in ("", "none"):
                raise RuntimeError(
                    f"{app_id} cycle#{cycle}: runtime error={st_after.last_error} "
                    f"event={st_after.last_event}"
                )

            closed = runner.post("/api/apps/close", {"id": app_id, "reason": "sprint2_cycle"})
            if not closed.get("ok", False):
                raise RuntimeError(f"{app_id} cycle#{cycle}: /close failed {closed}")
            st_idle = runner.status()
            assert_status_idle(st_idle)

            print(f"[HTTP] {app_id} cycle {cycle}/{cycles} OK")


def main() -> int:
    parser = argparse.ArgumentParser(description="Sprint 2 capture contract + endurance checks.")
    parser.add_argument("--mode", choices=("serial", "http"), default="serial")
    parser.add_argument("--cycles", type=int, default=10)
    parser.add_argument("--port", default="/dev/cu.usbmodem5AB90753301")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--base-url", default="http://192.168.4.1")
    parser.add_argument("--token", default="")
    args = parser.parse_args()

    started = time.time()
    try:
        if args.mode == "serial":
            run_serial_cycles(args.port, args.baud, args.timeout, args.cycles)
        else:
            run_api_cycles(args.base_url, args.token, args.cycles)
    except Exception as exc:
        print(f"FAILED: {exc}")
        return 1

    elapsed = time.time() - started
    print(f"SUCCESS: sprint2 capture cycles completed ({args.cycles} cycles/app) in {elapsed:.1f}s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
