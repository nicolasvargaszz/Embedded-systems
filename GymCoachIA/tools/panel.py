#!/usr/bin/env python3
"""Live dashboard on the laptop: run it and open http://localhost:8000 (it opens by itself).

Reads what the Wio already prints over USB with STREAM 1 and no camera image, so the Wio keeps
its speed: joint angles, Edge Impulse predictions and every counted rep. Reconnects on its own
when the Wio drops or reboots, announces each new exercise in English (macOS `say`) and saves
the reps of each session as CSV in data/sesiones/. Works offline.
"""
from __future__ import annotations
import argparse
import csv
import json
import subprocess
import threading
import time
import webbrowser
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import serial
from serial.tools import list_ports

RAIZ = Path(__file__).resolve().parents[1]
HTML = Path(__file__).with_name("panel.html")
ENGLISH = {"curl": "Bicep curl", "sentadilla": "Squat", "extension": "Leg extension", "descanso": "Rest"}

class Bus:
    """Events for the browser, plus the state a newly opened page needs (reps, status, exercise)."""
    def __init__(self):
        self.cond = threading.Condition()
        self.events: list[tuple[int, str]] = []
        self.seq = 0
        self.reps: list[dict] = []
        self.status = {"ok": False, "msg": "Looking for the Wio..."}
        self.exercise = ""
        self.mode: dict = {}

    def emit(self, kind: str, **data) -> None:
        with self.cond:
            self.seq += 1
            self.events.append((self.seq, json.dumps({"type": kind, **data})))
            del self.events[:-400]
            self.cond.notify_all()

    def since(self, seq: int, timeout: float) -> list[tuple[int, str]]:
        with self.cond:
            if self.seq <= seq: self.cond.wait(timeout)
            return [e for e in self.events if e[0] > seq]

    def set_status(self, ok: bool, msg: str) -> None:
        if self.status == {"ok": ok, "msg": msg}: return
        self.status = {"ok": ok, "msg": msg}
        self.emit("status", **self.status)

def buscar_wio() -> str | None:
    wio = [p.device for p in list_ports.comports() if p.vid == 0x2886]
    return wio[0] if wio else None

def numero(texto: str) -> float | None:
    return None if texto == "nan" else round(float(texto), 1)

class Lector:
    """Owns the serial port: parses the Wio's lines into events and keeps the session CSV."""
    def __init__(self, bus: Bus, port: str | None, voz: bool, iguales: int, minimo: float):
        self.bus, self.port, self.voz, self.iguales, self.minimo = bus, port, voz, iguales, minimo
        self.candidato, self.racha = None, 0
        self.csv: Path | None = None
        self.serial: serial.Serial | None = None
        self.escribir = threading.Lock()

    def enviar(self, comando: bytes) -> bool:
        """Command for the Wio from the page (START/END); False if it is not connected."""
        with self.escribir:
            if not self.serial: return False
            try: self.serial.write(comando + b"\n"); return True
            except (serial.SerialException, OSError): return False

    def correr(self) -> None:
        while True:
            port = buscar_wio() or self.port
            if not port:
                self.bus.set_status(False, "Wio not connected: plug in the USB cable"); time.sleep(1); continue
            try:
                with serial.Serial(port, 115200, timeout=0.3, write_timeout=1) as s:
                    time.sleep(0.5); s.reset_input_buffer(); s.write(b"CAM 0\nSTREAM 1\n")
                    self.serial = s
                    self.bus.set_status(True, "Live")
                    ultimo = time.time()
                    while True:
                        linea = s.readline().decode(errors="replace").strip()
                        if not linea:
                            if time.time() - ultimo > 3: self.bus.set_status(False, "No data from the Wio: check the Vision AI cable")
                            continue
                        ultimo = time.time()
                        if linea.startswith("# GymCoachIA"): s.write(b"CAM 0\nSTREAM 1\n")  # the Wio rebooted
                        self.procesar(linea)
            except (serial.SerialException, OSError):
                self.serial = None
                self.bus.set_status(False, "Wio disconnected: reconnecting...")
                time.sleep(1)

    def procesar(self, linea: str) -> None:
        if linea.startswith("F,"):
            partes = linea.split(",")
            if len(partes) != 8: return
            self.bus.set_status(True, "Live")
            self.bus.emit("frame", t=time.time(), side=int(partes[3]), a=[numero(x) for x in partes[4:]])
        elif linea.startswith("# ei "):
            try: probs = {k: float(v) for k, v in (par.split("=") for par in linea[5:].split())}
            except ValueError: return
            self.bus.emit("ei", probs=probs)
            self.estabilizar(probs)
        elif linea.startswith("# mode "):
            partes = linea[7:].split()
            datos = dict(par.split("=") for par in partes[1:] if "=" in par)
            self.bus.mode = {"state": partes[0] if partes else "", "exercise": datos.get("exercise", ""),
                             "good": int(datos.get("good", 0)), "goal": int(datos.get("goal", 0))}
            self.bus.emit("mode", **self.bus.mode)
        elif linea.startswith("# rep "):
            campos = dict(par.split("=") for par in linea[6:].split() if "=" in par)
            contado = self.bus.mode.get("exercise") if self.bus.mode.get("state") == "running" else ""
            rep = {"t": datetime.now().strftime("%H:%M:%S"), "epoch": time.time(),
                   "exercise": contado or self.bus.exercise or "?",
                   "valid": campos.get("valid") == "1", "faults": int(campos.get("faults", 0)),
                   "ms": int(campos.get("duration_ms", 0))}
            self.bus.reps.append(rep)
            self.guardar(rep)
            self.bus.emit("rep", **rep)

    def estabilizar(self, probs: dict[str, float]) -> None:
        """A new exercise counts after `iguales` equal confident predictions in a row (200 ms each)."""
        etiqueta = max(probs, key=probs.get)
        if probs[etiqueta] < self.minimo: self.candidato, self.racha = None, 0; return
        self.racha = self.racha + 1 if etiqueta == self.candidato else 1
        self.candidato = etiqueta
        if self.racha >= self.iguales and etiqueta != self.bus.exercise:
            self.bus.exercise = etiqueta
            self.bus.emit("exercise", label=etiqueta)
            if self.voz:
                try: subprocess.Popen(["say", "-v", "Samantha", ENGLISH.get(etiqueta, etiqueta)])
                except OSError: pass

    def guardar(self, rep: dict) -> None:
        if not self.csv:
            carpeta = RAIZ / "data" / "sesiones"; carpeta.mkdir(parents=True, exist_ok=True)
            self.csv = carpeta / f"sesion_{datetime.now():%Y%m%d_%H%M%S}.csv"
            self.csv.write_text("time,exercise,valid,faults,duration_ms\n")
        with self.csv.open("a", newline="") as f:
            csv.writer(f).writerow([rep["t"], rep["exercise"], int(rep["valid"]), rep["faults"], rep["ms"]])

