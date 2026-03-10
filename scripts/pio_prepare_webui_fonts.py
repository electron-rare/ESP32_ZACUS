#!/usr/bin/env python3
from __future__ import annotations

import os
from pathlib import Path
from urllib.request import urlopen

Import("env")  # type: ignore # noqa: F821


FONT_SOURCES = {
    "PressStart2P-Regular.ttf": "https://raw.githubusercontent.com/google/fonts/main/ofl/pressstart2p/PressStart2P-Regular.ttf",
    "ComicNeue-Regular.ttf": "https://raw.githubusercontent.com/google/fonts/main/ofl/comicneue/ComicNeue-Regular.ttf",
    "ComicNeue-Bold.ttf": "https://raw.githubusercontent.com/google/fonts/main/ofl/comicneue/ComicNeue-Bold.ttf",
}


def _download(url: str, target: Path) -> None:
    with urlopen(url, timeout=20) as response:  # nosec B310
        payload = response.read()
    if not payload:
        raise RuntimeError(f"empty payload for {url}")
    target.write_bytes(payload)


def _prepare_webui_fonts() -> None:
    project_dir = Path(env["PROJECT_DIR"])  # type: ignore # noqa: F821
    fonts_dir = project_dir / "data" / "webui" / "assets" / "fonts"
    fonts_dir.mkdir(parents=True, exist_ok=True)

    for file_name, source_url in FONT_SOURCES.items():
        target = fonts_dir / file_name
        if target.exists() and target.stat().st_size > 0:
            print(f"[webui-fonts] ok {target}")
            continue
        print(f"[webui-fonts] fetch {source_url}")
        _download(source_url, target)
        print(f"[webui-fonts] saved {target}")


_prepare_webui_fonts()
