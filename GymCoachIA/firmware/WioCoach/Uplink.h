#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "CoachCore.h"

// Bounded stop-and-wait UART queue. ACK means durably stored by the XIAO,
// not confirmed delivery to Blynk. No network work happens on the Wio.
class Uplink {
  char pending_[8][320] = {};
  char ids_[8][48] = {};
  uint8_t head_ = 0, count_ = 0;
  uint32_t lastSend_ = 0, sequence_ = 0;
  char boot_[32] = {};
  char rx_[128] = {}; size_t used_ = 0; bool overflow_ = false;
  void receiveLine() {
    JsonDocument d;
    if (deserializeJson(d, rx_)) return;
    const char* ack = d["ack"] | "";
    if (count_ && !strcmp(ack, ids_[head_])) { head_ = (head_ + 1) % 8; --count_; lastSend_ = 0; }
  }
public:
  bool full = false;
  uint8_t queued() const { return count_; }
  void begin() {
    Serial1.begin(115200);
    // Randomized per boot using analog noise and startup timing; not a security ID.
    uint32_t a = micros();
    for (int i = 0; i < 32; ++i) a = (a << 1) ^ analogRead(WIO_LIGHT) ^ micros();
    snprintf(boot_, sizeof(boot_), "%08lx-%08lx", static_cast<unsigned long>(a), static_cast<unsigned long>(micros()));
  }
  bool enqueue(coach::Exercise e, uint16_t reps, uint16_t bad, uint16_t goal, float weight, uint32_t duration, bool complete) {
    if (count_ == 8) { full = true; return false; }
    const uint8_t slot = (head_ + count_) % 8;
    snprintf(ids_[slot], sizeof(ids_[slot]), "%s-%lu", boot_, static_cast<unsigned long>(++sequence_));
    JsonDocument d;
    d["v"] = 1; d["type"] = "set"; d["id"] = ids_[slot]; d["exercise"] = coach::label(e);
    d["reps"] = reps; d["bad"] = bad; d["goal"] = goal; d["kg"] = weight;
    d["duration_ms"] = duration; d["complete"] = complete;
    if (measureJson(d) >= sizeof(pending_[slot])) return false;
    serializeJson(d, pending_[slot], sizeof(pending_[slot]));
    ++count_; full = false; return true;
  }
  void tick(uint32_t now) {
    while (Serial1.available()) {
      const char c = Serial1.read();
      if (c == '\n') {
        if (!overflow_) { rx_[used_] = 0; receiveLine(); }
        used_ = 0; overflow_ = false;
      } else if (c != '\r' && !overflow_) {
        if (used_ + 1 < sizeof(rx_)) rx_[used_++] = c; else overflow_ = true;
      }
    }
    if (count_ && (lastSend_ == 0 || now - lastSend_ >= 2000)) {
      Serial1.println(pending_[head_]); lastSend_ = now;
    }
  }
};
