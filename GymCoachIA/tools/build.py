#!/usr/bin/env python3
"""Compile, or explicitly upload, one board using project-local dependencies."""
import argparse
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BOARDS = {"wio": ("Seeeduino:samd:seeed_wio_terminal", "WioCoach"),
          "xiao": ("esp32:esp32:XIAO_ESP32C3", "XiaoBridge")}

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("board", choices=BOARDS)
    p.add_argument("--upload", metavar="PORT", help="Flash this port after a successful compile")
    p.add_argument("--self-test", action="store_true", help="Wio output test; no camera inference")
    p.add_argument("--no-servo", action="store_true", help="Build Wio without Servo library")
    p.add_argument("--config", type=Path, default=ROOT / ".arduino" / "config.yaml")
    a = p.parse_args()
    if not a.config.is_file() or not shutil.which("arduino-cli"):
        p.error("Run tools/install_arduino.py first and ensure arduino-cli is on PATH")
    if a.board == "xiao" and (a.self_test or a.no_servo):
        p.error("--self-test and --no-servo apply to the Wio only")
    fqbn, folder = BOARDS[a.board]
    sketch = ROOT / "firmware" / folder
    build = ROOT / "build" / (a.board + ("-selftest" if a.self_test else ""))
    common = ["arduino-cli", "--config-file", str(a.config)]
    command = common + ["compile", "--fqbn", fqbn, "--build-path", str(build)]
    flags = []
    if a.self_test: flags.append("-DCOACH_SELF_TEST=1")
    if a.no_servo: flags.append("-DCOACH_USE_SERVO=0")
    if flags: command += ["--build-property", "compiler.cpp.extra_flags=" + " ".join(flags)]
    subprocess.run(command + [str(sketch)], check=True)
    if a.upload:
        subprocess.run(common + ["upload", "--fqbn", fqbn, "--port", a.upload,
                                 "--input-dir", str(build), str(sketch)], check=True)
