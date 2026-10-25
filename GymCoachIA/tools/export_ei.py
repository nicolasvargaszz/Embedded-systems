#!/usr/bin/env python3
import argparse
from pathlib import Path
from dataset import export

if __name__ == "__main__":
    p = argparse.ArgumentParser(description="Re-export a raw recording into a new directory at 10 Hz")
    p.add_argument("raw", type=Path)
    p.add_argument("--output", required=True, type=Path)
    a = p.parse_args()
    for path in export(a.raw, a.output):
        print(path)
