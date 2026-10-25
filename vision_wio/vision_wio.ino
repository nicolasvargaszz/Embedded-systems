#include <Seeed_Arduino_SSCMA.h>
#include "TFT_eSPI.h"
#include <TJpg_Decoder.h>

SSCMA AI;
TFT_eSPI tft;
TFT_eSprite canvas = TFT_eSprite(&tft);

const int   CANVAS    = 240;
const float SRC_RES   = 240.0;  // la Vision AI manda imagen de 240 px (medido)
const int   MIN_SCORE = 50;
const int   PX        = 246;

// La Vision AI manda la foto como JPEG en base64 dentro de la respuesta JSON.
// Buffer de recepcion grande (por defecto la libreria usa 4 KB en el SAMD51 y la foto no entra).
const uint32_t RX_BUFFER = 32 * 1024;
uint8_t jpgBuf[12 * 1024];  // el JPEG medido pesa ~3 KB

bool drawToCanvas(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  canvas.pushImage(x, y, w, h, bitmap);
  return true;
}

// Decodifica base64 a binario; devuelve la cantidad de bytes escritos
size_t base64Decode(const String &in, uint8_t *out, size_t outMax) {
  uint32_t acc = 0;
  int bits = 0;
  size_t n = 0;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+') v = 62;
    else if (c == '/') v = 63;
    else continue;  // '=' y cualquier otro caracter
    acc = (acc << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (n >= outMax) return 0;
      out[n++] = (acc >> bits) & 0xFF;
    }
  }
  return n;
}

// Dibuja la foto en el canvas; devuelve el ancho original de la imagen (0 si no hay foto)
int drawCamera() {
  String img = AI.last_image();
  if (img.length() == 0) return 0;
  size_t len = base64Decode(img, jpgBuf, sizeof(jpgBuf));
  if (len == 0) return 0;

  uint16_t w = 0, h = 0;
  TJpgDec.getJpgSize(&w, &h, jpgBuf, len);
  uint8_t scale = 1;
  while (scale < 8 && max(w, h) / scale > CANVAS) scale *= 2;
  TJpgDec.setJpgScale(scale);
  TJpgDec.drawJpg(0, 0, jpgBuf, len);
  return w;
}

void panel(const char *label, String value, int y, uint8_t size) {
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(label, PX, y);
  tft.setTextSize(size);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(value + "   ", PX, y + 12);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Iniciando Vision AI...", 10, 110);

  if (!AI.begin()) {
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.drawString("No encuentro la", 10, 90);
    tft.drawString("Vision AI por I2C", 10, 120);
    while (true) delay(1000);
  }

  AI.set_rx_buffer(RX_BUFFER);
  TJpgDec.setCallback(drawToCanvas);

  canvas.setColorDepth(8);
  canvas.createSprite(CANVAS, CANVAS);
  tft.fillScreen(TFT_BLACK);
}

void loop() {
  unsigned long t0 = millis();

  if (AI.invoke(1, false, true) != 0) {
    tft.setTextSize(1);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("SIN RESPUESTA", PX, 222);
    delay(200);
    return;
  }

  canvas.fillSprite(TFT_BLACK);
  int imgW = drawCamera();
  canvas.drawRect(0, 0, CANVAS, CANVAS, TFT_DARKGREY);

  // Las cajas vienen en pixeles de la imagen original; si no hay foto se usa SRC_RES
  float k = CANVAS / (imgW > 0 ? (float)imgW : SRC_RES);
  int count = 0, best = 0;

  for (size_t i = 0; i < AI.boxes().size(); i++) {
    int score = AI.boxes()[i].score;
    if (score < MIN_SCORE) continue;
    count++;
    if (score > best) best = score;

    int w = AI.boxes()[i].w * k;
    int h = AI.boxes()[i].h * k;
    int x = AI.boxes()[i].x * k - w / 2;
    int y = AI.boxes()[i].y * k - h / 2;

    canvas.drawRect(x, y, w, h, TFT_GREEN);
    canvas.drawRect(x + 1, y + 1, w - 2, h - 2, TFT_GREEN);
    canvas.setTextColor(TFT_GREEN);
    canvas.drawString(String(score) + "%", max(x + 4, 2), max(y + 4, 2));
  }
  canvas.pushSprite(0, 0);

  digitalWrite(LED_BUILTIN, count > 0 ? HIGH : LOW);

  unsigned long dt = millis() - t0;
  panel("PERSONAS",   String(count), 8, 3);
  panel("CONFIANZA",  String(best) + "%", 62, 2);
  panel("INFERENCIA", String(AI.perf().inference) + "ms", 112, 2);
  panel("FPS TOTAL",  String(dt > 0 ? 1000 / dt : 0), 162, 2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("OK           ", PX, 222);

  Serial.print("personas=");  Serial.print(count);
  Serial.print(" conf=");     Serial.print(best);
  Serial.print(" inf_ms=");   Serial.print(AI.perf().inference);
  Serial.print(" ciclo_ms="); Serial.print(dt);
  Serial.print(" img_w=");    Serial.println(imgW);
}
