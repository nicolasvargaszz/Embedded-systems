#pragma once
// Platform-independent pose geometry and repetition logic. Angles are degrees.
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace coach {
constexpr float PI_F = 3.14159265358979323846f;
inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline bool finite(float x) { return std::isfinite(x); }
inline uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }
enum class Exercise : uint8_t { Curl, Squat, Extension, Rest };
inline const char* label(Exercise e) {
  switch (e) {
    case Exercise::Curl: return "curl";
    case Exercise::Squat: return "sentadilla";
    case Exercise::Extension: return "extension";
    default: return "descanso";
  }
}
struct Point { float x = 0, y = 0; uint8_t score = 0; };
struct Pose { Point p[17]; };
inline bool usable(const Point& p, uint8_t threshold = 50) {
  return p.score >= threshold && finite(p.x) && finite(p.y);
}
inline float angle(const Point& a, const Point& b, const Point& c, uint8_t threshold = 50) {
  if (!usable(a, threshold) || !usable(b, threshold) || !usable(c, threshold)) return NAN;
  const float ux = a.x - b.x, uy = a.y - b.y, vx = c.x - b.x, vy = c.y - b.y;
  const float n = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
  if (n < 1e-5f) return NAN;
  return std::acos(clampf((ux * vx + uy * vy) / n, -1, 1)) * 180 / PI_F;
}
struct Angles {
  float elbow = NAN, knee = NAN, hip = NAN, torso = NAN;
  int side = -1; // 0 = subject's left; 1 = subject's right
  bool allValid() const { return finite(elbow) && finite(knee) && finite(hip) && finite(torso); }
  float primary(Exercise e) const { return e == Exercise::Curl ? elbow : knee; }
  bool validFor(Exercise e) const {
    if (e == Exercise::Rest) return allValid();
    return finite(primary(e)) && (e != Exercise::Squat || finite(torso));
  }
};
inline Angles angles(const Pose& p, int side, uint8_t threshold = 50) {
  Angles a; a.side = side;
  if (side < 0 || side > 1) return a;
  const Point& s = p.p[5 + side]; const Point& e = p.p[7 + side];
  const Point& w = p.p[9 + side]; const Point& h = p.p[11 + side];
  const Point& k = p.p[13 + side]; const Point& t = p.p[15 + side];
  a.elbow = angle(s, e, w, threshold); a.knee = angle(h, k, t, threshold);
  a.hip = angle(s, h, k, threshold);
  if (usable(s, threshold) && usable(h, threshold)) {
    float dx = s.x - h.x, dy = h.y - s.y;
    if (dx * dx + dy * dy > 1e-5f) a.torso = std::atan2(std::fabs(dx), dy) * 180 / PI_F;
  }
  return a;
}
inline int bestSide(const Pose& p, Exercise e, bool requireAll, uint8_t threshold = 50) {
  float best = -1; int result = -1;
  for (int side = 0; side < 2; ++side) {
    Angles a = angles(p, side, threshold);
    if (requireAll ? !a.allValid() : !a.validFor(e)) continue;
    float score = 0; int n = 0;
    for (int i = 5 + side; i < 17; i += 2) {
      if (requireAll || e == Exercise::Rest ||
          (e == Exercise::Curl && i <= 9) ||
          (e != Exercise::Curl && (i >= 11 || (e == Exercise::Squat && i == 5)))) {
        score += p.p[i].score; ++n;
      }
    }
    if (n && score / n > best) { best = score / n; result = side; }
  }
  return result;
}
// Mean of the last frames. Two, not three: at ~3 fps three frames span over a second and shave
// the bottom off a rep (a curl to 42 deg read as 59, flagged as incomplete range).
constexpr unsigned SMOOTH_FRAMES = 2;
class Mean3 {
  float values_[SMOOTH_FRAMES] = {}; unsigned count_ = 0, next_ = 0;
public:
  void reset() { count_ = next_ = 0; }
  float push(float v) {
    if (!finite(v)) { reset(); return NAN; }
    values_[next_] = v; next_ = (next_ + 1) % SMOOTH_FRAMES;
    if (count_ < SMOOTH_FRAMES) ++count_;
    float sum = 0; for (unsigned i = 0; i < count_; ++i) sum += values_[i];
    return sum / count_;
  }
};
struct Profile {
  float startGate;       // broad initial resting position, permits diagnosing partial ROM
  float goodRest;        // valid return position
  float target;          // required far position
  float direction;      // +1 = extension; -1 = flexion
  float maxTorso = 45;
  // Lenient for a ~3 fps camera: it often misses the deepest and the resting frame, and timing is coarse.
  float depart = 10, reversal = 10, minimumTravel = 20;
  uint32_t minimumCycleMs = 400, minimumGoodMs = 500;
  uint32_t restHoldMs = 350, maximumCycleMs = 15000;
};
inline Profile profile(Exercise e) {
  Profile p;
  if (e == Exercise::Curl) { p.startGate = 110; p.goodRest = 120; p.target = 90; p.direction = -1; }
  else if (e == Exercise::Squat) { p.startGate = 140; p.goodRest = 145; p.target = 135; p.direction = -1; }
  else { p.startGate = 125; p.goodRest = 130; p.target = 135; p.direction = 1; }
  return p;
}
enum Fault : uint8_t { None = 0, Range = 1, RestRange = 2, Fast = 4, Torso = 8 };
struct RepEvent {
  bool completed = false, valid = false, rejected = false;
  uint8_t faults = None;
  float minimum = 180, maximum = 0, maxTorso = 0;
  uint32_t durationMs = 0, returnMs = 0;
};
enum class Phase : uint8_t { SeekRest, Ready, Outbound, Returning };
// Over Grove I2C frames arrive every 300-650 ms, so "tracking lost" must allow more than one
// frame gap (300 ms reset the counter on every frame and no rep was ever counted).
constexpr uint32_t TRACKING_GAP_MS = 1000;
constexpr float REST_TOLERANCE_DEG = 10;  // still enough to start; low fps makes angles jumpier

