# Inputs and pinned dependencies

Prepared 2026-10-25 for the supplied Gym Coach IA guide. The hardware and scope
come from `Guia_Gym_Coach_IA (1).pdf`, especially sections 1–6 and its pin diagram.
A final report from a previous cohort describes another project and
contributed no implementation requirements.

## Dependency versions

| Dependency | Version/source |
|---|---|
| Arduino CLI used for validation | 1.3.1 |
| Wio core | Seeeduino:samd 1.8.6 |
| LCD | Seeed_Arduino_LCD 1.6.0, bundled in the above core |
| Vision interface | Seeed Arduino SSCMA 1.0.3 |
| JSON | ArduinoJson 7.4.3 |
| Display | TM1637 by Avishay Orpaz, 1.2.0 (`TM1637Display.h`) |
| Servo | adafruit/Servo, `adafruit_samd51` commit `962192408a0a2608351fc54e567bca28ccb2b74b`, patched TC0→TC1 |
| XIAO core | esp32:esp32 2.0.17 |
| Cloud client | Blynk 1.3.2, SSL variant |
| Python serial | pyserial 3.5 |
| Python ESP tool dependencies | esptool 4.5.1 |
| Your exercise model | Your Edge Impulse Arduino export; not supplied |

The Servo download is SHA-256 checked by `install_arduino.py`. Its original
upstream copyright/license notices remain in the downloaded dependency. Third-party
libraries and compiler binaries are installed by the script, not redistributed in
this source ZIP. The dependency versions are selected for reproducibility, not
claimed to be the newest releases. The local compiler build used the exact Wio
core's source for pin and timer verification.

## Primary references

- [Seeed Wio digital I/O, UART and Servo guidance](https://wiki.seeedstudio.com/Wio-Terminal-IO-Digital/)
- [Wio getting started](https://wiki.seeedstudio.com/Wio-Terminal-Getting-Started/)
- [Wio board variant definitions](https://github.com/Seeed-Studio/ArduinoCore-samd/tree/master/variants/wio_terminal)
- [Seeed core's buzzer timer implementation](https://github.com/Seeed-Studio/ArduinoCore-samd/blob/master/cores/arduino/Tone.cpp)
- [Grove Vision AI V2](https://wiki.seeedstudio.com/grove_vision_ai_v2/)
- [SSCMA interface source](https://github.com/Seeed-Studio/Seeed_Arduino_SSCMA)
- [Seeed pose deployment workflow](https://wiki.seeedstudio.com/ma_deploy_yolov8_pose/)
- [SAMD51 Servo branch](https://github.com/adafruit/Servo/tree/adafruit_samd51)
- [XIAO ESP32-C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/)
- [Edge Impulse Arduino deployment](https://docs.edgeimpulse.com/hardware/deployments/run-arduino-2-0)
- [Edge Impulse inference functions](https://docs.edgeimpulse.com/tools/libraries/sdks/inference/cpp/functions)
- [Blynk virtual pins](https://docs.blynk.io/en/blynk-library-firmware-api/virtual-pins)

## Deliberate implementation choices

The guide's broad design is retained. The counter separates a completed attempt
from a valid rep, allowing partial-range feedback. Extension direction is reversed.
Sampling uses measured frame times and bounded interpolation instead of claiming
fixed-rate fresh inference. Servo and buzzer are assigned different timers. RGB
states use digital output; 5 V display operation requires appropriate signal-level
handling. These choices are documented in code and README rather than silently
copying assumptions from example sketches.
