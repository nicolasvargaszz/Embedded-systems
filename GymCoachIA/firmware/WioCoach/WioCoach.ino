#include <Arduino.h>
#include <algorithm>
#include <functional>
#include <vector>
#include "Settings.h"
#include "CoachCore.h"
#include "Vision.h"
#include "Outputs.h"
#include "EIAdapter.h"
#include "Uplink.h"
#include <TFT_eSPI.h> // after STL: this bundled LCD library defines a min macro

using coach::Exercise;
enum class Mode { Idle, AutoWait, Running, Paused, Done };
struct Key {
  uint8_t pin; bool raw = false, stable = false; uint32_t changed = 0;
  explicit Key(uint8_t p) : pin(p) {}
  void begin() { pinMode(pin, INPUT_PULLUP); }
  bool pressed(uint32_t now) {
    bool down = digitalRead(pin) == LOW;
    if (down != raw) { raw = down; changed = now; }
    if (now - changed >= 25 && stable != raw) { stable = raw; return stable; }
    return false;
  }
};
Key up(WIO_5S_UP), down(WIO_5S_DOWN), left(WIO_5S_LEFT), right(WIO_5S_RIGHT);
Key center(WIO_5S_PRESS), keyA(WIO_KEY_A), keyB(WIO_KEY_B), keyC(WIO_KEY_C);
TFT_eSPI lcd;
Vision vision;
Outputs outputs;
EIAdapter classifier;
Uplink uplink;
coach::RepCounter counter;
Mode mode = Mode::Idle;
Exercise exercise = Exercise::Curl;
int choice = 0, field = 0, sideChoice = -1;
uint16_t goal = 10;
float weightKg = 5;
bool streaming = false, frameValid = false, summaryPending = false;
uint32_t lastVision = 0, lastUI = 0, lastLoop = 0, setMs = 0, flashUntil = 0;
uint8_t feedbackFaults = 0;
float fps = 0;
char command[64] = {};
size_t commandSize = 0;
bool commandOverflow = false;
const char* notice = "Choose settings; press stick";