// A complete excursion and return is required. Tracking loss discards only the
// unfinished cycle. Thresholds are engineering defaults, not clinical judgments.
class RepCounter {
  Exercise exercise_ = Exercise::Curl;
  Profile p_ = profile(Exercise::Curl);
  Phase phase_ = Phase::SeekRest;
  Mean3 smooth_;
  float baseline_ = 0, peak_ = 0, back_ = 0, lo_ = 180, hi_ = 0, torso_ = 0;
  float rawPeak_ = 0;  // deepest raw (unsmoothed) frame: range is judged on it, one good frame is enough
  uint32_t start_ = 0, turn_ = 0, lastMotion_ = 0, lastSeen_ = 0;
  bool seen_ = false, resting_ = false;
  uint32_t restSince_ = 0;
  float restValue_ = 0;
  bool startOK(float a) const { return p_.direction < 0 ? a >= p_.startGate : a <= p_.startGate; }
  bool returnOK(float a) const { return p_.direction < 0 ? a >= p_.goodRest : a <= p_.goodRest; }
  void track(float a, float t) {
    if (a < lo_) lo_ = a;
    if (a > hi_) hi_ = a;
    if (finite(t) && t > torso_) torso_ = t;
  }
  void beginCycle(float a, float t, uint32_t now) {
    phase_ = Phase::Outbound; start_ = turn_ = lastMotion_ = now;
    peak_ = rawPeak_ = p_.direction * a; lo_ = hi_ = p_.direction * baseline_; torso_ = 0;
    track(a, t);
  }
  RepEvent finish(uint32_t now) {
    RepEvent e; e.durationMs = elapsed(now, start_); e.returnMs = elapsed(now, turn_);
    e.minimum = lo_; e.maximum = hi_; e.maxTorso = torso_;
    if (e.durationMs < p_.minimumCycleMs) { e.rejected = true; e.faults = Fast; return e; }
    e.completed = true;
    const float farAngle = p_.direction * std::fmax(peak_, rawPeak_), restAngle = p_.direction * back_;
    if ((p_.direction < 0 && farAngle > p_.target) || (p_.direction > 0 && farAngle < p_.target)) e.faults |= Range;
    if (!returnOK(restAngle) || !returnOK(p_.direction * baseline_)) e.faults |= RestRange;
    if (e.durationMs < p_.minimumGoodMs || (exercise_ == Exercise::Extension && e.returnMs < 200)) e.faults |= Fast;
    if (exercise_ == Exercise::Squat && torso_ > p_.maxTorso) e.faults |= Torso;
    e.valid = e.faults == None; return e;
  }
public:
  uint16_t attempts = 0, good = 0, bad = 0;
  void configure(Exercise e, Profile p) { exercise_ = e; p_ = p; reset(); }
  void reset() { attempts = good = bad = 0; loseTracking(); }
  void loseTracking() { phase_ = Phase::SeekRest; smooth_.reset(); seen_ = resting_ = false; current = NAN; }
  Phase phase() const { return phase_; }
  float current = NAN;
  const Profile& settings() const { return p_; }
  float progress() const {
    if (!finite(current)) return 0;
    return clampf((current - p_.goodRest) / (p_.target - p_.goodRest), 0, 1);
  }
  RepEvent update(float raw, float torso, bool valid, uint32_t now) {
    RepEvent event;
    if (!valid || !finite(raw)) {
      current = NAN;
      if (seen_ && elapsed(now, lastSeen_) > TRACKING_GAP_MS) loseTracking();
      return event;
    }
    if (seen_ && elapsed(now, lastSeen_) > TRACKING_GAP_MS) loseTracking();
    seen_ = true; lastSeen_ = now;
    current = smooth_.push(raw); const float q = p_.direction * current;
    if (phase_ == Phase::SeekRest) {
      if (!startOK(current)) { resting_ = false; return event; }
      if (!resting_ || std::fabs(current - restValue_) > REST_TOLERANCE_DEG) {
        resting_ = true; restSince_ = now; restValue_ = current;
      }
      if (elapsed(now, restSince_) >= p_.restHoldMs) { baseline_ = q; phase_ = Phase::Ready; }
      return event;
    }
    if (phase_ == Phase::Ready) {
      if (q < baseline_) baseline_ = q;
      if (q - baseline_ >= p_.depart) beginCycle(current, torso, now);
      return event;
    }
    if (elapsed(now, start_) > p_.maximumCycleMs) { loseTracking(); return event; }
    track(current, torso);
    if (p_.direction * raw > rawPeak_) rawPeak_ = p_.direction * raw;
    if (phase_ == Phase::Outbound) {
      if (q > peak_) { peak_ = q; turn_ = now; }
      if (peak_ - q >= p_.reversal && peak_ - baseline_ >= p_.minimumTravel) {
        phase_ = Phase::Returning; back_ = q; lastMotion_ = now;
      } else if (q <= baseline_ + 3 && peak_ - baseline_ < p_.minimumTravel) {
        phase_ = Phase::Ready; // noise or a tiny movement is not a repetition
      }
      return event;
    }
    if (q < back_ - 2) { back_ = q; lastMotion_ = now; }
    else if (q < back_) back_ = q;
    const bool newCycle = q - back_ >= p_.reversal;
    const bool partialRest = peak_ - back_ >= p_.minimumTravel && elapsed(now, lastMotion_) >= p_.restHoldMs;
    if (returnOK(current) || newCycle || partialRest) {
      event = finish(now);
      if (event.completed) { ++attempts; if (event.valid) ++good; else ++bad; }
      baseline_ = back_; phase_ = Phase::Ready;
      if (newCycle) beginCycle(current, torso, now);
    }
    return event;
  }
};

