#!/usr/bin/env python3
"""Make a subject-disjoint train/test split without moving the originals."""
import argparse
import csv
import json
import re
import shutil
from pathlib import Path
from dataset import LABELS, EI_HEADER

def split(source: Path, destination: Path, test_people: set[str]):
    if destination.exists():
        raise ValueError("Destination must be new, so stale training files cannot leak into testing")
    records = []
    for path in sorted(source.glob("*.csv")):
        match = re.fullmatch(r"(curl|sentadilla|extension|descanso)\.([A-Za-z0-9_-]+)_(izq|der)_s\d+_.+_part\d+", path.stem)
        if not match:
            raise ValueError(f"Filename does not contain an unambiguous participant ID: {path.name}")
        label, person, side = match.groups()
        with path.open(newline="") as f:
            reader = csv.reader(f)
            if tuple(next(reader, [])) != EI_HEADER:
                raise ValueError(f"Not an exported EI CSV: {path}")
            rows = list(reader)
            if len(rows) < 20 or any(int(r[0]) != i * 100 for i, r in enumerate(rows)):
                raise ValueError(f"Unexpected sampling interval or segment length: {path}")
        subset = "testing" if person in test_people else "training"
        records.append(dict(file=path.name, label=label, participant=person, side=side, subset=subset, samples=len(rows)))
    if not records:
        raise ValueError("No CSV files found")
    people = {r["participant"] for r in records}
    if not test_people <= people:
        raise ValueError(f"No recordings for test participants: {sorted(test_people-people)}")
    if not people - test_people:
        raise ValueError("No training participants remain")
    destination.mkdir(parents=True)
    for subset in ("training", "testing"):
        (destination / subset).mkdir()
    for r in records:
        shutil.copy2(source / r["file"], destination / r["subset"] / r["file"])
    (destination / "manifest.json").write_text(json.dumps(records, indent=2) + "\n")
    for subset in ("training", "testing"):
        for label in LABELS:
            minutes = sum(r["samples"] for r in records if r["subset"] == subset and r["label"] == label) / 600
            print(f"{subset:8} {label:10} {minutes:.2f} min")
    return records

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("source", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--test-people", nargs="+", required=True)
    args = p.parse_args()
    try:
        split(args.source, args.output, set(args.test_people))
    except ValueError as e:
        raise SystemExit(str(e))
