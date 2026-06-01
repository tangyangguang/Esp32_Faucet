#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise SystemExit(message)


platformio = (ROOT / "platformio.ini").read_text()
main_cpp = (ROOT / "src/main.cpp").read_text()
bringup = (ROOT / "docs/07-board-bringup.md").read_text()

require(
    "-D ESP32BASE_EB_FILELOG_DEFAULT_MODE=ESP32BASE_FILELOG_MODE_WARN" in platformio,
    "platformio.ini must set System Logs default mode to WARN",
)
require(
    "Esp32BaseFileLog::setMode" not in main_cpp,
    "src/main.cpp must not force a FileLog mode at boot; persisted user config must win",
)
require(
    "ESP32BASE_EB_FILELOG_DEFAULT_MODE=ESP32BASE_FILELOG_MODE_WARN" in bringup,
    "docs/07-board-bringup.md must document the WARN System Logs default",
)
require(
    "显式应用 `Esp32BaseFileLog::setMode" not in bringup,
    "docs/07-board-bringup.md must not document a forced startup FileLog mode",
)

print("FileLog default WARN checks passed")
