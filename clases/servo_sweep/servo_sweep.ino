// Checkpoint Oct 6 - servo con la libreria Servo.h (ejemplo Sweep, pin 9 -> D0 = PWM0)
// Requiere los archivos de Adafruit con soporte SAMD51 en
// ~/Library/Arduino15/libraries/Servo/src/samd/ (Servo.cpp y ServoTimers.h)
// Cableado: senal -> pin 13 (D0 / PWM0), rojo -> pin 2 o 4 (5V), marron -> pin 39 (GND)

#include <Servo.h>

Servo myservo;

void setup() {
  myservo.attach(D0);  // PWM0 = pin fisico 13 del header
}
   
void loop() {
  for (int pos = 0; pos <= 180; pos++) {
    myservo.write(pos);
    delay(15);
  }
  for (int pos = 180; pos >= 0; pos--) {
    myservo.write(pos);
    delay(15);
  }
}
