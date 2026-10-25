#!/usr/bin/env python3
"""Compute held-out F1 from CSV columns participant,truth,prediction.

Use real held-out predictions from your model, not rule-generated labels.
"""
import argparse
import csv
import json
from pathlib import Path
from dataset import LABELS

def evaluate(rows):
    matrix = [[0 for _ in LABELS] for _ in LABELS]
    people = set(); total = 0
    for row in rows:
        truth, prediction = row["truth"], row["prediction"]
        if truth not in LABELS or prediction not in LABELS or not row["participant"]:
            raise ValueError("Rows need a participant and recognized truth/prediction labels")
        matrix[LABELS.index(truth)][LABELS.index(prediction)] += 1
        people.add(row["participant"]); total += 1
    if not total:
        raise ValueError("No predictions")
    by_class = {}
    for i, name in enumerate(LABELS):
        tp = matrix[i][i]; support = sum(matrix[i]); predicted = sum(row[i] for row in matrix)
        precision = tp / predicted if predicted else 0
        recall = tp / support if support else 0
        f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0
        by_class[name] = dict(precision=precision, recall=recall, f1=f1, support=support)
    return dict(labels=LABELS, confusion_matrix=matrix, per_class=by_class,
                macro_f1=sum(x["f1"] for x in by_class.values()) / len(LABELS),
                accuracy=sum(matrix[i][i] for i in range(len(LABELS))) / total,
                participants=sorted(people), samples=total)

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("predictions", type=Path)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    with a.predictions.open(newline="") as f:
        result = evaluate(csv.DictReader(f))
    with a.output.open("x") as f:
        json.dump(result, f, indent=2); f.write("\n")
    print(f"Macro F1: {result['macro_f1']:.4f}; accuracy: {result['accuracy']:.4f}")
