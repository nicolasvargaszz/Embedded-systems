"""Gym Coach IA - vista en la compu + grabacion de datos para Edge Impulse.

Lee la Grove Vision AI V2 por USB (protocolo AT de SSCMA) con el modelo YOLOv8n-pose,
dibuja el esqueleto, calcula angulos (codo, rodilla, cadera, torso), cuenta reps y graba
muestras etiquetadas a 10 Hz en el formato de Edge Impulse (y las sube si hay API key).

Uso:   python coach_pc.py --persona juan [--lado izq|der] [--port PUERTO] [--seg 10] [--test]
Teclas: 1 curl | 2 sentadilla | 3 extension | 4 descanso | g modo guiado | r resetear reps | q salir
Modo guiado (--guiado N o tecla g): una voz pide curl, sentadilla, extension y descanso N veces y graba sola.
API key de Edge Impulse: variable EI_API_KEY o archivo ~/.ei_api_key (nunca en esta carpeta).
Funciona en macOS, Windows y Linux; el puerto se detecta solo si hay un unico candidato.
"""
import argparse, base64, glob, json, math, os, re, subprocess, sys, time, urllib.request
import cv2
import numpy as np
import serial
from serial.tools import list_ports

ETIQUETAS = {ord("1"): "curl", ord("2"): "sentadilla", ord("3"): "extension", ord("4"): "descanso"}
SENSORES = ["codo", "rodilla", "cadera", "torso"]
PLAN_GUIADO = ["curl", "sentadilla", "extension", "descanso"]
INTERVALO_MS = 100
MIN_CONF = 0.3
ESQUELETO = [(5, 7), (7, 9), (6, 8), (8, 10), (5, 6), (5, 11), (6, 12), (11, 12),
             (11, 13), (13, 15), (12, 14), (14, 16), (0, 1), (0, 2), (1, 3), (2, 4)]
LADO = {"izq": (5, 7, 9, 11, 13, 15), "der": (6, 8, 10, 12, 14, 16)}  # hombro codo muneca cadera rodilla tobillo
DATOS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "datos")


def angulo(a, b, c):
    """Angulo en b (grados) entre b->a y b->c."""
    v1, v2 = np.subtract(a, b), np.subtract(c, b)
    n = np.linalg.norm(v1) * np.linalg.norm(v2)
    return float(np.degrees(np.arccos(np.clip(np.dot(v1, v2) / n, -1, 1)))) if n else math.nan


def puntos(persona):
    """[[box], pts] de SSCMA -> lista de 17 (x, y, conf 0-1). Firmware 2025.01.02: pts = [[x, y, score 0-100, id], ...]."""
    pts = persona[1]
    if pts and not isinstance(pts[0], list):
        pts = [pts[i:i + 3] for i in range(0, len(pts), 3)]
    return [(p[0], p[1], p[2] / 100) for p in pts]


def angulos(kp, lado=None):
    """[codo, rodilla, cadera, torso] en grados (NaN si no se ve). Sin lado fijo usa el mas visible."""
    lado = LADO[lado] if lado else max(LADO.values(), key=lambda idx: sum(kp[i][2] for i in idx))
    h, co, mu, ca, ro, to = (kp[i] for i in lado)
    ok = lambda *ps: all(p[2] >= MIN_CONF for p in ps)
    codo = angulo(h[:2], co[:2], mu[:2]) if ok(h, co, mu) else math.nan
    rodilla = angulo(ca[:2], ro[:2], to[:2]) if ok(ca, ro, to) else math.nan
    cadera = angulo(h[:2], ca[:2], ro[:2]) if ok(h, ca, ro) else math.nan
    torso = math.degrees(math.atan2(abs(h[0] - ca[0]), abs(ca[1] - h[1]) + 1e-6)) if ok(h, ca) else math.nan
    return [codo, rodilla, cadera, torso]


class Contador:
    """Rep = bajar de 'alto' a 'bajo' y volver (histeresis, sin rebotes)."""
    def __init__(self, bajo, alto):
        self.bajo, self.alto, self.reps, self.abajo = bajo, alto, 0, False

    def actualizar(self, a):
        if math.isnan(a):
            return
        if not self.abajo and a < self.bajo:
            self.abajo = True
        elif self.abajo and a > self.alto:
            self.abajo, self.reps = False, self.reps + 1


