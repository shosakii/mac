#!/usr/bin/env python3
"""Fail fast when a V6 whole-life result matrix is incomplete or contaminated."""

import argparse
import csv
from pathlib import Path


def read_rows(path):
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("experiment_dir", type=Path)
    parser.add_argument("--expected-runs", type=int, default=5)
    args = parser.parse_args()
    root = args.experiment_dir.resolve()
    runs = sorted(root.glob("random-seed*"))
    errors = []
    if len(runs) != args.expected_runs:
        errors.append(f"expected {args.expected_runs} runs, found {len(runs)}")
    all_nodes = read_rows(root / "all-node-lifetimes.csv")
    if len(all_nodes) != 64 * args.expected_runs:
        errors.append(f"expected {64 * args.expected_runs} node lives, found {len(all_nodes)}")
    reps = read_rows(root / "all-representative-nodes.csv")
    if len(reps) != 6 * args.expected_runs:
        errors.append(f"expected {6 * args.expected_runs} representatives, found {len(reps)}")

    violation_keys = (
        "data_phy_attempts_in_control_window",
        "data_phy_attempts_in_end_guard",
        "data_tx_crossed_superframe_boundary",
    )
    violation_totals = {key: 0 for key in violation_keys}
    final_exact = []
    for run in runs:
        summary = read_rows(run / "summary.csv")[0]
        for key in violation_keys:
            violation_totals[key] += int(float(summary[key]))
        final_exact.append(float(summary["final_exact_both_ratio"]))
        if len(read_rows(run / "node-lifetime-summary.csv")) != 64:
            errors.append(f"{run.name}: node-lifetime-summary does not contain 64 nodes")
        if len(read_rows(run / "node-life-report/representative-nodes.csv")) != 6:
            errors.append(f"{run.name}: representative list does not contain 6 roles")
    if any(violation_totals.values()):
        errors.append(f"window violations: {violation_totals}")

    print(f"runs={len(runs)} node_lives={len(all_nodes)} representatives={len(reps)}")
    print(f"window_violations={violation_totals}")
    print(f"final_exact_both_ratio={final_exact}")
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        raise SystemExit(1)
    print("INTEGRITY=PASS")


if __name__ == "__main__":
    main()