const char* title(Exercise e) {
  if (e == Exercise::Curl) return "Preacher curl";
  if (e == Exercise::Squat) return "Squat";
  if (e == Exercise::Extension) return "Leg extension";
  return "Rest";
}
const char* correction(uint8_t f) {
  if (f & coach::Torso) return "PECHO ARRIBA";
  if (f & coach::Range) return exercise == Exercise::Squat ? "BAJA MAS" : (exercise == Exercise::Curl ? "FLEXIONA MAS" : "EXTENDE COMPLETO");
  if (f & coach::RestRange) return exercise == Exercise::Curl ? "ESTIRA EL BRAZO" : "VOLVE AL INICIO";
  if (f & coach::Fast) return "MAS LENTO";
  return "BIEN HECHO";
}
void startSet(Exercise e) {
  if (summaryPending) return;
  exercise = e; counter.configure(e, coach::profile(e)); setMs = 0;
  vision.unlock(); classifier.reset(); mode = Mode::Running;
  feedbackFaults = 0; flashUntil = 0; notice = "Hold the starting position";
  outputs.reps(0, goal);
}
void finishSet() {
  if (mode != Mode::Running && mode != Mode::Paused) return;
  if (mode == Mode::Running) { const uint32_t now = millis(); setMs += now - lastLoop; lastLoop = now; }
  mode = Mode::Done; summaryPending = true;
  notice = counter.good >= goal ? "SET COMPLETE" : "Set saved";
  outputs.beep(counter.good >= goal ? 3 : 1);
  counter.loseTracking(); classifier.reset();
}
void toggleStart() {
  if (mode == Mode::Running || mode == Mode::Paused) { finishSet(); return; }
  if (mode == Mode::AutoWait) { mode = Mode::Idle; notice = "Auto cancelled"; return; }
  if (summaryPending) { notice = "Upload queue full; keep power"; return; }
  if (choice == 3) {
#if COACH_USE_EI
    classifier.reset(); vision.unlock(); mode = Mode::AutoWait; notice = "Warm-up: identify exercise";
#else
    notice = "Install EI model to use AUTO";
#endif
  } else startSet(static_cast<Exercise>(choice));
}
void handleKeys(uint32_t now) {
  const bool u = up.pressed(now), d = down.pressed(now), l = left.pressed(now), r = right.pressed(now);
  const bool c = center.pressed(now), a = keyA.pressed(now), b = keyB.pressed(now), reset = keyC.pressed(now);
  if (c) toggleStart();
  if (a) finishSet();
  if (b && (mode == Mode::Running || mode == Mode::Paused)) {
    mode = mode == Mode::Running ? Mode::Paused : Mode::Running;
    counter.loseTracking(); classifier.reset(); vision.unlock();
    notice = mode == Mode::Paused ? "PAUSED" : "Hold the starting position";
  }
  if (mode != Mode::Idle && mode != Mode::Done) return;
  if (summaryPending) return;
  if (reset) { counter.reset(); outputs.reps(0, goal); mode = Mode::Idle; notice = "Ready"; }
  if (u) field = (field + 3) % 4;
  if (d) field = (field + 1) % 4;
  const int delta = int(r) - int(l);
  if (!delta) return;
  if (field == 0) {
    choice = (choice + delta + 4) % 4;
    if (choice < 3) exercise = static_cast<Exercise>(choice);
    classifier.reset(); vision.unlock();
  } else if (field == 1) goal = constrain(int(goal) + delta, 1, 99);
  else if (field == 2) weightKg = coach::clampf(weightKg + delta * 0.5f, 0, 250);
  else { sideChoice = ((sideChoice + 1 + delta + 3) % 3) - 1; vision.requestedSide = sideChoice; vision.unlock(); }
}
void handleCommand() {
  if (!strcmp(command, "STREAM 1")) { streaming = true; classifier.reset(); vision.unlock(); Serial.println("# stream on; F,1,ms,side,codo,rodilla,cadera,torso"); }
  else if (!strcmp(command, "STREAM 0")) streaming = false;
  else if (!strcmp(command, "SIDE AUTO")) { sideChoice = -1; vision.requestedSide = -1; vision.unlock(); }
  else if (!strcmp(command, "SIDE LEFT")) { sideChoice = 0; vision.requestedSide = 0; vision.unlock(); }
  else if (!strcmp(command, "SIDE RIGHT")) { sideChoice = 1; vision.requestedSide = 1; vision.unlock(); }
  else if (!strcmp(command, "STATUS")) { Serial.print("# vision_error="); Serial.print(vision.lastError); Serial.print(" queue="); Serial.println(uplink.queued()); return; }
  else { Serial.println("# commands: STREAM 1|0, SIDE AUTO|LEFT|RIGHT, STATUS"); return; }
  counter.loseTracking(); classifier.reset();
}
void readCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      if (!commandOverflow) { command[commandSize] = 0; handleCommand(); }
      commandSize = 0; commandOverflow = false;
    } else if (c != '\r' && !commandOverflow) {
      if (commandSize + 1 < sizeof(command)) command[commandSize++] = c; else commandOverflow = true;
    }
  }
}
void logFrame() {
  if (!streaming || !Serial) return;
  const coach::Angles& a = vision.value;
  Serial.print("F,1,"); Serial.print(vision.frameMs); Serial.print(','); Serial.print(a.side);
  for (float v : {a.elbow, a.knee, a.hip, a.torso}) { Serial.print(','); if (coach::finite(v)) Serial.print(v, 3); else Serial.print("nan"); }
  Serial.println();
}
void readPose() {
  const bool needsAll = streaming || choice == 3;
  frameValid = vision.read(exercise, needsAll, mode == Mode::Running || streaming || choice == 3);
  const uint32_t now = vision.frameMs;
  const uint32_t dt = now - lastVision;
  if (lastVision && dt) fps = fps == 0 ? 1000.0f / dt : 0.8f * fps + 0.2f * 1000.0f / dt;
  lastVision = now;
  if (vision.subjectChanged) { counter.loseTracking(); classifier.reset(); notice = "Reacquired: hold start"; }
  logFrame();
  classifier.push(now, vision.value);  // live classification in every mode and menu choice
  if (mode == Mode::AutoWait && classifier.ready && classifier.detected != Exercise::Rest) {
    startSet(classifier.detected); notice = "Identified; hold start"; return;
  }
  if (mode == Mode::Running) {
    coach::RepEvent event = counter.update(vision.value.primary(exercise), vision.value.torso,
                                         vision.value.validFor(exercise), now);
    if (event.completed || event.rejected) {
      feedbackFaults = event.faults; flashUntil = now + 1800; notice = correction(event.faults);
      outputs.beep(event.valid ? 1 : 2); outputs.reps(counter.good, goal);
      if (Serial) {
        Serial.print("# rep valid="); Serial.print(event.valid); Serial.print(" faults="); Serial.print(event.faults);
        Serial.print(" duration_ms="); Serial.println(event.durationMs);
      }
      if (counter.good >= goal) finishSet();
    }
  }
}
void drawSkeleton() {
  lcd.fillRect(0, 43, 145, 159, TFT_BLACK);
  float xmin = 1e9, ymin = 1e9, xmax = -1e9, ymax = -1e9;
  int n = 0;
  for (const auto& p : vision.pose.p) if (coach::usable(p, settings::MIN_SCORE)) {
    xmin = min(xmin, p.x); ymin = min(ymin, p.y); xmax = max(xmax, p.x); ymax = max(ymax, p.y); ++n;
  }
  if (n < 3 || xmax - xmin < 1 || ymax - ymin < 1) {
    lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK); lcd.drawString("No clear pose", 12, 104, 2); return;
  }
  float scale = min(125.0f / (xmax - xmin), 139.0f / (ymax - ymin));
  int x[17], y[17];
  for (int i = 0; i < 17; ++i) { x[i] = 10 + (vision.pose.p[i].x - xmin) * scale; y[i] = 52 + (vision.pose.p[i].y - ymin) * scale; }
  const uint8_t bones[][2] = {{5,7},{7,9},{6,8},{8,10},{5,6},{5,11},{6,12},{11,12},{11,13},{13,15},{12,14},{14,16}};
  for (const auto& b : bones) if (coach::usable(vision.pose.p[b[0]]) && coach::usable(vision.pose.p[b[1]])) lcd.drawLine(x[b[0]], y[b[0]], x[b[1]], y[b[1]], TFT_CYAN);
  for (int i = 5; i < 17; ++i) if (coach::usable(vision.pose.p[i])) lcd.fillCircle(x[i], y[i], 3, i % 2 != vision.value.side ? TFT_GREEN : TFT_YELLOW);
}
void drawUI(uint32_t now) {
  lcd.fillRect(0, 0, 320, 43, 0x0925);
  lcd.setTextColor(TFT_WHITE, 0x0925); lcd.drawString("GYM COACH IA", 8, 3, 2);
  char line[80]; if (*classifier.label) snprintf(line, sizeof(line), "%.1f FPS | NOW: %s %.0f%%", fps, classifier.label, classifier.confidence * 100);
  else snprintf(line, sizeof(line), "%s | %.1f FPS | Q:%u", streaming ? "REC" : "POSE", fps, uplink.queued());
  lcd.drawString(line, 8, 23, 2);
  drawSkeleton(); lcd.fillRect(146, 43, 174, 159, TFT_BLACK);
  const bool editing = mode == Mode::Idle || mode == Mode::Done;
  for (int row = 0; row < 4; ++row) {
    lcd.setTextColor(editing && field == row ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
    if (row == 0) snprintf(line, sizeof(line), "%s", choice == 3 && (mode == Mode::Idle || mode == Mode::AutoWait) ? "AUTO (EI)" : title(exercise));
    if (row == 1) snprintf(line, sizeof(line), "Valid %u / %u", counter.good, goal);
    if (row == 2) snprintf(line, sizeof(line), "Weight %.1f kg", weightKg);
    if (row == 3) snprintf(line, sizeof(line), "Side %s", sideChoice < 0 ? "AUTO" : sideChoice == 0 ? "LEFT" : "RIGHT");
    lcd.drawString(line, 150, 49 + row * 22, 2);
  }
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  const float a = vision.value.primary(exercise);
  if (coach::finite(a)) snprintf(line, sizeof(line), "Angle %.0f deg", a); else snprintf(line, sizeof(line), "Angle --");
  lcd.drawString(line, 150, 141, 2);
  snprintf(line, sizeof(line), "Errors %u | %lu s", counter.bad, static_cast<unsigned long>(setMs / 1000));
  lcd.drawString(line, 150, 163, 2);
  lcd.drawString(mode == Mode::Running ? "RUNNING" : mode == Mode::Paused ? "PAUSED" : mode == Mode::AutoWait ? "IDENTIFYING" : "READY", 150, 184, 2);
  const bool flashing = flashUntil && static_cast<int32_t>(flashUntil - now) > 0;
  const char* message = notice;
  if (!vision.available) message = "Check Vision AI / left Grove";
  else if (!frameValid && (mode == Mode::Running || mode == Mode::AutoWait)) message = "Show joints; hold still to start";
  else if (mode == Mode::Running && !flashing) message = counter.phase() == coach::Phase::SeekRest ? "Hold starting position" : "Complete the full movement";
  if (summaryPending) message = uplink.full ? "Queue full: keep boards powered" : "Saving set...";
  lcd.fillRect(0, 204, 320, 36, 0x0925); lcd.setTextColor(flashing && feedbackFaults ? TFT_ORANGE : TFT_WHITE, 0x0925);
  lcd.drawString(message, 5, 204, 2);
  lcd.setTextColor(TFT_LIGHTGREY, 0x0925); lcd.drawString("Stick:start/end  A:end B:pause C:clear", 5, 226, 1);
}
void setup() {
  Serial.begin(settings::BAUD); // never wait for USB; the demo works without a laptop
  lcd.init(); lcd.setRotation(3); lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE); lcd.drawString("Starting Gym Coach IA...", 8, 80, 2);
  for (Key* k : {&up,&down,&left,&right,&center,&keyA,&keyB,&keyC}) k->begin();
  outputs.begin(); uplink.begin();
#if !COACH_SELF_TEST
  vision.begin();
#endif
  lastLoop = millis(); Serial.println("# GymCoachIA v1; type STATUS or STREAM 1");
}
void loop() {
  uint32_t now = millis();
  if (mode == Mode::Running) setMs += now - lastLoop;
  lastLoop = now;
  outputs.tick(now); uplink.tick(now);
#if COACH_SELF_TEST
  const uint8_t phase = (now / 2000) % 4;
  outputs.color(phase == 0 ? Outputs::Red : phase == 1 ? Outputs::Green : phase == 2 ? Outputs::Blue : Outputs::Off);
  outputs.depth(phase / 3.0f); outputs.reps(phase, 3);
  if (now - lastUI > 2000) { lastUI = now; outputs.beep(phase == 3 ? 3 : 1); lcd.fillScreen(TFT_BLACK); lcd.drawString("OUTPUT SELF TEST", 12, 80, 4); }
#else
  readCommands(); handleKeys(now);
  if (summaryPending && uplink.enqueue(exercise, counter.good, counter.bad, goal, weightKg, setMs, counter.good >= goal)) summaryPending = false;
  if (now - lastVision >= settings::VISION_PERIOD_MS) readPose();
  now = millis();
  const bool flash = flashUntil && static_cast<int32_t>(flashUntil - now) > 0;
  outputs.color(flash ? (feedbackFaults ? Outputs::Red : Outputs::Green) : (mode == Mode::Running && frameValid && counter.phase() != coach::Phase::SeekRest ? Outputs::Green : Outputs::Blue));
  outputs.depth(mode == Mode::Running && frameValid ? counter.progress() : 0);
  if (now - lastUI >= settings::UI_PERIOD_MS) { lastUI = now; drawUI(now); }
#endif
}
