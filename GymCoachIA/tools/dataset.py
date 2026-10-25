"""Shared, dependency-free frame parsing and Edge Impulse CSV export.

Raw serial: F,1,device_ms,side,codo,rodilla,cadera,torso
Raw disk: timestamp,side,codo,rodilla,cadera,torso (unwrapped device time).
EI disk: timestamp,codo,rodilla,cadera,torso (per-segment relative ms).
"""
from __future__ import annotations
import csv
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

AXES = ("codo", "rodilla", "cadera", "torso")
RAW_HEADER = ("timestamp", "side", *AXES)
EI_HEADER = ("timestamp", *AXES)
LABELS = ("curl", "sentadilla", "extension", "descanso")

@dataclass(frozen=True)
class Frame:
    timestamp: int
    side: int
    values: tuple[float, ...]

    @property
    def valid(self) -> bool:
        return self.side in (0, 1) and len(self.values) == 4 and all(math.isfinite(v) and 0 <= v <= 180 for v in self.values)

def parse_line(line: str) -> Frame | None:
    parts = line.strip().split(",")
    if len(parts) != 8 or parts[:2] != ["F", "1"]:
        return None
    try:
        t, side = int(parts[2]), int(parts[3])
        values = tuple(float(x) for x in parts[4:])
    except ValueError:
        return None
    if not 0 <= t <= 0xFFFFFFFF or side not in (-1, 0, 1):
        return None
    return Frame(t, side, values)

class Clock:
    """Unwrap millis(); reject a board reset instead of joining unrelated sessions."""
    def __init__(self):
        self.previous = None
        self.epoch = 0

    def unwrap(self, t: int) -> int:
        if self.previous is not None and t < self.previous:
            if self.previous > 0xF0000000 and t < 0x10000000:
                self.epoch += 1 << 32
            else:
                raise ValueError("Wio clock moved backwards: board reset; start a new recording")
        self.previous = t
        return self.epoch + t

def read_raw(path: Path) -> list[Frame]:
    with path.open(newline="") as f:
        reader = csv.DictReader(f)
        if tuple(reader.fieldnames or ()) != RAW_HEADER:
            raise ValueError(f"Unexpected raw CSV columns in {path}")
        return [Frame(int(r["timestamp"]), int(r["side"]), tuple(float(r[a]) for a in AXES)) for r in reader]

def resample_segments(frames: Iterable[Frame], interval_ms: int = 100, max_gap_ms: int = 250):
    """Match CoachCore::Resampler; split at missing joints, side changes or gaps.

    Linear interpolation requires valid bracketing frames. Never fabricate a long
    repeated hold to conceal missing inference. Each output segment starts at 0.
    """
    segment = []
    previous = None
    start = next_time = 0
    for frame in frames:
        if previous is not None and frame.timestamp <= previous.timestamp:
            if frame.timestamp == previous.timestamp:
                continue
            raise ValueError("Raw timestamps must be increasing")
        broken = previous is not None and (frame.side != previous.side or frame.timestamp - previous.timestamp > max_gap_ms)
        if not frame.valid or broken:
            if segment:
                yield segment
            segment, previous = [], None
        if not frame.valid:
            continue
        if previous is None:
            start = frame.timestamp
            next_time = start + interval_ms
            segment = [(0, *frame.values)]
        else:
            while next_time <= frame.timestamp:
                fraction = (next_time - previous.timestamp) / (frame.timestamp - previous.timestamp)
                values = tuple(a + fraction * (b - a) for a, b in zip(previous.values, frame.values))
                segment.append((next_time - start, *values))
                next_time += interval_ms
        previous = frame
    if segment:
        yield segment

def export(raw: Path, destination: Path, minimum_samples: int = 20) -> list[Path]:
    frames = read_raw(raw)
    destination.mkdir(parents=True, exist_ok=True)
    outputs = []
    for index, segment in enumerate(resample_segments(frames), 1):
        if len(segment) < minimum_samples:
            continue
        path = destination / f"{raw.stem}_part{index:03d}.csv"
        with path.open("x", newline="") as f:
            writer = csv.writer(f); writer.writerow(EI_HEADER)
            for row in segment:
                writer.writerow([row[0], *(f"{v:.3f}" for v in row[1:])])
        outputs.append(path)
    return outputs
