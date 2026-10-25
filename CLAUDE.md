# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this directory is

Workspace for the **2026 NTUST Embedded Systems course** (Lecturer: David Tseng / CAVEDU). Git repo,
pushed to the public <https://github.com/nicolasvargaszz/Embedded-systems> so teammates can clone it;
the course doc and `temas_de_años_anteriores/` are git-ignored (other cohorts' names/grades). Layout:

- `2026 NTUST embedded system Course Info.docx` / `.pdf` — the live course document (schedule,
  assignment specs, code templates, links). Source of truth for every requirement.
- `clases/<activity>/<activity>.ino` — in-class checkpoints (servo, mic, DHT20, RGB).
- `vision_wio/` — Grove Vision AI V2 → Wio over I2C with live camera on the LCD.
- `proyecto/` — `coach_pc.py` (laptop viewer + dataset recorder, Vision AI over USB), pose model, `datos/`.
- `GymCoachIA/` — teammates' full Wio firmware + tools (made with ChatGPT; own README, isolated `.arduino/`).
- `Guia_Gym_Coach_IA.pdf` — the group's semester-project guide (Gym Coach IA: pose-based rep
  counter + technique checker). Wiring, pin map, dataset protocol, plan.
- `image/` — kit photos (`componentes.jpeg` = pinout card). `temas_de_años_anteriores/` — previous
  cohorts' final-project PDFs.

Keep it lean: one folder per sketch, no extra scaffolding.

### Reading the course document

It is a `.docx`, so read it as text rather than opening it:

```bash
unzip -o -q "2026 NTUST embedded system Course Info.docx" -d /tmp/coursedoc
sed -e 's/<w:p [^>]*>/\n/g' -e 's/<[^>]*>//g' /tmp/coursedoc/word/document.xml | sed '/^[[:space:]]*$/d'
```

Caveat: the doc is a reused Google Doc. It carries **completed mid/final demo grading comments and
group rosters from a previous cohort** alongside the 2026 schedule. Don't treat a dated section as
this semester's requirement without checking it against the live doc (<http://bit.ly/2026_ntust_iot>).

## Target hardware

**Seeed Wio Terminal** — ATSAMD51P19 (Cortex-M4F @ 120 MHz) + Realtek RTL8720DN radio. Everything
must run on the Wio Terminal; a plain Arduino board does not satisfy the project requirements.

On-board peripherals the assignments use (no wiring needed): 320x240 LCD, LIS3DHTR accelerometer,
microphone, buzzer, light sensor, 5-way switch, 3 user buttons, microSD slot.

Kit peripherals (Grove connectors): ultrasonic distance sensor, DHT20 temp/humidity (ASAIR),
multichannel gas sensor. Groups additionally buy LEDs, RGB LED, SG90 servo, breadboard, relay,
jumper wires.

Off-board wiring goes to the 40-pin header: **header pin 37 = D8**, **pin 39 = GND**. `componentes.jpeg`
is the full pinout card — read it before assigning pins.

## Toolchain

Documented workflow is the **Arduino IDE 2.x GUI** (no Makefile or CI). `arduino-cli` 1.5.1 is
also installed (Homebrew, user-approved 2026-10-06) and shares the IDE's dirs, so it can build and
upload directly:
`arduino-cli compile -u -p /dev/cu.usbmodem101 --fqbn Seeeduino:samd:seeed_wio_terminal <sketch_dir>`.
Add `--library ~/Library/Arduino15/packages/Seeeduino/hardware/samd/1.8.6/libraries/Seeed_Arduino_LCD`
to use the core's TFT_eSPI instead of Seeed_GFX; pass `--clean` when switching, or it fails to link.

Current machine state (verify before assuming a build will work):

- Sketchbook: `~/Documents/Arduino`, libraries in `~/Documents/Arduino/libraries`
- Arduino data dir: `~/Library/Arduino15`
- Seeed SAMD core 1.8.6 is installed.

One-time IDE setup:

1. File → Preferences → Additional Boards Manager URLs:
   `https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json`
