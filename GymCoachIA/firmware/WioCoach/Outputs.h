#pragma once
#include <Arduino.h>
#include <TM1637Display.h>
#include "Settings.h"
#if COACH_USE_SERVO
#include <Servo.h>
#ifndef GYM_COACH_SERVO_TC1
#error "Run tools/install_arduino.py: the supplied SAMD51 Servo patch reserves TC1 and leaves TC0 for tone()."
#endif
#endif

class Outputs {
  TM1637Display digits_{settings::DISPLAY_CLK, settings::DISPLAY_DIO};
#if COACH_USE_SERVO
  Servo needle_;
#endif
  int position_ = 0, target_ = 0, shown_ = -1;
  uint32_t lastServo_ = 0, nextTone_ = 0;
  uint8_t sound_ = 0, step_ = 0;
public:
  enum Color { Blue, Green, Red, Off };
  void color(Color c) {
    // Common anode: LOW turns a channel on. No PWM timers are needed here.
    digitalWrite(settings::RGB_R, c == Red ? LOW : HIGH);
    digitalWrite(settings::RGB_G, c == Green ? LOW : HIGH);
    digitalWrite(settings::RGB_B, c == Blue ? LOW : HIGH);
  }
  void depth(float p) {
    digitalWrite(settings::DEPTH_R, p >= 0.15f);
    digitalWrite(settings::DEPTH_Y, p >= 0.55f);
    digitalWrite(settings::DEPTH_G, p >= 0.90f);
  }
  void begin() {
    for (uint8_t pin : {settings::RGB_R, settings::RGB_G, settings::RGB_B,
                        settings::DEPTH_R, settings::DEPTH_Y, settings::DEPTH_G}) pinMode(pin, OUTPUT);
    color(Blue); depth(0); digits_.setBrightness(4); reps(0, 10);
    pinMode(WIO_BUZZER, OUTPUT);
#if COACH_USE_SERVO
    needle_.attach(settings::SERVO_PIN, settings::SERVO_MIN_US, settings::SERVO_MAX_US);
    needle_.write(0);
#endif
  }
  void reps(uint16_t n, uint16_t goal) {
    if (n != shown_) { digits_.showNumberDec(n > 9999 ? 9999 : n, false); shown_ = n; }
    target_ = goal ? min(180, int(180UL * n / goal)) : 0;
  }
  void beep(uint8_t pattern) { sound_ = pattern; step_ = 0; nextTone_ = millis(); }
  void tick(uint32_t now) {
#if COACH_USE_SERVO
    if (now - lastServo_ >= 20 && position_ != target_) {
      // Catch up to elapsed time when camera inference delays this loop.
      int step = min(10, int((now - lastServo_) / 20));
      step = min(step, abs(target_ - position_));
      lastServo_ = now; position_ += position_ < target_ ? step : -step;
      needle_.write(position_);
    }
#endif
    if (!sound_ || static_cast<int32_t>(now - nextTone_) < 0) return;
    // Even steps are notes; odd steps are gaps. Pattern 1/2/3 = rep/error/end.
    const uint8_t notes = sound_ == 3 ? 3 : sound_;
    if (step_ >= notes * 2) { noTone(WIO_BUZZER); sound_ = 0; return; }
    if (step_ % 2 == 0) tone(WIO_BUZZER, sound_ == 3 ? 1100 + 400 * (step_ / 2) : (sound_ == 2 ? 700 : 1800), 90);
    else noTone(WIO_BUZZER);
    nextTone_ = now + 120; ++step_;
  }
};
