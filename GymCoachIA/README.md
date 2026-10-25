# Gym Coach IA — full source package

This project targets the hardware in your **Guia_Gym_Coach_IA (1).pdf**:
Wio Terminal + Grove Vision AI V2 + OV5647 camera, XIAO ESP32-C3, TM1637
four-digit display, common-anode RGB LED, three LEDs, SG90 servo and Wio buzzer.
The other attachment is an unrelated page-replacement simulator and is not part
of this implementation.

The default firmware performs pose-based counting and rule-based feedback for
preacher curls, squats and leg extensions. It runs without a laptop or Wi-Fi once
the pose model has been deployed to the Vision AI module. The XIAO is optional
for local counting. Its firmware adds queued set summaries to Blynk.

**Two model stages are distinct.** The Vision AI needs a compatible, pretrained
17-keypoint COCO pose model deployed with SenseCraft. Automatic exercise selection
also needs *your own trained Edge Impulse Arduino export*. No pose binary, custom
trained weights, real participant dataset, F1 result, Wi-Fi credentials or Blynk
token is included. Manual selection needs only the first model. The EI integration
code is supplied but disabled until you have trained and exported your model.

## Start here

1. Unzip this folder and open a terminal inside `GymCoachIA`.
2. Install Python 3.10+ and [Arduino CLI](https://arduino.github.io/arduino-cli/latest/installation/).
3. Set up the Python environment and pinned Arduino dependencies:

```bash
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -r requirements.txt
python3 tools/install_arduino.py --with-xiao
```

On Windows activate with `.venv\Scripts\activate` and use `python` if that is your
Python command. Omit `--with-xiao` if you are starting with just the Wio.
Keep this environment activated for ESP32 compilation: the core's esptool needs
the Python dependencies in `requirements.txt`.

4. With power disconnected, wire the components using the table below. First
leave the servo unplugged while checking the LEDs and display.
5. Find the USB port:

```bash
arduino-cli --config-file .arduino/config.yaml board list
```

6. Check the outputs, replacing the example port with yours:

```bash
python3 tools/build.py wio --self-test --no-servo --upload /dev/cu.usbmodem14101
```

The test cycles RGB colors, depth lights, digits and beeps. After checking servo
power and wiring, run the test with the servo enabled:

```bash
python3 tools/build.py wio --self-test --upload /dev/cu.usbmodem14101
```

The needle moves gradually across its configured range. Unplug immediately if it
pushes against a mechanical stop; tune `SERVO_MIN_US`/`SERVO_MAX_US` in `Settings.h`.

7. Deploy the pose model to the Vision AI as described in `docs/POSE_AND_EI.md`.
8. Upload normal Wio firmware, removing the self-test flag:

```bash
python3 tools/build.py wio --upload /dev/cu.usbmodem14101
```

You can compile without uploading by omitting `--upload PORT`. Both build scripts
use an isolated `.arduino/` directory, so they do not replace the Servo patch or
libraries already installed in your Mac's global Arduino IDE sketchbook.

## Wiring

These are **physical positions on the Wio's 40-pin header**, not Arduino pin
numbers. Preserve the orientation in the guide; identify the marked pin 1.
All LED color channels need their own 220-ohm resistor.

| Component connection | Wio physical pin | Firmware name |
|---|---:|---|
| RGB red cathode, through 220 ohm | 13 | D0 |
| RGB green cathode, through 220 ohm | 15 | D1 |
| RGB blue cathode, through 220 ohm | 33 | D6 |
| RGB common anode | 17 | 3.3 V |
| Green LED anode, through 220 ohm | 16 | D2 |
| Yellow LED anode, through 220 ohm | 32 | D5 |
| Red LED anode, through 220 ohm | 36 | D7 |
| All three separate LED cathodes | 9 | GND |
| TM1637 CLK | 18 | D3 |
| TM1637 DIO | 22 | D4 |
| TM1637 VCC | 1 | 3.3 V |
| TM1637 GND | 6 | GND |
| SG90 signal, orange | 37 | D8 |
| SG90 supply, red | 2 | 5 V |
| SG90 ground, brown | 39 | GND |
| Vision AI Grove cable | Left Grove port, viewed from screen | Wire, I2C 0x62 |
| XIAO D7/RX (GPIO20) | 8, Wio TX | Serial1 TX |
| XIAO D6/TX (GPIO21) | 10, Wio RX | Serial1 RX |
| XIAO GND | 14 | GND |

The RGB is common anode: LOW lights a channel. This project uses digital color
states, avoiding PWM timer use on those pins. The bundled installer patches
Adafruit's SAMD51 Servo implementation onto TC1. Wio `tone()` uses TC0. Do not
add `analogWrite()` on D2/D8, or D6 while using the buzzer.

Start the TM1637 at 3.3 V as specified in your guide. **Do not move it directly to
5 V if its CLK/DIO pull-ups would then feed 5 V into the Wio.** If your particular
display needs 5 V, use suitable bidirectional level shifting on its signal lines.
That extra hardware is not assumed to be in your kit.

The SG90 needs 5 V, never the 3.3 V rail. If moving it resets the Wio or Vision AI,
use an adequate regulated 5 V servo supply with common ground. Do not join that
supply's 5 V output to a separately USB-powered board's 5 V rail. Start with the
servo disconnected if the available supply is uncertain.

Power the XIAO from its own USB and connect only UART and ground between boards.
Solder its headers and attach the antenna first. It remains a separate bridge,
not a second controller mounted onto the Vision AI socket. Keep the Vision AI's
USB disconnected while it is powered by the Wio Grove cable.

## Wio controls and behavior

| Control | Action |
|---|---|
| Stick up/down | Select exercise, target, weight or camera-facing body side |
| Stick left/right | Change selected setting |
| Stick press | Start a set; finish/save an active set; cancel AUTO identification |
| A | Finish/save the active or paused set |
| B | Pause/resume; discard an unfinished repetition |
| C | Clear the displayed result while idle/done |

Settings are session-local; reboot returns to curl, 10 reps, 5 kg and automatic
side selection. `LEFT`/`RIGHT` means the **person's anatomical side**, not screen
left/right. Weight is entered manually, not measured by a sensor.

For a manual set, choose the exercise, press the stick and hold its starting
position for about half a second. The counter requires an outward movement and
return. Leg extensions use increasing knee angle; curls and squats use decreasing
joint angle. A three-frame mean reduces jitter. Small movements do not count.

The big number, TM1637 and servo use **valid reps**. Bad completed attempts appear
in `Errors` and do not advance the target. Very short cycles below 600 ms are
rejected as noise/too fast and do not increment either counter. Completing the
valid-rep target ends the set automatically. A manually ended set is also saved,
with `complete=false` if the goal was not reached.

RGB is blue while waiting/paused or pose is missing, green during tracked
activity, and red briefly after a detected fault. The separate LEDs form a
cumulative red-to-yellow-to-green range bar. One beep marks a valid rep, two a
fault, and three ascending notes mark the goal. Feedback text follows the
Spanish labels in your guide.

Only the exercise's required joints must be visible for manual counting. Dataset
capture and EI classification require shoulder, elbow, wrist, hip, knee and ankle
on one side, so all four features are real. Side selection locks during a set or
recording. If tracking disappears for over 300 ms, the unfinished rep is discarded
and the user must return to the start. No old pose is counted after an I2C error.
Use a single, clearly visible person; the bounding-box continuity check is not
full multi-person identity tracking.

## Thresholds and calibration

`firmware/WioCoach/CoachCore.h`, function `profile()`, contains all exercise
thresholds. They are provisional engineering defaults adapted from your guide.

| Exercise | Broad position to arm | Good return | Required far position |
|---|---:|---:|---:|
| Curl | Elbow >=115 degrees | >=140 degrees | <=55 degrees |
| Squat | Knee >=145 degrees | >=160 degrees | <=100 degrees |
| Extension | Knee <=115 degrees | <=105 degrees | >=155 degrees |

A broad arming range lets incomplete curls be recognized and flagged when they
return or reverse, instead of silently never producing an event. Squat torso
inclination over 45 degrees and completed cycles under one second are flagged.
Extension returns under 400 ms also produce the slow-down message. These are
2D motion heuristics, not a validated assessment of safe technique.

Position the camera side-on and level, with the working joints visible. Record
controlled examples from your actual camera view, compare counted reps against
manual counts and adjust thresholds before the demo. Seated machine occlusion,
camera tilt and depth motion can distort 2D angles. Keep camera placement
consistent. Threshold edits require rebuilding/flashing the Wio.

## Dataset and automatic exercise selection

Full training/deployment instructions are in `docs/POSE_AND_EI.md`.

```bash
python3 tools/grabar.py --port /dev/cu.usbmodem14101 \
  --label sentadilla --persona p03 --lado izq --serie 1 --seconds 60

python3 tools/split_dataset.py data/ei --output data/split \
  --test-people p07 p08
```

Record all four labels: `curl`, `sentadilla`, `extension`, `descanso`. The recorder
saves raw timestamps and metadata, then exports uniform 10 Hz segments. It splits
at missing joints, side changes or gaps over 250 ms. Short segments are excluded;
missing motion is not filled by holding the last angle. Firmware EI sampling uses
the same interpolation policy. Real camera FPS is reported; an exported 10 Hz
file is not proof that inference runs at 10 FPS.

AUTO requires a 2-second, four-axis EI model. After enabling it and pressing
start, perform warm-up movements until one label exceeds the confidence
threshold for three overlapping decisions. The detected exercise then stays
fixed for that set. Return to the starting position before counted reps begin.
Warm-up reps are deliberately excluded. Select the exercise manually whenever
automatic identification is unavailable or unreliable.

## XIAO and Blynk

Edit `firmware/XiaoBridge/secrets.h` with your 2.4 GHz Wi-Fi details, template ID,
template name and device auth token. The provided file contains only empty/example
values. Keep a populated copy private; it is ignored by Git.

Create the virtual datastreams described in `docs/BLYNK.md`, then:

```bash
python3 tools/build.py xiao --upload /dev/cu.usbmodem14201
```

With empty credentials the bridge still compiles and can acknowledge stored UART
sets, but will not connect to Wi-Fi/Blynk. Set summaries are queued in flash on
the XIAO. Cloud delivery is best effort; `docs/BLYNK.md` describes the limits.

## Files

| Path | Purpose |
|---|---|
| `firmware/WioCoach/WioCoach.ino` | Main loop, controls, LCD, telemetry, session lifecycle |
| `firmware/WioCoach/CoachCore.h` | Joint geometry, resampling, tested rep state machine |
| `firmware/WioCoach/Vision.h` | SSCMA I2C pose acquisition, person continuity, body side |
| `firmware/WioCoach/Outputs.h` | TM1637, RGB, depth LEDs, servo, sound patterns |
| `firmware/WioCoach/EIAdapter.h` | Rolling-window Edge Impulse integration |
| `firmware/WioCoach/Uplink.h` | Bounded UART summary queue, retry and ACK handling |
| `firmware/WioCoach/Settings.h` | Pins, feature switches, confidence and servo settings |
| `firmware/XiaoBridge/XiaoBridge.ino` | Persistent queue, deduplication, Wi-Fi/Blynk upload |
| `tools/grabar.py`, `dataset.py` | Serial recorder and matching resampling/export logic |
| `tools/export_ei.py` | Re-export existing raw recordings into a fresh directory |
| `tools/split_dataset.py` | Participant-disjoint training/testing folders |
| `tools/evaluate_predictions.py` | Confusion matrix, per-class F1 and macro F1 |
| `tools/install_arduino.py`, `build.py` | Dependency setup, compile and explicit upload |
| `tools/run_tests.py`, `tests/` | Automated logic and dataset checks |
| `docs/VALIDATION.md` | What was verified and what needs hardware testing |

Run software checks with `python3 tools/run_tests.py`. A C++ compiler is required.
Nothing in those tests is a real training dataset or a hardware accuracy claim.

## Troubleshooting

- **No Vision AI:** check the left Grove port, the 0x62 device, CSI ribbon and pose
  deployment. The LCD reports acquisition failure. The SSCMA library uses blocking
  requests, so a disconnected camera can temporarily delay button response.
- **No reps:** check the exercise and body side; hold the start, make a full cycle
  and expose required joints. Invalid or partial attempts do not increase valid reps.
- **Servo compile error or no patch marker:** use this project's setup script. Do
  not update the isolated Servo folder through the IDE Library Manager.
- **Upload port disappears:** enter the Wio bootloader with its double switch
  action, re-list ports, close Serial Monitor and retry using the new port.
- **Recorder can't open port:** close the Arduino Serial Monitor and verify the
  Wio USB port, not the XIAO. `--port` is needed if detection is ambiguous.
- **No EI CSV segments:** the chosen side needs all six joints and at least two
  seconds without long gaps. Adjust lighting/framing and inspect actual FPS.
- **Queue full:** keep boards powered and reconnect the XIAO/network. The current
  completed set is retained on the Wio and another set is blocked until queued.
- **Wio restarts with servo:** disconnect the servo and correct its power supply.

Source references and dependency versions are in `docs/SOURCES.md`.