2. Boards Manager → search "wio" → install **Seeed SAMD Boards** (latest is fine).
3. Select board: *Seeeduino Wio Terminal*. Port appears as `/dev/cu.usbmodem*` on macOS.

If upload fails, double-tap the power switch down to enter bootloader mode.

## Known traps (all cost real debugging time)

- **Servo library on SAMD51** — stock `arduino-libraries/Servo` (1.2.2 / 1.3.0) has a
  `src/samd/` backend for SAMD21 only (`GCLK->CLKCTRL`, `STATUS.bit.SYNCBUSY` → compile errors on
  the Wio). Fix applied on this Mac: `src/samd/Servo.cpp` and `ServoTimers.h` replaced with
  Adafruit's SAMD51-capable versions (github.com/adafruit/ArduinoCore-samd, libraries/Servo) in
  **both** copies: `~/Documents/Arduino/libraries/Servo` (1.3.0, the one the IDE uses) and
  `~/Library/Arduino15/libraries/Servo`. Originals are backed up in
  `~/Documents/Arduino/Servo_samd21_original/` and `~/Library/Arduino15/libraries/Servo/samd21_original/`.
  Updating Servo from the Library Manager undoes the fix, so re-apply it. That backend uses
  **TC1**, so don't `analogWrite` on D2/D8 while Servo is attached. The fallback without a library
  (bit-bang 1–2 ms pulse every 20 ms) is in `clases/servo_test/`.
