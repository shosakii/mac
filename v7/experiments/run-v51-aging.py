#!/usr/bin/env python3
"""Neighbor aging trade-off: removal delay versus false deletion under loss."""

import argparse
import csv
import math
import statistics
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seeds", default="51,52,53")
    parser.add_argument("--remove-misses", default="1,2,3")
    parser.add_argument("--losses", default="0,0.05,0.10,0.20")
    parser.add_argument("--duration", type=float, default=35.0)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--output", default="self-organizing-mac-v6/results/aging-v51")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    binary = root / "build/scratch/self-organizing-mac-v6/ns3.47-self-organizing-64node-v6-default"
    out = root / args.output
    out.mkdir(parents=True, exist_ok=True)
    seeds = [int(value) for value in args.seeds.split(",")]
    remove_misses = [int(value) for value in args.remove_misses.split(",")]
    losses = [float(value) for value in args.losses.split(",")]

    def run_case(parameters):
        seed, misses, loss = parameters
        case = out / f"miss{misses}-loss{loss:.2f}-seed{seed}"
        command = [str(binary), f"--duration={args.duration}", "--controlFrame=beacon",
                           "--beaconAddress=standard", "--controlContenders=8",
                           "--neighborEncoding=adaptive", "--density=sparse",
                           "--topologyScenario=scripted", "--trafficPattern=burst",
                           "--dataQueuePolicy=defer", "--perNodeMbps=0.05",
                           f"--neighborRemoveMisses={misses}",
                           f"--neighborExpireMisses={misses + 1}",
                           f"--controlLossRate={loss}", f"--run={seed}",
                           f"--output={case.relative_to(root)}"]
        print(f"[RUN] seed={seed} remove={misses} loss={loss}", flush=True)
        subprocess.run(command, cwd=root, check=True)
        with (case / "summary.csv").open(newline="", encoding="utf-8") as handle:
            row = next(csv.DictReader(handle))
        with (case / "node-lifetime-summary.csv").open(newline="", encoding="utf-8") as handle:
            lifetime = list(csv.DictReader(handle))
        with (case / "topology-timeseries.csv").open(newline="", encoding="utf-8") as handle:
            timeline = [sample for sample in csv.DictReader(handle)
                        if float(sample["time_s"]) >= 3.0]
        with (case / "event-detection.csv").open(newline="", encoding="utf-8") as handle:
            detection = list(csv.DictReader(handle))
        row["mean_post_cold_exact_both_ratio"] = statistics.mean(
            float(item["post_cold_exact_both_ratio"]) for item in lifetime)
        for field in ("one_hop_false_positive", "one_hop_false_negative",
                      "two_hop_false_positive", "two_hop_false_negative"):
            row[f"time_mean_{field}"] = statistics.mean(float(item[field])
                                                          for item in timeline)
        for event_type in ("NODE_FAIL", "NODE_RECOVER"):
            for hop in ("1", "2"):
                values = [float(item["stable_correct_delay_s"]) for item in detection
                          if item["event_type"] == event_type and item["hop"] == hop
                          and float(item["stable_correct_delay_s"]) >= 0]
                row[f"mean_{event_type.lower()}_{hop}hop_stable_delay_s"] = (
                    statistics.mean(values) if values else -1.0)
        return row

    cases = [(seed, misses, loss) for seed in seeds for misses in remove_misses
             for loss in losses]
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as executor:
        rows = list(executor.map(run_case, cases))
    with (out / "aging-per-run.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    metrics = ("final_one_hop_f1", "final_two_hop_f1", "final_exact_both_ratio",
               "control_rx", "offered_completion_ratio", "mac_pdr",
               "mean_post_cold_exact_both_ratio",
               "time_mean_one_hop_false_positive", "time_mean_one_hop_false_negative",
               "time_mean_two_hop_false_positive", "time_mean_two_hop_false_negative",
               "mean_node_fail_1hop_stable_delay_s",
               "mean_node_fail_2hop_stable_delay_s",
               "mean_node_recover_1hop_stable_delay_s",
               "mean_node_recover_2hop_stable_delay_s")
    aggregate = []
    for misses in remove_misses:
        for loss in losses:
            group = [row for row in rows if int(row["neighbor_remove_misses"]) == misses
                     and abs(float(row["control_loss_rate"]) - loss) < 1e-12]
            result = {"neighbor_remove_misses": misses,
                      "neighbor_expire_misses": misses + 1,
                      "control_loss_rate": loss, "runs": len(group)}
            for metric in metrics:
                values = [float(row[metric]) for row in group]
                result[f"mean_{metric}"] = statistics.mean(values)
                result[f"ci95_{metric}"] = (1.96 * statistics.stdev(values) /
                                               math.sqrt(len(values))) if len(values) > 1 else 0.0
            aggregate.append(result)
    with (out / "aging-aggregate.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(aggregate[0]))
        writer.writeheader()
        writer.writerows(aggregate)


if __name__ == "__main__":
    main()
