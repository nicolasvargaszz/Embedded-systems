#!/usr/bin/env python3
"""Guided live test of the Edge Impulse model running on the Wio.

A voice (macOS `say`) tells you which exercise to do and counts down; meanwhile the
Wio streams its predictions ("# ei ..." lines). At the end it prints, for each step,
what the model predicted, plus overall accuracy. The raw log is kept in data/pruebas/.
"""
from __future__ import annotations
import argparse
import subprocess
import sys
import time
from collections import Counter
from datetime import datetime
from pathlib import Path
import serial
from serial.tools import list_ports

PASOS = [("descanso", "Quedate quieto, parado de costado", 20),
         ("curl", "Curl de bíceps con el brazo que da a la cámara", 25),
         ("sentadilla", "Sentadillas, sin salir del cuadro", 25),
         ("extension", "Sentate y hacé extensiones de pierna", 25),
         ("descanso", "Quieto otra vez", 15)]

def decir(texto: str) -> None:
    print(f"\n>>> {texto}")
    try: subprocess.Popen(["say", "-v", "Eddy (Español (México))", texto])
    except OSError: pass

def puerto() -> str:
    wio = [p.device for p in list_ports.comports() if p.vid == 0x2886]
    if len(wio) != 1: sys.exit(f"No encuentro un único Wio por USB ({wio}); pasá --port")
    return wio[0]

def parsear(linea: str):
    if not linea.startswith("# ei "): return None
    probs = dict(par.split("=") for par in linea[5:].split())
    probs = {k: float(v) for k, v in probs.items()}
    return max(probs, key=probs.get), probs

ENGLISH = {"curl": "Bicep curl", "sentadilla": "Squat", "extension": "Leg extension", "descanso": "Rest"}

def en_vivo(s: serial.Serial, iguales: int, minimo: float) -> None:
    """Announce (in English) each time the Wio's prediction settles on a new exercise."""
    print("En vivo: Ctrl+C para salir")
    actual, candidato, racha = None, None, 0
    try:
        while True:
            r = parsear(s.readline().decode(errors="replace").strip())
            if not r: continue
            etiqueta, probs = r
            if probs[etiqueta] < minimo: candidato, racha = None, 0; continue
            racha = racha + 1 if etiqueta == candidato else 1
            candidato = etiqueta
            print(f"   {etiqueta:<10} {probs[etiqueta]:.0%}")
            if racha >= iguales and etiqueta != actual:
                actual = etiqueta
                print(f">>> {ENGLISH[etiqueta]}")
                subprocess.Popen(["say", "-v", "Samantha", ENGLISH[etiqueta]])
    except KeyboardInterrupt:
        pass
    finally:
        s.write(b"STREAM 0\n")

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port")
    p.add_argument("--preparacion", type=int, default=30, help="Segundos para ponerte en posición")
    p.add_argument("--en-vivo", action="store_true", help="Sin guion: anuncia en inglés cada cambio de ejercicio")
    p.add_argument("--iguales", type=int, default=3, help="Predicciones seguidas (cada 200 ms) para anunciar")
    p.add_argument("--minimo", type=float, default=0.7, help="Confianza mínima de una predicción")
    a = p.parse_args()
    s = serial.Serial(a.port or puerto(), 115200, timeout=0.2)
    time.sleep(0.5); s.reset_input_buffer(); s.write(b"STREAM 1\n")
    if a.en_vivo: return en_vivo(s, a.iguales, a.minimo)
    salida = Path(__file__).resolve().parents[1] / "data" / "pruebas"
    salida.mkdir(parents=True, exist_ok=True)
    log = (salida / f"prueba_{datetime.now():%Y%m%d_%H%M%S}.log").open("w")
    resultados: list[tuple[str, str, dict]] = []
    cuadros = validos = 0

    def leer_hasta(fin: float, etiqueta: str | None):
        nonlocal cuadros, validos
        while time.time() < fin:
            linea = s.readline().decode(errors="replace").strip()
            if not linea: continue
            log.write(f"{etiqueta or '-'}\t{linea}\n")
            if linea.startswith("F,"):
                cuadros += 1; validos += "nan" not in linea
            r = parsear(linea)
            if r and etiqueta:
                resultados.append((etiqueta, r[0], r[1]))
                print(f"   modelo: {r[0]:<10} ({r[1][r[0]]:.0%})")

    try:
        decir(f"Ponete en posición. Empezamos en {a.preparacion} segundos")
        leer_hasta(time.time() + a.preparacion - 3, None)
        for i, (etiqueta, instruccion, segundos) in enumerate(PASOS):
            decir(instruccion)
            leer_hasta(time.time() + 3, None)   # time to get into the movement; not scored
            decir("Ya")
            for falta in range(segundos, 0, -5):
                leer_hasta(time.time() + min(5, falta), etiqueta)
                if 5 < falta <= 10: decir("Cinco segundos")
            if i + 1 < len(PASOS): decir("Cambio")
        decir("Terminamos")
    finally:
        s.write(b"STREAM 0\n"); log.close()

    print(f"\nCuadros con el cuerpo completo: {validos}/{cuadros}")
    print(f"\n{'Paso':<12}{'decisiones':>11}  predicciones")
    aciertos = 0
    for etiqueta in dict.fromkeys(e for e, _, _ in PASOS):
        preds = Counter(pr for e, pr, _ in resultados if e == etiqueta)
        aciertos += preds[etiqueta]
        detalle = ", ".join(f"{k} {v}" for k, v in preds.most_common()) or "ninguna (¿no te veía entero?)"
        print(f"{etiqueta:<12}{sum(preds.values()):>11}  {detalle}")
    if resultados: print(f"\nAciertos: {aciertos}/{len(resultados)} = {aciertos / len(resultados):.0%}")
    print(f"Log: {log.name}")

if __name__ == "__main__":
    main()
