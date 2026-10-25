#!/usr/bin/env python3
"""Record the Wio over USB and export gap-aware, 10 Hz Edge Impulse CSVs."""
from __future__ import annotations
import argparse
import csv
import json
import re
import time
from datetime import datetime, timezone
from pathlib import Path
from dataset import AXES, RAW_HEADER, LABELS, Clock, parse_line, export

def safe_name(value: str) -> str:
    if not re.fullmatch(r"[a-zA-Z0-9_-]+", value):
        raise argparse.ArgumentTypeError("Use only letters, digits, underscores and hyphens")
    return value

def arguments():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", help="e.g. /dev/cu.usbmodem14101 or COM5")
    p.add_argument("--label", required=True, choices=LABELS)
    p.add_argument("--persona", required=True, type=safe_name, help="Anonymous participant ID, e.g. p03")
    p.add_argument("--lado", required=True, choices=("izq", "der"))
    p.add_argument("--serie", type=int, default=1)
    p.add_argument("--seconds", type=float, default=60)
    p.add_argument("--output", type=Path, default=Path("data"))
    p.add_argument("--notes", default="")
    a = p.parse_args()
    if a.serie < 1 or a.seconds <= 0:
        p.error("--serie and --seconds must be positive")
    return a

def main():
    args = arguments()
    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        raise SystemExit("Install dependencies: python3 -m pip install -r requirements.txt")
    port = args.port
    if not port:
        ports = [p for p in list_ports.comports() if p.vid == 0x2886]
        if len(ports) != 1:
            found = ", ".join(p.device for p in list_ports.comports()) or "none"
            raise SystemExit(f"Specify --port. Detected ports: {found}")
        port = ports[0].device
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    stem = f"{args.label}.{args.persona}_{args.lado}_s{args.serie:03d}_{stamp}"
    raw = args.output / "raw" / f"{stem}.csv"
    raw.parent.mkdir(parents=True, exist_ok=True)
    frames = valid = 0; first_t = last_t = None
    timestamps = []; clock = Clock(); reason = "duration"
    try:
        with serial.Serial(port, 115200, timeout=0.3) as connection, raw.open("x", newline="") as f:
            time.sleep(2)  # USB board reset, if any, happens before the recording
            connection.reset_input_buffer()
            connection.write(f"SIDE {'LEFT' if args.lado == 'izq' else 'RIGHT'}\nSTREAM 1\n".encode())
            writer = csv.writer(f); writer.writerow(RAW_HEADER)
            started = time.monotonic(); report_at = started
            print(f"Recording {args.label}, participant {args.persona}, side {args.lado}. Ctrl+C stops safely.")
            try:
                while time.monotonic() - started < args.seconds:
                    line = connection.readline().decode("utf-8", errors="replace")
                    frame = parse_line(line)
                    if frame is not None:
                        t = clock.unwrap(frame.timestamp)
                        if last_t == t:
                            continue
                        first_t = t if first_t is None else first_t; last_t = t
                        writer.writerow((t, frame.side, *frame.values)); f.flush()
                        frames += 1; valid += frame.valid; timestamps.append(t)
                    now = time.monotonic()
                    if now - report_at >= 2:
                        print(f"{frames} frames; {valid} with all four angles")
                        report_at = now
                    if not frames and now - started > 15:
                        raise RuntimeError("No frames. Check Wio firmware, port, camera, and close Serial Monitor.")
            except KeyboardInterrupt:
                reason = "user_stop"
            finally:
                connection.write(b"STREAM 0\n")
    except (serial.SerialException, OSError, ValueError, RuntimeError) as error:
        reason = str(error)
        print(f"Recording stopped: {error}")
    if not raw.exists():
        raise SystemExit(1)
    outputs = export(raw, args.output / "ei") if frames else []
    duration = ((last_t - first_t) / 1000) if frames > 1 else 0
    fps = (frames - 1) / duration if duration else 0
    metadata = dict(label=args.label, participant=args.persona, side=args.lado, series=args.serie,
                    notes=args.notes, frames=frames, valid_frames=valid, actual_fps=round(fps, 3),
                    duration_seconds=duration, stopped_because=reason, axes=list(AXES),
                    exported_files=[str(p) for p in outputs], sample_interval_ms=100,
                    max_interpolation_gap_ms=250, raw_file=str(raw))
    raw.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Raw: {raw}\nActual rate: {fps:.2f} FPS; exported {len(outputs)} contiguous CSV segments.")
    if fps < 8:
        print("Acquisition is slow. Measure it before training; 10 Hz export does not create new camera observations.")
    if not outputs:
        print("No segment had 20 valid samples. Reframe all six joints on the selected side and record again.")
    return 0 if outputs else 1

if __name__ == "__main__":
    raise SystemExit(main())
