// Checkpoint Oct 6 - microfono + LCD + servo como medidor (gauge)
// Basado en https://wiki.seeedstudio.com/Wio-Terminal-Mic/ (ejemplo con line chart)
// - El servo (con el brazo/horn) funciona como aguja: 0 grados = silencio, 180 = maximo.
// - El LCD muestra la lectura de sonido y un grafico de lineas.
// - Si el sonido se mantiene sobre THRESHOLD durante 3 s -> WARNING en el LCD.
//
// Librerias: Servo (con los archivos SAMD51 de Adafruit) + "Seeed_Arduino_Linechart"
// Cableado del servo: naranja -> pin 13 (D0 / PWM0), rojo -> pin 2 (5V), marron -> pin 39 (GND)
// Microfono: integrado en el Wio, sin cables.
// Calibrar THRESHOLD y MAX_LEVEL mirando Tools > Serial Plotter (115200).

#include <Servo.h>
#include "seeed_line_chart.h"

TFT_eSPI tft;
TFT_eSprite spr = TFT_eSprite(&tft);
Servo gauge;
doubles data;

const int SERVO_PIN = D0;            // PWM0 = pin fisico 13 del header
const int THRESHOLD = 400;           // nivel "maximo" que dispara el WARNING
const int MAX_LEVEL = 600;           // nivel que lleva la aguja a 180 grados
const unsigned long HOLD_MS = 3000;  // tiempo sostenido para el WARNING
const int MAX_POINTS = 50;           // puntos visibles en el grafico

unsigned long loudStart = 0;  // millis() en que empezo el ruido fuerte, 0 = no hay ruido fuerte
float smoothLevel = 0;        // nivel suavizado para que la aguja no tiemble

// Amplitud pico a pico durante ~20 ms (mas estable que una sola lectura)
int readLevel() {
  int lo = 1023, hi = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 20) {
    int v = analogRead(WIO_MIC);
    lo = min(lo, v);
    hi = max(hi, v);
  }
  return hi - lo;
}

void setup() {
  Serial.begin(115200);
  pinMode(WIO_MIC, INPUT);
  gauge.attach(SERVO_PIN);

  tft.begin();
  tft.setRotation(3);
  spr.createSprite(TFT_HEIGHT, TFT_WIDTH);
}

void loop() {
  int level = readLevel();
  unsigned long now = millis();

  // Contar cuanto tiempo lleva el sonido sobre el umbral
  if (level >= THRESHOLD) {
    if (loudStart == 0) loudStart = now;
  } else {
    loudStart = 0;
  }
  unsigned long loudMs = loudStart ? now - loudStart : 0;
  bool warning = loudMs >= HOLD_MS;

  // Aguja del servo
  smoothLevel = 0.7 * smoothLevel + 0.3 * level;
  gauge.write(map(constrain((int)smoothLevel, 0, MAX_LEVEL), 0, MAX_LEVEL, 0, 180));

  // Datos del grafico
  if (data.size() == MAX_POINTS) data.pop();
  data.push(level);

  // Encabezado: lectura de sonido, contador o WARNING
  String title;
  if (warning) title = "!! WARNING !!";
  else if (loudStart) title = "Fuerte: " + String(loudMs / 1000.0, 1) + " s";
  else title = "Sonido: " + String(level);

  spr.fillSprite(TFT_DARKGREY);
  auto header = text(0, 0)
                    .value(title.c_str())
                    .align(center)
                    .valign(vcenter)
                    .width(spr.width())
                    .color(loudStart && !warning ? TFT_YELLOW : TFT_WHITE)
                    .thickness(2);
  header.height(header.font_height(&spr) * 2);
  if (warning) spr.fillRect(0, 0, spr.width(), header.height(), TFT_RED);
  header.draw(&spr);

  auto content = line_chart(20, header.height());
  content
      .height(spr.height() - header.height() * 1.5)
      .width(spr.width() - content.x() * 2)
      .based_on(0.0)
      .show_circle(false)
      .y_role_color(TFT_WHITE)
      .x_role_color(TFT_WHITE)
      .value(data)
      .max_size(MAX_POINTS)
      .color(warning ? TFT_RED : TFT_CYAN)
      .backgroud(TFT_DARKGREY)
      .draw(&spr);

  spr.setTextColor(TFT_WHITE);
  spr.drawString("Umbral: " + String(THRESHOLD), 5, spr.height() - 12);
  spr.pushSprite(0, 0);

  Serial.print("Nivel:");
  Serial.print(level);
  Serial.print(",Umbral:");
  Serial.println(THRESHOLD);
}
