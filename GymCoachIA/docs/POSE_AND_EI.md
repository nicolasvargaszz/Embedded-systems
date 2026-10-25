# Pose model and your own exercise classifier

## 1. Deploy the Vision AI pose model

Power down the Wio and unplug the Vision AI Grove lead. Connect the Vision AI V2
to your computer using a USB data cable, with the OV5647 ribbon correctly seated.
Open [SenseCraft AI](https://sensecraft.seeed.cc/ai) in a browser with Web Serial
support, select Grove Vision AI V2 and deploy a supported human-pose model with
17 COCO keypoints. Model catalog names may change: verify the output and device
compatibility rather than choosing an ordinary person-detection model.

Check the preview before reconnecting to the Wio. You need shoulder, elbow, wrist,
hip, knee and ankle points on a side-on person, including seated exercise views.
Record the observed inference rate. Disconnect the module's USB, then reconnect
its Grove cable to the Wio's left port and power the Wio.

No separate custom Vision AI firmware is part of this package. SenseCraft supplies
its model/runtime; `Seeed_Arduino_SSCMA` retrieves the pose results over I2C.
`invoke(1, true, false)` is used to avoid transferring camera images. Frames are
cleared before each invocation so omitted/failed results cannot reuse old poses.
The skeleton is fitted to the LCD without assuming a fixed 240-pixel source image.
Angles are calculated in the original coordinate system.

## 2. Record real examples

Use participant IDs instead of names. Keep the camera side-on and record both
left-facing and right-facing views for each participant. Use at least eight
participants, with two sets per exercise per side, if following your guide's study
design. Include stationary sitting,
standing, walking and machine setup in `descanso`; include varied speeds and
clothing in the exercise classes.

```bash
python3 tools/grabar.py --label curl --persona p01 --lado izq \
  --serie 1 --seconds 60 --port /dev/cu.usbmodem14101
```

The data files are timestamped to avoid overwriting earlier sessions:

```text
data/raw/curl.p01_izq_s001_<capture-time>.csv
data/raw/curl.p01_izq_s001_<capture-time>.json
data/ei/curl.p01_izq_s001_<capture-time>_part001.csv
```

Raw files preserve actual frame times and missing data. EI files have exactly:

```csv
timestamp,codo,rodilla,cadera,torso
```

The header above is a schema, not an example training sample. Features are in
degrees: shoulder-elbow-wrist, hip-knee-ankle, shoulder-hip-knee and shoulder-hip
inclination from vertical. Left/right use the same anatomical definitions.
The firmware classifier and Python exporter both linearly interpolate adjacent
valid frames on a 100 ms grid. They reset on a gap over 250 ms. No interpolation
crosses an invalid frame. Segments shorter than 20 samples are not training inputs.

For curl recordings, the lower-body features still need to be visible even though
the manual curl counter only requires upper-body joints. A scene containing only
the arm cannot supply the four-axis model's inputs.

Collect roughly balanced amounts for each label. The guide's target is at least
10 minutes per class. Review labels against your observations; don't leave long
idle periods labeled as an exercise. Use the raw metadata to inspect missing
frames and actual acquisition speed. If the camera cannot sustain useful samples,
fix acquisition before training or consistently redesign *both* sampling pipelines.

## 3. Keep participants out of the training set

```bash
python3 tools/split_dataset.py data/ei --output data/split --test-people p07 p08
```

This makes `training/`, `testing/` and a manifest. It refuses to reuse an existing
destination or silently ignore missing participant IDs. Upload the two folders
to their matching categories in Edge Impulse. Do not randomize windows from the
same participant between training and testing. Where possible also reserve
separate training participants for validation and threshold selection.

## 4. Build the Edge Impulse project

Create a time-series classification project. Use the CSV Wizard if needed: first
column is timestamp in milliseconds, remaining columns are four numeric axes.
Ensure labels are exactly `curl`, `sentadilla`, `extension`, `descanso`.

Configure a 2,000 ms window, 500 ms window increase, and 10 Hz frequency. Axis
order must remain **codo, rodilla, cadera, torso**. Start with a Flatten processing
block and a small classification network. Fit normalization and all preprocessing
using training data. Train, inspect the confusion matrix, then evaluate the held-out
participants. Check all four classes have test support. Do not quote an F1 until
you have real test results.

To calculate macro F1 independently, prepare one CSV row per tested decision with
columns `participant,truth,prediction`, using real held-out predictions:

```bash
python3 tools/evaluate_predictions.py predictions.csv --output heldout_metrics.json
```

The tool reports a confusion matrix, per-class precision/recall/F1 and unweighted
four-class macro F1. Overlapping windows are correlated, so report the test design
and participant IDs alongside the metric. Keep a separate manual rep-count
evaluation; exercise-label F1 does not measure counter accuracy.

## 5. Install and enable the exported model

From Edge Impulse Deployment, build the **Arduino library**. First test its
`static_buffer` example with a real validation window to check preprocessing and
inference. The Wio has limited SRAM; prefer a small quantized dense classifier.

Install the ZIP into the project's isolated sketchbook:

```bash
arduino-cli --config-file .arduino/config.yaml lib install \
  --zip-path /absolute/path/to/your-model.zip
```

If Arduino CLI disallows ZIP installs, explicitly enable that project-local
setting with `arduino-cli --config-file .arduino/config.yaml config set
library.enable_unsafe_install true`, or extract the exported Arduino library root
into `.arduino/user/libraries/` yourself. Only install your trusted export.

In `firmware/WioCoach/Settings.h`:

```cpp
#define COACH_USE_EI 1
#define COACH_EI_HEADER "your_actual_export_inferencing.h"
```

Use the actual top-level inferencing header name in your export. Then rebuild and
upload the Wio. The adapter checks 80 raw feature values, four axes, 100 ms sample
interval and four labels at compile time. Features are interleaved in time order:
four values at t0, then four at t1, etc. Do not manually flatten features before
passing them to `run_classifier`; the exported DSP block does that.

If an export's build fails inside a porting directory, keep the original ZIP,
record its SDK version and inspect the exact architecture error. The guide mentions
an Espressif-port conflict on SAMD, but this package does not indiscriminately delete
SDK source folders. Verify the export's static-buffer example first and use the
appropriate board/compiler compatibility fix for that export.

AUTO warms a rolling window and evaluates every five new samples (500 ms). Three
consecutive predictions must agree with confidence >=0.80. These are overlapping
decisions, not independent evidence. `descanso` never starts an exercise. Once a
set begins, the selected exercise is locked until the set ends. If a label is wrong,
end that set and select the exercise manually.

Finally publish your EI project when ready for the course, and record its actual
public URL, dataset protocol and held-out metrics in your report. This package
does not create or publish an EI account/project on your behalf.
