# Mid demo script (8 min, 4 speakers)

Corrected version of the team's script: no servo, no TM1637 display, no XIAO/Blynk in the mid
demo; real Edge Impulse numbers filled in.

## Speaker 1 (0:00–2:00)

**Slide 1: Cover (~40 s)**

"Good morning, everyone. We are [team names], and today we present Gym Coach IA, our midterm
project for Embedded Systems.

The idea is simple. Imagine a small device that watches you exercise, counts your repetitions,
and knows which exercise you are doing, without a phone and without the cloud. The AI runs on
small embedded hardware, and this is what we built using the Wio Terminal Education Kit. Let me
explain what the system does."

**Slide 2: What it does (~80 s)**

"The system works in three steps.

Step one is See. A Grove Vision AI V2 camera runs a pose model, and this model finds 17 points
on the human body, such as shoulders, elbows, hips, knees and ankles.

Step two is Measure. From those points, we calculate four angles: elbow, knee, hip and torso.
When these angles go up and down, we know a repetition happened, so the system counts reps
automatically.

Step three is Recognize. The four angles over time feed a classifier trained in Edge Impulse. It
decides which exercise you are doing: a bicep curl, a squat, a leg extension, or rest.

The important point is that this is TinyML: machine learning that runs directly on a small
microcontroller. My teammate will now explain how the pose pipeline works."

## Speaker 2 (2:00–4:00)

**Slide 3: Pose pipeline (~60 s)**

"Thank you. Here you can see the pipeline from left to right, in four blocks.

One, the camera. We use an OV5647 camera connected to the Vision AI V2 board.

Two, the pose model. The board runs YOLOv8n-pose, a small version of the YOLO family optimized
to run on this chip. It gives us 17 keypoints for each frame.

Three, the angles. With the keypoints, we calculate elbow, knee, hip and torso angles.

Four, the rep counter. It watches the angles and counts each full repetition live.

One practical detail: SenseCraft AI does not include a pose model for this board, so we had to
upload our own model file. The upload takes about three minutes and shows no progress bar, so
you must not disconnect the board early."

**Slide 4: Recording the dataset (~60 s)**

"A classifier needs data, so we recorded our own dataset with a Python script called
coach_pc.py. It shows the camera image, the skeleton, the angles and the rep counter in real
time.

There are four labels: curl, squat, extension and rest. Each sample is 10 seconds long at 10 Hz.
The script has a guided mode: a voice asks for curl, squat, extension and rest, round after
round, and it records by itself.

Data quality matters, so we follow simple rules: 2 to 3 meters from the camera, full body in the
frame, and only one person. We recorded three people, from the side and from the front. If the
knee is not visible, the sample is bad and we repeat it.

Now my teammate will show how we trained the model."

## Speaker 3 (4:00–6:00)

**Slide 5: Training in Edge Impulse (~60 s)**

"Thank you. We uploaded all samples to Edge Impulse and designed the impulse.

We use a window of 2000 milliseconds, which is 2 seconds of movement, and the window moves
forward every 500 milliseconds. The data runs at 10 Hz. The processing block is Flatten, and the
learning block is a small neural network with two dense layers, with the four angles as input.

We trained on 182 samples. On the test set the model reaches 86.6% accuracy and an F1 score of
0.90. Squat is almost perfect, at 0.99. The hardest one is leg extension, at 0.78, because it
looks similar to a squat: in both, the knee bends."

**Slide 6: Running on the hardware (~60 s)**

"Now, how does it run on the real hardware? We split the work between two boards.

The Grove Vision AI V2 runs the pose model, because it has the camera and the processing power
for images. It sends the 17 keypoints to the Wio over I2C.

The Wio Terminal calculates the angles and runs the Edge Impulse model, one decision every
200 milliseconds. In AUTO mode, you do a few warm-up reps and the model chooses the exercise by
itself.

Then the system gives feedback through the outputs: the Wio screen shows the exercise and the
reps, an RGB LED shows the state (blue for rest, green for exercise, red for an error), three
LEDs show how deep the movement is, and the buzzer beeps for each valid rep. Over USB, the
laptop also says the exercise out loud.

So the whole chain, from camera to decision to feedback, runs on small embedded devices. Now my
teammate will show it working."

## Speaker 4 (6:00–8:00)

**Slide 7: Demo and next steps (~120 s)**

Live demo (~60–70 s). "Let me show you the system. I stand sideways to the camera, with my full
body in the frame. I do a few curls. You can see the exercise on the Wio screen, the RGB LED
turning green and the repetition counter going up. Now I do squats, and the system changes the
exercise automatically. [Narrate what appears on the screen and LEDs.]"

Result (~15 s). "As you saw, [one sentence about how well it worked]. In our own live test on
the Wio, it was right 86% of the time."

Lesson (~20 s). "Our main lesson was about hardware details. If the camera ribbon cable is loose,
the camera fails with an error. The full body must be in the frame, even for curls, because the
model uses all four angles. And the servo drew too much current and froze the Wio, so we removed
it for now."

Next steps (~20 s). "Next, we want to record more people and both sides of the body, add more
exercises, and send the workout history to a Blynk dashboard with a XIAO ESP32-C3.

Thank you for your attention. We are happy to answer your questions."
