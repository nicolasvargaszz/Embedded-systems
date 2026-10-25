#pragma once
#include <Wire.h>
#include <Seeed_Arduino_SSCMA.h>
#include "Settings.h"
#include "CoachCore.h"

class Vision {
  SSCMA ai_;
  int lockedSide_ = -1;
  float lastX_ = 0, lastY_ = 0, lastW_ = 0, lastH_ = 0;
  bool tracked_ = false;
  uint32_t lastMatch_ = 0;
public:
  coach::Pose pose;
  coach::Angles value;
  bool available = false, subjectChanged = false;
  uint32_t frameMs = 0;
  int lastError = 0, requestedSide = -1;
  void begin() { available = ai_.begin(&Wire, -1, 0x62, 2, 400000); }
  void unlock() { lockedSide_ = -1; tracked_ = false; value = coach::Angles(); }
  bool read(coach::Exercise exercise, bool allJoints, bool lockSide) {
    subjectChanged = false;
    // Some library versions retain results when a JSON field is absent.
    ai_.keypoints().clear();
    lastError = ai_.invoke(1, true, false); // results only, no image transfer
    frameMs = millis();
    pose = coach::Pose(); value = coach::Angles();
    if (lastError != CMD_OK) { available = false; return false; }
    available = true;
    auto& people = ai_.keypoints();
    int chosen = -1; float largest = -1;
    const bool keepIdentity = tracked_ && frameMs - lastMatch_ < 1000;
    for (size_t i = 0; i < people.size(); ++i) {
      const auto& k = people[i];
      if (k.points.size() != 17 || k.box.score < settings::MIN_SCORE) continue;
      if (keepIdentity) {
        const float dx = float(k.box.x) - lastX_, dy = float(k.box.y) - lastY_;
        const float scale = lastW_ > lastH_ ? lastW_ : lastH_;
        if (scale < 1 || dx * dx + dy * dy > scale * scale * 0.36f) continue;
        float ratio = float(k.box.w) * k.box.h / (lastW_ * lastH_ + 1);
        if (ratio < 0.4f || ratio > 2.5f) continue;
      }
      const float area = float(k.box.w) * k.box.h;
      if (area > largest) { largest = area; chosen = static_cast<int>(i); }
    }
    if (chosen < 0) return false;
    if (tracked_ && !keepIdentity) { subjectChanged = true; lockedSide_ = -1; }
    const auto& k = people[chosen];
    lastX_ = k.box.x; lastY_ = k.box.y; lastW_ = k.box.w; lastH_ = k.box.h;
    lastMatch_ = frameMs; tracked_ = true;
    for (int i = 0; i < 17; ++i) {
      pose.p[i].x = k.points[i].x; pose.p[i].y = k.points[i].y; pose.p[i].score = k.points[i].score;
    }
    int side = requestedSide >= 0 ? requestedSide : lockedSide_;
    if (side < 0) side = coach::bestSide(pose, exercise, allJoints, settings::MIN_SCORE);
    if (side >= 0 && lockSide) lockedSide_ = side;
    value = coach::angles(pose, side, settings::MIN_SCORE);
    return allJoints ? value.allValid() : value.validFor(exercise);
  }
};
