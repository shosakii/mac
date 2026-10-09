#!/usr/bin/env python3
"""Run V6 whole-life experiments and build cross-run evidence tables.

The topology actors and event timing are selected from the ns-3 run number.
Representative radios are then selected by declared rules, never by hand.
"""

import argparse
import csv
import math
import statistics
import subprocess
import sys
from pathlib import Path


def find_ns3_root(start: Path) -> Path:
    for candidate in (start, *start.parents):
        if (candidate / "ns3").exists() and (candidate / "build").exists():
            return candidate
    raise RuntimeError("cannot locate ns-3 root above this script")


def read_rows(path: Path):
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def write_rows(path: Path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def ci95(values):
    return 0.0 if len(values) < 2 else 1.96 * statistics.stdev(values) / math.sqrt(len(values))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seeds", default="61,62,63,64,65")
    parser.add_argument("--duration", type=float, default=35.0)
    parser.add_argument("--load", type=float, default=0.25)
    parser.add_argument("--output", default="self-organizing-mac-v6/results/node-lives-v6")
    parser.add_argument("--aggregate-only", action="store_true")
    args = parser.parse_args()

    script = Path(__file__).resolve()
    root = find_ns3_root(script)
    project = script.parents[1]
    binary = root / "build/scratch/self-organizing-mac-v6/ns3.47-self-organizing-64node-v6-default"
    report_builder = project / "analysis/build-node-life-report.py"
    output = root / args.output
    output.mkdir(parents=True, exist_ok=True)
    seeds = [int(value) for value in args.seeds.split(",") if value.strip()]

    for seed in seeds:
        case = output / f"random-seed{seed}"
        if not args.aggregate_only:
            command = [
                str(binary),
                f"--duration={args.duration}",
                "--controlFrame=beacon",
                "--beaconAddress=standard",
                "--controlContenders=8",
                "--neighborEncoding=adaptive",
                "--density=sparse",
                "--topologyScenario=random",
                "--trafficPattern=burst",
                "--dataQueuePolicy=defer",
                f"--perNodeMbps={args.load}",
                f"--run={seed}",
                f"--output={case.relative_to(root)}",
            ]
            print(f"[RUN] whole-life random seed={seed}", flush=True)
            subprocess.run(command, cwd=root, check=True)
        subprocess.run([sys.executable, str(report_builder), str(case)], cwd=root, check=True)

    all_nodes = []
    all_events = []
    representatives = []
    for seed in seeds:
        case = output / f"random-seed{seed}"
        for row in read_rows(case / "node-lifetime-summary.csv"):
            all_nodes.append({"run": seed, **row})
        for row in read_rows(case / "event-detection.csv"):
            all_events.append({"run": seed, **row})
        for row in read_rows(case / "node-life-report/representative-nodes.csv"):
            representatives.append({"run": seed, **row})
    write_rows(output / "all-node-lifetimes.csv", all_nodes)
    write_rows(output / "all-event-detections.csv", all_events)
    write_rows(output / "all-representative-nodes.csv", representatives)

    ratios = [float(row["post_cold_exact_both_ratio"]) for row in all_nodes]
    max_errors = [float(row["max_continuous_error_s"]) for row in all_nodes]
    cold = [float(row["cold_start_first_exact_both_s"]) for row in all_nodes]
    detection_valid = [row for row in all_events if float(row["stable_correct_delay_s"]) >= 0]
    aggregate = [{
        "runs": len(seeds),
        "node_lives": len(all_nodes),
        "mean_post_cold_exact_both_ratio": statistics.mean(ratios),
        "ci95_post_cold_exact_both_ratio": ci95(ratios),
        "minimum_post_cold_exact_both_ratio": min(ratios),
        "median_post_cold_exact_both_ratio": statistics.median(ratios),
        "mean_cold_start_s": statistics.mean(cold),
        "maximum_continuous_error_s": max(max_errors),
        "affected_observer_hop_records": len(all_events),
        "stable_detection_success_ratio": len(detection_valid) / len(all_events),
        "mean_successful_stable_detection_s": statistics.mean(
            float(row["stable_correct_delay_s"]) for row in detection_valid
        ),
    }]
    write_rows(output / "node-life-aggregate.csv", aggregate)
    print(f"[DONE] {output}")


if __name__ == "__main__":
    main()
