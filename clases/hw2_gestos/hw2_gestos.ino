// HW #2: reconocimiento de gestos con el sensor de luz (Edge Impulse)
// Pasás la mano sobre el sensor de luz (está ATRÁS de la Wio) y el modelo entrenado en
// Edge Impulse reconoce piedra / papel / tijera:
//   - tijera  -> LED ROJO
//   - piedra  -> LED VERDE
//   - papel   -> melodía en el buzzer
// Extras:
//   - clase "idle" + umbral de confianza: sin mano no se dispara nada
//   - inferencia continua: ventana deslizante, clasifica cada 100 ms (el ejemplo de Seeed
//     junta 1 s, clasifica y recién ahí vuelve a medir, así que puede cortar un gesto al medio)
//   - LCD: gesto detectado, barras de confianza de cada clase y nivel de luz en vivo
//   - mini juego: la Wio juega piedra-papel-tijera contra vos y lleva el marcador
//
// Cableado (header de 40 pines):
//   pin 13 (D0) -> resistencia 220 ohm -> pata larga (+) del LED rojo
//   pin 15 (D1) -> resistencia 220 ohm -> pata larga (+) del LED verde
//   patas cortas (-) de los dos LEDs -> pin 9 (GND)
// Buzzer: el de la Wio (WIO_BUZZER), sin cables.
//
// El include depende del nombre del proyecto en Edge Impulse:
// proyecto "hw2_gestos_luz" -> librería "hw2_gestos_luz_inferencing".
// Etiquetas: rock, paper, scissors (mayúsculas no importan) y opcional idle.

#include <hw2_gestos_luz_inferencing.h>
#include <TFT_eSPI.h>
#include <strings.h>  // strcasecmp

TFT_eSPI tft = TFT_eSPI();

const int LED_ROJO = D0;
const int LED_VERDE = D1;
const float UMBRAL = 0.80;            // confianza mínima para aceptar un gesto
const unsigned long LED_MS = 1500;    // cuánto queda prendido el LED
const unsigned long PAUSA_MS = 1200;  // tiempo muerto entre gestos (la ventana dura 1 s)
const size_t PASO = 10;               // clasificar cada 10 muestras nuevas (100 ms a 100 Hz)
const size_t N = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;  // muestras por ventana (100 = 1 s a 100 Hz)
const unsigned long INTERVALO_US = (unsigned long)(EI_CLASSIFIER_INTERVAL_MS * 1000);

float ventana[N];          // buffer circular con las lecturas de luz
size_t pos = 0;            // próxima posición a escribir (= la muestra más vieja)
size_t llenas = 0, nuevas = 0;
unsigned long proximaMuestra = 0;  // en micros
unsigned long apagarLed = 0, ultimoGesto = 0;
int victorias = 0, derrotas = 0, empates = 0;

// Gestos: 0 piedra, 1 papel, 2 tijera (en ese orden cada uno le gana al anterior)
const char *NOMBRE[] = {"PIEDRA", "PAPEL", "TIJERA"};
const uint16_t COLOR[] = {TFT_GREEN, TFT_YELLOW, TFT_RED};

// Melodía del papel, tocada sin bloquear el loop (si no, se pierden muestras de luz)
const int NOTAS[] = {1047, 1319, 1568};  // DO MI SOL
int notaActual = -1;
unsigned long proximaNota = 0;

int gestoDe(const char *label) {
  // sin distinguir mayúsculas: en el proyecto quedaron "rock", "Paper" y "Scissors"
  if (strcasecmp(label, "rock") == 0) return 0;
  if (strcasecmp(label, "paper") == 0) return 1;
  if (strncasecmp(label, "scissor", 7) == 0) return 2;  // "scissor" o "scissors"
  return -1;                                          // idle
}

// Edge Impulse pide los datos de a pedazos: se los damos en orden, de la muestra más vieja a la más nueva
int leerVentana(size_t offset, size_t length, float *out) {
  for (size_t i = 0; i < length; i++) {
    out[i] = ventana[(pos + offset + i) % N];
  }
  return 0;
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_ROJO, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(WIO_BUZZER, OUTPUT);
  pinMode(WIO_LIGHT, INPUT);

  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("HW2 - gestos con luz", 5, 5);
  tft.setTextSize(1);
  tft.drawString("pasa la mano sobre el sensor de atras", 5, 26);
  dibujarMarcador();

  randomSeed(analogRead(WIO_LIGHT) * 31 + micros());
  proximaMuestra = micros();
}

