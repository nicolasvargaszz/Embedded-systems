# Gym Coach IA — NTUST Embedded Systems 2026

Entrenador de gimnasio con TinyML: la **Grove Vision AI V2** corre un modelo de pose
(YOLOv8n-pose, 17 puntos del cuerpo), se calculan los ángulos de codo, rodilla, cadera y
torso, se cuentan repeticiones y un clasificador de **Edge Impulse** reconoce el ejercicio
(curl, sentadilla, extensión, descanso). Todo el plan está en `Guia_Gym_Coach_IA.pdf`.

---

## Grabar datos (sentadillas y curls) — empezá acá

Solo necesitás la **Vision AI V2** (con su cámara) enchufada por USB a la compu. No hace
falta el Wio Terminal ni Arduino.

### 1. Instalar (una sola vez)

Necesitás Python 3.10 o más nuevo y git.

**Mac / Linux**

```bash
git clone https://github.com/nicolasvargaszz/Embedded-systems.git
cd Embedded-systems/proyecto
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

**Windows** (PowerShell)

```powershell
git clone https://github.com/nicolasvargaszz/Embedded-systems.git
cd Embedded-systems\proyecto
py -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
```

### 2. Grabar

Enchufá la Vision AI por USB (cable de datos, la cinta de la cámara bien calzada) y corré,
con **tu nombre** en `--persona` y el lado del cuerpo que mira a la cámara en `--lado`:

```bash
python coach_pc.py --persona juan --lado izq
```

Se abre una ventana con la cámara, el esqueleto, los ángulos y un contador de reps.
El puerto se detecta solo; si hay más de uno te los lista y lo pasás con
`--port COM5` (Windows) o `--port /dev/cu.usbmodemXXXX` (Mac).

| Tecla | Qué hace |
|---|---|
| `g` | **Modo guiado** (lo más fácil): una voz pide curl → sentadilla → descanso, 4 rondas, y graba sola |
| `1` / `2` / `4` | Graba una muestra suelta de curl / sentadilla / descanso (10 s, cuenta 3 s antes) |
| `3` | Extensión de pierna (para cuando tengamos la máquina) |
| `r` | Reinicia los contadores de reps |
| `q` | Salir |

Cada muestra queda en `proyecto/datos/` como `etiqueta.persona_NNN.json` (formato de
adquisición de Edge Impulse, 10 Hz, ejes `codo, rodilla, cadera, torso`).

### 3. Cómo pararse para que los datos sirvan

- Cámara **de costado**, a 2–3 m, a la altura de la cadera, sin moverla entre muestras.
- **Todo el cuerpo en cuadro**, también en los curls: el modelo usa los cuatro ángulos. Si
  la terminal dice `OJO: casi no se vio rodilla`, repetí esa muestra con mejor encuadre.
- Una sola persona en cuadro.
- En `descanso`: parado, caminando, acomodándose, sentado. Cualquier cosa que no sea el ejercicio.
- Mínimo **4 muestras de cada etiqueta por persona** (`g` hace exactamente eso). Si podés,
  grabá también del otro lado (`--lado der`).
- Cada persona con su propio nombre: el test se separa por persona.

### 4. Compartir los datos

**Opción A — al repo** (Nico los tiene que agregar como colaboradores):

```bash
git pull
git add proyecto/datos
git commit -m "Datos de juan: curl, sentadilla, descanso"
git push
```

**Opción B — directo a Edge Impulse**: pedile a Nico la API key del proyecto
`gym-coach-ia` y guardala en un archivo `~/.ei_api_key` (o en la variable `EI_API_KEY`).
Con eso cada muestra se sube sola al terminar. Con `--test` va al set de test en lugar de
training. **Nunca subas la key al repo.**

### Si algo falla

| Síntoma | Solución |
|---|---|
| `No pude elegir el puerto solo` | Pasá `--port` con el que corresponda de la lista |
| La ventana no muestra imagen | Desenchufá y volvé a enchufar la Vision AI; cerrá cualquier otro programa que use el puerto (SenseCraft, Arduino IDE) |
| Imagen pero sin esqueleto | La Vision AI no tiene el modelo de pose: ver "Modelo de pose" abajo |
| Windows no ve el puerto | Instalá el driver USB-serial CH343 de WCH y reconectá |
| `Se desconecto la Vision AI` | Cable flojo; reconectá y volvé a correr el script |

---

## Modelo de pose (solo si hay que volver a cargarlo)

SenseCraft no trae un modelo de pose para esta placa. El que funciona es el YOLOv8n-pose 256
de Himax: `proyecto/yolov8n_pose_256_vela.tflite`.

1. Abrí [SenseCraft AI](https://sensecraft.seeed.cc/ai) en Chrome → *Workspace* → Grove Vision AI V2.
2. *Upload Model*: cualquier nombre, un objeto `person`, elegí el `.tflite`.
3. **No hay barra de progreso** y tarda unos 3 minutos. No desconectes hasta que cambie el
   *Model Name*.

Si al arrancar aparece `dw_iic_write err ... Fail:-60`, la cinta de la cámara está floja.

---

## Qué hay en el repo

| Carpeta | Qué es |
|---|---|
| `proyecto/` | `coach_pc.py` (vista + grabador de datos en la compu), el modelo de pose y `datos/` |
| `GymCoachIA/` | Firmware completo para el Wio Terminal (conteo, técnica, salidas, Edge Impulse, XIAO + Blynk) con sus herramientas y tests. Ver su propio `README.md` |
| `vision_wio/` | Prueba de Vision AI V2 → Wio por I2C con la cámara en el LCD |
| `clases/` | Checkpoints de clase (servo, micrófono, DHT20, RGB) y tareas HW1/HW2 |
| `image/` | Fotos del kit y tarjeta de pines (`componentes.jpeg`) |
| `Guia_Gym_Coach_IA.pdf` | Guía del proyecto: hardware, conexiones, dataset, plan |
| `CLAUDE.md` | Notas técnicas y trampas conocidas del hardware (vale leerlas) |

Hardware: Seeed Wio Terminal, Grove Vision AI V2 + cámara OV5647, XIAO ESP32-C3, display
TM1637, LED RGB (ánodo común), 3 LEDs, servo SG90.
