/*
 * servo_test - sweep test for an SG90 servo, wired directly (no breadboard)
 *
 * NOTE: the arduino-libraries Servo (1.2.2) only implements SAMD21 timer
 * registers in its "samd" backend -- it does not compile for the Wio
 * Terminal's SAMD51 chip (GCLK/TC register layout differs). So this sketch
 * bit-bangs the servo pulse by hand instead of using <Servo.h>.
 *
 * Wiring: plug the servo's 3-wire connector straight onto the Wio
 * Terminal's 40-pin header with male-male jumpers, one wire at a time
 * (per componentes.jpeg):
 *   Signal (orange/yellow) -> D0  (header pin 13, PWM0)
 *   VCC    (red)           -> 5V  (header pin 2 or pin 4)
 *   GND    (brown/black)   -> GND (header pin 39, or 6/9/14/20/25/30/34)
 * SG90 needs ~4.8-6V to move reliably -- use the 5V pin, not 3V3.
 *
 * Boards manager : Seeed SAMD Boards -> board "Seeeduino Wio Terminal"
 */

const int SERVO_PIN = D0;  // PWM0 = pin fisico 13 del header (NO es D13)

void writeServoAngle(int angle) {
  int pulseWidth = map(angle, 0, 180, 1000, 2000);  // microseconds
  digitalWrite(SERVO_PIN, HIGH);
  delayMicroseconds(pulseWidth);
  digitalWrite(SERVO_PIN, LOW);
  delay(20);  // ~50Hz refresh rate that hobby servos expect
}

void setup() {
  pinMode(SERVO_PIN, OUTPUT);
}

const int STEP = 10;  // bigger jump per pulse = faster sweep

void loop() {
  for (int pos = 0; pos <= 180; pos += STEP) {
    writeServoAngle(pos);
  }
  for (int pos = 180; pos >= 0; pos -= STEP) {
    writeServoAngle(pos);
  }
}
