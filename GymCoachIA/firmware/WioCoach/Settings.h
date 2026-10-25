#pragma once
#include <Arduino.h>

// Keep 0 until you have exported your own Edge Impulse Arduino library.
// tools/install_ei.py writes ei_model.h; tools/build.py wio --ei sets this to 1.
#ifndef COACH_USE_EI
#define COACH_USE_EI 0
#endif
#define COACH_EI_HEADER "ei_model.h"
#ifndef COACH_USE_SERVO
#define COACH_USE_SERVO 1
#endif
// Set 1 for an output-only hardware check. Restore 0 for the camera coach.
#ifndef COACH_SELF_TEST
#define COACH_SELF_TEST 0
#endif

namespace settings {
constexpr uint8_t RGB_R = D0, RGB_G = D1, RGB_B = D6;
constexpr uint8_t DEPTH_G = D2, DEPTH_Y = D5, DEPTH_R = D7;
constexpr uint8_t DISPLAY_CLK = D3, DISPLAY_DIO = D4, SERVO_PIN = D8;
constexpr uint8_t MIN_SCORE = 50;
constexpr uint32_t BAUD = 115200;
constexpr uint16_t VISION_PERIOD_MS = 80; // request ceiling; actual frame rate is measured
constexpr uint16_t UI_PERIOD_MS = 200;
constexpr int SERVO_MIN_US = 600, SERVO_MAX_US = 2300; // tune to your SG90; avoid hard stops
constexpr float EI_MIN_CONFIDENCE = 0.80f;
constexpr uint8_t EI_CONFIRM_WINDOWS = 3;
}
