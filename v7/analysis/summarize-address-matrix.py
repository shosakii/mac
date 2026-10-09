#!/usr/bin/env python3
"""Summarize paired V7 address-mode runs without pooling unequal traffic."""

from __future__ import annotations

import argparse
import csv
import statistics
from pathlib import Path


METRICS = (
    "rx_packets", "offered_completion_ratio", "data_airtime_us",
    "airtime_us_per_delivered_kib", "topology_airtime_us",
    "mean_queue_delay_ms", "p95_queue_delay_ms",
    "final_one_hop_f1", "final_two_hop_f1",
    "data_ppdu_attempts", "data_mpdu_attempts", "data_retry_mpdus",
    "data_mpdu_bytes",
)
PAIRS = (("A", "B"), ("A", "C"), ("A", "D"), ("B", "D"))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    with (args.directory / "matrix-summary.csv").open(newline="", encoding="utf-8") as stream:
        matrix = list(csv.DictReader(stream))
    with (args.directory / "pairing-checks.csv").open(newline="", encoding="utf-8") as stream:
        checks = list(csv.DictReader(stream))
    if not matrix or len(matrix) != 4 * len(checks):
        raise ValueError("Incomplete four-mode matrix")
    if any(row["identical_offered_demands"] != "1" for row in checks):
        raise ValueError("Offered demand mismatch; paired comparison invalid")

    by_run = {(int(row["run"]), row["mode"]): row for row in matrix}
    seeds = sorted(int(row["run"]) for row in checks)
    rows = []
    for baseline, treatment in PAIRS:
        for metric in METRICS:
            baseline_values = [float(by_run[seed, baseline][metric]) for seed in seeds]
            treatment_values = [float(by_run[seed, treatment][metric]) for seed in seeds]
            differences = [new - old for old, new in zip(baseline_values, treatment_values)]
            percentages = [100 * (new - old) / old for old, new in zip(baseline_values, treatment_values)
                           if old != 0]
            rows.append({
                "comparison": f"{baseline}->{treatment}", "metric": metric,
                "seeds": len(seeds), "baseline_mean": statistics.mean(baseline_values),
                "treatment_mean": statistics.mean(treatment_values),
                "paired_mean_difference": statistics.mean(differences),
                "paired_mean_percent": statistics.mean(percentages) if percentages else "",
                "paired_min_difference": min(differences),
                "paired_max_difference": max(differences),
            })
    destination = args.directory / "paired-comparison.csv"
    with destination.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f"Wrote {destination}; {len(seeds)} paired seeds, {len(matrix)} runs")


if __name__ == "__main__":
    main()
