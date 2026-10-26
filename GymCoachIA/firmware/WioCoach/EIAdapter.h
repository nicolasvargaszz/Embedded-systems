#pragma once
#include "Settings.h"
#include "CoachCore.h"
#include <cstring>

#if COACH_USE_EI
#include COACH_EI_HEADER
static_assert(EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE == 80, "Export 20 samples x 4 axes (2 s at 10 Hz)");
static_assert(EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME == 4, "Axis order: codo, rodilla, cadera, torso");
static_assert(EI_CLASSIFIER_INTERVAL_MS == 100, "Export a model sampled at 10 Hz");
static_assert(EI_CLASSIFIER_LABEL_COUNT == 4, "Required labels: curl, sentadilla, extension, descanso");
#endif

class EIAdapter {
  coach::Resampler sampler_;
  uint32_t lastGrid_ = 0;
  bool haveGrid_ = false;
  float window_[80] = {};
  size_t count_ = 0, hop_ = 0;
  coach::Exercise candidate_ = coach::Exercise::Rest;
  uint8_t confirmations_ = 0;
#if COACH_USE_EI
  static float*& signalBuffer() { static float* p = nullptr; return p; }
  static int getData(size_t offset, size_t length, float* out) {
    if (offset + length > 80) return -1;
    std::memcpy(out, signalBuffer() + offset, length * sizeof(float)); return 0;
  }
  void infer() {
    ei::signal_t signal; signal.total_length = 80; signal.get_data = getData;
    signalBuffer() = window_; ei_impulse_result_t result = {};
    if (run_classifier(&signal, &result, false) != EI_IMPULSE_OK) { clearVote(); error = true; return; }
    error = false;
    float best = 0; const char* name = "";
    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; ++i)
      if (result.classification[i].value > best) { best = result.classification[i].value; name = result.classification[i].label; }
    confidence = best; label = name;
    if (Serial) {
      Serial.print("# ei");
      for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; ++i) {
        Serial.print(' '); Serial.print(result.classification[i].label); Serial.print('='); Serial.print(result.classification[i].value, 2);
      }
      Serial.println();
    }
    coach::Exercise prediction;
    if (!std::strcmp(name, "curl")) prediction = coach::Exercise::Curl;
    else if (!std::strcmp(name, "sentadilla")) prediction = coach::Exercise::Squat;
    else if (!std::strcmp(name, "extension")) prediction = coach::Exercise::Extension;
    else if (!std::strcmp(name, "descanso")) prediction = coach::Exercise::Rest;
    else { clearVote(); error = true; return; }
    if (best < settings::EI_MIN_CONFIDENCE) { clearVote(); return; }
    if (candidate_ != prediction) { candidate_ = prediction; confirmations_ = 0; }
    if (confirmations_ < settings::EI_CONFIRM_WINDOWS) ++confirmations_;
    ready = confirmations_ >= settings::EI_CONFIRM_WINDOWS; detected = prediction;
  }
#endif
  void clearVote() { ready = false; confirmations_ = 0; }
  void sample(uint32_t t, const float* v) {
    if (haveGrid_ && t - lastGrid_ != 100) { count_ = hop_ = 0; clearVote(); }
    haveGrid_ = true; lastGrid_ = t;
    if (count_ == 20) { std::memmove(window_, window_ + 4, 76 * sizeof(float)); count_ = 19; }
    std::memcpy(window_ + 4 * count_, v, 4 * sizeof(float)); ++count_; ++hop_;
#if COACH_USE_EI
    if (count_ == 20 && hop_ >= 2) { hop_ = 0; infer(); }  // a decision every 200 ms
#endif
  }
public:
  bool ready = false, error = false;
  float confidence = 0;
  const char* label = "";  // latest prediction, shown live on the LCD
  coach::Exercise detected = coach::Exercise::Rest;
  void reset() { sampler_.reset(); count_ = hop_ = 0; haveGrid_ = false; confidence = 0; label = ""; clearVote(); }
  void push(uint32_t t, const coach::Angles& a) {
#if COACH_USE_EI
    // Skip frames with a missing joint instead of dropping the window: the resampler
    // interpolates across them, and a loss longer than RESAMPLE_MAX_GAP_MS still resets.
    if (!a.allValid()) return;
    sampler_.push(t, a, [this](uint32_t ts, const float* v) { sample(ts, v); });
#else
    (void)t; (void)a;
#endif
  }
};
