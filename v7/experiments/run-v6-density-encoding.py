#!/usr/bin/env python3
"""Compare list/bitmap/adaptive under sparse, medium and dense topologies."""

import argparse
import csv
import math
import statistics
import subprocess
from pathlib import Path


def find_ns3_root(start: Path) -> Path:
    for candidate in (start, *start.parents):
        if (candidate / "ns3").exists() and (candidate / "build").exists():
            return candidate
    raise RuntimeError("cannot locate ns-3 root above this script")


def read_one(path):
    with path.open(newline="", encoding="utf-8") as handle:
        return next(csv.DictReader(handle))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seeds", default="81,82,83")
    parser.add_argument("--densities", default="sparse,medium,dense")
    parser.add_argument("--addresses", default="standard")
    parser.add_argument("--duration", type=float, default=35.0)
    parser.add_argument("--output", default="self-organizing-mac-v6/results/density-encoding-v6")
    parser.add_argument("--aggregate-only", action="store_true")
    args = parser.parse_args()
    root = find_ns3_root(Path(__file__).resolve())
    binary = root / "build/scratch/self-organizing-mac-v6/ns3.47-self-organizing-64node-v6-default"
    output = root / args.output
    output.mkdir(parents=True, exist_ok=True)
    seeds = [int(x) for x in args.seeds.split(",")]
    densities = [x.strip() for x in args.densities.split(",")]
    addresses = [x.strip() for x in args.addresses.split(",")]

    rows = []
    for density in densities:
        for address in addresses:
            for encoding in ("list", "bitmap", "adaptive"):
                for seed in seeds:
                    case = output / f"{density}-{address}-{encoding}-seed{seed}"
                    if not args.aggregate_only:
                        command = [str(binary), f"--duration={args.duration}",
                                   "--controlFrame=beacon", f"--beaconAddress={address}",
                                   "--controlContenders=8", f"--neighborEncoding={encoding}",
                                   f"--density={density}", "--topologyScenario=random",
                                   "--trafficPattern=burst", "--dataQueuePolicy=defer",
                                   "--perNodeMbps=0.25", f"--run={seed}",
                                   f"--output={case.relative_to(root)}"]
                        print(f"[RUN] {density} {address} {encoding} seed={seed}", flush=True)
                        subprocess.run(command, cwd=root, check=True)
                    summary = read_one(case / "summary.csv")
                    lifetime = list(csv.DictReader(
                        (case / "node-lifetime-summary.csv").open(newline="", encoding="utf-8")))
                    rows.append({
                        "density": density,
                        "beacon_address": address,
                        "neighbor_encoding": encoding,
                        "seed": seed,
                        "topology_tx_bytes": summary["topology_tx_bytes"],
                        "topology_airtime_us": summary["topology_airtime_us"],
                        "control_window_attempt_occupancy": summary["control_window_attempt_occupancy"],
                        "final_one_hop_f1": summary["final_one_hop_f1"],
                        "final_two_hop_f1": summary["final_two_hop_f1"],
                        "final_exact_both_ratio": summary["final_exact_both_ratio"],
                        "mean_post_cold_exact_both_ratio": statistics.mean(
                            float(x["post_cold_exact_both_ratio"]) for x in lifetime),
                        "offered_completion_ratio": summary["offered_completion_ratio"],
                        "mac_pdr": summary["mac_pdr"],
                    })

    with (output / "density-encoding-per-run.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    aggregate = []
    metrics = list(rows[0])[4:]
    for density in densities:
        for address in addresses:
            for encoding in ("list", "bitmap", "adaptive"):
                group = [row for row in rows if row["density"] == density and
                         row["beacon_address"] == address and
                         row["neighbor_encoding"] == encoding]
                result = {"density": density, "beacon_address": address,
                          "neighbor_encoding": encoding, "runs": len(group)}
                for metric in metrics:
                    values = [float(row[metric]) for row in group]
                    result[f"mean_{metric}"] = statistics.mean(values)
                    result[f"ci95_{metric}"] = (1.96 * statistics.stdev(values) /
                                                 math.sqrt(len(values)) if len(values) > 1 else 0.0)
                aggregate.append(result)
    with (output / "density-encoding-aggregate.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(aggregate[0]))
        writer.writeheader()
        writer.writerows(aggregate)
    print(f"[DONE] {output}")


if __name__ == "__main__":
    main()