- Other library state on this Mac: `DHT_kxn` was moved out to `~/Documents/Arduino/DHT_kxn_desactivada`
  (its `DHT.h` shadowed Seeed's DHT20 library). Seeed_Arduino_Linechart, Seeed_Arduino_SSCMA,
  ArduinoJson and TJpg_Decoder are installed in `~/Documents/Arduino/libraries`.
- **Header pin number ≠ Arduino pin name.** The professor says "use PWM0": that is header
  **pin 13 = `D0`** in code, not `D13`. Always translate via the card (pin 13 = D0/A0/PWM0,
  pin 15 = D1/A1, pin 37 = D8/A8/PWM4).
- **Grove Vision AI V2 (SSCMA library) over I2C** — on non-ESP32 boards the library's rx buffer
  is 4 KB, but every `invoke()` reply carries a ~4 KB base64 JPEG, so replies overflow → timeouts /
  "SIN RESPUESTA" / ~10 s per result. Call `AI.set_rx_buffer(32*1024)` after `AI.begin()`. Image
  is 240 px wide; boxes are in those pixels. See `vision_wio/vision_wio.ino`.
- **Grove Vision AI V2 pose model** — SenseCraft's library has no pose model for this board. The
  working one is Himax's YOLOv8n-pose 256 (`proyecto/yolov8n_pose_256_vela.tflite`), flashed via
  SenseCraft Workspace → *Upload Model* (any name, one object "person"). The flash shows **no
  progress bar** and takes ~3 min: don't disconnect until "Model Name" changes. Over USB serial
  (921600) each AT message arrives as `\r{...}\n`; keypoints are `[[x,y,score 0-100,id] ×17]`.
- **Vision AI camera flex** — boot log `dw_iic_write err ... Fail:-60` plus `AT+SENSOR` / `INVOKE`
  returning `code 4` means the camera ribbon is not seated; reseat it and replug USB.
- **TJpg_Decoder** — `#define TJPGD_LOAD_SD_LIBRARY` in its `src/User_Config.h` is commented out
  here (the Wio core has no `SD.h`). A library update will undo that.
- **Wi-Fi is 2.4 GHz only.** With an iPhone hotspot, enable *Maximize Compatibility* or the board
  will never associate.
- **Wi-Fi firmware flash** — close the Arduino IDE first, otherwise the serial port is occupied.
  Run `ambd_flash_tool erase` then `ambd_flash_tool flash`.
- **Wio bootloader drive never mounts on this Mac** — double-sliding the switch does enter the
  bootloader (USB PID `0x002d`; normal mode is `0x802d`), but no "Arduino" disk appears. Flash a
  `.uf2` instead by converting it to `.bin` (payload starts at `0x4000`) and running the core's
  `bossac` 1.8 while the board sits in the bootloader:
  `bossac --port=cu.usbmodemXXXX -U -i --offset=0x4000 -w -v fw.bin -R`.
- **Edge Impulse firmware (wio-terminal-ei-1.4.0)** samples the light sensor only at **62.5 or
  100 Hz** (Seeed's tutorial says 40 Hz, which this firmware doesn't offer). **Use 100 Hz**:
  room lights flicker at 120 Hz (60 Hz mains) and the sensor sees it; at 62.5 Hz it aliases to
  5 Hz, right on top of the hand gestures (HW2's first dataset: 2/3 of the signal, model at 40%).
  At 100 Hz it lands at 20 Hz, killed by a fixed 5-tap moving average (first layer of
  `clases/hw2_gestos/modelo_keras.py`).
- **Edge Impulse "Conflicting CPU architectures"** — delete the espressif porting folder from the
  generated library: `<sketchbook>/libraries/<model>_inferencing/src/edge-impulse-sdk/porting/espressif`.
- **RGB LEDs: the course asks for common cathode, but this group's two are common ANODE** — the common
  (longest) leg goes to 3V3, and a channel is ON when its pin is LOW (`analogWrite(pin, 255 - v)`).
- **Buzzer** is `WIO_BUZZER`; drive it with `analogWrite()` for on/off volume or `tone(pin, freq, ms)`
  for pitch. Wio pin constants (`WIO_BUZZER`, `WIO_MIC`, `WIO_LIGHT`, `WIO_KEY_A/B/C`,
  `WIO_5S_UP/DOWN/LEFT/RIGHT/PRESS`) come from the Seeed core — prefer them over raw numbers.

## Stack used across assignments

Work spans three layers; a finished project usually touches all three.

- **Firmware** — Arduino C++ on the Wio Terminal. LCD library is **`Seeed_GFX`** — the old
  `Seeed_Arduino_LCD` repo was renamed, so that name finds nothing in Library Manager; install the
  ZIP from <https://github.com/Seeed-Studio/Seeed_GFX>. It still provides `TFT_eSPI.h` and
  auto-detects the Wio Terminal (`BOARD_SCREEN_COMBO 500`), so no `User_Setup.h` editing is needed.
  Installed here at `~/Documents/Arduino/libraries/Seeed_GFX`. The IDE only scans that folder at
  startup — restart it after a manual library install.
- **Edge ML** — Edge Impulse is the main pipeline: collect on-device (WebUSB firmware) → train in
  the EI studio → deploy back as an Arduino library that runs offline on the board. Also used:
  Google Teachable Machine (`.tflite` + `labels.txt`, run through `TM2_tflite.py`), TensorFlow Lite
  Micro, MediaPipe on the host. Deliverables normally require a **public Edge Impulse project URL**
  plus the F1 score.
- **IoT / host side** — Blynk (template ID + auth token + virtual pins, e.g. V5) for dashboards,
  charts, and control widgets; Python on the laptop talking to the board over USB serial
  (`pyserial`) for the MediaPipe assignment.

Assignments are group work (max 4 people). HW1–HW5 are 6% each, mid demo 25%, final demo 35%. The
mid demo is an early slice of the final project, not a separate thing. Code plus screenshots get
uploaded to the course Google Drive per group, so keep sketches self-contained and reproducible
from the doc alone. Circuit diagrams are expected in Fritzing.

## Course reference links

- Wio Terminal wiki: <https://wiki.seeedstudio.com/Wio-Terminal-Getting-Started/>
- Edge Impulse TinyML series: <https://wiki.seeedstudio.com/Wio-Terminal-TinyML-EI-1/>
- Seeed sketchbook examples: <https://github.com/Seeed-Studio/Seeed_Arduino_Sketchbook>
- CAVEDU blog (Chinese tutorials, Blynk/TinyML/MediaPipe): <https://blog.cavedu.com>
