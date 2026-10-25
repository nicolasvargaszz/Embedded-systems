/*
 * rgb_checkpoint - random pure red/green/blue every second
 *
 * Checkpoint: once a second, pick ONE of red/green/blue at random (no
 * mixed/blended colors), drive it out to an RGB LED, and print the R/G/B
 * values to the Serial Monitor (the computer's console) -- NOT the Wio
 * Terminal's own LCD.
 *
 * Wiring on a breadboard (course kit is common cathode, but this sketch
 * also supports the common ANODE LED some of us ended up buying --
 * flip COMMON_ANODE below to match what you actually have):
 *   R leg -> 220ohm resistor -> D8  (header pin 37, PWM4)
 *   G leg -> 220ohm resistor -> D2  (header pin 16, PWM1)
 *   B leg -> 220ohm resistor -> D0  (header pin 13, PWM0)
 *   common (longest) leg           -> GND (header pin 39) if common cathode
 *                                   -> 3V3 (header pin 17) if common anode
 * Double-check pin numbers against componentes.jpeg before wiring.
 *
 * Boards manager : Seeed SAMD Boards -> board "Seeeduino Wio Terminal"
 */

const int PIN_R = D8;
const int PIN_G = D2;
const int PIN_B = D0;

// true  = common ANODE  (common leg -> 3V3, pins sink current, PWM inverted)
// false = common CATHODE (common leg -> GND, pins source current, PWM normal)
const bool COMMON_ANODE = true;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);

  randomSeed(analogRead(WIO_LIGHT));  // ambient light noise as a seed
}

void loop() {
  int channel = random(0, 3);  // 0 = red, 1 = green, 2 = blue
  int r = (channel == 0) ? 255 : 0;
  int g = (channel == 1) ? 255 : 0;
  int b = (channel == 2) ? 255 : 0;

  if (COMMON_ANODE) {
    // common anode sinks current through the pin: LOW = brighter
    analogWrite(PIN_R, 255 - r);
    analogWrite(PIN_G, 255 - g);
    analogWrite(PIN_B, 255 - b);
  } else {
    analogWrite(PIN_R, r);
    analogWrite(PIN_G, g);
    analogWrite(PIN_B, b);
  }

  Serial.print("R: ");
  Serial.print(r);
  Serial.print("  G: ");
  Serial.print(g);
  Serial.print("  B: ");
  Serial.println(b);

  delay(1000);
}