def remuestrear(muestras):
    """(t_ms, [4 angulos]) irregulares -> grilla fija de 100 ms, rellenando huecos.

    Devuelve (valores, aviso). El aviso nombra los angulos que casi no se vieron (rellenados)."""
    t = np.array([m[0] for m in muestras], float)
    v = np.array([m[1] for m in muestras], float)
    grilla = np.arange(t[0], t[-1], INTERVALO_MS)
    cols, flojos = [], []
    for j in range(v.shape[1]):
        ok = ~np.isnan(v[:, j])
        if ok.mean() < 0.5:
            flojos.append(SENSORES[j])
        cols.append(np.interp(grilla, t[ok], v[ok, j]) if ok.sum() >= 2 else np.zeros(len(grilla)))
    aviso = f"  OJO: casi no se vio {', '.join(flojos)}; encuadra el cuerpo entero" if flojos else ""
    return np.round(np.stack(cols, 1), 1).tolist(), aviso


def guardar_y_subir(etiqueta, valores, persona, es_test):
    os.makedirs(DATOS, exist_ok=True)
    n = len(glob.glob(os.path.join(DATOS, f"{etiqueta}.{persona}_*.json"))) + 1
    nombre = f"{etiqueta}.{persona}_{n:03d}.json"
    doc = {"protected": {"ver": "v1", "alg": "none", "iat": int(time.time())},
           "signature": "0" * 64,
           "payload": {"device_name": "grove-vision-ai-v2", "device_type": "GROVE_VISION_AI_V2",
                       "interval_ms": INTERVALO_MS,
                       "sensors": [{"name": s, "units": "deg"} for s in SENSORES],
                       "values": valores}}
    with open(os.path.join(DATOS, nombre), "w") as f:
        json.dump(doc, f)
    key = os.environ.get("EI_API_KEY")
    if not key and os.path.exists(os.path.expanduser("~/.ei_api_key")):
        key = open(os.path.expanduser("~/.ei_api_key")).read().strip()
    if not key:
        return f"{nombre} guardado (sin API key, no se subio)"
    req = urllib.request.Request(
        f"https://ingestion.edgeimpulse.com/api/{'testing' if es_test else 'training'}/data",
        data=json.dumps(doc).encode(), method="POST",
        headers={"x-api-key": key, "x-file-name": nombre, "x-label": etiqueta,
                 "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return f"{nombre} subido a Edge Impulse ({r.status})"
    except Exception as e:
        return f"{nombre} guardado, fallo la subida: {e}"


def decir(texto):
    """Voz en macOS (say) o Windows (SAPI); en otro sistema, o si falla, solo lo imprime."""
    print(">>", texto, flush=True)
    if sys.platform == "darwin":
        cmd = ["say", "-v", "Eddy (Español (México))", texto]
    elif sys.platform == "win32":
        cmd = ["powershell", "-NoProfile", "-Command",
               "Add-Type -AssemblyName System.Speech; (New-Object System.Speech.Synthesis.SpeechSynthesizer)"
               f".Speak('{texto}')"]
    else:
        return
    try:
        subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except OSError:
        pass


def buscar_puerto():
    """La Vision AI V2 aparece como usbmodem (Mac), COMx (Windows) o ttyACM (Linux)."""
    puertos = [p.device for p in list_ports.comports()
               if re.search(r"usbmodem|ttyACM|COM\d", p.device) and "Bluetooth" not in p.device]
    if len(puertos) == 1:
        return puertos[0]
    todos = ", ".join(p.device for p in list_ports.comports()) or "ninguno"
    sys.exit(f"No pude elegir el puerto solo. Pasalo con --port. Puertos encontrados: {todos}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", help="ej. /dev/cu.usbmodem5A7A0001 (Mac) o COM5 (Windows); por defecto lo detecta")
    ap.add_argument("--seg", type=float, default=10, help="duracion de cada muestra")
    ap.add_argument("--persona", help="tu nombre sin espacios (va en el nombre de cada archivo)")
    ap.add_argument("--lado", choices=LADO, help="lado del cuerpo que ve la camara; por defecto el mas visible")
    ap.add_argument("--test", action="store_true", help="subir al set de test en vez de training")
    ap.add_argument("--guiado", type=int, default=0, help="rondas del modo guiado al arrancar")
    args = ap.parse_args()
    while not args.persona or not re.fullmatch(r"[A-Za-z0-9-]+", args.persona):
        args.persona = input("Tu nombre, sin espacios ni acentos (ej. juan): ").strip()

    vai = serial.Serial(args.port or buscar_puerto(), 921600, timeout=1)
    vai.write(b"AT+BREAK\r"); time.sleep(0.3); vai.reset_input_buffer()
    vai.write(b"AT+INVOKE=-1,0,0\r")  # infinito, sin diferir, con imagen

    curl, sentadilla = Contador(70, 140), Contador(100, 160)
    grabando, cuenta, muestras, estado = None, 0, [], "1 curl  2 sentadilla  3 extension  4 descanso"
    img, kp, ang, t0, fps_t, fps = None, None, [math.nan] * 4, 0, time.time(), 0.0
    cv2.namedWindow("Gym Coach IA", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("Gym Coach IA", 720, 720)
    plan, proximo = [], 0.0
    if args.guiado:
        plan, proximo = PLAN_GUIADO * args.guiado, time.time() + 15
        decir("Modo guiado. Ponete en posicion, empezamos en quince segundos")
    try:
        while True:
            linea = vai.readline().strip()  # cada mensaje llega como "\r{...}\n"
            if linea.startswith(b"{"):
                try:
                    msg = json.loads(linea)
                except ValueError:
                    msg = {}
                d = msg.get("data", {}) if msg.get("name") == "INVOKE" and msg.get("type") == 1 else None
                if isinstance(d, dict):
                    if d.get("image"):
                        img = cv2.imdecode(np.frombuffer(base64.b64decode(d["image"]), np.uint8), cv2.IMREAD_COLOR)
                    personas = d.get("keypoints") or []
                    kp = puntos(max(personas, key=lambda p: p[0][4])) if personas else None
                    ang = angulos(kp, args.lado) if kp else [math.nan] * 4
                    curl.actualizar(ang[0]); sentadilla.actualizar(ang[1])
                    ahora = time.time()
                    fps, fps_t = 0.8 * fps + 0.2 / max(ahora - fps_t, 1e-3), ahora
                    if grabando and ahora >= cuenta:
                        muestras.append(((ahora - t0) * 1000, ang))

            if img is not None:
                vis = cv2.resize(img, (480, 480))
                esc = 480 / img.shape[1]
                if kp:
                    for a, b in ESQUELETO:
                        if kp[a][2] >= MIN_CONF and kp[b][2] >= MIN_CONF:
                            cv2.line(vis, (int(kp[a][0] * esc), int(kp[a][1] * esc)),
                                     (int(kp[b][0] * esc), int(kp[b][1] * esc)), (0, 255, 0), 2)
                    for x, y, c in kp:
                        if c >= MIN_CONF:
                            cv2.circle(vis, (int(x * esc), int(y * esc)), 4, (0, 200, 255), -1)
                txt = [f"codo {ang[0]:5.0f}  rodilla {ang[1]:5.0f}",
                       f"cadera {ang[2]:5.0f}  torso {ang[3]:5.0f}",
                       f"reps curl {curl.reps}   sentadilla {sentadilla.reps}   {fps:4.1f} fps"]
                for i, s in enumerate(txt):
                    cv2.putText(vis, s, (8, 22 + 22 * i), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)
                if grabando:
                    ahora = time.time()
                    s = f"PREPARATE {cuenta - ahora:.0f}" if ahora < cuenta else f"GRABANDO {grabando} {t0 + args.seg - ahora:.0f}s"
                    cv2.putText(vis, s, (8, 460), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 255), 2)
                else:
                    cv2.putText(vis, estado, (8, 465), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 0), 1)
                cv2.imshow("Gym Coach IA", vis)

            if grabando and time.time() >= t0 + args.seg:
                if len(muestras) >= 5:
                    valores, aviso = remuestrear(muestras)
                    estado = guardar_y_subir(grabando, valores, args.persona, args.test) + aviso
                else:
                    estado = "muy pocos frames, repeti"
                print(estado, flush=True)
                decir("listo" if plan else "listo, terminamos")
                grabando, proximo = None, time.time() + 1.5

            if not grabando and plan and time.time() >= proximo:
                grabando, muestras = plan.pop(0), []
                cuenta = t0 = time.time() + 5
                decir(f"{grabando}, en cinco segundos")

            tecla = cv2.waitKey(1) & 0xFF
            if tecla == ord("q"):
                break
            if tecla == ord("g") and not plan and not grabando:
                plan, proximo = PLAN_GUIADO * 4, time.time() + 15
                decir("Modo guiado. Ponete en posicion, empezamos en quince segundos")
            if tecla == ord("r"):
                curl.reps = sentadilla.reps = 0
            if tecla in ETIQUETAS and not grabando:
                grabando, muestras = ETIQUETAS[tecla], []
                cuenta = time.time() + 3
                t0 = cuenta
                decir(f"{grabando}, en tres segundos")
    except serial.SerialException:
        print("Se desconecto la Vision AI (cable USB). Volve a enchufarla y abri el programa de nuevo.")
    finally:
        try:
            vai.write(b"AT+BREAK\r"); vai.close()
        except (serial.SerialException, OSError):
            pass
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
