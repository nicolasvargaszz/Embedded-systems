#include <TFT_eSPI.h> // Librería gráfica para el Wio Terminal

TFT_eSPI tft = TFT_eSPI(); // Crear un objeto TFT_eSPI para controlar la pantalla

// HW #1: pelota en una caja
// - círculo chico en el LCD, se mueve 5 px con el 5-way switch
// - no puede salir de la pantalla; al tocar un borde:
//   se pone roja + prende un LED real + suena el buzzer
// - extra: la pantalla está dividida en 8 zonas (4 columnas x 2 filas);
//   al entrar en una zona suena su nota: DO RE MI FA / SOL LA SI DO
//
// Cableado del LED (header de 40 pines):
//   pin 37 (D8) -> resistencia 220 ohm -> pata larga (+) del LED
//   pata corta (-) del LED -> pin 39 (GND)
//
// Cableado del buzzer externo:
//   pin 15 (D1) -> pata + del buzzer (la más larga)
//   pata - del buzzer -> GND (pin 39, compartido con el LED en la protoboard)

int xPos = 40;  // arranca en el centro de la zona DO
int yPos = 60;
int radius = 10;
int speed = 5; // píxeles por paso

// Pines del 5-way switch (constantes del core de Seeed)
const int PIN_LEFT  = WIO_5S_LEFT;
const int PIN_RIGHT = WIO_5S_RIGHT;
const int PIN_UP    = WIO_5S_UP;
const int PIN_DOWN  = WIO_5S_DOWN;

const int PIN_EDGE_LED = D8; // header pin 37 (PIN_LED ya existe en el core: es el LED interno)

const int PIN_BUZZER = D1; // header pin 15 (buzzer externo)

// 8 zonas: 4 columnas x 2 filas de 80x120 px
const int COLS = 4, ROWS = 2;
const int ZONE_W = 320 / COLS, ZONE_H = 240 / ROWS;

// Octava 7 (DO7..DO8): este buzzer resuena en ~2500 Hz, así que suena más fuerte acá
const char* NOTE_NAMES[8] = {"DO", "RE", "MI", "FA", "SOL", "LA", "SI", "DO'"};
const int   NOTE_FREQ[8]  = {2093, 2349, 2637, 2794, 3136, 3520, 3951, 4186};
const int   NOTE_MS       = 250;
const int   EDGE_FREQ     = 2500; // choque contra el borde: frecuencia de resonancia, el más fuerte
const int   EDGE_MS       = 80;

bool wasAtEdge = false; // para que suene solo al llegar al borde, no todo el tiempo
int lastZone = -1;      // para que la nota suene solo al cambiar de zona

int zoneOf(int x, int y){
    int col = min(x / ZONE_W, COLS - 1);
    int row = min(y / ZONE_H, ROWS - 1);
    return row * COLS + col;
}

// Líneas de la grilla y nombres de las notas; la zona actual va en amarillo
void drawGrid(int currentZone){
    for (int c = 1; c < COLS; c++) tft.drawFastVLine(c * ZONE_W, 0, 240, TFT_DARKGREY);
    for (int r = 1; r < ROWS; r++) tft.drawFastHLine(0, r * ZONE_H, 320, TFT_DARKGREY);
    for (int z = 0; z < COLS * ROWS; z++) {
        tft.setTextColor(z == currentZone ? TFT_YELLOW : TFT_DARKGREY, TFT_BLACK);
        tft.drawString(NOTE_NAMES[z], (z % COLS) * ZONE_W + 6, (z / COLS) * ZONE_H + 6, 2);
    }
}

void setup(){
    Serial.begin(9600);
    tft.init();
    tft.setRotation(3); // Configurar la orientación de la pantalla
    tft.fillScreen(TFT_BLACK); // Limpiar la pantalla una sola vez al arrancar

    pinMode(PIN_LEFT, INPUT_PULLUP);
    pinMode(PIN_RIGHT, INPUT_PULLUP);
    pinMode(PIN_UP, INPUT_PULLUP);
    pinMode(PIN_DOWN, INPUT_PULLUP);

    pinMode(PIN_EDGE_LED, OUTPUT);
    digitalWrite(PIN_EDGE_LED, LOW);
    pinMode(PIN_BUZZER, OUTPUT);

    lastZone = zoneOf(xPos, yPos);
    drawGrid(lastZone);
    tft.fillCircle(xPos, yPos, radius, TFT_BLUE);
}

void loop(){
    int oldX = xPos;
    int oldY = yPos;

    // Leer switches (activos LOW con INPUT_PULLUP)
    if (digitalRead(PIN_LEFT) == LOW)  xPos -= speed;
    if (digitalRead(PIN_RIGHT) == LOW) xPos += speed;
    if (digitalRead(PIN_UP) == LOW)    yPos -= speed;
    if (digitalRead(PIN_DOWN) == LOW)  yPos += speed;

    // Limitar posición para que no se salga de la pantalla
    int minX = radius, maxX = tft.width() - radius;
    int minY = radius, maxY = tft.height() - radius;
    xPos = constrain(xPos, minX, maxX);
    yPos = constrain(yPos, minY, maxY);

    // Detectar los cuatro bordes
    bool atLeft   = (xPos == minX);
    bool atRight  = (xPos == maxX);
    bool atTop    = (yPos == minY);
    bool atBottom = (yPos == maxY);
    bool atEdge = atLeft || atRight || atTop || atBottom;

    // LED prendido mientras la pelota toca un borde
    digitalWrite(PIN_EDGE_LED, atEdge ? HIGH : LOW);

    // Zona actual (0..7)
    int zone = zoneOf(xPos, yPos);

    // Sonido: el choque contra el borde tiene prioridad sobre la nota de la zona
    if (atEdge && !wasAtEdge) {
        tone(PIN_BUZZER, EDGE_FREQ, EDGE_MS);
        Serial.print("Borde: ");
        Serial.println(atLeft ? "izquierdo" : atRight ? "derecho" : atTop ? "arriba" : "abajo");
    } else if (zone != lastZone) {
        tone(PIN_BUZZER, NOTE_FREQ[zone], NOTE_MS);
        Serial.print("Zona: ");
        Serial.println(NOTE_NAMES[zone]);
    }

    // Redibujar si se movió o si cambió el estado de borde (cambia el color)
    if (xPos != oldX || yPos != oldY || atEdge != wasAtEdge) {
        tft.fillCircle(oldX, oldY, radius, TFT_BLACK);                     // borrar el viejo
        drawGrid(zone);                                                    // reparar lo que borró
        tft.fillCircle(xPos, yPos, radius, atEdge ? TFT_RED : TFT_BLUE);   // rojo en el borde
    }

    wasAtEdge = atEdge;
    lastZone = zone;
    delay(30); // para que no vuele cuando mantenés apretado
}
x