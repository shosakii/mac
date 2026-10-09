#!/usr/bin/env python3
"""Paired comparison of list/bitmap/adaptive and standard/short Beacon formats."""

import argparse
import csv
import math
import statistics
import subprocess
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seeds", default="41,42,43")
    parser.add_argument("--duration", type=float, default=35.0)
    parser.add_argument("--load", type=float, default=0.25)
    parser.add_argument("--output", default="self-organizing-mac-v6/results/encoding-v51")
    parser.add_argument("--aggregate-only", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    binary = root / "build/scratch/self-organizing-mac-v6/ns3.47-self-organizing-64node-v6-default"
    out = root / args.output
    out.mkdir(parents=True, exist_ok=True)
    seeds = [int(value) for value in args.seeds.split(",")]
    for seed in seeds:
        for address in ("standard", "short"):
            for encoding in ("list", "bitmap", "adaptive"):
                case = out / f"{address}-{encoding}-seed{seed}"
                command = [str(binary), f"--duration={args.duration}", "--controlFrame=beacon",
                           f"--beaconAddress={address}", "--controlContenders=8",
                           f"--neighborEncoding={encoding}", "--density=sparse",
                           "--topologyScenario=scripted", "--trafficPattern=burst",
                           "--dataQueuePolicy=defer", f"--perNodeMbps={args.load}",
                           f"--run={seed}", f"--output={case.relative_to(root)}"]
                if not args.aggregate_only:
                    print(f"[RUN] seed={seed} address={address} encoding={encoding}", flush=True)
                    subprocess.run(command, cwd=root, check=True)

    rows = []
    for seed in seeds:
        for address in ("standard", "short"):
            for encoding in ("list", "bitmap", "adaptive"):
                case = out / f"{address}-{encoding}-seed{seed}"
                with (case / "summary.csv").open(newline="", encoding="utf-8") as handle:
                    row = next(csv.DictReader(handle))
                with (case / "node-lifetime-summary.csv").open(newline="", encoding="utf-8") as handle:
                    lifetime = list(csv.DictReader(handle))
                with (case / "event-detection.csv").open(newline="", encoding="utf-8") as handle:
                    detection = list(csv.DictReader(handle))
                row["mean_post_cold_exact_both_ratio"] = statistics.mean(
                    float(item["post_cold_exact_both_ratio"]) for item in lifetime)
                for hop in ("1", "2"):
                    first = [float(item["first_correct_delay_s"]) for item in detection
                             if item["hop"] == hop and float(item["first_correct_delay_s"]) >= 0]
                    stable = [float(item["stable_correct_delay_s"]) for item in detection
                              if item["hop"] == hop and float(item["stable_correct_delay_s"]) >= 0]
                    row[f"mean_first_correct_{hop}hop_s"] = statistics.mean(first)
                    row[f"mean_stable_correct_{hop}hop_s"] = statistics.mean(stable)
                    row[f"censored_{hop}hop_observers"] = sum(
                        1 for item in detection
                        if item["hop"] == hop and float(item["stable_correct_delay_s"]) < 0)
                rows.append(row)
    with (out / "encoding-per-run.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    metrics = ("topology_airtime_us", "topology_tx_bytes",
               "control_window_attempt_occupancy", "control_rx", "final_one_hop_f1",
               "final_two_hop_f1", "final_exact_both_ratio",
               "offered_completion_ratio", "mac_pdr", "final_high_mac_pending",
               "mean_post_cold_exact_both_ratio", "mean_first_correct_1hop_s",
               "mean_first_correct_2hop_s", "mean_stable_correct_1hop_s",
               "mean_stable_correct_2hop_s", "censored_1hop_observers",
               "censored_2hop_observers")
    aggregate = []
    for address in ("standard", "short"):
        for encoding in ("list", "bitmap", "adaptive"):
            group = [row for row in rows if row["beacon_address"] == address
                     and row["neighbor_encoding"] == encoding]
            result = {"beacon_address": address, "neighbor_encoding": encoding,
                      "runs": len(group)}
            for metric in metrics:
                values = [float(row[metric]) for row in group]
                result[f"mean_{metric}"] = statistics.mean(values)
                result[f"ci95_{metric}"] = (1.96 * statistics.stdev(values) /
                                               math.sqrt(len(values))) if len(values) > 1 else 0.0
            aggregate.append(result)
    with (out / "encoding-aggregate.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(aggregate[0]))
        writer.writeheader()
        writer.writerows(aggregate)


if __name__ == "__main__":
    main()
