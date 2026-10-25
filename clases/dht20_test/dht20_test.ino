// DHT20 (Grove, I2C) -> LCD del Wio Terminal + Serial Monitor/Plotter
// Conexion A: cable Grove al puerto IZQUIERDO del Wio (el de I2C), mirando la pantalla de frente.
// Conexion B (protoboard): VCC->pin 1 (3V3), SDA->pin 3, SCL->pin 5, GND->pin 6 del header de 40 pines.
// Libreria: "Grove Temperature And Humidity Sensor" (Seeed) -> provee DHT.h

#include <Wire.h>
#include "DHT.h"
#include <TFT_eSPI.h>

DHT dht(DHT20);
TFT_eSPI tft;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  dht.begin();

  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("DHT20", 120, 10);
}

void loop() {
  float data[2];  // data[0] = humedad (%), data[1] = temperatura (C)

  if (dht.readTempAndHumidity(data) != 0) {
    Serial.println("Error leyendo el DHT20 - revisa el cable Grove (puerto izquierdo)");
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("Sensor no encontrado ", 20, 100);
    delay(1000);
    return;
  }

  float hum = data[0];
  float temp = data[1];

  // Formato "nombre:valor" para que el Serial Plotter dibuje las dos curvas
  Serial.print("Temperatura:");
  Serial.print(temp);
  Serial.print(",Humedad:");
  Serial.println(hum);

  tft.setTextSize(3);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("Temp: " + String(temp, 1) + " C  ", 20, 80);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Hum:  " + String(hum, 1) + " %  ", 20, 140);

  delay(1000);
}
