#include "secrets.h"
#define BLYNK_PRINT Serial
#include <Arduino.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32_SSL.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <cmath>
#include <time.h>

constexpr uint8_t QUEUE_SIZE = 12; // leave NVS room for copy-on-write and Wi-Fi settings
constexpr size_t PACKET_BYTES = 320, ID_BYTES = 48;
constexpr uint32_t STORE_MAGIC = 0x47434F31;
struct Store {
  uint32_t magic;
  uint8_t head, count, nextRecent, reserved;
  char queue[QUEUE_SIZE][PACKET_BYTES];
  char recent[QUEUE_SIZE][ID_BYTES];
};
Store state = {};
Preferences prefs;
bool storageReady = false, credentialsReady = false;
char input[384] = {};
size_t inputSize = 0;
bool overflow = false;
uint32_t lastWifi = 0, lastBlynk = 0, lastPublish = 0, lastClockNotice = 0;
uint8_t publishField = 0;
bool sentAll = false;

bool persist() { return prefs.putBytes("state", &state, sizeof(state)) == sizeof(state); }
bool validPacket(JsonDocument& d) {
  if (d["v"].as<int>() != 1 || strcmp(d["type"] | "", "set")) return false;
  const char* id = d["id"] | "";
  const char* e = d["exercise"] | "";
  if (!*id || strlen(id) >= ID_BYTES) return false;
  for (const char* p = id; *p; ++p) if (!isalnum(static_cast<unsigned char>(*p)) && *p != '-') return false;
  if (strcmp(e, "curl") && strcmp(e, "sentadilla") && strcmp(e, "extension")) return false;
  if (!d["reps"].is<int>() || !d["bad"].is<int>() || !d["goal"].is<int>() ||
      !d["kg"].is<float>() || !d["duration_ms"].is<uint32_t>() || !d["complete"].is<bool>()) return false;
  float kg = d["kg"].as<float>();
  return d["reps"].as<int>() >= 0 && d["reps"].as<int>() <= 9999 &&
         d["bad"].as<int>() >= 0 && d["bad"].as<int>() <= 9999 &&
         d["goal"].as<int>() >= 1 && d["goal"].as<int>() <= 99 &&
         std::isfinite(kg) && kg >= 0 && kg <= 250;
}
void reply(const char* key, const char* value) {
  JsonDocument d; d[key] = value; serializeJson(d, Serial1); Serial1.println();
}
bool alreadyStored(const char* id) {
  for (uint8_t i = 0; i < QUEUE_SIZE; ++i) if (!strcmp(id, state.recent[i])) return true;
  for (uint8_t i = 0; i < state.count; ++i) {
    JsonDocument d;
    if (!deserializeJson(d, state.queue[(state.head + i) % QUEUE_SIZE]) && !strcmp(id, d["id"] | "")) return true;
  }
  return false;
}
void receivePacket() {
  if (!storageReady) { reply("nack", "storage_error"); return; }
  JsonDocument d;
  if (deserializeJson(d, input) || !validPacket(d) || measureJson(d) >= PACKET_BYTES) {
    reply("nack", "invalid_packet"); return;
  }
  const char* id = d["id"];
  if (alreadyStored(id)) { reply("ack", id); return; }
  if (state.count == QUEUE_SIZE) { reply("nack", "queue_full"); return; }
  uint8_t slot = (state.head + state.count) % QUEUE_SIZE;
  serializeJson(d, state.queue[slot], PACKET_BYTES); ++state.count;
  if (!persist()) { --state.count; storageReady = false; reply("nack", "storage_error"); return; }
  reply("ack", id);
  Serial.printf("Stored set. Queue: %u\n", state.count);
}
void readUART() {
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {
      if (!overflow) { input[inputSize] = 0; receivePacket(); }
      else reply("nack", "line_too_long");
      inputSize = 0; overflow = false;
    } else if (c != '\r' && !overflow) {
      if (inputSize + 1 < sizeof(input)) input[inputSize++] = c; else overflow = true;
    }
  }
}
void loadStore() {
  if (!prefs.begin("gymcoach", false)) return;
  const size_t length = prefs.getBytesLength("state");
  if (length == 0) {
    memset(&state, 0, sizeof(state)); state.magic = STORE_MAGIC; storageReady = persist(); return;
  }
  if (length != sizeof(state) || prefs.getBytes("state", &state, sizeof(state)) != sizeof(state)) return;
  if (state.magic != STORE_MAGIC || state.head >= QUEUE_SIZE || state.count > QUEUE_SIZE || state.nextRecent >= QUEUE_SIZE) return;
  for (uint8_t i = 0; i < QUEUE_SIZE; ++i) {
    if (!memchr(state.queue[i], 0, PACKET_BYTES) || !memchr(state.recent[i], 0, ID_BYTES)) return;
  }
  for (uint8_t i = 0; i < state.count; ++i) {
    JsonDocument d;
    if (deserializeJson(d, state.queue[(state.head + i) % QUEUE_SIZE]) || !validPacket(d)) return;
  }
  storageReady = true;
}
void networkTick(uint32_t now) {
  if (!credentialsReady) return;
  if (WiFi.status() != WL_CONNECTED) {
    publishField = 0; sentAll = false;
    if (now - lastWifi >= 10000) { lastWifi = now; WiFi.begin(WIFI_SSID, WIFI_PASSWORD); }
    return;
  }
  // TLS certificate validity needs a real clock. SNTP runs asynchronously;
  // UART storage continues while time is unavailable.
  if (time(nullptr) < 1700000000) {
    if (now - lastClockNotice >= 15000) { lastClockNotice = now; Serial.println("Waiting for NTP clock before TLS connection"); }
    return;
  }
  if (!Blynk.connected()) {
    publishField = 0; sentAll = false;
    if (now - lastBlynk >= 5000) { lastBlynk = now; Blynk.connect(100); }
    return;
  }
  Blynk.run();
}
void publishTick(uint32_t now) {
  if (!storageReady || !state.count || !Blynk.connected() || now - lastPublish < 250) return;
  lastPublish = now;
  JsonDocument d;
  if (deserializeJson(d, state.queue[state.head])) { storageReady = false; return; }
  if (sentAll) {
    // No application-level cloud ACK exists for virtualWrite. A reconnect before
    // this point replays the same id; consumers should deduplicate by id.
    const char* id = d["id"];
    const uint8_t oldHead = state.head, oldCount = state.count, oldRecent = state.nextRecent;
    char previousId[ID_BYTES]; memcpy(previousId, state.recent[oldRecent], ID_BYTES);
    strlcpy(state.recent[oldRecent], id, ID_BYTES);
    state.nextRecent = (oldRecent + 1) % QUEUE_SIZE;
    state.head = (state.head + 1) % QUEUE_SIZE; --state.count;
    if (!persist()) {
      state.head = oldHead; state.count = oldCount; state.nextRecent = oldRecent;
      memcpy(state.recent[oldRecent], previousId, ID_BYTES); storageReady = false;
    }
    publishField = 0; sentAll = false; return;
  }
  switch (publishField) {
    case 0: Blynk.virtualWrite(V0, d["exercise"].as<const char*>()); break;
    case 1: Blynk.virtualWrite(V1, d["reps"].as<int>()); break;
    case 2: Blynk.virtualWrite(V2, d["kg"].as<float>()); break;
    case 3: Blynk.virtualWrite(V3, d["duration_ms"].as<uint32_t>() / 1000.0); break;
    case 4: Blynk.virtualWrite(V4, d["bad"].as<int>()); break;
    case 5: Blynk.virtualWrite(V5, d["goal"].as<int>()); break;
    case 6: Blynk.virtualWrite(V6, d["complete"].as<bool>() ? 1 : 0); break;
    case 7: Blynk.virtualWrite(V7, d["id"].as<const char*>()); break;
    case 8: Blynk.virtualWrite(V8, state.queue[state.head]); sentAll = true; break;
  }
  ++publishField;
}
void setup() {
  Serial.begin(115200);
  Serial1.setRxBufferSize(2048);
  Serial1.begin(115200, SERIAL_8N1, D7, D6); // RX = GPIO20, TX = GPIO21
  loadStore();
  credentialsReady = strlen(WIFI_SSID) > 0 && strlen(BLYNK_AUTH_TOKEN) > 0 && strcmp(BLYNK_TEMPLATE_ID, "TMPL_REPLACE_ME");
  if (credentialsReady) {
    WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
    Blynk.config(BLYNK_AUTH_TOKEN);
  }
  Serial.printf("Gym Coach bridge. Storage=%s; network=%s; queued=%u\n", storageReady ? "OK" : "ERROR", credentialsReady ? "configured" : "add secrets.h", state.count);
}
void loop() {
  readUART();
  networkTick(millis());
  readUART();
  publishTick(millis());
  delay(1);
}
