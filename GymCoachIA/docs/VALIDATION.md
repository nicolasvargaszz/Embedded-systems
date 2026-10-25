# Validation — 2026-10-25

The default firmware was cross-compiled with the real Arduino board cores and
libraries listed in `SOURCES.md`; these were not mock Arduino header builds.

| Build | Result | Program size |
|---|---|---:|
| Wio normal, servo enabled, EI disabled | PASS | 92,768 / 507,904 bytes |
| Wio output self-test, servo enabled | PASS | 68,104 / 507,904 bytes |
| Wio output self-test, servo disabled | PASS | 67,048 / 507,904 bytes |
| XIAO bridge, placeholder credentials | PASS | 862,940 / 1,310,720 bytes |

The XIAO build reported 44,764 bytes of global variables out of 327,680 bytes of
dynamic memory. A trained EI export changes Wio memory requirements; no memory
claim is made for an absent model.

`python3 tools/run_tests.py` passed **20 C++ checks and eight Python tests**.
The C++ tests compile with `-Wall -Wextra -Werror -pedantic`. They cover geometry,
low confidence, degenerate joints, the three exercise directions, multiple reps,
partial ranges, excessive torso inclination, jitter, startup mid-motion, missing
poses, unfinished reps, very fast movement, long gaps, millis rollover, side
selection and resampling.

Python tests cover noisy serial parsing, invalid angles, timestamp resets and
rollover, interpolation, gap/side boundaries, export schema, overwrite refusal,
participant-disjoint splits and F1 calculation. The eighth test runs the actual
`grabar.py` subprocess through a POSIX pseudo-terminal, emits explicitly synthetic
Wio-style serial frames, and verifies raw output, metadata and the 10 Hz EI CSV.
It does not use a real board or represent a training dataset. That test is skipped
on systems without POSIX pseudo-terminals or pyserial; the remaining tests still run.

All supplied Python files were syntax checked. The installer Servo patch was
checked against the pinned upstream archive: it changes the SAMD51 TC0 timer and
interrupt selection to TC1, retains SAMD21 definitions, and adds the compile-time
marker checked by the sketch. It leaves the SAMD51 shared GCLK channel at 9.

## Not verified here

- Flashing or running on your physical Wio, XIAO, camera, display, LEDs or servo.
- Current supply stability, I2C signal integrity, servo travel, buzzer volume,
  camera pose quality or sustained on-device frame rate.
- Real-world rep-count accuracy or the suitability of angle/tempo thresholds.
- A real Blynk connection, TLS/NTP reachability, account datastream configuration,
  on-device NVS power-loss behavior or cloud receipt.
- An EI-enabled build, trained model accuracy, held-out F1 or model memory usage.
  No custom trained model was provided. The disabled adapter follows the public
  inference API and includes dimension/interval checks for your eventual export.

## Hardware acceptance sequence

1. Check LEDs/display with the servo disconnected; then test servo and buzzer
   together using `--self-test` with the configured timer patch.
2. Verify the pose skeleton in SenseCraft, then on the Wio; observe actual FPS.
3. Manually compare five controlled repetitions of each exercise with the counter.
   Repeat partial ranges and a tracking interruption; confirm they do not become
   valid reps. Adjust thresholds for your camera setup and participants.
4. Record one short series and inspect raw timestamps, validity and EI segments.
5. Add the XIAO, confirm UART ACKs, then confirm matching set IDs in Blynk. Test
   offline queuing and reconnection before depending on remote history.
6. Train/export the real EI model, validate its static-buffer example, enable the
   adapter and repeat the hardware checks with the actual classifier installed.

Passing compilation and software tests is evidence about the implementation,
not proof of physical integration or measured exercise-recognition accuracy.