// A stream is sampled onto a fixed 100 ms grid. Both Python export and the EI
// adapter use linear interpolation of adjacent fresh frames, never stale holds.
// Over Grove I2C the Vision AI delivers ~3.3 fps (~300 ms/frame), so the gap
// limit must stay above that or the 2 s EI window never fills.
constexpr uint32_t RESAMPLE_MAX_GAP_MS = 1000;  // ~650 ms/frame with the camera view on
class Resampler {
  bool have_ = false;
  uint32_t previousTime_ = 0, nextTime_ = 0;
  float previous_[4] = {};
public:
  void reset() { have_ = false; }
  template<class Emit> void push(uint32_t now, const Angles& a, Emit emit) {
    if (!a.allValid()) { reset(); return; }
    const float v[4] = {a.elbow, a.knee, a.hip, a.torso};
    const uint32_t dt = now - previousTime_;
    if (!have_ || dt > RESAMPLE_MAX_GAP_MS) {
      have_ = true; previousTime_ = now; nextTime_ = now + 100;
      for (int i = 0; i < 4; ++i) previous_[i] = v[i];
      emit(now, v); return;
    }
    if (dt == 0) return;
    while (static_cast<int32_t>(now - nextTime_) >= 0) {
      float sample[4]; const float f = float(nextTime_ - previousTime_) / dt;
      for (int i = 0; i < 4; ++i) sample[i] = previous_[i] + f * (v[i] - previous_[i]);
      emit(nextTime_, sample); nextTime_ += 100;
    }
    previousTime_ = now; for (int i = 0; i < 4; ++i) previous_[i] = v[i];
  }
};
} // namespace coach
