#!/usr/bin/env python3
"""Summarize V7 robustness runs by paired seed, never by pooled packets."""

from __future__ import annotations

import argparse
import csv
import math
import statistics
from collections import defaultdict
from pathlib import Path


METRICS = (
    "offered_completion_ratio", "mac_pdr", "rx_packets",
    "topology_airtime_us", "control_window_attempt_occupancy",
    "data_airtime_us", "airtime_us_per_delivered_kib",
    "final_one_hop_f1", "final_two_hop_f1", "final_exact_both_ratio",
    "mean_node_lifetime_exact_ratio", "worst_node_lifetime_exact_ratio",
    "mean_queue_delay_ms", "p95_queue_delay_ms", "control_rx",
    "injected_control_drops",
)
T95 = {2: 12.706, 3: 4.303, 4: 3.182, 5: 2.776,
       6: 2.571, 7: 2.447, 8: 2.365, 9: 2.306, 10: 2.262}


def ci95(values: list[float]) -> float:
    if len(values) < 2:
        return float("nan")
    critical = T95.get(len(values), 1.96 if len(values) > 30 else 2.0)
    return critical * statistics.stdev(values) / math.sqrt(len(values))


def write(path: Path, rows: list[dict[str, object]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("result_dir", type=Path)
    args = parser.parse_args()
    result_dir = args.result_dir.resolve()
    with (result_dir / "per-run.csv").open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    by_profile_seed: dict[tuple[str, int], dict[str, dict[str, str]]] = defaultdict(dict)
    for row in rows:
        key = (row["profile"], int(row["run"]))
        if row["mode"] in by_profile_seed[key]:
            raise ValueError(f"Duplicate case: {key} {row['mode']}")
        by_profile_seed[key][row["mode"]] = row
    complete = {key: modes for key, modes in by_profile_seed.items()
                if set(modes) == {"A", "B", "C", "D"}}
    if len(complete) != len(by_profile_seed):
        incomplete = sorted(set(by_profile_seed) - set(complete))
        raise ValueError(f"Incomplete mode pairs: {incomplete}")

    grouped: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    for (profile, _seed), modes in complete.items():
        for mode, row in modes.items():
            grouped[(profile, mode)].append(row)
    aggregate: list[dict[str, object]] = []
    for (profile, mode), group in sorted(grouped.items()):
        for metric in METRICS:
            values = [float(row[metric]) for row in group]
            aggregate.append({"profile": profile, "mode": mode, "metric": metric,
                              "runs": len(values), "mean": statistics.mean(values),
                              "min": min(values), "max": max(values),
                              "ci95_halfwidth": ci95(values)})

    contrasts: list[dict[str, object]] = []
    profiles = sorted({profile for profile, _seed in complete})
    for profile in profiles:
        seeds = sorted(seed for p, seed in complete if p == profile)
        for mode in ("B", "C", "D"):
            for metric in METRICS:
                baselines = [float(complete[(profile, seed)]["A"][metric]) for seed in seeds]
                tests = [float(complete[(profile, seed)][mode][metric]) for seed in seeds]
                deltas = [test - base for base, test in zip(baselines, tests)]
                pct = [100 * delta / base for base, delta in zip(baselines, deltas)
                       if base != 0]
                contrasts.append({
                    "profile": profile, "comparison": f"A_to_{mode}", "metric": metric,
                    "runs": len(seeds), "baseline_mean": statistics.mean(baselines),
                    "test_mean": statistics.mean(tests),
                    "paired_mean_delta": statistics.mean(deltas),
                    "paired_delta_ci95_halfwidth": ci95(deltas),
                    "paired_mean_pct_delta": statistics.mean(pct) if pct else float("nan"),
                })
    write(result_dir / "robustness-aggregate.csv", aggregate)
    write(result_dir / "robustness-paired-contrasts.csv", contrasts)
    print(f"[DONE] {len(complete)} complete seed/profile pairs across {len(profiles)} profiles")


if __name__ == "__main__":
    main()
