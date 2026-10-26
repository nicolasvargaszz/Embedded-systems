# Gym Coach IA — referencia para el mid demo (27/10)

Borrador base para el documento breve y las slides. Ajustar con fotos y el diagrama de Fritzing.

## Documento breve (lo que pide el curso)

**Nombre:** Gym Coach IA

**Descripción (una oración):** Un entrenador de gimnasio embebido que usa una cámara con IA y un
Wio Terminal para reconocer qué ejercicio hacés, contar repeticiones y avisarte en tiempo real si
la técnica es incompleta.

**Funciones:**
- Reconoce 4 estados en vivo: curl de bíceps, sentadilla, extensión de pierna y descanso
  (modelo de Edge Impulse corriendo en el Wio, una decisión cada 200 ms).
- Cuenta repeticiones válidas y marca errores (recorrido incompleto, movimiento demasiado rápido,
  torso muy inclinado en la sentadilla).
- Feedback inmediato: RGB (azul = descanso, verde = ejercicio, rojo = error), barra de 3 LEDs con
  la profundidad del movimiento, buzzer (1 bip = rep válida, 2 = error, 3 notas = meta), y una voz
  en la laptop que anuncia el ejercicio en inglés.
- El peso se carga con el joystick del Wio.

**Entradas:** Grove Vision AI V2 + cámara OV5647 (pose de 17 puntos), joystick y botones del Wio.

**Salidas:** LCD del Wio (esqueleto, ejercicio detectado, reps), LED RGB, 3 LEDs, buzzer, voz por
USB en la laptop.

**Cómo funciona:**
1. El Vision AI corre YOLOv8n-pose y le pasa al Wio los 17 keypoints por I2C (Grove), ~3 fps.
2. El Wio calcula 4 ángulos: codo, rodilla, cadera y torso.
3. Una ventana de 2 s (10 Hz) entra al clasificador de Edge Impulse (Flatten + red densa 40/20).
4. Una máquina de estados con histéresis cuenta repeticiones según el ejercicio y aplica reglas
   de técnica.
5. Los resultados se muestran en el LCD y en el circuito; por USB salen a la laptop para la voz.

**Hardware:** Wio Terminal (obligatorio), Grove Vision AI V2 + OV5647, LED RGB de ánodo común,
3 LEDs (rojo, amarillo, verde), resistencias de 220 Ω, protoboard. El servo y el display de
7 segmentos se sacaron: el servo colgaba el Wio por consumo y el display no era el módulo de 4 pines.

**Conexiones (header de 40 pines):**

| Componente | Pin físico | Código |
|---|---|---|
| RGB rojo / verde / azul (220 Ω) | 13 / 15 / 33 | D0 / D1 / D6 |
| RGB común (ánodo) | 17 | 3V3 |
| LED verde / amarillo / rojo (220 Ω) | 16 / 32 / 36 | D2 / D5 / D7 |
| Cátodos de los LEDs | 9 | GND |
| Vision AI | Grove izquierdo | I2C 0x62 |

**Software:** Arduino IDE / arduino-cli, Edge Impulse, SenseCraft AI (para cargar el modelo de pose),
Python (grabación de datos y voz).

**Modelo y resultados:**
- Datos: 3 personas (nico, sebastian, mathias), series de 10 s a 10 Hz, de costado y de frente;
  182 muestras de entrenamiento, ~3.000 ventanas.
- Validación: 84 %, F1 0,84. Test: 86,6 %, F1 0,90 (sentadilla 0,99, descanso 0,87, curl 0,85,
  extensión 0,78).
- Prueba en el Wio con guion de voz: 86 % de aciertos en vivo.
- Proyecto Edge Impulse: `gym-coach-ia` (hacerlo público y poner el link).

**Referencias:** Guía del proyecto (`Guia_Gym_Coach_IA.pdf`), Seeed Wiki de Wio Terminal y Grove
Vision AI V2, Edge Impulse docs, YOLOv8-pose (Ultralytics / Himax), repo
<https://github.com/nicolasvargaszz/Embedded-systems>.

## Slides (8 min, letra grande, pocas decoraciones)

1. Título + equipo.
2. Problema: entrenar solo, sin saber si la técnica está bien.
3. Solución en una imagen: cámara → Vision AI → Wio → LEDs/buzzer/voz.
4. Hardware y diagrama de Fritzing.
5. Pipeline de IA: pose (17 puntos) → 4 ángulos → Edge Impulse → conteo y reglas.
6. Datos y resultados (tabla de F1 + matriz de confusión de Edge Impulse).
7. **Demo en vivo** (lo más importante, ~3 min).
8. Problemas resueltos: fps bajo por I2C, servo que colgaba el Wio, clasificación sentadilla vs
   extensión.
9. Próximos pasos para el final: más personas y ejercicios, reentrenar con datos a la velocidad
   real del Wio, historial en Blynk con el XIAO ESP32-C3.

## Cómo correr el demo

```bash
# firmware (desde GymCoachIA/)
python3 tools/build.py wio --ei --no-servo --upload /dev/cu.usbmodem101
# voz en inglés (y --camara para ver la imagen y acomodarse)
~/.venvs/gym-coach/bin/python tools/probar_modelo.py --en-vivo
```

De costado, a 2–3 m, con hombro, codo, muñeca, cadera, rodilla y tobillo visibles.
