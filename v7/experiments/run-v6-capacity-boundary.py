#!/usr/bin/env python3
"""V6: refine the previously bracketed K=8..12 Beacon capacity boundary."""

from __future__ import annotations

import argparse
import csv
import math
import statistics
import subprocess
from collections import defaultdict
from pathlib import Path


def read_last_row(path: Path) -> dict[str, str]:
    with path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise RuntimeError(f"no data rows in {path}")
    return rows[-1]


def average(values: list[float]) -> float:
    return statistics.fmean(values) if values else 0.0


def find_ns3_root(start: Path) -> Path:
    for candidate in (start, *start.parents):
        if (candidate / "ns3").exists() and (candidate / "build").exists():
            return candidate
    raise RuntimeError("cannot locate ns-3 root above this script")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--duration", type=float, default=8.0)
    parser.add_argument("--seeds", default="71,72,73,74,75")
    parser.add_argument("--contenders", default="8,9,10,11,12")
    parser.add_argument("--densities", default="sparse")
    parser.add_argument("--output", default="self-organizing-mac-v6/results/capacity-boundary-v6")
    args = parser.parse_args()

    root = find_ns3_root(Path(__file__).resolve())
    binary = root / "build/scratch/self-organizing-mac-v6/ns3.47-self-organizing-64node-v6-default"
    output_root = root / args.output
    output_root.mkdir(parents=True, exist_ok=True)
    contenders_set = [int(value) for value in args.contenders.split(",")]
    seeds = [int(value) for value in args.seeds.split(",")]
    densities = [value.strip() for value in args.densities.split(",")]

    per_run: list[dict[str, object]] = []
    for density in densities:
        for address in ("standard", "short"):
            for contenders in contenders_set:
                for seed in seeds:
                    case = output_root / f"{density}-{address}-k{contenders}-seed{seed}"
                    command = [
                        str(binary),
                        f"--duration={args.duration}",
                        "--controlFrame=beacon",
                        f"--beaconAddress={address}",
                        f"--controlContenders={contenders}",
                        "--neighborEncoding=adaptive",
                        f"--density={density}",
                        "--topologyScenario=static",
                        "--perNodeMbps=0",
                        f"--run={seed}",
                        f"--output={case.relative_to(root)}",
                    ]
                    print(f"[RUN] density={density} address={address} K={contenders} seed={seed}",
                          flush=True)
                    subprocess.run(command, cwd=root, check=True)
                    summary = read_last_row(case / "summary.csv")
                    with (case / "control-window-capacity.csv").open(
                        newline="", encoding="utf-8"
                    ) as handle:
                        windows = list(csv.DictReader(handle))
                    rx = [float(row["receiver_delivery_ratio"]) for row in windows]
                    tx = [float(row["sender_completion_ratio"]) for row in windows]
                    per_run.append(
                        {
                            "density": density,
                            "beacon_address": address,
                            "control_contenders": contenders,
                            "seed": seed,
                            "rotation_ms": summary["beacon_rotation_ms"],
                            "windows": len(windows),
                            "mean_sender_completion": average(tx),
                            "min_sender_completion": min(tx),
                            "mean_receiver_delivery": average(rx),
                            "min_receiver_delivery": min(rx),
                            "final_one_hop_f1": float(summary["final_one_hop_f1"]),
                            "final_two_hop_f1": float(summary["final_two_hop_f1"]),
                            "final_exact_both_ratio": float(summary["final_exact_both_ratio"]),
                            "topology_airtime_us": float(summary["topology_airtime_us"]),
                            "control_window_attempt_occupancy": float(
                                summary["control_window_attempt_occupancy"]
                            ),
                        }
                    )

    with (output_root / "capacity-per-run.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(per_run[0]))
        writer.writeheader()
        writer.writerows(per_run)

    grouped: dict[tuple[str, str, int], list[dict[str, object]]] = defaultdict(list)
    for row in per_run:
        key = (str(row["density"]), str(row["beacon_address"]),
               int(row["control_contenders"]))
        grouped[key].append(row)

    aggregate: list[dict[str, object]] = []
    for (density, address, contenders), rows in sorted(grouped.items()):
        rx_values = [float(row["mean_receiver_delivery"]) for row in rows]
        one_values = [float(row["final_one_hop_f1"]) for row in rows]
        two_values = [float(row["final_two_hop_f1"]) for row in rows]
        exact_values = [float(row["final_exact_both_ratio"]) for row in rows]
        ci95 = (1.96 * statistics.stdev(rx_values) / math.sqrt(len(rx_values))
                if len(rx_values) > 1 else 0.0)
        safe = (min(float(row["min_sender_completion"]) for row in rows) >= 0.999 and
                min(rx_values) >= 0.99 and min(one_values) >= 0.99 and
                min(two_values) >= 0.99 and min(exact_values) >= 0.99)
        aggregate.append(
            {
                "density": density,
                "beacon_address": address,
                "control_contenders": contenders,
                "runs": len(rows),
                "mean_receiver_delivery": average(rx_values),
                "receiver_delivery_ci95": ci95,
                "worst_run_receiver_delivery": min(rx_values),
                "mean_final_one_hop_f1": average(one_values),
                "worst_final_one_hop_f1": min(one_values),
                "mean_final_two_hop_f1": average(two_values),
                "worst_final_two_hop_f1": min(two_values),
                "mean_final_exact_both_ratio": average(exact_values),
                "worst_final_exact_both_ratio": min(exact_values),
                "safe_under_v6_rule": int(safe),
            }
        )

    with (output_root / "capacity-aggregate.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(aggregate[0]))
        writer.writeheader()
        writer.writerows(aggregate)
    print(f"[DONE] {output_root / 'capacity-aggregate.csv'}")


if __name__ == "__main__":
    main()
