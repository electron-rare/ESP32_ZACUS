#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


EXIT_CODES = {
    "normal_boot_ok": 0,
    "safe_diagnostic_ok": 10,
    "build_blocked": 20,
    "runtime_regression": 30,
}


def load_text(path: Path | None) -> str:
    if path is None or not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="ignore")


def relevant_segment(text: str) -> str:
    for marker in ("[HOST] ", "[MAIN] Freenove all-in-one boot", "[BOOT] safe diagnostic mode enabled"):
        idx = text.rfind(marker if marker != "[HOST] " else "SENT RESET")
        if idx != -1:
            return text[idx:]
    return text


def has_any(text: str, needles: list[str]) -> bool:
    return any(needle in text for needle in needles)


def classify(text: str, build_ok: bool, upload_ok: bool) -> tuple[str, list[str]]:
    evidence: list[str] = []
    if not build_ok or not upload_ok:
        if not build_ok:
            evidence.append("build_ok=0")
        if not upload_ok:
            evidence.append("upload_ok=0")
        return "build_blocked", evidence

    segment = relevant_segment(text)
    psram_match = re.findall(r"psram_found=(\d)", segment)
    if psram_match:
        evidence.append(f"psram_found={psram_match[-1]}")

    safe_markers = [
        "[BOOT] safe diagnostic mode enabled: PSRAM required, app stack disabled",
        "[SAFE] boot path: storage + serial + display + buttons only",
    ]
    safe_forbidden = [
        "[NET]",
        "[WEB]",
        "[CAM] boot start",
        "wifi:alloc pp wdev funcs fail",
        "tag=fx_",
        "tag=fx_sprite",
    ]
    fatal_markers = [
        "Guru Meditation",
        "Backtrace:",
        "abort()",
        "panic",
        "wifi:alloc pp wdev funcs fail",
        "[MEM] alloc_fail",
    ]

    if has_any(segment, safe_markers):
        evidence.extend(marker for marker in safe_markers if marker in segment)
        forbidden = [marker for marker in safe_forbidden if marker in segment]
        if not forbidden and "STATUS mode=safe_diagnostic" in segment:
            evidence.append("STATUS mode=safe_diagnostic")
            return "safe_diagnostic_ok", evidence
        evidence.extend(f"forbidden={marker}" for marker in forbidden)
        return "runtime_regression", evidence

    normal_markers = [
        "psram_found=1",
        "LVGL + display ready",
        "PONG",
    ]
    if has_any(segment, normal_markers):
        evidence.extend(marker for marker in normal_markers if marker in segment)
        fatals = [marker for marker in fatal_markers if marker in segment]
        if "psram_found=1" in segment and "LVGL + display ready" in segment and not fatals:
            return "normal_boot_ok", evidence
        evidence.extend(f"fatal={marker}" for marker in fatals)
        return "runtime_regression", evidence

    if segment.strip():
        evidence.append("boot segment captured but acceptance markers missing")
    else:
        evidence.append("boot log empty")
    return "runtime_regression", evidence


def write_summary(path: Path, state: str, evidence: list[str], log_path: Path | None) -> None:
    lines = [
        f"state={state}",
    ]
    if log_path is not None:
        lines.append(f"log={log_path}")
    for item in evidence:
        lines.append(f"evidence={item}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Classify ESP32 boot logs.")
    parser.add_argument("--log")
    parser.add_argument("--summary", required=True)
    parser.add_argument("--build-ok", type=int, choices=(0, 1), default=1)
    parser.add_argument("--upload-ok", type=int, choices=(0, 1), default=1)
    args = parser.parse_args()

    log_path = Path(args.log) if args.log else None
    summary_path = Path(args.summary)
    text = load_text(log_path)
    state, evidence = classify(text, build_ok=bool(args.build_ok), upload_ok=bool(args.upload_ok))
    write_summary(summary_path, state, evidence, log_path)
    print(state)
    return EXIT_CODES[state]


if __name__ == "__main__":
    raise SystemExit(main())