void loop() {
  unsigned long ahora = micros();
  if ((long)(ahora - proximaMuestra) >= 0) {
    // misma frecuencia que el modelo; si el loop se atrasó (dibujando), no recupera muestras perdidas
    proximaMuestra = ((long)(ahora - proximaMuestra) > (long)INTERVALO_US) ? ahora + INTERVALO_US
                                                                          : proximaMuestra + INTERVALO_US;
    ventana[pos] = analogRead(WIO_LIGHT);
    pos = (pos + 1) % N;
    if (llenas < N) llenas++;
    nuevas++;
  }

  if (llenas == N && nuevas >= PASO) {
    nuevas = 0;
    clasificar();
  }

  actualizarSalidas();
}

void clasificar() {
  signal_t signal;
  signal.total_length = N;
  signal.get_data = &leerVentana;

  ei_impulse_result_t result = { 0 };
  if (run_classifier(&signal, &result, false) != EI_IMPULSE_OK) {
    Serial.println("ERROR al clasificar");
    return;
  }

  size_t mejor = 0;
  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (result.classification[i].value > result.classification[mejor].value) mejor = i;
    Serial.print(result.classification[i].label);
    Serial.print(": ");
    Serial.print(result.classification[i].value, 2);
    Serial.print("  ");
  }
  Serial.println();
  dibujarBarras(result);

  int g = gestoDe(result.classification[mejor].label);
  if (g >= 0 && result.classification[mejor].value >= UMBRAL && millis() - ultimoGesto > PAUSA_MS) {
    ultimoGesto = millis();
    reaccionar(g);
  }
}

void reaccionar(int g) {
  // Lo que pide la tarea
  digitalWrite(LED_ROJO, g == 2 ? HIGH : LOW);   // tijera -> rojo
  digitalWrite(LED_VERDE, g == 0 ? HIGH : LOW);  // piedra -> verde
  if (g != 1) apagarLed = millis() + LED_MS;
  if (g == 1) {                                  // papel -> sonido
    notaActual = 0;
    proximaNota = millis();
  }

  // Extra: la Wio juega contra vos
  int wio = random(3);
  int r = (g - wio + 3) % 3;  // 0 empate, 1 ganaste, 2 ganó la Wio
  if (r == 0) empates++;
  else if (r == 1) victorias++;
  else derrotas++;

  tft.fillRect(0, 40, 320, 40, TFT_BLACK);
  tft.setTextSize(4);
  tft.setTextColor(COLOR[g], TFT_BLACK);
  tft.drawString(NOMBRE[g], 5, 44);

  tft.fillRect(0, 196, 320, 18, TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(String("Wio: ") + NOMBRE[wio], 5, 196);
  tft.setTextColor(r == 1 ? TFT_GREEN : (r == 2 ? TFT_RED : TFT_LIGHTGREY), TFT_BLACK);
  tft.drawString(r == 1 ? "GANASTE" : (r == 2 ? "PERDISTE" : "EMPATE"), 200, 196);
  dibujarMarcador();
}

void actualizarSalidas() {
  if (apagarLed && millis() > apagarLed) {
    digitalWrite(LED_ROJO, LOW);
    digitalWrite(LED_VERDE, LOW);
    apagarLed = 0;
  }
  if (notaActual >= 0 && millis() >= proximaNota) {
    if (notaActual < 3) {
      tone(WIO_BUZZER, NOTAS[notaActual], 120);
      proximaNota = millis() + 150;
      notaActual++;
    } else {
      notaActual = -1;
    }
  }
}

// Una barra por clase con su confianza, más el nivel de luz actual
void dibujarBarras(const ei_impulse_result_t &result) {
  tft.setTextSize(2);
  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT && i < 4; i++) {
    int y = 90 + i * 22;
    float v = result.classification[i].value;
    int g = gestoDe(result.classification[i].label);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(result.classification[i].label, 5, y);
    tft.fillRect(120, y, (int)(v * 150), 16, g >= 0 ? COLOR[g] : TFT_DARKGREY);
    tft.fillRect(120 + (int)(v * 150), y, 150 - (int)(v * 150), 16, TFT_BLACK);
    tft.drawString(String((int)(v * 100)) + "%", 275, y);
  }
  int luz = ventana[(pos + N - 1) % N];
  tft.setTextSize(1);
  tft.drawString("luz", 5, 182);
  tft.fillRect(30, 180, luz * 270 / 1023, 10, TFT_CYAN);
  tft.fillRect(30 + luz * 270 / 1023, 180, 270 - luz * 270 / 1023, 10, TFT_BLACK);
}

void dibujarMarcador() {
  tft.fillRect(0, 220, 320, 20, TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(String("Vos ") + victorias + "  Wio " + derrotas + "  Empates " + empates, 5, 222);
}