def servidor(bus: Bus, puerto: int, lector: Lector) -> ThreadingHTTPServer:
    class Web(BaseHTTPRequestHandler):
        def log_message(self, *args): pass

        def do_GET(self):
            if self.path == "/":
                cuerpo = HTML.read_bytes()
                self.send_response(200); self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(cuerpo))); self.end_headers(); self.wfile.write(cuerpo)
            elif self.path == "/events":
                self.send_response(200); self.send_header("Content-Type", "text/event-stream")
                self.send_header("Cache-Control", "no-cache"); self.end_headers()
                seq = bus.seq
                inicio = [{"type": "reps", "items": bus.reps}, {"type": "status", **bus.status},
                          {"type": "exercise", "label": bus.exercise}, {"type": "mode", **bus.mode}]
                try:
                    for e in inicio: self.wfile.write(f"data: {json.dumps(e)}\n\n".encode())
                    self.wfile.flush()
                    while True:
                        nuevos = bus.since(seq, 10)
                        for seq, e in nuevos: self.wfile.write(f"data: {e}\n\n".encode())
                        if not nuevos: self.wfile.write(b": ping\n\n")
                        self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError):
                    return
            else:
                self.send_error(404)

        def do_POST(self):
            if self.path in ("/start", "/end"):
                ok = lector.enviar(b"START" if self.path == "/start" else b"END")
                self.send_response(204 if ok else 503); self.end_headers(); return
            if self.path != "/reset": return self.send_error(404)
            bus.reps.clear(); lector.csv = None  # next rep starts a new CSV
            bus.emit("reps", items=[])
            self.send_response(204); self.end_headers()

    ThreadingHTTPServer.daemon_threads = True
    return ThreadingHTTPServer(("127.0.0.1", puerto), Web)

def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", help="Serial port of the Wio (default: found automatically)")
    p.add_argument("--web", type=int, default=8000, help="Port of the page on this computer")
    p.add_argument("--sin-voz", action="store_true", help="Do not announce the exercise out loud")
    p.add_argument("--iguales", type=int, default=3, help="Equal predictions in a row to switch exercise")
    p.add_argument("--minimo", type=float, default=0.7, help="Minimum confidence of a prediction")
    a = p.parse_args()
    bus = Bus()
    lector = Lector(bus, a.port, not a.sin_voz, a.iguales, a.minimo)
    threading.Thread(target=lector.correr, daemon=True).start()
    web = servidor(bus, a.web, lector)
    url = f"http://localhost:{a.web}"
    print(f"Panel en {url}  (Ctrl+C para salir)")
    webbrowser.open(url)
    try: web.serve_forever()
    except KeyboardInterrupt: pass
    if lector.csv: print(f"Sesión guardada en {lector.csv}")

if __name__ == "__main__":
    main()
