#!/usr/bin/env python3
"""Install pinned Arduino dependencies into this project's isolated .arduino/.

Requires arduino-cli on PATH and an internet connection. Does not change the
global Arduino IDE sketchbook or replace an existing global Servo installation.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STATE = ROOT / ".arduino"
CONFIG = STATE / "config.yaml"
SERVO_COMMIT = "962192408a0a2608351fc54e567bca28ccb2b74b"
SERVO_SHA256 = "dae194f6e70cd31342878c7675b3367808836d85fb7f63ba733b147d4e4c22d1"

def configure():
    STATE.mkdir(exist_ok=True)
    for folder in ("data", "downloads", "user"):
        (STATE / folder).mkdir(exist_ok=True)
    CONFIG.write_text(
        "board_manager:\n  additional_urls:\n"
        "    - https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json\n"
        "    - https://espressif.github.io/arduino-esp32/package_esp32_index.json\n"
        "directories:\n" + "".join(f"  {k}: {json.dumps(str(STATE / v))}\n" for k, v in
                                     (("data", "data"), ("downloads", "downloads"), ("user", "user"))))

def cli(*args):
    subprocess.run(["arduino-cli", "--config-file", str(CONFIG), *args], check=True)

def patch_servo(source: Path):
    timers = source / "src" / "samd" / "ServoTimers.h"
    text = timers.read_text()
    if "#if defined(__SAMD51__)" not in text or "TC0_Handler" not in text:
        raise RuntimeError("Unexpected Servo source; refusing an unverified patch")
    # SAMD51 TC0 and TC1 share GCLK ID 9. Move only the SAMD51 timer/IRQ.
    text = text.replace("TC0", "TC1")
    text += "\n// Gym Coach: reserve TC1; tone() in Seeed SAMD 1.8.6 owns TC0.\n#define GYM_COACH_SERVO_TC1 1\n"
    timers.write_text(text)
    (source / "GYM_COACH_PATCH.txt").write_text(f"Upstream adafruit/Servo {SERVO_COMMIT}\nSAMD51 timer TC0 -> TC1; GCLK remains 9.\n")

def install_servo():
    destination = STATE / "user" / "libraries" / "Servo"
    if destination.exists():
        marker = destination / "GYM_COACH_PATCH.txt"
        if marker.exists() and SERVO_COMMIT in marker.read_text():
            print("Pinned Servo patch already installed")
            return
        raise RuntimeError(f"Existing local Servo at {destination}; move it aside before installing the pinned version")
    with tempfile.TemporaryDirectory(dir=STATE) as temp:
        temp = Path(temp)
        archive = temp / "servo.zip"
        urllib.request.urlretrieve(f"https://github.com/adafruit/Servo/archive/{SERVO_COMMIT}.zip", archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != SERVO_SHA256:
            raise RuntimeError("Servo archive checksum differs from the tested source")
        with zipfile.ZipFile(archive) as z:
            for name in z.namelist():
                if Path(name).is_absolute() or ".." in Path(name).parts:
                    raise RuntimeError("Unsafe archive path")
            z.extractall(temp)
        source = temp / f"Servo-{SERVO_COMMIT}"
        patch_servo(source)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(source, destination)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--with-xiao", action="store_true", help="Also install ESP32 2.0.17 and Blynk")
    a = p.parse_args()
    if not shutil.which("arduino-cli"):
        raise SystemExit("Install arduino-cli first: https://arduino.github.io/arduino-cli/latest/installation/")
    configure()
    cli("core", "update-index")
    cli("core", "install", "Seeeduino:samd@1.8.6")
    cli("lib", "install", "ArduinoJson@7.4.3", "Seeed Arduino SSCMA@1.0.3", "TM1637@1.2.0")
    install_servo()
    if a.with_xiao:
        cli("core", "install", "esp32:esp32@2.0.17")
        cli("lib", "install", "Blynk@1.3.2")
    print("Installed. Next: python3 tools/build.py wio")

if __name__ == "__main__":
    main()
